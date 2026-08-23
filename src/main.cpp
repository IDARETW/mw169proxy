#include "config.h"
#include "auth.h"
#include "content_state.h"
#include "crash_diagnostics.h"
#include "dvars.h"
#include "ddl_state.h"
#include "fence.h"
#include "hook.h"
#include "log.h"
#include "lua_dump.h"
#include "lua_errors.h"
#include "lua_menu.h"
#include "menu_ownership.h"
#include "network_guard.h"
#include "online_fences.h"
#include "patch_fence.h"
#include "profile_identity.h"
#include "protected_build.h"
#include "proxy.h"
#include "startup_marker.h"
#include "trigger.h"

#include "../thirdparty/cwhook/src/entry.h"

#include <windows.h>

#include <cstdint>
#include <cwchar>

namespace
{
    HMODULE proxy_module = nullptr;
    bool network_backup_installed = false;

    DWORD WINAPI initialize(void*);

    using create_thread_function = HANDLE(WINAPI*)(
        LPSECURITY_ATTRIBUTES,
        SIZE_T,
        LPTHREAD_START_ROUTINE,
        LPVOID,
        DWORD,
        LPDWORD);

    HANDLE start_initialize_thread()
    {
        // CWHook redirects kernelbase!CreateThread. Resolve the kernel32
        // entry at runtime, as mw164proxy does, so the worker starts without
        // entering CWHook's hardware-breakpoint thread wrapper.
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!kernel32) return nullptr;

        auto create_thread = reinterpret_cast<create_thread_function>(
            GetProcAddress(kernel32, "CreateThread"));
        if (!create_thread) return nullptr;

        return create_thread(nullptr, 0, &initialize, nullptr, 0, nullptr);
    }

    bool is_target_game()
    {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        return _wcsicmp(name, L"ModernWarfare.exe") == 0;
    }

    [[noreturn]] void stop_process_for_network_safety()
    {
        OutputDebugStringA(
            "mw169proxy: The network backup could not return to a safe state. "
            "The process will stop.\r\n");
        RaiseFailFastException(nullptr, nullptr, 0);
    }

    DWORD WINAPI initialize(void*)
    {
        logging::init(proxy_module, config::show_console, config::trace_dvars);
        LOG_INFO("Core", "mw169proxy started");
        LOG_INFO("Core", "Protection probe: %08X", mw169_build_protection_probe());

        if (config::install_cwhook)
        {
            // Do this after logging starts and outside loader lock. The 1.69
            // process exits before the splash when the full CWHook base runs
            // from DLL_PROCESS_ATTACH. InstallHealers still waits for the
            // decrypted LoadImageA edge.
            arxan::Install();
            LOG_INFO(
                "Arxan",
                "CWHook base status after startup worker install: %s",
                arxan::IsInstalled() ? "armed" : "not armed");
        }

        if (is_target_game())
            startup_marker::remove_game_marker();

        if (config::crash_diagnostics)
        {
            crash_diagnostics::install(proxy_module);
            LOG_INFO("Crash", "Exception diagnostics armed");
        }

        dvars::init();
        auth::init();
        content_state::init();
        ddl_state::init();
        fence::init();
        menu_ownership::init();
        online_fences::init();
        patch_fence::init();
        profile_identity::init();
        lua_dump::init();
        lua_errors::init();
        lua_menu::init();

        const auto game_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!game_base)
        {
            LOG_ERROR("Core", "The game module is absent");
            return 0;
        }

        if (network_backup_installed)
            LOG_INFO("Network", "The main-executable network backup is active");
        else if (config::block_public_network_as_backup && !is_target_game())
            LOG_INFO("Network", "The network backup was skipped outside the game process");
        else
            LOG_INFO("Network", "The public network backup is disabled");

        if (config::install_load_image_trigger)
        {
            if (!trigger::install(game_base, &dvars::install, false))
                LOG_ERROR("Core", "The startup trigger did not install");
        }
        else
        {
            LOG_INFO("Trigger", "LoadImageA and game hooks are disabled for startup isolation");
        }

        if (config::preload_original_discord)
            proxy::preload();
        else
            LOG_INFO("Proxy", "Original Discord DLL load is deferred until an export is used");

        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        proxy_module = instance;
        proxy::set_module(instance);

        if (config::minimal_proxy)
            return TRUE;
        if (config::proxy_only)
            return TRUE;

        if (config::block_public_network_as_backup && is_target_game())
        {
            const auto game_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!game_base) return FALSE;
            if (!network_guard::install(game_base))
            {
                if (!network_guard::uninstall()) stop_process_for_network_safety();
                return FALSE;
            }
            network_backup_installed = true;
        }

        if (is_target_game())
            startup_marker::remove_game_marker_early();

        HANDLE thread = start_initialize_thread();
        if (!thread)
        {
            if (network_backup_installed && !network_guard::uninstall())
                stop_process_for_network_safety();
            network_backup_installed = false;
            return FALSE;
        }
        CloseHandle(thread);
    }
    return TRUE;
}

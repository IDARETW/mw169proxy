#include "config.h"
#include "dvars.h"
#include "hook.h"
#include "log.h"
#include "network_guard.h"
#include "protected_build.h"
#include "proxy.h"
#include "trigger.h"

// arxan
#include "../thirdparty/cwhook/src/entry.h"

#include <windows.h>

#include <cstdint>
#include <cwchar>

namespace
{
    HMODULE proxy_module = nullptr;
    bool network_backup_installed = false;

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

        dvars::init();

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

        if (!trigger::install(game_base, &dvars::install))
            LOG_ERROR("Core", "The startup trigger did not install");

        proxy::preload();

        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        proxy_module = instance;
        arxan::Install();
        proxy::set_module(instance);

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

        HANDLE thread = CreateThread(nullptr, 0, &initialize, nullptr, 0, nullptr);
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

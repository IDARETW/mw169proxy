#include "content_state.h"

#include "config.h"
#include "ddl_state.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "safe_mem.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace content_state
{
    namespace
    {
        constexpr int multiplayer_mode = 2;
        constexpr int installed_state = 3;

        std::atomic<game::ContentProgress> original_content_progress = nullptr;
        std::atomic<game::ModeInstalled> original_mode_installed = nullptr;
        std::atomic<game::LuaToInteger> lua_to_integer = nullptr;
        std::atomic<game::LuaToBoolean> lua_to_boolean = nullptr;
        std::atomic<bool> content_progress_installed = false;
        std::atomic<bool> mode_installed = false;
        std::atomic<bool> local_files_checked = false;
        std::atomic<bool> local_files_present = false;
        std::atomic<int> last_content_state = -1;
        std::atomic<int> last_mode_state = -1;
        std::mutex install_mutex;

        bool prologue_matches(
            const void* target,
            const std::uint8_t* expected,
            std::size_t size)
        {
            std::uint8_t current[32]{};
            if (size > sizeof(current)) return false;
            return safe_mem::read_bytes(target, current, size) &&
                   std::memcmp(current, expected, size) == 0;
        }

        bool local_content_enumeration_finished(std::uintptr_t game_base)
        {
            std::uint8_t finished = 0;
            return safe_mem::read(
                       reinterpret_cast<const void*>(
                           game_base + game::content_enumeration_finished_rva),
                       finished) &&
                   finished != 0;
        }

        bool local_mp_files_present()
        {
            bool expected = false;
            if (!local_files_checked.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel))
            {
                return local_files_present.load(std::memory_order_acquire);
            }

            wchar_t module_path[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(
                nullptr,
                module_path,
                static_cast<DWORD>(sizeof(module_path) / sizeof(module_path[0])));
            if (length == 0 ||
                length >= sizeof(module_path) / sizeof(module_path[0]))
            {
                LOG_ERROR("Content", "Could not resolve the game directory");
                return false;
            }

            std::wstring root(module_path, length);
            const auto separator = root.find_last_of(L"\\/");
            if (separator == std::wstring::npos)
            {
                LOG_ERROR("Content", "The game module path has no directory");
                return false;
            }
            root.resize(separator);

            constexpr std::array<const wchar_t*, 3> required_files = {
                L"main\\techsets_common_base_mp.psob",
                L"main\\techsets_global_core_mp.psob",
                L"main\\techsets_global_mp.psob"
            };

            for (const auto* relative : required_files)
            {
                const std::wstring path = root + L"\\" + relative;
                const DWORD attributes = GetFileAttributesW(path.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES ||
                    (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    LOG_INFO("Content", "Local MP file is absent: %ls", relative);
                    return false;
                }
            }

            local_files_present.store(true, std::memory_order_release);
            LOG_INFO("Content", "Observed local MP base files are present");
            return true;
        }

        bool local_mp_state_ready(std::uintptr_t game_base)
        {
            return local_content_enumeration_finished(game_base) &&
                   local_mp_files_present();
        }

        void push_integer(void* lua_state, int value)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(
                GetModuleHandleW(nullptr));
            if (!base) return;

            const auto push = reinterpret_cast<game::LuaPushInteger>(
                base + game::lua_push_integer_rva);
            push(lua_state, value);
        }

        void push_boolean(void* lua_state, int value)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(
                GetModuleHandleW(nullptr));
            if (!base) return;

            const auto push = reinterpret_cast<game::LuaPushBoolean>(
                base + game::lua_push_boolean_rva);
            push(lua_state, value);
        }

        int read_mode(void* lua_state)
        {
            const auto read = lua_to_integer.load(std::memory_order_acquire);
            return read ? read(lua_state, 1) : -1;
        }

        int __fastcall content_progress_detour(void* lua_state)
        {
            const auto original = original_content_progress.load(
                std::memory_order_acquire);
            if (!original) return 0;

            const int result_count = original(lua_state);
            const int mode = read_mode(lua_state);
            const auto read_integer = lua_to_integer.load(
                std::memory_order_acquire);
            int state = -1;
            if (read_integer && result_count > 0 && result_count <= 8)
                state = read_integer(lua_state, -result_count);

            if (last_content_state.exchange(state, std::memory_order_acq_rel) != state)
                LOG_INFO(
                    "Content",
                    "Engine.ECHHDAIBD mode=%d state=%d results=%d",
                    mode,
                    state,
                    result_count);

            ddl_state::main_thread_kick(
                reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),
                mode == multiplayer_mode);

            if (config::force_local_mp_content_state &&
                mode == multiplayer_mode &&
                state != installed_state &&
                local_mp_state_ready(reinterpret_cast<std::uintptr_t>(
                    GetModuleHandleW(nullptr))))
            {
                LOG_INFO(
                    "Content",
                    "Engine.ECHHDAIBD MP state changed locally to INSTALLED");
                push_integer(lua_state, installed_state);
                return 1;
            }

            return result_count;
        }

        int __fastcall mode_installed_detour(void* lua_state)
        {
            const auto original = original_mode_installed.load(
                std::memory_order_acquire);
            if (!original) return 0;

            const int result_count = original(lua_state);
            const int mode = read_mode(lua_state);
            const auto read_boolean = lua_to_boolean.load(
                std::memory_order_acquire);
            const bool installed = read_boolean && result_count > 0 &&
                                   read_boolean(lua_state, -1) != 0;

            if (mode == multiplayer_mode &&
                last_mode_state.exchange(installed ? 1 : 0, std::memory_order_acq_rel) !=
                    (installed ? 1 : 0))
            {
                LOG_INFO(
                    "Content",
                    "Engine.DBEGJIECGB MP original installed=%d results=%d",
                    installed ? 1 : 0,
                    result_count);
            }

            ddl_state::main_thread_kick(
                reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),
                mode == multiplayer_mode);

            if (config::force_local_mp_content_state &&
                mode == multiplayer_mode && !installed &&
                local_mp_state_ready(reinterpret_cast<std::uintptr_t>(
                    GetModuleHandleW(nullptr))))
            {
                LOG_INFO(
                    "Content",
                    "Engine.DBEGJIECGB MP state changed locally to installed");
                push_boolean(lua_state, 1);
                return 1;
            }

            return result_count;
        }
    }

    void init()
    {
        LOG_INFO(
            "Content",
            "Retail local MP content-state hook is %s",
            config::hook_local_mp_content_state ? "enabled" : "disabled");
        LOG_INFO(
            "Content",
            "MP local state requires content enumeration and three retail MP base files");
    }

    bool install(std::uintptr_t game_base)
    {
        if (!game_base) return false;
        if (!config::hook_local_mp_content_state)
        {
            content_progress_installed.store(true, std::memory_order_release);
            mode_installed.store(true, std::memory_order_release);
            return true;
        }

        const bool ready = content_progress_installed.load(std::memory_order_acquire) &&
                           mode_installed.load(std::memory_order_acquire);
        if (ready) return true;

        std::lock_guard lock(install_mutex);
        auto* integer_target = reinterpret_cast<void*>(
            game_base + game::lua_to_integer_rva);
        auto* boolean_target = reinterpret_cast<void*>(
            game_base + game::lua_to_boolean_rva);
        if (!prologue_matches(
                integer_target,
                game::lua_to_integer_prologue,
                sizeof(game::lua_to_integer_prologue)) ||
            !prologue_matches(
                boolean_target,
                game::lua_to_boolean_prologue,
                sizeof(game::lua_to_boolean_prologue)))
        {
            LOG_TRACE("Content", "Lua conversion helpers are not ready");
            return false;
        }
        lua_to_integer.store(
            reinterpret_cast<game::LuaToInteger>(integer_target),
            std::memory_order_release);
        lua_to_boolean.store(
            reinterpret_cast<game::LuaToBoolean>(boolean_target),
            std::memory_order_release);

        if (!content_progress_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(
                game_base + game::content_progress_rva);
            if (!prologue_matches(
                    target,
                    game::content_progress_prologue,
                    sizeof(game::content_progress_prologue)))
            {
                LOG_TRACE("Content", "Content progress hook is waiting at %p", target);
                return false;
            }
            auto* trampoline = hook::install(
                target,
                reinterpret_cast<void*>(&content_progress_detour));
            if (!trampoline)
            {
                LOG_ERROR("Content", "Content progress hook failed at %p", target);
                return false;
            }
            original_content_progress.store(
                reinterpret_cast<game::ContentProgress>(trampoline),
                std::memory_order_release);
            content_progress_installed.store(true, std::memory_order_release);
            LOG_INFO("Content", "Engine.ECHHDAIBD hook installed at %p", target);
        }

        if (!mode_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(
                game_base + game::mode_installed_rva);
            if (!prologue_matches(
                    target,
                    game::mode_installed_prologue,
                    sizeof(game::mode_installed_prologue)))
            {
                LOG_TRACE("Content", "Mode installed hook is waiting at %p", target);
                return false;
            }
            auto* trampoline = hook::install(
                target,
                reinterpret_cast<void*>(&mode_installed_detour));
            if (!trampoline)
            {
                LOG_ERROR("Content", "Mode installed hook failed at %p", target);
                return false;
            }
            original_mode_installed.store(
                reinterpret_cast<game::ModeInstalled>(trampoline),
                std::memory_order_release);
            mode_installed.store(true, std::memory_order_release);
            LOG_INFO("Content", "Engine.DBEGJIECGB hook installed at %p", target);
        }

        return content_progress_installed.load(std::memory_order_acquire) &&
               mode_installed.load(std::memory_order_acquire);
    }
}

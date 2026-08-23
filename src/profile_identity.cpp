#include "profile_identity.h"

#include "config.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "safe_mem.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>

namespace profile_identity
{
    namespace
    {
        std::atomic<game::ProfileDataGetter> original_getter = nullptr;
        std::atomic<bool> installed = false;
        std::atomic<bool> account_seen_logged = false;
        std::atomic<bool> otp_seen_logged = false;
        std::mutex install_mutex;

        bool prologue_matches(const void* target)
        {
            std::uint8_t current[sizeof(game::profile_data_getter_prologue)]{};
            return safe_mem::read_bytes(
                       target,
                       current,
                       sizeof(current)) &&
                   std::memcmp(
                       current,
                       game::profile_data_getter_prologue,
                       sizeof(current)) == 0;
        }

        void push_boolean(void* lua_state, int value)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!base) return;

            const auto push = reinterpret_cast<game::LuaPushBoolean>(
                base + game::lua_push_boolean_rva);
            push(lua_state, value);
        }

        bool is_account_flag(const char* key, bool& otp)
        {
            if (!key) return false;
            if (std::strcmp(key, "hasEverSeen_CODAccount") == 0)
            {
                otp = false;
                return true;
            }
            if (std::strcmp(key, "hasEverSeen_CODAccountOTP") == 0)
            {
                otp = true;
                return true;
            }
            return false;
        }

        int __fastcall profile_data_getter_detour(void* lua_state)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const auto to_string = base
                ? reinterpret_cast<game::LuaToLString>(base + game::lua_tolstring_rva)
                : nullptr;

            bool otp = false;
            const char* key = to_string ? to_string(lua_state, 1, nullptr) : nullptr;
            if (is_account_flag(key, otp))
            {
                auto& logged = otp ? otp_seen_logged : account_seen_logged;
                if (!logged.exchange(true, std::memory_order_acq_rel))
                    LOG_INFO("Identity", "Profile flag %s reported true", key);
                push_boolean(lua_state, 1);
                return 1;
            }

            game::ProfileDataGetter original = nullptr;
            while (!(original = original_getter.load(std::memory_order_acquire)))
                YieldProcessor();
            return original(lua_state);
        }
    }

    void init()
    {
        LOG_INFO(
            "Identity",
            "Retail profile completion hook is %s",
            config::hook_profile_account_flags ? "enabled" : "disabled");
    }

    bool install(std::uintptr_t game_base)
    {
        if (installed.load(std::memory_order_acquire)) return true;
        if (!game_base) return false;

        if (!config::hook_profile_account_flags)
        {
            installed.store(true, std::memory_order_release);
            return true;
        }

        std::lock_guard lock(install_mutex);
        if (installed.load(std::memory_order_relaxed)) return true;

        auto* target = reinterpret_cast<void*>(game_base + game::profile_data_getter_rva);
        if (!prologue_matches(target))
        {
            LOG_TRACE("Identity", "Profile getter is not ready at %p", target);
            return false;
        }

        void* trampoline = hook::install(
            target,
            reinterpret_cast<void*>(&profile_data_getter_detour));
        if (!trampoline)
        {
            LOG_ERROR("Identity", "Profile getter hook failed at %p", target);
            return false;
        }

        original_getter.store(
            reinterpret_cast<game::ProfileDataGetter>(trampoline),
            std::memory_order_release);
        installed.store(true, std::memory_order_release);
        LOG_INFO("Identity", "Retail profile getter hooked at %p", target);
        return true;
    }
}

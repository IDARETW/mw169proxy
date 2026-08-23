#include "fence.h"

#include "auth.h"
#include "config.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "safe_mem.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace fence
{
    namespace
    {
        std::atomic<bool> installed = false;
        std::mutex install_mutex;

        bool prologue_matches(const void* target)
        {
            std::uint8_t current[sizeof(game::lui_is_user_signed_in_prologue)]{};
            return safe_mem::read_bytes(
                       target,
                       current,
                       sizeof(current)) &&
                   std::memcmp(
                       current,
                       game::lui_is_user_signed_in_prologue,
                       sizeof(current)) == 0;
        }

        std::int64_t __fastcall sign_in_fence_detour(void* lua_state)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (base)
            {
                // This local fence is a narrow retail consumer of sign-in state.
                // Initialize local state here instead of forcing the broad
                // Demonware sign-in getter to return true.
                auth::initialize_local_state(base, 0);
                const auto push_boolean = reinterpret_cast<game::LuaPushBoolean>(
                    base + game::lua_push_boolean_rva);
                push_boolean(lua_state, 1);
            }
            return 1;
        }
    }

    void init()
    {
        LOG_INFO(
            "Fence",
            config::hook_sign_in_fence
                ? "Retail sign-in fence hook is enabled"
                : "Retail sign-in fence hook is disabled");
    }

    bool install(std::uintptr_t game_base)
    {
        if (installed.load(std::memory_order_acquire)) return true;
        if (!game_base || !config::hook_sign_in_fence)
        {
            installed.store(true, std::memory_order_release);
            return true;
        }

        std::lock_guard lock(install_mutex);
        if (installed.load(std::memory_order_relaxed)) return true;

        auto* target = reinterpret_cast<void*>(game_base + game::lui_is_user_signed_in_rva);
        if (!prologue_matches(target))
        {
            LOG_TRACE("Fence", "LUI sign-in fence is not ready at %p", target);
            return false;
        }

        if (!hook::install(target, reinterpret_cast<void*>(&sign_in_fence_detour)))
        {
            LOG_ERROR("Fence", "LUI sign-in fence hook failed at %p", target);
            return false;
        }

        installed.store(true, std::memory_order_release);
        LOG_INFO("Fence", "LUI sign-in fence hooked at %p", target);
        return true;
    }
}

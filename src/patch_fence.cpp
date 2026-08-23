#include "patch_fence.h"

#include "config.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "safe_mem.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace patch_fence
{
    namespace
    {
        std::atomic<bool> query_installed = false;
        std::atomic<bool> state_installed = false;
        std::atomic<bool> query_seen = false;
        std::atomic<bool> state_seen = false;
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

        void push_success(void* lua_state)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!base) return;

            const auto push = reinterpret_cast<game::LuaPushInteger>(
                base + game::lua_push_integer_rva);
            push(lua_state, 1);
        }

        int __fastcall patch_query_start_detour(void*)
        {
            if (!query_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Patch", "Fences.BDCICIIGD was called; local query suppressed");
            return 0;
        }

        int __fastcall patch_state_detour(void* lua_state)
        {
            if (!state_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Patch", "Fences.IBEEFACJG was called; local success reported");
            push_success(lua_state);
            return 1;
        }
    }

    void init()
    {
        LOG_INFO(
            "Patch",
            "Retail patch-query fence hook is %s",
            config::hook_patch_query_fence ? "enabled" : "disabled");
        LOG_INFO(
            "Patch",
            "Retail patch-state fence hook is %s",
            config::hook_patch_state_fence ? "enabled" : "disabled");
    }

    bool install(std::uintptr_t game_base)
    {
        if (!game_base) return false;

        const bool query_ready =
            !config::hook_patch_query_fence ||
            query_installed.load(std::memory_order_acquire);
        const bool state_ready =
            !config::hook_patch_state_fence ||
            state_installed.load(std::memory_order_acquire);
        if (query_ready && state_ready) return true;

        std::lock_guard lock(install_mutex);

        if (config::hook_patch_query_fence &&
            !query_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(game_base + game::patch_query_fence_rva);
            if (!prologue_matches(
                    target,
                    game::patch_query_fence_prologue,
                    sizeof(game::patch_query_fence_prologue)))
            {
                LOG_TRACE("Patch", "Patch-query fence is waiting at %p", target);
            }
            else if (hook::install(target, reinterpret_cast<void*>(&patch_query_start_detour)))
            {
                query_installed.store(true, std::memory_order_release);
                LOG_INFO("Patch", "Fences.BDCICIIGD hook installed at %p", target);
            }
            else
            {
                LOG_ERROR("Patch", "Fences.BDCICIIGD hook failed at %p", target);
            }
        }

        if (config::hook_patch_state_fence &&
            !state_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(game_base + game::patch_state_fence_rva);
            if (!prologue_matches(
                    target,
                    game::patch_state_fence_prologue,
                    sizeof(game::patch_state_fence_prologue)))
            {
                LOG_TRACE("Patch", "Patch-state fence is waiting at %p", target);
            }
            else if (hook::install(target, reinterpret_cast<void*>(&patch_state_detour)))
            {
                state_installed.store(true, std::memory_order_release);
                LOG_INFO("Patch", "Fences.IBEEFACJG hook installed at %p", target);
            }
            else
            {
                LOG_ERROR("Patch", "Fences.IBEEFACJG hook failed at %p", target);
            }
        }

        return (!config::hook_patch_query_fence ||
                query_installed.load(std::memory_order_acquire)) &&
               (!config::hook_patch_state_fence ||
                state_installed.load(std::memory_order_acquire));
    }
}

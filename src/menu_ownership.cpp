#include "menu_ownership.h"

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

namespace menu_ownership
{
    namespace
    {
        std::atomic<bool> mode_installed = false;
        std::atomic<bool> premium_installed = false;
        std::atomic<bool> offline_installed = false;
        std::atomic<bool> mode_seen = false;
        std::atomic<bool> premium_seen = false;
        std::atomic<bool> offline_seen = false;
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

        void push_boolean(void* lua_state, int value)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!base) return;

            const auto push = reinterpret_cast<game::LuaPushBoolean>(
                base + game::lua_push_boolean_rva);
            push(lua_state, value);
        }

        int __fastcall mode_ownership_detour(void* lua_state)
        {
            // CEGDBDIIIE is the retail binding behind the Campaign,
            // Multiplayer, and Co-op blade conditions.
            if (!mode_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Menu", "CEGDBDIIIE was called");
            push_boolean(lua_state, 1);
            return 1;
        }

        int __fastcall premium_gate_detour(void* lua_state)
        {
            // JACCCCEDI reports whether a party member should be rejected
            // for not owning the full game. False keeps that menu gate open.
            if (!premium_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Menu", "JACCCCEDI was called");
            push_boolean(lua_state, 0);
            return 1;
        }

        int __fastcall offline_ownership_detour(void* lua_state)
        {
            // CFHBIHABCB is the retail binding used by MainMenuUtils.lua
            // for the offline full-game ownership blade gate.
            if (!offline_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Menu", "CFHBIHABCB was called");
            push_boolean(lua_state, 1);
            return 1;
        }
    }

    void init()
    {
        LOG_INFO(
            "Menu",
            "Retail LUI mode ownership hook is %s",
            config::hook_lui_mode_ownership ? "enabled" : "disabled");
        LOG_INFO(
            "Menu",
            "Retail LUI premium gate hook is %s",
            config::hook_lui_premium_gate ? "enabled" : "disabled");
        LOG_INFO(
            "Menu",
            "Retail LUI offline ownership hook is %s",
            config::hook_lui_offline_ownership ? "enabled" : "disabled");
    }

    bool install(std::uintptr_t game_base)
    {
        if (!game_base) return false;

        const bool mode_ready =
            !config::hook_lui_mode_ownership ||
            mode_installed.load(std::memory_order_acquire);
        const bool premium_ready =
            !config::hook_lui_premium_gate ||
            premium_installed.load(std::memory_order_acquire);
        const bool offline_ready =
            !config::hook_lui_offline_ownership ||
            offline_installed.load(std::memory_order_acquire);
        if (mode_ready && premium_ready && offline_ready) return true;

        std::lock_guard lock(install_mutex);

        if (config::hook_lui_mode_ownership &&
            !mode_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(game_base + game::lui_mode_ownership_rva);
            if (!prologue_matches(
                    target,
                    game::lui_mode_ownership_prologue,
                    sizeof(game::lui_mode_ownership_prologue)))
            {
                LOG_TRACE("Menu", "Mode ownership hook is waiting at %p", target);
            }
            else if (hook::install(target, reinterpret_cast<void*>(&mode_ownership_detour)))
            {
                mode_installed.store(true, std::memory_order_release);
                LOG_INFO("Menu", "CEGDBDIIIE mode gate hooked at %p", target);
            }
            else
            {
                LOG_ERROR("Menu", "CEGDBDIIIE mode gate hook failed at %p", target);
            }
        }

        if (config::hook_lui_premium_gate &&
            !premium_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(game_base + game::lui_premium_gate_rva);
            if (!prologue_matches(
                    target,
                    game::lui_premium_gate_prologue,
                    sizeof(game::lui_premium_gate_prologue)))
            {
                LOG_TRACE("Menu", "Premium gate hook is waiting at %p", target);
            }
            else if (hook::install(target, reinterpret_cast<void*>(&premium_gate_detour)))
            {
                premium_installed.store(true, std::memory_order_release);
                LOG_INFO("Menu", "JACCCCEDI premium gate hooked at %p", target);
            }
            else
            {
                LOG_ERROR("Menu", "JACCCCEDI premium gate hook failed at %p", target);
            }
        }

        if (config::hook_lui_offline_ownership &&
            !offline_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(game_base + game::lui_offline_ownership_rva);
            if (!prologue_matches(
                    target,
                    game::lui_offline_ownership_prologue,
                    sizeof(game::lui_offline_ownership_prologue)))
            {
                LOG_TRACE("Menu", "Offline ownership hook is waiting at %p", target);
            }
            else if (hook::install(target, reinterpret_cast<void*>(&offline_ownership_detour)))
            {
                offline_installed.store(true, std::memory_order_release);
                LOG_INFO("Menu", "CFHBIHABCB offline ownership hook hooked at %p", target);
            }
            else
            {
                LOG_ERROR("Menu", "CFHBIHABCB offline ownership hook failed at %p", target);
            }
        }

        return (!config::hook_lui_mode_ownership ||
                mode_installed.load(std::memory_order_acquire)) &&
               (!config::hook_lui_premium_gate ||
                premium_installed.load(std::memory_order_acquire)) &&
               (!config::hook_lui_offline_ownership ||
                offline_installed.load(std::memory_order_acquire));
    }
}

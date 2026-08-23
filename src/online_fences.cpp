#include "online_fences.h"

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

namespace online_fences
{
    namespace
    {
        std::atomic<bool> service_installed = false;
        std::atomic<bool> data_installed = false;
        std::atomic<bool> battle_net_connected_installed = false;
        std::atomic<bool> platform_services_state_installed = false;
        std::atomic<bool> local_exchange_state_installed = false;
        std::atomic<bool> service_seen = false;
        std::atomic<bool> data_seen = false;
        std::atomic<bool> battle_net_connected_seen = false;
        std::atomic<bool> platform_services_state_seen = false;
        std::atomic<bool> local_exchange_state_seen = false;
        std::atomic<int> local_exchange_state_last_value = -1;
        std::atomic<game::CommerceExchangeState> commerce_exchange_state_original = nullptr;
        std::atomic<game::LuaToInteger> lua_to_integer = nullptr;
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

        void push_integer(void* lua_state, int value)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!base) return;

            const auto push = reinterpret_cast<game::LuaPushInteger>(
                base + game::lua_push_integer_rva);
            push(lua_state, value);
        }

        void push_success(void* lua_state)
        {
            push_integer(lua_state, 3);
        }

        void push_boolean_success(void* lua_state)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!base) return;

            const auto push = reinterpret_cast<game::LuaPushBoolean>(
                base + game::lua_push_boolean_rva);
            push(lua_state, 1);
        }

        void push_platform_services_success(void* lua_state)
        {
            push_integer(lua_state, 1);
        }

        int __fastcall online_service_state_detour(void* lua_state)
        {
            if (!service_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Online", "Fences.CCFEHFAFH was called");
            push_success(lua_state);
            return 1;
        }

        int __fastcall online_data_state_detour(void* lua_state)
        {
            if (!data_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Online", "Fences.CIHFIEHIDE was called");
            push_success(lua_state);
            return 1;
        }

        int __fastcall battle_net_connected_detour(void* lua_state)
        {
            if (!battle_net_connected_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Online", "Fences.BDEBEIHECI was called");
            push_boolean_success(lua_state);
            return 1;
        }

        int __fastcall platform_services_state_detour(void* lua_state)
        {
            if (!platform_services_state_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Online", "Engine.BAAABFICDA was called; local pass reported");
            push_platform_services_success(lua_state);
            return 1;
        }

        int __fastcall local_exchange_state_detour(void* lua_state)
        {
            const auto original = commerce_exchange_state_original.load(
                std::memory_order_acquire);
            if (!original)
            {
                LOG_ERROR("Online", "Commerce.CIIJICHHF original is unavailable");
                push_integer(lua_state, 2);
                return 1;
            }

            const int original_result_count = original(lua_state);
            const auto read_integer = lua_to_integer.load(std::memory_order_acquire);
            if (original_result_count < 1 || !read_integer)
                return original_result_count;

            const int original_state = read_integer(lua_state, -1);
            const int previous_state = local_exchange_state_last_value.exchange(
                original_state,
                std::memory_order_acq_rel);
            if (previous_state != original_state)
                LOG_INFO(
                    "Online",
                    "Commerce.CIIJICHHF original state=%d",
                    original_state);

            if (original_state <= 1)
            {
                if (!local_exchange_state_seen.exchange(true, std::memory_order_acq_rel))
                    LOG_INFO(
                        "Online",
                        "Commerce.CIIJICHHF blocking state overridden with local GOOD");
                push_integer(lua_state, 2);
                return 1;
            }

            return original_result_count;
        }
    }

    void init()
    {
        LOG_INFO(
            "Online",
            "Retail online-service fence hook is %s",
            config::hook_online_service_fence ? "enabled" : "disabled");
        LOG_INFO(
            "Online",
            "Retail online-data fence hook is %s",
            config::hook_online_data_fence ? "enabled" : "disabled");
        LOG_INFO(
            "Online",
            "Retail Battle.net connection fence hook is %s",
            config::hook_battle_net_connected_fence ? "enabled" : "disabled");
        LOG_INFO(
            "Online",
            "Retail platform-services state hook is %s",
            config::hook_platform_services_state ? "enabled" : "disabled");
        LOG_INFO(
            "Online",
            "Retail local exchange-state hook is %s",
            config::hook_local_exchange_state ? "enabled" : "disabled");
    }

    bool install(std::uintptr_t game_base)
    {
        if (!game_base) return false;

        const bool service_ready =
            !config::hook_online_service_fence ||
            service_installed.load(std::memory_order_acquire);
        const bool data_ready =
            !config::hook_online_data_fence ||
            data_installed.load(std::memory_order_acquire);
        const bool battle_net_connected_ready =
            !config::hook_battle_net_connected_fence ||
            battle_net_connected_installed.load(std::memory_order_acquire);
        const bool platform_services_state_ready =
            !config::hook_platform_services_state ||
            platform_services_state_installed.load(std::memory_order_acquire);
        const bool local_exchange_state_ready =
            !config::hook_local_exchange_state ||
            local_exchange_state_installed.load(std::memory_order_acquire);
        if (service_ready && data_ready && battle_net_connected_ready &&
            platform_services_state_ready && local_exchange_state_ready)
            return true;

        std::lock_guard lock(install_mutex);

        if (config::hook_online_service_fence &&
            !service_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(game_base + game::online_service_fence_rva);
            if (!prologue_matches(
                    target,
                    game::online_service_fence_prologue,
                    sizeof(game::online_service_fence_prologue)))
            {
                LOG_TRACE("Online", "Online-service fence hook is waiting at %p", target);
            }
            else if (hook::install(target, reinterpret_cast<void*>(&online_service_state_detour)))
            {
                service_installed.store(true, std::memory_order_release);
                LOG_INFO("Online", "Fences.CCFEHFAFH hook installed at %p", target);
            }
            else
            {
                LOG_ERROR("Online", "Fences.CCFEHFAFH hook failed at %p", target);
            }
        }

        if (config::hook_online_data_fence &&
            !data_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(game_base + game::online_data_fence_rva);
            if (!prologue_matches(
                    target,
                    game::online_data_fence_prologue,
                    sizeof(game::online_data_fence_prologue)))
            {
                LOG_TRACE("Online", "Online-data fence hook is waiting at %p", target);
            }
            else if (hook::install(target, reinterpret_cast<void*>(&online_data_state_detour)))
            {
                data_installed.store(true, std::memory_order_release);
                LOG_INFO("Online", "Fences.CIHFIEHIDE hook installed at %p", target);
            }
            else
            {
                LOG_ERROR("Online", "Fences.CIHFIEHIDE hook failed at %p", target);
            }
        }

        if (config::hook_battle_net_connected_fence &&
            !battle_net_connected_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(
                game_base + game::battle_net_connected_fence_rva);
            if (!prologue_matches(
                    target,
                    game::battle_net_connected_fence_prologue,
                    sizeof(game::battle_net_connected_fence_prologue)))
            {
                LOG_TRACE(
                    "Online",
                    "Battle.net connection fence is waiting at %p",
                    target);
            }
            else if (hook::install(
                         target,
                         reinterpret_cast<void*>(&battle_net_connected_detour)))
            {
                battle_net_connected_installed.store(true, std::memory_order_release);
                LOG_INFO(
                    "Online",
                    "Fences.BDEBEIHECI hook installed at %p",
                    target);
            }
            else
            {
                LOG_ERROR(
                    "Online",
                    "Fences.BDEBEIHECI hook failed at %p",
                    target);
            }
        }

        if (config::hook_platform_services_state &&
            !platform_services_state_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(
                game_base + game::platform_services_state_rva);
            if (!prologue_matches(
                    target,
                    game::platform_services_state_prologue,
                    sizeof(game::platform_services_state_prologue)))
            {
                LOG_TRACE(
                    "Online",
                    "Platform-services state hook is waiting at %p",
                    target);
            }
            else if (hook::install(
                         target,
                         reinterpret_cast<void*>(&platform_services_state_detour)))
            {
                platform_services_state_installed.store(true, std::memory_order_release);
                LOG_INFO(
                    "Online",
                    "Engine.BAAABFICDA hook installed at %p",
                    target);
            }
            else
            {
                LOG_ERROR(
                    "Online",
                    "Engine.BAAABFICDA hook failed at %p",
                    target);
            }
        }

        if (config::hook_local_exchange_state &&
            !local_exchange_state_installed.load(std::memory_order_relaxed))
        {
            auto* target = reinterpret_cast<void*>(
                game_base + game::commerce_exchange_state_rva);
            if (!prologue_matches(
                    target,
                    game::commerce_exchange_state_prologue,
                    sizeof(game::commerce_exchange_state_prologue)))
            {
                LOG_TRACE(
                    "Online",
                    "Local exchange-state hook is waiting at %p",
                    target);
            }
            else
            {
                auto* integer_target = reinterpret_cast<void*>(
                    game_base + game::lua_to_integer_rva);
                if (!prologue_matches(
                        integer_target,
                        game::lua_to_integer_prologue,
                        sizeof(game::lua_to_integer_prologue)))
                {
                    LOG_ERROR(
                        "Online",
                        "Commerce.CIIJICHHF helper prologue differs at %p",
                        integer_target);
                }
                else
                {
                    lua_to_integer.store(
                        reinterpret_cast<game::LuaToInteger>(integer_target),
                        std::memory_order_release);
                    auto trampoline = hook::install(
                        target,
                        reinterpret_cast<void*>(&local_exchange_state_detour));
                    if (trampoline)
                    {
                        commerce_exchange_state_original.store(
                            reinterpret_cast<game::CommerceExchangeState>(trampoline),
                            std::memory_order_release);
                        local_exchange_state_installed.store(
                            true,
                            std::memory_order_release);
                        LOG_INFO(
                            "Online",
                            "Commerce.CIIJICHHF hook installed at %p",
                            target);
                    }
                    else
                    {
                        LOG_ERROR(
                            "Online",
                            "Commerce.CIIJICHHF hook failed at %p",
                            target);
                    }
                }
            }
        }

        return (!config::hook_online_service_fence ||
                service_installed.load(std::memory_order_acquire)) &&
               (!config::hook_online_data_fence ||
                data_installed.load(std::memory_order_acquire)) &&
               (!config::hook_battle_net_connected_fence ||
                battle_net_connected_installed.load(std::memory_order_acquire)) &&
               (!config::hook_platform_services_state ||
                platform_services_state_installed.load(std::memory_order_acquire)) &&
               (!config::hook_local_exchange_state ||
                local_exchange_state_installed.load(std::memory_order_acquire));
    }
}

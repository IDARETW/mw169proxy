#include "auth.h"

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

#if defined(MW169_PROTECT_BUILD) && defined(__clang__)
#define MW169_PROTECT_FUNCTION __attribute__((used))
#else
#define MW169_PROTECT_FUNCTION
#endif

namespace auth
{
    namespace
    {
        std::atomic<game::LiveIsUserSignedInToDemonware> original_signed_in = nullptr;
        std::atomic<bool> installed = false;
        std::atomic<bool> content_state_written = false;
        std::mutex install_mutex;

        MW169_PROTECT_FUNCTION bool prologue_matches(const void* target)
        {
            std::uint8_t current[sizeof(game::live_is_user_signed_in_to_demonware_prologue)]{};
            return safe_mem::read_bytes(
                       target,
                       current,
                       sizeof(current)) &&
                   std::memcmp(
                       current,
                       game::live_is_user_signed_in_to_demonware_prologue,
                       sizeof(current)) == 0;
        }

        MW169_PROTECT_FUNCTION bool writable_range(const void* address, std::size_t size)
        {
            if (!address || size == 0) return false;

            const auto start = reinterpret_cast<std::uintptr_t>(address);
            const auto end = start + size;
            if (end < start) return false;

            auto cursor = start;
            while (cursor < end)
            {
                MEMORY_BASIC_INFORMATION info{};
                if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)))
                    return false;
                if (info.State != MEM_COMMIT) return false;

                const auto protection = info.Protect & 0xFF;
                if (protection != PAGE_READWRITE &&
                    protection != PAGE_WRITECOPY &&
                    protection != PAGE_EXECUTE_READWRITE &&
                    protection != PAGE_EXECUTE_WRITECOPY)
                    return false;

                const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
                if (region_end <= cursor) return false;
                cursor = region_end < end ? region_end : end;
            }
            return true;
        }

        template <typename T>
        MW169_PROTECT_FUNCTION bool write_global(
            std::uintptr_t address,
            T value,
            const char* label)
        {
            auto* destination = reinterpret_cast<void*>(address);
            if (!writable_range(destination, sizeof(T)))
            {
                LOG_WARN("Auth", "Skipped %s: retail global is not writable", label);
                return false;
            }

            T previous{};
            if (!safe_mem::read(destination, previous))
            {
                LOG_WARN("Auth", "Skipped %s: retail global is not readable", label);
                return false;
            }

            std::memcpy(destination, &value, sizeof(value));
            LOG_INFO("Auth", "%s: 0x%X -> 0x%X", label,
                     static_cast<unsigned int>(previous),
                     static_cast<unsigned int>(value));
            return true;
        }

        MW169_PROTECT_FUNCTION void complete_local_auth(
            std::uintptr_t game_base,
            int controller_index)
        {
            bool expected = false;
            if (!content_state_written.compare_exchange_strong(expected, true)) return;

            // These are retail-owned state fields. The addresses and offsets come
            // from the 1.69 sign-in and Battle.net predicates, not from the dev
            // database. Keep the writes together so the retail readers see one
            // coherent local-auth state.
            const auto safe_controller = controller_index >= 0 && controller_index < 8
                                              ? static_cast<std::uintptr_t>(controller_index)
                                              : 0;
            const auto sign_in_record = game_base + game::sign_in_records_rva +
                                        safe_controller * game::sign_in_record_stride;
            const auto bnet_class = game_base + game::bnet_class_rva;

            const bool sign_in_state = write_global<std::uint32_t>(
                sign_in_record,
                2,
                "local sign-in state");
            const bool sign_in_ready = write_global<std::uint8_t>(
                game_base + game::sign_in_ready_rva,
                1,
                "sign-in record ready");
            const bool content_finished = write_global<std::uint8_t>(
                game_base + game::content_enumeration_finished_rva,
                1,
                "content enumeration");
            const bool bnet_state = write_global<std::uint32_t>(
                bnet_class,
                2,
                "Battle.net state");
            const bool bnet_finished = write_global<std::uint8_t>(
                bnet_class + game::bnet_finished_auth_offset,
                1,
                "Battle.net finished_auth");
            const bool bnet_var3 = write_global<std::uint32_t>(
                bnet_class + game::bnet_var3_offset,
                0x795230F0,
                "Battle.net auth result");
            const bool bnet_var4 = write_global<std::uint8_t>(
                bnet_class + game::bnet_var4_offset,
                0x1F,
                "Battle.net auth version");
            const bool bnet_var5 = write_global<std::uint32_t>(
                bnet_class + game::bnet_var5_offset,
                0,
                "Battle.net auth flags");

            if (sign_in_state && sign_in_ready && content_finished && bnet_state &&
                bnet_finished && bnet_var3 && bnet_var4 && bnet_var5)
                LOG_INFO("Auth", "Local Battle.net auth state completed");
            else
                LOG_WARN("Auth", "Local Battle.net auth state was only partially written");
        }

        MW169_PROTECT_FUNCTION bool __fastcall signed_in_detour(int controller_index)
        {
            (void)controller_index;
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (base) complete_local_auth(base, controller_index);
            return true;
        }
    }

    void initialize_local_state(std::uintptr_t game_base, int controller_index)
    {
        if (game_base) complete_local_auth(game_base, controller_index);
    }

    void init()
    {
        if (config::hook_signed_in_getter)
        {
            LOG_INFO("Auth", "1.69 sign-in getter hook is enabled");
            LOG_INFO("Auth", "The getter will return true after retail prologue validation");
        }
        else
        {
            LOG_INFO("Auth", "The broad sign-in getter hook is disabled for the retail boot A/B test");
        }
    }

    bool install(std::uintptr_t game_base)
    {
        if (installed.load(std::memory_order_acquire)) return true;
        if (!game_base) return false;

        if (!config::hook_signed_in_getter)
        {
            installed.store(true, std::memory_order_release);
            LOG_INFO("Auth", "Skipped Live_IsUserSignedInToDemonware hook");
            return true;
        }

        std::lock_guard lock(install_mutex);
        if (installed.load(std::memory_order_relaxed)) return true;

        auto* target = reinterpret_cast<void*>(game_base + game::live_is_user_signed_in_to_demonware_rva);
        if (!prologue_matches(target))
        {
            LOG_TRACE("Auth", "Live_IsUserSignedInToDemonware is not ready at %p", target);
            return false;
        }

        void* trampoline = hook::install(target, reinterpret_cast<void*>(&signed_in_detour));
        if (!trampoline)
        {
            LOG_ERROR("Auth", "Live_IsUserSignedInToDemonware hook failed at %p", target);
            return false;
        }

        original_signed_in.store(
            reinterpret_cast<game::LiveIsUserSignedInToDemonware>(trampoline),
            std::memory_order_release);
        installed.store(true, std::memory_order_release);
        LOG_INFO("Auth", "Live_IsUserSignedInToDemonware hooked at %p", target);
        return true;
    }
}

#include "ddl_state.h"

#include "config.h"
#include "game.h"
#include "log.h"
#include "safe_mem.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace ddl_state
{
    namespace
    {
        std::atomic<bool> installed = false;
        std::atomic<bool> stats_init_ready = false;
        std::atomic<bool> seam_logged = false;
        std::atomic<bool> build_logged = false;
        std::atomic<unsigned int> attempts = 0;
        std::atomic<std::uint64_t> last_attempt = 0;
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

        bool is_executable(const void* address)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!address || !VirtualQuery(address, &info, sizeof(info))) return false;
            if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) != 0)
                return false;

            switch (info.Protect & 0xFF)
            {
                case PAGE_EXECUTE:
                case PAGE_EXECUTE_READ:
                case PAGE_EXECUTE_READWRITE:
                case PAGE_EXECUTE_WRITECOPY:
                    return true;
                default:
                    return false;
            }
        }

        void log_bytes_once(std::uintptr_t game_base)
        {
            bool expected = false;
            if (!seam_logged.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel))
                return;

            const auto address = game_base + game::live_storage_stats_fetched_rva;
            std::uint8_t bytes[24]{};
            if (!safe_mem::read_bytes(
                    reinterpret_cast<const void*>(address),
                    bytes,
                    sizeof(bytes)))
            {
                LOG_WARN(
                    "DDL",
                    "Stats-fetched seam bytes could not be read at %p",
                    reinterpret_cast<void*>(address));
                return;
            }

            char text[24 * 3 + 1]{};
            std::size_t offset = 0;
            for (const auto byte : bytes)
            {
                const int written = _snprintf_s(
                    text + offset,
                    sizeof(text) - offset,
                    _TRUNCATE,
                    "%02X%s",
                    byte,
                    offset + 1 < sizeof(text) ? " " : "");
                if (written <= 0) break;
                offset += static_cast<std::size_t>(written);
            }

            LOG_INFO(
                "DDL",
                "Retail stats-fetched seam rva=0x%zX executable=%d bytes=%s",
                static_cast<std::size_t>(game::live_storage_stats_fetched_rva),
                is_executable(reinterpret_cast<void*>(address)) ? 1 : 0,
                text);
        }
    }

    void init()
    {
        LOG_INFO(
            "DDL",
            "Retail DDL context seed is %s",
            config::seed_local_ddl ? "enabled" : "disabled");
        LOG_INFO(
            "DDL",
            "StatsInit rva=0x%zX; stats-fetched seam rva=0x%zX",
            static_cast<std::size_t>(game::live_storage_stats_init_rva),
            static_cast<std::size_t>(game::live_storage_stats_fetched_rva));
    }

    bool install(std::uintptr_t game_base)
    {
        if (!game_base) return false;
        if (installed.load(std::memory_order_acquire)) return true;

        std::lock_guard lock(install_mutex);
        if (installed.load(std::memory_order_relaxed)) return true;

        auto* stats_init = reinterpret_cast<void*>(
            game_base + game::live_storage_stats_init_rva);
        log_bytes_once(game_base);
        if (!prologue_matches(
                stats_init,
                game::live_storage_stats_init_prologue,
                sizeof(game::live_storage_stats_init_prologue)))
        {
            LOG_TRACE("DDL", "StatsInit prologue is not ready at %p", stats_init);
            if (!config::seed_local_ddl)
            {
                installed.store(true, std::memory_order_release);
                return true;
            }
            return false;
        }

        stats_init_ready.store(true, std::memory_order_release);
        installed.store(true, std::memory_order_release);
        LOG_INFO(
            "DDL",
            "Retail StatsInit prologue verified at %p rva 0x%zX",
            stats_init,
            static_cast<std::size_t>(game::live_storage_stats_init_rva));
        return true;
    }

    void main_thread_kick(std::uintptr_t game_base, bool multiplayer_query)
    {
        if (!config::seed_local_ddl || !multiplayer_query ||
            !stats_init_ready.load(std::memory_order_acquire) ||
            build_logged.load(std::memory_order_acquire))
            return;

        const auto now = GetTickCount64();
        auto previous = last_attempt.load(std::memory_order_relaxed);
        if (now - previous < 200) return;
        if (!last_attempt.compare_exchange_strong(
                previous,
                now,
                std::memory_order_acq_rel))
            return;

        if (attempts.fetch_add(1, std::memory_order_relaxed) >= 50)
        {
            if (!build_logged.exchange(true, std::memory_order_acq_rel))
                LOG_WARN("DDL", "Retail DDL seed stopped after 50 attempts");
            return;
        }

        // The first pass only proves that the exact retail call seam is live.
        // The call sequence is enabled after the second seam has a verified
        // 1.69 prologue and its buffer gates are mapped.
        (void)game_base;
    }
}

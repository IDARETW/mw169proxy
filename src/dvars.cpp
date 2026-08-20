#include "dvars.h"

#include "config.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "safe_mem.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>

namespace dvars
{
    namespace
    {
        struct Override
        {
            const char* token;
            const char* label;
            bool value;
        };

        constexpr Override overrides[] = {
            {"MPSSOTQQPM", "force_offline_enabled", true},
            {"LSTQOKLTRN", "force_offline_menus",   true}
        };

        std::atomic<game::DvarRegisterBool> original_bool = nullptr;
        std::atomic<game::DvarRegisterVariant> original_variant = nullptr;
        std::atomic<bool> installed = false;
        std::mutex install_mutex;

        const Override* find_override(const char* name)
        {
            if (!name || !*name) return nullptr;
            for (const auto& entry : overrides)
            {
                if (std::strcmp(name, entry.token) == 0 || std::strcmp(name, entry.label) == 0)
                    return &entry;
            }
            return nullptr;
        }

        bool prologue_matches(const void* target, const std::uint8_t* expected, std::size_t size)
        {
            std::uint8_t current[32]{};
            if (size > sizeof(current)) return false;
            return safe_mem::read_bytes(target, current, size) &&
                   std::memcmp(current, expected, size) == 0;
        }

        void format_value(int type, void* value, char* output, std::size_t output_size)
        {
            if (!value)
            {
                strcpy_s(output, output_size, "?");
                return;
            }

            if (type == 0)
            {
                std::uint8_t boolean = 0;
                if (safe_mem::read(value, boolean))
                    strcpy_s(output, output_size, boolean ? "true" : "false");
                else
                    strcpy_s(output, output_size, "?");
                return;
            }

            if (type == 1)
            {
                float number = 0.0F;
                if (safe_mem::read(value, number))
                    _snprintf_s(output, output_size, _TRUNCATE, "%g", number);
                else
                    strcpy_s(output, output_size, "?");
                return;
            }

            std::uint32_t raw = 0;
            if (safe_mem::read(value, raw))
                _snprintf_s(output, output_size, _TRUNCATE, "0x%08X", raw);
            else
                strcpy_s(output, output_size, "?");
        }

        void* __fastcall register_bool_detour(
            const char* name,
            bool value,
            unsigned int flags,
            const char* description)
        {
            char safe_name[96]{};
            safe_mem::read_string(name, safe_name, sizeof(safe_name));

            const Override* entry = find_override(safe_name);
            const bool replacement = entry ? entry->value : value;
            game::DvarRegisterBool original = nullptr;
            while (!(original = original_bool.load(std::memory_order_acquire))) YieldProcessor();
            void* result = original(name, replacement, flags, description);

            if (entry)
            {
                LOG_INFO(
                    "Dvars",
                    "%s: default %s -> %s",
                    entry->label,
                    value ? "true" : "false",
                    replacement ? "true" : "false");
            }
            return result;
        }

        void* __fastcall register_variant_detour(
            const char* name,
            unsigned int checksum,
            int type,
            unsigned int flags,
            void* value,
            void* domain,
            const char* description)
        {
            char safe_name[96]{};
            char safe_value[96]{};
            safe_mem::read_string(name, safe_name, sizeof(safe_name));
            format_value(type, value, safe_value, sizeof(safe_value));

            LOG_TRACE(
                "Dvar",
                "name=%s checksum=0x%08X type=%d flags=0x%08X value=%s",
                safe_name[0] ? safe_name : "?",
                checksum,
                type,
                flags,
                safe_value);

            game::DvarRegisterVariant original = nullptr;
            while (!(original = original_variant.load(std::memory_order_acquire))) YieldProcessor();
            return original(name, checksum, type, flags, value, domain, description);
        }
    }

    void init()
    {
        LOG_INFO("Dvars", "Loaded %zu Bool overrides", std::size(overrides));
        LOG_INFO("Dvars", "The 1.69 offline registration candidates will be forced");
        LOG_INFO("Dvars", "systemlink and systemlink_host remain under game control");
    }

    bool install(std::uintptr_t game_base)
    {
        if (installed.load(std::memory_order_acquire)) return true;

        std::lock_guard lock(install_mutex);
        if (installed.load(std::memory_order_relaxed)) return true;

        auto* bool_target = reinterpret_cast<void*>(game_base + game::dvar_register_bool_rva);
        auto* variant_target = reinterpret_cast<void*>(game_base + game::dvar_register_variant_rva);

        if (config::hook_dvar_bool &&
            !original_bool.load(std::memory_order_acquire) &&
            !prologue_matches(
                bool_target,
                game::dvar_register_bool_prologue,
                sizeof(game::dvar_register_bool_prologue)))
            return false;

        if (config::hook_dvar_variant &&
            !original_variant.load(std::memory_order_acquire) &&
            !prologue_matches(
                variant_target,
                game::dvar_register_variant_prologue,
                sizeof(game::dvar_register_variant_prologue)))
            return false;

        if (config::hook_dvar_variant &&
            !original_variant.load(std::memory_order_acquire))
        {
            void* variant_trampoline = hook::install(
                variant_target,
                reinterpret_cast<void*>(&register_variant_detour));
            if (!variant_trampoline) return false;
            original_variant.store(
                reinterpret_cast<game::DvarRegisterVariant>(variant_trampoline),
                std::memory_order_release);
            LOG_INFO("Dvars", "Dvar_RegisterVariant hook installed at %p", variant_target);
        }

        if (config::hook_dvar_bool &&
            !original_bool.load(std::memory_order_acquire))
        {
            void* bool_trampoline = hook::install(
                bool_target,
                reinterpret_cast<void*>(&register_bool_detour));
            if (!bool_trampoline) return false;
            original_bool.store(
                reinterpret_cast<game::DvarRegisterBool>(bool_trampoline),
                std::memory_order_release);
            LOG_INFO("Dvars", "Dvar_RegisterBool hook installed at %p", bool_target);
        }

        installed.store(true, std::memory_order_release);
        return true;
    }
}

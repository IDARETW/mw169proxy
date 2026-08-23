#include "dvars.h"

#include "auth.h"
#include "config.h"
#include "content_state.h"
#include "ddl_state.h"
#include "fence.h"
#include "game.h"
#include "hook.h"
#include "lua_dump.h"
#include "lua_errors.h"
#include "lua_menu.h"
#include "log.h"
#include "menu_ownership.h"
#include "online_fences.h"
#include "patch_fence.h"
#include "profile_identity.h"
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

        struct VariantOverride
        {
            const char* token;
            const char* label;
            int type;
            std::uint32_t value;
        };

        constexpr Override overrides[] = {
            // Ported from the 1.64 offline registration table. These tokens were
            // confirmed as Boolean registrations in the live 1.69 trace.
            {"MLNMPQOON", "cg_viewedSplashScreen",                 true},
            {"MTSTMKPMRM", "ui_onlineRequired",                    false},
            {"LPNMMPKRL", "com_lan_lobby_enabled",                 true},
            {"RLSPOOTTT", "com_checkIfGameModeInstalled",         false},
            {"LSTQOKLTRN", "force_offline_menus",                  false},
            {"OKLQKPPKPQ", "con_bindableGrave",                    false},
            {"LKSQOLNKLP", "lui_skip_boot_flow",                   true},
            {"LMMRONPQMO", "lui_force_online_menus",               false},
            {"LNTOKPTKS", "lui_cod_points_enabled",                false},
            {"LSSRRSMNMR", "lui_dev_features_enabled",             true},
            {"LRKPTLNQTT", "lui_enable_magma_blade_layout",        false},
            {"LSPSKLPNQT", "lui_wz_tutorial_optional",              true},
            {"LTOQRQMMLQ", "online_lan_cross_play",                true},
            {"NTTRLOPQKS", "xp_dec_dc",                            false},
            {"NSPPTONLNP", "online_blueprints_enabled",            false},
            {"MPSSOTQQPM", "force_offline_enabled",                true},
            {"NOSONNPTLM", "online_auth_skip_auth",                 true},
            // The 1.69 MP blade uses this local feature flag before it opens
            // the Battle.net install-management popup. Community play does
            // not use the Battle.net desktop application.
            {"LQQNTKTLQK", "bnet_modify_install_enabled",             false},
            {"LQKTNLONLP", "mp_private_match_enabled",                 true},
            {"LOMSTMNPRR", "mp_trials_enabled",                        true},
            {"OLMKQPQOM", "online_anticheat_should_com_error_if_mp_or_cp_banned", false},
            {"LNSPMQMSS", "online_anticheat_should_main_menu_fence_fail_if_mp_banned", false},
            {"LKSTRMKTML", "should_check_dlc",                     false},
            {"OLKMKMTKRO", "unlockAllItems",                       true},
            {"MNLPOPMMSK", "force_unlock_all_attachments",         true},
            {"LSPQSSPSOL", "force_unlock_all_attachment_lines",    true},
            {"NQRLNKMTSL", "force_unlock_all_killstreaks",         true}
        };

        // Retail registers r_fullscreen as an enum (type 8), not as a Bool.
        // The value points to a four-byte enum value during registration.
        constexpr VariantOverride variant_overrides[] = {
            {"NNSQSMTQPP", "r_fullscreen", 8, 2}
        };

        std::atomic<game::DvarRegisterBool> original_bool = nullptr;
        std::atomic<game::DvarRegisterVariant> original_variant = nullptr;
        std::atomic<bool> installed = false;
        std::atomic<unsigned int> status_logged = 0;
        std::mutex install_mutex;

        void log_status_once(unsigned int bit, const char* message)
        {
            const unsigned int previous = status_logged.fetch_or(bit, std::memory_order_relaxed);
            if ((previous & bit) == 0) LOG_INFO("Dvars", "%s", message);
        }

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

        const VariantOverride* find_variant_override(const char* name, int type)
        {
            if (!name || !*name) return nullptr;
            for (const auto& entry : variant_overrides)
            {
                if (entry.type == type &&
                    (std::strcmp(name, entry.token) == 0 || std::strcmp(name, entry.label) == 0))
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

            const VariantOverride* entry = find_variant_override(safe_name, type);
            std::uint32_t replacement_value = 0;
            void* value_for_original = value;
            if (entry && safe_mem::read(value, replacement_value))
            {
                replacement_value = entry->value;
                value_for_original = &replacement_value;
            }

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
            void* result = original(
                name,
                checksum,
                type,
                flags,
                value_for_original,
                domain,
                description);

            if (entry)
            {
                LOG_INFO(
                    "Dvars",
                    "%s: registered value %s -> %u",
                    entry->label,
                    safe_value,
                    entry->value);
            }
            return result;
        }
    }

    void init()
    {
        LOG_INFO("Dvars", "Loaded %zu Bool overrides", std::size(overrides));
        LOG_INFO("Dvars", "Loaded %zu enum overrides", std::size(variant_overrides));
        LOG_INFO("Dvars", "1.69 offline registration candidates will be forced");
        LOG_INFO("Dvars", "systemlink, systemlink_host, and xblive_loggedin remain under game control");
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
        {
            log_status_once(1u, "Offline hooks waiting: Bool code is not ready");
            return false;
        }

        if (config::hook_dvar_variant &&
            !original_variant.load(std::memory_order_acquire) &&
            !prologue_matches(
                variant_target,
                game::dvar_register_variant_prologue,
                sizeof(game::dvar_register_variant_prologue)))
        {
            log_status_once(2u, "Offline hooks waiting: Variant code is not ready");
            return false;
        }

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
        if (!auth::install(game_base))
        {
            installed.store(false, std::memory_order_release);
            LOG_TRACE("Dvars", "Dvar hooks are ready; waiting for the auth seam");
            return false;
        }
        if (!fence::install(game_base))
        {
            installed.store(false, std::memory_order_release);
            LOG_TRACE("Dvars", "Dvar and auth hooks are ready; waiting for the sign-in fence");
            return false;
        }
        if (!menu_ownership::install(game_base))
        {
            installed.store(false, std::memory_order_release);
            LOG_TRACE("Dvars", "Dvar, auth, and fence hooks are ready; waiting for menu gates");
            return false;
        }
        if (!online_fences::install(game_base))
        {
            installed.store(false, std::memory_order_release);
            LOG_TRACE("Dvars", "Dvar, auth, fence, and menu hooks are ready; waiting for online fences");
            return false;
        }
        if (!content_state::install(game_base))
        {
            installed.store(false, std::memory_order_release);
            LOG_TRACE("Dvars", "Online fences are ready; waiting for local MP content state");
            return false;
        }
        if (!ddl_state::install(game_base) && config::seed_local_ddl)
        {
            installed.store(false, std::memory_order_release);
            LOG_TRACE("Dvars", "Content hooks are ready; waiting for the retail DDL seam");
            return false;
        }
        if (!patch_fence::install(game_base))
        {
            installed.store(false, std::memory_order_release);
            LOG_TRACE("Dvars", "Online fences are ready; waiting for the patch fence");
            return false;
        }
        if (!profile_identity::install(game_base))
        {
            installed.store(false, std::memory_order_release);
            LOG_TRACE("Dvars", "Online fences are ready; waiting for the profile getter");
            return false;
        }
        (void)lua_dump::install(game_base);
        (void)lua_errors::install(game_base);
        if (config::hook_lua_mp_menu && !lua_menu::install(game_base))
        {
            installed.store(false, std::memory_order_release);
            LOG_TRACE("Dvars", "Core hooks are ready; waiting for the 1.69 MP Lua loader");
            return false;
        }
        return true;
    }
}

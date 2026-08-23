#pragma once

#include <cstddef>
#include <cstdint>

namespace game
{
    inline constexpr std::uintptr_t dvar_register_bool_rva = 0x3E79C20;
    inline constexpr std::uint8_t dvar_register_bool_prologue[] = {
        0x48, 0x8B, 0xC4,
        0x48, 0x89, 0x58, 0x08,
        0x48, 0x89, 0x68, 0x18,
        0x48, 0x89, 0x70, 0x20
    };

    inline constexpr std::uintptr_t dvar_register_variant_rva = 0x3E7AE30;
    inline constexpr std::uint8_t dvar_register_variant_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x48, 0x89, 0x6C, 0x24, 0x10,
        0x48, 0x89, 0x74, 0x24, 0x18
    };

    inline constexpr std::uintptr_t dvar_generate_checksum_rva = 0x3E74420;
    inline constexpr std::uintptr_t r_end_frame_rva = 0x6559850;

    // Retail 1.69 DDL investigation anchors. StatsInit creates the native
    // player-data contexts that PlayerData.BFFBGAJGD reads.
    inline constexpr std::uintptr_t live_storage_stats_init_rva = 0x1F60410;
    inline constexpr std::uint8_t live_storage_stats_init_prologue[] = {
        0x44, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x88, 0x44,
        0x24, 0x18, 0x88, 0x54, 0x24, 0x10, 0x89, 0x4C,
        0x24, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54
    };
    using LiveStorageStatsInit = std::int64_t(__fastcall*)(
        unsigned int,
        std::uint8_t,
        std::uint8_t,
        unsigned int);

    // The retail offline stats callback calls this routine after StatsInit.
    // Its exact 1.69 prologue is logged before the call is enabled.
    inline constexpr std::uintptr_t live_storage_stats_fetched_rva = 0x2801320;
    using LiveStorageStatsFetched = void(__fastcall*)(unsigned int, unsigned int);

    inline constexpr std::uintptr_t live_is_user_signed_in_to_demonware_rva = 0x4224A80;
    inline constexpr std::uint8_t live_is_user_signed_in_to_demonware_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xD9,
        0xE8, 0x73, 0xF9, 0xDE, 0xFE, 0x84, 0xC0, 0x74
    };

    inline constexpr std::uintptr_t content_enumeration_finished_rva = 0x152F0940;
    inline constexpr std::uintptr_t bnet_class_rva = 0x15AE04C0;
    inline constexpr std::uintptr_t sign_in_records_rva = 0x1201F978;
    inline constexpr std::uintptr_t sign_in_ready_rva = 0x1201FA78;

    inline constexpr std::size_t sign_in_record_stride = 0x100;
    inline constexpr std::size_t bnet_finished_auth_offset = 0x2D0;
    inline constexpr std::size_t bnet_var3_offset = 0x2F4;
    inline constexpr std::size_t bnet_var4_offset = 0x2F8;
    inline constexpr std::size_t bnet_var5_offset = 0x2FC;

    using DvarRegisterBool = void*(__fastcall*)(
        const char* name,
        bool value,
        unsigned int flags,
        const char* description);

    using DvarRegisterVariant = void*(__fastcall*)(
        const char* name,
        unsigned int checksum,
        int type,
        unsigned int flags,
        void* value,
        void* domain,
        const char* description);

    using LiveIsUserSignedInToDemonware = bool(__fastcall*)(int controller_index);

    inline constexpr std::uintptr_t lui_is_user_signed_in_rva = 0x67A0160;
    inline constexpr std::uint8_t lui_is_user_signed_in_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0xE8, 0x82, 0x47, 0x8C, 0x00
    };

    inline constexpr std::uintptr_t lui_mode_ownership_rva = 0x67A1DF0;
    inline constexpr std::uint8_t lui_mode_ownership_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0xF9, 0xE8, 0xEE, 0x29,
        0x04, 0x00
    };

    inline constexpr std::uintptr_t lui_premium_gate_rva = 0x67A45D0;
    inline constexpr std::uint8_t lui_premium_gate_prologue[] = {
          0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B,
          0x05, 0x4B, 0xD1, 0xF1, 0x04, 0x48, 0x33, 0xC4,
          0x48, 0x89, 0x44, 0x24, 0x40
      };

    inline constexpr std::uintptr_t lui_offline_ownership_rva = 0x67A95D0;
    inline constexpr std::uint8_t lui_offline_ownership_prologue[] = {
          0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
          0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x0E, 0xB3,
          0x8B, 0x00, 0xBF, 0x01, 0x00, 0x00, 0x00
      };

    inline constexpr std::uintptr_t lua_push_boolean_rva = 0x7064EB0;
    using LuaPushBoolean = void(__fastcall*)(void*, int);

    inline constexpr std::uintptr_t lua_push_integer_rva = 0x7065010;
    using LuaPushInteger = void(__fastcall*)(void*, int);

    inline constexpr std::uintptr_t lua_tolstring_rva = 0x7065980;
    using LuaToLString = const char*(__fastcall*)(void*, int, std::size_t*);

    // The retail LUI wrappers use this 32-bit integer conversion thunk to
    // read integer results from the Lua stack.
    inline constexpr std::uintptr_t lua_to_integer_rva = 0x66FD040;
    inline constexpr std::uint8_t lua_to_integer_prologue[] = {
        0xE9, 0x9B, 0x07, 0xFE, 0xFF
    };
    using LuaToInteger = int(__fastcall*)(void*, int);

    // lua_toboolean reads the LuaJIT TValue type bits. The 1.69 retail body
    // matches the Lua 5.1 API used by the LUI wrappers.
    inline constexpr std::uintptr_t lua_to_boolean_rva = 0x7065900;
    inline constexpr std::uint8_t lua_to_boolean_prologue[] = {
        0x48, 0x83, 0xEC, 0x28, 0xE8, 0x57, 0xDF, 0xFF,
        0xFF, 0x48, 0x8B, 0x08, 0x33, 0xC0, 0x48, 0xC1,
        0xF9, 0x2F, 0x83, 0xF9, 0xFE, 0x0F, 0x92, 0xC0
    };
    using LuaToBoolean = int(__fastcall*)(void*, int);

    // luaL_openlib registers a null-terminated luaL_Reg table. The 1.69
    // retail entry matches the registration API used by the 1.64 reference.
    inline constexpr std::uintptr_t lua_openlib_rva = 0x7068F40;
    inline constexpr std::uint8_t lua_openlib_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x41,
        0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41,
        0x28
    };
    using LuaOpenLib = void(__fastcall*)(void*, const char*, void*, unsigned int);

    // lua_atpanic stores the panic callback in LuaJIT global state. The
    // 1.69 match table maps this function to 0x706AC60 with an exact byte
    // match. The prologue below follows the same LuaJIT layout as the
    // verified 1.64 source. The runtime check remains mandatory.
    inline constexpr std::uintptr_t lua_atpanic_rva = 0x706AC60;
    inline constexpr std::uint8_t lua_atpanic_prologue[] = {
        0x48, 0x8B, 0x41, 0x10,
        0x48, 0x8B, 0x80, 0x58, 0x01, 0x00, 0x00,
        0x48, 0x89, 0x90, 0x58, 0x01, 0x00, 0x00,
        0xC3
    };
    using LuaAtPanic = std::int64_t(__fastcall*)(void*, void*);
    using LuaPanic = int(__fastcall*)(void*);

    // LuaJIT lua_State fields used by the passive raw-value reader. These
    // offsets are also used by the existing 1.69 Lua reverse-engineering
    // notes. The reader never calls a Lua API from an error path.
    inline constexpr std::size_t lua_state_top_offset = 0x28;
    inline constexpr std::size_t lua_state_base_offset = 0x20;
    inline constexpr std::int32_t lua_string_type = -5;
    inline constexpr std::size_t lua_string_data_offset = 0x18;

    // The CoD fast-file Lua loader is the path used by the retail require()
    // flow. The 1.69 bytes below are the verified loader prologue from the
    // retail image and the existing 1.69 reverse-engineering notes.
    inline constexpr std::uintptr_t lua_loadfile_fastfile_rva = 0x66E48A0;
    inline constexpr std::uint8_t lua_loadfile_fastfile_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x48, 0x89, 0x6C, 0x24, 0x10,
        0x48, 0x89, 0x74, 0x24, 0x18
    };
    using LuaLoadfileFastfile = int(__fastcall*)(void*, const char*);

    inline constexpr std::uintptr_t lua_loadbuffer_rva = 0x706ADC0;
    using LuaLoadBuffer = int(__fastcall*)(void*, const char*, std::size_t, const char*);

    // Engine.ECHHDAIBD returns the local content progress state. State 3 is
    // INSTALLED in the 1.69 Lua source.
    inline constexpr std::uintptr_t content_progress_rva = 0x67A19E0;
    inline constexpr std::uint8_t content_progress_prologue[] = {
        0x48, 0x89, 0x4C, 0x24, 0x08, 0x57, 0x41, 0x56,
        0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x48,
        0x8B, 0xF9, 0xE8, 0xF9, 0x2E, 0x8C, 0x00, 0x83
    };
    using ContentProgress = int(__fastcall*)(void*);

    // Engine.DBEGJIECGB reports whether a game mode is installed. The Lua
    // source passes CoD.PlayMode.Core (2) for Multiplayer.
    inline constexpr std::uintptr_t mode_installed_rva = 0x67A1F70;
    inline constexpr std::uint8_t mode_installed_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x6E, 0x29,
        0x8C, 0x00, 0x83, 0xF8, 0x01, 0x74, 0x0F
    };
    using ModeInstalled = int(__fastcall*)(void*);

    inline constexpr std::uintptr_t profile_data_getter_rva = 0x67A2FD0;
    inline constexpr std::uint8_t profile_data_getter_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57,
        0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B
    };
    using ProfileDataGetter = int(__fastcall*)(void*);

    inline constexpr std::uintptr_t online_service_fence_rva = 0x694E860;
    inline constexpr std::uint8_t online_service_fence_prologue[] = {
        0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x33, 0xF6, 0xE8,
        0x7C, 0x60, 0x71, 0x00
    };

    inline constexpr std::uintptr_t online_data_fence_rva = 0x694EB20;
    inline constexpr std::uint8_t online_data_fence_prologue[] = {
        0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x33, 0xF6, 0xE8,
        0xBC, 0x5D, 0x71, 0x00
    };

    inline constexpr std::uintptr_t battle_net_connected_fence_rva = 0x694F540;
    inline constexpr std::uint8_t battle_net_connected_fence_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0xE8, 0x12, 0xD2, 0x85, 0xFC, 0x0F, 0xB6,
        0xD0, 0x48, 0x8B, 0xCB, 0xE8, 0x57, 0x59, 0x71,
        0x00
    };

    // Engine.BAAABFICDA is the Battle.net platform-services fence getter.
    // FencePlatformServices.lua treats return value 1 as the pass state.
    inline constexpr std::uintptr_t platform_services_state_rva = 0x679BB60;
    inline constexpr std::uint8_t platform_services_state_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xBA, 0x01,
        0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0xE8, 0xDD,
        0x14, 0xF6, 0xFF, 0xE8
    };

    // Commerce.CIIJICHHF returns the retail exchange state used by
    // FenceExchange.lua. State 2 is the retail GOOD state.
    inline constexpr std::uintptr_t commerce_exchange_state_rva = 0x6A39AA0;
    inline constexpr std::uint8_t commerce_exchange_state_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0xE8, 0x42, 0xAE, 0x62, 0x00, 0x83, 0xF8,
        0x01, 0x74, 0x0F
    };
    using CommerceExchangeState = int(__fastcall*)(void*);

    // Fences.BDCICIIGD. FencePatch.init calls this binding to start the
    // retail patch query. The body is 125 bytes in the retail export.
    inline constexpr std::uintptr_t patch_query_fence_rva = 0x694EE10;
    inline constexpr std::uint8_t patch_query_fence_prologue[] = {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xF9, 0xE8, 0xD2, 0x5A, 0x71, 0x00, 0x85, 0xC0,
        0x74, 0x0F, 0x48, 0x8D
    };

    // Fences.IBEEFACJG. FencePatch.UpdateState treats code 1 as success.
    // The body is 238 bytes in the retail export.
    inline constexpr std::uintptr_t patch_state_fence_rva = 0x694ED20;
    inline constexpr std::uint8_t patch_state_fence_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0xF9, 0xE8, 0xBE, 0x5B,
        0x71, 0x00, 0x85, 0xC0
    };
}

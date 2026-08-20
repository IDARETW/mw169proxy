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
}


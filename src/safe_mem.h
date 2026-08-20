#pragma once

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace safe_mem
{
    inline bool is_readable(DWORD protection)
    {
        if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;

        switch (protection & 0xFF)
        {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
        }
    }

    inline bool read_bytes(const void* source, void* destination, std::size_t size)
    {
        if (!source || !destination) return false;

        auto* input = static_cast<const std::uint8_t*>(source);
        auto* output = static_cast<std::uint8_t*>(destination);
        std::size_t remaining = size;

        while (remaining > 0)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(input, &info, sizeof(info))) return false;
            if (info.State != MEM_COMMIT || !is_readable(info.Protect)) return false;

            const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            const auto current = reinterpret_cast<std::uintptr_t>(input);
            if (region_end <= current) return false;

            const std::size_t available = region_end - current;
            const std::size_t amount = std::min(remaining, available);
            std::memcpy(output, input, amount);
            input += amount;
            output += amount;
            remaining -= amount;
        }
        return true;
    }

    inline bool read_string(const char* source, char* output, std::size_t output_size)
    {
        if (!output || output_size == 0) return false;
        output[0] = '\0';
        if (!source) return false;

        std::size_t written = 0;
        const char* cursor = source;

        while (written + 1 < output_size)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(cursor, &info, sizeof(info))) return false;
            if (info.State != MEM_COMMIT || !is_readable(info.Protect)) return false;

            const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            const auto current = reinterpret_cast<std::uintptr_t>(cursor);
            if (region_end <= current) return false;

            const std::size_t available = region_end - current;
            const std::size_t amount = std::min(output_size - written - 1, available);
            for (std::size_t index = 0; index < amount; ++index)
            {
                const char value = cursor[index];
                output[written++] = value;
                if (value == '\0') return true;
            }
            cursor += amount;
        }

        output[written] = '\0';
        return false;
    }

    template <typename T>
    bool read(const void* source, T& value)
    {
        return read_bytes(source, &value, sizeof(value));
    }
}


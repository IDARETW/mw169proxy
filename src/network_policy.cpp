#include "network_policy.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <iterator>

namespace network_policy
{
    namespace
    {
        template <typename Character>
        bool equals_ascii(const Character* text, const char* expected)
        {
            if (!text || !expected) return false;
            while (*text && *expected)
            {
                const auto value = static_cast<unsigned int>(*text);
                const auto lower = value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
                if (lower != static_cast<unsigned int>(*expected)) return false;
                ++text;
                ++expected;
            }
            return *text == 0 && *expected == 0;
        }

        template <typename Character>
        bool ends_with_ascii(const Character* text, const char* suffix)
        {
            if (!text || !suffix) return false;
            std::size_t text_size = 0;
            std::size_t suffix_size = 0;
            while (text[text_size]) ++text_size;
            while (suffix[suffix_size]) ++suffix_size;
            if (suffix_size > text_size) return false;
            return equals_ascii(text + text_size - suffix_size, suffix);
        }

        template <typename Character>
        bool parse_ipv4(const Character* text, std::uint8_t output[4])
        {
            if (!text || !*text) return false;
            for (unsigned int part = 0; part < 4; ++part)
            {
                const Character* part_start = text;
                unsigned int value = 0;
                unsigned int digits = 0;
                while (*text >= '0' && *text <= '9')
                {
                    value = value * 10 + static_cast<unsigned int>(*text - '0');
                    if (value > 255) return false;
                    ++digits;
                    ++text;
                }
                if (!digits) return false;
                if (digits > 1 && *part_start == '0') return false;
                output[part] = static_cast<std::uint8_t>(value);
                if (part != 3)
                {
                    if (*text != '.') return false;
                    ++text;
                }
            }
            return *text == 0;
        }

        template <typename Character>
        bool copy_ipv6_address(
            const Character* text,
            Character* output,
            std::size_t output_size)
        {
            if (!text || !*text || !output || output_size < 2) return false;

            std::size_t address_size = 0;
            while (text[address_size] && text[address_size] != '%') ++address_size;
            if (!address_size || address_size >= output_size) return false;

            if (text[address_size] == '%')
            {
                std::size_t scope = address_size + 1;
                if (!text[scope]) return false;
                while (text[scope])
                {
                    if (text[scope] < '0' || text[scope] > '9') return false;
                    ++scope;
                }
            }

            for (std::size_t index = 0; index < address_size; ++index)
                output[index] = text[index];
            output[address_size] = 0;
            return true;
        }

        bool parse_ipv6(const char* text, std::uint8_t output[16])
        {
            char address_text[INET6_ADDRSTRLEN]{};
            IN6_ADDR address{};
            if (!copy_ipv6_address(text, address_text, std::size(address_text)) ||
                InetPtonA(AF_INET6, address_text, &address) != 1)
                return false;
            std::memcpy(output, &address, 16);
            return true;
        }

        bool parse_ipv6(const wchar_t* text, std::uint8_t output[16])
        {
            wchar_t address_text[INET6_ADDRSTRLEN]{};
            IN6_ADDR address{};
            if (!copy_ipv6_address(text, address_text, std::size(address_text)) ||
                InetPtonW(AF_INET6, address_text, &address) != 1)
                return false;
            std::memcpy(output, &address, 16);
            return true;
        }

        template <typename Character>
        bool allow_text_hostname(const Character* hostname)
        {
            if (!hostname || !*hostname) return true;
            if (equals_ascii(hostname, "localhost") ||
                equals_ascii(hostname, "localhost.localdomain"))
                return true;

            if (ends_with_ascii(hostname, ".local")) return true;

            std::uint8_t ipv4[4]{};
            if (parse_ipv4(hostname, ipv4)) return allow_ipv4(ipv4);

            std::uint8_t ipv6[16]{};
            if (parse_ipv6(hostname, ipv6)) return allow_ipv6(ipv6);
            return false;
        }
    }

    bool allow_ipv4(const std::uint8_t address[4])
    {
        if (!address) return false;
        const auto a = address[0];
        const auto b = address[1];

        if (a == 0 && b == 0 && address[2] == 0 && address[3] == 0) return true;
        if (a == 10 || a == 25 || a == 26 || a == 127) return true;
        if (a == 100 && b >= 64 && b <= 127) return true;
        if (a == 169 && b == 254) return true;
        if (a == 172 && b >= 16 && b <= 31) return true;
        if (a == 192 && b == 168) return true;
        if (a >= 224 && a <= 239) return true;
        if (a == 255 && b == 255 && address[2] == 255 && address[3] == 255) return true;
        return false;
    }

    bool allow_ipv6(const std::uint8_t address[16])
    {
        if (!address) return false;

        bool unspecified = true;
        for (std::size_t index = 0; index < 16; ++index)
            unspecified = unspecified && address[index] == 0;
        if (unspecified) return true;

        bool loopback = address[15] == 1;
        for (std::size_t index = 0; index < 15; ++index)
            loopback = loopback && address[index] == 0;
        if (loopback) return true;

        bool mapped = address[10] == 0xFFu && address[11] == 0xFFu;
        for (std::size_t index = 0; index < 10; ++index)
            mapped = mapped && address[index] == 0;
        if (mapped) return allow_ipv4(address + 12);

        if ((address[0] & 0xFEu) == 0xFCu) return true;
        if (address[0] == 0xFEu && (address[1] & 0xC0u) == 0x80u) return true;
        if (address[0] == 0xFFu) return true;
        return false;
    }

    bool allow_hostname(const char* hostname)
    {
        return allow_text_hostname(hostname);
    }

    bool allow_hostname(const wchar_t* hostname)
    {
        return allow_text_hostname(hostname);
    }
}

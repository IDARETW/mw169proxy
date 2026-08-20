#pragma once

#include <cstddef>
#include <cstdint>

namespace network_policy
{
    bool allow_ipv4(const std::uint8_t address[4]);
    bool allow_ipv6(const std::uint8_t address[16]);
    bool allow_hostname(const char* hostname);
    bool allow_hostname(const wchar_t* hostname);
}

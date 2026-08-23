#include "network_policy.h"

#include <cstdint>
#include <cstdio>

namespace
{
    bool expect(bool actual, bool expected, const char* label)
    {
        if (actual == expected) return true;
        std::fprintf(stderr, "FAIL: %s\n", label);
        return false;
    }
}

int main()
{
    bool passed = true;

    const std::uint8_t loopback[] = {127, 0, 0, 1};
    const std::uint8_t private_lan[] = {192, 168, 1, 20};
    const std::uint8_t private_172_low[] = {172, 16, 0, 1};
    const std::uint8_t private_172_high[] = {172, 31, 255, 254};
    const std::uint8_t public_172[] = {172, 32, 0, 1};
    const std::uint8_t hamachi[] = {25, 4, 3, 2};
    const std::uint8_t radmin[] = {26, 4, 3, 2};
    const std::uint8_t tailscale[] = {100, 100, 20, 30};
    const std::uint8_t outside_tailscale[] = {100, 128, 0, 1};
    const std::uint8_t multicast[] = {239, 255, 0, 1};
    const std::uint8_t limited_broadcast[] = {255, 255, 255, 255};
    const std::uint8_t reserved[] = {240, 0, 0, 1};
    const std::uint8_t public_dns[] = {8, 8, 8, 8};
    const std::uint8_t public_web[] = {1, 1, 1, 1};
    const std::uint8_t ipv4_unspecified[] = {0, 0, 0, 0};
    const std::uint8_t ipv6_unspecified[16]{};
    const std::uint8_t ipv6_loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const std::uint8_t ipv6_ula[16] = {0xFD, 0x12, 0x34, 0x56};
    const std::uint8_t ipv6_link_local[16] = {0xFE, 0x80, 0, 0};
    const std::uint8_t ipv6_multicast[16] = {0xFF, 0x02, 0, 0};
    const std::uint8_t ipv6_mapped_private[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 192, 168, 1, 20};
    const std::uint8_t ipv6_mapped_public[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 8, 8, 8, 8};
    const std::uint8_t ipv6_public[16] = {0x20, 0x01, 0x48, 0x60};

    passed &= expect(network_policy::allow_ipv4(loopback), true, "IPv4 loopback");
    passed &= expect(network_policy::allow_ipv4(private_lan), true, "IPv4 LAN");
    passed &= expect(network_policy::allow_ipv4(private_172_low), true, "IPv4 private lower edge");
    passed &= expect(network_policy::allow_ipv4(private_172_high), true, "IPv4 private upper edge");
    passed &= expect(network_policy::allow_ipv4(public_172), false, "IPv4 public after private range");
    passed &= expect(network_policy::allow_ipv4(hamachi), true, "Hamachi range");
    passed &= expect(network_policy::allow_ipv4(radmin), true, "Radmin range");
    passed &= expect(network_policy::allow_ipv4(tailscale), true, "Tailscale range");
    passed &= expect(network_policy::allow_ipv4(outside_tailscale), false, "Outside Tailscale range");
    passed &= expect(network_policy::allow_ipv4(multicast), true, "IPv4 multicast");
    passed &= expect(network_policy::allow_ipv4(limited_broadcast), true, "IPv4 broadcast");
    passed &= expect(network_policy::allow_ipv4(reserved), false, "IPv4 reserved range");
    passed &= expect(network_policy::allow_ipv4(public_dns), false, "Public DNS");
    passed &= expect(network_policy::allow_ipv4(public_web), false, "Public web");
    passed &= expect(network_policy::allow_ipv4(ipv4_unspecified), true, "IPv4 unspecified");
    passed &= expect(network_policy::allow_ipv6(ipv6_unspecified), true, "IPv6 unspecified");
    passed &= expect(network_policy::allow_ipv6(ipv6_loopback), true, "IPv6 loopback");
    passed &= expect(network_policy::allow_ipv6(ipv6_ula), true, "IPv6 unique local");
    passed &= expect(network_policy::allow_ipv6(ipv6_link_local), true, "IPv6 link local");
    passed &= expect(network_policy::allow_ipv6(ipv6_multicast), true, "IPv6 multicast");
    passed &= expect(network_policy::allow_ipv6(ipv6_mapped_private), true, "IPv6 mapped LAN");
    passed &= expect(network_policy::allow_ipv6(ipv6_mapped_public), false, "IPv6 mapped public");
    passed &= expect(network_policy::allow_ipv6(ipv6_public), false, "IPv6 public");

    passed &= expect(network_policy::allow_hostname("localhost"), true, "localhost");
    passed &= expect(network_policy::allow_hostname("game-host"), false, "Unqualified host");
    passed &= expect(network_policy::allow_hostname("server.local"), true, "mDNS host");
    passed &= expect(network_policy::allow_hostname("10.0.0.5"), true, "LAN text address");
    passed &= expect(network_policy::allow_hostname("8.8.8.8"), false, "Public text address");
    passed &= expect(network_policy::allow_hostname("010.0.0.1"), false, "Legacy octal public address");
    passed &= expect(network_policy::allow_hostname("00127.0.0.1"), false, "Padded public address");
    passed &= expect(network_policy::allow_hostname("::1"), true, "IPv6 text loopback");
    passed &= expect(network_policy::allow_hostname(":"), false, "Short IPv6 text");
    passed &= expect(network_policy::allow_hostname("f:"), false, "Short IPv6 prefix");
    passed &= expect(network_policy::allow_hostname("fd12::5"), true, "IPv6 text unique local");
    passed &= expect(network_policy::allow_hostname("fe80::5"), true, "IPv6 text link local");
    passed &= expect(network_policy::allow_hostname("fe80::5%12"), true, "IPv6 link-local scope");
    passed &= expect(network_policy::allow_hostname("fe00::5"), false, "IPv6 text reserved");
    passed &= expect(network_policy::allow_hostname("fd::1"), false, "Short IPv6 ULA lookalike");
    passed &= expect(network_policy::allow_hostname("fc::1"), false, "Short IPv6 ULA prefix");
    passed &= expect(network_policy::allow_hostname("ff::1"), false, "Short IPv6 multicast prefix");
    passed &= expect(network_policy::allow_hostname("fe8::1"), false, "Short IPv6 link-local prefix");
    passed &= expect(network_policy::allow_hostname("0:0:0:0:0:0:0:1"), true, "Expanded loopback");
    passed &= expect(network_policy::allow_hostname("fe80::5%"), false, "Empty IPv6 scope");
    passed &= expect(network_policy::allow_hostname("::ffff:192.168.1.20"), true, "IPv6 mapped LAN text");
    passed &= expect(
        network_policy::allow_hostname("0:0:0:0:0:ffff:c0a8:114"),
        true,
        "Expanded IPv6 mapped LAN text");
    passed &= expect(network_policy::allow_hostname("::ffff:8.8.8.8"), false, "IPv6 mapped public text");
    passed &= expect(network_policy::allow_hostname("example.com"), false, "Public hostname");
    passed &= expect(network_policy::allow_hostname(L"192.168.1.20"), true, "Wide LAN address");
    passed &= expect(network_policy::allow_hostname(L"fd12::5"), true, "Wide IPv6 unique local");
    passed &= expect(network_policy::allow_hostname(L"010.0.0.1"), false, "Wide octal address");
    passed &= expect(network_policy::allow_hostname(L"example.com"), false, "Public wide hostname");

    if (!passed) return 1;
    std::puts("Network policy tests passed");
    return 0;
}

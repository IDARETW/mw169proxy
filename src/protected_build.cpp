#include "protected_build.h"

extern "C" __declspec(noinline) std::uint32_t mw169_build_protection_probe()
{
    static constexpr char marker[] = "mw169proxy shifting probe 169";
    const volatile auto* bytes = reinterpret_cast<const volatile unsigned char*>(marker);
    std::uint32_t hash = 2166136261u;
    for (std::uint32_t index = 0; index < sizeof(marker) - 1; ++index)
    {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    return hash;
}

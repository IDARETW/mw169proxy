#pragma once

#include <cstdint>

namespace trigger
{
    using InstallFunction = bool(*)(std::uintptr_t game_base);

    bool install(
        std::uintptr_t game_base,
        InstallFunction install_function,
        bool install_cwhook_trigger);
}

#pragma once

#include <cstdint>

namespace profile_identity
{
    void init();
    bool install(std::uintptr_t game_base);
}

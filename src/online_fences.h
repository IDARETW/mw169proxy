#pragma once

#include <cstdint>

namespace online_fences
{
    void init();
    bool install(std::uintptr_t game_base);
}

#pragma once

#include <cstdint>

namespace patch_fence
{
    void init();
    bool install(std::uintptr_t game_base);
}

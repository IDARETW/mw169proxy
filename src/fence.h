#pragma once

#include <cstdint>

namespace fence
{
    void init();
    bool install(std::uintptr_t game_base);
}

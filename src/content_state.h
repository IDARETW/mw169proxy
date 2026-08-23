#pragma once

#include <cstdint>

namespace content_state
{
    void init();
    bool install(std::uintptr_t game_base);
}

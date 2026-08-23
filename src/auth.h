#pragma once

#include <cstdint>

namespace auth
{
    void init();
    void initialize_local_state(std::uintptr_t game_base, int controller_index);
    bool install(std::uintptr_t game_base);
}

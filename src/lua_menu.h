#pragma once

#include <cstdint>

namespace lua_menu
{
    void init();
    bool install(std::uintptr_t game_base);
}

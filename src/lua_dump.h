#pragma once

#include <cstdint>

namespace lua_dump
{
    void init();
    bool install(std::uintptr_t game_base);
}

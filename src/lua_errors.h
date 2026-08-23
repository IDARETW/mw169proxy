#pragma once

#include <cstdint>

namespace lua_errors
{
    void init();
    bool install(std::uintptr_t game_base);
    void capture_compile_error(void* lua_state, const char* chunk_name, int status);
}

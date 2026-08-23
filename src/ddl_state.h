#pragma once

#include <cstdint>

namespace ddl_state
{
    void init();
    bool install(std::uintptr_t game_base);
    void main_thread_kick(std::uintptr_t game_base, bool multiplayer_query);
}

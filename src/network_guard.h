#pragma once

#include <cstdint>

namespace network_guard
{
    bool install(std::uintptr_t game_base);
    bool uninstall();
}

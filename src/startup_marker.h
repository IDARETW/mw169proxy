#pragma once

namespace startup_marker
{
    // Remove the marker before the game entry point can inspect it.
    void remove_game_marker_early();
    bool remove_game_marker();
}

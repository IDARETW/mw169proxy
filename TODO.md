# TODO

## online multiplayer menus

- Reproduce the current Lua and playlist failure from a clean 1.69 launch.
- Capture the complete passive Lua error text and LuaJIT stack for each failed chunk.
- Keep the Lua error observer passive. Do not hook `lua_pcall` or `cpparser`.
- Verify that `MPPlayMenu.lua` and `MPPlayMenuButtons.lua` load only after the correct
  LUI registration point is ready.
- Open the Multiplayer page from the main menu and verify the Private Match and Trials
  buttons.
- Trace the playlist fence, content ownership, DDL, and menu state transitions around
  the first MP page load.
- Keep playlist retrieval local. Do not start official services or use public-network
  fallback paths.
- Add a guarded local playlist result only after the retail consumer and fence order are
  confirmed.
- Record the exact Lua loader, fence, and playlist log lines for the next live test.

## Known state

- The 1.69 game reaches the main menu with Multiplayer selected.
- The local MP Lua replacements are loaded, but the MP submenu and playlist flow are not
  confirmed.
- CWHook checksum healing finds 181 sites at the decrypted `LoadImageA` edge.
- CWHook VEH and broad system hooks remain disabled because they caused an exception loop
  at `game+0xB1EE64` during earlier tests.

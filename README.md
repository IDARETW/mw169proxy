# mw169proxy

mw169proxy is an offline and community-mod foundation for Call of Duty: Modern Warfare
2019 retail build 1.69.0.26668155.

It is a proxy DLL named `discord_game_sdk.dll`. It forwards the original Discord exports
and adds guarded local hooks. The project focuses on local play, LAN support, and future
community servers. It does not target cheating or official online services.

## Build

Use xmake on Windows:

```powershell
xmake f -m release -a x64 -y
xmake
```

The output is `x64/Release/discord_game_sdk.dll`.

The full build needs the local dependency trees. They are not included in this public
repository.

## Layout

- `src/` contains the active code.
- `tests/` contains local smoke and network-policy tests.
- `xmake.lua` contains the build definition.
- `TODO.md` contains the next work items.

The local workspace also has private reverse-engineering notes, databases, logs, and
third-party source. `.gitignore` keeps those files out of the public repository.

## Status

The latest 1.69 test reached the main menu with Multiplayer selected. Lua menu loading
and playlist handling still need work.

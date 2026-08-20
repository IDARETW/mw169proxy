# mw169proxy

I figured I'd open source a large portion of the early-in-development mw169proxy project for others to take from, use as a base for custom implementations, review, and whatever else you want to use it for.

mw169proxy is a small proxy DLL foundation for Modern Warfare 2019 build
1.69.0.26668155.

The DLL uses the name `discord_game_sdk.dll`. It loads the original library from
`discord_game_sdks.dll` in the same folder. It forwards the three imported Discord
exports.

## Build

Use xmake from this folder.

e.g.:
```powershell
xmake f -m release -a x64 -y
xmake
```

The output is `x64/<config>/discord_game_sdk.dll`.

## General Layout

| Path | Purpose |
|---|---|
| `src/` | All active first-party code |
| `src/game.h` | 1.69 RVAs and verified prologues |
| `src/config.h` | Compile-time switches |
| `thirdparty/` (not included) | required libraries |

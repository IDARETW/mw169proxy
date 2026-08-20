# mw169proxy

mw169proxy is a small proxy DLL foundation for Modern Warfare 2019 build
1.69.0.26668155.

The DLL uses the name `discord_game_sdk.dll`. It loads the original library from
`discord_game_sdks.dll` in the same folder. It forwards the three imported Discord
exports.

The active mod code checks game-code readiness on each `LoadImageA` call. It then installs
the 1.69 `Dvar_RegisterBool` and `Dvar_RegisterVariant` hooks. The Bool hook forces two
retail registration candidates for offline mode. The Variant hook writes a trace.

The main executable import table also has an early network backup. It rejects destinations
outside loopback, LAN, multicast, and selected community VPN ranges. It is not an
operating-system firewall. See `docs/OFFLINE_NETWORK.md` for its exact boundary.

## Build

Use xmake from this folder.

```powershell
xmake f -m release -a x64 -y
xmake
```

The output is `x64/Release/discord_game_sdk.dll`.

The release and debug builds need no Python tool. The project has no packer, INI file,
or Visual Studio project.

Build an interactive protected DLL with these commands:

```powershell
xmake f -m protect -a x64 -y
xmake
```

The first protected build installs a local Python environment. It also downloads the
pinned Shifting.Codes source and its locked dependencies. The build then opens the
protection window. Select the functions and passes. Select `Protect and continue build`.
The build validates the x64 object before it links the DLL.

The protected output is `x64/Protect/discord_game_sdk.dll`. Add source files to
`SHIFTING_SOURCES` in `xmake.lua`. Protect mode compiles each file to bitcode, merges
the modules into `x64/Protect/Shifting/shifting.bc`, and opens that module in the
protection window. The default list contains the probe file and `src/hook.cpp`.

Use the headless preset for an automated build test:

```powershell
xmake f -m protect -a x64 --shifting_headless=y -y
xmake
```

See `docs/SHIFTING.md` for the tool pin, setup details, and license notice.

Run the complete local release check with this command:

```powershell
.\tools\verify.ps1
```

Add `-Protect` to run the deterministic protected build check too.

Build the optional local load test with this command:

```powershell
xmake build load_smoke
xmake network_policy_test
```

The test loads a supplied proxy path and calls `LoadImageA`. It does not test game RVAs.
The policy test checks the allowed local ranges and blocked public ranges.

See `docs/OFFLINE_NETWORK.md` for the PDB-to-retail evidence and the live test limit.

## Layout

| Path | Purpose |
|---|---|
| `src/` | All active first-party code |
| `src/game.h` | 1.69 RVAs and verified prologues |
| `src/config.h` | Compile-time switches |
| `tools/shifting/` | Optional protected-build bridge |
| `extras/` | Disabled extension points for later work |
| `docs/` | Porting facts and live-test steps |
| `legacy/pre-modern-port/` | Archived 1.69 reference code |
| `thirdparty/` | PolyHook2 and its required libraries |

To add a feature, add one source file and one header file. Add one direct initialization
call in `src/main.cpp`. Add an RVA to `src/game.h` only after you verify it against the
1.69 retail image.

## Deploy

The deploy script requires the game folder. The first deployment also requires explicit
approval to preserve the original Discord DLL.

The default game folder is `E:\IW8\Binaries\1.69-Retail-BNet`.

```powershell
.\deploy_mw169.ps1 -BackupOriginal
```

Later deployments do not need `-BackupOriginal`. You can use `-GameDir` to select a
different 1.69 folder.

Use `-Configuration Protect` to deploy the interactive protected build.

The game must close before the copy. The script waits while `ModernWarfare.exe` runs.

## Current verification state

The project can be verified by a local build and a proxy export check. A game result is
not confirmed until the owner runs build 1.69 and supplies `mw169proxy.log` and
`mw169proxy.trace.log`.

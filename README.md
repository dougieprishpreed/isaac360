# The Binding of Isaac Rebirth Complete Edition for Xbox 360

`boi360` is a work-in-progress native port of **The Binding of Isaac Rebirth Complete Edition** from the **Windows Steam version** to the **Microsoft Xbox 360 XDK**.

The goal is to preserve the original game's content and behavior, including procedural generation, combat, items, progression, saves, local multiplayer, artwork, animation and audio. The gameplay runtime uses C++ compatible with Visual Studio 2010 and the Xbox 360 XDK. Windows host builds test the same runtime used by the Xbox adapter.

## Project status

The port is incomplete, and no playable Xbox 360 release is available.

Resource readers, level generation, room components and parts of player initialization have been implemented and checked on Windows. A normal New Run currently stops during player initialization, before playable gameplay begins.

The next milestone is a normal New Run that fully initializes a player and supports repeated movement and collision in its generated starting room. Integrated combat, room transitions, progression, save/Continue and complete presentation and audio remain unfinished. Passing host checks does not establish Xbox 360 playability.

XEX builds and Xenia testing are scheduled to resume after the initial gameplay milestone. Physical-console testing remains necessary before release acceptance.

## Development requirements

- Windows with Visual Studio 2010 C++ build tools.
- Microsoft Xbox 360 XDK for target development.
- Python 3 and PowerShell for data preparation and verification.
- A local copy of the Windows Steam game data, plus the prepared resource and reference inputs used by the development tools.

Original game data belongs in `boi360\game`, the repository root's `game/` directory, and remains read-only. Game files, prepared resources, development references and compiled binaries are excluded from the repository. A fresh clone therefore needs these local inputs before builds or verification can run.

## Windows host verification

With the toolchain and local inputs configured, run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_runs.ps1 -Seeds 128
```

This builds the maintained Windows executable and runs deterministic simulation checks without rendering or frame delays. Verification runs serially on one logical CPU and writes build products and reports under `build/` and `logs/`. It does not build an XEX or launch Xenia.

## Repository layout

- `src/game/` — shared gameplay runtime.
- `src/content/` — resource and room readers.
- `src/xbox/` — Xbox graphics/input adapter and XDK project.
- `src/simulation/` — Windows simulation checks.
- `tools/` — preparation, build and verification tools.
- `tests/` — regression fixtures and supporting checks.

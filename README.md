# The Binding of Isaac Rebirth Complete Edition for Xbox 360

`boi360` is a work-in-progress native port of **The Binding of Isaac Rebirth Complete Edition** from the **Windows Steam version** to the **Microsoft Xbox 360 XDK**.

The goal is to preserve the original game's content and behavior, including procedural generation, combat, items, progression, saves, local multiplayer, artwork, animation and audio. The gameplay runtime uses C++ compatible with Visual Studio 2010 and the Xbox 360 XDK. Windows host builds test the same runtime used by the Xbox adapter.

## Project status

The port is incomplete, and no playable Xbox 360 release is available. The target is the complete game: Normal, Hard and Greed modes, every character and unlock, original progression, and the full item/enemy/boss/room/audio presentation.

Resource readers, level generation, room components and the normal player lifecycle have been implemented and checked on Windows. A normal New Run now generates its first floor, registers the player, constructs and enters the selected starting room, completes placement and post-generation initialization, and executes repeated whole simulation ticks in that room. Each tick completes the `414dc0` actor/player update, `6712a0` collision, the disabled `8d8470` particle gate, the `8798f0` room-effect cache update, enemy updates and the primary projectile lifecycle. The reached ordinary `72c430` no-item path performs its actual `0x1280`/`0x1284` head-timer and `0x1e88` call-counter work. Primary fire consumes the evaluated shot vector and the real `fireDelay`/`shotSpeed`/`damage`/`range` stats; host evidence observes spawned tears, wall collision and range expiry. A generated Gaper encounter is killed by those tears, clears the room and opens a normal door. The player then crosses the cleared door into the exact linked room, where rocks and the next enemy set are constructed and the player is placed at the reciprocal door. The Windows host also observes the complete initialized player moving to the real room boundary, stopping stably and reversing.

Primary firing, bidirectional enemy/player damage, death and normal-door transitions now work on Windows. A stage-owned generated Gaper room constructs its actual spawn plan, updates the enemy, and the projectile system damages and kills it, sets room clear and opens a normal door. A cleared door accepts the player into the exact linked resident, including rocks and the next enemy set, places the player at the reciprocal door and continues whole ticks. Enemy contact damages the player on a 30-tick invulnerability cadence; a generated Gaper encounter kills the starting player after six hits, a 60-tick dying state reaches game over and restart rebuilds a ready player at full health. Treasure doors remain closed without a key, consume one key on contact and open; boss doors open only after the boss-defeated flag. The port is still not a fully playable release: remaining enemy variants/attacks/death FX, actual boss/later-floor behavior, Greed mode, the full character/progression matrix, the `747490`/`74ffa0` item/effect branches, GPU layer composition, the D6 starting grant, save/Continue and complete presentation and audio remain unfinished. Passing host checks does not establish Xbox 360 playability.

The shared sources compile into an Xbox 360 XEX with the installed XDK, and the current build runs on the target: a New Run reaches `PNR_READY`, consumes XInput frames, executes repeated whole gameplay ticks, renders the actual selected room from original backdrop pixels plus the Isaac costume, enemies, rocks, doors, tears and HUD hearts, and moves through an open door into the linked enemy room. Target combat was observed through a hidden, muted Xenia window with frame captures; physical-console testing remains outstanding. Pickups, full enemy animation/AI, bosses, later floors, Hard/Greed modes, the full character/progression matrix, save/Continue, original audio and release packaging are still unfinished.

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

- `src/game/` â€” shared gameplay runtime.
- `src/content/` â€” resource and room readers.
- `src/xbox/` â€” Xbox graphics/input adapter and XDK project.
- `src/simulation/` â€” Windows simulation checks.
- `tools/` â€” preparation, build and verification tools.
- `tests/` â€” regression fixtures and supporting checks.

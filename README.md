# The Binding of Isaac Rebirth Complete Edition for Xbox 360

`boi360` is a work-in-progress native port of **The Binding of Isaac Rebirth Complete Edition** from the **Windows Steam version** to the **Microsoft Xbox 360 XDK**.

The goal is to preserve the original game's content and behavior, including procedural generation, combat, items, progression, saves, local multiplayer, artwork, animation and audio. The gameplay runtime uses C++ compatible with Visual Studio 2010 and the Xbox 360 XDK. Windows host builds test the same runtime used by the Xbox adapter.

## Project status

The port is incomplete, and no playable Xbox 360 release is available. The target is the complete game: Normal, Hard and Greed modes, every character and unlock, original progression, and the full item/enemy/boss/room/audio presentation.

Resource readers, level generation, room components and the normal player lifecycle have been implemented and checked on Windows. A normal New Run now generates its first floor, registers the player, constructs and enters the selected starting room, completes placement and post-generation initialization, and executes repeated whole simulation ticks in that room. Each tick completes the `414dc0` actor/player update, `6712a0` collision, the disabled `8d8470` particle gate, the `8798f0` room-effect cache update, enemy updates and the primary projectile lifecycle. The reached ordinary `72c430` no-item path performs its actual `0x1280`/`0x1284` head-timer and `0x1e88` call-counter work. Primary fire consumes the evaluated shot vector and the real `fireDelay`/`shotSpeed`/`damage`/`range` stats; host evidence observes spawned tears, wall collision and range expiry. A generated Gaper encounter is killed by those tears, clears the room and opens a normal door. The player then crosses the cleared door into the exact linked room, where rocks and the next enemy set are constructed and the player is placed at the reciprocal door. The Windows host also observes the complete initialized player moving to the real room boundary, stopping stably and reversing.

Primary firing, bidirectional enemy/player damage, death and normal-door transitions now work on Windows. A stage-owned generated Gaper room constructs its actual spawn plan, updates the enemy, and the projectile system damages and kills it, sets room clear and opens a normal door. A cleared door accepts the player into the exact linked resident, including rocks and the next enemy set, places the player at the reciprocal door and continues whole ticks. Enemy contact damages the player on a 30-tick invulnerability cadence; a generated Gaper encounter kills the starting player after six hits, a 60-tick dying state reaches game over and restart rebuilds a ready player at full health. Treasure doors remain closed without a key, consume one key on contact and open; boss-room entry doors open on room clear, and the prepared Dingle boss (300 HP, slide/spit behavior) can be fought and killed to set the floor boss-defeated flag. Enemy walk cycles are composed from the original ANM2 tracks; Pooters and Dingle fire hostile tears that collide with the room and the player. Boss death opens a trapdoor and the adapter descends into a generated Cellar floor that keeps the surviving player, with stage-2 doors, enemies and pickups continuing to work; 24 sampled generated boss rooms all construct. All twelve floors now generate and descend, all 36 character slots start and tick, treasure/trinket/card/pill pedestals select and record ownership, starting collectibles and active slots are granted, bombs place/explode and destroy grids, Hard mode reaches ready, and a maintained set of item/pill/card/active effects applies stats, hearts and resources. The port is still not a fully playable release: remaining boss families and terminal-floor fidelity, the full native stat-cache operator, familiar behavior, Greed mode, the full progression/unlock matrix, save/Continue, original audio and complete presentation remain unfinished. Passing host checks does not establish Xbox 360 playability.

The shared sources compile into an Xbox 360 XEX with the installed XDK. Earlier target runs reached `PNR_READY`, consumed XInput frames, executed whole gameplay ticks and rendered rooms, Isaac, enemies, doors, tears and HUD hearts; the current XEX has not been re-run after the latest character/item/bomb/Hard-mode changes. The Windows visual target is temporarily blocked by the host display session failing D3D9 device creation, while headless host verification continues. Physical-console validation, Greed mode, save/Continue, audio and full progression remain unfinished.

## Current verification

The required serial 128-seed host packet passes with `5,907,327` checks and `0` failures at `logs/normal_flow_20261031T071431306739Z/results.json`; `tools/audit_port.py` verifies the packet against the current tree. Focused host evidence records `CHARACTERS ok=36 fail=0`, `DESCEND from=1 to=12`, `ITEM pool=0 id=55 owned=1`, `BOMB explosions=1`, `POCKET hp=10/10`, `ACTIVE id=45 hp=6/6`, `FAMILIAR spawned=3 shots=2 hits=1`, `CONTINUE stage=3 hp=7/8 keys=99 card=9 pill=7`, `NATIVE_CONTINUE seed=1 stage=1 hp=6/6`, `NATIVE_CURRENT_EFFECT count=2 timer=1234`, `NATIVE_CURRENT stage=3 hp=5/10 soul=2 keys=42 bombs=3 coins=17 active=45 card=9 pill=7 poolCounter=1 pos=(333.0,444.0) fd=8.50 dmg=7.25 spd=1.40 luck=3.0 effects=2` and `GREED start wave=1/11 room=0 type=11 variant=0 enemies=5` / `GREED complete waves=11`. Greed mode now starts from the complete-edition greed room catalogue and advances eleven actual wave rooms on room clear; the Ultra Greed boss wave and greed save/Continue remain open. A maintained checkpoint saves/restores mid-run state, and the native GSR0142 path now writes a current-state game/player/item-pool snapshot and reads it back for exact Continue; native effect type/id/count/timer capture and restore now work; persistent effect-owner/callback details remain open. The focused result is `build/gameplay/result.json`. The Windows target compiles at SHA256 `87D0AB9E07D21259D5B1308A63939E0C43521912B6D1716FD8C9E64F1CD20287`; the current XEX is `runtime/default.xex` at SHA256 `9821A4F330A68632222C0D16173CA405F363FCD4AB907764B685D6FF6E19177A` (compiled, not re-run on target).

## Development requirements

- Windows with Visual Studio 2010 C++ build tools.
- Microsoft Xbox 360 XDK for target development.
- Python 3 and PowerShell for data preparation and verification.
- A local copy of the Windows Steam game data, plus the prepared resource and reference inputs used by the development tools.

Original game data belongs in `boi360\game`, the repository root's `game/` directory, and remains read-only. Game files, prepared resources, development references and compiled binaries are excluded from the repository. A fresh clone therefore needs these local inputs before builds or verification can run.

## Windows gameplay target

`build/windows/boi360.exe` runs the same shared runtime directly on Windows with in-process keyboard/scripted input, no emulator focus and fast fixed-step execution. Build it with `tools/build_windows_target.ps1`. See `docs/WINDOWS_TARGET.md`. It renders the actual room, player, enemy, pickup and HUD art from the original assets and can save verified screenshots.

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

# The Binding of Isaac Rebirth Complete Edition for Xbox 360

This project ports **The Binding of Isaac Rebirth Complete Edition** from the **Windows Steam version** to the **Microsoft Xbox 360 XDK**. All maintained runtime code must remain strictly compatible with Visual Studio 2010 and the Xbox 360 XDK; Windows host builds exercise the same runtime for fast testing. Applicable content, original behavior and RNG ownership/order, presentation/audio, progression, local multiplayer and complete persistence remain the deliverable. An alpha RC is a milestone for that full deliverable.

Original game data is always in `boi360\game` (the workspace's read-only `game/` directory). This repository tracks maintained source, tools, tests, README and standard repository files. Detailed development docs (`docs/` and `AGENTS.md`), original/prepared game data, evidence, logs and build outputs stay local and are excluded from Git. Once gameplay is established through milestone 1, resume XEX builds and Xenia testing. Gameplay implementation remains paused at the user's request.

Local development guides are maintained in `docs/PROJECT_DESCRIPTION.md`, `docs/CONFIGURATION_POLICY.md`, `docs/HANDOFF.md`, `docs/PORTING_SEQUENCE.md` and `docs/COVERAGE.md`. The local `docs/NEXT_SESSION.md` is the resume assignment. These files are available in the development workspace and are not included in the repository.

## Current boundary - reviewed 2026-10-27

Both adapters now use the owned native creation/Init prefix before generation. Actual session New Run stops at `PNR_NEED_NATIVE_INIT`, with `NormalRunFlow` still `NRF_NEED_FLOOR_CONTEXT`; the next required call is `0070a994 ->0076ea50` (appearance). It does not generate using projected health/resources. **Complete initialized players and whole gameplay ticks remain zero.**

The serial 128-seed host packet retains the separate generation/selected-room component checks and 64 frontend/session creation cases. Nested constructor owners, the default weapon factory/Init, numeric reset, can-shoot, indexed RNG banks, the single trinket97 draw and empty costume reset are integrated prefix work. Appearance, remaining Init/callbacks, reached grants/stats, placement/entry and whole update/collision remain required. The bounded creation ledger is maintained locally in `docs/PLAYER_CREATION.md`. No live combat, playable stage, dynamic Continue or current target playability is established.

## Development direction

The unit of accepted progress is an executable player capability through the shared session. The active milestone is **normal New Run to a fully initialized player completing movement/collision ticks in its generated starting room**. Component implementation and reference work are subordinate tasks inside that milestone, not replacement deliverables.

Do not extend the old pattern of closing another function or expanding another fixture matrix while leaving the same gameplay boundary. Complete the required producers, lifecycle and callers. Do not bypass missing behavior, invent defaults, repick supported rooms, remove original presentation or create a second gameplay implementation.

The maintained Windows entry point is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_runs.ps1 -Seeds 128
```

It builds and writes verification evidence. Audit/checkpoint scripts also write reports; they are not read-only review commands. Documentation-only work runs none of them. Current execution is resolved by `runtime/port/normal_flow.json`; documentation edits do not create a new verified execution or supersede frozen packets.

Use `src/` for maintained code, `runtime/game/`, `recovered/` and `evidence/` for prepared inputs/references, and read-only `boi360\game` when original source bytes are needed. A fresh clone needs the local game data and prepared inputs before builds or verification can run. One worker, no agents, serial single-core host work until gameplay is established; later XDK/Xenia work follows the limits in the local AGENTS.md. Never launch the original executable. No mods or online features. Workspace rules are maintained locally in `docs/WORKSPACE.md`.

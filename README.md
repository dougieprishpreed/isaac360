# The Binding of Isaac Rebirth Complete Edition for Xbox 360

`isaac360` brings **The Binding of Isaac Rebirth Complete Edition** (the Windows Steam version, Repentance v1.7.5) to the **Xbox 360**. It runs your own copy of the game's program, `isaac-ng.exe`, translated to the console ahead of time: every function of the game, its Lua 5.3 library and its C++ runtime library becomes C, compiled for the console's PowerPC CPU with the open [xsdk](https://github.com/xenosworks/xsdk) toolchain. What the game asks of Windows (files, threads, OpenGL, OpenAL, XInput, Steam) is answered by the port; its OpenGL drawing runs on the console's GPU through [vulkan4xenon](https://github.com/xenosworks/vulkan4xenon).

Because the game's own code runs, all of its content and behaviour comes along: every floor, item, enemy, boss, character, mode, cutscene and save. Nothing from the game is in this repository. You build the console files from your own copy of the game.

## Status

**Not yet playable on the console.** The port works on a PC, builds for the Xbox 360, and is in its first hardware tests.

- **On a PC**, the same translated code runs as a Windows program (`isaac360_recomp.exe`), drawing through the same Vulkan path the console uses:
  - It plays the intro, menus and runs, and is driven by test scripts.
  - An automated run went through all 31 floor variants and the special rooms. It played 8 cutscenes, wrote options and save files, and fought 19 bosses from Larry Jr. to Mom. Mom's death played the ending and credits as in the original. None of it crashed.
- **The console build** compiles: `default.xex` is about 97 MB. Three hardware boots so far, each a step further:
  - The first sat on its first frame because of slow file lookups on the USB drive.
  - The third loaded for 80 seconds, then returned to the dashboard. The game checks every archive's checksums at start, reading all 780 MB of them from the drive. Its render targets also share a depth buffer of another size, which the console's GPU driver refuses.
  - Both are fixed in the current build: start-up now reads about 10 MB.
- **Memory** is the console's main limit: it has 512 MB, and about 290 MB is left for the game once the program is loaded. Basement I went from about 300 MB to about 150 MB without changing how anything looks or sounds:
  - Textures reach the GPU only when first drawn. The rest wait compressed: the game loads far more than a screen shows.
  - Sound effects stay on the drive at their original quality until they play. The recently played stay in memory.
- **Shaders:** all 22 of the game's shader programs are compiled for the console's GPU. Each was checked against exact maths on the xenos toolchain's simulator.
- Mods are not supported yet.

## How it works

- `tools/recomp/analyze.py` finds every function in an image. `tools/recomp/translate.py` writes each one as a C function over the game's little-endian memory, which is byte-swapped on the big-endian console.
- `src/recomp/runtime.cpp` loads the images, runs threads and keeps the game's writes in a save folder; the game folder stays read-only.
- The `src/recomp/shims_*.cpp` files stand in for the Windows, C runtime, OpenAL, XInput and Steam functions the game imports.
- `src/recomp/gl_vk.cpp` is a small OpenGL state machine on Vulkan. The game's GLSL shaders are converted ahead of time by `tools/recomp/shaders.py` (to SPIR-V, then to Xenos microcode by xsdk's xost).
- `src/recomp/host_win32.cpp` and `src/recomp/host_xsdk.cpp` are the PC and console hosts. The console host adds the display, controllers, the audio feed, a watchdog and Back + Start to quit.
- Console data: `tools/recomp/console_sfx.py` moves the sound effects' samples into one file, `sfx.pcm`, read as sounds play. It rewrites the four archives that hold them to point there; every other file is used as the game ships it.
- `tools/recomp/translate.py` has one patch to the game's code: the start-up archive checksum pass, which takes 80 seconds on a USB drive, accepts each entry's stored checksum.

The earlier hand-written port (`src/game/`, `src/app/`, `src/render/`, `src/platform/`, `src/simulation/`) stays in the repository; the console build no longer uses it.

## Requirements

- Your copy of the Windows Steam game in `game/` at the repository root (read-only; `isaac-ng.exe`, `Lua5.3.3r.dll`, `resources/`).
- Windows with Visual Studio 2022 Build Tools, LLVM (clang-cl), CMake, Ninja and a Vulkan driver.
- Python 3, and a virtual environment `tools/.venv` with `iced-x86` and `pefile`.
- For the console: WSL (or Linux) with xsdk, vulkan4xenon and glslang-xenon built (see xsdk's `docs/GETTING_STARTED.md`).

## Building

From the repository root:

1. Rebase the two DLLs to their guest addresses:

   ```powershell
   tools\.venv\Scripts\python tools\recomp\rebase.py game\Lua5.3.3r.dll 0x00C60000
   ```

   ```powershell
   tools\.venv\Scripts\python tools\recomp\rebase.py C:\Windows\SysWOW64\msvcp140.dll 0x00CA0000
   ```

2. Analyse and translate the three images. Run the pair of commands for each image: the game itself (no variables), then with `ISAAC360_IMAGE=build/recomp/dlls/lua5.3.3r.dll ISAAC360_TAG=lua`, then with `ISAAC360_IMAGE=build/recomp/dlls/msvcp140.dll ISAAC360_TAG=msvcp`:

   ```powershell
   tools\.venv\Scripts\python tools\recomp\analyze.py
   ```

   ```powershell
   tools\.venv\Scripts\python tools\recomp\translate.py
   ```

3. Convert the shaders (they come from the game's archives, extracted by `tools/extract_resources.py`):

   ```powershell
   tools\.venv\Scripts\python tools\recomp\shaders.py
   ```

4. The PC program, `build\recomp\isaac360_recomp.exe`:

   ```powershell
   cmd /c tools\recomp\build_win.cmd
   ```

5. The console's sound archives, in `build\console_data`:

   ```powershell
   py -3 tools\recomp\console_sfx.py
   ```

6. The console's `default.xex`, in WSL. The xsdk and vulkan4xenon checkouts get the patches in `tools/xsdk/patches` (a 1920x1080 display on a 1080p console) and their libraries are built first (`tools/xsdk/deps.sh`), so give it checkouts of its own, side by side:

   ```sh
   XSDK=/path/to/deps/xsdk VULKAN4XENON=/path/to/deps/vulkan4xenon tools/xsdk/build_recomp.sh
   ```

7. Stage the folder for the USB drive:

   ```powershell
   py -3 tools\stage_console.py --out E:\Apps\isaac360
   ```

## Playing on the console

Copy the staged folder to the console's drive and start `default.xex` from the dashboard of a console that runs homebrew (such as with ABadAvatar). The controls are the game's own. Hold Back + Start for two seconds to return to the dashboard.

The folder holds:
- `default.xex`
- `game\` (your game files, with the converted sound archives and `sfx.pcm`)
- `dlls\`
- `save\` (saves and settings)
- `isaac360_args.txt`: `--mem MB` sets the game's address space; a line `ISAAC360_DISPLAY=720p` keeps the picture at 1280x720 on a 1080p console; a line `ISAAC360_DEADZONE=N` sets the sticks' dead zone in percent (default 24, XInput's own; 100 turns the sticks off and leaves the d-pad); a line `ISAAC360_SWAP_IMAGES=2` shows each frame one refresh sooner where the game keeps up (default 3, which lets the GPU work on one frame while the next is made)

On a console set to 1080p the game shows a 1920x1080 picture with each of its pixels as an exact 2x2 block, as the PC version does at 1920x1080. On other video settings it draws 1280x720, which the console scales to the screen.

Each session writes `isaac360.log` and `default.log` beside the XEX. They show how far loading got and how much memory is free, so copy them off before ejecting the drive.

## Testing on a PC

The PC program runs hidden and muted for tests:

```powershell
build\recomp\isaac360_recomp.exe --hidden --vk --mem 320 --data-overlay build/console_data
```

- `--vk` draws through the console's Vulkan path, `--data-overlay` uses the console's sound archives and `--mem 320` gives it the console's memory size.
- `ISAAC360_PAD` scripts the controller, `ISAAC360_SHOTS` saves frames and `ISAAC360_TRACE_FILES` and `ISAAC360_TRACE_PRINTF` log file opens and the game's own log lines. `ISAAC360_TRACE_MUSIC=1` logs each music stream's starts, stops and underruns.
- A Lua script that logs `audit: <what>` (through `Isaac.DebugString`) splits the run into windows: each gets a log line with its frames' game time and GPU time, average and worst, for finding effects that cost more than a room's usual frame.
- `ISAAC360_POOL_CHECK=1` and `ISAAC360_IMAGE_CHECK=1` compare reused room textures and cached images with fresh decodes; `ISAAC360_PLAIN_SPRITES=0` and `ISAAC360_RELOAD_POOL=0` turn off the plain sprite shader and the texture reuse for comparisons.
- `py -3 tools\recomp\coverage.py` plays a run through every floor, boss room, special room and cutscene with the game's debug commands, hidden, and reports memory and errors.
- `tools/recomp/check_shaders.py` (in WSL) runs each converted shader on the xenos toolchain's simulator against exact maths, on inputs recorded by `coverage.py`.

## Acknowledgements

- xsdk and vulkan4xenon (xenosworks): the toolchain, the console runtime and the Vulkan driver, plus xost and its simulator.
- iced-x86 and pefile: instruction decoding and PE images.
- The Khronos OpenGL registry and Vulkan headers.

The staged folder is made from your copy of the game: keep it for yourself and don't share it.

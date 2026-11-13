# The Binding of Isaac Rebirth Complete Edition for Xbox 360

`isaac360` lets you play **The Binding of Isaac Rebirth Complete Edition** on an **Xbox 360**. It runs the game's own program from your Windows Steam copy (Repentance v1.7.5), translated ahead of time for the console's PowerPC CPU. All of the content and behaviour is the original: every floor, item, enemy, boss, character, challenge, cutscene, ending and save.

Nothing from the game is included here. You build the console files from your own copy of the game.

## Status

**Alpha: playable, with performance still being worked on.**

- Whole runs play on the console, from the title screen through the floors, shops, special rooms and bosses, with saving and continuing.
- The picture matches the PC version, including the game's Filter option. On a 1080p console each game pixel is an exact 2x2 block.
- Sound effects are at their original quality. Music streams from the game's archives.

## What you need

- An Xbox 360 that runs homebrew (such as with ABadAvatar), a controller, and a USB drive with about 1.2 GB free.
- Your copy of **The Binding of Isaac: Rebirth** with Afterbirth, Afterbirth+ and Repentance, the Windows Steam version at **Repentance v1.7.5**. Other versions of `isaac-ng.exe` are not supported.
- A Windows PC to build on, with:
  - Visual Studio 2022 Build Tools, LLVM (clang-cl), CMake and Ninja;
  - Python 3 with a virtual environment in `tools/.venv` holding `iced-x86` and `pefile`;
  - WSL (or Linux) with the xsdk toolchain, vulkan4xenon and glslang built (see xsdk's `docs/GETTING_STARTED.md`).

## Building

Copy your game folder (the one with `isaac-ng.exe`, `Lua5.3.3r.dll` and `resources\`) to `game\` in this repository; it is only read. Then, from the repository root:

1. Rebase the two DLLs to the addresses the translation uses:

   ```powershell
   tools\.venv\Scripts\python tools\recomp\rebase.py game\Lua5.3.3r.dll 0x00C60000
   ```

   ```powershell
   tools\.venv\Scripts\python tools\recomp\rebase.py C:\Windows\SysWOW64\msvcp140.dll 0x00CA0000
   ```

2. Analyse and translate the game's program (this takes 20 to 30 minutes):

   ```powershell
   tools\.venv\Scripts\python tools\recomp\analyze.py; tools\.venv\Scripts\python tools\recomp\translate.py
   ```

   Then its Lua library:

   ```powershell
   $env:ISAAC360_IMAGE='build/recomp/dlls/lua5.3.3r.dll'; $env:ISAAC360_TAG='lua'; tools\.venv\Scripts\python tools\recomp\analyze.py; tools\.venv\Scripts\python tools\recomp\translate.py
   ```

   Then its C++ library, and clear the two settings again:

   ```powershell
   $env:ISAAC360_IMAGE='build/recomp/dlls/msvcp140.dll'; $env:ISAAC360_TAG='msvcp'; tools\.venv\Scripts\python tools\recomp\analyze.py; tools\.venv\Scripts\python tools\recomp\translate.py; Remove-Item Env:ISAAC360_IMAGE, Env:ISAAC360_TAG
   ```

3. Convert the game's shaders, which are read from its archives and its program. This runs glslang in WSL: set `ISAAC360_WSL_DISTRO` to the name of your WSL distribution and `GLSLANG` to the glslang program inside it first.

   ```powershell
   tools\.venv\Scripts\python tools\recomp\shaders.py
   ```

4. Prepare the console's sound archives (written to `build\console_data`):

   ```powershell
   tools\.venv\Scripts\python tools\recomp\console_sfx.py
   ```

5. Build `default.xex` in WSL, from the repository folder. The build applies the patches in `tools/xsdk/patches` (the 1920x1080 display) to the xsdk and vulkan4xenon checkouts and builds their libraries (`tools/xsdk/deps.sh`), so give it checkouts of its own, side by side. `XSDK_TOOLCHAIN` is where the xsdk toolchain is installed, `GLSLANG` the glslang program, and `ISAAC_XSDK_BUILD` a folder for the build's objects (by default `/opt/isaac360-xsdk/recomp`). It takes 30 to 60 minutes and writes `build/console/recomp/default.xex`.

   ```sh
   XSDK=/path/to/deps/xsdk VULKAN4XENON=/path/to/deps/vulkan4xenon XSDK_TOOLCHAIN=/path/to/toolchain GLSLANG=/path/to/glslang tools/xsdk/build_recomp.sh
   ```

6. Stage the folder on your USB drive:

   ```powershell
   tools\.venv\Scripts\python tools\stage_console.py --out E:\Apps\isaac360
   ```

To try the same build on a PC first, `cmd /c tools\recomp\build_win.cmd` makes `build\recomp\isaac360_recomp.exe`.

## Playing

Start `default.xex` from the dashboard. The controls are the game's own: left stick or d-pad to move, right stick or face buttons to shoot. Hold **Back + Start** for two seconds to return to the dashboard.

The staged folder holds:

- `default.xex`;
- `game\`: your game files, with the sound archives converted for the console;
- `dlls\`: the game's runtime libraries, prepared for the console;
- `save\`: your saves and the game's options;
- `isaac360_args.txt`: settings, one per line or separated by spaces:
  - `--mem 320`: the memory the game gets (leave it as it is);
  - `ISAAC360_DEADZONE=24`: the sticks' dead zone in percent; raise it if a worn stick makes Isaac drift, and 100 turns the sticks off (the d-pad still works);
  - `ISAAC360_VOLUME=100`: the overall volume in percent, on top of the game's own sound options;
  - `ISAAC360_DISPLAY=720p`: a 1280x720 picture on a console set to 1080p;
  - `ISAAC360_SWAP_IMAGES=2`: each frame a refresh sooner where the game keeps up (the default, 3, is smoother in busy moments).

On a console set to 1080p the game shows a 1920x1080 picture. On other video settings it draws 1280x720, which the console scales to the screen.

Each session writes `isaac360.log` and `default.log` beside the XEX. If something goes wrong, copy them off before ejecting the drive: they show what the game was doing.

## How it works

The tools in `tools/recomp` find every function in the game's program and its two libraries and write each one as C. That code is compiled for the console with the xsdk toolchain. The port answers what the game asks of Windows: files, threads, timers, OpenGL, OpenAL, XInput and Steam. The game's OpenGL drawing runs on the console's GPU through vulkan4xenon, and its shaders are converted ahead of time. Because the game's own code runs, gameplay is as on the PC.

## Acknowledgements

- xsdk and vulkan4xenon (xenosworks): the toolchain, the console runtime and the Vulkan driver, plus xost and its simulator.
- iced-x86 and pefile: instruction decoding and PE images.
- The Khronos OpenGL registry and Vulkan headers.

The staged folder is made from your copy of the game: keep it for yourself and don't share it.

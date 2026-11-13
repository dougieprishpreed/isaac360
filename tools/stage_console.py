"""Stages the console build of the recompiled game (docs/RECOMP_PLAN.md) in one folder for the USB drive.

The console runs the user's own isaac-ng.exe, recompiled: it needs the game's files beside the XEX as the PC
version has them, with the archives that hold sound effects replaced by the console's copies (each effect's
samples moved to sfx.pcm and read when it plays, tools/recomp/console_sfx.py; the console has 512 MB for
everything), and the game's runtime DLLs rebased (tools/recomp/rebase.py). Layout:
  default.xex            the game (tools/xsdk/build_recomp.sh)
  game/isaac-ng.exe      the game's image (its data sections are read at start-up)
  game/resources/...     resources/packed/*.a (four converted), packed/sfx.pcm and resources/scripts/
  dlls/                  lua5.3.3r.dll and msvcp140.dll, rebased
  save/                  the game's saves and anything it writes beside itself
  isaac360_args.txt        options (--mem MB: the guest address space; NAME=value: a setting the PC takes from
                         the environment, e.g. ISAAC360_SHOTS=600,1200 for screenshots beside the XEX)

    py -3 tools/stage_console.py [--xex build/console/recomp/default.xex] [--out build/console/isaac360]

Copy the output folder to the console's drive and start default.xex from the dashboard of a console that runs
homebrew (such as with ABadAvatar). Never share the output: it is made from your copy of the game.
"""
import argparse
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'game')
CONVERTED = os.path.join(ROOT, 'build', 'console_data')
DLLS = os.path.join(ROOT, 'build', 'recomp', 'dlls')
CONVERTED_ARCHIVES = ['sfx.a', 'afterbirth.a', 'afterbirthp.a', 'repentance.a']


def copy_if_changed(src, dst):
    if os.path.isfile(dst) and os.path.getsize(dst) == os.path.getsize(src) and \
            int(os.path.getmtime(dst)) >= int(os.path.getmtime(src)):
        return False
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    return True


def stage_game(out):
    """The files the game reads: its image and resources/ (with the converted archives)."""
    files = [('isaac-ng.exe', os.path.join(GAME, 'isaac-ng.exe'))]
    for folder, _, names in os.walk(os.path.join(GAME, 'resources')):
        for n in names:
            path = os.path.join(folder, n)
            rel = os.path.relpath(path, GAME).replace('\\', '/')
            files.append((rel, path))
    # the effects' samples, which the converted archives refer to (console_sfx.py)
    files.append(('resources/packed/sfx.pcm', os.path.join(CONVERTED, 'resources', 'packed', 'sfx.pcm')))
    copied = total = 0
    for rel, path in files:
        if rel.startswith('resources/packed/') and os.path.basename(rel) in CONVERTED_ARCHIVES:
            path = os.path.join(CONVERTED, rel)
            if not os.path.isfile(path):
                raise SystemExit('missing %s: run tools/recomp/console_sfx.py' % path)
        total += os.path.getsize(path)
        copied += copy_if_changed(path, os.path.join(out, 'game', rel))
    return len(files), copied, total


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--xex', default=os.path.join(ROOT, 'build', 'console', 'recomp', 'default.xex'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'console', 'isaac360'))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    count, copied, total = stage_game(args.out)
    print('game/: %d files, %.1f MB (%d copied)' % (count, total / 1048576.0, copied))
    for dll in ('lua5.3.3r.dll', 'msvcp140.dll'):
        src = os.path.join(DLLS, dll)
        if not os.path.isfile(src):
            raise SystemExit('missing %s: run tools/recomp/rebase.py (docs/RECOMP_PLAN.md)' % src)
        copy_if_changed(src, os.path.join(args.out, 'dlls', dll))
    if os.path.isfile(args.xex):
        shutil.copy2(args.xex, os.path.join(args.out, 'default.xex'))
        print('default.xex: %.1f MB' % (os.path.getsize(args.xex) / 1048576.0))
    else:
        print('no XEX at %s (tools/xsdk/build_recomp.sh builds it)' % args.xex, file=sys.stderr)
    licenses = os.path.join(os.path.dirname(args.xex), 'licenses')
    if os.path.isdir(licenses):
        shutil.copytree(licenses, os.path.join(args.out, 'licenses'), dirs_exist_ok=True)
    os.makedirs(os.path.join(args.out, 'save'), exist_ok=True)
    args_file = os.path.join(args.out, 'isaac360_args.txt')
    if not os.path.isfile(args_file):
        with open(args_file, 'w', newline='\r\n') as f:
            f.write('--mem 320\n')
    with open(os.path.join(args.out, 'README.txt'), 'w', newline='\r\n') as f:
        f.write(README)
    print('staged', os.path.abspath(args.out))


README = '''The Binding of Isaac: Rebirth Complete Edition for Xbox 360 (isaac360) - console test build

Copy this whole folder to the console's drive (for example E:\\Games\\isaac360) and start
default.xex from the dashboard of a console that runs homebrew (such as with ABadAvatar).

Files
  default.xex        the game
  game\\              your copy of the game's files (do not share them); the archives that
                     hold sound effects are converted for the console's memory
  dlls\\              the game's runtime libraries, prepared for the console
  save\\              saves and the game's settings are written here
  isaac360.log         written each session: copy it off before ejecting the drive
  isaac360_args.txt    options: --mem MB (address space for the game); a line ISAAC360_DISPLAY=720p
                     keeps the picture at 1280x720 when the console is set to 1080p; a line
                     ISAAC360_DEADZONE=N sets the sticks' dead zone in percent (default 24; 100
                     turns the sticks off, the d-pad still moves); a line ISAAC360_SWAP_IMAGES=2
                     shows each frame a refresh sooner where the game keeps up (default 3)
  licenses\\          licences of the toolchain the build uses

Controls: the game's own controller layout (left stick moves, right stick fires).
Hold Back + Start for two seconds to return to the dashboard.
'''


if __name__ == '__main__':
    main()

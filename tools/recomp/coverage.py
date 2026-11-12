"""PC coverage run of the recompiled game (docs/RECOMP_PLAN.md): hidden, muted and self-driven.

A fresh save folder (build/recomp/save_cov) gets the test-only script tools/recomp/coverage_mod/main.lua appended to an
overlay copy of the game's resources/scripts/main.lua,
which walks a run through every floor and its variants, boss and special rooms and cutscenes with the game's debug
commands; a pad script starts the run from the menus. The run uses the console's data (build/console_data) and its
guest memory size, so the heap and image totals in the frame lines are the console's budget. Also records the shader
inputs tools/recomp/check_shaders.py replays (ISAAC360_SHADER_SAMPLES).

    py -3 tools/recomp/coverage.py [--minutes 25] [--mem 320] [--shots 900]

Afterwards: build/recomp/cov.log (host log with the game's lines, "cov:" the script's),
build/recomp/cov_shots/*.png, build/recomp/shader_samples.txt.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, 'build', 'recomp', 'isaac360_recomp.exe')
SAVE = os.path.join(ROOT, 'build', 'recomp', 'save_cov')
LOG = os.path.join(ROOT, 'build', 'recomp', 'cov.log')
SHOTS = os.path.join(ROOT, 'build', 'recomp', 'cov_shots')
GAME_LOG = os.path.join(SAVE, 'My Games', 'Binding of Isaac Repentance', 'log.txt')
MENU_PAD = '500:504:E;700:704:E;900:904:E;1100:1104:E;1300:1304:E;1500:1504:E;1700:1704:E;2000:2060:A'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=25)
    ap.add_argument('--mem', type=int, default=320)
    ap.add_argument('--shots', type=int, default=900, help='a screenshot every N frames (0: none)')
    ap.add_argument('--mod', default='main', help='the script in tools/recomp/coverage_mod: main (the walk) or '
                    'sound_stress (sound channels, music, cutscenes and restarts against the sound thread)')
    ap.add_argument('--heap-check', action='store_true', help='ISAAC360_HEAP_CHECK=1: guard bytes and held frees')
    args = ap.parse_args()
    if os.path.isdir(SAVE):
        shutil.rmtree(SAVE)
    # the script rides on the game's own main.lua (overlay copy, the game's file plus it): the game reads mods'
    # folders but not yet their files (mods are a later phase)
    scripts = os.path.join(SAVE, 'overlay', 'resources', 'scripts')
    os.makedirs(scripts)
    with open(os.path.join(scripts, 'main.lua'), 'wb') as f:
        f.write(open(os.path.join(ROOT, 'game', 'resources', 'scripts', 'main.lua'), 'rb').read())
        f.write(b'\n\n' + open(os.path.join(ROOT, 'tools', 'recomp', 'coverage_mod', args.mod + '.lua'), 'rb').read())
    for old in glob.glob(os.path.join(ROOT, 'build', 'recomp', 'shot_*.bmp')):
        os.remove(old)
    # the game writes log.txt only when it exits cleanly: its lines (and the script's "cov:") come from the printf
    # trace in the host log
    env = dict(os.environ, ISAAC360_PAD=MENU_PAD, ISAAC360_SHADER_SAMPLES='1', ISAAC360_GL_STATS='1',
               ISAAC360_TRACE_PRINTF='400000', ISAAC360_TRACE_PRINTF_ONLY='cov:,ERROR,Error,error,warn,Lua,ASSERT')
    if args.shots:
        env['ISAAC360_SHOTS'] = ','.join(str(f) for f in range(args.shots, 200000, args.shots))
    if args.heap_check:
        env['ISAAC360_HEAP_CHECK'] = '1'
    cmd = [EXE, '--hidden', '--vk', '--mem', str(args.mem), '--data-overlay', 'build/console_data',
           '--save', 'build/recomp/save_cov', '--log', 'build/recomp/cov.log']
    t0 = time.time()
    p = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    done = False
    while p.poll() is None and time.time() - t0 < args.minutes * 60:
        time.sleep(5)
        if os.path.isfile(LOG) and 'cov: done' in tail(LOG):
            done = True
            time.sleep(5)
            break
    if p.poll() is None:
        p.kill()
        p.wait()
    print('ran %.0f s, exit %s, %s' % (time.time() - t0, p.returncode, 'finished the steps' if done else 'NOT finished'))
    summarize()


def tail(path, n=1 << 16):
    with open(path, 'rb') as f:
        f.seek(0, 2)
        f.seek(max(0, f.tell() - n))
        return f.read().decode('latin-1')


def summarize():
    host = open(LOG, errors='replace').read().splitlines() if os.path.isfile(LOG) else []
    frames = [l for l in host if l.startswith('frame ')]
    peak_heap = max([int(m.group(1)) for m in (re.search(r'heap (\d+) MB', l) for l in frames) if m] or [0])
    peak_img = max([int(m.group(1)) for m in (re.search(r'images (\d+) MB', l) for l in frames) if m] or [0])
    print('host log: %d lines; %s; peak heap %d MB, images %d MB' % (len(host), frames[-1] if frames else 'no frames',
                                                                     peak_heap, peak_img))
    for l in host:
        if ('FATAL' in l or 'unhandled' in l.lower() or 'exception' in l.lower() or l.startswith('heap check')
                or l.startswith('host fault') or 'free of a bad block' in l):
            print('  ' + l)
    game = [l[8:] for l in host if l.startswith('printf> ')]
    cov = [l for l in game if 'cov:' in l]
    errs = [l for l in game if '[ERROR]' in l or '[ASSERT]' in l or 'Lua Error' in l or 'error' in l.lower()
            and '/' not in l]
    print('game lines: %d, %d cov lines, %d error lines' % (len(game), len(cov), len(errs)))
    for l in cov[-3:]:
        print('  ' + l)
    for l in errs[:15]:
        print('  ERR ' + l)
    os.makedirs(SHOTS, exist_ok=True)
    try:
        from PIL import Image
    except ImportError:
        return
    for f in sorted(glob.glob(os.path.join(ROOT, 'build', 'recomp', 'shot_*.bmp'))):
        Image.open(f).convert('RGB').resize((320, 180)).save(
            os.path.join(SHOTS, os.path.basename(f)[:-4] + '.png'))
        os.remove(f)


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--summary':
        summarize()
    else:
        main()

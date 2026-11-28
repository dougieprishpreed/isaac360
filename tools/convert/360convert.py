"""360convert: prepares your Steam copy of The Binding of Isaac: Rebirth (Repentance+ v1.9.7.17) for the Xbox 360 port.

Run it from the folder that holds (or will hold) the isaac360 folder. It asks for isaac-ng.exe in your Steam game folder
(a file open dialog), then writes into ./isaac360, making the folder if there is none:
  game/isaac-ng.exe      the game's program (the console reads its data sections at start-up)
  game/resources/...     the game's resources, with the four archives that hold sound effects converted: each
                         effect's samples move to resources/packed/sfx.pcm and are read when it plays (the console has
                         512 MB for everything), and the Womb's L room floor PNG is widened to 8 bits
                         (tools/recomp/console_sfx.py)
  dlls/                  Lua5.3.3r.dll (from the game) and msvcp140.dll (from Windows), rebased to the addresses the
                         translation uses (tools/recomp/rebase.py); an msvcp140.dll already there is used as it is
                         (the one the XEX was translated from, build/recomp/dlls, can be put there first, and the
                         folder made on another PC), a Lua5.3.3r.dll already there is kept (rebased if it is a
                         plain copy)
  save/                  where the game writes its saves
  isaac360_args.txt      options (written once; yours are kept)
  README.txt
Add your default.xex (README.md, "Building") and copy the folder to the console's drive.

    360convert.exe [path to isaac-ng.exe or the game folder] [--no-pause]

The exe is built by tools/convert/build_360convert.cmd (PyInstaller, Python's standard library only).
"""
import ctypes
import os
import shutil
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'recomp'))
sys.path.insert(0, os.path.join(HERE, '..'))
import console_sfx  # noqa: E402
import stage_console  # noqa: E402

LUA_BASE = 0x00D40000                               # README.md, "Building" step 1
MSVCP_BASE = 0x00D80000


def choose_game():
    """isaac-ng.exe's path from a file open dialog, or None."""
    class OPENFILENAMEW(ctypes.Structure):
        _fields_ = [('lStructSize', ctypes.c_uint32), ('hwndOwner', ctypes.c_void_p), ('hInstance', ctypes.c_void_p),
                    ('lpstrFilter', ctypes.c_wchar_p), ('lpstrCustomFilter', ctypes.c_wchar_p),
                    ('nMaxCustFilter', ctypes.c_uint32), ('nFilterIndex', ctypes.c_uint32),
                    ('lpstrFile', ctypes.c_wchar_p), ('nMaxFile', ctypes.c_uint32),
                    ('lpstrFileTitle', ctypes.c_wchar_p), ('nMaxFileTitle', ctypes.c_uint32),
                    ('lpstrInitialDir', ctypes.c_wchar_p), ('lpstrTitle', ctypes.c_wchar_p),
                    ('Flags', ctypes.c_uint32), ('nFileOffset', ctypes.c_uint16), ('nFileExtension', ctypes.c_uint16),
                    ('lpstrDefExt', ctypes.c_wchar_p), ('lCustData', ctypes.c_void_p), ('lpfnHook', ctypes.c_void_p),
                    ('lpTemplateName', ctypes.c_wchar_p), ('pvReserved', ctypes.c_void_p),
                    ('dwReserved', ctypes.c_uint32), ('FlagsEx', ctypes.c_uint32)]
    buf = ctypes.create_unicode_buffer(1024)
    ofn = OPENFILENAMEW()
    ofn.lStructSize = ctypes.sizeof(OPENFILENAMEW)
    ofn.lpstrFilter = 'The Binding of Isaac (isaac-ng.exe)\0isaac-ng.exe\0All files\0*.*\0\0'
    ofn.nFilterIndex = 1
    ofn.lpstrFile = ctypes.cast(buf, ctypes.c_wchar_p)
    ofn.nMaxFile = len(buf)
    steam = r'C:\Program Files (x86)\Steam\steamapps\common\The Binding of Isaac Rebirth'
    if os.path.isdir(steam):
        ofn.lpstrInitialDir = steam
    ofn.lpstrTitle = 'Select isaac-ng.exe in your Steam game folder (The Binding of Isaac Rebirth)'
    ofn.Flags = 0x00001000 | 0x00000800 | 0x00080000 | 0x00000008   # file and path must exist, explorer, keep cwd
    if not ctypes.windll.comdlg32.GetOpenFileNameW(ctypes.byref(ofn)):
        return None
    return buf.value


def rebase(src, dst, base):
    """The 32-bit DLL at src, rebased to base (each HIGHLOW relocation gets the difference), written to dst."""
    data = bytearray(open(src, 'rb').read())
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise SystemExit('%s is not a Windows DLL' % src)
    sections, opt_size = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from('<H', data, opt)[0] != 0x10b:
        raise SystemExit('%s is not a 32-bit DLL' % src)
    old = struct.unpack_from('<I', data, opt + 28)[0]
    reloc_rva, reloc_size = struct.unpack_from('<II', data, opt + 96 + 5 * 8)
    table = []
    for i in range(sections):
        s = opt + opt_size + 40 * i
        vsize, va, raw, ptr = struct.unpack_from('<IIII', data, s + 8)
        table.append((va, max(vsize, raw), ptr))

    def offset(rva):
        for va, size, ptr in table:
            if va <= rva < va + size:
                return rva - va + ptr
        raise SystemExit('%s: address %08x is in no section' % (src, rva))
    delta = (base - old) & 0xffffffff
    at, end, n = offset(reloc_rva), offset(reloc_rva) + reloc_size, 0
    while at + 8 <= end:
        page, size = struct.unpack_from('<II', data, at)
        if size < 8:
            break
        for k in range(at + 8, at + size, 2):
            e = struct.unpack_from('<H', data, k)[0]
            if e >> 12 == 3:
                o = offset(page + (e & 0xfff))
                struct.pack_into('<I', data, o, (struct.unpack_from('<I', data, o)[0] + delta) & 0xffffffff)
                n += 1
        at += size
    struct.pack_into('<I', data, opt + 28, base)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    console_sfx.write_if_changed(dst, bytes(data))
    print('  %s: %d relocations, now at 0x%08x' % (os.path.basename(dst), n, base))


def copy_game(game, out):
    """isaac-ng.exe and resources/, but for the archives console_sfx.py converts."""
    files = [('isaac-ng.exe', os.path.join(game, 'isaac-ng.exe'))]
    for folder, _, names in os.walk(os.path.join(game, 'resources')):
        for n in names:
            path = os.path.join(folder, n)
            files.append((os.path.relpath(path, game).replace('\\', '/'), path))
    files = [(rel, path) for rel, path in files
             if not (rel.startswith('resources/packed/') and os.path.basename(rel) in console_sfx.ARCHIVES)]
    copied = total = 0
    for rel, path in files:
        total += os.path.getsize(path)
        copied += stage_console.copy_if_changed(path, os.path.join(out, 'game', rel))
    print('  %d files, %.1f MB (%d copied)' % (len(files), total / 1048576.0, copied))


def image_base(path):
    data = open(path, 'rb').read(4096)
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    return struct.unpack_from('<I', data, pe + 24 + 28)[0]


def prepare_dll(src, dst, base):
    """A DLL already in dlls/ is kept (the copy the XEX was translated from: build/recomp/dlls), rebased in place if it
    is a plain copy; otherwise src, rebased."""
    name = os.path.basename(dst)
    if os.path.isfile(dst):
        if image_base(dst) == base:
            print('  %s: kept (already in dlls)' % name)
            return
        print('  %s: already in dlls, not yet rebased' % name)
        src = dst
    rebase(src, dst, base)


def convert(game, out):
    for need in ('isaac-ng.exe', 'Lua5.3.3r.dll', os.path.join('resources', 'packed', 'sfx.a'),
                 os.path.join('tools', 'ResourceExtractor', 'filelist.txt')):
        if not os.path.isfile(os.path.join(game, need)):
            raise SystemExit('%s has no %s: choose isaac-ng.exe in the Steam folder of The Binding of Isaac: Rebirth '
                             'with Repentance+ (v1.9.7.17)' % (game, need))
    msvcp = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'SysWOW64', 'msvcp140.dll')
    if not os.path.isfile(os.path.join(out, 'dlls', 'msvcp140.dll')) and not os.path.isfile(msvcp):
        raise SystemExit('%s is missing: install the Microsoft Visual C++ 2015-2022 Redistributable (x86), or put the '
                         'msvcp140.dll your build made (build\\recomp\\dlls) in isaac360\\dlls' % msvcp)
    t0 = time.time()
    print('Game folder: %s' % game)
    print('Output: %s' % out)
    os.makedirs(out, exist_ok=True)
    print('Copying the game files...')
    copy_game(game, out)
    print('Converting the sound archives (a few minutes)...')
    console_sfx.convert(game, os.path.join(out, 'game', 'resources', 'packed'))
    print('Preparing the runtime libraries...')
    prepare_dll(os.path.join(game, 'Lua5.3.3r.dll'), os.path.join(out, 'dlls', 'lua5.3.3r.dll'), LUA_BASE)
    msvcp_out = os.path.join(out, 'dlls', 'msvcp140.dll')
    if os.path.isfile(msvcp_out):                   # one already in dlls is used as it is
        print('  msvcp140.dll: already in dlls, skipped')
    else:
        rebase(msvcp, msvcp_out, MSVCP_BASE)
    os.makedirs(os.path.join(out, 'save'), exist_ok=True)
    args_file = os.path.join(out, 'isaac360_args.txt')
    if not os.path.isfile(args_file):
        with open(args_file, 'w', newline='\r\n') as f:
            f.write('--mem 320\n')
    with open(os.path.join(out, 'README.txt'), 'w', newline='\r\n') as f:
        f.write(stage_console.README)
    print('Done in %.0f s.' % (time.time() - t0))
    if not os.path.isfile(os.path.join(out, 'default.xex')):
        print('Now put your default.xex in %s, then copy the folder to your Xbox 360\'s drive.' % out)
    else:
        print('Copy %s to your Xbox 360\'s drive.' % out)


def main():
    argv = [a for a in sys.argv[1:] if a != '--no-pause']
    pause = '--no-pause' not in sys.argv[1:]
    print('360convert: The Binding of Isaac: Rebirth (Repentance+ v1.9.7.17) for isaac360')
    print('Your game files stay yours: never share the isaac360 folder.\n')
    code = 0
    try:
        out = os.path.join(os.getcwd(), 'isaac360')
        path = argv[0] if argv else choose_game()
        if not path:
            print('No game chosen.')
            code = 1
        else:
            game = path if os.path.isdir(path) else os.path.dirname(path)
            convert(os.path.abspath(game), out)
    except SystemExit as e:
        if e.code not in (None, 0):
            print('\nError: %s' % e.code)
            code = 1
    except Exception as e:                          # shown, not a vanished window
        print('\nError: %r' % e)
        code = 1
    if pause:
        try:
            input('\nPress Enter to close.')
        except EOFError:
            pass
    sys.exit(code)


if __name__ == '__main__':
    main()

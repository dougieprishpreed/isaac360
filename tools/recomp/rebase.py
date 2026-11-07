"""Copies a 32-bit DLL to build/recomp/dlls/ rebased to a free guest address (docs/RECOMP_PLAN.md).

The recompiled code has the image's addresses built in, so a DLL whose preferred base collides with another one
(msvcp140.dll and Lua5.3.3r.dll both ask for 0x10000000) is rebased once, before analysis and translation, and
the runtime loads the same rebased copy.

    tools/.venv/Scripts/python tools/recomp/rebase.py C:/Windows/SysWOW64/msvcp140.dll 0x10200000
"""
import os
import sys

import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    src, base = sys.argv[1], int(sys.argv[2], 0)
    pe = pefile.PE(src)
    data = bytearray(open(src, 'rb').read())
    delta = (base - pe.OPTIONAL_HEADER.ImageBase) & 0xffffffff
    # each HIGHLOW relocation site gets the delta (pefile's relocate_image damages this DLL's import table)
    n = 0
    for block in pe.DIRECTORY_ENTRY_BASERELOC:
        for e in block.entries:
            if e.type != 3:
                continue
            off = pe.get_offset_from_rva(e.rva)
            v = int.from_bytes(data[off:off + 4], 'little')
            data[off:off + 4] = ((v + delta) & 0xffffffff).to_bytes(4, 'little')
            n += 1
    field = pe.OPTIONAL_HEADER.get_field_absolute_offset('ImageBase')
    data[field:field + 4] = base.to_bytes(4, 'little')
    out = os.path.join(ROOT, 'build', 'recomp', 'dlls')
    os.makedirs(out, exist_ok=True)
    dst = os.path.join(out, os.path.basename(src).lower())
    open(dst, 'wb').write(bytes(data))
    print('%d relocations applied' % n)
    print('%s -> %s at 0x%08x (%u bytes of image)' % (src, dst, base, pe.OPTIONAL_HEADER.SizeOfImage))


if __name__ == '__main__':
    main()

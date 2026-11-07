"""Writes each shader of src/render/shaders/spirv.inc as <name>.<stage>.spv (little-endian words), the input
vulkan4xenon's tools/build_shaders.py bundles: the bundle then holds the very words the renderer passes to
vkCreateShaderModule, so the console finds every shader compiled and compiles none at start-up.

    python3 tools/xsdk/spirv_files.py src/render/shaders/spirv.inc <folder>
"""
import os
import re
import struct
import sys


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    text = open(src, encoding='ascii').read()
    names = []
    for m in re.finditer(r'static const u32 SPV_(\w+)_(vert|frag)\[\d+\] = \{([^}]*)\};', text):
        words = [int(w, 16) for w in re.findall(r'0x([0-9a-fA-F]+)', m.group(3))]
        name = '%s.%s.spv' % (m.group(1), m.group(2))
        data = struct.pack('<%dI' % len(words), *words)
        path = os.path.join(out, name)
        if not os.path.exists(path) or open(path, 'rb').read() != data:
            open(path, 'wb').write(data)
        names.append(name)
    print(' '.join(sorted(names)))


if __name__ == '__main__':
    main()

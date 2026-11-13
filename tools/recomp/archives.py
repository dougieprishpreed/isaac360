"""The game's resource archives (resources/packed/*.a), read in place.

An archive: "ARCH000" and a version byte, then the table's offset (u32) and entry count (u16); the table's entries are
five u32s: two hashes of the entry's name, its offset, size and checksum (a rotate-right-and-add over its words).
Version 0 stores an entry word-scrambled with a seed from its name hash; version 2 stores 1 KB blocks (u32 header:
final << 31 | size), deflated, or after a first non-final block of exactly 1024 bytes, XORed with a generator
seeded by the name hash ("raw" mode); version 1 blocks are LZW. Names come from the list the game ships beside its
resource extractor (tools/ResourceExtractor/filelist.txt).

    from archives import read_archive, shader_sources
"""
import glob
import os
import struct
import zlib

MASK = 0xffffffff
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(ROOT, 'game')


def filename_hash(name):
    first, second = 0x1505, 0x5bb2220e
    for value in name.replace('\\', '/').encode('utf-8'):
        if 65 <= value <= 90:
            value += 32
        first = (first * 33 + value) & MASK
        second = ((second ^ value) * 0x1000193) & MASK
    return first, second


def checksum(data):
    value = 0xababeb98
    buffer = bytearray(512)
    for start in range(0, len(data), 512):
        chunk = data[start:start + 512]
        buffer[:len(chunk)] = chunk
        for word, in struct.iter_unpack('<I', buffer[:(len(chunk) + 3) & ~3]):
            value = (((value >> 1) | ((value & 1) << 31)) + word) & MASK
    return value


def unscramble_words(data, seed):
    result = bytearray(len(data))
    if len(data) % 4:
        raise ValueError('Word-scrambled payload must be padded to four bytes')
    for offset, (word,) in enumerate(struct.iter_unpack('<I', data)):
        word ^= seed
        mode = seed & 15
        if mode == 9:
            word = ((word & 0x00ff00ff) << 8) | ((word & 0xff00ff00) >> 8)
        elif mode == 13:
            word = ((word << 16) | (word >> 16)) & MASK
        elif mode == 2:
            word = int.from_bytes(word.to_bytes(4, 'little'), 'big')
        struct.pack_into('<I', result, offset * 4, word)
        seed = ((seed << 8) ^ seed) & MASK
        seed ^= seed >> 9
        seed = ((seed << 23) ^ seed) & MASK
    return bytes(result)


def mix(values):
    a, b, c, d, e, f, g, h = values
    a ^= (b << 11) & MASK; d = (d + a) & MASK; b = (b + c) & MASK
    b ^= c >> 2; e = (e + b) & MASK; c = (c + d) & MASK
    c ^= (d << 8) & MASK; f = (f + c) & MASK; d = (d + e) & MASK
    d ^= e >> 16; g = (g + d) & MASK; e = (e + f) & MASK
    e ^= (f << 10) & MASK; h = (h + e) & MASK; f = (f + g) & MASK
    f ^= g >> 4; a = (a + f) & MASK; g = (g + h) & MASK
    g ^= (h << 8) & MASK; b = (b + g) & MASK; h = (h + a) & MASK
    h ^= a >> 9; c = (c + h) & MASK; a = (a + b) & MASK
    return [a, b, c, d, e, f, g, h]


class ArchiveRng:
    def __init__(self, seed):
        state = (seed << 32) | (((seed << 8) ^ (seed >> 9) ^ seed ^ (seed << 23)) & MASK)
        results = []
        for _ in range(256):
            word = (((state >> 18) ^ state) >> 27) & MASK
            rotate = state >> 59
            results.append(((word >> rotate) | (word << ((32 - rotate) & 31))) & MASK)
            state = (state * 0x5851f42d4c957f2d + 0x7f) & 0xffffffffffffffff
        values = [0x9e3779b9] * 8
        for _ in range(4):
            values = mix(values)
        self.memory = [0] * 256
        for i in range(0, 256, 8):
            values = mix([(a + b) & MASK for a, b in zip(values, results[i:i + 8])])
            self.memory[i:i + 8] = values
        for i in range(0, 256, 8):
            values = mix([(a + b) & MASK for a, b in zip(values, self.memory[i:i + 8])])
            self.memory[i:i + 8] = values
        self.a = self.b = self.c = 0
        self.generate()

    def generate(self):
        self.c = (self.c + 1) & MASK
        a, b = self.a, (self.b + self.c) & MASK
        result = []
        memory = self.memory
        for i in range(256):
            x = memory[i]
            shift = i & 3
            a ^= ((a << 13) & MASK) if shift == 0 else (a >> 6 if shift == 1 else ((a << 2) & MASK if shift == 2 else a >> 16))
            a = (a + memory[(i + 128) & 255]) & MASK
            y = (memory[(x >> 2) & 255] + a + b) & MASK
            memory[i] = y
            b = (memory[(y >> 10) & 255] + x) & MASK
            result.append(b)
        self.a, self.b, self.results, self.position = a, b, result, 0

    def next(self):
        value = self.results[self.position]
        self.position += 1
        if self.position == 256:
            self.generate()
        return value

    def unscramble(self, data):
        output = bytearray(data)
        for offset in range(0, len(data), 4):
            key = self.next()
            for i in range(min(4, len(data) - offset)):
                output[offset + i] ^= (key >> (8 * i)) & 255
        return bytes(output)


class LzwDecoder:
    def __init__(self):
        self.reset()

    def reset(self):
        self.dictionary = [bytes([i]) for i in range(256)]
        self.width = 8

    def decode(self, data):
        position = bits = buffer = 0

        def read(width):
            nonlocal position, bits, buffer
            while bits < width:
                if position >= len(data):
                    raise ValueError('Truncated LZW code')
                buffer = ((buffer << 8) | data[position]) & MASK
                position += 1
                bits += 8
            bits -= width
            return (buffer >> bits) & ((1 << width) - 1)

        previous = self.dictionary[read(self.width)]
        output = bytearray(previous)
        while position < len(data):
            if len(self.dictionary) + 1 > 4095:
                self.reset()
                previous = self.dictionary[read(8)]
                output += previous
            if position >= len(data):
                break
            self.width = max(self.width, len(self.dictionary).bit_length())
            code = read(self.width)
            if code == len(self.dictionary):
                current = previous + previous[:1]
            elif code < len(self.dictionary):
                current = self.dictionary[code]
            else:
                raise ValueError('Invalid LZW code %d, dictionary size %d' % (code, len(self.dictionary)))
            output += current
            if len(self.dictionary) < 4096:
                self.dictionary.append(previous + current[:1])
            previous = current
        return bytes(output)


def exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise ValueError('Unexpected end of archive')
    return data


def decode_entry(stream, version, size, hash2):
    if version == 0:
        return unscramble_words(exact(stream, (size + 3) & ~3), (hash2 ^ 0xf9524287) | 1)[:size]
    output = bytearray()
    lzw = LzwDecoder() if version == 1 else None
    inflater = zlib.decompressobj(-15) if version == 2 else None
    rng = None
    raw_mode = False
    while len(output) < size:
        header, = struct.unpack('<I', exact(stream, 4))
        final = bool(header >> 31)
        encoded_size = header & 0x7fffffff
        if encoded_size > 2 * 1024 * 1024:
            raise ValueError('Invalid encoded block size %d' % encoded_size)
        encoded = exact(stream, encoded_size)
        if version == 1:
            chunk = lzw.decode(encoded)
        elif version == 2:
            if not raw_mode and not final and encoded_size == 1024:
                raw_mode = True
                rng = ArchiveRng(hash2)
            chunk = rng.unscramble(encoded) if raw_mode else inflater.decompress(encoded)
        else:
            raise ValueError('Unsupported archive version %d' % version)
        if not chunk or len(chunk) > 1024 or len(output) + len(chunk) > size:
            raise ValueError('Invalid decoded block length %d for remaining %d' % (len(chunk), size - len(output)))
        output += chunk
    return bytes(output)


def read_table(stream, path):
    """(version, [(hash1, hash2, offset, size, checksum)]) of an open archive."""
    header = exact(stream, 14)
    if header[:7] != b'ARCH000':
        raise ValueError('%s: not a resource archive' % path)
    version = header[7]
    table_offset, count = struct.unpack_from('<IH', header, 8)
    stream.seek(table_offset)
    return version, [struct.unpack('<IIIII', exact(stream, 20)) for _ in range(count)]


def file_names(game=GAME):
    """{(hash1, hash2): name} from the game's own file list."""
    names = {}
    listing = os.path.join(game, 'tools', 'ResourceExtractor', 'filelist.txt')
    with open(listing, encoding='utf-8-sig') as f:
        for line in f:
            name = line.strip()
            if name:
                names[filename_hash(name)] = name.replace('\\', '/')
    return names


def read_archive(path, wanted):
    """{name: bytes} of the entries of the archive at path whose names wanted(name) accepts."""
    names = file_names()
    out = {}
    with open(path, 'rb') as stream:
        version, table = read_table(stream, path)
        for hash1, hash2, offset, size, expected in table:
            name = names.get((hash1, hash2))
            if not name or not wanted(name):
                continue
            stream.seek(offset)
            data = decode_entry(stream, version, size, hash2)
            if checksum(data) != expected:
                raise ValueError('%s: %s fails its checksum' % (path, name))
            out[name] = data
    return out


def shader_sources(game=GAME):
    """{lower-case name: bytes} of every shader stage (*/shaders/*.vs, *.fs) in the game's archives, the later archive
    (in name order) winning where two hold the same file."""
    files = {}
    wanted = lambda n: '/shaders/' in n.lower() and n.lower().endswith(('.vs', '.fs'))
    for path in sorted(glob.glob(os.path.join(game, 'resources', 'packed', '*.a'))):
        for name, data in read_archive(path, wanted).items():
            files[name.lower()] = data
    return files

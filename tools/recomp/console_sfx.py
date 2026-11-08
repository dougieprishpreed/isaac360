"""Rewrites the game's archives that hold sound effects for the console (docs/RECOMP_PLAN.md, "Console memory").

The game keeps every sound effect's WAV in memory for the whole run (1,150 files, 243 MB of 16-bit PCM): it plays
them through a pool of OpenAL buffers and uploads a sound's samples with alBufferData each time it starts. The
console has 512 MB for everything. So its copy of the archives keeps each effect's WAV with its "fmt " chunk but a
"data" chunk that only names where the samples are: "B3RF", u32 offset, u32 bytes (little-endian) into
resources/packed/sfx.pcm, which holds every effect's original samples, as the game ships them (identical ones
once). The OpenAL shim (src/recomp/shims_al.cpp) reads an effect from there when it is first played and keeps the
recently played ones in memory. The sound is the original; only where it waits differs. Every other entry of the
archives is copied byte for byte, with the archive's checksums recomputed; the PC keeps the original archives.

Archive entries (tools/extract_resources.py has the decoder): version 0 stores an entry word-scrambled with a
seed from its name hash; version 2 stores 1 KB blocks (u32 header: final << 31 | size), deflated, or after a
first non-final block of exactly 1024 bytes, XORed with a generator seeded by the name hash ("raw" mode).

    py -3 tools/recomp/console_sfx.py [--out build/console_data]
"""
import argparse
import hashlib
import io
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import extract_resources as ar  # noqa: E402

ARCHIVES = ['sfx.a', 'afterbirth.a', 'afterbirthp.a', 'repentance.a']
MARK = b'B3RF'
PCM_FILE = 'sfx.pcm'
ALIGN = 2048                                        # each effect starts on a drive sector


class PcmStore:
    """The effects' samples, one after another; the same samples are stored once."""

    def __init__(self, path):
        self.f = open(path, 'wb')
        self.at = 0
        self.seen = {}

    def add(self, samples):
        key = hashlib.sha1(samples).digest()
        if key not in self.seen:
            pad = (-self.at) % ALIGN
            self.f.write(b'\0' * pad)
            self.at += pad
            self.seen[key] = self.at
            self.f.write(samples)
            self.at += len(samples)
        return self.seen[key]

    def close(self):
        self.f.close()


def convert_wav(data, store):
    """A PCM16 WAV -> the same WAV whose data chunk refers to its samples in the store, or None to keep it."""
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        return None
    chunks, p = [], 12
    fmt = None
    while p + 8 <= len(data):
        cid, size = data[p:p + 4], struct.unpack_from('<I', data, p + 4)[0]
        body = data[p + 8:p + 8 + size]
        chunks.append((cid, body))
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', body, 0)
        p += 8 + size + (size & 1)
    if not fmt or fmt[0] != 1 or fmt[5] != 16:
        return None
    out = io.BytesIO()
    for cid, body in chunks:
        if cid == b'data':
            body = MARK + struct.pack('<II', store.add(body), len(body))
        out.write(cid + struct.pack('<I', len(body)) + body + (b'\0' if len(body) & 1 else b''))
    payload = out.getvalue()
    return b'RIFF' + struct.pack('<I', 4 + len(payload)) + b'WAVE' + payload


def encode_entry(version, data, hash2):
    if version == 0:
        # unscramble_words is its own inverse only for the XOR; the swaps apply after it, so undo them first
        padded = data + b'\0' * ((-len(data)) % 4)
        out = bytearray(len(padded))
        seed = (hash2 ^ 0xf9524287) | 1
        for i in range(0, len(padded), 4):
            word = struct.unpack_from('<I', padded, i)[0]
            mode = seed & 15
            if mode == 9:
                word = ((word & 0x00ff00ff) << 8) | ((word & 0xff00ff00) >> 8)
            elif mode == 13:
                word = ((word << 16) | (word >> 16)) & ar.MASK
            elif mode == 2:
                word = int.from_bytes(word.to_bytes(4, 'little'), 'big')
            struct.pack_into('<I', out, i, word ^ seed)
            seed = ((seed << 8) ^ seed) & ar.MASK
            seed ^= seed >> 9
            seed = ((seed << 23) ^ seed) & ar.MASK
        return bytes(out)
    if version != 2:
        raise ValueError('version %d' % version)
    out = bytearray()
    if len(data) <= 1024:
        z = zlib.compressobj(9, zlib.DEFLATED, -15)
        enc = z.compress(data) + z.flush(zlib.Z_FINISH)
        out += struct.pack('<I', 0x80000000 | len(enc)) + enc
        return bytes(out)
    rng = ar.ArchiveRng(hash2)
    for i in range(0, len(data), 1024):
        chunk = data[i:i + 1024]
        final = i + 1024 >= len(data)
        if i == 0 and final:
            raise ValueError('a raw entry needs a first block that is not the last')
        out += struct.pack('<I', (0x80000000 if final else 0) | len(chunk)) + rng.unscramble(chunk)
    return bytes(out)


def names():
    table = {}
    for path in (os.path.join(ROOT, 'tools', 'data', 'resource_names.txt'),
                 os.path.join(ROOT, 'game', 'tools', 'ResourceExtractor', 'filelist.txt')):
        if os.path.isfile(path):
            for name in open(path, encoding='utf-8-sig').read().splitlines():
                name = name.strip()
                if name:
                    table[ar.filename_hash(name)] = name.replace('\\', '/')
    return table


def encoded_length(data, version, off, size):
    """Bytes an entry takes in the archive."""
    if version == 0:
        return (size + 3) & ~3
    p, done = off, 0
    while True:
        header = struct.unpack_from('<I', data, p)[0]
        p += 4 + (header & 0x7fffffff)
        if header >> 31:
            return p - off
        done += 1


def rewrite(src, dst, table, store):
    data = open(src, 'rb').read()
    version = data[7]
    table_offset, count = struct.unpack_from('<IH', data, 8)
    entries = [list(struct.unpack_from('<IIIII', data, table_offset + 20 * i)) for i in range(count)]
    start = min(e[2] for e in entries) if entries else table_offset
    payload = bytearray(data[:start])
    converted = before = after = 0
    # the entries in their original order in the file; converted effects re-encoded, the rest copied
    for e in sorted(entries, key=lambda e: e[2]):
        h1, h2, off, size, check = e
        name = table.get((h1, h2), '')
        enc = None
        if name.lower().endswith('.wav'):
            stream = io.BytesIO(data)
            stream.seek(off)
            wav = ar.decode_entry(stream, version, size, h2)
            new = convert_wav(wav, store)
            if new is None:
                print('  kept %s (not 16-bit PCM)' % name)
            else:
                enc = encode_entry(version, new, h2)
                if ar.decode_entry(io.BytesIO(enc), version, len(new), h2) != new:
                    raise SystemExit('entry encoding does not round-trip: ' + name)
                e[3] = len(new)
                e[4] = ar.checksum(new)
                converted += 1
                before += len(wav)
                after += len(new)
        if enc is None:
            enc = data[off:off + encoded_length(data, version, off, size)]
        e[2] = len(payload)
        payload += enc
    out = bytearray(payload)
    struct.pack_into('<I', out, 8, len(payload))
    for e in entries:
        out += struct.pack('<IIIII', *e)
    out += data[table_offset + 20 * count:]          # anything after the table, as it was
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'wb').write(bytes(out))
    print('%-14s %4d WAVs %6.1f MB -> %5.1f MB; archive %6.1f MB -> %6.1f MB' % (
        os.path.basename(src), converted, before / 1048576, after / 1048576, len(data) / 1048576, len(out) / 1048576))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--out', default=os.path.join(ROOT, 'build', 'console_data'))
    args = parser.parse_args()
    table = names()
    packed = os.path.join(args.out, 'resources', 'packed')
    os.makedirs(packed, exist_ok=True)
    store = PcmStore(os.path.join(packed, PCM_FILE))
    for name in ARCHIVES:
        rewrite(os.path.join(ROOT, 'game', 'resources', 'packed', name), os.path.join(packed, name), table, store)
    store.close()
    print('%s: %d effects, %.1f MB' % (PCM_FILE, len(store.seen), store.at / 1048576.0))


if __name__ == '__main__':
    main()

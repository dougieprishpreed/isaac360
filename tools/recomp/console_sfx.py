"""Rewrites the game's archives that hold sound effects for the console (docs/RECOMP_PLAN.md, "Console memory").

The game keeps every sound effect's WAV in memory for the whole run (1,150 files, ~213 MB of 16-bit PCM): it plays
them through a pool of 16 OpenAL buffers and uploads a sound's samples with alBufferData each time it starts. The
console has 512 MB for everything, so its copy of the archives holds the effects as 4-bit IMA ADPCM (a quarter
of the size). Each WAV keeps its PCM "fmt " chunk, so the game parses and plays it as before; its "data" chunk
starts with a marker the OpenAL shim (src/recomp/shims_al.cpp) recognises and decodes. Every other entry of the
archives is copied byte for byte; the PC keeps the original archives.

Data chunk of a converted WAV: "B3AD", u32 frames, u32 channels, then for each channel a stream: s16 first
sample, u8 step index, u8 0, and (frames - 1) 4-bit codes, low nibble first, padded to a byte. Little-endian.
"B3AH" instead: the same at half the rate the "fmt " chunk declares (effects at 32 kHz and up; half_rate).

Archive entries (tools/extract_resources.py has the decoder): version 0 stores an entry word-scrambled with a
seed from its name hash; version 2 stores 1 KB blocks (u32 header: final << 31 | size), deflated, or after a
first non-final block of exactly 1024 bytes, XORed with a generator seeded by the name hash ("raw" mode).

    py -3 tools/recomp/console_sfx.py [--out build/console_data]
"""
import argparse
import io
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import extract_resources as ar  # noqa: E402

ARCHIVES = ['sfx.a', 'afterbirth.a', 'afterbirthp.a', 'repentance.a']
MARK = b'B3AD'
# Effects at 32 kHz and up are stored at half their rate (22.05 kHz for the game's 44.1 kHz ones): half the memory
# again (the user accepted the small loss above 11 kHz for it). Low-pass first, so nothing above the new Nyquist
# folds back: a 63-tap Blackman-windowed sinc at 0.45 of the new rate.
MARK_HALF = b'B3AH'
HALF_FROM = 32000
HALF_TAPS = 63


def half_rate(samples):
    import numpy as np
    x = np.asarray(samples, dtype=np.float64)
    n = np.arange(HALF_TAPS) - (HALF_TAPS - 1) / 2.0
    cutoff = 0.45 * 0.5                              # of the input rate: 0.45 of the output's Nyquist
    taps = 2 * cutoff * np.sinc(2 * cutoff * n) * np.blackman(HALF_TAPS)
    taps /= taps.sum()
    y = np.convolve(x, taps, mode='same')[::2]
    return [int(v) for v in np.clip(np.rint(y), -32768, 32767)]

STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
         107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
         876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
         4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
         22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def ima_encode(samples):
    """One channel of s16 samples -> header + nibbles (the decoder's state is tracked exactly)."""
    if not samples:
        return struct.pack('<hBB', 0, 0, 0)
    pred, index = samples[0], 0
    # a starting step that suits the opening samples
    if len(samples) > 1:
        d = abs(samples[1] - samples[0])
        while index < 88 and STEPS[index] < d:
            index += 1
    out = bytearray(struct.pack('<hBB', pred, index, 0))
    nib = []
    for s in samples[1:]:
        step = STEPS[index]
        diff = s - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        delta = step >> 3
        if diff >= step:
            code |= 4
            diff -= step
            delta += step
        if diff >= step >> 1:
            code |= 2
            diff -= step >> 1
            delta += step >> 1
        if diff >= step >> 2:
            code |= 1
            delta += step >> 2
        pred = pred - delta if code & 8 else pred + delta
        pred = -32768 if pred < -32768 else 32767 if pred > 32767 else pred
        index += INDEX[code & 7]
        index = 0 if index < 0 else 88 if index > 88 else index
        nib.append(code)
    if len(nib) & 1:
        nib.append(0)
    out += bytes(nib[i] | (nib[i + 1] << 4) for i in range(0, len(nib), 2))
    return bytes(out)


def ima_decode(stream, frames):
    """The shim's decoder, for the round-trip check."""
    pred, index = struct.unpack_from('<hB', stream, 0)
    out = [pred]
    for i in range(frames - 1):
        b = stream[4 + (i >> 1)]
        code = b & 15 if not i & 1 else b >> 4
        step = STEPS[index]
        delta = step >> 3
        if code & 4:
            delta += step
        if code & 2:
            delta += step >> 1
        if code & 1:
            delta += step >> 2
        pred = pred - delta if code & 8 else pred + delta
        pred = -32768 if pred < -32768 else 32767 if pred > 32767 else pred
        index += INDEX[code & 7]
        index = 0 if index < 0 else 88 if index > 88 else index
        out.append(pred)
    return out


def convert_wav(data):
    """A PCM16 WAV -> the same WAV with an ADPCM data chunk, or None to keep it."""
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
    channels, rate = fmt[1], fmt[2]
    out = io.BytesIO()
    for cid, body in chunks:
        if cid == b'data':
            frames = len(body) // (2 * channels)
            pcm = struct.unpack('<%dh' % (frames * channels), body[:frames * 2 * channels])
            streams = [list(pcm[ch::channels]) for ch in range(channels)]
            mark = MARK
            if rate >= HALF_FROM and frames >= 2:
                # half the sample rate (the "fmt " chunk keeps the game's rate; the shim plays it at half)
                streams = [half_rate(s) for s in streams]
                frames = len(streams[0])
                mark = MARK_HALF
            enc = mark + struct.pack('<II', frames, channels)
            for s in streams:
                enc += ima_encode(s)
            body = enc
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


def rewrite(src, dst, table):
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
            new = convert_wav(wav)
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
    parser.add_argument('--check', action='store_true', help='round-trip one effect through the ADPCM coder')
    args = parser.parse_args()
    if args.check:
        import math
        pcm = [int(12000 * math.sin(i / 7.0) + 3000 * math.sin(i / 1.3)) for i in range(4000)]
        dec = ima_decode(ima_encode(pcm), len(pcm))
        err = math.sqrt(sum((a - b) ** 2 for a, b in zip(pcm, dec)) / len(pcm))
        print('ADPCM round trip: rms error %.1f of rms %.1f' % (err, math.sqrt(sum(a * a for a in pcm) / len(pcm))))
        return
    table = names()
    for name in ARCHIVES:
        rewrite(os.path.join(ROOT, 'game', 'resources', 'packed', name),
                os.path.join(args.out, 'resources', 'packed', name), table)


if __name__ == '__main__':
    main()

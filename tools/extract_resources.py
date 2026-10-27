"""Local archive decoder reconstructed from the supplied symbol-bearing helper.

Evidence addresses are recorded in docs/ARCHIVE_FORMAT.md. Every entry is kept
under a generic package ID to preserve duplicates and layer boundaries.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import struct
import time
import zlib
from source_data import SOURCE_ROOT,source_root

ROOT = Path(__file__).resolve().parents[1]
MASK = 0xffffffff


def filename_hash(name):
    first, second = 0x1505, 0x5bb2220e
    for value in name.replace('\\','/').encode('utf-8'):
        if 65 <= value <= 90:
            value += 32
        first = (first * 33 + value) & MASK
        second = ((second ^ value) * 0x1000193) & MASK
    return first, second


def checksum(data):
    value = 0xababeb98
    buffer = bytearray(512)
    for start in range(0,len(data),512):
        chunk = data[start:start+512]
        buffer[:len(chunk)] = chunk
        for word, in struct.iter_unpack('<I', buffer[:(len(chunk)+3)&~3]):
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
            word = int.from_bytes(word.to_bytes(4,'little'),'big')
        struct.pack_into('<I',result,offset*4,word)
        seed = ((seed << 8) ^ seed) & MASK
        seed ^= seed >> 9
        seed = ((seed << 23) ^ seed) & MASK
    return bytes(result)


def mix(values):
    a,b,c,d,e,f,g,h = values
    a ^= (b << 11) & MASK; d=(d+a)&MASK; b=(b+c)&MASK
    b ^= c >> 2; e=(e+b)&MASK; c=(c+d)&MASK
    c ^= (d << 8) & MASK; f=(f+c)&MASK; d=(d+e)&MASK
    d ^= e >> 16; g=(g+d)&MASK; e=(e+f)&MASK
    e ^= (f << 10) & MASK; h=(h+e)&MASK; f=(f+g)&MASK
    f ^= g >> 4; a=(a+f)&MASK; g=(g+h)&MASK
    g ^= (h << 8) & MASK; b=(b+g)&MASK; h=(h+a)&MASK
    h ^= a >> 9; c=(c+h)&MASK; a=(a+b)&MASK
    return [a,b,c,d,e,f,g,h]


class ArchiveRng:
    def __init__(self, seed):
        state = (seed << 32) | (((seed << 8) ^ (seed >> 9) ^ seed ^ (seed << 23)) & MASK)
        results = []
        for _ in range(256):
            word = (((state >> 18) ^ state) >> 27) & MASK
            rotate = state >> 59
            results.append(((word >> rotate) | (word << ((32-rotate)&31))) & MASK)
            state = (state * 0x5851f42d4c957f2d + 0x7f) & 0xffffffffffffffff
        values = [0x9e3779b9]*8
        for _ in range(4):
            values = mix(values)
        self.memory = [0]*256
        for i in range(0,256,8):
            values = mix([(a+b)&MASK for a,b in zip(values,results[i:i+8])])
            self.memory[i:i+8] = values
        for i in range(0,256,8):
            values = mix([(a+b)&MASK for a,b in zip(values,self.memory[i:i+8])])
            self.memory[i:i+8] = values
        self.a = self.b = self.c = 0
        self.generate()

    def generate(self):
        self.c = (self.c+1)&MASK
        a, b = self.a, (self.b+self.c)&MASK
        result = []
        memory = self.memory
        for i in range(256):
            x = memory[i]
            shift = i & 3
            a ^= ((a << 13)&MASK) if shift == 0 else (a >> 6 if shift == 1 else ((a << 2)&MASK if shift == 2 else a >> 16))
            a = (a+memory[(i+128)&255])&MASK
            y = (memory[(x>>2)&255]+a+b)&MASK
            memory[i] = y
            b = (memory[(y>>10)&255]+x)&MASK
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
        for offset in range(0,len(data),4):
            key = self.next()
            for i in range(min(4,len(data)-offset)):
                output[offset+i] ^= (key >> (8*i)) & 255
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
            nonlocal position,bits,buffer
            while bits < width:
                if position >= len(data):
                    raise ValueError('Truncated LZW code')
                buffer = ((buffer << 8) | data[position]) & MASK
                position += 1
                bits += 8
            bits -= width
            return (buffer >> bits) & ((1 << width)-1)

        previous = self.dictionary[read(self.width)]
        output = bytearray(previous)
        while position < len(data):
            if len(self.dictionary)+1 > 4095:
                self.reset()
                previous = self.dictionary[read(8)]
                output += previous
            if position >= len(data):
                break
            self.width = max(self.width,len(self.dictionary).bit_length())
            code = read(self.width)
            if code == len(self.dictionary):
                current = previous+previous[:1]
            elif code < len(self.dictionary):
                current = self.dictionary[code]
            else:
                raise ValueError(f'Invalid LZW code {code}, dictionary size {len(self.dictionary)}')
            output += current
            if len(self.dictionary) < 4096:
                self.dictionary.append(previous+current[:1])
            previous = current
        return bytes(output)


def exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise ValueError('Unexpected end of archive')
    return data


def decode_entry(stream, version, size, hash2):
    if version == 0:
        return unscramble_words(exact(stream,(size+3)&~3),(hash2 ^ 0xf9524287) | 1)[:size]
    output = bytearray()
    lzw = LzwDecoder() if version == 1 else None
    inflater = zlib.decompressobj(-15) if version == 2 else None
    rng = None
    raw_mode = False
    while len(output) < size:
        header, = struct.unpack('<I',exact(stream,4))
        final = bool(header >> 31)
        encoded_size = header & 0x7fffffff
        if encoded_size > 2*1024*1024:
            raise ValueError(f'Invalid encoded block size {encoded_size}')
        encoded = exact(stream,encoded_size)
        if version == 1:
            chunk = lzw.decode(encoded)
        elif version == 2:
            if not raw_mode and not final and encoded_size == 1024:
                raw_mode = True
                rng = ArchiveRng(hash2)
            chunk = rng.unscramble(encoded) if raw_mode else inflater.decompress(encoded)
        else:
            raise ValueError(f'Unsupported archive version {version}')
        if not chunk or len(chunk) > 1024 or len(output)+len(chunk) > size:
            raise ValueError(f'Invalid decoded block length {len(chunk)} for remaining {size-len(output)}')
        output += chunk
    return bytes(output)


def extract(source, package_ids=None):
    source=source_root(source)
    names = {}
    for dictionary in (ROOT/'tools/data/resource_names.txt',source/'tools/ResourceExtractor/filelist.txt'):
        if dictionary.is_file():
            for name in dictionary.read_text(encoding='utf-8-sig').splitlines():
                name=name.strip()
                if name:names[filename_hash(name)]=name.replace('\\','/')
    packages = json.loads((ROOT/'evidence/packages.json').read_text())
    all_summaries = []
    failed = False
    for package in packages:
        if package_ids and package['id'] not in package_ids:
            continue
        summary_file = ROOT/'evidence/packages'/f'{package["id"]}.json'
        if summary_file.exists():
            old = json.loads(summary_file.read_text())
            if old['complete']:
                print(f'Skipping completed {package["id"]}',flush=True)
                all_summaries.append(old)
                continue
        archive = source/package['source_relative']
        output_root = (ROOT/'assets/packages'/package['id']).resolve()
        output_root.mkdir(parents=True,exist_ok=True)
        entries = []
        started = time.monotonic()
        print(f'Extracting {package["id"]}',flush=True)
        with archive.open('rb') as stream:
            header = exact(stream,14)
            if header[:7] != b'ARCH000':
                raise ValueError('Invalid archive magic')
            version = header[7]
            table_offset,count = struct.unpack_from('<IH',header,8)
            if table_offset+20*count > archive.stat().st_size:
                raise ValueError('Archive table exceeds file size')
            stream.seek(table_offset)
            table = [struct.unpack('<IIIII',exact(stream,20)) for _ in range(count)]
            for i,(hash1,hash2,offset,size,expected) in enumerate(table):
                name = names.get((hash1,hash2))
                relative = PurePosixPath(name) if name else PurePosixPath('unresolved')/f'{hash1:08x}{hash2:08x}.bin'
                target = output_root.joinpath(*relative.parts).resolve()
                if relative.is_absolute() or not target.is_relative_to(output_root):
                    raise ValueError('Unsafe extracted filename')
                row = dict(index=i,hash1=f'{hash1:08x}',hash2=f'{hash2:08x}',offset=offset,
                           bytes=size,expected_checksum=f'{expected:08x}',resolved_name=name,
                           output=target.relative_to(ROOT).as_posix())
                try:
                    if not 14 <= offset <= table_offset:
                        raise ValueError('Entry offset outside payload area')
                    stream.seek(offset)
                    data = decode_entry(stream,version,size,hash2)
                    actual = checksum(data)
                    row['actual_checksum'] = f'{actual:08x}'
                    row['checksum_ok'] = actual == expected
                    row['sha256'] = hashlib.sha256(data).hexdigest()
                    target.parent.mkdir(parents=True,exist_ok=True)
                    target.write_bytes(data)
                    if actual != expected:
                        row['error'] = 'Archive checksum mismatch'
                        failed = True
                except Exception as error:
                    row['error'] = str(error)
                    failed = True
                entries.append(row)
                if (i+1)%250 == 0:
                    print(f'{package["id"]}: {i+1}/{count}, {int(time.monotonic()-started)}s',flush=True)
        summary = {'id':package['id'],'version':version,'expected_entries':count,'entries':entries,
                   'errors':sum('error' in e for e in entries),'unresolved_names':sum(e['resolved_name'] is None for e in entries),
                   'complete':len(entries)==count and all(e.get('checksum_ok') for e in entries),
                   'elapsed_seconds':round(time.monotonic()-started,2)}
        summary_file.parent.mkdir(parents=True,exist_ok=True)
        summary_file.write_text(json.dumps(summary,indent=2)+'\n')
        all_summaries.append(summary)
        print(f'{package["id"]}: {count} entries, {summary["errors"]} errors, {summary["unresolved_names"]} unnamed',flush=True)
        if summary['errors']:
            # Stop promptly to repair the decoder before processing the rest.
            break
    report = {'packages':len(all_summaries),'complete':len(all_summaries)==len(packages) and all(p['complete'] for p in all_summaries),
              'entries':sum(len(p['entries']) for p in all_summaries),'errors':sum(p['errors'] for p in all_summaries),
              'unresolved_names':sum(p['unresolved_names'] for p in all_summaries)}
    (ROOT/'evidence/extraction_summary.json').write_text(json.dumps(report,indent=2)+'\n')
    if failed:
        raise SystemExit(1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=SOURCE_ROOT)
    parser.add_argument('--packages',nargs='+')
    args = parser.parse_args()
    extract(args.source.resolve(strict=True),args.packages)

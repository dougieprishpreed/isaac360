"""Prepare original WAV SFX and the reached music streams for the audio mixer.

All content is read from the read-only prepared game archive. SFX files are
hard-linked (or copied) unchanged; reached OGG music streams are decoded to
44.1 kHz stereo PCM WAV because the shared runtime and Xbox adapter use a
small, dependency-free WAV mixer. The OGG sources remain the authority and are
hash-checked by the archive index.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import xml.etree.ElementTree as ET
from original_resources import OriginalResources

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / 'runtime/game'
OUT = ROOT / 'runtime/port/audio'
RATE = 44100
CHANNELS = 2


def find_ffmpeg():
    found = shutil.which('ffmpeg')
    if found:
        return found
    for candidate in (r'C:\ffmpeg\ffmpeg.exe', r'C:\ffmpeg\bin\ffmpeg.exe'):
        if Path(candidate).exists():
            return candidate
    raise RuntimeError('ffmpeg is required to decode the original OGG music streams')


def source_entries():
    rows = []
    for line in (GAME / 'file_index.jsonl').read_text(encoding='utf-8').splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def music_paths():
    resources = OriginalResources(GAME)
    raw, _ = resources.read('music.xml')
    root = ET.fromstring(raw)
    base = root.get('root') or ''
    wanted = {1, 4, 14, 64, 95}
    paths = []
    for entry in root:
        if int(entry.get('id', '0')) not in wanted:
            continue
        for key in ('intro', 'path', 'layerintro', 'layer'):
            value = entry.get(key)
            if value:
                virtual = resources.resolve(base + value)['virtual_path']
                if virtual not in paths:
                    paths.append(virtual)
    if not paths:
        raise RuntimeError('No resolved music streams in music.xml')
    return sorted(paths)


def sha256_file(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        while True:
            block = f.read(1024 * 1024)
            if not block:
                break
            h.update(block)
    return h.hexdigest()


def same_file_or_copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() and dst.stat().st_size == src.stat().st_size:
        return
    if dst.exists():
        dst.unlink()
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def prepare():
    rows = source_entries()
    resources = OriginalResources(GAME)
    OUT.mkdir(parents=True, exist_ok=True)
    ffmpeg = find_ffmpeg()

    sfx = []
    seen = set()
    for r in rows:
        virtual = r['virtual_path']
        if not virtual.lower().endswith('.wav') or virtual in seen:
            continue
        seen.add(virtual)
        # Resolve through the same mount precedence the original VFS uses;
        # six virtual SFX paths have more than one package copy.
        entry = resources.resolve(virtual)
        src = GAME / entry['file']
        if not src.exists() or src.stat().st_size != entry['bytes']:
            raise RuntimeError('Prepared SFX mismatch: ' + virtual)
        dst = OUT / virtual
        same_file_or_copy(src, dst)
        sfx.append(dict(virtual_path=virtual, target=str(dst.relative_to(OUT)).replace('\\', '/'),
                        bytes=entry['bytes'], sha256=entry['sha256']))

    music = []
    for virtual in music_paths():
        entry = resources.resolve(virtual)
        src = GAME / entry['file']
        if not src.exists() or src.stat().st_size != entry['bytes']:
            raise RuntimeError('Prepared music stream mismatch: ' + virtual)
        if sha256_file(src) != entry['sha256']:
            raise RuntimeError('Prepared music stream hash mismatch: ' + virtual)
        rel = virtual + '.wav'
        dst = OUT / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not dst.exists() or dst.stat().st_size == 0:
            command = [ffmpeg, '-y', '-v', 'error', '-i', str(src),
                       '-ac', str(CHANNELS), '-ar', str(RATE), '-c:a', 'pcm_s16le', str(dst)]
            run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if run.returncode != 0:
                raise RuntimeError('ffmpeg failed for %s: %s' % (virtual, run.stderr.decode('utf-8', 'replace')))
        music.append(dict(virtual_path=virtual, target=rel, source_sha256=entry['sha256'],
                          bytes=dst.stat().st_size, output_sha256=sha256_file(dst)))

    manifest = dict(format='native-audio-pack-v1', sample_rate=RATE, channels=CHANNELS,
                    ffmpeg=str(ffmpeg), sfx_count=len(sfx), sfx_bytes=sum(x['bytes'] for x in sfx),
                    music_count=len(music), music_bytes=sum(x['bytes'] for x in music),
                    sfx=sfx, music=music)
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=1) + '\n', encoding='ascii')
    print('Prepared audio: %d SFX (%d bytes), %d music streams (%d bytes)' %
          (len(sfx), manifest['sfx_bytes'], len(music), manifest['music_bytes']))


if __name__ == '__main__':
    prepare()

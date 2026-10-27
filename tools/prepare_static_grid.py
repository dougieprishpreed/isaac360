"""Pin the selected ordinary-wall caller and its actual constructor/Init path."""
from pathlib import Path
import hashlib,json,re,struct
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(out,stages):
    memory=Memory(ROOT/'recovered/main');listing=ROOT/'recovered/main/disassembly.txt'
    spans=[(0x6b2ff0,0x6b3019),(0x6b4a19,0x6b4a78),(0x6b4b39,0x6b4b94),
        (0x6c5640,0x6c56e0),(0x6c5720,0x6c574f),(0x6c69a6,0x6c69d1),(0x6c6c8a,0x6c6c96),
        (0x6d8a69,0x6d8aa4),(0x6d8cfc,0x6d8d0e),(0x6d8ef5,0x6d8f07),
        (0x79b505,0x79b516),(0x79c1b3,0x79c1c0),(0x79c347,0x79c38a),
        (0x7f53fe,0x7f5416),(0x8dfcc0,0x8dfcc7)]
    lines=[]
    for line in listing.open():
        match=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if match and any(a<=int(match[1],16)<b for a,b in spans):
            at=int(match[1],16);code=bytes.fromhex(match[2]);assert memory.read(at,len(code))==code
            lines.append(line)
    pinned=ROOT/'evidence/reference_instructions/static-grid.txt';pinned.write_text(''.join(lines),encoding='ascii')
    assert memory.words(0x6c6cbc+15*4,1)==(0x6c69a6,)
    assert memory.words(0xb2a774,11)==(0x6c56e0,0x6d8ce0,0x407240,0x6d8950,0x6c5640,0x6c5650,0x6c5900,0x6c5660,0x6c6780,0x6c6790,0x6d8a40)
    #One existing ordinary wall-only record, retained verbatim. This is a
    #component fixture, not a new route, unlock, or substituted starting room.
    room=stages[35][12];assert room['shape']==2 and room['type']!=16
    assert len(room['spawns'])==18 and all(e['type']==1999 for s in room['spawns'] for e in s['entities'])
    catalog=json.loads((ROOT/'runtime/port/normal_catalog.json').read_text());stage=next(s for s in catalog['stages'] if s['id']==35)
    raw,binding=OriginalResources(ROOT/'runtime/game').read(stage['request'])
    record=raw[room['file_offset']:room['file_offset']+room['file_bytes']]
    (out/'static_wall_room.bin').write_bytes(b'NGSWALL1'+struct.pack('<II',room['id'],len(record))+record)
    report=dict(format='normal-static-wall-reference-v1',instructions=dict(path=pinned.relative_to(ROOT).as_posix(),sha256=sha(pinned),count=len(lines)),
        spans=[dict(start=f'{a:08x}',end_exclusive=f'{b:08x}',sha256=hashlib.sha256(memory.read(a,b-a)).hexdigest()) for a,b in spans],
        source=binding,fixture=dict(definition=room['id'],record_sha256=hashlib.sha256(record).hexdigest()),
        native_path='1999 -> grid15 at0079c1b3,006c5670/006c69a6 factory,006d8a40 Init,006d8ce0 ordinary update return, base damage=false at006c5640.',
        rng='Selected main award clone becomes native+14; no MT or additional local draw. Automatic boundary walls at0079c4d0 use global MT separately and are not invoked here.',
        scope='Ordinary selected static walls, construction/collision ownership and resident retention. Room16 graphics, automatic perimeter/doors, complete native entry and player ticks remain open.')
    report_path=ROOT/'runtime/port/normal_static_grid_reference.json';report_path.write_text(json.dumps(report,indent=2)+'\n')
    return ['tools/prepare_static_grid.py',pinned.relative_to(ROOT).as_posix(),report_path.relative_to(ROOT).as_posix(),'runtime/game/'+binding['file']]

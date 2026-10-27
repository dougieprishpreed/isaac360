"""Bind original indexed catalogue extents and fresh-player RNG initialization."""
from pathlib import Path
import hashlib,json,re,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def shift(n,a,b,c):
    n^=n>>a;n=(n^(n<<b))&0xffffffff;return n^(n>>c)
def catalogue():
    m=Memory(ROOT/'recovered/main');resources=OriginalResources(ROOT/'runtime/game')
    #006dc340 initializes indexed vectors, not a compact list of declarations.
    assert m.read(0x6dc345,5)==bytes.fromhex('68dd020000')
    assert m.read(0x6dc369,5)==bytes.fromhex('68be000000')
    assert m.read(0x6e7e70,3)==bytes.fromhex('83f932')
    assert m.read(0x6e7ee0,3)==bytes.fromhex('83f962')
    #006e43c0 dispatches card/rune to ItemConfig+24 and pilleffect to+30.
    #These are the vectors consumed by the fourth and third RNG banks.
    for address,value in ((0xb29918,b'card\0'),(0xb2b7e4,b'rune\0'),(0xb2bda0,b'pilleffect\0')):
        assert m.read(address,len(value))==value
    assert m.read(0x6e5683,3)==bytes.fromhex('8b4624')
    assert m.read(0x6e58f8,3)==bytes.fromhex('83c130')
    slots=[733,190,50,98];present=[set() for _ in slots];bindings=[]
    for request in ('items.xml','pocketitems.xml'):
        raw,binding=resources.read(request);bindings.append(binding)
        for e in ET.fromstring(raw):
            bank=0 if e.tag in ('passive','active','familiar') else 1 if e.tag=='trinket' else 2 if e.tag=='pilleffect' else 3 if e.tag in ('card','rune') else -1
            if bank>=0:
                i=int(e.get('id'));assert 0<=i<slots[bank],(request,bank,i)
                present[bank].add(i)
    #Current normal data never grows these native initial extents. Empty
    #indices (including collectible0) still receive RNG owners.
    generated=ROOT/'build/generated/native_player_startup_data.h';generated.parent.mkdir(parents=True,exist_ok=True)
    generated.write_text('//Original indexed ItemConfig extents; normal_player_startup_reference.json.\nstatic const unsigned int kNativePlayerSeedSlots[4]={733,190,50,98};\n',encoding='ascii')
    return m,slots,present,bindings
def prepare_catalogue():
    catalogue()
def prepare(out):
    m,slots,present,bindings=catalogue()
    spans=[(0x6dc340,0x6dc405),(0x6e7e60,0x6e7fa2),(0x904986,0x904a2a),
        (0x709365,0x70985f),(0x70993b,0x709988),(0x928190,0x92825e),(0x6b42db,0x6b4307),(0x6b4a19,0x6b4a35),
        (0x6e43f6,0x6e43fe),(0x6e4ac2,0x6e4b2a),(0x6e519c,0x6e5251),
        (0x6e55d8,0x6e5694),(0x6e56a5,0x6e5750),(0x6e58e9,0x6e5942),(0x6e5a22,0x6e5a2f)]
    lines=[]
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        match=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if match and any(a<=int(match[1],16)<b for a,b in spans):
            code=bytes.fromhex(match[2]);assert m.read(int(match[1],16),len(code))==code;lines.append(line)
    path=ROOT/'evidence/reference_instructions/player-startup.txt';path.write_text(''.join(lines),encoding='ascii')
    triples=[m.words(0x9e9a88+i*12,3) for i in range(5,10)]
    assert triples==[(1,11,16),(1,19,3),(1,21,20),(1,27,27),(2,5,15)]
    assert m.words(0x9e9c20,3)==(5,7,22)
    def fold(h,n):
        for byte in struct.pack('<I',n):h=((h^byte)*16777619)&0xffffffff
        return h
    rows=[]
    for seed in (1,17,0x12345678,0xffffffff,0x80000000,0x18000,0x9e3779b9,0xabcdef01):
        state=seed;banks=[]
        for bank,size in enumerate(slots):
            state=shift(state,*triples[0]);local=state;entries=[]
            for _ in range(size):local=shift(local,*triples[bank+1]);entries.append(local)
            banks.append(entries)
        entity=shift(state,*triples[0]);h=2166136261
        for n in (1,seed,13,entity):h=fold(h,n)
        for entries in banks:
            h=fold(h,len(entries))
            for value in entries:h=fold(fold(h,value),35)
        rows.append([seed,entity,h]+[n for entries in banks for n in (entries[0],entries[1],entries[-1])])
    (out/'player_startup.tsv').write_text(''.join('\t'.join(map(str,row))+'\n' for row in rows),encoding='ascii')
    report=dict(format='normal-player-startup-reference-v1',instructions=dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path),count=len(lines)),
        source_bindings=bindings,slots=slots,declared_indices=[len(p) for p in present],empty_indices=[sorted(set(range(n))-p) for n,p in zip(slots,present)],
        pocket_dispatch=dict(routine='006e43c0',card_and_rune_vector='ItemConfig+24, indexed write006e5683..8c',pill_effect_vector='ItemConfig+30, indexed write006e5a22..2a',tag_strings=['00b29918','00b2b7e4','00b2bda0']),
        spans=[dict(start=f'{a:08x}',end_exclusive=f'{b:08x}',sha256=hashlib.sha256(m.read(a,b-a)).hexdigest()) for a,b in spans],
        ownership='First-player manager remains seeded/index13 and undrawn. Independent index5 parent draws seed local6/7/8/9 banks, one child/index35 per indexed slot. Fifth parent draw is passed to base entity Init. No MT or floor owner is used.',
        scope='Fresh first-player RNG prefix and authoritative catalogue extents. Saved-instruction arithmetic expectations, not original execution. Full707b40 object construction,66f440 base entity/visual setup, stats/inventory, post-generation initialization and complete ticks remain open.')
    target=ROOT/'runtime/port/normal_player_startup_reference.json';target.write_text(json.dumps(report,indent=2)+'\n')
    return ['tools/prepare_player_startup.py',path.relative_to(ROOT).as_posix(),target.relative_to(ROOT).as_posix()]+['runtime/game/'+b['file'] for b in bindings]

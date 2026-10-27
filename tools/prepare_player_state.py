"""Bind player numeric Init and effect-cache metadata to preserved local sources."""
from pathlib import Path
import hashlib,json,re,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def catalogue():
    m=Memory(ROOT/'recovered/main');r=OriginalResources(ROOT/'runtime/game')
    #006db5b0's native string->cache-bit initializer (not public enum guesses).
    addresses=[0xb2b788,0xb2b734,0xb2b740,0xb2b74c,0xb2b754,0xb2b7bc,0xb2b7c8,
               0xb2b7d4,0xb2b7dc,0xb2b790,0xb2b79c,0xb2b7a4,0xb29810,0xb2b7ac]
    stores=[0x6db643,0x6db6c2,0x6db741,0x6db7c1,0x6db841,0x6db8c1,0x6db941,
            0x6db9c1,0x6dba41,0x6dbac1,0x6dbb41,0x6dbbc1,0x6dbc41,0x6dbcc1]
    flags={}
    for i,(address,store) in enumerate(zip(addresses,stores)):
        assert m.read(store,2)==b'\xc7\x00' and int.from_bytes(m.read(store+2,4),'little')==1<<i
        flags[m.read(address,24).split(b'\0')[0].decode('ascii')]=1<<i
    assert m.read(0x6da295,7)==bytes.fromhex('c7415400000000')
    assert m.read(0x6da2d4,7)==bytes.fromhex('c7417800000000')
    assert m.read(0x6da352,9)==bytes.fromhex('66c781b40000000001') #persistent=false.
    assert m.read(0x6dd88f,3)==bytes.fromhex('094754')
    assert m.read(0x6ddc26,3)==bytes.fromhex('894778')
    raw,binding=r.read('items.xml');rows=[];seen=set()
    types={'null':0,'passive':1,'trinket':2,'active':3,'familiar':4}
    for e in ET.fromstring(raw):
        if e.tag not in types:continue
        a=e.attrib;tokens=a.get('cache','').split();assert all(t in flags or t=='all' for t in tokens),tokens
        bits=0
        for t in tokens:bits|=0xffff if t=='all' else flags[t]
        value=a.get('persistent','false');persistent=value.lower()=='true' or value not in ('true','false') and bool(int(value or '0'))
        row=(types[e.tag],int(a['id']),bits,int(a.get('cooldown','0')),int(persistent))
        if row[:2] in seen:
            #Normal inherited XML repeats null124 verbatim. Its effective
            #numeric record is identical; retain the raw source unmodified.
            assert row in rows and e.tag=='null' and row[1]==124
            continue
        seen.add(row[:2]);rows.append(row)
    rows.sort();target=ROOT/'build/generated/native_player_effect_data.h';target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text('//Original inherited ItemConfig fields for effect cache. No unlock filtering.\nstatic const NativeEffectDef kNativeEffectDefs[]={\n'+
        ''.join('{%d,%d,%dU,%d,%s},\n'%(*row[:4],'true' if row[4] else 'false') for row in rows)+'};\n',encoding='ascii')
    return m,binding,flags,rows

def prepare_catalogue():catalogue()

def prepare(out):
    m,binding,flags,rows=catalogue()
    spans=[(0x6da240,0x6da3a2),(0x6db5b0,0x6dbe0d),(0x6dcfc0,0x6dd328),(0x6dd7fe,0x6dd8d3),
        (0x6ddbce,0x6ddc2e),(0x6de69b,0x6de817),(0x8793c0,0x8799b5),(0x879b20,0x879b60),
        (0x78bb50,0x78bb95),(0x908b40,0x908ba6),(0x707dd6,0x707e0d),(0x70998c,0x70999d),(0x7099a2,0x709ff6),
        (0x76c1d0,0x76c390),(0x76c54f,0x76c606),(0x76cc70,0x76ccca),(0x70ce20,0x70d00e),(0x9e9a88,0x9e9e54)]
    lines=[]
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        match=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if match and any(a<=int(match[1],16)<b for a,b in spans):
            code=bytes.fromhex(match[2]);assert m.read(int(match[1],16),len(code))==code;lines.append(line)
    p=ROOT/'evidence/reference_instructions/player-state-effects.txt';p.write_text(''.join(lines),encoding='ascii')
    (out/'player_effect_defs.tsv').write_text(''.join('\t'.join(map(str,row))+'\n' for row in rows),encoding='ascii')
    #Independent source-instruction arithmetic for the later child draw:
    #two index5 parent advances,98 index7 local advances to child97, then
    #one child/index35 advance. No maintained C++ RNG helper is used here.
    def draw(n,index):
        a,b,c=m.words(0x9e9a88+index*12,3)
        n^=n>>a;n=(n^(n<<b))&0xffffffff;return n^(n>>c)
    expected=[]
    for seed in (1,17,0x12345678,0xffffffff,0x80000000,0x18000,0x9e3779b9,0xabcdef01):
        local=draw(draw(seed,5),5)
        for _ in range(98):local=draw(local,7)
        after=draw(local,35);expected.append((seed,local,after,12+after%9))
    (out/'player_state_rng.tsv').write_text(''.join('\t'.join(map(str,row))+'\n' for row in expected),encoding='ascii')
    report=dict(format='normal-player-state-effects-v1',source_binding=binding,cache_flags=flags,definitions=len(rows),
        instructions=dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)),
        decoded_continuation=dict(path='evidence/reference_instructions/player-init-after-log.txt',sha256=sha(ROOT/'evidence/reference_instructions/player-init-after-log.txt')),
        spans=[dict(start=f'{a:08x}',end_exclusive=f'{b:08x}',sha256=hashlib.sha256(m.read(a,b-a)).hexdigest()) for a,b in spans],
        expectations='Original metadata, native byte-bound instructions, addressed literal interaction cases; no original execution or new capture.',
        scope='Numeric fresh player0 health/resources, zeroed inventory vectors, pending starting grants, trinket97 child advance and temporary-effect cache. Body Init HP1/entity collision5. Full constructor, item/costume/removal providers, recursive cache mutation, UI resource side effects, stat evaluation and complete ticks remain open.')
    q=ROOT/'runtime/port/normal_player_state_reference.json';q.write_text(json.dumps(report,indent=2)+'\n')
    return ['tools/prepare_player_state.py',p.relative_to(ROOT).as_posix(),q.relative_to(ROOT).as_posix(),
        'evidence/reference_instructions/player-init-after-log.txt','runtime/game/'+binding['file']]

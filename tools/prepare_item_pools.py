"""Immutable normal ItemPool inputs. Runtime code owns all selection and RNG."""
from pathlib import Path
import hashlib,json,re,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]

def prepare():
    m=Memory(ROOT/'recovered/main');r=OriginalResources(ROOT/'runtime/game')
    sources=[]
    def xml(path):
        raw,binding=r.read(path);sources.append(binding);return ET.fromstring(raw)
    items=xml('items.xml');pockets=xml('pocketitems.xml');pools=xml('itempools.xml')
    names=[m.cstring(p) for p in m.words(0x9e99c0,31)]
    assert len(set(names))==31 and {p.get('Name') for p in pools}==set(names)
    assert m.words(0x9e9aac,3)==(1,9,29) and m.words(0x9e9aa0,3)==(1,5,19)
    assert m.words(0x9e9c2c,3)==(5,9,7)
    def flag(value):
        return value in ('true','True') or (value.lstrip('-').isdigit() and int(value)!=0)
    def definition(node):
        return '{%d,%d,%s,%s},'%(int(node.get('id')),int(node.get('achievement','0')),
            str(flag(node.get('special','0'))).lower(),str(flag(node.get('hidden','0'))).lower())
    lines=['//Original normal-001 XML definitions; no prepared live state or expected RNG.',
        'struct NativePoolItemDef {int id,achievement;bool special,hidden;};',
        'static const NativePoolItemDef kPoolCollectibles[]={']
    lines += [definition(n) for n in items if n.tag in ('passive','active','familiar')]
    lines += ['};','static const NativePoolItemDef kPoolTrinkets[]={']
    lines += [definition(n) for n in items if n.tag=='trinket']
    lines += ['};','struct NativePillDef {int id,achievement,classification,announcer;};',
        'static const NativePillDef kPoolPills[]={']
    pills=sorted((n for n in pockets if n.tag=='pilleffect'),key=lambda n:int(n.get('id')))
    assert len(pills)==50 and all(n.get('class','')[0:1].isdigit() for n in pills)
    lines += ['{%s,%s,%s,%s},'%(n.get('id'),n.get('achievement','0'),n.get('class')[0],n.get('announcer','0')) for n in pills]
    cards=sorted((n for n in pockets if n.tag in ('card','rune')),key=lambda n:int(n.get('id')))
    assert [int(n.get('id')) for n in cards]==list(range(98))
    lines += ['};','struct NativePoolCardDef {int id,announcer;};','static const NativePoolCardDef kPoolCards[]={']
    lines += ['{%s,%s},'%(n.get('id'),n.get('announcer','0')) for n in cards]
    lines += ['};',
        'struct NativePoolEntryDef {int id;unsigned int weight,decrease,remove;};']
    def bits(value):return struct.unpack('<I',struct.pack('<f',float(value)))[0]
    for i,name in enumerate(names):
        pool=next(p for p in pools if p.get('Name')==name)
        lines+=['static const NativePoolEntryDef kPoolEntries%d[]={'%i]
        for n in pool:
            assert n.tag=='Item'
            lines+=['{%s,0x%08xU,0x%08xU,0x%08xU},'%(n.get('Id','0'),bits(n.get('Weight','1')),
                bits(n.get('DecreaseBy','.5')),bits(n.get('RemoveOn','.1')))]
        lines+=['};']
    lines+=['struct NativePoolDef {const char* name;const NativePoolEntryDef* entries;unsigned int count;};',
        'static const NativePoolDef kPoolDefinitions[]={']
    lines+=['{%s,kPoolEntries%d,sizeof(kPoolEntries%d)/sizeof(kPoolEntries%d[0])},'%(json.dumps(name),i,i,i) for i,name in enumerate(names)]
    lines+=['};']
    output=ROOT/'build/generated/native_item_pool_data.h';output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    spans=[]
    code_ranges=((0x6eb5e0,0xa6b),(0x6ec050,0x107),(0x6ec160,0xb58),(0x6ed210,0x12b),
                (0x6e74c0,0x352),(0x6ecf40,0x2ca),(0x6e4fa0,0x64),(0x8762f0,0x42),
                (0x6b4676,0x2f))
    for a,n in code_ranges+((0x9e99c0,124),(0x9e9aa0,24),(0x9e9c2c,12),(0x6b4749,0x153)):
        raw=m.read(a,n);assert raw is not None
        spans.append(dict(address='%08x'%a,bytes=n,sha256=hashlib.sha256(raw).hexdigest()))
    proof=ROOT/'evidence/reference_instructions/normal-item-pools.txt';rows=[]
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        match=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if match and any(a<=int(match[1],16)<a+n for a,n in code_ranges):
            code=bytes.fromhex(match[2]);assert m.read(int(match[1],16),len(code))==code;rows.append(line)
    proof.write_text(''.join(rows),encoding='ascii')
    (ROOT/'runtime/port/native_item_pool_reference.json').write_text(json.dumps(dict(
        format='normal-native-item-pool-inputs-v1',sources=sources,spans=spans,pool_names=names,
        instructions=dict(path=proof.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(proof.read_bytes()).hexdigest()),
        scope='6b47f8 ordinary pre-player init, challenge0, no mods/custom seed/daily, empty player manager; normal profile awards retained',
        announcers='Pill+3c/card+5c refer to sounds, not graphics. Clear native SFX+199 for each eligible pill then all98 cards/runes; an unloaded sound only logs at level0.'),indent=2)+'\n')

if __name__=='__main__':prepare()

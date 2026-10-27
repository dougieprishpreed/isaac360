"""Export explicitly separated prepared boss-pool profiles; no active mount change.

Raw XML is preserved. Both supplied pool files have a mismatched final root
closing tag; profile4 also has nested comment starts. The bounded export uses
first '-->' comment termination and completed pool/boss tags explicitly. It
does not repair the raw files or establish full native parser equivalence.
Native VFS/version binding and general XML parser parity are not established.
"""
from pathlib import Path
import hashlib,json,re,struct,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
STAGE_NAMES=('basement','cellar','burning basement','caves','catacombs','flooded caves','depths','necropolis','dank depths',
    'womb','utero','scarred womb','blue womb','sheol','cathedral','dark room','chest')
NAMES={name:i+1 for i,name in enumerate(STAGE_NAMES)}
NAMES.update({'void':26,'downpour':27,'dross':28,'mines':29,'ashpit':30,'mausoleum':31,'gehenna':32,'corpse':33,'mortis':34})
def f32(value):return struct.unpack('<f',struct.pack('<f',value))[0]
def literal(value):
    s=format(value,'.9g');return s+('.0' if '.' not in s and 'e' not in s else '')+'f'
def prepare():
    index=[json.loads(line) for line in (ROOT/'runtime/game/file_index.jsonl').open()]
    def resource(package,name):
        entry=next(e for e in index if e['package']==package and e['virtual_path']==name)
        raw=(ROOT/'runtime/game'/entry['file']).read_bytes()
        assert len(raw)==entry['bytes'] and hashlib.sha256(raw).hexdigest()==entry['sha256']
        return entry,raw
    lines=['// Explicit prepared metadata profiles. See runtime/port/native_boss_data.json.']
    reports=[]
    for profile,prefix in ((4,'resources/'),(12,'resources-dlc3/')):
        pool_source,raw=resource(f'package_{profile:02}',prefix+'bosspools.xml')
        portrait_source,portrait_raw=resource(f'package_{profile:02}',prefix+'bossportraits.xml')
        portraits=ET.fromstring(portrait_raw);achievements={int(n.attrib['id']):int(n.get('achievement','-1')) for n in portraits}
        text=re.sub(r'<!--.*?-->','',raw.decode('utf-8-sig'),flags=re.S)
        tokens=re.findall(r'<pool\b[^>]*?/\s*>|<pool\b[^>]*>.*?</pool>',text,flags=re.S)
        entries=[];pools=[dict(first=0,count=0,paired=0,total=0.) for _ in range(37)];seen=set()
        for token in tokens:
            tag=token[:token.index('>')+1];attrs=dict(re.findall(r'(\w+)="([^"]*)"',tag));stage=NAMES[attrs['name']]
            assert stage not in seen;seen.add(stage);first=len(entries);total=0.
            for boss in re.findall(r'<boss\b[^>]*/>',token):
                attrs_b=dict(re.findall(r'(\w+)="([^"]*)"',boss));identity=int(attrs_b['id']);weight=f32(float(attrs_b['weight']))
                assert 0<=identity<104 and identity in achievements and 0<=weight<=1000000
                entries.append(dict(id=identity,weight=weight,achievement=achievements[identity],variant=int(attrs_b.get('variant','0'))))
                total=f32(total+weight)
            pools[stage]=dict(first=first,count=len(entries)-first,paired=int(attrs.get('doubletrouble','0')),total=total)
        assert 0<len(entries)<512 and len(tokens)==len(seen)
        lines.append(f'static const NativeBossIdentityEntry kBossEntries{profile}[]={{')
        lines.extend('{%d,%s,%s,%d,%d},'%(e['id'],literal(e['weight']),literal(e['weight']),e['achievement'],e['variant']) for e in entries)
        lines.append('};');lines.append(f'static const NativeBossIdentityPoolData kBossPools{profile}[37]={{')
        lines.extend('{%d,%d,%d,%s},'%(p['first'],p['count'],p['paired'],literal(p['total'])) for p in pools);lines.append('};')
        reports.append(dict(profile=profile,pool_source=pool_source,portrait_source=portrait_source,defined_pools=sorted(seen),entries=entries,pools=pools))
    lines.append('static const NativeBossIdentityData kPreparedBossData[]={')
    lines.extend('{%d,%d,kBossPools%d,kBossEntries%d},'%(r['profile'],len(r['entries']),r['profile'],r['profile']) for r in reports);lines.append('};')
    generated=ROOT/'build/generated/native_boss_data.h';generated.write_text('\n'.join(lines)+'\n',encoding='ascii')
    paths=['tools/prepare_boss_data.py','recovered/main/disassembly.txt','build/generated/native_boss_data.h']
    report=dict(format='generic-native-boss-data-v1',profiles=reports,
        limitations=['Explicit prepared profiles4/12, not native VFS/mount precedence or reference-version equivalence. Active route/content policy unchanged.',
            'Bounded pool-token export accepts completed pools while preserving malformed raw root/comment text. Full native XML parser equivalence unproven.',
            'Achievement IDs are metadata only; caller must supply effective unlock state. Broad bosses/rooms remain unsupported in active gameplay.'],
        files=[dict(path=p,sha256=hashlib.sha256((ROOT/p).read_bytes()).hexdigest()) for p in paths])
    (ROOT/'runtime/port/native_boss_data.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Prepared explicit boss metadata profiles:',', '.join('%d=%d pools/%d entries'%(r['profile'],len(r['defined_pools']),len(r['entries'])) for r in reports))
if __name__=='__main__':prepare()

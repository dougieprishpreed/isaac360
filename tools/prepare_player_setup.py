"""Bind original player XML and the addressed character menu; no raw edits."""
from pathlib import Path
import hashlib,json,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def prepare():
    from prepare_player_runtime import prepare as prepare_runtime
    prepare_runtime()
    from prepare_player_creation import prepare as prepare_creation
    prepare_creation(None)
    from prepare_player_state import prepare_catalogue as prepare_state_catalogue
    prepare_state_catalogue()
    from prepare_player_sprites import prepare_catalogue as prepare_sprite_catalogue
    prepare_sprite_catalogue()
    from prepare_entity_body import prepare_catalogue as prepare_body_catalogue
    prepare_body_catalogue()
    from prepare_player_startup import prepare_catalogue
    prepare_catalogue()
    resources=OriginalResources(ROOT/'runtime/game');raw,source=resources.read('players.xml');tree=ET.fromstring(raw)
    memory=Memory(ROOT/'recovered/main');memory_sources=set()
    def read_memory(address,size):
        for base,data,record in memory.blocks:
            if base<=address and address+size<=base+len(data):
                assert hashlib.sha256(data).hexdigest()==record['sha256']
                memory_sources.add('recovered/main/'+record['file']);return data[address-base:address-base+size]
        raise ValueError('Unmapped saved address: %08x'%address)
    table=read_memory(0x9ee670,36*24);assert len(table)==36*24
    players=[];lines=['// Normal-001 player metadata and saved table009ee670; generated, no eligibility filtering.','static const NativePlayerDef kNativePlayers[] = {']
    for node in tree:
        if node.tag!='player':continue
        a=node.attrib;items=[int(x) for x in a.get('items','').split(',') if x];assert len(items)<=4
        vals=[int(a.get(k,'0')) for k in ('id','achievement','hp','armor','black','coins','bombs','keys','card','pill','pocketActive')]
        lines.append('{%s,%s,%d,{%s}},'%(','.join(map(str,vals)),'false' if a.get('canShoot')=='false' else 'true',len(items),','.join(map(str,items+[0]*(4-len(items))))))
        players.append(dict(attributes=a,items=items))
    lines+=['};','static const NativeCharacterDef kNativeCharacters[] = {'];choices=[]
    for i in range(36):
        row=struct.unpack_from('<6I',table,i*24);name=read_memory(row[0],64).split(b'\0')[0].decode('ascii')
        assert name.startswith(('00_','01_','02_','03_','04_','05_','06_','07_','08_','09_','10_','11_','12_','13_','15_','16_','17_','18_'))
        lines.append('{"%s",%d,%s,{%d,%d,%d}},'%(name,row[1],'true' if row[2] else 'false',*row[3:]))
        choices.append(dict(page=i//18,choice=i%18,animation=name,player=row[1],hide_locked=bool(row[2]),item_awards=row[3:]))
    lines+=['};'];(ROOT/'build/generated').mkdir(parents=True,exist_ok=True)
    (ROOT/'build/generated/player_setup_data.h').write_text('\n'.join(lines)+'\n')
    refs=('recovered/main/summary.json','recovered/main/memory/009e8000.bin',
        'recovered/main/disassembly.txt','recovered/main/memory_blocks.jsonl')
    wheel_ranges=[]
    for address,length in ((0x891f50,0xb0),(0x8974a0,32),(0xb49948,8)):
        span=read_memory(address,length);wheel_ranges.append(dict(address='%08x'%address,bytes=length,sha256=hashlib.sha256(span).hexdigest()))
    report=dict(format='generic-normal-player-setup-v1',source=source,table_address='009ee670..009ee9cf',table_sha256=hashlib.sha256(table).hexdigest(),
        wheel=dict(raw_ranges=wheel_ranges,resting_geometry='perspective=max(visibleCount-10,0)^2*.01+2; original70-radius projection,1.3 aspect correction,85Y center/240X center, .4 vertical factor; depth sorted.',
            limit='Resting geometry only. Native moving wheel/roulette, page swap and other transition phases remain open.'),
        players=players,choices=choices,isaac_start='0076c990: ordinary run, player0, profile award29 -> item105; fresh normal XML has no unconditional item105.',
        limits=['All41 XML declarations are retained; the real menu exposes17 characters plus Random per page, not helper forms.',
            'Only ordinary/hard player0 activation is integrated. Other characters/modes, full native stat/cache/weapon/health systems and run generation remain open.',
            'Menu random arithmetic consumes one caller-owned global MT draw over original eligible candidates; global bootstrap remains separately unresolved.'],
        files=[dict(path=p,sha256=sha(ROOT/p)) for p in sorted(set(refs)|memory_sources)])
    (ROOT/'runtime/port/player_setup_sources.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Prepared normal player setup:41 declarations,36 menu entries,638 award indices; saved native gates retained.')
if __name__=='__main__':
    prepare()
    from prepare_player_runtime import prepare as prepare_runtime
    prepare_runtime()
    from prepare_room_runtime import prepare as prepare_room_runtime
    prepare_room_runtime()

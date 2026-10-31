"""Bind the complete-edition Greed room files as a separate native catalogue."""
from pathlib import Path
import hashlib,json,re,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
#Greed rooms are selected by chapter/backdrop, not by the normal stages.xml.
STAGES={1:'resources-dlc3/rooms/greed/01.basement.stb',4:'resources-dlc3/rooms/greed/04.caves.stb',
    7:'resources-dlc3/rooms/greed/07.depths.stb',10:'resources-dlc3/rooms/greed/10.womb.stb',
    14:'resources-dlc3/rooms/greed/14.sheol.stb',18:'resources-dlc3/rooms/greed/00.special rooms.stb',
    24:'resources-dlc3/rooms/greed/24.the shop.stb',25:'resources-dlc3/rooms/greed/25.ultra greed.stb'}
def prepare():
    resources=OriginalResources(ROOT/'runtime/game');evidence=json.loads((ROOT/'evidence/rooms.json').read_text())
    sources=[];records=[];bindings=[];ranges={}
    for stage,request in sorted(STAGES.items()):
        try:e=resources.resolve(request)
        except ValueError as error:raise SystemExit(str(error))
        source=next(s for s in evidence if s['package']==e['package'] and s['sha256']==e['sha256'])
        assert source['format']=='STB1'
        if not any(s['request']==request for s in sources):sources.append(dict(request=request,**e))
        sid=next(i for i,s in enumerate(sources) if s['request']==request)
        blob=(ROOT/'runtime/game'/e['file']).read_bytes();assert hashlib.sha256(blob).hexdigest()==e['sha256'] and blob[:4]==b'STB1'
        fixture=ROOT/'recovered'/source['decoded'];rows=[json.loads(l) for l in fixture.open()];assert len(rows)==struct.unpack_from('<I',blob,4)[0]
        bindings.append(dict(path=fixture.relative_to(ROOT).as_posix(),sha256=sha(fixture)))
        first=len(records)
        for ordinal,r in enumerate(rows):
            assert r['index']==ordinal and r['shape'] in range(1,13)
            offset=r['file_offset'];assert struct.unpack_from('<IIIB',blob,offset)==(r['type'],r['variant'],r['subtype'],r['difficulty'])
            name=len(bytes.fromhex(r['name_bytes_hex']));assert struct.unpack_from('<I',blob,offset+15+name)[0]==r['weight_bits']
            coords=[(-1,3),(6,-1),(r['width'],3),(6,r['height']),(-1,10),(19,-1),(r['width'],10),(19,r['height'])]
            if r['shape']==9:coords[0],coords[1]=(12,3),(6,6)
            if r['shape']==10:coords[2],coords[5]=(13,3),(19,6)
            if r['shape']==11:coords[3],coords[4]=(6,7),(12,10)
            if r['shape']==12:coords[6],coords[7]=(13,10),(19,7)
            doors=0
            for d in r['doors']:
                if d['exists_byte']:doors|=1<<coords.index((d['x'],d['y']))
            records.append((0x20000+sid,stage,ordinal,r['type'],r['variant'],r['subtype'],r['difficulty'],r['shape'],doors,r['width'],r['height'],r['weight'],offset,r['file_bytes']))
        ranges[stage]=(0x20000+sid,first,len(rows))
    assert len(records)<32768 and max(ranges)<256
    lines=['// Complete-edition Greed room pools. Stage ids are greed chapter/backdrop ids.','static const NativeRoomDef kGreedRoomDefs[] = {']
    for r in records:lines.append(re.sub(r'(?<=[,{])(-?\d+)f(?=[,}])',r'\1.0f',('{%d,%d,%d,%d,%uU,%uU,%d,%d,%uU,%d,%d,%.9gf,%uU,%uU},'%r)))
    lines+=['};','static const NormalStageRange kGreedStageRanges[256] = {']
    for stage in range(256):
        s=ranges.get(stage);lines.append('{%d,%d,%d},'%s if s else '{-1,0,0},')
    lines+=['};','static const NativeRoomSourceDef kGreedRoomSources[] = {']
    for s in sources:lines.append('{"%s","%s",%d,{%s}},'%(s['request'],s['virtual_path'],int(s['package'].split('_')[1]),','.join('0x'+s['sha256'][i:i+2] for i in range(0,64,2))))
    fingerprint=int(hashlib.sha256(b''.join(bytes.fromhex(s['sha256']) for s in sources)).hexdigest()[:8],16)
    lines+=['};','static const unsigned int kGreedCatalogFingerprint=0x%08xU;'%fingerprint]
    (ROOT/'build/generated/greed_room_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    report=dict(format='complete-greed-stage-catalogue-v1',stages={str(k):v for k,v in STAGES.items()},sources=sources,
        stage_owned_records=len(records),fingerprint='%08x'%fingerprint,identity='0x20000000 + stage*65536 + raw ordinal.',
        limits=['Complete-edition Greed rooms are a separate catalogue; normal-stage binding remains unchanged.',
            'Wave selection/control flow is maintained runtime state, not a replacement room catalogue.'],
        files=sorted({e['path']:e for e in bindings+[dict(path=p,sha256=sha(ROOT/p)) for p in ('tools/prepare_greed_catalog.py','tools/original_resources.py','runtime/game/file_index.jsonl','evidence/rooms.json')]}.values(),key=lambda e:e['path']))
    (ROOT/'runtime/port/greed_catalog.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Prepared greed catalogue: %d sources/%d stage-owned records'%(len(sources),len(records)))
if __name__=='__main__':prepare()

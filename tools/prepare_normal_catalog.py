"""Bind ordinary normal-game stage pools through the confirmed resource rules."""
from pathlib import Path
import hashlib,json,re,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def prepare():
    resources=OriginalResources(ROOT/'runtime/game');raw,stage_source=resources.read('stages.xml');tree=ET.fromstring(raw)
    evidence=json.loads((ROOT/'evidence/rooms.json').read_text());sources=[];stages=[];records=[];bindings=[]
    for n in tree:
        stage=int(n.get('id'));request='rooms/'+str(Path(n.get('path')).with_suffix('.stb')).replace('\\','/').lower()
        try:e=resources.resolve(request)
        except ValueError as error:
            if str(error)!='Original resource missing: '+request:raise
            stages.append(dict(id=stage,attributes=n.attrib,request=request,source=-1,first=len(records),count=0,status='missing_resource'));continue
        source=next(s for s in evidence if s['package']==e['package'] and s['sha256']==e['sha256'])
        assert source['format']=='STB1','Other formats require an explicit native loader implementation'
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
            records.append((0x10000+sid,stage,ordinal,r['type'],r['variant'],r['subtype'],r['difficulty'],r['shape'],doors,r['width'],r['height'],r['weight'],offset,r['file_bytes']))
        stages.append(dict(id=stage,attributes=n.attrib,request=request,source=0x10000+sid,first=first,count=len(rows),status='bound'))
    assert len(records)<32768 and max(s['id'] for s in stages)<256
    lines=['// Normal-001 ordinary pools. Stage-owned identities preserve native pool ownership.','static const NativeRoomDef kNormalRoomDefs[] = {']
    for r in records:lines.append(re.sub(r'(?<=[,{])(-?\d+)f(?=[,}])',r'\1.0f',('{%d,%d,%d,%d,%uU,%uU,%d,%d,%uU,%d,%d,%.9gf,%uU,%uU},'%r)))
    lines+=['};','struct NormalStageRange {int source,first,count;};','static const NormalStageRange kNormalStageRanges[256] = {']
    for stage in range(256):
        s=next((s for s in stages if s['id']==stage),None);lines.append('{%d,%d,%d},'%(s['source'],s['first'],s['count']) if s else '{-1,0,0},')
    lines+=['};','static const NativeRoomSourceDef kNormalRoomSources[] = {']
    for s in sources:lines.append('{"%s","%s",%d,{%s}},'%(s['request'],s['virtual_path'],int(s['package'].split('_')[1]),','.join('0x'+s['sha256'][i:i+2] for i in range(0,64,2))))
    fingerprint=int(hashlib.sha256(raw+b''.join(bytes.fromhex(s['sha256']) for s in sources)).hexdigest()[:8],16)
    lines+=['};','static const unsigned int kNormalCatalogFingerprint=0x%08xU;'%fingerprint]
    (ROOT/'build/generated/normal_room_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    report=dict(format='generic-normal-stage-catalog-v1',configuration='normal-001',stage_source=stage_source,stages=stages,sources=sources,
        stage_owned_records=len(records),fingerprint='%08x'%fingerprint,identity='0x10000000 + stage*65536 + raw ordinal.',
        limits=['Ordinary stage pools only; alternative native-mode pools and challenge merges remain open.',
            'Stage24 has no resolved normal request. No file substitution or support filtering.',
            'Raw order, weights and stage-alias ownership are preserved; catalogue binding is not gameplay eligibility or a complete floor.'],
        files=sorted({e['path']:e for e in bindings+[dict(path=p,sha256=sha(ROOT/p)) for p in ('tools/prepare_normal_catalog.py','tools/original_resources.py','runtime/game/file_index.jsonl','evidence/rooms.json','recovered/main/disassembly.txt')]}.values(),key=lambda e:e['path']))
    (ROOT/'runtime/port/normal_catalog.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Prepared normal stage catalogue: %d declarations/%d bound sources/%d stage-owned records; missing requests explicit.'%(len(stages),len(sources),len(records)))
if __name__=='__main__':prepare()

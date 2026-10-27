"""Prepare original player animation words and addressed numeric state vectors."""
from pathlib import Path,PurePosixPath
import hashlib,json,math,re,struct,xml.etree.ElementTree as ET
from animation_cache import decode,encode
from extract_resources import filename_hash
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def f32(n):return struct.unpack('<f',struct.pack('<f',n))[0]
def bits(n):return struct.unpack('<I',struct.pack('<f',n))[0]
def fold(h,n):
    for b in struct.pack('<I',n&0xffffffff):h=((h^b)*16777619)&0xffffffff
    return h
def string(h,s):
    for b in s.encode('ascii')+b'\0':h=((h^b)*16777619)&0xffffffff
    return h
def frame_words(frame,end):
    w=frame['words'];assert len(w) in (5,11) and w[-1]>0
    assert frame['visible_byte'] in (0,1) and frame['interpolation_byte'] in (0,1)
    return w[:-1]+[0]*(11-len(w)),w[-1],frame['color_words'],frame['rotation_word'],end,frame['visible_byte'],frame['interpolation_byte']
def track_hash(h,track,layer):
    for n in (track['id'],track['visible_byte'],len(track['frames'])):h=fold(h,n)
    end=0
    for frame in track['frames']:
        end+=frame['words'][-1];g,delay,color,rotation,total,v,i=frame_words(frame,end)
        for n in g+[delay]+color+[rotation,total,v,i]:h=fold(h,n)
    return h
def catalogue():
    resource=OriginalResources(ROOT/'runtime/game');bindings=[]
    def read(path):
        raw,b=resource.read(path);bindings.append(dict(request=path,**b));return raw,b
    xml,b=read('entities2.xml');root=ET.fromstring(xml);assert root.get('anm2root')=='gfx/'
    entity=next(e for e in root if e.tag=='entity' and e.get('id')=='1' and e.get('variant')=='0' and int(e.get('subtype','0'))==0)
    request=root.get('anm2root')+entity.get('anm2path');_,source=read(request);path=source['virtual_path']
    raw,cache=read('animations.b');key=filename_hash(path)[0];found=[e for e in decode(raw) if int(e['lookup_hash'],16)==key]
    assert len(found)==1;asset=found[0];record=raw[asset['file_offset']:asset['file_offset']+asset['file_bytes']]
    assert encode([asset])[4:]==record and asset['default_animation']['text']=='WalkDown'
    base=asset['base_directory']['text'].replace('\\','/')
    if not base.lower().startswith('resources/'):base='resources/'+base
    assert PurePosixPath(base)==PurePosixPath(path).parent
    asset['canonical_path']=path
    for sheet in asset['spritesheets']:
        resolved=str(PurePosixPath(base)/sheet['path']['text'].replace('\\','/'));png,b=read(resolved.removeprefix('resources/'))
        assert png[:8]==b'\x89PNG\r\n\x1a\n' and png[12:16]==b'IHDR'
        sheet['canonical_path']=resolved;sheet['width'],sheet['height']=struct.unpack('>II',png[16:24])
    assert set((len(a['layer_tracks']),len(a['null_tracks'])) for a in asset['animations'])=={(15,8)}
    assert [x['id'] for x in asset['layers']]==list(range(15)) and [x['id'] for x in asset['nulls']]==list(range(8))
    lines=['//Original animation-cache record; normal_player_sprite_reference.json.']
    def track(name,t):
        end=0;rows=[]
        for frame in t['frames']:
            end+=frame['words'][-1];assert end<=0xffffffff
            g,d,c,r,total,v,i=frame_words(frame,end)
            rows.append('{{%s},%d,{%s},%d,%d,%s,%s},'%(','.join(str(x)+'U' for x in g),d,','.join(str(x)+'U' for x in c),r,total,'true' if v else 'false','true' if i else 'false'))
        if rows:lines.extend(['static const NativeSpriteFrame '+name+'[] = {']+rows+['};'])
        return '{%d,%s,%s,%d}'%(t['id'],'true' if t['visible_byte'] else 'false',name if rows else '0',len(rows))
    clips=[]
    for i,a in enumerate(asset['animations']):
        assert a['frame_count']>0 and a['loop_byte'] in (0,1)
        prefix='kPlayerAnim%d'%i;rootTrack=track(prefix+'Root',dict(id=-1,visible_byte=1,frames=a['root_frames']))
        groups=[]
        for group,keyName in (('layer_tracks','Layers'),('null_tracks','Nulls')):
            entries=[track(prefix+keyName+str(j),t) for j,t in enumerate(a[group])]
            lines+=['static const NativeSpriteTrack '+prefix+keyName+'[] = {']+[x+',' for x in entries]+['};'];groups.append((prefix+keyName,len(entries)))
        events=a['triggers']
        if events:lines+=['static const NativeSpriteEvent '+prefix+'Events[] = {']+['{%d,%d},'%(e['event'],e['frame']) for e in events]+['};']
        clips.append('{%s,%d,%s,%s,%s,%d,%s,%d,%s,%d},'%(json.dumps(a['name']['text']),a['frame_count'],'true' if a['loop_byte'] else 'false',rootTrack,*groups[0],*groups[1],prefix+'Events' if events else '0',len(events)))
    lines+=['static const NativeSpriteClip kPlayerClips[] = {']+clips+['};','static const NativeSpriteSheet kPlayerSheets[] = {']
    lines+=['{%d,%s,%d,%d},'%(s['id'],json.dumps(s['canonical_path']),s['width'],s['height']) for s in asset['spritesheets']]+['};','static const NativeSpriteLayer kPlayerLayers[] = {']
    lines+=['{%d,%d,%s},'%(s['id'],s['spritesheet'],json.dumps(s['name']['text'])) for s in asset['layers']]+['};']
    for group,name in (('nulls','kPlayerNulls'),('events','kPlayerEvents')):
        lines+=['static const NativeSpriteName '+name+'[] = {']+['{%d,%s},'%(n['id'],json.dumps(n['name']['text'])) for n in asset[group]]+['};']
    lines+=['static const NativeSpriteAsset kNativePlayerSprite = {%s,%s,kPlayerSheets,%d,kPlayerLayers,%d,kPlayerNulls,%d,kPlayerEvents,%d,kPlayerClips,%d};'%(json.dumps(path),json.dumps(asset['default_animation']['text']),len(asset['spritesheets']),len(asset['layers']),len(asset['nulls']),len(asset['events']),len(asset['animations']))]
    target=ROOT/'build/generated/native_player_sprite_data.h';target.parent.mkdir(parents=True,exist_ok=True);target.write_text('\n'.join(lines)+'\n',encoding='ascii')
    h=string(string(2166136261,path),asset['default_animation']['text']);h=fold(h,len(asset['spritesheets']))
    for s in asset['spritesheets']:
        h=fold(h,s['id']);h=string(h,s['canonical_path']);h=fold(fold(h,s['width']),s['height'])
    h=fold(h,len(asset['layers']))
    for s in asset['layers']:h=string(fold(fold(h,s['id']),s['spritesheet']),s['name']['text'])
    for group in ('nulls','events'):
        h=fold(h,len(asset[group]))
        for n in asset[group]:h=string(fold(h,n['id']),n['name']['text'])
    h=fold(h,len(asset['animations']))
    for a in asset['animations']:
        h=fold(fold(string(h,a['name']['text']),a['frame_count']),a['loop_byte']);h=track_hash(h,dict(id=-1,visible_byte=1,frames=a['root_frames']),False)
        for group in ('layer_tracks','null_tracks'):
            h=fold(h,len(a[group]))
            for t in a[group]:h=track_hash(h,t,group=='layer_tracks')
        h=fold(h,len(a['triggers']))
        for event in a['triggers']:h=fold(fold(h,event['event']),event['frame'])
    return asset,bindings,dict(lookup_hash=asset['lookup_hash'],offset=asset['file_offset'],bytes=asset['file_bytes'],sha256=hashlib.sha256(record).hexdigest(),roundtrip_exact=True,canonical_hash=h),record
def prepare_catalogue():
    catalogue()
    from prepare_player_aux_sprites import prepare_catalogue as auxiliary
    auxiliary()
def cursor_hash(clip,frame):
    h=2166136261
    for group in ('layer_tracks','null_tracks'):
        h=fold(h,len(clip[group]))
        for track in clip[group]:
            ends=[];end=0
            for f in track['frames']:end+=f['words'][-1];ends.append(end)
            #Last frame is retained; empty tracks retain native cursor0.
            cursor=sum(frame>=v for v in ends[:-1]);h=fold(h,cursor)
    return h
def timeline(clip,speed,steps):
    def mask(frames):
        n=0
        for e in clip['triggers']:
            if e['frame'] in frames:n|=1<<(e['event']&31)
        return n
    current=0.;playing=True;history=events=mask({0});rows=[]
    for _ in range(steps):
        if playing:
            candidate=f32(current+speed);target=math.floor(candidate)
            if target>=clip['frame_count'] and clip['loop_byte']:
                current=0.;events=history=mask({0});sampleFrame=0
            else:
                finished=target>=clip['frame_count'];target=min(target,clip['frame_count']-1)
                events=mask(set(range(math.floor(current)+1,target+1)));history|=events
                #00403c2d calls00403960(last), which only moves time forward
                #through integer boundaries. A last-frame fraction is retained.
                current=max(current,f32(target)) if finished else candidate;playing=not finished;sampleFrame=target
        else:sampleFrame=math.floor(current)
        rows.append([bits(current),int(playing),events,history,cursor_hash(clip,sampleFrame)])
    return rows
def prepare(out):
    asset,bindings,recordInfo,raw=catalogue();m=Memory(ROOT/'recovered/main')
    assert m.cstring(0xb21ab8)=='WalkDown' and m.cstring(0xb23808)=='HeadDown'
    symbols=ROOT/'recovered/main/symbols.jsonl'
    floorSymbol=next(json.loads(line) for line in symbols.open() if '"address":"009ce918"' in line)
    assert floorSymbol['name'].endswith('::floor')
    spans=[(0x402360,0x402524),(0x402d30,0x402f87),(0x4035a0,0x403c7f),(0x405090,0x4050f5),(0x405250,0x4052c6),
        (0x4053a0,0x40547d),(0x4054a0,0x405558),(0x4055b0,0x40560d),(0x405760,0x4057d6),(0x405910,0x4059c8),
        (0x406d50,0x406e9c),(0x409730,0x409a00),(0x707b80,0x707c09)]
    lines=[]
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        match=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if match and any(a<=int(match[1],16)<b for a,b in spans):
            code=bytes.fromhex(match[2]);assert m.read(int(match[1],16),len(code))==code;lines.append(line)
    proof=ROOT/'evidence/reference_instructions/player-sprite.txt';proof.write_text(''.join(lines),encoding='ascii')
    #Preserve the already decoded saved Init continuation; no new capture.
    saved=ROOT/'evidence/reference_instructions/player-sprite-init.txt'
    assert saved.is_file(),'Missing preserved bounded Init continuation; do not recapture it during routine preparation.'
    textRows=[];cases=frames=0
    for i,a in enumerate(asset['animations']):
        for speed in (.5,1.,2.,80.):
            steps=a['frame_count']*2+3;textRows.append('A\t%d\t%d\t%d'%(i,bits(speed),steps));cases+=1
            for row in timeline(a,speed,steps):textRows.append('F\t'+'\t'.join(map(str,row)));frames+=1
    (out/'player_sprite_timelines.tsv').write_text('\n'.join(textRows)+'\n',encoding='ascii')
    (out/'player_sprite_record.bin').write_bytes(raw)
    (out/'player_sprite_asset.tsv').write_text('%d\t%d\t%d\t%d\t%d\n'%(recordInfo['canonical_hash'],len(asset['animations']),len(asset['layers']),len(asset['nulls']),len(asset['events'])),encoding='ascii')
    report=dict(format='normal-player-sprite-reference-v1',bindings=bindings,record=recordInfo,
        instructions=dict(path=proof.relative_to(ROOT).as_posix(),sha256=sha(proof),count=len(lines)),
        spans=[dict(start=f'{a:08x}',end_exclusive=f'{b:08x}',sha256=hashlib.sha256(m.read(a,b-a)).hexdigest()) for a,b in spans],
        init_continuation=dict(path=saved.relative_to(ROOT).as_posix(),sha256=sha(saved),start='0070a730',end_exclusive='0070a8db',range_sha256=hashlib.sha256(m.read(0x70a730,0x70a8db-0x70a730)).hexdigest()),
        cases=cases,component_updates=frames,floor_import=floorSymbol,
        scope='14 direct Sprite numeric owners from native construction; property-selected body/head original cache words, timing/cursors/events. Main WalkDown0 and overlay HeadDown0 stopped as in Init. Auxiliary Init assets have a separate binding/report. Costumes, interpolated drawing, texture upload, full player Init and complete ticks remain open.',
        expectations='Original cache data, byte-pinned instructions and independent integer-crossing timeline expectations. Component arithmetic/regression evidence, not original runtime observation or target playability.')
    target=ROOT/'runtime/port/normal_player_sprite_reference.json';target.write_text(json.dumps(report,indent=2)+'\n')
    return ['tools/prepare_player_sprites.py','tools/animation_cache.py','recovered/main/symbols.jsonl',proof.relative_to(ROOT).as_posix(),saved.relative_to(ROOT).as_posix(),target.relative_to(ROOT).as_posix()]+['runtime/game/'+b['file'] for b in bindings]

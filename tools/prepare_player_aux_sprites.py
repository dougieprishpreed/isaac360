"""Original player Init auxiliary assets, including normal XML cache misses."""
from pathlib import Path,PurePosixPath
import hashlib,json,posixpath,re,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from animation_cache import decode,encode
from extract_resources import filename_hash
from reference_memory import Memory
from sprite_asset_export import f32,bits,emit_asset,asset_hash
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def text(s):return dict(text=s)
def boolean(s):
    assert s.lower() in ('true','false'),s
    return int(s.lower()=='true')
def xml_asset(raw,path,m):
    #0040dbb7..d8 rounds atof to float before division by100. The colour
    #reader similarly divides rounded floats by255 at0040df37..58.
    scale=struct.unpack('<f',m.read(0xb49c98,4))[0];tint=struct.unpack('<f',m.read(0xb49d88,4))[0]
    assert scale==100 and tint==255
    root=ET.fromstring(raw);assert root.tag=='AnimatedActor'
    content=root.find('Content');animations=root.find('Animations')
    def frame(e,layer):
        a=e.attrib
        fields=('XCrop','YCrop','Width','Height','XPosition','YPosition','XScale','YScale','XPivot','YPivot') if layer else ('XPosition','YPosition','XScale','YScale')
        words=[]
        for key in fields:
            n=f32(float(a[key]));words.append(bits(f32(n/scale) if key in ('XScale','YScale') else n))
        delay=int(a['Delay']);assert delay>0
        color=[bits(f32(f32(float(a[k]))/tint)) for k in ('RedTint','GreenTint','BlueTint','AlphaTint','RedOffset','GreenOffset','BlueOffset')]
        return dict(words=words+[delay],visible_byte=boolean(a['Visible']),color_words=color,rotation_word=bits(float(a['Rotation'])),interpolation_byte=boolean(a['Interpolated']))
    sheets=[dict(id=int(e.attrib['Id']),path=text(e.attrib['Path'])) for e in content.find('Spritesheets')]
    layers=[dict(id=int(e.attrib['Id']),spritesheet=int(e.attrib['SpritesheetId']),name=text(e.attrib['Name'])) for e in content.find('Layers')]
    assert [e['id'] for e in layers]==list(range(len(layers)))
    nulls=[dict(id=int(e.attrib['Id']),name=text(e.attrib['Name'])) for e in content.find('Nulls')]
    assert not list(content.find('Events')),'Event declarations still require the native XML event reader.'
    clips=[]
    for a in animations:
        tracks=[dict(id=int(t.attrib['LayerId']),visible_byte=boolean(t.attrib['Visible']),frames=[frame(f,True) for f in t]) for t in a.find('LayerAnimations')]
        assert sorted(t['id'] for t in tracks)==[l['id'] for l in layers]
        null_tracks=[dict(id=int(t.attrib['NullId']),visible_byte=boolean(t.attrib['Visible']),frames=[frame(f,False) for f in t]) for t in a.find('NullAnimations')]
        assert sorted(t['id'] for t in null_tracks)==sorted(n['id'] for n in nulls)
        assert not list(a.find('Triggers'))
        clips.append(dict(name=text(a.attrib['Name']),frame_count=int(a.attrib['FrameNum']),loop_byte=boolean(a.attrib['Loop']),
            root_frames=[frame(f,False) for f in a.find('RootAnimation')],layer_tracks=tracks,null_tracks=null_tracks,triggers=[]))
    return dict(canonical_path=path,default_animation=text(animations.attrib['DefaultAnimation']),spritesheets=sheets,layers=layers,nulls=nulls,events=[],animations=clips)

def catalogue():
    m=Memory(ROOT/'recovered/main');resource=OriginalResources(ROOT/'runtime/game');bindings=[]
    def read(path):
        raw,b=resource.read(path);bindings.append(dict(request=path,**b));return raw,b
    #The first two charge owners deliberately share one immutable asset.
    requests=[(11,m.cstring(0xb26c24)),(2,m.cstring(0xb2ce38)),(3,m.cstring(0xb2cfcc))]+[(4+i,m.cstring(p)) for i,p in enumerate(m.words(0xb2cd40,7))]
    raw,_=read('animations.b');cache={int(e['lookup_hash'],16):e for e in decode(raw)}
    lines=['//Original auxiliary Init assets; normal_player_aux_sprite_reference.json.'];assets=[];records=[];slots=[];byRequest={}
    for slot,request in requests:
        key=request.lower()
        if key in byRequest:slots.append((slot,request,byRequest[key]));continue
        xml,b=read(request);path=b['virtual_path'];candidate=xml_asset(xml,path,m)
        packed=cache.get(filename_hash(path)[0]);record=dict(request=request,path=path,source='xml-native-reader',cache_match=False)
        if packed:
            record.update(source='original-cache',offset=packed['file_offset'],bytes=packed['file_bytes'],sha256=hashlib.sha256(raw[packed['file_offset']:packed['file_offset']+packed['file_bytes']]).hexdigest())
            assert encode([packed])[4:]==raw[packed['file_offset']:packed['file_offset']+packed['file_bytes']]
            #Compare independent XML conversion to every native cache word;
            #runtime still uses the original cached record on cache hits.
            for field in ('default_animation','layers','nulls','events','animations'):
                def numeric(v):
                    if isinstance(v,list):return [numeric(x) for x in v]
                    if isinstance(v,dict):return {k:numeric(v[k]) for k in v if k in ('id','name','text','spritesheet','frame_count','loop_byte','root_frames','layer_tracks','null_tracks','triggers','frames','words','visible_byte','color_words','rotation_word','interpolation_byte','frame','event')}
                    return v
                assert numeric(candidate[field])==numeric(packed[field]),(request,field)
            record['cache_match']=True;asset=packed;asset['canonical_path']=path
        else:asset=candidate
        base=str(PurePosixPath(path).parent)
        for sheet in asset['spritesheets']:
            resolved=posixpath.normpath(base+'/'+sheet['path']['text'].replace('\\','/'))
            #Resolve through the same normal roots as the XML loader. The
            #source may inherit a base PNG even when the XML lives in dlc3.
            requestPng=resolved.split('/',1)[1];png,p=read(requestPng)
            assert png[:8]==b'\x89PNG\r\n\x1a\n' and png[12:16]==b'IHDR'
            sheet.update(canonical_path=resolved,width=struct.unpack('>I',png[16:20])[0],height=struct.unpack('>I',png[20:24])[0])
        index=len(assets);byRequest[key]=index;assets.append(asset);slots.append((slot,request,index))
        record.update(canonical_hash=asset_hash(asset),clips=len(asset['animations']),layers=len(asset['layers']),sheets=len(asset['spritesheets']))
        records.append(record);emit_asset(lines,asset,'kAuxSprite%d'%index)
    lines+=['static const NativePlayerSpriteInit kNativePlayerSpriteInit[]={']
    lines+=['{%d,%s,&kAuxSprite%d,%s,%d},'%(slot,json.dumps(request),index,'"Idle"' if slot==11 else '0',1 if slot in (2,3) else -1) for slot,request,index in slots]+['};']
    target=ROOT/'build/generated/native_player_aux_sprite_data.h';target.parent.mkdir(parents=True,exist_ok=True);target.write_text('\n'.join(lines)+'\n',encoding='ascii')
    return m,bindings,assets,records,slots

def prepare_catalogue():catalogue()
def prepare(out):
    m,bindings,assets,records,slots=catalogue()
    #Keep the already prepared Init decode; this is a saved-byte decode,
    #not a program capture. Preserve its needed earlier section outside build.
    source=ROOT/'build/reference_ranges/0070a1f8_0070ad00.txt'
    saved=ROOT/'evidence/reference_instructions/player-aux-sprite-init.txt'
    if not saved.is_file():
        rows=[]
        for line in source.read_text().splitlines():
            match=re.match(r'  ([0-9A-F]{8}):',line)
            if match:keep=0x70a344<=int(match[1],16)<0x70a730
            if 'keep' in locals() and keep:rows.append(line)
        saved.write_text('Bounded decode of preserved saved .text; no original program run.\n'+'\n'.join(rows)+'\n',encoding='ascii')
    for line in saved.read_text().splitlines():
        match=re.match(r'  ([0-9A-F]{8}):((?: [0-9A-F]{2})+)',line)
        if match:
            code=bytes.fromhex(match[2]);assert m.read(int(match[1],16),len(code))==code
    spans=[(0x40db79,0x40e43a),(0x94bb50,0x94bbe6),(0x70a344,0x70a8db),(0xb2cd40,0xb2cd5c)]
    proof=ROOT/'evidence/reference_instructions/player-aux-sprite.txt';rows=[]
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        match=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if match and any(a<=int(match[1],16)<b for a,b in spans):
            code=bytes.fromhex(match[2]);assert m.read(int(match[1],16),len(code))==code;rows.append(line)
    proof.write_text(''.join(rows),encoding='ascii')
    (out/'player_aux_assets.tsv').write_text(''.join('%s\t%d\t%d\t%d\n'%(a['canonical_path'],asset_hash(a),len(a['animations']),len(a['layers'])) for a in assets),encoding='ascii')
    (out/'player_aux_startup.tsv').write_text(''.join('%d\t%s\t%d\t%d\n'%(slot,request,1 if slot==11 else 0,1 if slot in (2,3) else -1) for slot,request,index in slots),encoding='ascii')
    #All original auxiliary clips exercise existing runtime timing on their
    #actual dimensions, including empty null vectors and nonloop charge bars.
    from prepare_player_sprites import timeline
    lines=[];cases=frames=0
    for assetIndex,a in enumerate(assets):
        for clipIndex,clip in enumerate(a['animations']):
            for speed in (.5,2.,200.):
                steps=clip['frame_count']+3;lines.append('A\t%d\t%d\t%d\t%d'%(assetIndex,clipIndex,bits(speed),steps));cases+=1
                for row in timeline(clip,speed,steps):lines.append('F\t'+'\t'.join(map(str,row)));frames+=1
    (out/'player_aux_timelines.tsv').write_text('\n'.join(lines)+'\n',encoding='ascii')
    report=dict(format='normal-player-aux-sprites-v1',bindings=bindings,records=records,slots=[dict(slot=s,request=r,asset=i) for s,r,i in slots],
        cases=cases,component_updates=frames,xml_cache_matches=sum(r['cache_match'] for r in records),
        instructions=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in (saved,proof)],
        spans=[dict(start=f'{a:08x}',end_exclusive=f'{b:08x}',sha256=hashlib.sha256(m.read(a,b-a)).hexdigest()) for a,b in spans],
        scope='Original blood-gush, crown and seven charge Sprite owners on fresh player Init. Cache hits retain original words; four native XML misses use saved-reader conversions. Independent mutable instances, native Idle, viewport-scale bindings and explicit layer1 blend preset2. No GPU/default-render-state resolution, costumes, item grants or complete Init/ticks.',
        expectations='Original data/cache cross-checks, saved instruction arithmetic and maintained timeline regression; no original execution or target claim.')
    target=ROOT/'runtime/port/normal_player_aux_sprite_reference.json';target.write_text(json.dumps(report,indent=2)+'\n')
    return ['tools/prepare_player_aux_sprites.py','tools/sprite_asset_export.py',saved.relative_to(ROOT).as_posix(),proof.relative_to(ROOT).as_posix(),target.relative_to(ROOT).as_posix()]+['runtime/game/'+b['file'] for b in bindings]

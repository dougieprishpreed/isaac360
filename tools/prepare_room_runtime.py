"""Prepare original resources used by the reached first-room FX initialization.

No expected gameplay state is generated. The runtime owns all random choices.
"""
from pathlib import Path,PurePosixPath
import hashlib,json,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
from animation_cache import decode,encode
from extract_resources import filename_hash
from sprite_asset_export import emit_asset,bits
from prepare_player_aux_sprites import xml_asset
from prepare_entity_body import literal
ROOT=Path(__file__).resolve().parents[1]

def prepare():
    resources=OriginalResources(ROOT/'runtime/game');m=Memory(ROOT/'recovered/main');bindings=[]
    def read(request):
        raw,b=resources.read(request);bindings.append(dict(request=request,**b));return raw,b
    raw,b=read('fxlayers.xml');root=ET.fromstring(b'<nativeDocument>'+raw+b'</nativeDocument>')
    fx=root.find('fxLayers');entry=next(x for x in fx if x.get('id')=='7')
    assert entry.attrib==dict(id='7',path='Overlays/fx_grid.png',numLayers='1',xMin='-10',xMax='10',yMin='-10',yMax='10',layer='lighting',stage='1',altStages='7',parallax='0.025')
    pngs=[]
    def png(request):
        raw,b=read(request);assert raw[:8]==b'\x89PNG\r\n\x1a\n';w,h=struct.unpack('>II',raw[16:24]);return b['virtual_path'],w,h
    for x in entry:
        if x.get('roomshape')=='1':pngs.append(png(fx.get('gfxroot')+x.get('path')))
    assert len(pngs)==5
    lines=['//Original normal first-chapter FX resources; choices are made in C++.',
           'static const NativeSpriteSheet kRoomLightingTextures[]={']
    lines+=['{%d,%s,%d,%d},'%(i,json.dumps(p),w,h) for i,(p,w,h) in enumerate(pngs)]+['};']
    groups=[];rays=[]
    for x in root.find('fxRays'):
        assert x.get('stages') in ('1a,1b','6ab')
        first=len(rays)
        for e in x:
            p,w,h=png(fx.get('gfxroot')+e.get('path'));rays.append((p,w,h,float(e.get('opacity')),float(e.get('animationSpeed'))))
        groups.append((float(x.get('xMin')),float(x.get('xMax')),float(x.get('topParallax')),float(x.get('bottomParallax')),float(x.get('rayLength')),float(x.get('raySpacing')),float(x.get('perspective')),first,len(rays)-first,x.get('stages')=='1a,1b'))
    lines+=['static const NativeRoomFxRayDef kRoomRayDefs[]={']
    lines+=['{{%d,%s,%d,%d},0x%08xU,0x%08xU},'%(i,json.dumps(p),w,h,bits(op),bits(speed)) for i,(p,w,h,op,speed) in enumerate(rays)]+['};']
    lines+=['static const NativeRoomFxRayGroupDef kRoomRayGroups[]={']
    lines+=['{{%s},%d,%d,%s},'%(','.join('0x%08xU'%bits(v) for v in g[:7]),g[7],g[8],'true' if g[9] else 'false') for g in groups]+['};']
    params=next(e for e in root.find('fxParams') if e.get('stage')=='1' and e.get('altStages')=='3');assert not list(params)
    lines+=['static const unsigned int kRoomShadowAlpha=0x%08xU;'%bits(float(params.get('shadowAlpha'))),
        'static const unsigned int kRoomRayPhaseScale=0x%08xU;'%m.words(0xb49a0c,1)[0]]
    p,w,h=png(m.cstring(0xb2e130))
    lines+=['static const NativeSpriteSheet kRoomShadingTexture={0,%s,%d,%d};'%(json.dumps(p),w,h)]
    #Retain the original cache record, including every timeline and event.
    xml,b=read(m.cstring(0xb3a180));raw,cacheBinding=read('animations.b');path=b['virtual_path']
    matches=[e for e in decode(raw) if int(e['lookup_hash'],16)==filename_hash(path)[0]]
    if matches:
        assert len(matches)==1;asset=matches[0];assert encode([asset])[4:]==raw[asset['file_offset']:asset['file_offset']+asset['file_bytes']];asset['canonical_path']=path
    else:asset=xml_asset(xml,path,m)
    for sheet in asset['spritesheets']:
        request=str(PurePosixPath(path).parent/PurePosixPath(sheet['path']['text'].replace('\\','/'))).split('/',1)[1]
        p,w,h=png(request);sheet.update(canonical_path=p,width=w,height=h)
    emit_asset(lines,asset,'kRoomVeinsSprite')
    (ROOT/'build/generated/native_room_fx_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    effect_lines=['//Inherited entity records and original animation cache; no generated entity state.']
    entityRaw,entityBinding=read('entities2.xml');entities=ET.fromstring(entityRaw);entries=decode(raw)
    effect_names=[];properties=[]
    for variant in (21,33,68):
        e=next(e for e in entities if e.get('id')=='1000' and int(e.get('variant','0'))==variant and int(e.get('subtype','0'))==0)
        request=entities.get('anm2root')+e.get('anm2path');xml,b=read(request);path=b['virtual_path']
        matches=[a for a in entries if int(a['lookup_hash'],16)==filename_hash(path)[0]]
        assert len(matches)==1;asset=matches[0];asset['canonical_path']=path
        assert encode([asset])[4:]==raw[asset['file_offset']:asset['file_offset']+asset['file_bytes']]
        for sheet in asset['spritesheets']:
            request=str(PurePosixPath(path).parent/PurePosixPath(sheet['path']['text'].replace('\\','/'))).split('/',1)[1]
            p,w,h=png(request);sheet.update(canonical_path=p,width=w,height=h)
        name='kRoomEffectSprite%d'%variant;emit_asset(effect_lines,asset,name);effect_names.append(name)
        values=[int(e.get('shadowSize','0'))/100.,float(e.get('friction','0')),float(e.get('collisionRadius','1')),
            float(e.get('collisionRadiusXMulti','1')),float(e.get('collisionRadiusYMulti','1')),float(e.get('collisionDamage','0')),
            float(e.get('collisionMass','0')),float(e.get('baseHP','0')),float(e.get('stageHP','0'))]
        assert not e.findall('gridcollision') and 'gridcollision' not in e.attrib
        properties.append('{1000,%d,0,%d,%s,%s},'%(variant,int(e.get('numGridCollisionPoints','0')),','.join(map(literal,values)),json.dumps(path)))
    effect_lines+=['static const NativeEntityBodyProperties kRoomEffectBodies[]={']+properties+['};',
        'static const NativeSpriteAsset* const kRoomEffectSprites[]={'+','.join('&'+n for n in effect_names)+'};']
    constants={'GlobalFloatScale':0xb49334,'Pi':0xb49a0c,'FlyFriction':0xb497a4,'Half':0xb49618,'Two':0xb49940,
        'FlyDistance':0xb49c28,'FlySquaredDistance':0xb49e9c,'FlyForce':0xb49f8c,'FlyRandomForce':0xb49744,
        'Epsilon':0xb493a8,'WallCoordinate':0xb49ec8,'GridSize':0xb49bd8,'TurnScale':0xb49bfc,'TurnOffset':0xb49b88,
        'RightAngle':0xb49c80,'LeftAngle':0xb4a0d8,'WallTurnRate':0xb494ec,'Radians':0xb493c4,
        'BugFriction':0xb49700,'BugWallDistance':0xb49b64,'BugWallVelocity':0xb49468}
    effect_lines+=['static const unsigned int kEffect%s=0x%08xU; //%08x'%(n,m.words(a,1)[0],a) for n,a in constants.items()]
    (ROOT/'build/generated/native_room_effect_data.h').write_text('\n'.join(effect_lines)+'\n',encoding='ascii')
    #Original static viewport initialization and16:9 fallback at89d33c.
    assert m.read(0x89d33c,10)==bytes.fromhex('b8e0010000b90e010000')
    assert m.words(0xb88594,2)==(bits(270),bits(480))
    view_constants={'ViewportWidth':0xb88598,'ViewportHeight':0xb88594,'RoomWidth':0xb49dc4,'RoomHeight':0xb49d28,
        'CameraBorder':0xb49c08,'GridScale':0xb4967c,'CameraPadding':0xb49c70,'CameraXReference':0xb4a070,
        'CameraYReference':0xb49c28,'WorldXReference':0xb49c28,'WorldYReference':0xb49ce0,'Flag512Brightness':0xb493dc}
    (ROOT/'build/generated/native_room_view_data.h').write_text('//Saved viewport/camera/lighting operands.\n'+''.join(
        'static const unsigned int kView%s=0x%08xU; //%08x\n'%(name,m.words(address,1)[0],address) for name,address in view_constants.items()),encoding='ascii')
    #+64 of the native13c-byte backdrop record is the original walls count:
    #87be16 -> localfd04,87ce77 ->87d282..288. It gates wall bugs at7a3184.
    backdropRaw,backdropBinding=read('backdrops.xml');backdrops=ET.fromstring(backdropRaw)
    background_lines=['//Original backdrops.xml and cached controls; choices remain in C++.']
    definitions=[]
    for i in (1,2):
        e=next(e for e in backdrops if e.get('id')==str(i))
        assert not e.get('anm2') and int(e.get('walls'))>0
        sheets=[]
        for key in ('gfx','lfloorgfx','nfloorgfx'):
            p,w,h=png(backdrops.get('gfxroot')+e.get(key));sheets.append('{0,%s,%d,%d}'%(json.dumps(p),w,h))
        definitions.append('{%s,%s},'%(','.join(str(int(e.get(key))) for key in ('id','walls','wallvariants','floors','floorvariants')),','.join(sheets)))
    background_lines+=['static const NativeRoomBackgroundDef kBackgroundDefs[]={']+definitions+['};']
    names=[]
    for i,request in enumerate(('gfx/backdrop/controls.anm2','gfx/backdrop/controls_buttons.anm2')):
        xml,b=read(request);path=b['virtual_path'];matches=[a for a in entries if int(a['lookup_hash'],16)==filename_hash(path)[0]]
        if matches:
            assert len(matches)==1;asset=matches[0];asset['canonical_path']=path
            assert encode([asset])[4:]==raw[asset['file_offset']:asset['file_offset']+asset['file_bytes']]
        else:asset=xml_asset(xml,path,m)
        for sheet in asset['spritesheets']:
            request=str(PurePosixPath(path).parent/PurePosixPath(sheet['path']['text'].replace('\\','/'))).split('/',1)[1]
            p,w,h=png(request);sheet.update(canonical_path=p,width=w,height=h)
        name='kBackgroundControls%d'%i;emit_asset(background_lines,asset,name);names.append(name)
    background_lines+=['static const NativeSpriteAsset* const kBackgroundSprites[]={'+','.join('&'+n for n in names)+'};']
    background_lines+=['static const unsigned int kBackground%s=0x%08xU; //%08x'%(name,m.words(address,1)[0],address)
        for name,address in (('Unit',0xb49b90),('Border',0xb49c08))]
    (ROOT/'build/generated/native_room_background_data.h').write_text('\n'.join(background_lines)+'\n',encoding='ascii')
    door_lines=['//Reached native door assets and inherited normal backdrop sheets.'];names=[]
    for i,address in enumerate((0xb2ab04,0xb2ac4c,0xb2aa18)):
        xml,b=read(m.cstring(address));path=b['virtual_path'];matches=[a for a in entries if int(a['lookup_hash'],16)==filename_hash(path)[0]]
        if matches:
            assert len(matches)==1;asset=matches[0];asset['canonical_path']=path
            assert encode([asset])[4:]==raw[asset['file_offset']:asset['file_offset']+asset['file_bytes']]
        else:asset=xml_asset(xml,path,m)
        for sheet in asset['spritesheets']:
            request=str(PurePosixPath(path).parent/PurePosixPath(sheet['path']['text'].replace('\\','/'))).split('/',1)[1]
            p,w,h=png(request);sheet.update(canonical_path=p,width=w,height=h)
        name='kDoorAsset%d'%i;emit_asset(door_lines,asset,name);names.append(name)
    door_lines+=['static const NativeSpriteAsset* const kDoorSprites[]={'+','.join('&'+n for n in names)+'};',
        'static const NativeSpriteSheet kDoorSheets[]={']
    for i,request in enumerate([backdrops.get('gridgfxroot')+next(e for e in backdrops if e.get('id')==str(i)).get('door') for i in (1,2)]+[m.cstring(0xb2ad80)]):
        p,w,h=png(request);door_lines.append('{%d,%s,%d,%d},'%(i,json.dumps(p),w,h))
    door_lines+=['};','static const char* const kDoorKeyClosed='+json.dumps(m.cstring(0xb2aa0c))+';']
    door_lines+=['static const unsigned int kDoor%s=0x%08xU; //%08x'%(name,m.words(address,1)[0],address)
        for name,address in (('Offset',0xb49b48),('RightAngle',0xb49c80),('Radians',0xb493c4))]
    (ROOT/'build/generated/native_door_resource_data.h').write_text('\n'.join(door_lines)+'\n',encoding='ascii')
    report=dict(format='normal-room-fx-resources-v1',bindings=bindings,scope='Reached ordinary first chapter, stage types0/1, shape1; original FX7; all four ray groups loaded, first two retained; default params, Veins and ambient entities21/33/68. No generated player/room state.',source='read-only prepared normal resources and hash-checked saved instruction/data bytes')
    (ROOT/'runtime/port/normal_room_fx_reference.json').write_text(json.dumps(report,indent=2)+'\n')
    return sorted(set('runtime/game/'+b['file'] for b in bindings))+['runtime/port/normal_room_fx_reference.json']

if __name__=='__main__':prepare()

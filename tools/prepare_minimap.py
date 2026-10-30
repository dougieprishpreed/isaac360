"""Prepare the original two minimap sprites and shared icon sprite."""
from pathlib import Path,PurePosixPath
import json,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
from animation_cache import decode,encode
from extract_resources import filename_hash
from sprite_asset_export import emit_asset
from prepare_player_aux_sprites import xml_asset
ROOT=Path(__file__).resolve().parents[1]

def prepare():
    r=OriginalResources(ROOT/'runtime/game');m=Memory(ROOT/'recovered/main');bindings=[]
    def read(name):
        raw,b=r.read(name);bindings.append(dict(request=name,**b));return raw,b
    raw,_=read('animations.b');entries=decode(raw);lines=['//Original8d2100 minimap resources and saved instruction operands.'];names=[]
    for index,address in enumerate((0xb39eec,0xb39ed4,0xb39e64)):
        xml,b=read(m.cstring(address));path=b['virtual_path']
        found=[a for a in entries if int(a['lookup_hash'],16)==filename_hash(path)[0]]
        if found:
            assert len(found)==1;asset=found[0];asset['canonical_path']=path
            assert encode([asset])[4:]==raw[asset['file_offset']:asset['file_offset']+asset['file_bytes']]
        else:asset=xml_asset(xml,path,m)
        for sheet in asset['spritesheets']:
            request=str(PurePosixPath(path).parent/PurePosixPath(sheet['path']['text'].replace('\\','/'))).split('/',1)[1]
            png,b=read(request);assert png[:8]==b'\x89PNG\r\n\x1a\n';w,h=struct.unpack('>II',png[16:24])
            sheet.update(canonical_path=b['virtual_path'],width=w,height=h)
        name='kMinimapAsset%d'%index;emit_asset(lines,asset,name);names.append(name)
    lines+=['static const NativeSpriteAsset* const kMinimapAssets[]={'+','.join('&'+s for s in names)+'};',
        'static const char* const kMinimapRoomIcons[]={']
    for i,address in enumerate(m.words(0x8d1fc4,29),1):
        raw=m.read(address,5)
        icon=m.cstring(struct.unpack('<I',raw[1:5])[0]) if raw[0]==184 else ''
        if i==4:icon=m.cstring(0xb39c04) #Normal mode; descriptor flag2048 selects the alternate.
        if i==11:icon=m.cstring(0xb39cb4) #Subtype!=1; runtime selects the other explicitly.
        lines.append(json.dumps(icon)+', //room%d'%i)
    lines+=['};']
    for key,address in (('Outline',0xb39bc8),('Current',0xb39ba0),('Visited',0xb39b94),('Unvisited',0xb39b84),
                        ('TreasureAlternate',0xb39c18),('ChallengeNormal',0xb39cb4),('ChallengeBoss',0xb39cc4)):
        lines.append('static const char* const kMinimap%s=%s;'%(key,json.dumps(m.cstring(address))))
    (ROOT/'build/generated/native_minimap_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    (ROOT/'runtime/port/native_minimap_reference.json').write_text(json.dumps(dict(format='native-initial-minimap-v1',bindings=bindings,
        scope='Original8d2100 map definitions,8cffb0 first-entry outline/room/icon passes; no fabricated exploration or rewards.'),indent=2)+'\n')

if __name__=='__main__':prepare()

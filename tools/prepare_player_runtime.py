"""Prepare reached normal player/manager constants from existing read-only inputs."""
from pathlib import Path
import json,hashlib,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]

def prepare():
    from prepare_pause_ui import prepare as prepare_pause_ui
    prepare_pause_ui()
    from prepare_music import prepare as prepare_music
    prepare_music()
    from prepare_minimap import prepare as prepare_minimap
    prepare_minimap()
    from prepare_native_persistence import prepare as prepare_persistence
    prepare_persistence()
    from prepare_nightmares import prepare as prepare_nightmares
    prepare_nightmares()
    from prepare_item_pools import prepare as prepare_pools
    prepare_pools()
    m=Memory(ROOT/'recovered/main');r=OriginalResources(ROOT/'runtime/game')
    effects=[i+29 for i,x in enumerate(m.read(0x6e7874,200)) if m.words(0x6e786c+4*x,1)[0]==0x6e7866]+[355,508,732]
    assert m.words(0x9e9bcc,3)==(3,23,25)
    raw,binding=r.read('babies.xml');babies={int(e.get('id')):e for e in ET.fromstring(raw) if e.tag=='baby'}
    #907ee2 iterates exactly the original71-entry prefix. Other XML entries
    #remain preserved and are not added to this native caller's candidate set.
    assert all(i in babies for i in range(71))
    lines=['//Existing normal resource identities and saved native constants.',
        'static const int kNativeEffectCollectibles[]={'+','.join(map(str,effects))+'};',
        'struct NativeManagerBabyDef {int id,achievement;};',
        'static const NativeManagerBabyDef kNativeManagerBabies[]={']
    lines+=['{%d,%d},'%(i,int(babies[i].get('achievement','0'))) for i in range(71)]
    lines+=['};','static const int kNativeManagerCharacterOrder[]={'+','.join(map(str,m.words(0xb3b220,34)))+'};']
    preload,preload_binding=r.read('preload.xml')
    group=next(e for e in ET.fromstring(preload) if e.get('id')==m.cstring(0xb3b174))
    assert len(group)==1 and group[0].tag=='png'
    texture,texture_binding=r.read(group[0].get('path'))
    assert texture[:8]==b'\x89PNG\r\n\x1a\n'
    width,height=struct.unpack('>II',texture[16:24])
    lines+=['static const NativeSpriteSheet kNativePlayerTearsSheet={0,%s,%d,%d};'%
        (json.dumps(texture_binding['virtual_path']),width,height)]
    path=ROOT/'build/generated/native_player_resource_data.h';path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('\n'.join(lines)+'\n',encoding='ascii')
    constants={'TearsCoefficient':0xb49828,'TearsBase':0xb49b4c,'TearsScale':0xb49ab0,
        'Thirty':0xb49ba8,'DamageCoefficient':0xb49808,'DamageBase':0xb49a38,
        'IsaacDamageOffset':0xb2cc78,'IsaacSpeedOffset':0xbc5610,
        'MinSpeed':0x9e9a40,'MaxSpeed':0x9e9a58,'MinShotSpeed':0x9e9a3c,
        'MinRange':0x9e9a4c,'MinDamage':0x9e9a44,'MinFireDelay':0x9e9a48,
        'RangeDecay':0xb49744,'RangeDecayScale':0xb4a03c,'RangeDecayMinimum':0xb493a8}
    #The two reset operands are immediates in the actual reached range block.
    assert m.read(0x71a6c3,6)==bytes.fromhex('c70600008243')
    assert m.read(0x71a6c9,10)==bytes.fromhex('c780b01300000000bec1')
    stat_lines=['//Saved native716840/646730 operands; no expected player state.',
        'static const unsigned int kStatRangeBase=0x43820000U,kStatHeightBase=0xc1be0000U;']
    stat_lines += ['static const unsigned int kStat%s=0x%08xU; //%08x'%(name,m.words(address,1)[0],address) for name,address in constants.items()]
    (ROOT/'build/generated/native_player_stat_constants.h').write_text('\n'.join(stat_lines)+'\n',encoding='ascii')
    report=dict(format='native-normal-player-runtime-v1',baby_source=binding,manager_baby_count=71,
        player_tears_preload=preload_binding,player_tears_texture=texture_binding,
        character_order_address='00b3b220',manager_shuffle_index=27,effect_predicate='006e7820',effect_collectibles=effects,
        source='existing recovered/main saved instruction/data bytes and runtime/game objects; no original execution')
    (ROOT/'runtime/port/native_player_runtime_reference.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':prepare()

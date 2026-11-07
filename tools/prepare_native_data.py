"""Export focused saved RNG constants and prepared base-room metadata locally."""
from pathlib import Path
import hashlib
import json
import struct
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]

def prepare():
    (ROOT/'build/generated').mkdir(parents=True,exist_ok=True)
    from prepare_player_motion import prepare_constants as prepare_motion_constants
    prepare_motion_constants()
    from prepare_room_space import prepare_constants
    prepare_constants()
    from prepare_room_entry import prepare_backdrops
    prepare_backdrops()
    gen=ROOT/'build/generated'; gen.mkdir(parents=True,exist_ok=True)
    out=ROOT/'runtime/port'; out.mkdir(parents=True,exist_ok=True)
    memory=ROOT/'recovered/main/memory/009e8000.bin'
    blob=memory.read_bytes()
    assert hashlib.sha256(blob).hexdigest()=='eb8e16627faf6b328351c147146c5a32eeabf93b3327c501527daf38abd313ce'
    shifts=[struct.unpack_from('<III',blob,0x009e9a88-0x009e8000+i*12) for i in range(81)]
    assert all(0<a<32 and 0<b<32 and 0<c<32 for a,b,c in shifts)
    alphabet=blob[0x00b3c840-0x009e8000:0x00b3c860-0x009e8000].decode('ascii')
    scale=struct.unpack_from('<I',blob,0x00b49330-0x009e8000)[0]
    assert len(set(alphabet))==32 and scale==0x2f7ffffe
    lines=['// From hash-verified saved reference ranges; see runtime/port/native_data.json.',
           'static const unsigned int kNativeShifts[81][3] = {']
    lines+=['{%d,%d,%d},'%t for t in shifts]
    lines+=['};','static const char kNativeSeedAlphabet[]="'+alphabet+'";',
            'static const unsigned int kNativeFloatScaleBits=0x%08xU;'%scale]
    (gen/'native_rng_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    report=dict(format='native-rng-data-v2',rng_reference=dict(path=memory.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(blob).hexdigest(),shift_table='009e9a88..009e9e53',seed_alphabet='00b3c840..00b3c85f',float_scale='00b49330',float_scale_bits='2f7ffffe'))
    (out/'native_data.json').write_text(json.dumps(report,indent=2)+'\n')
    from prepare_normal_catalog import prepare as prepare_normal_catalog
    prepare_normal_catalog()
    prepare_decorations()
    print('Prepared81 verified RNG triples and normal inherited room data.')

def prepare_decorations():
    """Copy the backdrop->props animation-count ownership used by room decorations.

    The original backdrop definition owns one props ANM2. Only its leading
    PropNN animations participate in the room-decoration animation roll; the
    puddle animations that follow in some files are selected by water state,
    not by this count. Keeping the mapping per backdrop also preserves the
    shorter DLC sets instead of reusing the basement's 43 entries.
    """
    from original_resources import OriginalResources
    gen=ROOT/'build/generated'
    resources=OriginalResources(ROOT/'runtime/game')
    backdrops,_=resources.read('backdrops.xml')
    root=ET.fromstring(backdrops)
    entries=[];counts={}
    for backdrop in root:
        prop=backdrop.get('props')
        if not prop:
            continue
        data,_=resources.read('gfx/grid/'+prop)
        names=[animation.get('Name','') for animation in ET.fromstring(data).find('Animations')]
        props=[name for name in names if name.startswith('Prop') and name[4:].isdigit()]
        assert props and names[:len(props)]==props,prop
        count=len(props)
        key=prop.lower()
        assert key not in counts or counts[key]==count,prop
        counts[key]=count
        entries.append((int(backdrop.get('id')),count))
    assert entries
    lines=['//Backdrop props animation sets from inherited backdrops.xml and ANM2s.',
           'struct NativeDecorationSet { unsigned int backdrop,animations; };',
           'static const NativeDecorationSet kNativeDecorationSets[] = {']
    lines+=['{%d,%d},'%(backdrop,count) for backdrop,count in entries]
    lines+=['};']
    (gen/'native_decoration_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
if __name__=='__main__':prepare()

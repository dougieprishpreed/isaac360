"""Bind reached native room music to the original resolved resources."""
from pathlib import Path
import json,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]

def prepare():
    r=OriginalResources(ROOT/'runtime/game');m=Memory(ROOT/'recovered/main');bindings=[]
    def read(name):
        raw,b=r.read(name);bindings.append(dict(request=name,**b));return raw
    music=ET.fromstring(read('music.xml'));stages=ET.fromstring(read('stages.xml'))
    ids={1,4,14,64,95};defs=[]
    for e in music:
        i=int(e.get('id'))
        if i not in ids:continue
        paths=[]
        for key in ('intro','path','layerintro','layer'):
            path=music.get('root')+e.get(key) if e.get(key) else ''
            if path:
                raw=read(path);assert raw[:4]==b'OggS'
                path=bindings[-1]['virtual_path']
            paths.append(json.dumps(path))
        assert not list(e)
        defs.append('{%d,%s,%s},'%(i,','.join(paths),'true' if e.get('loop')=='true' else 'false'))
    assert len(defs)==len(ids)
    lines=['//Original resolved stages/music XML and reached stream identities.',
        'static const NativeMusicDef kMusicDefs[]={']+defs+['};','static const int kMusicStages[][2]={']
    lines+=['{%d,%d},'%(int(e.get('id')),int(e.get('music'))) for e in stages if int(e.get('id')) in (1,2,3)]
    lines+=['};']
    for key,address in (('DefaultVolume',0xb4954c),('DefaultFade',0xb49448),('Item232Intensity',0xb49744)):
        lines.append('static const unsigned int kMusic%s=0x%08xU; //%08x'%(key,m.words(address,1)[0],address))
    (ROOT/'build/generated/native_music_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    report=dict(format='native-initial-room-music-v1',bindings=bindings,
        scope='Original character-page jingle submissions and normal stage1 room music queue; no invented audio completion or playback result.',
        source='Hash-checked saved instructions7990b0/791210/89975b/86e1fe and resolved inherited XML/Ogg resources')
    (ROOT/'runtime/port/native_music_reference.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':prepare()

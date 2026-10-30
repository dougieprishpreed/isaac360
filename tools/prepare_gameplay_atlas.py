"""Pack the original room/player/HUD pixels for the target gameplay viewport.

The Xbox 360 D3DX image path produced tiled-memory sampling artifacts, so the
port ships a prepared linear RGBA atlas exactly like the frontend one. Pixels
are copied from the inherited original PNGs without replacement artwork.
"""
from pathlib import Path
import hashlib,json,struct
from PIL import Image
from original_resources import OriginalResources

ROOT=Path(__file__).resolve().parents[1]
WIDTH=HEIGHT=2048

IMAGES=[
    ('backdrop1','gfx/backdrop/01_basement.png',(0,0)),
    ('backdrop2','gfx/backdrop/02_cellar.png',(0,468)),
    ('player','gfx/characters/costumes/character_001_isaac.png',(0,1024)),
    ('hearts','gfx/ui/ui_hearts.png',(1544,512)),
    ('tears','gfx/tears.png',(1544,600)),
]

def prepare():
    resources=OriginalResources(ROOT/'runtime/game')
    atlas=Image.new('RGBA',(WIDTH,HEIGHT),(0,0,0,0))
    manifest={'format':'gameplay-atlas-v1','width':WIDTH,'height':HEIGHT,'entries':[]}
    for name,request,at in IMAGES:
        raw,entry=resources.read(request)
        image=Image.open(__import__('io').BytesIO(raw)).convert('RGBA')
        atlas.alpha_composite(image,at)
        manifest['entries'].append(dict(name=name,request=request,package=entry['package'],
            sha256=hashlib.sha256(raw).hexdigest(),x=at[0],y=at[1],width=image.width,height=image.height))
    output=ROOT/'runtime/port/gameplay'
    output.mkdir(parents=True,exist_ok=True)
    with (output/'atlas.bin').open('wb') as f:
        f.write(b'GPLAY1\0\0');f.write(struct.pack('<II',WIDTH,HEIGHT))
        f.write(atlas.tobytes())
    (output/'manifest.json').write_text(json.dumps(manifest,indent=1)+'\n')
    positions={e['name']:(e['x'],e['y']) for e in manifest['entries']}
    lines=['//Prepared original gameplay atlas rectangles (linear RGBA atlas).',
           'static const unsigned int kGameplayAtlasWidth=%dU;'%WIDTH,
           'static const unsigned int kGameplayAtlasHeight=%dU;'%HEIGHT]
    for name,_request,(x,y) in IMAGES:
        e=next(e for e in manifest['entries'] if e['name']==name)
        lines.append('static const int kGameplay%sX=%d,kGameplay%sY=%d,kGameplay%sW=%d,kGameplay%sH=%d;'%(
            name.title(),x,name.title(),y,name.title(),e['width'],name.title(),e['height']))
    (ROOT/'build/generated/gameplay_atlas_data.h').write_text('\n'.join(lines)+'\n')
    print('Prepared gameplay atlas: %d entries, %dx%d'%(len(manifest['entries']),WIDTH,HEIGHT))

if __name__=='__main__':prepare()

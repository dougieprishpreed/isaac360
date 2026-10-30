"""Pack the original room/player/enemy/HUD pixels for the target viewport.

The Xbox 360 D3DX image path produced tiled-memory sampling artifacts, so the
port ships a prepared linear RGBA atlas exactly like the frontend one. Pixels
are copied from the inherited original PNGs without replacement artwork.
Enemy entries are first-frame compositions of their original ANM2 layers.
"""
from pathlib import Path
import hashlib,io,json,struct
import xml.etree.ElementTree as ET
from PIL import Image
from original_resources import OriginalResources

ROOT=Path(__file__).resolve().parents[1]
WIDTH=HEIGHT=2048
ENEMY_SIZE=64
ENEMY_ANCHOR=(32,48)

IMAGES=[
    ('backdrop1','gfx/backdrop/01_basement.png',(0,0)),
    ('backdrop2','gfx/backdrop/02_cellar.png',(0,468)),
    ('player','gfx/characters/costumes/character_001_isaac.png',(0,1024)),
    ('hearts','gfx/ui/ui_hearts.png',(1544,512)),
    ('tears','gfx/tears.png',(1544,600)),
    ('bodies01','gfx/monsters/classic/monster_000_bodies01.png',(520,900)),
    ('frowninggaper','gfx/monsters/classic/monster_024_frowninggaper.png',(1040,900)),
    ('flysheet','gfx/monsters/classic/monster_010_fly.png',(520,1160)),
    ('pootersheet','gfx/monsters/classic/monster_001_pooter.png',(780,1160)),
    ('rocksbasement','gfx/grid/rocks_basement.png',(920,1160)),
    ('doornormal','gfx/grid/door_01_normaldoor.png',(1060,1160)),
    ('fattysheet','gfx/monsters/rebirth/monster_207_fatty.png',(520,1420)),
    ('poopsheet','gfx/grid/grid_poop_1.png',(1200,1160)),
]
ACTORS=[
    ('gaper','gfx/010.000_Frowning Gaper.anm2','WalkHori','Head',(1544,800)),
    ('fly','gfx/013.000_Fly.anm2','Fly',None,(1616,800)),
    ('pooter','gfx/014.000_Pooter.anm2','Fly','Attack',(1688,800)),
    ('fatty','gfx/207.000_Fatty.anm2','WalkHori','AttackVert',(1760,800)),
    ('skinny','gfx/226.000_Skinny.anm2','WalkHori','Head',(1544,864)),
    ('rotty','gfx/226.001_Rotty.anm2','WalkHori','Head',(1616,864)),
]
_sheet_cache={}
def load_image(resources,request):
    raw,entry=resources.read(request)
    return Image.open(io.BytesIO(raw)).convert('RGBA'),raw,entry
def tint(image,values):
    if values==(255,255,255,255):return image
    r,g,b,a=image.split();rr,gg,bb,aa=values
    r=r.point(lambda v,rr=rr:v*rr//255);g=g.point(lambda v,gg=gg:v*gg//255)
    b=b.point(lambda v,bb=bb:v*bb//255);a=a.point(lambda v,aa=aa:v*aa//255)
    return Image.merge('RGBA',(r,g,b,a))
def compose_actor(resources,request,primary,overlay):
    raw,entry=resources.read(request);root=ET.fromstring(raw)
    sheets={s.get('Id'):s.get('Path') for s in root.find('Content/Spritesheets')}
    animations={a.get('Name'):a for a in root.find('Animations')}
    canvas=Image.new('RGBA',(ENEMY_SIZE,ENEMY_SIZE),(0,0,0,0))
    used=[]
    for layer in root.find('Content/Layers'):
        frame=None
        for name in (primary,overlay):
            if not name or name not in animations:continue
            for track in animations[name].find('LayerAnimations'):
                if track.get('LayerId')==layer.get('Id') and list(track):
                    frame=list(track)[0];used.append('%s/%s'%(name,layer.get('Name')));break
            if frame is not None:break
        if frame is None:continue
        sheet=sheets.get(layer.get('SpritesheetId'))
        if not sheet:raise ValueError('Missing sheet for '+request)
        request_path='gfx/'+sheet.replace('\\','/')
        key=request_path.lower()
        if key not in _sheet_cache:_sheet_cache[key]=load_image(resources,request_path)[0]
        image=_sheet_cache[key]
        crop=(int(frame.get('XCrop',0)),int(frame.get('YCrop',0)),
              int(frame.get('Width',0)),int(frame.get('Height',0)))
        piece=image.crop((crop[0],crop[1],crop[0]+crop[2],crop[1]+crop[3]))
        tint_values=tuple(int(frame.get(k,'255')) for k in ('RedTint','GreenTint','BlueTint','AlphaTint'))
        piece=tint(piece,tint_values)
        scale_x=int(frame.get('XScale','100'))/100.0;scale_y=int(frame.get('YScale','100'))/100.0
        if abs(scale_x-1.0)>1e-6 or abs(scale_y-1.0)>1e-6:
            piece=piece.resize((max(1,int(piece.width*scale_x)),max(1,int(piece.height*scale_y))),Image.BILINEAR)
        dest=(ENEMY_ANCHOR[0]+int(frame.get('XPosition','0'))-int(frame.get('XPivot','0')),
              ENEMY_ANCHOR[1]+int(frame.get('YPosition','0'))-int(frame.get('YPivot','0')))
        canvas.alpha_composite(piece,dest)
    return canvas,raw,entry,used

def prepare():
    resources=OriginalResources(ROOT/'runtime/game')
    atlas=Image.new('RGBA',(WIDTH,HEIGHT),(0,0,0,0))
    manifest={'format':'gameplay-atlas-v2','width':WIDTH,'height':HEIGHT,'entries':[]}
    for name,request,at in IMAGES:
        image,raw,entry=load_image(resources,request)
        atlas.alpha_composite(image,at)
        manifest['entries'].append(dict(name=name,request=request,package=entry['package'],
            sha256=hashlib.sha256(raw).hexdigest(),x=at[0],y=at[1],width=image.width,height=image.height))
    for name,request,primary,overlay,at in ACTORS:
        image,raw,entry,used=compose_actor(resources,request,primary,overlay)
        atlas.alpha_composite(image,at)
        manifest['entries'].append(dict(name=name,request=request,package=entry['package'],
            sha256=hashlib.sha256(raw).hexdigest(),x=at[0],y=at[1],width=image.width,height=image.height,
            composed='%s%s'%(primary,'/'+overlay if overlay else ''),tracks=used))
    output=ROOT/'runtime/port/gameplay'
    output.mkdir(parents=True,exist_ok=True)
    with (output/'atlas.bin').open('wb') as f:
        f.write(b'GPLAY1\0\0');f.write(struct.pack('<II',WIDTH,HEIGHT))
        f.write(atlas.tobytes())
    (output/'manifest.json').write_text(json.dumps(manifest,indent=1)+'\n')
    lines=['//Prepared original gameplay atlas rectangles (linear RGBA atlas).',
           'static const unsigned int kGameplayAtlasWidth=%dU;'%WIDTH,
           'static const unsigned int kGameplayAtlasHeight=%dU;'%HEIGHT]
    for entry in manifest['entries']:
        name=entry['name'].title()
        lines.append('static const int kGameplay%sX=%d,kGameplay%sY=%d,kGameplay%sW=%d,kGameplay%sH=%d;'%(
            name,entry['x'],name,entry['y'],name,entry['width'],name,entry['height']))
    (ROOT/'build/generated/gameplay_atlas_data.h').write_text('\n'.join(lines)+'\n')
    print('Prepared gameplay atlas: %d entries, %dx%d'%(len(manifest['entries']),WIDTH,HEIGHT))

if __name__=='__main__':prepare()

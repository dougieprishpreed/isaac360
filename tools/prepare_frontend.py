"""Prepare only original title/files/main/character artwork; no authored game data."""
from pathlib import Path,PurePosixPath
import hashlib,io,json,math,struct,xml.etree.ElementTree as ET
from PIL import Image
from original_resources import OriginalResources
from compiled_presentation import select_compiled
from prepare_player_setup import prepare as prepare_player_setup
ROOT=Path(__file__).resolve().parents[1];GAME=ROOT/'runtime/game';OUT=ROOT/'runtime/port/frontend';GEN=ROOT/'build/generated'
def main():
    prepare_player_setup();OUT.mkdir(parents=True,exist_ok=True);GEN.mkdir(parents=True,exist_ok=True)
    original=OriginalResources(GAME);original_sources={};original_bindings=[];original_sheets={}
    def original_resource(relative):
        raw,e=original.read(relative);original_sources[(e['package'],e['virtual_path'])]=e
        binding=dict(request=relative,package=e['package'],virtual_path=e['virtual_path'],sha256=e['sha256'])
        if binding not in original_bindings:original_bindings.append(binding)
        return raw
    def resource(name):
        return original_resource(original_sheets.get(name,name.removeprefix('resources/')))
    atlas = Image.new('RGBA', (2048, 4096))
    sheets = {}
    free_rects = [(2,2,2046,4094)]

    def sheet(name):
        name = name.replace('\\', '/').lower()
        if name in sheets:
            return sheets[name]
        im = Image.open(io.BytesIO(resource(name))).convert('RGBA')
        w, h = im.size
        # Maximal free rectangles avoid wasting a full shelf on small item icons.
        # The two original character pages extend the atlas budget to32 MiB.
        pw,ph=w+2,h+2
        fits=[r for r in free_rects if pw<=r[2] and ph<=r[3]]
        if not fits:
            raise ValueError('Atlas budget exceeded')
        x,y,_,_=min(fits,key=lambda r:(min(r[2]-pw,r[3]-ph),max(r[2]-pw,r[3]-ph),r[1],r[0]))
        remaining=[]
        for fx,fy,fw,fh in free_rects:
            if x>=fx+fw or x+pw<=fx or y>=fy+fh or y+ph<=fy:
                remaining.append((fx,fy,fw,fh)); continue
            if x>fx: remaining.append((fx,fy,x-fx,fh))
            if x+pw<fx+fw: remaining.append((x+pw,fy,fx+fw-x-pw,fh))
            if y>fy: remaining.append((fx,fy,fw,y-fy))
            if y+ph<fy+fh: remaining.append((fx,y+ph,fw,fy+fh-y-ph))
        free_rects[:]=[r for i,r in enumerate(remaining) if not any(i!=j and
            q[0]<=r[0] and q[1]<=r[1] and q[0]+q[2]>=r[0]+r[2] and q[1]+q[3]>=r[1]+r[3] and
            (q!=r or j<i) for j,q in enumerate(remaining))]
        atlas.paste(im, (x, y))
        sheets[name] = (x, y, w, h)
        return sheets[name]

    sprites = []
    frame_ranges = []
    animations = []

    def frame_at(track, tick):
        frames = list(track)
        if not frames:
            return None
        for f in frames:
            delay = int(f.get('Delay', '1'))
            if tick < delay:
                return f
            tick -= delay
        return frames[-1]

    def values_at(track, tick):
        frames = list(track)
        for i, f in enumerate(frames):
            delay = int(f.get('Delay', '1'))
            if tick < delay or i == len(frames) - 1:
                v = dict(f.attrib)
                if f.get('Interpolated') == 'true' and i + 1 < len(frames):
                    following = frames[i + 1]
                    for key in ('XPosition', 'YPosition', 'XScale', 'YScale', 'Rotation',
                                'RedTint', 'GreenTint', 'BlueTint', 'FrontendTint',
                                'RedOffset', 'GreenOffset', 'BlueOffset', 'FrontendOffset'):
                        if key in v and key in following.attrib:
                            v[key] = str(float(v[key]) + (float(following.get(key)) - float(v[key])) * tick / delay)
                return v
            tick -= delay
        return None

    def animation(tag, path, name, layers=None, item_layer=None,tree=None,layer_frames=None):
        if tree is None:tree = ET.fromstring(resource(path))
        a = tree.find(f'Animations/Animation[@Name="{name}"]')
        if a is None:
            raise ValueError(f'Missing animation {path}: {name}')
        sheet_paths = {}
        for s in tree.findall('Content/Spritesheets/Spritesheet'):
            # Animation sheet paths are relative to the animation directory.
            p = str(PurePosixPath(path).parent / s.get('Path').replace('\\', '/')).lower()
            sheet_paths[int(s.get('Id'))] = p
        layer_sheets = {int(l.get('Id')): sheet_paths[int(l.get('SpritesheetId'))]
                        for l in tree.findall('Content/Layers/Layer')}
        first = len(frame_ranges)
        # Native UI can select an individual layer's frame independently of the
        # actor's one-frame Idle. Keep those tracks literal, not time-expanded.
        count = layer_frames if layer_frames is not None else int(a.get('FrameNum'))
        for tick in range(count):
            start = len(sprites)
            root = values_at(a.find('RootAnimation'), tick)
            for track in a.findall('LayerAnimations/LayerAnimation'):
                if layers is not None and int(track.get('LayerId')) not in layers:
                    continue
                f = values_at(track, tick)
                if f is None or track.get('Visible') == 'false' or f.get('Visible') == 'false':
                    continue
                if float(f.get('FrontendOffset',0)) or float(root.get('FrontendOffset',0)):
                    raise ValueError('Unsupported alpha offset in selected animation')
                sx, sy = (float(f.get(k, 100)) / 100 for k in ('XScale', 'YScale'))
                rx, ry = (float(root.get(k, 100)) / 100 for k in ('XScale', 'YScale'))
                x, y, _, _ = sheet(layer_sheets[int(track.get('LayerId'))])
                w, h = int(f.get('Width')), int(f.get('Height'))
                angle = math.radians(float(f.get('Rotation', 0)))
                root_angle = math.radians(float(root.get('Rotation', 0)))
                corners = []
                for u, v in ((0, 0), (w, 0), (w, h), (0, h)):
                    dx, dy = (u - float(f.get('XPivot', 0))) * sx, (v - float(f.get('YPivot', 0))) * sy
                    dx, dy = dx * math.cos(angle) - dy * math.sin(angle), dx * math.sin(angle) + dy * math.cos(angle)
                    dx, dy = (dx + float(f.get('XPosition', 0))) * rx, (dy + float(f.get('YPosition', 0))) * ry
                    corners += [float(root.get('XPosition', 0)) + dx * math.cos(root_angle) - dy * math.sin(root_angle),
                                float(root.get('YPosition', 0)) + dx * math.sin(root_angle) + dy * math.cos(root_angle)]
                color = [round(float(f.get(c, 255)) * float(root.get(c, 255)) / 255) for c in ('FrontendTint', 'RedTint', 'GreenTint', 'BlueTint')]
                argb = (color[0] << 24) | (color[1] << 16) | (color[2] << 8) | color[3]
                offsets = [(float(f.get(c, 0)) * float(root.get(t, 255)) / 255 + float(root.get(c, 0))) / 255
                           for c, t in (('RedOffset','RedTint'),('GreenOffset','GreenTint'),('BlueOffset','BlueTint'))]
                sprites.append((x + int(f.get('XCrop')), y + int(f.get('YCrop')), w, h, *corners, argb, *offsets,
                                int(int(track.get('LayerId')) == item_layer)))
            frame_ranges.append((start, len(sprites) - start))
        animations.append((tag, first, count, a.get('Loop') == 'true'))

    slot_path='resources/gfx/ui/main menu/saveselectmenu.anm2'
    slot_tree,slot_proof=select_compiled(original_resource('animations.b'),slot_path)
    for s in slot_tree.findall('Content/Spritesheets/Spritesheet'):
        name=str(PurePosixPath(slot_path).parent/s.get('Path').replace('\\','/')).lower()
        original_sheets[name]=name[len('resources/'):]
    animation('SlotsBase',slot_path,'Idle',layers={0,4,9},tree=slot_tree)
    for choice,label in enumerate(('File1','File2','File3','Delete','Import')):
        layer=(1,2,3,5,10)[choice]
        animation('Slots'+label,slot_path,label+'Select',layers={layer},tree=slot_tree)
        animation('Slots'+label+'Off',slot_path,label+'Deselect',layers={layer},tree=slot_tree)
    for tag,name in (('SlotsConfirm','DeleteConfirmationIdle'),('SlotsConfirmIn','DeleteConfirmationAppear'),('SlotsConfirmOut','DeleteConfirmationDissapear')):
        animation(tag,slot_path,name,tree=slot_tree)
    slot_anchors=[]
    for file_id in range(3):
        states=[]
        for state in ('Select','Deselect'):
            a=slot_tree.find('Animations/Animation[@Name="File%d%s"]'%(file_id+1,state))
            track=a.find('NullAnimations/NullAnimation[@NullId="%d"]'%file_id)
            frames=[]
            for tick in range(5):
                f=values_at(track,tick)
                if float(f.get('Rotation')) or any(float(f.get(k)) for k in ('RedOffset','GreenOffset','BlueOffset')):raise ValueError('Unsupported slot anchor transform')
                tint=[round(float(f.get(k))) for k in ('AlphaTint','RedTint','GreenTint','BlueTint')]
                argb=sum(value<<(24-8*i) for i,value in enumerate(tint))
                frames.append((float(f['XPosition']),float(f['YPosition']),float(f['XScale'])/100,float(f['YScale'])/100,argb))
            states.append(frames)
        slot_anchors.append(states)
    drawing_path='resources/gfx/ui/main menu/saveselectmenu_drawing.anm2'
    drawing_tree,drawing_proof=select_compiled(original_resource('animations.b'),drawing_path)
    for s in drawing_tree.findall('Content/Spritesheets/Spritesheet'):
        name=str(PurePosixPath(drawing_path).parent/s.get('Path').replace('\\','/')).lower()
        original_sheets[name]=name[len('resources/'):]
    animation('SlotsDrawing',drawing_path,'Idle',layers={0},tree=drawing_tree)
    animation('SlotsDrawingAnimated',drawing_path,'Animated',layers={0},tree=drawing_tree)
    def frontend_xml(relative):
        path='resources/'+relative
        raw=original_resource(relative);source=original.resolve(relative)
        tree=ET.fromstring(raw)
        for s in tree.findall('Content/Spritesheets/Spritesheet'):
            name=str(PurePosixPath(path).parent/s.get('Path').replace('\\','/')).lower()
            original_sheets[name]=name[len('resources/'):]
        return path,tree,dict(kind='xml',package=source['package'],virtual_path=source['virtual_path'],sha256=source['sha256'])
    title_path,title_tree,title_provenance=frontend_xml('gfx/ui/main menu/titlemenu.anm2')
    animation('Title',title_path,'Idle',tree=title_tree)
    presentation=dict(profile='normal-001',animation_package=title_provenance['package'],graphics_package=original.resolve('gfx/ui/main menu/titlemenu.png')['package'],
                      virtual_path=title_provenance['virtual_path'],source=title_provenance,
                      scope='Corrected direct-launch observation and addressed normal resource roots. No alternate title selector. Room entry and gameplay remain unfinished.')
    main_path,main_tree,main_provenance=frontend_xml('gfx/ui/main menu/gamemenu.anm2')
    animation('MainBase',main_path,'Idle',layers={0,10},tree=main_tree)
    for tag,layer in (('MainNew',1),('MainChallenges',3),('MainStats',4),('MainOptions',5),('MainMods',12)):
        animation(tag,main_path,'Idle',layers={layer},tree=main_tree)
    animation('MainContinue',main_path,'Idle',layers={2},tree=main_tree,layer_frames=3)
    animation('MainCursor',main_path,'Idle',layers={6},tree=main_tree,layer_frames=6)
    # 009ee670: genuine character pages. Helpers in players.xml are not entries.
    choices=json.loads((ROOT/'runtime/port/player_setup_sources.json').read_text())['choices']
    character_art=[]
    for page,suffix in enumerate(('', 'alt')):
        path,tree,proof=frontend_xml('gfx/ui/main menu/charactermenu'+suffix+'.anm2')
        portrait_path,portrait_tree,portrait_proof=frontend_xml('gfx/ui/main menu/characterportraits'+suffix+'.anm2')
        character_art.append(dict(menu=proof,portraits=portrait_proof))
        for c in choices[page*18:(page+1)*18]:
            tag='Character%d_%d_'%(page,c['choice'])
            for part,layers in (('Base',{0,1,2,3,19}),('Stats',{4,5,6,9,10,11}),('Locked',{16}),('Item0',{7,8}),('Item1',{12,13}),('Item2',{14,15})):
                animation(tag+part,path,c['animation'],layers=layers,tree=tree)
            animation(tag+'Portrait',portrait_path,c['animation'],tree=portrait_tree,layer_frames=2)
    path,tree,proof=frontend_xml('gfx/ui/main menu/charactermenubg.anm2')
    for name in ('Idle','Face0','Face1'):animation('CharacterBG'+name,path,name,tree=tree)
    path,tree,proof=frontend_xml('gfx/ui/main menu/charactermenubgextra.anm2')
    animation('CharacterBGExtra',path,'Idle',tree=tree)
    path,tree,proof=frontend_xml('gfx/ui/main menu/difficultywidget.anm2')
    for tag,name,count in (('Difficulty','Unlocked',3),('DifficultyAll','AB+',4)):
        animation(tag,path,name,tree=tree,layer_frames=count)
    path,tree,proof=frontend_xml('gfx/ui/main menu/seedwidget.anm2')
    animation('CharacterSeed',path,'Idle',layers={0,1},tree=tree)
    path,tree,proof=frontend_xml('gfx/ui/buttons.anm2')
    animation('ControllerButtons',path,'XboxOne',tree=tree,layer_frames=26)
    used_height=max(y+h+2 for x,y,w,h in sheets.values());atlas=atlas.crop((0,0,atlas.width,1<<(used_height-1).bit_length()))
    (OUT/'atlas.bin').write_bytes(b'GATLAS1\0'+struct.pack('<II',*atlas.size)+atlas.tobytes())
    lines=['//Generated from inherited original frontend artwork.','#ifndef GENERIC_FRONTEND_ASSETS_H','#define GENERIC_FRONTEND_ASSETS_H',
        'static const char* const kTitlePresentationProfile="normal-001";',
        'struct OriginalBinding {const char* request;int package;};',
        'static const OriginalBinding kOriginalBindings[]={'+','.join('{"%s",%d}'%(e['request'],int(e['package'].split('_')[1])) for e in original_bindings)+'};',
        'static const int kOriginalBindingCount=%d;'%len(original_bindings),
        'struct SlotAnchor {float x,y,sx,sy;unsigned int tint;};',
        'static const SlotAnchor kSlotAnchors[3][2][5]={'+','.join('{'+','.join('{'+','.join('{%.5ff,%.5ff,%.5ff,%.5ff,0x%08x}'%f for f in frames)+'}' for frames in states)+'}' for states in slot_anchors)+'};',
        'struct FrontendSprite {int x,y,w,h;float corners[8];unsigned int color;float add[3];int itemLayer;};',
        'static const int kAtlasWidth=%d,kAtlasHeight=%d;'%atlas.size,
        'struct FrontendFrame {int first,count;};','struct FrontendAnimation {int first,count,loop;};',
        'enum FrontendAnim {'+', '.join('AN_'+a[0] for a in animations)+'};',
        'static const int kCharacterArt[2][18][7]={'+','.join('{'+','.join('{'+','.join('AN_Character%d_%d_%s'%(page,c,part) for part in ('Base','Stats','Locked','Item0','Item1','Item2','Portrait'))+'}' for c in range(18))+'}' for page in range(2))+'};',
        'static const FrontendSprite kSprites[]={']
    for sprite in sprites:lines.append(('{%d,%d,%d,%d,{'+','.join(['%.5ff']*8)+'},0x%08x,{'+','.join(['%.5ff']*3)+'},%d},')%sprite)
    lines+=['};','static const FrontendFrame kFrames[]={']+['{%d,%d},'%f for f in frame_ranges]+['};','static const FrontendAnimation kAnimations[]={']+['{%d,%d,%d},'%(a[1],a[2],a[3]) for a in animations]+['};','#endif']
    (GEN/'frontend_assets.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    manifest=dict(format='original-frontend-assets-v1',presentation=presentation,bindings=original_bindings,sources=list(original_sources.values()),sheets=sheets,atlas_size=list(atlas.size),atlas_sha256=hashlib.sha256((OUT/'atlas.bin').read_bytes()).hexdigest(),header_sha256=hashlib.sha256((GEN/'frontend_assets.h').read_bytes()).hexdigest(),scope='Original inherited title/files/main/characters. Native transitions/profile drawings/options/seed/challenges/stats and full gameplay still require implementation.')
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Prepared original frontend:',len(animations),'animations;',atlas.width*atlas.height*4,'atlas bytes.')
if __name__=='__main__':main()

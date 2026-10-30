"""Original offline pause/options resources reached by901240; no generated state."""
from pathlib import Path,PurePosixPath
import json,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
from animation_cache import decode,encode
from extract_resources import filename_hash
from prepare_player_aux_sprites import xml_asset
from sprite_asset_export import emit_asset
ROOT=Path(__file__).resolve().parents[1]
def prepare():
    r=OriginalResources(ROOT/'runtime/game');m=Memory(ROOT/'recovered/main');bindings=[]
    def read(request):
        raw,b=r.read(request);bindings.append(dict(request=request,**b));return raw,b
    packed,_=read('animations.b');cache=decode(packed);lines=['//Original pause/options assets and font records.'];names=[]
    requests=('gfx/ui/PauseScreen.anm2','gfx/ui/main menu/OptionsMenu.anm2','gfx/ui/main menu/GammaMenu.anm2','gfx/ui/Prompt_YesNo_Small.anm2')
    for i,request in enumerate(requests):
        xml,b=read(request);path=b['virtual_path'];found=[a for a in cache if int(a['lookup_hash'],16)==filename_hash(path)[0]]
        if found:
            assert len(found)==1;asset=found[0];asset['canonical_path']=path
            assert encode([asset])[4:]==packed[asset['file_offset']:asset['file_offset']+asset['file_bytes']]
        else:asset=xml_asset(xml,path,m)
        for sheet in asset['spritesheets']:
            request=str(PurePosixPath(path).parent/PurePosixPath(sheet['path']['text'].replace('\\','/'))).split('/',1)[1]
            png,b=read(request);assert png[:8]==b'\x89PNG\r\n\x1a\n';w,h=struct.unpack('>II',png[16:24]);sheet.update(canonical_path=b['virtual_path'],width=w,height=h)
        name='kPauseAsset%d'%i;emit_asset(lines,asset,name);names.append(name)
    lines+=['static const NativeSpriteAsset* const kPauseAssets[]={'+','.join('&'+n for n in names)+'};']
    dark,b=read('gfx/ui/main menu/OptionsMenuDark.png');w,h=struct.unpack('>II',dark[16:24])
    lines+=['static const NativeSpriteSheet kPauseDark={0,%s,%d,%d};'%(json.dumps(b['virtual_path']),w,h)]
    fonts=[]
    for i,request in enumerate(('font/TeamMeatFont16Bold.fnt','font/TeamMeatFont12.fnt','font/MPlus_12b.fnt','font/KR_MeatFont14.fnt')):
        try:raw,b=read(request)
        except ValueError as e:
            assert str(e).startswith('Original resource missing: ');fonts.append('{%s,0,0,0,0,0,0,0,0,0},'%json.dumps(request));continue
        assert raw[:4]==b'BMF\3';blocks={};at=4
        while at<len(raw):
            kind=raw[at];size=struct.unpack_from('<I',raw,at+1)[0];blocks[kind]=raw[at+5:at+5+size];at+=5+size
        assert at==len(raw) and not blocks.get(5,b''),'Nonempty kerning needs its native table export.'
        common=struct.unpack_from('<5H',blocks[2]);lineHeight,base,tw,th,pageCount=common
        pages=blocks[3].rstrip(b'\0').split(b'\0');assert len(pages)==pageCount
        pageNames=[]
        for n,p in enumerate(pages):
            png,pb=read('font/'+p.decode('ascii'));assert struct.unpack('>II',png[16:24])==(tw,th)
            pageNames.append('{%d,%s,%d,%d},'%(n,json.dumps(pb['virtual_path']),tw,th))
        lines+=['static const NativeSpriteSheet kPauseFontPages%d[]={'%i]+pageNames+['};']
        chars=list(struct.iter_unpack('<I4H3h2B',blocks[4]));assert len(blocks[4])==20*len(chars)
        lines+=['static const NativeFontGlyph kPauseFontGlyphs%d[]={'%i]+['{'+','.join(map(str,c))+'},' for c in chars]+['};']
        fonts.append('{%s,%s,%d,%d,%d,%d,kPauseFontPages%d,%d,kPauseFontGlyphs%d,%d},'%(json.dumps(request),json.dumps(b['virtual_path']),lineHeight,base,tw,th,i,pageCount,i,len(chars)))
    lines+=['static const NativeFontAsset kPauseFonts[]={']+fonts+['};']
    (ROOT/'build/generated/native_pause_ui_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    (ROOT/'runtime/port/native_pause_ui_reference.json').write_text(json.dumps(dict(format='native-pause-ui-v1',bindings=bindings,
        scope='901240/9002b0/7deeb0 CPU resource ownership, English/offline/no mods.9527a0 missing optional-font return retained. No authored screen or GPU result.'),indent=2)+'\n')
if __name__=='__main__':prepare()

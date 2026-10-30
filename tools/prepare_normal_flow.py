"""Focused normal-flow expectations from pinned raw inputs and saved algorithms.

Does not import the C++ header exporter or execute the original program. These
are data/arithmetic references, not an independently recorded original run.
"""
from pathlib import Path
import hashlib,json,struct,shutil,xml.etree.ElementTree as ET
from original_resources import OriginalResources
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/normal_flow/data'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def shift(n,a,b,c):
    n^=n>>a;n=(n^(n<<b))&0xffffffff;return n^(n>>c)
def main():
    OUT.mkdir(parents=True,exist_ok=True);refs={}
    shutil.copyfile(ROOT/'tests/fixtures/native_rng_vectors.tsv',OUT/'rng_vectors.tsv')
    baseline=ROOT/'tests/fixtures/normal_generation_before_session.tsv'
    proof=json.loads((ROOT/'tests/fixtures/normal_generation_before_session.json').read_text())
    assert sha(baseline)==proof['trace_sha256']
    shutil.copyfile(baseline,OUT/'generation_baseline.tsv')
    def bind(p):refs[p]=dict(path=p,sha256=sha(ROOT/p))
    bind('tests/fixtures/native_rng_vectors.tsv');bind('tests/fixtures/native_rng_provenance.json')
    bind('tests/fixtures/normal_generation_before_session.tsv');bind('tests/fixtures/normal_generation_before_session.json')
    resources=OriginalResources(ROOT/'runtime/game');raw,source=resources.read('stages.xml');bind('runtime/game/'+source['file'])
    decoded=json.loads((ROOT/'evidence/rooms.json').read_text());stages={};catalog=[]
    for stage in ET.fromstring(raw):
        sid=int(stage.get('id'));request='rooms/'+str(Path(stage.get('path')).with_suffix('.stb')).replace('\\','/').lower()
        try:raw,e=resources.read(request)
        except ValueError as error:
            if str(error)!='Original resource missing: '+request:raise
            assert sid==24;continue
        bind('runtime/game/'+e['file']);meta=next(s for s in decoded if s['package']==e['package'] and s['sha256']==e['sha256'])
        path='recovered/'+meta['decoded'];bind(path);rooms=[json.loads(line) for line in (ROOT/path).open()];stages[sid]=rooms
        for r in rooms:
            # Raw header/weight and whole-record checksum independently bind
            # the decoded expectations to the byte range the engine must read.
            data=raw[r['file_offset']:r['file_offset']+r['file_bytes']]
            assert len(data)==r['file_bytes'] and struct.unpack_from('<IIIB',data)==(r['type'],r['variant'],r['subtype'],r['difficulty'])
            assert struct.unpack_from('<I',data,15+len(bytes.fromhex(r['name_bytes_hex'])))[0]==r['weight_bits']
            h=2166136261
            for n in data:h=((h^n)*16777619)&0xffffffff
            slots=[(-1,3),(6,-1),(r['width'],3),(6,r['height']),(-1,10),(19,-1),(r['width'],10),(19,r['height'])]
            replacements={9:{0:(12,3),1:(6,6)},10:{2:(13,3),5:(19,6)},11:{3:(6,7),4:(12,10)},12:{6:(13,10),7:(19,7)}}
            for i,xy in replacements.get(r['shape'],{}).items():slots[i]=xy
            r['mask']=sum(1<<i for i,xy in enumerate(slots) if any(d['exists_byte'] and (d['x'],d['y'])==xy for d in r['doors']))
            r['id']=0x10000000+sid*65536+r['index']
            row=[r['id'],sid,r['index'],r['type'],r['variant'],r['subtype'],r['difficulty'],r['shape'],r['mask'],r['width'],r['height'],r['weight_bits'],r['file_offset'],r['file_bytes'],h or 1,int(e['package'].split('_')[1])]
            catalog.append(row)
    def tsv(name,rows):(OUT/name).write_text(''.join('\t'.join(map(str,row))+'\n' for row in rows),encoding='ascii')
    tsv('catalog.tsv',catalog)
    reference=json.loads((ROOT/'runtime/port/native_data.json').read_text())['rng_reference'];bind(reference['path'])
    memory=(ROOT/reference['path']).read_bytes();shifts=struct.unpack_from('<243I',memory,0x9e9a88-0x9e8000)
    def draw(n,index):return shift(n,*shifts[index*3:index*3+3])
    def sample(n):return f32(f32(float(n))*struct.unpack('<f',bytes.fromhex('feff7f2f'))[0])
    vectors=[]
    for sid,rooms in stages.items():
        for doors in (0,15):
            selected=[r for r in rooms if r['type']==1 and r['difficulty'] in range(1,16) and r['mask']&doors==doors]
            weights={r['id']:r['weight'] for r in rooms}
            for seed in (1,0x12345678,0xffffffff,0x48c0ffee,17,29,31,47):
                total=exact=0.;nexact=0
                for r in selected:
                    total=f32(total+weights[r['id']])
                    if doors and r['mask']==doors:exact=f32(exact+weights[r['id']]);nexact+=1
                out=-1;draws=0;result=2;after=0 # NR_NO_MATCH below, verified enum.
                if total>0:
                    n=draw(seed,7);point=sample(n);draws=1;prefer=False
                    if nexact:n=draw(n,7);draws+=1;prefer=sample(n)<f32(f32(exact/total)*f32(9.99))
                    limit=f32(point*(exact if prefer else total));acc=0.
                    for r in selected:
                        if prefer and r['mask']!=doors:continue
                        acc=f32(acc+weights[r['id']])
                        if limit<acc:
                            out=r['id'];weights[out]=max(f32(weights[out]*f32(.1)),f32(.0000001));after=bits(weights[out]);result=0;break
                vectors.append([sid,doors,seed,result,out,draws,after])
    tsv('selectors.tsv',vectors)
    floors=[]
    for seed in (1,17,0x12345678,0xffffffff,0x48c0ffee):
        n=draw(draw(seed,27),27) # SetStart fills stage0, then stage1.
        for mode in (0,1):
            for kind in range(3):
                for curses in (0,2,4):
                    #006fa027 is the second Level::Init advance.
                    #Explicit fixture setup owner is its clone.
                    initial=draw(draw(n,35),35);layout=draw(initial,35);target=8+(layout&1)
                    if curses&2:target=int(target*1.8)
                    elif curses&4:target+=4
                    secondary=n;setup=draw(initial,35)
                    if mode:target+=2+(setup&1)
                    topology=draw(layout,35)
                    low,high=((5,15) if mode else (1,10)) if curses&2 else ((5,10) if mode else (1,5))
                    eligible=[r for r in stages[kind+1] if r['type']==1 and low<=r['difficulty']<=high]
                    if len(eligible)<20:
                        low,high=1,15;eligible=[r for r in stages[kind+1] if r['type']==1 and low<=r['difficulty']<=high]
                    hist=[sum(r['shape']==shape for r in eligible) for shape in range(13)]
                    floors.append([seed,mode,kind,curses,target,6 if curses&2 else 5,low,high,len(eligible),topology,secondary,setup]+hist)
    tsv('floors.tsv',floors)
    def mid_pick(seed,kind,subtype,low,high):
        rows=[r for r in stages[0] if r['type']==kind and (subtype<0 or r['subtype']==subtype) and low<=r['difficulty']<=high]
        total=0.
        for r in rows:total=f32(total+r['weight'])
        if total<=0:return -1,0
        limit=f32(sample(draw(seed,7))*total);acc=0.
        for r in rows:
            acc=f32(acc+r['weight'])
            if limit<acc:return r['id'],1
        return -1,1
    mid=[]
    for routine in range(5):
        for seed_index in range(1,65):
            seed=1 if seed_index==1 else 0xffffffff if seed_index==2 else (seed_index*0x9e3779b9)&0xffffffff
            for case in range(16):
                stage=(1,2,3,7)[case%4];kind=4 if case&8 else 0;effective=stage+(kind in (4,5))
                keys=2 if case&1 else 0;coins=5 if case&2 else 0
                flags=(case&1)|((bool(case&2))<<1)|((bool(case&8))<<2)|((bool(case&4))<<3)|((bool(case&1))<<4)|((bool(case&2))<<5)|((bool(case&4))<<6)|((bool(case&8))<<7)|((stage in (2,4,6,8))<<8)|((bool(case&2))<<9)|((bool(case&4))<<10)
                flags|=(bool(case&1)<<11)|(bool(case&2)<<12)|(bool(case&4)<<13)
                disabled=(0x7e00 if case==15 else 1<<11 if case==4 else 0)
                owner=seed;ndraws=0;selection_seed=0;definition=-1;subtype=-1;calls=selector_draws=0;roll=roll2=0;lowflag=highflag=0;chance=f32((0.,.01,.25,1.)[case%4]);chance_roll=0.
                def advance():
                    nonlocal owner,ndraws
                    owner=draw(owner,35);ndraws+=1;return owner
                def select(room_type,subtype,low=1,high=10):
                    nonlocal calls,selector_draws
                    calls+=1;definition,draws=mid_pick(selection_seed,room_type,subtype,low,high);selector_draws+=draws;return definition
                if routine==0:
                    room_type=6;bound=0
                    if effective>2:
                        bound=30 if flags&1 else 80
                        if flags&2:bound=10
                        if flags&4:bound=5
                    if effective>=3 and advance()%10==0 and not flags&8:advance();subtype=14
                    else:
                        super_variant=bool(bound and advance()%bound==0)
                        candidates=[n+(7 if super_variant else 0) for i,n in enumerate((2,3,1,0,5,6)) if not disabled&(1<<(i+9))]
                        if candidates:subtype=candidates[advance()%len(candidates)]
                    if subtype>=0:
                        selection_seed=advance();difficulty=1 if stage<7 else 10;definition=select(6,subtype,difficulty,difficulty)
                        if definition<0:selection_seed=advance();definition=select(6,subtype)
                    pre_geometry,pre_draws=owner,ndraws
                    install=False
                    if not flags&16:
                        roll=advance();install=roll&3==0
                        if not install:roll2=advance();install=roll2%3==0 and effective==1
                    for i,n in enumerate((2,3,1,0,5,6)):
                        if subtype in (n,n+7):lowflag=0x200<<i
                    highflag=int(subtype==14)
                elif routine==1:
                    room_type=11;subtype=int(bool(flags&256));selection_seed=advance();definition=select(11,subtype)
                    pre_geometry,pre_draws=owner,ndraws;install=False
                    if definition>=0 and not flags&32:
                        roll=advance();install=(not roll&1 or effective>=3) and bool(flags&512) and effective>=2
                elif routine==2:
                    roll=advance();type20Requested=roll%10==0
                    if not type20Requested:roll2=advance();type20Requested=roll2%3==0 and keys>1
                    type20Requested=type20Requested and not flags&128;eligible=bool(flags&1024)
                    room_type=20 if type20Requested and not eligible else 9;subtype=-1 if room_type==20 else int(eligible)
                    selection_seed=advance();definition=select(room_type,subtype)
                    if definition<0 and room_type==9 and subtype==1:subtype=0;selection_seed=advance();definition=select(9,0)
                    pre_geometry,pre_draws=owner,ndraws
                    configured=bool(flags&(64 if room_type==9 else 128));resource=(coins>=5 or eligible) if room_type==9 else keys>1
                    install=not configured and resource and (effective in (2,4,6,8) or (eligible and effective<11))
                elif routine==3:
                    room_type=24;subtype=-1;selection_seed=advance();definition=select(24,-1);pre_geometry,pre_draws=owner,ndraws;install=False
                    if definition>=0 and not flags&(2048|4096):
                        roll=advance();chance_roll=sample(roll);install=chance_roll<chance and bool(flags&8192)
                else:
                    room_type=0;pre_geometry,pre_draws=owner,ndraws;install=False
                    if stage<7:
                        predicate=not flags&32 and bool(flags&4)
                        odd=advance()&1
                        room_type=18 if (not odd or not predicate) and not flags&16 else 19
                        selection_seed=advance();definition=select(room_type,-1)
                        pre_geometry,pre_draws=owner,ndraws
                        roll=advance();install=roll%50==0
                        if not install:roll2=advance();install=roll2%5==0 and bool(flags&8)
                mid.append([routine,seed,stage,kind,keys,coins,flags,disabled,room_type,subtype,definition,0 if definition>=0 else 2,owner,ndraws,selector_draws,calls,pre_geometry,pre_draws,selection_seed,roll,roll2,int(install),lowflag,highflag,bits(chance),bits(chance_roll)])
    tsv('mid_special.tsv',mid)
    # Bind the boss profile actually used by the normal caller to its resolver.
    boss=json.loads((ROOT/'runtime/port/native_boss_data.json').read_text());p=next(p for p in boss['profiles'] if p['profile']==12)
    for request,key in (('bosspools.xml','pool_source'),('bossportraits.xml','portrait_source')):
        e=resources.resolve(request);assert (e['package'],e['sha256'])==(p[key]['package'],p[key]['sha256']);bind('runtime/game/'+e['file'])
    for path in ('tools/prepare_normal_flow.py','tools/original_resources.py','evidence/rooms.json','runtime/game/file_index.bin','recovered/main/disassembly.txt'):
        bind(path)
    for path in ('tools/verify_level_init_reference.py','runtime/port/normal_level_init_reference.json','evidence/reference_instructions/normal-level-init-continuation-reference.txt','evidence/reference_instructions/normal-level-init-post-curse-reference.txt'):
        bind(path)
    for path in ('tools/prepare_mid_special_reference.py','runtime/port/normal_mid_special_reference.json','logs/normal-mid-special-reference.txt','tools/prepare_floor_tail_reference.py','runtime/port/normal_grid_room_reference.json','logs/normal-grid-room-reference.txt'):
        bind(path)
    from prepare_secret_vectors import prepare
    geometry,blacklists=prepare(OUT,stages,draw)
    bind('tools/prepare_secret_vectors.py')
    offgrid=[]
    for i in range(1,33):
      for kind in range(3):
       for gates in range(8):
        for held550 in (0,1):
            seed=1 if i==1 else i*0x9e3779b9&0xffffffff;state=seed;row=[seed,kind,gates,held550]
            rolls=[]
            for _ in range(2):state=draw(state,35);rolls.append(bits(sample(state)))
            entries=[];subtype=0
            for cell,stage,typ,sub in [(-2,0,3,-1),(-6,0,22,-1),(-4,0,16,-1),(-5,0,17,-1),(-7,0,5,55),(-8,13,1,1),(-13,0,2,0),(-18,0,15,1)]:
                owner_index=35;owner=state
                if cell==-13:
                    state=draw(state,35);owner=state;owner_index=19
                    owner=draw(owner,19);subtype=owner%(1+bool(gates&2))
                    owner=draw(owner,19);subtype+=owner%(1+bool(gates&1)+bool(gates&4));sub=subtype
                elif cell==-18:state=draw(state,35);owner=state;owner_index=20
                entry=owner;room_seeds=[]
                for _ in range(3):owner=draw(owner,owner_index);room_seeds.append(owner)
                room_seeds.append(shift(owner,11,7,12));owner=draw(owner,owner_index)
                rows=[r for r in stages[stage] if r['type']==typ and (sub<0 or r['subtype']==sub) and r['difficulty'] in (range(1,11) if cell!=-8 else [0])]
                if cell==-5 and held550:
                    rows=[r for r in stages[stage] if r['type']==17 and r['variant']==0];definition=rows[0]['id'] if rows else -1
                else:
                    total=0.
                    for r in rows:total=f32(total+r['weight'])
                    limit=f32(sample(draw(owner,7))*total);acc=0.;definition=-1
                    for r in rows:
                        acc=f32(acc+r['weight'])
                        if limit<acc:definition=r['id'];break
                entries.extend([definition,entry,owner,owner_index]+room_seeds)
                if owner_index==35:state=owner
            offgrid.append(row+[state]+rolls+[subtype]+entries)
    tsv('offgrid.tsv',offgrid)
    bind('evidence/reference_instructions/normal-floor-return-reference.txt')
    from prepare_level_doors import prepare as prepare_doors
    doors=prepare_doors(OUT,stages)
    for path in ('tools/prepare_level_doors.py','runtime/port/normal_level_doors_reference.json','logs/normal-level-doors-reference.txt'):bind(path)
    from prepare_room_entry import prepare as prepare_entry
    room_entry,entry_refs=prepare_entry(OUT,stages)
    for path in entry_refs:bind(path)
    from prepare_door_actors import prepare as prepare_actors
    for path in prepare_actors(OUT):bind(path)
    from prepare_room_space import prepare as prepare_space
    for path in prepare_space(OUT):bind(path)
    from prepare_player_input import prepare as prepare_input
    for path in prepare_input(OUT):bind(path)
    from prepare_player_motion import prepare as prepare_motion
    for path in prepare_motion(OUT):bind(path)
    from prepare_static_grid import prepare as prepare_static_grid
    for path in prepare_static_grid(OUT,stages):bind(path)
    from prepare_player_startup import prepare as prepare_player_startup
    for path in prepare_player_startup(OUT):bind(path)
    from prepare_entity_body import prepare as prepare_entity_body
    for path in prepare_entity_body(OUT):bind(path)
    from prepare_player_sprites import prepare as prepare_player_sprites
    for path in prepare_player_sprites(OUT):bind(path)
    from prepare_player_aux_sprites import prepare as prepare_player_aux_sprites
    for path in prepare_player_aux_sprites(OUT):bind(path)
    from prepare_player_state import prepare as prepare_player_state
    for path in prepare_player_state(OUT):bind(path)
    from prepare_player_creation import prepare as prepare_player_creation
    for path in prepare_player_creation(OUT):bind(path)
    from prepare_room_runtime import prepare as prepare_room_runtime
    for path in prepare_room_runtime():bind(path)
    (OUT/'manifest.json').write_text(json.dumps(dict(format='normal-flow-inputs-v4',records=len(catalog),selectors=len(vectors),floors=len(floors),mid_special=len(mid),secret_geometry=geometry,secret_blacklists=blacklists,offgrid=len(offgrid),level_doors=doors,sources=list(refs.values()),oracle_scope='Pinned raw data and verified instructions with separate arithmetic expectations; no original execution trace.'),indent=2)+'\n')
    print('Normal-flow inputs:',len(catalog),'raw record expectations,',len(vectors),'selection vectors,',len(floors),'floor contexts.')
if __name__=='__main__':main()

"""Original door geometry/factory and pre-asset binding reference packet."""
from pathlib import Path
import hashlib,json,random,re,struct
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def fold(h,n):
    for b in struct.pack('<I',n&0xffffffff):h=((h^b)*16777619)&0xffffffff
    return h
def mt(seed):
    words=[seed]
    for i in range(1,624):words.append(((words[-1]^(words[-1]>>30))*1812433253+i)&0xffffffff)
    for i in range(624):
        n=(words[i]&0x80000000)|(words[(i+1)%624]&0x7fffffff)
        words[i]=words[(i+397)%624]^(n>>1)^(0x9908b0df if n&1 else 0)
    return words
def next_mt(words,i):
    n=words[i];n^=n>>11;n^=(n<<7)&0x9d2c5680;n^=(n<<15)&0xefc60000;return n^(n>>18)
def select(c,variant,aux):
    #The labels follow006cb870's integer branch graph, rather than the C++
    #boolean simplification. Graphics/animation callees are intentionally outside.
    st,tt,sv,tv,sf,tf,stage,kind,mode,current,target,dimension,treasure,count,challenge,mirror,effect,trap,sd,td=c
    alt=mode in (2,3)
    if (sf|tf)&0x400:
        if st in (7,8):st=1
        if tt in (7,8):tt=1
    pc='943';steps=0
    while True:
        steps+=1;assert steps<30,pc
        if pc=='943':pc='b05' if variant else '94e'
        elif pc=='94e':
            if alt and tt==23 and count<10+(mode==3):variant=6;pc='b32'
            elif st in (7,8,29,10):pc='9f9'
            elif tt==27:
                if not td:pc='9f9'
                elif tv:pc='a7e'
                else:pc='9f9'
            elif st==27:pc='a7e' if sd and sv else '9f9'
            elif tt==5:pc='a7e' if td and tv==90 else '9f9'
            elif st==5:pc='a7e' if sd and sv==90 else '9f9'
            else:pc='9f9'
        elif pc=='a7e':
            if tt==27 or st==27:
                subtype=(tv if td else 0) if tt==27 else (sv if sd else 0)
                variant=3 if subtype in (2,4) else 1
                if subtype==3:aux=1
            else:variant=3
            if current==-10:variant=8
            pc='b05'
        elif pc=='9f9':
            if current==-16:variant=8;pc='b32'
            elif target==-101 and dimension==0:variant=3;pc='b14'
            elif target==-100:pc='c56'
            else:pc='bfc'
        elif pc=='bfc':
            if st in (7,8,29,10):pc='ce3'
            elif tt in (9,24,12):pc='c56'
            elif tt in (2,4):
                if mirror and dimension==1:pc='cac'
                elif stage==1 and kind not in (4,5):pc='cac'
                elif alt or (tt==2 and effect):pc='cac'
                else:pc='c56'
            elif tt==5 and target>=0 and stage==9:pc='c56'
            else:pc='cac'
        elif pc=='cac':pc='c56' if challenge==24 and tt not in (7,8,29) else 'ce3'
        elif pc=='c56':variant=1;pc='b14'
        elif pc=='ce3':
            if alt and tt==4 and st not in (7,8) and target==treasure:pc='c56'
            elif st in (7,8,29):pc='d3f'
            elif tt in (18,19):variant=3;pc='b05'
            elif tt in (20,21):variant=2;pc='b05'
            elif st==6:pc='d4e'
            else:pc='d3f'
        elif pc=='d3f':pc='d4e' if trap else 'd9e'
        elif pc=='d4e':
            if not sf&1 and tt not in (7,8,29):variant=4;pc='b32'
            else:pc='d9e'
        elif pc=='d9e':
            if target==-7:variant=5;pc='b32'
            elif st in (7,8,29) or tt in (7,8,29):variant=7;pc='b32'
            else:variant=8;pc='b32'
        elif pc=='b05':pc='b14' if variant in (1,2,3) else 'b32'
        elif pc=='b14':
            if sf&0x400 and not tf&0x400:variant=8
            pc='b32'
        elif pc=='b32':return [st,tt,variant,aux,int(tt==29)]
        else:raise ValueError(pc)
def prepare(out):
    memory=Memory(ROOT/'recovered/main');source=ROOT/'recovered/main/disassembly.txt'
    ranges=[(0x6c5670,0x6c56e0),(0x6cbe50,0x6cbed1),(0x6cb870,0x6cbde3),(0x79c7b0,0x79c9dc),(0x79faf0,0x79fcf7),(0x6f430f,0x6f443d),(0x6ae7e0,0x6ae970),(0x700510,0x700523),(0x6b5fc0,0x6b5fd6)]
    lines=[]
    for line in source.open():
        m=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if m and any(a<=int(m[1],16)<b for a,b in ranges):
            at=int(m[1],16);raw=bytes.fromhex(m[2]);assert memory.read(at,len(raw))==raw;lines.append(line)
    reference=ROOT/'evidence/reference_instructions/door-actors.txt';reference.write_text(''.join(lines),encoding='ascii')
    assert memory.words(0xb2a7cc,1)==(0x6cbe50,)
    angles=struct.unpack('<4f',memory.read(0xb4ba60,16));assert angles==(-90.,0.,90.,180.)
    tables={shape:memory.words(at,8) for shape,at in ((0,0x79fd78),(9,0x79fcf8),(10,0x79fd18),(11,0x79fd38),(12,0x79fd58))}
    widths={s:(15 if s<=5 else 28) for s in range(1,13)};heights={s:(9 if s<=3 or s in (6,7) else 16) for s in range(1,13)}
    rows=[]
    for shape in range(1,13):
        w,h=widths[shape],heights[shape];left=4 if shape in (3,5) else 0;right=w-1-left;top=3 if shape in (2,7) else 0;bottom=h-1-top
        #Map the actual saved switch destinations to their row/column expressions.
        destinations={0x79fce3:(left,4),0x79fc83:(7,top),0x79fce5:(right,4),0x79fc97:(7,bottom),0x79fca9:(left,11),0x79fcbd:(20,top),0x79fcab:(right,11),0x79fcd1:(20,bottom),
            0x79fb57:(13,4),0x79fb1b:(7,7),0x79fb2f:(0,11),0x79fb43:(20,0),0x79fbc1:(0,4),0x79fb7f:(7,0),0x79fb93:(14,4),0x79fbaa:(20,7),0x79fbe6:(7,8),0x79fbfd:(13,11),0x79fc25:(14,11),0x79fc3c:(20,8)}
        grids=[destinations[p][0]+destinations[p][1]*w for p in tables.get(shape,tables[0])]
        for seed in (1,5489,0x12345678,0xffffffff):
            words=mt(seed)
            for allowed in range(256):
                frame=allowed*17;count=allowed.bit_count();hsh=2166136261;owner=2166136261
                for n in words:owner=fold(owner,n)
                owner=fold(owner,count if count else 624)
                for n in (allowed,count):hsh=fold(hsh,n)
                for n in grids:hsh=fold(hsh,n)
                draw=0;cost=[0]*448
                for slot,cell in enumerate(grids):
                    if not allowed&(1<<slot):continue
                    seed_value=next_mt(words,draw);draw+=1;cost[cell]=1000
                    values=[cell,slot,slot&3,16,0,1,0,0,0,-1,1,0,seed_value,1,frame,0,0xaa17414f,35]
                    for n in values:hsh=fold(hsh,n)
                    for value in ((cell%w)*40.+40.,(cell//w)*40.+120.,angles[slot&3],angles[slot&3]):hsh=fold(hsh,struct.unpack('<I',struct.pack('<f',value))[0])
                    for n in (1,0,0,0):hsh=fold(hsh,n)
                for n in cost:hsh=fold(hsh,n)
                if not count:
                    #No MT twist occurs when there are no initialized doors.
                    initial=[seed]
                    for i in range(1,624):initial.append(((initial[-1]^(initial[-1]>>30))*1812433253+i)&0xffffffff)
                    owner=2166136261
                    for n in initial+[624]:owner=fold(owner,n)
                rows.append([shape,allowed,seed,frame,hsh,owner])
    (out/'door_actors.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in rows),encoding='ascii')
    selections=[]
    for st in range(32):
      for tt in range(32):
       for case in range(96):
        n=(case*0x9e3779b9+st*0x45d9f3b+tt*0x119de1f3)&0xffffffff
        sv=(0,1,2,3,4,90)[(case//4)%6];tv=(0,1,2,3,4,90)[case%6]
        source_flags=(0x400 if n&1 else 0)|(1 if n&2 else 0);target_flags=0x400 if n&4 else 0
        c=[st,tt,sv,tv,source_flags,target_flags,(1,2,9,13)[case%4],(0,4,5)[(case//3)%3],(case//4)%4,
            (84,-16,-10)[(case//8)%3],(71,-101,-100,-7,-2)[(case//2)%5],case%3,71,(9,10,11,12)[case%4],24 if n&8 else 0,
            int(bool(n&16)),int(bool(n&32)),int(bool(n&64)),int(bool(n&128)),int(bool(n&256))]
        variant=0 if case<72 else 1+(case%8);aux=case%3
        selections.append(c+[variant,aux]+select(c,variant,aux))
    (out/'door_variants.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in selections),encoding='ascii')
    mt_rows=[]
    for seed in (0,1,5489,0x12345678,0xffffffff):
        initial=[seed]
        for i in range(1,624):initial.append(((initial[-1]^(initial[-1]>>30))*1812433253+i)&0xffffffff)
        for skip in (0,1,622,623,624,625,1246,1247,1248):
            #Independent stdlib MT implementation, with the saved seed routine's
            #624 words supplied directly (Python's own seed mixer is not used).
            rng=random.Random(0);rng.setstate((3,tuple(initial+[624]),None))
            for _ in range(skip):rng.getrandbits(32)
            draws=[rng.getrandbits(32) for _ in range(10)];hsh=2166136261
            for word in rng.getstate()[1]:hsh=fold(hsh,word)
            mt_rows.append([seed,skip]+draws+[hsh])
    (out/'door_mt.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in mt_rows),encoding='ascii')
    table=memory.words(0x6cbde4,4);assert table==(0x6cbae8,0x6cbad3,0x6cbae1,0x6cbad3)
    memory_sources=[dict(path='recovered/main/'+r['file'],sha256=r['sha256']) for _,_,r in memory.blocks]
    report=dict(format='native-door-actors-reference-v1',source_module_sha256=next(e['sha256'] for e in json.loads((ROOT/'evidence/modules.json').read_text()) if e['id']=='main'),
        instructions=dict(path=reference.relative_to(ROOT).as_posix(),sha256=sha(reference),count=len(lines)),
        geometry_tables={str(s):[f'{p:08x}' for p in t] for s,t in tables.items()},angles=list(angles),custom_variant_table=[f'{p:08x}' for p in table],memory_sources=memory_sources,
        actor_cases=len(rows),variant_cases=len(selections),mt_boundary_cases=len(mt_rows),mt_oracle='CPython standard-library MT with624 saved-algorithm seed words; no Python seed mixing. Values and full owner hashes across twist boundaries.',
        ownership='Each initialized door consumes one006ae820 global MT value in ascending enabled-slot order. Attached slots draw none; descriptor/floor/local seed owners are not consumed.',
        scope='Fresh door factory0079c7b0, reset006cbe50, geometry0079faf0 and integer selection of006cb870 before006c8140. Separate arithmetic/control-flow vectors are not original execution traces. Graphics, animation, interaction, payment, blast, secret-door opening and complete native entry remain open.')
    (ROOT/'runtime/port/normal_door_actors_reference.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Door actors:',len(rows),'factory/MT cases;',len(selections),'native variant cases.')
    return ['tools/prepare_door_actors.py','tools/reference_memory.py','recovered/main/memory_blocks.jsonl','runtime/port/normal_door_actors_reference.json',reference.relative_to(ROOT).as_posix()]+[e['path'] for e in memory_sources]

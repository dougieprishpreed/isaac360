"""Pin room-entry instructions/data and generate independent spawn/setup vectors."""
from pathlib import Path
import hashlib,json,re,struct,xml.etree.ElementTree as ET
from reference_memory import Memory
from original_resources import OriginalResources
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def f32(n):return struct.unpack('<f',struct.pack('<f',n))[0]
def shift(n,a,b,c):
    n^=n>>a;n=(n^(n<<b))&0xffffffff;return n^(n>>c)
def mark(t,v,s,m):
    #flags, six marker booleans, optional direction. Order is significant.
    if t in (825,907):m[1]|=1
    elif t==829:m[1]|=2
    elif t in (876,918):m[1]|=8;m[0]|=0x1000
    elif t==882:m[0]|=0x1000
    elif t==905:m[1]|=16
    elif t==919:m[1]|=32
    elif t==970:
        if v==0:m[0]|=0x200
        elif v==3:m[0]|=0x2000
        elif v==1:
            if s==20:m[1]|=2
            elif s==11:m[1]|=4
            elif s==10:m[1]|=8;m[0]|=0x1000
            elif s<4:m[2]=s
def prepare_backdrops():
    resources=OriginalResources(ROOT/'runtime/game');raw,binding=resources.read('backdrops.xml');backdrops=ET.fromstring(raw)
    modes={int(e.get('id')):int(e.get('waterpitsmode','0')) for e in backdrops}
    assert all(n in (0,1,2) for n in modes.values())
    generated=ROOT/'build/generated/native_room_setup_data.h';generated.parent.mkdir(parents=True,exist_ok=True)
    generated.write_text('//Generated from the inherited normal backdrops.xml. Missing IDs stay unresolved.\n'
        'static const int kNativeBackdropSetupCount=%d;\nstatic const signed char kNativeBackdropWaterPitsMode[]={%s};\n'%(max(modes)+1,','.join(str(modes.get(i,-1)) for i in range(max(modes)+1))),encoding='ascii')
    return modes,binding
def prepare(out,stages):
    mem=Memory(ROOT/'recovered/main');listing=ROOT/'recovered/main/disassembly.txt'
    spans=[(0x6aece0,0x6aefc8),(0x7a0b28,0x7a10cf),(0x7a12c1,0x7a13b9),(0x7a3c18,0x7a3c5a),
           (0x79b7b8,0x79b7c4),(0x87ac80,0x87acb0),(0x87cd5d,0x87cdc1),(0x87d390,0x87d3d0),(0x87d500,0x87d530)]
    lines=[]
    for line in listing.open():
        m=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if m and any(a<=int(m[1],16)<b for a,b in spans):
            at=int(m[1],16);raw=bytes.fromhex(m[2]);assert mem.read(at,len(raw))==raw;lines.append(line)
    pinned=ROOT/'logs/normal-room-entry-reference.txt';pinned.write_text(''.join(lines),encoding='ascii')
    continuation=ROOT/'evidence/reference_instructions/native-entry-marker-prefix.txt';verified=0
    for line in continuation.read_text().splitlines():
        m=re.match(r'^\s+([0-9A-F]{8}): ((?:[0-9A-F]{2} )+)',line)
        if m:
            at=int(m[1],16);raw=bytes.fromhex(m[2]);assert mem.read(at,len(raw))==raw;verified+=1
    assert verified>=15
    assert mem.words(0x9e9b0c,3)==(2,7,7) and mem.words(0x9e9ad0,3)==(1,19,3) and mem.words(0x9e9c2c,3)==(5,9,7)
    destinations=mem.words(0x7a4610,8);table=mem.read(0x7a4630,146)
    actions={825:0x7a0dc8,829:0x7a0c8a,876:0x7a0caa,882:0x7a0cb1,905:0x7a0dd4,907:0x7a0dc8,918:0x7a0caa,919:0x7a0de0,970:0x7a0c5e}
    assert {825+i:destinations[n] for i,n in enumerate(table) if destinations[n]!=0x7a0cbb}==actions
    resources=OriginalResources(ROOT/'runtime/game');modes,binding=prepare_backdrops()
    marker_rows=[]
    for t in range(824,972):
        for v in range(5):
            for s in (0,1,2,3,4,10,11,20,65535):
                m=[0,0,-1];mark(t,v,s,m);marker_rows.append([t,v,s]+m)
    (out/'room_markers.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in marker_rows),encoding='ascii')
    setup_rows=[]
    for backdrop in modes:
      for seed in (1,17,0x12345678,0xffffffff):
       for case in range(64):
        stage=(1,27,28,33)[case%4];typ=(1,2,16,27)[(case>>2)%4];visits=case>>5
        flags=(0x320 if case&1 else 0x40);level=0x400000 if case&2 else 0;disabled=bool(case&4)
        marker=[0,((case>>1)^case)&63,-1];mark(970,0 if case&8 else 3,0,marker)
        combined=flags|marker[0];water_seed=style=seed;wd=sd=0;water=int(backdrop in (6,44))
        if backdrop==12 and typ==1:water_seed=shift(seed,1,19,3);wd=1;water=int(water_seed%5==0)
        if stage in (27,28) and typ!=27:water=1
        if combined&0x100 or marker[1]&4:water=1
        if backdrop in (16,35) or typ==16 or (marker[1]&8 and not combined&0x100):water=0
        if not visits:
            if backdrop==6 and not disabled:pits=True
            elif modes[backdrop]==1:style=shift(style,5,9,7);sd+=1;pits=not(style&1)
            else:pits=modes[backdrop]==2
            combined=(combined|0x20) if pits else combined&~0x20
            style=shift(style,5,9,7);sd+=1;combined=(combined|0x40) if not(style&1) else combined&~0x40
            if backdrop==32 and combined&0x200:combined&=~0x20
            force=bool(marker[1]&1);deny=bool(marker[1]&2)
            if backdrop in (58,59):
                combined|=0x2000
                if level&0x400000:force=True;deny=False
            if force:combined|=0x20
            if deny:combined&=~0x20
            if backdrop==46 and combined&0x20:combined|=0x1000
        setup_rows.append([backdrop,seed,stage,typ,visits,flags,level,int(disabled),marker[0],marker[1],combined,water_seed,style,wd,sd,water,int(not visits and typ==2)])
    (out/'room_setup.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in setup_rows),encoding='ascii')
    def fold(h,n):
        for v in struct.pack('<I',n&0xffffffff):h=((h^v)*16777619)&0xffffffff
        return h
    rawstages,_=resources.read('stages.xml');stage_paths={int(e.get('id')):'rooms/'+str(Path(e.get('path')).with_suffix('.stb')).replace('\\','/').lower() for e in ET.fromstring(rawstages)}
    count=0;seeds=(1,0x18000,0x12345678,0xffffffff);actual_markers=set()
    with (out/'room_spawns.bin').open('wb') as f:
        f.write(b'NRSPAWN1');f.write(struct.pack('<I',sum(map(len,stages.values()))))
        for sid,rooms in stages.items():
            raw,_=resources.read(stage_paths[sid])
            for r in rooms:
                blob=raw[r['file_offset']:r['file_offset']+r['file_bytes']];f.write(struct.pack('<III',r['id'],len(blob),len(seeds)));f.write(blob)
                for seed in seeds:
                    state=seed;choices=[];markers=[0,0,-1]
                    for stack in r['spawns']:
                        state=shift(state,2,7,7);selected=None;rows=stack['entities']
                        if rows:
                            total=0.
                            for e in rows:total=f32(total+e['weight'])
                            sample=f32(f32(float(state))*struct.unpack('<f',bytes.fromhex('feff7f2f'))[0]);limit=f32(sample*total);acc=0.
                            for i,e in enumerate(rows):
                                acc=f32(acc+e['weight'])
                                if limit<acc:selected=(i,e);break
                            assert selected is not None
                        if selected:
                            i,e=selected;mark(e['type'],e['variant'],e['subtype'],markers)
                            if e['type'] in actions:actual_markers.add((e['type'],e['variant'],e['subtype']))
                            grid=(stack['y']+1)*(r['width']+2)+stack['x']+1
                            choices.append([1,grid,state,i,e['type'],e['variant'],e['subtype'],e['weight_bits']])
                        else:choices.append([0])
                    h=2166136261
                    for n in [len(choices),state,state]+markers:
                        h=fold(h,n)
                    for choice in choices:
                        for n in choice:h=fold(h,n)
                    f.write(struct.pack('<III',seed,state,h));count+=1
    report=dict(format='normal-room-entry-reference-v1',source_module_sha256=next(e['sha256'] for e in json.loads((ROOT/'evidence/modules.json').read_text()) if e['id']=='main'),
        saved_listing=dict(path=listing.relative_to(ROOT).as_posix(),sha256=sha(listing)),
        instructions=dict(path=pinned.relative_to(ROOT).as_posix(),sha256=sha(pinned),count=len(lines)),
        continuation=dict(path=continuation.relative_to(ROOT).as_posix(),sha256=sha(continuation),verified_lines=verified,span_sha256=hashlib.sha256(mem.read(0x7a0b5c,0x64)).hexdigest()),
        marker_dispatch=dict(table='007a4630',destinations='007a4610',actions={str(k):f'{v:08x}' for k,v in actions.items()},raw_hex=table.hex()),
        backdrop_source=binding,spawn_cases=count,marker_cases=len(marker_rows),setup_cases=len(setup_rows),normal_selected_marker_identities=sorted(actual_markers),
        owner='Preliminary award2/7/7 COPY at007a0b64/98. Main ORIGINAL at007a12e5. Separate spawn1/19/3 water clone and spawn5/9/7 fresh pit/style clone; no descriptor or floor owner advances.',
        scope='Unmodified weights and fresh no-saved-grid stack choices, preliminary marker actions and water/pit setup arithmetic. Independent Python/data expectations, not recorded original execution. Main actor/grid behavior, later native Init/player/doors and full saves remain open.')
    report_path=ROOT/'runtime/port/normal_room_entry_reference.json';report_path.write_text(json.dumps(report,indent=2)+'\n')
    refs=['tools/prepare_room_entry.py','runtime/port/normal_room_entry_reference.json','logs/normal-room-entry-reference.txt','evidence/reference_instructions/native-entry-marker-prefix.txt','runtime/game/'+binding['file']]
    print('Room entry:',count,'raw spawn cases,',len(marker_rows),'marker cases,',len(setup_rows),'setup cases.')
    return dict(spawns=count,markers=len(marker_rows),setup=len(setup_rows)),refs

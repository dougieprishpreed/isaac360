"""Pin original door-finalization instructions and independent finite fixtures."""
from pathlib import Path
import hashlib,json,re,struct
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(out,stages):
    mem=Memory(ROOT/'recovered/main');source=ROOT/'recovered/main/disassembly.txt';lines=[];count=0
    for line in source.open():
        m=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if not m:continue
        at=int(m[1],16)
        if not (0x6f3530<=at<0x6f3b2c or 0x6eead0<=at<0x6eeaf1 or 0x706270<=at<0x7062e2):continue
        raw=bytes.fromhex(m[2]);assert mem.read(at,len(raw))==raw;lines.append(line);count+=1
    target=ROOT/'logs/normal-level-doors-reference.txt';target.write_text(''.join(lines),encoding='ascii')
    table=struct.unpack('<12I',mem.read(0x6f3a94,48))
    assert table==(0x6f36d7,0x6f372c,0x6f37a7,0x6f381e,0x6f37a7,0x6f384a,0x6f372c,0x6f38b8,0x6f3876,0x6f3886,0x6f3896,0x6f38a8)
    report=dict(format='normal-level-doors-reference-v1',instructions=count,source_module_sha256=next(m['sha256'] for m in json.loads((ROOT/'evidence/modules.json').read_text()) if m['id']=='main'),
        saved_listing=dict(path=source.relative_to(ROOT).as_posix(),sha256=sha(source)),bounded_instructions=dict(path=target.relative_to(ROOT).as_posix(),sha256=sha(target)),
        switch_table=dict(address='006f3a94',destinations=[f'{p:08x}' for p in table]),
        owner='No RNG in006f3ad0/006f3530. One layout advance follows at006fa670..006fa6c4.',
        field_identity='Descriptor+3c display/visited visibility flags; type29 writes unresolved descriptor+B4=99. These are distinct.',
        scope='Instruction/data arithmetic fixtures, not original recorded execution. Interactive door actor rules and room activation remain separate.')
    (ROOT/'runtime/port/normal_level_doors_reference.json').write_text(json.dumps(report,indent=2)+'\n')
    allrows=[r for rs in stages.values() for r in rs];defs={r['id']:r for r in allrows};choices={}
    for shape in range(1,13):
        rs=[r for r in allrows if r['shape']==shape]
        choices[shape]=list({r['id']:r for r in [min(rs,key=lambda r:r['mask']),max(rs,key=lambda r:r['mask'])]}.values())
    def cells(d,grid):
        x,y=grid%13,grid//13;w,h=d['width']//13,d['height']//7
        coords=[(x-1,y),(x,y-1),(x+w,y),(x,y+h),(x-1,y+1),(x+1,y-1),(x+w,y+1),(x+1,y+h)]
        if d['shape']==9:coords[0]=coords[1]=(x,y)
        if d['shape']==10:coords[2]=coords[5]=(x+1,y)
        if d['shape']==11:coords[3]=coords[4]=(x,y+1)
        if d['shape']==12:coords[6]=coords[7]=(x+1,y+1)
        disabled={1:{4,5,6,7},2:{1,3,4,5,6,7},3:{0,2,4,5,6,7},4:{5,7},5:{0,2,4,5,6,7},6:{4,6},7:{1,3,4,5,6,7}}.get(d['shape'],set())
        return [px+py*13 if 0<=px<13 and 0<=py<13 and (s not in disabled or d['mask']&(1<<s)) else -1 for s,(px,py) in enumerate(coords)]
    def footprint(d,grid):
        w,h=(1 if d['shape'] in (1,2,3,4,5) else 2),(1 if d['shape'] in (1,2,3,6,7) else 2)
        miss={9:(0,0),10:(1,0),11:(0,1),12:(1,1)}.get(d['shape'])
        return {grid+x+y*13 for y in range(h) for x in range(w) if (x,y)!=miss}
    packets=[]
    for a in range(1,13):
     for b in range(1,13):
      for source in choices[a]:
       for dest in choices[b]:
        for offset in (-2,-1,1,2,-26,-13,13,26,-25,-12,14,27):
            origins=[70,70+offset];occupied=[footprint(d,g) for d,g in zip([source,dest],origins)]
            if occupied[0]&occupied[1]:continue
            owner={cell:i for i,c in enumerate(occupied) for cell in c};data=[source,dest]
            for init in (0,255):
                doors=[init,init];targets=[cells(d,g) for d,g in zip(data,origins)];slots=[[-1]*8,[-1]*8]
                for i in range(2):
                    for s,cell in enumerate(targets[i]):
                        if cell<0:continue
                        j=owner.get(cell);reverse=-1
                        if j is not None:
                            delta=cell-origins[j];reverse=(s+2)%4
                            if (reverse%2 and delta%13>0) or (reverse%2==0 and delta//13>0):reverse+=4
                            slots[i][s]=reverse
                            if data[i]['mask']&(1<<s) and data[j]['mask']&(1<<reverse):doors[i]|=1<<s;doors[j]|=1<<reverse;continue
                        doors[i]&=~(1<<s)
                        if j is not None:doors[j]&=~(1<<reverse)
                packets.append([source['id'],origins[0],dest['id'],origins[1],len(packets)%3,init,init]+doors+targets[0]+slots[0]+targets[1]+slots[1])
    (out/'level_doors.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in packets),encoding='ascii')
    print('Level door reference:',count,'verified instructions,',len(packets),'independent pair fixtures.')
    return len(packets)

"""Pin native room boundaries, clamping/wrapping and collision query references."""
from pathlib import Path
import hashlib,json,re,struct
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def f32(n):return struct.unpack('<f',struct.pack('<f',n))[0]
def bits(n):return struct.unpack('<I',struct.pack('<f',n))[0]
def value(n):return struct.unpack('<f',struct.pack('<I',n))[0]
def add(a,b):return f32(a+b)
def sub(a,b):return f32(a-b)

def constants():
    memory=Memory(ROOT/'recovered/main')
    table=memory.words(0xba1e60,13*8)
    special=[struct.unpack('<I',memory.read(at,4))[0] for at in (0x79fdca,0x79fdd1,0x79fdd8,0x79fddf)]
    assert special==[0xc244ec50,0x42909d88,0x442c4ec5,0x43f3d89e]
    assert struct.unpack('<3f',memory.read(0xb49bd8,4)+memory.read(0xb49cbc,4)+memory.read(0xb49b64,4))==(40.,120.,20.)
    return memory,table,special

def prepare_constants():
    _,table,special=constants();out=ROOT/'build/generated';out.mkdir(parents=True,exist_ok=True)
    lines=['//Hash-verified saved00ba1e60 table; see normal_room_space_reference.json.','static const int kNativeRoomSpaceCells[13][8]={']
    lines+=['{'+','.join(map(str,table[i:i+8]))+'},' for i in range(0,len(table),8)]
    lines+=['};','static const unsigned int kNativeSpecialRoomBounds[4]={'+','.join('0x%08xU'%n for n in special)+'};']
    (out/'native_room_space_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')

def boundaries(shape,stage,typ,table,special):
    #0079fda0(true) and007c9420 coordinate outputs, before caller margins.
    w=15 if shape<=5 else 28;h=9 if shape<=3 or shape in (6,7) else 16
    outer=[60.,140.,float((w-2)*40+60),float((h-2)*40+140)]
    t=table[shape*8:shape*8+8]
    rects=[float(n*40+offset) for n,offset in zip(t,(20,100,60,140)*2)]
    if shape in (2,3,5,7):outer=[rects[i] for i in (0,1,6,7)]
    if stage==35 and typ==16:outer=list(map(value,special))
    if shape<9:rects=outer*2
    return outer,rects

def clamp(shape,p,m,b,r):
    #Scalar registers/branch targets of007bd810. This expectation does not
    #import the generated constants header or use the maintained C++ service.
    left,top,right,bottom=m
    xmm4=add(left,b[0]);low_y=add(top,b[1]);high_x=sub(b[2],right);high_y=sub(b[3],bottom)
    x=max(xmm4,min(high_x,p[0]));y=max(low_y,min(high_y,p[1]))
    if shape<9:return [x,y]
    xmm6=add(r[0],left);xmm7=sub(r[2],right);xmm3=add(r[4],left);xmm5=sub(r[6],right)
    xmm2=add(r[5],top);saved08=sub(r[3],bottom)
    if sub(xmm5,xmm3)<=sub(xmm7,xmm6):xmm2=saved08
    pc={9:'98e',10:'9d2',11:'a12',12:'a30'}[shape]
    if pc=='98e':
        if xmm6>x and xmm2>y:
            if sub(xmm2,y)>sub(xmm6,x):return [xmm6,y]
            return [x,xmm2]
    elif pc=='9d2':
        if x>xmm7 and xmm2>y:
            if sub(xmm2,y)>sub(x,xmm7):return [xmm7,y]
            return [x,xmm2]
    elif pc=='a12':
        if xmm3>x and y>xmm2:
            if sub(y,xmm2)>sub(xmm3,x):return [xmm3,y]
            return [x,xmm2]
    else:
        if x>xmm5 and y>xmm2:
            if sub(y,xmm2)>sub(x,xmm5):return [xmm5,y]
            return [x,xmm2]
    return [x,y]

def wrap(p,m,b,q):
    #007bdb20 selects its axis from the delta; it does not clamp again.
    if p==q:return p
    dx=sub(q[0],p[0]);dy=sub(q[1],p[1]);horizontal=False
    if dy==0:horizontal=True
    elif dx!=0 and abs(dy)>abs(dx):horizontal=True
    x,y=p
    if horizontal:
        width=sub(sub(b[2],m[2]),add(b[0],m[0]))
        if q[0]>p[0]:x=add(x,width)
        elif p[0]>q[0]:x=sub(x,width)
    else:
        height=sub(sub(b[3],m[3]),add(b[1],m[1]))
        if q[1]>p[1]:y=add(y,height)
        elif p[1]>q[1]:y=sub(y,height)
    return [x,y]

def prepare(out):
    memory,table,special=constants();listing=ROOT/'recovered/main/disassembly.txt'
    spans=[(0x79fda0,0x7a01b0),(0x7bd810,0x7bdd58),(0x7c9420,0x7c964a),(0x79e1f0,0x79e25b),(0x7bd4d0,0x7bd540),(0x7a10cf,0x7a10e2),(0x79c94f,0x79c97d)]
    instructions=[]
    for line in listing.open():
        m=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if m and any(a<=int(m[1],16)<b for a,b in spans):
            at=int(m[1],16);raw=bytes.fromhex(m[2]);assert memory.read(at,len(raw))==raw;instructions.append(line)
    pinned=ROOT/'evidence/reference_instructions/room-space.txt';pinned.write_text(''.join(instructions),encoding='ascii')
    rows=[]
    margins=[(0,0,0,0),(10,10,10,10),(26,26,26,26),(-20,-20,-20,-20),(600,600,600,600),(0,10,20,30),(-20,40,1,26),(260,0,261,0)]
    for shape in range(1,13):
        for stage,typ in ((1,1),(35,16)):
            b,r=boundaries(shape,stage,typ,table,special)
            #Grid centers, external points, each boundary's adjacent float,
            #internal L corners and equal-distance ties all have literal rows.
            w=15 if shape<=5 else 28;h=9 if shape<=3 or shape in (6,7) else 16
            points={(float(x*40+40),float(y*40+120)) for y in range(h) for x in range(w)}
            for x in (b[0],b[2],r[0],r[2],r[4],r[6]):
                for y in (b[1],b[3],r[1],r[3],r[5],r[7]):
                    for axis in (0,1):
                        for delta in (-1,0,1):
                            p=[x,y];p[axis]=value(bits(p[axis])+delta);points.add(tuple(p))
            points|={(-400.,-400.),(2000.,1400.),(320.,280.),(580.,420.),(560.,400.),(600.,440.)}
            for p in sorted(points):
                for m in margins:
                    m=tuple(map(float,m));q=clamp(shape,p,m,b,r);wrapped=wrap(p,m,b,q)
                    margin=m[0];inside=clamp(shape,p,(margin,)*4,b,r)==list(p) if shape>=9 else add(b[0],margin)<=p[0]<=sub(b[2],margin) and add(b[1],margin)<=p[1]<=sub(b[3],margin)
                    rows.append([shape,stage,typ]+list(map(bits,p+m))+list(map(bits,b+r+q+wrapped))+[int(inside)])
    (out/'room_space.tsv').write_text(''.join('\t'.join(map(str,row))+'\n' for row in rows),encoding='ascii')
    collisions=[]
    for cell in (-999,-1,0,67,134,447,448,1000):
      for zero in (0,1):
       for occupied in (0,1):
        for collision in (-1,0,1,2,3,4,99):
         for cost in (-2147483648,-1,0,999,1000,2999,3000,3999,4000,2147483647):
            ok=zero or cell<448;result=31337
            if zero:result=0
            elif cell<0:result=4
            elif cell<448:
                if occupied:result=collision
                elif cost>=3000:result=1 if cost<=3999 else 3
                else:result=0 if cost<1000 else 3
            collisions.append([cell,zero,occupied,collision,cost,int(bool(ok)),result])
    (out/'room_collision.tsv').write_text(''.join('\t'.join(map(str,row))+'\n' for row in collisions),encoding='ascii')
    refreshed=[]
    for shape in range(1,13):
      w=15 if shape<=5 else 28;h=9 if shape<=3 or shape in (6,7) else 16
      initial=[1,1,w-2,h-2]
      if shape in (2,3,5,7):initial=[table[shape*8+i] for i in (0,1,6,7)]
      x0,y0,x1,y1=initial
      for stage,typ in ((1,1),(35,16)):
       for pattern in range(16):
        cells=[0]*448
        for y in range(h):
         for x in range(w):
            k=y*w+x
            if pattern==1:cells[k]=15
            elif pattern==2 and x<x0+2:cells[k]=15
            elif pattern==3 and x>x1-2:cells[k]=16
            elif pattern==4 and y<y0+2:cells[k]=15
            elif pattern==5 and y>y1-2:cells[k]=16
            elif pattern==6 and (x in (x0,x1) or y in (y0,y1)):cells[k]=15
            elif pattern==7 and (x==x0 or y==y0):cells[k]=15 if x==x0 else 16
            elif pattern==8 and (x,y)!=((x0+x1)//2,(y0+y1)//2):cells[k]=15
            elif pattern==9:cells[k]=15+(k&1)
            elif pattern==10:cells[k]=15 if k&1 else 1
            elif pattern==11:cells[k]=1
            elif pattern==12 and (y==y1 or x==x0 and y&1):cells[k]=16
            elif pattern==13 and not(x0<=x<=x1 and y0<=y<=y1):cells[k]=15
            elif pattern==14:cells[k]=(0,1,15,16,17)[((k*0x45d9f3b)^shape)%5]
            elif pattern==15 and (x<x0+2 or x>x1-2 or y<y0+2 or y>y1-2):cells[k]=15+(k&1)
        #Independent row/column reductions of the saved false-branch scan.
        open_cols=[x for x in range(x0,x1+1) if any(cells[y*w+x] not in (15,16) for y in range(y0,y1+1))]
        open_rows=[y for y in range(y0,y1+1) if any(cells[y*w+x] not in (15,16) for x in range(x0,x1+1))]
        low_x,high_x=(open_cols[0],open_cols[-1]) if open_cols else (x1+1,x0-1)
        low_y,high_y=(open_rows[0],open_rows[-1]) if open_rows else (y1+1,y0-1)
        b=[float(low_x*40+20),float(low_y*40+100),float(high_x*40+60),float(high_y*40+140)]
        if stage==35 and typ==16:b=list(map(value,special))
        refreshed.append((shape,stage,typ,bytes(cells),list(map(bits,b))))
    with (out/'room_bounds.bin').open('wb') as f:
        f.write(b'NRBOUND1');f.write(struct.pack('<I',len(refreshed)))
        for shape,stage,typ,cells,b in refreshed:f.write(struct.pack('<III',shape,stage,typ));f.write(cells);f.write(struct.pack('<4I',*b))
    report=dict(format='normal-room-space-reference-v1',instructions=dict(path=pinned.relative_to(ROOT).as_posix(),sha256=sha(pinned),count=len(instructions)),
        constants=dict(shape_table_address='00ba1e60',shape_cells=[list(table[i:i+8]) for i in range(0,len(table),8)],special_bounds=[f'{n:08x}' for n in special]),
        cases=len(rows),collision_cases=len(collisions),bounds_refresh_cases=len(refreshed),source_module_sha256=next(e['sha256'] for e in json.loads((ROOT/'evidence/modules.json').read_text()) if e['id']=='main'),
        scope='Initial physical bounds and occupied-grid type15/16 bounds refresh, L rectangles, scalar clamping/wrapping/containment and numeric collision lookup. Separate data/instruction arithmetic expectations, not original execution traces. Entity movement, swept collision, physical grid factories and final door collision remain open.')
    path=ROOT/'runtime/port/normal_room_space_reference.json';path.write_text(json.dumps(report,indent=2)+'\n')
    print('Room space:',len(rows),'geometry/float cases;',len(collisions),'collision lookup cases.')
    return ['tools/prepare_room_space.py',path.relative_to(ROOT).as_posix(),pinned.relative_to(ROOT).as_posix()]

"""Focused action sampling/vector expectations from saved player input code."""
from pathlib import Path
import hashlib,itertools,json,math,re,struct
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def f32(n):return struct.unpack('<f',struct.pack('<f',n))[0]
def bits(n):return struct.unpack('<I',struct.pack('<f',n))[0]
def add(a,b):return f32(a+b)
def sub(a,b):return f32(a-b)
def mul(a,b):return f32(a*b)
def fold(h,n):
    for b in struct.pack('<I',n&0xffffffff):h=((h^b)*16777619)&0xffffffff
    return h

def expected(c,actions,drop,shooting):
    trace=[]
    def query(action,kind=0):
        trace.append((kind,action,c[1],c[2]))
        return drop if kind else actions[action]
    def reflect(x):
        return mul(x,-1) if c[13]==1 and c[12] in (4,5) and (c[11]==2 or c[11]==1 and c[14]&2) else x
    def cardinal(x,y,move):
        n=add(mul(y,y),mul(x,x)) if move else add(mul(x,x),mul(y,y))
        magnitude=f32(math.sqrt(n))
        #Native equality selects the vertical axis, even at the zero vector.
        axis=0 if abs(x)>abs(y) else 1
        direction=-1. if (x,y)[axis]<0 else 1.
        return (mul(direction,magnitude),mul(0.,magnitude)) if axis==0 else (mul(0.,magnitude),mul(direction,magnitude))
    def movement():
        pc='52d'
        if c[0]==20:pc='56e'
        elif c[3] and c[4]==1:pc='56e'
        elif c[0] in (29,38) and c[5] and c[8]<c[7] and not c[6]:pc='56e'
        if pc=='56e' and c[9] and c[10]<=0 and query(11,1):return (0.,0.)
        x=sub(0.,query(0));x=add(query(1),x)
        y=sub(0.,query(2));y=add(query(3),y)
        if c[15]:x=mul(x,-1);y=mul(y,-1)
        if c[18]:x,y=cardinal(x,y,True)
        return reflect(x),y
    if not shooting:result=movement()
    else:
        if c[16]:x,y=movement()
        elif c[17]:
            x,y=movement();x=mul(x,-1);y=mul(y,-1)
        else:
            x=sub(0.,query(4));x=add(query(5),x)
            y=sub(0.,query(6));y=add(query(7),y)
            if c[15]:x=mul(x,-1);y=mul(y,-1)
        if c[18]:x,y=cardinal(x,y,False)
        result=reflect(x),y
    h=2166136261
    for record in trace:
        for n in record:h=fold(h,n)
    return list(map(bits,result))+[len(trace),h]

def prepare(out):
    memory=Memory(ROOT/'recovered/main');listing=ROOT/'recovered/main/disassembly.txt'
    spans=[(0x729520,0x729799),(0x7297a0,0x729a5f),(0x423db0,0x423ddc),(0x7f844a,0x7f845e),(0x72aeeb,0x72af8c)]
    lines=[]
    for line in listing.open():
        m=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if m and any(a<=int(m[1],16)<b for a,b in spans):
            at=int(m[1],16);raw=bytes.fromhex(m[2]);assert memory.read(at,len(raw))==raw;lines.append(line)
    pinned=ROOT/'evidence/reference_instructions/player-input.txt';pinned.write_text(''.join(lines),encoding='ascii')
    for at,n in ((0xb49fd0,-1.),(0xb497b4,1.),(0xbcf6a8,0.),(0xbcf6ac,0.)):
        assert struct.unpack('<f',memory.read(at,4))[0]==n
    patterns=[(0,0,0,0,0,0,0,0),(0,1,0,0,1,0,0,0),(1,0,0,1,0,1,1,0),(0,1,0,1,1,0,1,0),
        (0,.6,0,.8,.3,.9,.8,.2),(.5,.5,.5,.5,0,0,0,0),(.75,.25,.25,.75,.125,.375,.875,.625),
        (1,1,0,1,0,0,1,1),(-0.,0.,-0.,0.,-0.,0.,-0.,0.),(0,.5,0,.5,0,.8,0,.8),
        (0,.50000006,0,.5,0,.5,0,.50000006),(1,0,0,0,0,0,0,1),(0,0,0,1,0,1,0,0),
        (0,.000001,0,.000001,0,.99999994,0,1),(0,1,1,0,1,0,0,1),(.1,.9,.9,.1,.2,.8,.1,.9)]
    patterns=[tuple(map(f32,p)) for p in patterns]
    rows=[]
    def row(c,pattern,drop):
        actions=patterns[pattern%len(patterns)];move=expected(c,actions,drop,False);shoot=expected(c,actions,drop,True)
        #19 context fields,8 action bits,drop, two vectors and two trace pairs.
        rows.append(c+list(map(bits,actions))+[int(drop)]+move[:2]+shoot[:2]+move[2:]+shoot[2:])
    mirror=list(itertools.product((0,1,2,3),(0,4,5),(0,1,2),(0,2)))
    for player in range(41):
      for case in range(64):
       for effects in range(16):
        stage,typ,dim,flags=mirror[(case+player)%len(mirror)]
        n=(case*0x45d9f3b+player*0x119de1f3+effects*0x9e3779b9)&0xffffffff
        c=[player,(case%5)-1,0x81234000+player,int(bool(case&1)),1 if case&2 else 5,int(bool(case&4)),int(bool(case&8)),
            -2 if case&16 else 2,-3 if case&16 else 3,int(bool(case&32)),(-1,0,1)[case%3],stage,typ,dim,flags]
        c+=[int(bool(effects&(1<<i))) for i in range(4)]
        row(c,case+effects+player,bool(n&0x40))
    #Uncorrelated drop/pair gates and reflection cases with literal evaluated
    #inputs. No character unlock, seed-effect activation or device policy.
    for player in (0,20,29,38):
     for enabled,cooldown,drop in itertools.product((0,1),(-1,0,1),(0,1)):
      for stage,typ,dim,flags in mirror:
       c=[player,3,0xf1234567,0,0,1,0,7,6,enabled,cooldown,stage,typ,dim,flags,0,1,0,1]
       row(c,4,drop)
    path=out/'player_input.bin'
    with path.open('wb') as f:
        f.write(b'NPIREF1\0');f.write(struct.pack('<II',len(rows),36))
        for r in rows:assert len(r)==36;f.write(struct.pack('<36I',*(n&0xffffffff for n in r)))
    sources=[dict(path='recovered/main/'+r['file'],sha256=r['sha256']) for _,_,r in memory.blocks]
    report=dict(format='normal-player-input-reference-v1',cases=len(rows),instructions=dict(path=pinned.relative_to(ROOT).as_posix(),sha256=sha(pinned),count=len(lines)),
        source_module_sha256=next(e['sha256'] for e in json.loads((ROOT/'evidence/modules.json').read_text()) if e['id']=='main'),memory_sources=sources,
        actions=dict(movement=[0,1,2,3],shooting=[4,5,6,7],paired_drop=11),seed_effect_ids=[12,74,75,76],
        sqrt='Rounded scalar sum, convert to double, SQRTSD, round to float at00423db0.',
        scope='Native movement/shooting input getters over explicit evaluated player/level fields and resolved action values. Separate scalar/branch expectations and query traces, not recorded original gameplay. Device mapping/deadzones, action callbacks, full entity initialization, movement integration, weapon firing and complete play remain open.')
    ref=ROOT/'runtime/port/normal_player_input_reference.json';ref.write_text(json.dumps(report,indent=2)+'\n')
    print('Player input:',len(rows),'movement/shooting/owner/query-order cases.')
    return ['tools/prepare_player_input.py',ref.relative_to(ROOT).as_posix(),pinned.relative_to(ROOT).as_posix(),'recovered/main/memory_blocks.jsonl']+[e['path'] for e in sources]

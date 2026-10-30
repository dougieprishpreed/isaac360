"""Bounded saved-instruction velocity expectations; no original execution."""
from pathlib import Path
import hashlib,json,math,re,struct
from reference_memory import Memory
from prepare_player_input import f32,bits,add,sub,mul,fold
ROOT=Path(__file__).resolve().parents[1]
CONSTANTS={
 '005':(0xb4940c,0x3d4ccccd),'0185':(0xb494d4,0x3e3d70a4),
 '020Below':(0xb494e8,0x3e4ccccc),'020':(0xb494ec,0x3e4ccccd),
 '0225':(0xb4951c,0x3e666666),'025':(0xb49528,0x3e800000),
 '030':(0xb4954c,0x3e99999a),'040':(0xb495d0,0x3ecccccd),
 '045':(0xb49600,0x3ee66666),'050':(0xb49618,0x3f000000),
 '051':(0xb49624,0x3f028f5c),'060':(0xb49660,0x3f19999a),
 '070':(0xb496ac,0x3f333333),'075':(0xb496dc,0x3f400000),
 '0775':(0xb496e4,0x3f466666),'080':(0xb49700,0x3f4ccccd),
 '097':(0xb49798,0x3f7851ec),'3':(0xb499fc,0x40400000),
 '441':(0xb49a7c,0x408d1eb8),'Degrees':(0xb49c20,0x42652ee0),
 '60':(0xb49c28,0x42700000),'180':(0xb49d24,0x43340000),
 '360':(0xb49dd0,0x43b40000)}
K={k:struct.unpack('<f',struct.pack('<I',v[1]))[0] for k,v in CONSTANTS.items()}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def div(a,b):return f32(a/b)
def length(p):return f32(math.sqrt(add(mul(p[0],p[0]),mul(p[1],p[1]))))
def scale(p,k):return [mul(a,k) for a in p]
def plus(p,q):return [add(a,b) for a,b in zip(p,q)]
def minus(p,q):return [sub(a,b) for a,b in zip(p,q)]
def unit(p):
    n=length(p)
    return scale(p,div(1.,n)) if n>0 else p[:]
def signed(n):return (n+0x80000000)%0x100000000-0x80000000
def angle(a):
    if abs(a)>K['360']:a=f32(math.fmod(a,K['360']))
    if a>K['180']:a=sub(a,K['360'])
    elif a<-K['180']:a=add(a,K['360'])
    return a
def prepare_constants():
    (ROOT/'build/generated').mkdir(parents=True,exist_ok=True)
    m=Memory(ROOT/'recovered/main');lines=['//Hash-verified saved scalar constants; normal_player_motion_reference.json.']
    for key,(at,n) in CONSTANTS.items():
        assert struct.unpack('<I',m.read(at,4))[0]==n,(key,hex(at))
        lines.append('static const unsigned int kMotion%s=0x%08xU;'%(key,n))
    (ROOT/'build/generated/native_player_motion_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')

class Missing(Exception):
    def __init__(self,pc,effect=-1):self.pc,self.effect=pc,effect
class Tail(Exception):
    def __init__(self,pc,effect):self.pc,self.effect=pc,effect

def evaluate(c,t,e,s,actions,drops,known,effectmask):
    """Separate scalar state transition following saved PCs; floats round per op."""
    before=s[:];trace=[];pc=0;missing=-1
    def query(kind,n,ctx):
        trace.append((kind,n,0 if kind in (2,3) else ctx[1],ctx[2]))
    def movement(ctx,values,drop):
        paired=ctx[0]==20 or ctx[3] and ctx[4]==1 or ctx[0] in (29,38) and ctx[5] and not ctx[6] and ctx[8]<ctx[7]
        if paired and ctx[9] and ctx[10]<=0:
            query(1,11,ctx)
            if drop:return [0.,0.]
        for a in range(4):query(0,a,ctx)
        p=[add(sub(0.,values[0]),values[1]),add(sub(0.,values[2]),values[3])]
        if ctx[15]:p=scale(p,-1.)
        if ctx[18]:
            n=f32(math.sqrt(add(mul(p[1],p[1]),mul(p[0],p[0]))))
            axis=int(abs(p[0])<=abs(p[1]));direction=-1. if p[axis]<0 else 1.
            p=[mul(0.,n),mul(0.,n)];p[axis]=mul(direction,n)
        if ctx[13]==1 and ctx[12] in (4,5) and (ctx[11]==2 or ctx[11]==1 and ctx[14]&2):p[0]=mul(p[0],-1.)
        return p
    def effect(n,pc):
        query(2,n,c);flag=1 if n==63 else 2 if n==50 else 0
        if not flag:return False
        if not known&flag:raise Missing(pc,n)
        return bool(effectmask&flag)
    def collectible(n,pc):
        #The saved velocity packet carries no collectible inventory. Every
        #query is an explicit absent owner; the action/cache trace still
        #records the native query position.
        query(3,n,c);return False
    #e: twinKnown,flagsLow,flagsHigh,1d78,2a0,speed,2ec,1580,1798,1e24,
    #14a4,1ef4,1f80,targetPresent,posXY,targetXY,188,18cXY,194XY.
    try:
        if e[3]<0 or e[4]<0:result,pc=0,0x72c3c4
        elif e[13]:s[:2]=minus(e[16:18],e[14:16]);result,pc=0,0x72c3c4
        else:
            s[2:4]=s[:2];p=[0.,0.];horizontal=bool(e[1]&0x8000 and not e[10]);special=c[0]==35 and not e[12]
            if c[9] and c[10]<=0:
                p=movement(c,actions[0],drops[0])
                if horizontal:p[1]=0.
                if length(p)>1:p=unit(p)
            if special or e[9] or e[11]:p=[0.,0.]
            if c[0] in (19,20,29,38) and c[5] or c[0]==26:
                twin=bool(c[5] and c[8]<c[7])
                if twin and not e[0]:raise Missing(0x72b012)
                q=movement(t if twin else c,actions[int(twin)],drops[int(twin)])
                if any(q):s[10]=0
            active63=effect(63,0x72b054)
            if active63:
                if length(p)>.25:
                    if s[11]<=0:
                        q=unit(p);s[:2]=plus(s[:2],scale(plus(q,q),e[6]));s[11]=5
                    else:s[11]=max(s[11]-1,1)
                else:s[11]=signed(s[11]-1)
                q=[0.,0.]
            else:q=p[:];s[11]=0
            if e[8]>0:
                old=s[4:6];axis=int(abs(old[0])<=abs(old[1]));card=[0.,0.];card[axis]=-1. if old[axis]<0 else 1.
                q=plus(q,scale(minus(card,q),K['060']))
            s[4:6]=q
            if any(q):s[6:8]=q
            q=scale(q,add(mul(e[5],K['025']),K['075']))
            s[8]=(0 if q[0]<0 else 2) if abs(q[0])>abs(q[1]) else (1 if q[1]<0 else 3 if q[1]>0 else -1)
            if e[9]==1:s[8]=(0 if s[0]<0 else 2) if abs(s[0])>abs(s[1]) else (1 if s[1]<0 else 3)
            if s[8]!=-1:
                a,b=angle(s[9]),angle(mul(f32(math.atan2(q[1],q[0])),K['Degrees']))
                if abs(sub(b,a))>K['180']:
                    if b>a:a=add(a,K['360'])
                    else:b=add(b,K['360'])
                s[9]=angle(add(mul(sub(b,a),K['030']),a))
            factor=div(sub(f32(e[7]),0.),K['60']);previous=s[:2]
            if special:
                damping=1. if e[1]&0x4000 else K['097'] if e[2]&0x04000000 else add(mul(factor,K['051']),K['045'])
            else:
                if e[1]&0x80:factor=0.
                if any(q) and not horizontal:
                    dot=add(mul(s[0],q[0]),mul(s[1],q[1]));projection=scale(q,dot)
                    root=f32(math.sqrt(add(mul(factor,K['020Below']),K['080'])))
                    s[:2]=plus(scale(minus(s[:2],projection),root),projection)
                previous=s[:2];dot=add(mul(s[0],q[0]),mul(s[1],q[1]))
                positive=max(0.,mul(sub(mul(factor,K['070']),K['020']),dot))
                amount=mul(add(div(positive,e[5]),K['3']),K['040'])
                base=1.
                if not e[2]&0x04000000:
                    alt=effect(63,0x72b4e0) or e[9]>1 or special
                    base=add(mul(factor,K['051' if alt else '0185']),K['045' if alt else '0775'])
                damping=f32(math.sqrt(base))
                accel=add(mul(sub(mul(amount,K['025']),amount),factor),amount)
                s[:2]=plus(s[:2],scale(scale(q,K['050']),accel))
                cap=mul(e[5],K['441'])
                if factor>0 and add(mul(s[0],s[0]),mul(s[1],s[1]))>mul(cap,cap):
                    n=length(s[:2])
                    if n>0:s[:2]=scale(s[:2],div(length(previous),n))
            s[:2]=[mul(s[0],damping),add(s[1],K['0225']) if horizontal else mul(damping,s[1])]
            if effect(50,0x72b661):
                s[:2]=plus(s[:2],scale(previous,K['005']))
                if e[18]:
                    dot=add(mul(e[19],e[21]),mul(e[20],e[22]));correction=scale(scale(e[21:23],dot),K['080'])
                    s[:2]=minus(e[19:21],plus(correction,correction))
            if not e[1]&0x4000:
                for ident,at in ((93,0x72b734),(77,0x72b800),(298,0x72b8a0),(299,0x72b951),(58,0x72b9f9),(313,0x72bab4)):
                    if effect(ident,at):raise Tail(at,ident)
                if collectible(594,0x72bb4d):raise Tail(0x72bb5a,594)
                s[12]=0
            if collectible(593,0x72c0ff):raise Tail(0x72c11e,593)
            elif collectible(705,0x72c111):raise Tail(0x72c11e,593)
            s[13]=s[13]+1 if s[13]<10000 else 10000
            result,pc=0,0x72c3c4
        source=0
    except Missing as exc:result,pc,missing,source=3,exc.pc,exc.effect,2;s=before
    except Tail as exc:result,pc,missing,source=1,exc.pc,exc.effect,0
    h=2166136261
    for record in trace:
        for n in record:h=fold(h,n)
    return s,[result,pc,missing,source,len(trace),h]

def encode_state(s):return [bits(n) if i<8 or i==9 else n for i,n in enumerate(s)]
def encode_extra(e):return [bits(n) if i in (3,4,5,6,14,15,16,17,19,20,21,22) else n for i,n in enumerate(e)]
def prepare(out):
    prepare_constants();m=Memory(ROOT/'recovered/main')
    spans=[(0x72ae20,0x72b71c),(0x72c3c4,0x72c3df),(0x647d90,0x647dec),(0x6722a0,0x6722ea),(0x6491a0,0x649301),
        (0x94ef50,0x94efb8),(0x94efd0,0x94f045),(0x94f2d0,0x94f337),(0x447920,0x447959),(0x4479e0,0x447a19),(0x423db0,0x423ddc),(0x413c30,0x413c4c),(0x9ce8c4,0x9ce8d0)]
    for at,value in ((0xb497b4,1.),(0xb49320,0.),(0xbcf6a8,0.),(0xbcf6ac,0.),(0xb4a104,-180.)):
        assert struct.unpack('<f',m.read(at,4))[0]==value
    assert struct.unpack('<I',m.read(0xb4ba80,4))[0]==0x7fffffff
    imports=[json.loads(line) for line in (ROOT/'recovered/main/symbols.jsonl').open() if '::_CIatan2"' in line or '::_CIfmod"' in line]
    assert len(imports)==4 and sum(e['source']=='IMPORTED' for e in imports)==2
    assert {e['address'] for e in imports if not e['external']}=={'009ce8c4','009ce8ca'}
    lines=[]
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        hit=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if hit and any(a<=int(hit[1],16)<b for a,b in spans):
            raw=bytes.fromhex(hit[2]);assert m.read(int(hit[1],16),len(raw))==raw;lines.append(line)
    pinned=ROOT/'evidence/reference_instructions/player-motion.txt';pinned.write_text(''.join(lines),encoding='ascii')
    rows=[];sequences=0
    float_states=(0.,.125,.6,1.,-1.,3.,-4.25,17.,-13.)
    patterns=[(0.,0.,0.,0.),(0.,1.,0.,0.),(0.,1.,0.,1.),(1.,0.,1.,0.),(.2,.8,.9,.1),(0.,.1,0.,.2),(0.,.25,0.,0.),(0.,.25000003,0.,0.)]
    def row(c,t,e,s,acts,drops,known,mask):
        #The packet transports float fields. Round inputs before evaluating,
        #not only when encoding the expected result; Python literals are double.
        e=[f32(n) if i in (3,4,5,6,14,15,16,17,19,20,21,22) else n for i,n in enumerate(e)]
        s=[f32(n) if i<8 or i==9 else n for i,n in enumerate(s)]
        acts=[list(map(f32,a)) for a in acts]
        result,tail=evaluate(c,t,e,s[:],acts,drops,known,mask)
        rows.append(c+t+encode_extra(e)+encode_state(s)+list(map(bits,acts[0]))+[drops[0]]+list(map(bits,acts[1]))+[drops[1],known,mask]+tail[:4]+encode_state(result)+tail[4:])
        assert len(rows[-1])==107,len(rows[-1])
        return result
    for i in range(16384):
        n=(i*0x9e3779b9+0x1234567)&0xffffffff
        typ=(i//32)%41;controller=i%4;owner=0x81230000+controller
        c=[typ,controller,owner,int(bool(n&1)),1,int(bool(n&2)),0,7,6,int(bool(n&4)),(i%3)-1,1+(i%2),[0,4,5][i%3],(i//7)%3,(i//13)%4,
            int(bool(n&8)),0,0,int(bool(n&16))]
        t=c[:];t[0]=(typ+1)%41;t[1]=(controller+1)%4;t[2]=owner+16;t[5]=0;t[7]=6;t[15]=int(bool(n&32));t[18]=int(bool(n&64))
        flags=(0x80 if n&128 else 0)|(0x8000 if n&256 else 0)|(0x4000 if n&512 else 0)
        e=[int(i%19!=0),flags,0x04000000 if n&1024 else 0,0.,0.,[.5,1.,1.5,2.][i%4],f32(.7),[0,1,15,30,59,60,90][i%7],1 if n&2048 else 0,[0,0,0,1,2,-1][i%6],
            int(bool(n&4096)),int(i%23==0),int(bool(n&8192)),0,100.,200.,110.,170.,int(bool(n&16384)),1.5,-2.,.6,.8]
        e[21:23]=list(map(f32,e[21:23]))
        if i%97==0:e[3]=-1.
        if i%101==0:e[4]=-1.
        if i%103==0:e[13]=1
        s=[float_states[i%9],float_states[(i//9)%9],-8.,9.,float_states[(i//17)%9],float_states[(i//19)%9],1.,-1.,2,[-1080.,-181.,-180.,-90.,0.,90.,180.,181.,1080.][i%9],17,[-3,0,1,2,5,9][i%6],0,0]
        row(c,t,e,s,[patterns[i%8],patterns[(i//8)%8]],[int(i%11==0),int(i%13==0)],0 if i%127==0 else 1 if i%131==0 else 3,(i//3)%4)
    #Persist each owner's velocity/counters across unpaced frames; positions
    #are fixed inputs. This does not claim a full tick or a simulated game run.
    states=[[0.,0.,0.,0.,0.,0.,1.,0.,-1,0.,0,0,0,0] for _ in range(4)]
    for tick in range(512):
        for owner in range(4):
            c=[0,owner,100+owner,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0];t=c[:]
            e=[0,0,0,0.,0.,1.,f32(.7),0,0,0,0,0,0,0,100.,200.,100.,200.,0,0.,0.,0.,0.]
            states[owner]=row(c,t,e,states[owner],[patterns[(tick//32+owner)%8],patterns[0]],[0,0],3,1 if owner==1 else 2 if owner==2 else 0);sequences+=1
    path=out/'player_motion.bin'
    with path.open('wb') as f:
        f.write(b'NPVREF1\0');f.write(struct.pack('<II',len(rows),107))
        for r in rows:f.write(struct.pack('<107I',*(n&0xffffffff for n in r)))
    sources=[dict(path='recovered/main/'+r['file'],sha256=r['sha256']) for _,_,r in m.blocks]
    report=dict(format='normal-player-motion-reference-v1',cases=len(rows),sequence_frames=sequences,
        instructions=dict(path=pinned.relative_to(ROOT).as_posix(),sha256=sha(pinned),count=len(lines)),
        source_module_sha256=next(e['sha256'] for e in json.loads((ROOT/'evidence/modules.json').read_text()) if e['id']=='main'),memory_sources=sources,
        constants=[dict(name=k,address='%08x'%at,bits='%08x'%n) for k,(at,n) in CONSTANTS.items()],crt_imports=imports,
        scope='Saved instruction scalar/branch expectations and query traces for0072ae20 through0072c3be, including the0072b71c effect/collectible tail and actual early returns. Explicit evaluated fields/effect cache/action values; no recorded original gameplay. Player initialization, position integration, swept collision, firing and full play remain separate. Finite vectors and host equality are not original gameplay parity.',
        timing='No invented delta time or +1580 increment; raw evaluated field is divided by60 at0072b2b5.',
        math='Round each scalar operation to float; sqrt/atan2/fmod use double arguments then float result. Cross-CRT transcendental rounding and PowerPC require later target checks.')
    ref=ROOT/'runtime/port/normal_player_motion_reference.json';ref.write_text(json.dumps(report,indent=2)+'\n')
    print('Player velocity:',len(rows),'cases, including',sequences,'persistent owner frames.')
    return ['tools/prepare_player_motion.py','tools/prepare_player_input.py',ref.relative_to(ROOT).as_posix(),pinned.relative_to(ROOT).as_posix(),'recovered/main/memory_blocks.jsonl','recovered/main/symbols.jsonl']+[e['path'] for e in sources]

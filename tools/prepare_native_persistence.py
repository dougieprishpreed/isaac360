"""Export addressed native transfers, never a process-state/save fixture."""
from pathlib import Path
import hashlib,json,re
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
ALIASES={r:p for p,rs in [('EAX','AL AH AX'),('EBX','BL BH BX'),('ECX','CL CH CX'),('EDX','DL DH DX'),('ESI','SI'),('EDI','DI')] for r in rs.split()}
def root(r):return ALIASES.get(r,r)
def width(s):
    for k,n in [('xmmword ptr',16),('qword ptr',8),('dword ptr',4),('word ptr',2),('byte ptr',1)]:
        if k in s:return n
    return 16 if s.startswith('XMM') else 1 if s in ('AL','AH','BL','BH','CL','CH','DL','DH') else 2 if s in ALIASES else 4

def prepare():
    memory=Memory(ROOT/'recovered/main');rows=[];reset=[];checkpoint=[]
    spans=[(0x6b6ef0,0x6b731b),(0x6fca60,0x6fcdda),(0x6c2080,0x6c219f),
        (0x79ca10,0x79cb60),(0x79cb60,0x79d02b),(0x79d610,0x79da9c),
        (0x907350,0x9074a9),(0x6af060,0x6af0fb),(0x6aece0,0x6aee6e),
        (0x6eeb00,0x6eebaf),(0x90b390,0x90b478),(0x6b2ff0,0x6b3336),
        (0x924f30,0x924fe7),(0x6b3d20,0x6b3d38),(0x6b45b6,0x6b45e4),
        (0x6b0c42,0x6b0c53),(0x6b021f,0x6b02e7),(0x6ee9f6,0x6eea55),
        (0x6f9eb8,0x6f9f08),(0x6fa6c4,0x6fa6ca),(0x6faa61,0x6faaab),
        (0x86e42c,0x86e44b),(0x89b577,0x89b585),(0x6c5670,0x6c56e0),
        (0x6c5720,0x6c5752),(0x79c72a,0x79c7a5),(0x79c94f,0x79c9a6),
        (0x7971bf,0x797215),(0x797840,0x797850),(0x49ddd0,0x49ddd9),
        (0x6f4a67,0x6f4b0b),(0x8d8470,0x8d848b),(0x7d0270,0x7d03f5),
        (0x711da0,0x711e36),(0x77c543,0x77c562)]
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        x=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  (.*)',line)
        if x and 0x761de0<=int(x[1],16)<0x7634d1:
            pc=int(x[1],16);code=bytes.fromhex(x[2]);assert memory.read(pc,len(code))==code
            rows.append((pc,x[3].strip(),line))
        if x and 0x911da0<=int(x[1],16)<0x912bbe:
            pc=int(x[1],16);code=bytes.fromhex(x[2]);assert memory.read(pc,len(code))==code
            reset.append((pc,x[3].strip(),line))
        if x and any(lo<=int(x[1],16)<hi for lo,hi in spans):
            pc=int(x[1],16);code=bytes.fromhex(x[2]);assert memory.read(pc,len(code))==code;checkpoint.append(line)
    assert rows[-1][0]==0x7634ce and rows[-1][1]=='RET 0x4',(hex(rows[-1][0]),rows[-1][1])
    ptr={};values={};transfers=[]
    def kill(r):ptr.pop(root(r),None);values.pop(root(r),None)
    def address(s):
        x=re.search(r'\[([A-Z0-9]+)(?: \+ (0x[0-9a-f]+|-0x[0-9a-f]+))?\]',s)
        if x and x[1] in ptr:
            kind,offset=ptr[x[1]];return kind,offset+int(x[2] or '0',16)
    for pc,op,_ in rows:
        ins,_,args=op.partition(' ');args=args.split(',');a=args[0];b=args[1] if len(args)>1 else ''
        if ins=='CALL':
            for r in ['EAX','ECX','EDX']+['XMM%d'%i for i in range(6)]:kill(r)
        elif ins in ('MOV','MOVZX','MOVSX','MOVSS','MOVQ','MOVUPS','MOVAPS','LEA'):
            src=address(b);dst=address(a);value=values.get(root(b));pointer=ptr.get(b)
            if '[' not in a:
                kill(a)
                if pc==0x761e1b or (a=='EDI' and b=='dword ptr [EBP + -0x20]'):ptr[a]=('source',0)
                elif a=='ESI' and b=='dword ptr [EBX + 0x8]':ptr[a]=('destination',0)
                elif ins=='LEA' and src:ptr[a]=src
                elif ins!='LEA' and src and src[0]=='source':values[root(a)]=(src[1],width(b),ins=='MOVSX',pc)
                elif value:values[root(a)]=value
                elif pointer:ptr[a]=pointer
            elif dst and dst[0]=='destination' and value:
                off,n,sign,load=value;destwidth=width(a)
                #Partial aliases read only that lane; MOVZX/SX may widen.
                sourcewidth=min(n,width(b),destwidth)
                #7626f7 belongs to the non-null parent branch. This initial
                #player has no parent, leaving saved520's Reset value-1.
                if pc!=0x7626f7:transfers.append((off,dst[1],sourcewidth,destwidth,sign,load,pc))
        elif ins in ('ADD','SUB') and a in ptr and re.fullmatch('0x[0-9a-f]+',b):
            k,n=ptr[a];ptr[a]=(k,n+int(b,16)*(1 if ins=='ADD' else -1));values.pop(a,None)
        elif ins not in ('CMP','TEST','PUSH','NOP','UCOMISS','COMISS') and not ins.startswith('J') and ins not in ('RET','POP'):
            if '[' not in a:kill(a)
        elif ins=='POP':kill(a)
    #Dynamic computations, containers, optional owners and loops are handled
    #by maintained C++; this table contains only direct fixed-member copies.
    assert len(transfers)>150
    lines=['//761de0 direct member transfers verified against saved instruction bytes.',
        'struct NativePlayerTransfer {unsigned int source,destination,sourceBytes,destinationBytes;bool signExtend;unsigned int pc;};',
        'static const NativePlayerTransfer kPlayerPersistentTransfers[]={']
    lines+=['{0x%x,0x%x,%d,%d,%s,0x%x},'%(s,d,n,z,'true' if sign else 'false',pc) for s,d,n,z,sign,load,pc in transfers]
    lines+=['};']
    defaults=[]
    for pc,op,_ in reset:
        if 0x91213f<=pc<0x91214f:continue #ESI temporarily holds a vector allocation.
        x=re.fullmatch(r'MOV (dword|word|byte) ptr \[ESI(?: \+ 0x([0-9a-f]+))?\],(0x[0-9a-f]+|EAX|AX|AL)',op)
        if x:
            operand=x[3]
            if not operand.startswith('0x') and pc>=0x911fde:continue
            defaults.append((int(x[2] or '0',16),{'dword':4,'word':2,'byte':1}[x[1]],int(operand,16) if operand.startswith('0x') else 0,pc))
    #Only four-lane zero stack records and the explicit zero memset. The six
    #viewport loads are deliberately not exported as saved process values;
    #761de0 overwrites them from the actual live player's1428..143c fields.
    defaults += [(off+i*4,4,0,pc) for off,pc in [(0x84,0x911e6c),(0x98,0x911e86),(0xac,0x911ea8),(0xc0,0x911ec2)] for i in range(4)]
    defaults += [(0x10c+i*4,4,0,0x911fef) for i in range(16)]
    lines+=['struct NativePersistentResetWrite {unsigned int offset,bytes,value,pc;};','static const NativePersistentResetWrite kPlayerPersistentReset[]={']
    lines+=['{0x%x,%d,0x%xU,0x%x},'%d for d in defaults]+['};']
    out=ROOT/'build/generated/native_player_persistence_data.h';out.parent.mkdir(parents=True,exist_ok=True);out.write_text('\n'.join(lines)+'\n',encoding='ascii')
    evidence=ROOT/'evidence/reference_instructions/player-persistent-transfer.txt'
    evidence.write_text(''.join(x[2] for x in rows),encoding='ascii')
    reset_evidence=ROOT/'evidence/reference_instructions/player-persistent-reset.txt'
    reset_evidence.write_text(''.join(x[2] for x in reset),encoding='ascii')
    checkpoint_evidence=ROOT/'evidence/reference_instructions/normal-native-checkpoint.txt'
    checkpoint_evidence.write_text(''.join(checkpoint),encoding='ascii')
    report={'format':'native-player-persistent-transfer-v1','source':str(evidence.relative_to(ROOT)).replace('\\','/'),
        'sha256':hashlib.sha256(evidence.read_bytes()).hexdigest(),'start':'00761de0','end_exclusive':'007634d1',
        'transfers':[dict(source=s,destination=d,source_bytes=n,destination_bytes=z,sign_extend=sign,load=f'{load:08x}',store=f'{pc:08x}') for s,d,n,z,sign,load,pc in transfers],
        'reset':{'path':reset_evidence.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(reset_evidence.read_bytes()).hexdigest()},
        'checkpoint':{'path':checkpoint_evidence.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(checkpoint_evidence.read_bytes()).hexdigest(),
            'spans':[dict(start=f'{lo:08x}',end_exclusive=f'{hi:08x}',sha256=hashlib.sha256(memory.read(lo,hi-lo)).hexdigest()) for lo,hi in spans],
            'source_boundary':'Typed first-checkpoint capture and reached empty room-entry tail only. Native file codec, nonempty/dynamic restores, room music and whole player lifecycle remain separate unfinished services.'},
        'scope':'Direct addressed transfers and explicit Reset writes only. Runtime owns mutable values, containers, branch conditions and computed stores; no original execution.'}
    (ROOT/'runtime/port/native_persistence_reference.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':prepare()

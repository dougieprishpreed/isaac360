"""Bind fresh nested player owners to existing saved instructions, read only."""
from pathlib import Path
import hashlib,json,re
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def prepare(out):
    m=Memory(ROOT/'recovered/main')
    spans=[(0x707b40,0x70877c),(0x7087b0,0x708eba),(0x7e0610,0x7e0762),
           (0x711d20,0x711d96),(0x904960,0x904b1d),
           (0x6b4a19,0x6b4bc0),(0x76ea93,0x76eafc),(0x76ed69,0x76ee38),
           (0x401a80,0x401a8b),(0x948d80,0x948dd5),(0x92c970,0x92c9e7),
           (0x92cfb0,0x92cfca),(0x948c10,0x948c26),(0x77c5d0,0x77c6d8),
           (0x4203f8,0x4203fe),(0x7099a2,0x70a344),
           (0x76ea50,0x76eafc),(0x76ec09,0x76ec9d),(0x76ee38,0x76ef7e),
           (0x70f9f0,0x70faa3),(0x70fbbb,0x70fdd9),(0x7101f2,0x710231),
           (0x710ae2,0x710b2c),(0x710c95,0x710cc5),(0x710e92,0x710f6e),
           (0x70ef90,0x70f1ad),(0x70f50e,0x70f558),(0x70f9a0,0x70f9eb)]
    lines=[];writes=[];object_writes=[];edi=0;esi=None
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        match=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  (.*)',line)
        if not match:continue
        pc=int(match[1],16)
        if not any(a<=pc<b for a,b in spans):continue
        code=bytes.fromhex(match[2]);assert m.read(pc,len(code))==code
        lines.append(line)
        #707b40 has no normal conditional numeric path. Track the two object
        #base registers explicitly; allocators and called constructors preserve
        #them. Never interpret an allocation result as a numeric field value.
        if 0x707b7b<=pc<0x70877c:
            op=match[3].strip()
            lea=re.fullmatch(r'LEA ESI,\[EDI \+ 0x([0-9a-f]+)\]',op)
            if lea:esi=edi+int(lea[1],16)
            if pc==0x708626:edi+=0x1eb8
            if pc==0x708690:esi=0
            store=re.fullmatch(r'MOV (dword|word|byte) ptr \[(EDI|ESI)(?: \+ 0x([0-9a-f]+))?\],(0x[0-9a-f]+)',op)
            if store:
                base=edi if store[2]=='EDI' else esi
                assert base is not None,(hex(pc),op)
                width={'dword':4,'word':2,'byte':1}[store[1]]
                offset=base+int(store[3] or '0',16);value=int(store[4],16)
                #Vtable is a maintained C++ class. The following addresses
                #hold owned sentinel/reference handles, not scalar zeroes.
                if offset not in (0,0x158c,0x1d5c,0x1e74,0x1ea0,0x1ebc):
                    object_writes.append((pc,offset,width,value))
        if not 0x7087dc<=pc<0x708e85:continue
        store=re.fullmatch(r'MOV (dword|word|byte) ptr \[EDI \+ 0x([0-9a-f]+)\],(0x[0-9a-f]+|EAX|AX)',match[3].strip())
        if store:
            width={'dword':4,'word':2,'byte':1}[store[1]]
            if store[3] in ('EAX','AX'):
                assert pc>=0x708d5a #XOR EAX,EAX at708d58, no intervening change.
                value=0
            else:value=int(store[3],16)
            offset=int(store[2],16)
            assert offset+width<=0x5c4
            writes.append((pc,offset,width,value))
    assert len(writes)==191,len(writes)
    #Four constructor float loads resolve the original zero vector. No saved
    #process-global value is used as a new gameplay input.
    assert m.words(0xbcf6a8,2)==(0,0)
    object_writes += [(0x708022,0x15b0,4,0),(0x708032,0x15b4,4,0),
        (0x708042,0x15b8,4,0),(0x708052,0x15bc,4,0)]
    #The four128-bit stores at70848f..a4 are all signed-1 lanes.
    object_writes += [(0x70848f+i*7,0x1d10+i*16+j*4,4,0xffffffff) for i in range(4) for j in range(4)]
    assert m.read(0x708d58,2)==b'\x33\xc0'
    assert m.words(0xb2cd60+4,1)==(0x709330,)
    assert m.words(0xb2cd60+0x4c,2)==(0x70b4f0,0x66f8f0)
    assert m.words(0xb4b3d0,4)==(0x3f000000,0x3f000000,0x3f000000,0x3f800000)
    assert m.read(0x401a80,10)==bytes.fromhex('c7051ca7bc00f83aba00')
    assert m.words(0xba3af8,1)==(0xb3cf90,)
    assert m.words(0xb3cf90,1)==(0x948d80,)
    assert m.words(0xb3cf98+4,1)==(0x92cfb0,)
    assert m.words(0xb3cf98+0x10,1)==(0x948c10,)
    assert m.read(0x4203f8,6)==bytes.fromhex('66c7436c0100')
    #Preserved continuation listings are necessary because the original
    #export incorrectly ends at its logging call. Read, never recapture them.
    continuation=ROOT/'evidence/reference_instructions/player-creation-init.txt'
    if not continuation.exists():
        source=ROOT/'build/reference_ranges/0070a1f8_0070ad00.txt'
        rows=[];keep=False
        for line in source.read_text().splitlines():
            x=re.match(r'  ([0-9A-F]{8}):',line)
            if x:keep=0x70a1f8<=int(x[1],16)<0x70a344
            if keep:rows.append(line)
        continuation.write_text('Bounded preserved Init instructions; no original execution.\n'+'\n'.join(rows)+'\n',encoding='ascii')
    decoded=[ROOT/'evidence/reference_instructions/player-init-after-log.txt',continuation]
    init=[]
    for path in decoded:
        for line in path.read_text().splitlines():
            code=re.match(r'  ([0-9A-F]{8}):((?: [0-9A-F]{2})+)',line)
            if code:
                prefix=bytes.fromhex(code[2]);assert m.read(int(code[1],16),len(prefix))==prefix
        init += [(int(x[1],16),x[2]) for l in path.read_text().splitlines()
            if (x:=re.match(r'  ([0-9A-F]{8}):.*?\s{2}(\S.*)',l)) and 0x7099a2<=int(x[1],16)<0x70a344]
    init=sorted(dict(init).items());fixed=[];xmm_zero=False
    for pc,op in init:
        raw=m.read(pc,16)
        #Decode only these original stores, not arbitrary x86 execution.
        prefix=raw[:2];offset=None;value=None;width=None
        if prefix in (b'\xc7\x87',b'\xc6\x87'):
            offset=int.from_bytes(raw[2:6],'little');width=4 if prefix[0]==0xc7 else 1;value=int.from_bytes(raw[6:6+width],'little')
        elif raw[:3]==b'\x66\xc7\x87':
            offset=int.from_bytes(raw[3:7],'little');width=2;value=int.from_bytes(raw[7:9],'little')
        elif prefix in (b'\x89\x87',b'\x88\x87') or raw[:3]==b'\x66\x89\x87':
            #EAX is explicitly zero in these spans. The dynamic trinket97
            #remainder at709e62 and vector pointer writes are delegated.
            if 0x7099b8<=pc<0x709afc or 0x709b16<=pc<0x709e04 or 0x709e69<=pc<0x709eb9:
                word=raw[0]==0x66;offset=int.from_bytes(raw[3:7] if word else raw[2:6],'little');width=2 if word else 1 if raw[0]==0x88 else 4;value=0
        if offset is not None and pc!=0x709ede:fixed.append((pc,offset,width,value)) #Variant1 only; the genuine initial player is variant0.
        #MOVSS from the verified original zero vector, followed by its store.
        if raw[:4]==b'\xf3\x0f\x10\x05' and int.from_bytes(raw[4:8],'little') in (0xbcf6a8,0xbcf6ac):xmm_zero=True
        elif raw[:3]==b'\x0f\x57\xc0':xmm_zero=True
        elif raw[:4]==b'\xf3\x0f\x11\x87' and xmm_zero:
            fixed.append((pc,int.from_bytes(raw[4:8],'little'),4,0));xmm_zero=False
        elif raw[:3]==b'\x0f\x11\x87' and xmm_zero:
            off=int.from_bytes(raw[3:7],'little');fixed += [(pc,off+i*4,4,0) for i in range(4)]
        elif re.search(r'\bxmm0,',op):xmm_zero=False
    #Zero flags atBCB0D8 and explicit zero stack pairs for the active slots.
    #Those MOVQ stores reload XMM0 from stack, rather than the zero vector.
    assert m.words(0xbcb0d8,4)==(0,0,0,0)
    for pc,offset,words in [(0x709f4e,0x1720,2),(0x70a130,0x13c0,4),
            (0x70a195,0x14c4,2),(0x70a1ba,0x14e0,2),
            (0x70a1df,0x14fc,2),(0x70a204,0x1518,2)]:
        fixed += [(pc,offset+i*4,4,0) for i in range(words)]
    #The nested sprite resolves packed entity key100000 to type1/variant0.
    assert m.read(0x7e069a,7)==bytes.fromhex('c745ec00001000')
    header=ROOT/'build/generated/native_player_creation_data.h'
    header.parent.mkdir(parents=True,exist_ok=True)
    header.write_text('//7087b0 explicit scalar writes; no unwritten-prefix defaults.\n'
        'struct NativeCreationWrite {unsigned int offset,bytes,value,address;};\n'
        'static const NativeCreationWrite kNativeCreationCacheWrites[]={\n'+
        ''.join('{0x%x,%d,0x%xU,0x%x},\n'%(offset,width,value,pc) for pc,offset,width,value in writes)+'};\n'
        'static const NativeCreationWrite kNativePlayerConstructorWrites[]={\n'+
        ''.join('{0x%x,%d,0x%xU,0x%x},\n'%(offset,width,value,pc) for pc,offset,width,value in object_writes)+'};\n'
        'static const NativeCreationWrite kNativePlayerInitFixedWrites[]={\n'+
        ''.join('{0x%x,%d,0x%xU,0x%x},\n'%(offset,width,value,pc) for pc,offset,width,value in sorted(fixed))+'};\n',encoding='ascii')
    target=ROOT/'evidence/reference_instructions/player-creation.txt'
    target.write_text(''.join(lines),encoding='ascii')
    report=dict(format='normal-player-creation-reference-v1',
        instructions=dict(path=target.relative_to(ROOT).as_posix(),sha256=sha(target)),
        spans=[dict(start=f'{a:08x}',end_exclusive=f'{b:08x}',sha256=hashlib.sha256(m.read(a,b-a)).hexdigest()) for a,b in spans],
        cache_scalar_writes=len(writes),player_constructor_scalar_writes=len(object_writes),
        init_fixed_writes=len(fixed),continuation=dict(path=continuation.relative_to(ROOT).as_posix(),sha256=sha(continuation)),
        lifecycle=['006b4a30 ->00904960 ->00707b40 (nested7e0610 twice,7087b0)',
            '009049e0/e6 first-player identity,00904a27 ->00709330',
            '00709330 banks ->0066f440 (+50=0066f8f0,+4c=0070b4f0)',
            '7099a2 reset/draw ->70a344 auxiliary/body ->70a91f/711d20',
            '70a994 ->76ea50 appearance BEFORE resources/grants/stats and generation',
            '006b4b5e ->006f9d20 generation;006b4b88 ->0076c990 again'],
        limits='Owned nested constructor states, defined scalar writes, default weapon Init, native can-shoot result and empty costume reset. Appearance76ea50 and the remaining Init/tail/callbacks/grants/stats/registration/entry/update are not accepted. The pre/post-generation76c990 dispatch must be reconciled through the actual reached caller chain, not added unconditionally. No original/target execution.')
    path=ROOT/'runtime/port/normal_player_creation_reference.json'
    path.write_text(json.dumps(report,indent=2)+'\n')
    return ['tools/prepare_player_creation.py',target.relative_to(ROOT).as_posix(),continuation.relative_to(ROOT).as_posix(),path.relative_to(ROOT).as_posix()]

if __name__=='__main__':prepare(ROOT/'build/normal_flow/data')

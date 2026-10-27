"""Verify saved instructions needed by normal grid-room completion."""
from pathlib import Path
import hashlib,json,re,struct
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
RANGES=[(0x420300,0x420435),(0x6af560,0x6af578),(0x6b5fc0,0x6b5fd6),
    (0x6ee510,0x6ee621),(0x6ee660,0x6ee70d),(0x6ef0c0,0x6ef3c8),(0x6efe6d,0x6efebc),
    (0x6f2022,0x6f33f4),(0x6f6100,0x6f62d9),(0x700030,0x700236),(0x700510,0x700523),(0x703050,0x7030c2),
    (0x703560,0x703680),(0x7d4f90,0x7d4ff0),(0x8fa9b0,0x8fb4d4)]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    memory=Memory(ROOT/'recovered/main');source=ROOT/'recovered/main/disassembly.txt';lines=[];counts=[0]*len(RANGES)
    for line in source.open():
        m=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  (.*)',line)
        if not m:continue
        at=int(m[1],16)
        for i,(a,b) in enumerate(RANGES):
            if a<=at<b:
                raw=bytes.fromhex(m[2]);assert memory.read(at,len(raw))==raw,hex(at)
                lines.append(line);counts[i]+=1;break
    assert min(counts)>5
    out=ROOT/'logs/normal-grid-room-reference.txt';out.write_text(''.join(lines),encoding='ascii')
    constants={}
    for at,n in [(0xb4bb60,4),(0xb4bb70,4),(0xb4a310,4),(0xb4bbc0,4),(0xb4bba0,4),(0xb4a350,4),(0xb4a3d0,4),(0xba2020,2)]:
        constants[f'{at:08x}']=list(struct.unpack('<'+'i'*n,memory.read(at,n*4)))
    assert constants['00ba2020']==[-1,-1]
    continuation=ROOT/'evidence/reference_instructions/normal-floor-return-reference.txt';verified=0
    for line in continuation.read_text().splitlines():
        m=re.match(r'^\s+([0-9A-F]{8}): ((?:[0-9A-F]{2} )+)',line)
        if not m:continue
        at=int(m[1],16);raw=bytes.fromhex(m[2]);assert memory.read(at,len(raw))==raw,hex(at);verified+=1
    assert verified>100 and memory.read(0x6f6f84,5)==bytes.fromhex('e8a771d2ff')
    report=dict(format='normal-grid-room-reference-v1',ranges=[dict(first=f'{a:08x}',end_exclusive=f'{b:08x}',instructions=counts[i]) for i,(a,b) in enumerate(RANGES)],
        bounded_instructions=dict(path=out.relative_to(ROOT).as_posix(),sha256=sha(out)),
        saved_disassembly=dict(path=source.relative_to(ROOT).as_posix(),sha256=sha(source)),constants=constants,
        caller_return=dict(path=continuation.relative_to(ROOT).as_posix(),sha256=sha(continuation),verified_lines=verified,first='006f6c10',bytes=0x3b0,saved_bytes_sha256=hashlib.sha256(memory.read(0x6f6c10,0x3b0)).hexdigest()),
        source_module_sha256=next(e['sha256'] for e in json.loads((ROOT/'evidence/modules.json').read_text()) if e['id']=='main'),
        scope='Saved instruction bytes verified against prepared memory; separate arithmetic fixtures are not original execution traces.',
        boundary='First-chapter grid and off-grid descriptor selection through006f33f3; ordinary caller commits at006f6f84. Outer level finalization/activation begins006fa669.')
    (ROOT/'runtime/port/normal_grid_room_reference.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Normal grid reference:',sum(counts),'verified instructions.')
if __name__=='__main__':main()

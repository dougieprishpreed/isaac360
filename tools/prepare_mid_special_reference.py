"""Export bounded existing instruction evidence; no recapture/decompilation."""
from pathlib import Path
import hashlib,json,re
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    memory=Memory(ROOT/'recovered/main');source=ROOT/'recovered/main/disassembly.txt';lines=[];n=0;owners={}
    for line in source.open():
        m=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  (.*)',line)
        if not m:continue
        at=int(m[1],16)
        if not 0x6f10a6<=at<0x6f2022:continue
        raw=bytes.fromhex(m[2]);assert memory.read(at,len(raw))==raw
        lines.append(line);n+=1
        if at in (0x6f126b,0x6f14d7,0x6f1fb8):owners[m[1]]=m[3].strip()
    assert n>500 and all(s=='MOV ECX,EBX' for s in owners.values()) and len(owners)==3
    out=ROOT/'logs/normal-mid-special-reference.txt';out.write_text(''.join(lines),encoding='ascii')
    report=dict(format='normal-mid-special-reference-v1',range='006f10a6..006f2021',instructions_checked=n,
        bounded_instructions=dict(path=out.relative_to(ROOT).as_posix(),sha256=sha(out)),saved_disassembly=dict(path=source.relative_to(ROOT).as_posix(),sha256=sha(source)),
        rng_call_owners=owners,source_module_sha256=json.loads((ROOT/'recovered/main/summary.json').read_text()).get('sha256'),
        source_scope='Saved instruction bytes match prepared memory; integer/chance calls use the floor layout owner at EBX. Source-based arithmetic vectors are not original-run recordings.',
        limits=['The saved listing has logger/noreturn gaps; undefined thin-door end-room resize geometry remains open.'])
    # summary field names differ by capture; module identity is authoritative.
    report['source_module_sha256']=next(e['sha256'] for e in json.loads((ROOT/'evidence/modules.json').read_text()) if e['id']=='main')
    (ROOT/'runtime/port/normal_mid_special_reference.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Bounded mid-special instruction evidence:',n,'saved instructions verified; RNG register owners pinned.')
if __name__=='__main__':main()

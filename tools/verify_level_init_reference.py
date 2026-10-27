"""Pin the concrete post-log instructions needed by normal level setup.

Uses the existing bounded dumpbin text and saved memory, with read-only byte
comparison to game/. Does not launch/decompile/copy the original program.
"""
from pathlib import Path
import hashlib,json,struct
from source_data import module_source
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    module=next(e for e in json.loads((ROOT/'evidence/modules.json').read_text()) if e['id']=='main')
    source=module_source(module);data=source.read_bytes();memory=Memory(ROOT/'recovered/main')
    pe=struct.unpack_from('<I',data,0x3c)[0];count=struct.unpack_from('<H',data,pe+6)[0];size=struct.unpack_from('<H',data,pe+20)[0]
    base=struct.unpack_from('<I',data,pe+52)[0];table=pe+24+size;entries=[]
    for address,length,text in ((0x6fa027,0x2ed,'evidence/reference_instructions/normal-level-init-continuation-reference.txt'),(0x6fa3b9,0x9e7,'evidence/reference_instructions/normal-level-init-post-curse-reference.txt')):
        for i in range(count):
            _,rva,n,offset=struct.unpack_from('<IIII',data,table+40*i+8)
            if rva<=address-base<rva+n:
                start=offset+address-base-rva;raw=data[start:start+length];assert raw==memory.read(address,length)
                entries.append(dict(start='%08x'%address,bytes=length,sha256=hashlib.sha256(raw).hexdigest(),disassembly=text,disassembly_sha256=sha(ROOT/text),matches_saved_memory=True));break
        else:raise ValueError('Unmapped original instruction span')
    report=dict(format='normal-level-init-continuation-reference-v2',module_sha256=module['sha256'],source='game/isaac-ng.exe',ranges=entries,
        reason='The saved function body is truncated at logger006fa022. Resolve second layout advance, stage-even challenge subtype, first-stage curse gate and actual floor-size owner passed at006fa5e8.',
        findings=['006fa027..0072 performs a second layout advance after the pre-log advance.',
            '006fa078..0094 sets level byte8 iff stage2/4/6/8.',
            '006fa1f4..021d forces stage1 curse denominator0 before native profile byte65 (award33).',
            '006fa249/03b9 clones the current5/9/7 layout owner into a local curse-evaluation owner.',
            '006fa5e8..05ee passes that local owner to006f6340; the independent1/5/19 level owner is not the floor-size owner.'],
        operations='Read-only bounded dumpbin comparison to saved memory and pinned local module. No original launch, raw edits, decompilation or EXE/DLL snapshot copy.')
    (ROOT/'runtime/port/normal_level_init_reference.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Verified post-log level-init spans against saved memory and the pinned local source.')
if __name__=='__main__':main()

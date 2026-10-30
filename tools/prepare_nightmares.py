"""Normal nightmare filenames and addressed New Run reset operands."""
from pathlib import Path
import hashlib,json,re,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def prepare():
    m=Memory(ROOT/'recovered/main');resources=OriginalResources(ROOT/'runtime/game')
    raw,binding=resources.read('nightmares.xml');tree=ET.fromstring(raw)
    assert tree.tag==m.cstring(0xb36d9c)
    definitions=[tree.get('root','')+n.get('anm2') for n in tree if n.tag=='nightmare']
    assert len(definitions)==17 and m.words(0xb4a6f0,4)==(17,17,17,17)
    #The loader retains these filenames; actual cinematic Sprite loading
    #occurs later. Keep the full resource identities, no selected-only list.
    sources=[]
    for path in definitions:
        _,source=resources.read(path);sources.append(source)
    lines=['//Normal nightmares.xml order; no shuffle result or live state.',
        'static const unsigned int kNativeNightmareInitialBackdrop=%dU;'%m.words(0xb4a6f0,1)[0],
        'static const char* const kNativeNightmares[]={']
    lines += [json.dumps(p)+',' for p in definitions]+['};']
    (ROOT/'build/generated/native_nightmare_data.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    spans=((0x86adc0,0xd7),(0x8a1d45,0x36),(0x86d780,0x12d),(0x86f592,0x35),(0x872d80,0x52))
    proof=ROOT/'evidence/reference_instructions/normal-nightmare-reset-profile.txt';rows=[]
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        match=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if match and any(a<=int(match[1],16)<a+n for a,n in spans):
            code=bytes.fromhex(match[2]);assert m.read(int(match[1],16),len(code))==code;rows.append(line)
    proof.write_text(''.join(rows),encoding='ascii')
    report=dict(format='normal-nightmare-reset-profile-v1',source=binding,filenames=definitions,resources=sources,
        initial_backdrop=17,shuffle='86adc0: rebuild17 indices,16 ordered global MT draws; no run seed bootstrap',
        profile='Main+c is PersistentGameData. Award bytes+38 length638; event counters+2b8 length496 dwords. Main+12e is award234.',
        limits='New Run reset and stage1 backdrop bookkeeping only. Preceding boot/frontend global consumers and cinematic playback remain open.',
        spans=[dict(address='%08x'%a,bytes=n,sha256=hashlib.sha256(m.read(a,n)).hexdigest()) for a,n in spans],
        instructions=dict(path=proof.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(proof.read_bytes()).hexdigest()))
    (ROOT/'runtime/port/native_nightmare_reference.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':prepare()

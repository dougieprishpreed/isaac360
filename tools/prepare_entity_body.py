"""Bind the normal player entity's physical properties and native size loop."""
from pathlib import Path
import hashlib,json,math,re,struct,xml.etree.ElementTree as ET
from original_resources import OriginalResources
from reference_memory import Memory
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def scalar(m,a):return struct.unpack('<f',m.read(a,4))[0]
def literal(v):
    s=format(f32(v),'.17g');return s+('' if '.' in s or 'e' in s else '.0')+'f'
def catalogue():
    m=Memory(ROOT/'recovered/main');resources=OriginalResources(ROOT/'runtime/game');raw,binding=resources.read('entities2.xml')
    root=ET.fromstring(raw);assert root.get('version')=='5'
    rows=[];records=[]
    assert m.words(0x9e9bf0,3)==(4,3,17)==m.words(0x9e9a88+30*12,3)
    assert m.words(0xba3b14,2)==(0x3f800000,0x3f800000)
    assert scalar(m,0xb49c98)==100 and m.words(0xb4935c,1)[0]==0x3a83126f
    #0065c5e6..5f5 converts integer shadow size, then divides by100.
    #0065b388..3a4 initializes radius1 and X/Y multipliers1.
    for variant in (0,1):
        matches=[e for e in root if e.tag=='entity' and int(e.get('id','0'))==1 and int(e.get('variant','0'))==variant and int(e.get('subtype','0'))==0]
        assert len(matches)==1;e=matches[0];a=e.attrib
        assert not e.findall('gridcollision') and 'gridcollision' not in a
        count=int(a.get('numGridCollisionPoints','0'));assert count==40
        values=[f32(f32(int(a.get('shadowSize','0')))/100),f32(float(a.get('friction','0'))),f32(float(a.get('collisionRadius','1'))),
            f32(float(a.get('collisionRadiusXMulti','1'))),f32(float(a.get('collisionRadiusYMulti','1'))),f32(float(a.get('collisionDamage','0'))),
            f32(float(a.get('collisionMass','0'))),f32(float(a.get('baseHP','0'))),f32(float(a.get('stageHP','0')))]
        assert values[-1]==0 #No stage-health scaling on these physical records.
        _,animation=resources.read(root.get('anm2root')+a['anm2path'])
        rows.append('{1,%d,0,%d,%s,%s},'%(variant,count,','.join(map(literal,values)),json.dumps(animation['virtual_path'])))
        records.append(dict(attributes=a,grid_points=count,scalar_bits=[bits(v) for v in values],animation_property=animation))
    constants=dict(kNativeBodyPi=0xb49a0c,kNativeBodyStageThreshold=0xb4935c,kNativeBodyStageCap=0xb49b14,kNativeBodyEarlyHP=0xb497b4,kNativeBodyLateHP=0xb49700)
    lines=['//Original inherited entities2.xml; normal_entity_body_reference.json.','static const NativeEntityBodyProperties kNativePlayerBodies[] = {']+rows+['};']
    lines+=['static const unsigned int %s=0x%08xU;'%(name,m.words(a,1)[0]) for name,a in constants.items()]
    p=ROOT/'build/generated/native_entity_body_data.h';p.parent.mkdir(parents=True,exist_ok=True);p.write_text('\n'.join(lines)+'\n',encoding='ascii')
    return m,binding,records,constants
def prepare_catalogue():catalogue()
def shape(r,x,y,count,pi):
    #Independent linear formula; emit native even/odd traversal order only at
    #the end. Preserve each specified scalar float rounding before CRT math.
    points=[]
    for i in range(max(0,count)):
        angle=f32(f32(f32(f32(i)+f32(i))*pi)/f32(count))
        points.append((f32(f32(math.cos(angle))*f32(r*x)),f32(f32(math.sin(angle))*f32(r*y))))
    return points[::2]+points[1::2]
def prepare(out):
    m,binding,records,constants=catalogue()
    continuation_path='evidence/reference_instructions/player-init-after-log.json'
    continuation=json.loads((ROOT/continuation_path).read_text())
    assert sha(ROOT/continuation['source'])==continuation['source_sha256']
    assert sha(ROOT/continuation['listing'])==continuation['listing_sha256']
    start,end=int(continuation['start'],16),int(continuation['end_exclusive'],16)
    assert hashlib.sha256(m.read(start,end-start)).hexdigest()==continuation['range_sha256']
    spans=[(0x65b340,0x65b3cc),(0x65c080,0x65c0fd),(0x662260,0x662415),(0x65c5d8,0x65c5fd),(0x65c9f4,0x65cd05),(0x66e942,0x66ec7f),
        (0x66f440,0x66f8e6),(0x66f8f0,0x66fa32),(0x66fb2c,0x66fb54),(0x671de0,0x67205e),(0x70b4f0,0x70b532),(0x709978,0x7099a2)]
    lines=[]
    for line in (ROOT/'recovered/main/disassembly.txt').open():
        match=re.match(r'^([0-9a-f]{8})  ([0-9a-f]+)  ',line)
        if match and any(a<=int(match[1],16)<b for a,b in spans):
            data=bytes.fromhex(match[2]);assert m.read(int(match[1],16),len(data))==data;lines.append(line)
    path=ROOT/'evidence/reference_instructions/entity-body.txt';path.write_text(''.join(lines),encoding='ascii')
    pi=scalar(m,constants['kNativeBodyPi']);cases=[]
    for count in (-2,0,1,2,3,4,5,7,40):
        for radius,x,y in ((10.,1.,1.),(7.,1.,1.),(3.5,2.,.5),(-2.,.5,1.5)):
            points=shape(radius,x,y,count,pi);cases.append([count,bits(radius),bits(x),bits(y),len(points)]+[bits(v) for p in points for v in p])
    (out/'entity_body_shapes.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in cases),encoding='ascii')
    report=dict(format='normal-entity-body-reference-v1',source_binding=binding,records=records,
        instructions=dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path),count=len(lines)),
        spans=[dict(start=f'{a:08x}',end_exclusive=f'{b:08x}',sha256=hashlib.sha256(m.read(a,b-a)).hexdigest()) for a,b in spans],
        constants={n:dict(address=f'{a:08x}',bits=f'{m.words(a,1)[0]:08x}') for n,a in constants.items()},unimplemented_player_continuation=continuation,
        semantics=['Retained fifth player startup value becomes initialSeed and undrawn entity index30 owner, not default35.',
            'Physical player records resolve inherited entities2.xml; shadow size is divided by100, X/Y multipliers default1.',
            '00671de0 constructs even-index boundary samples followed by odd indices. Unchanged radius/multipliers return without comparing count.',
            'Base body starts at zero position/velocity, frameCount0, original game frame retained. New Run placement320/400 is later and is not executed here.'],
        property_lookup_scope='Exact original type1/variant0/subtype0 record exists. 0065c080 fallback is not needed for this session. 00662260 preloads assets/dependencies and marks loaded; physical property writes are separate. Preloading is still unimplemented, not synthesized by the numeric body.',
        scope='Owned physical subset of native base entity constructor/Init and property loading, before player-specific Init/reset. No sprites, player health, inventory/cache, registry or complete ticks. Variant1 is a property/geometry component fixture, not local-player acceptance.',
        expectation_class='Saved-instruction arithmetic and original inherited properties, not recorded original execution. Float fields checked exactly; CRT trig-derived coordinates allow bounded float error. PowerPC equivalence remains untested.')
    target=ROOT/'runtime/port/normal_entity_body_reference.json';target.write_text(json.dumps(report,indent=2)+'\n')
    return ['tools/prepare_entity_body.py',path.relative_to(ROOT).as_posix(),target.relative_to(ROOT).as_posix(),'runtime/game/'+binding['file'],continuation_path,continuation['listing']]+['runtime/game/'+r['animation_property']['file'] for r in records]

"""Check dependency closure, original frontend bindings and clean build inputs."""
from pathlib import Path
import ast,hashlib,json,re,struct,sys,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from original_resources import OriginalResources
for p in (ROOT/'tools').glob('*.py'):ast.parse(p.read_text(),filename=str(p))
for p in (ROOT/'src').rglob('*'):
    if p.suffix not in ('.h','.cpp','.hlsl'):continue
    text=p.read_text()
    assert not re.search(r'\b(?:AlphaGame|AlphaRoom|AlphaEnemy|AlphaItemPool|SceneRoute|PortSaveInfo|ReplayAxis)\b',text),p
    for name in re.findall(r'^#include "([^"]+)"',text,re.M):
        assert (p.parent/name).is_file() or (ROOT/'build/generated'/name).is_file(),(p,name)
for p in (ROOT/'src').rglob('*.vcxproj'):
    ET.parse(p);text=p.read_text();assert 'prepare_alpha' not in text
    assert '<MultiProcessorCompilation>false' in text and '<BuildInParallel>false' in text
manifest=json.loads((ROOT/'runtime/port/frontend/manifest.json').read_text());resources=OriginalResources(ROOT/'runtime/game')
for e in manifest['bindings']:
    raw,actual=resources.read(e['request']);assert actual['sha256']==e['sha256'] and actual['package']==e['package']
atlas=(ROOT/'runtime/port/frontend/atlas.bin').read_bytes()
assert atlas[:8]==b'GATLAS1\0' and list(struct.unpack_from('<II',atlas,8))==manifest['atlas_size']
assert len(atlas)==16+4*manifest['atlas_size'][0]*manifest['atlas_size'][1]
assert hashlib.sha256(atlas).hexdigest()==manifest['atlas_sha256']
config=json.loads((ROOT/'runtime/port/original_configuration.json').read_text())
for e in config['files']:assert hashlib.sha256((ROOT/e['path']).read_bytes()).hexdigest()==e['sha256']
print('PASS maintained dependency closure, serial target project and',len(manifest['bindings']),'original frontend bindings.')

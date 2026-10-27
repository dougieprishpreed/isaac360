"""Read-only source routing and original observation checks."""
from pathlib import Path
import json,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from source_data import source_root,source_file,CHECK_PATH
from configuration_observation import observation_witness
assert source_root()==(ROOT/'game').resolve()
for path in (ROOT,ROOT/'runtime/game','Z:/unavailable_original_source'):
    try:source_root(path)
    except ValueError:pass
    else:raise AssertionError('External source accepted')
for name in ('../escape','a/../../escape','C:/external','/absolute'):
    try:source_file(name)
    except ValueError:pass
    else:raise AssertionError('Unsafe source path accepted')
proof=json.loads(CHECK_PATH.read_text());assert proof['saved_references_match'] and Path(proof['source_root'])==source_root()
assert len([e for e in proof['files'] if e['kind']=='module'])==18
assert len([e for e in proof['files'] if e['kind']=='package'])==21
observation_witness()
print('PASS project-local source routing and preserved observation; no original launch or writes.')

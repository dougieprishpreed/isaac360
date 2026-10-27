"""Authoritative project-local original data; read only, never launch or copy."""
from pathlib import Path, PurePosixPath
import argparse,datetime,hashlib,json,os
ROOT=Path(__file__).resolve().parents[1]
SOURCE_ROOT=ROOT/'game'
CHECK_PATH=ROOT/'runtime/port/source_data_check.json'

def source_root(candidate=None):
    if candidate is not None and Path(os.path.abspath(candidate))!=SOURCE_ROOT.absolute():
        raise ValueError('Use the project-local game directory for original data')
    root=SOURCE_ROOT.resolve(strict=True)
    if not root.is_relative_to(ROOT) or not root.is_dir():raise ValueError('Source data must be the project-local game directory')
    return root

def source_file(relative):
    name=PurePosixPath(str(relative).replace('\\','/'))
    if name.is_absolute() or any(part=='..' or ':' in part for part in name.parts):raise ValueError('Invalid source-relative file')
    root=source_root();path=(root/Path(*name.parts)).resolve(strict=True)
    if not path.is_relative_to(root) or not path.is_file():raise ValueError('Source file leaves the project-local game directory')
    return path

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for part in iter(lambda:f.read(1024*1024),b''):h.update(part)
    return h.hexdigest()

def module_source(module):
    path=source_file('isaac-ng.exe' if module['id']=='main' else module['source_relative'])
    if sha(path)!=module['sha256']:raise ValueError('Local source module differs from saved references: '+module['id'])
    return path

def verify_source_data():
    root=source_root();packages=json.loads((ROOT/'evidence/packages.json').read_text())
    modules=json.loads((ROOT/'evidence/modules.json').read_text());checked=[]
    for kind,entries in (('module',modules),('package',packages)):
        for entry in entries:
            relative='isaac-ng.exe' if kind=='module' and entry['id']=='main' else entry['source_relative']
            path=source_file(relative);digest=sha(path)
            if digest!=entry['sha256'] or path.stat().st_size!=entry['bytes']:
                raise ValueError('Local source differs from saved references: '+entry['id'])
            checked.append(dict(kind=kind,id=entry['id'],relative_path=relative,reference_alias=entry['source_relative'],bytes=path.stat().st_size,sha256=digest))
    report=dict(format='generic-project-source-check-v1',observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        source_root=str(root),source_relative='game',source_policy='User-provided workspace copy is the authoritative source whenever original data is needed. No per-use access gate or external source fallback.',
        operations='Read-only hashes of prepared-reference modules and packages. No original program launch, binary snapshot copy, extraction or decompilation.',
        executable_sha256=next(e['sha256'] for e in modules if e['id']=='main'),saved_references_match=True,
        original_program_launched=False,source_modified=False,files=checked)
    CHECK_PATH.parent.mkdir(parents=True,exist_ok=True);CHECK_PATH.write_text(json.dumps(report,indent=2)+'\n')
    print('Verified project-local game source: %d modules and %d archives match saved references; no launch/copy.'%(len(modules),len(packages)))
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--verify',action='store_true');args=parser.parse_args()
    if args.verify:verify_source_data()
    else:print(source_root())

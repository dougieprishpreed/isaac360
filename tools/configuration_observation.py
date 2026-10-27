"""Resolve the existing user observation without rewriting historical evidence."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
def observation_witness():
    observation=json.loads((ROOT/'evidence/normal_configuration_observation_20261025.json').read_text())
    expected=observation['input_image']
    paths=[ROOT/expected['path'],ROOT/'evidence/normal_title_observation.jpg']
    for path in paths:
        if not path.is_file():continue
        assert path.resolve().is_relative_to(ROOT)
        digest=hashlib.sha256(path.read_bytes()).hexdigest()
        assert digest==expected['sha256'] and path.stat().st_size==expected['bytes'],'User observation witness differs'
        return dict(path=path.relative_to(ROOT).as_posix(),sha256=digest)
    raise FileNotFoundError('No hash-matching preserved user observation witness')

"""Saved-program archive replacement and default English resource inheritance.

No installed access, runtime entitlement guess, supported-content filter or DLC UI.
The corrected direct-launch observation confirms this inherited normal setup.
"""
from pathlib import Path
import hashlib,struct
from extract_resources import filename_hash

MOUNTS=(7,8,9,10,11,19,20,21,1,3,2,4,6,5,12,13,14,15,16,17,18)
ROOTS=('resources-dlc3/','resources/','')

class OriginalResources:
    def __init__(self,game):
        self.game=Path(game); self.entries={}; raw=(self.game/'file_index.bin').read_bytes()
        if raw[:8]!=b'GPCIDX1\0':raise ValueError('Invalid prepared index')
        count,=struct.unpack_from('<I',raw,8);pos=12
        for _ in range(count):
            package,flags,nl,pl,size,a,b,digest=struct.unpack_from('<HHHHQII32s',raw,pos);pos+=56
            name=raw[pos:pos+nl].decode();pos+=nl
            path=raw[pos:pos+pl].decode();pos+=pl
            if not flags&1 and filename_hash(name)!=(a,b):raise ValueError('Index name/hash mismatch')
            if package not in MOUNTS:continue
            e=dict(package='package_%02d'%package,virtual_path=name,file=path,bytes=size,sha256=digest.hex())
            key=(a,b);rank=MOUNTS.index(package)
            if key in self.entries and rank==self.entries[key][0]:raise ValueError('Duplicate key in one mount')
            if key not in self.entries or rank>self.entries[key][0]:self.entries[key]=(rank,e)
        if pos!=len(raw):raise ValueError('Index trailing bytes')

    def resolve(self,name):
        for root in ROOTS:
            match=self.entries.get(filename_hash(root+name))
            if match:return match[1]
        raise ValueError('Original resource missing: '+name)

    def read(self,name):
        e=self.resolve(name);raw=(self.game/e['file']).read_bytes()
        if len(raw)!=e['bytes'] or hashlib.sha256(raw).hexdigest()!=e['sha256']:raise ValueError('Prepared object mismatch')
        return raw,e

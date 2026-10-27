"""Read original saved reference memory without modifying or executing it."""
from pathlib import Path
import hashlib,json,struct
def rows(path):
    if path.exists():
        with path.open(encoding='utf-8') as stream:
            yield from (json.loads(line) for line in stream if line.strip())


class Memory:
    def __init__(self, folder):
        self.blocks = []
        for row in rows(folder/'memory_blocks.jsonl'):
            try:
                start = int(row['start'],16)
            except ValueError:
                continue
            if row.get('file'):
                data=(folder/row['file']).read_bytes()
                if len(data)!=row['size'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
                    raise ValueError('Saved reference memory hash mismatch: '+row['file'])
                self.blocks.append((start,data,row))

    def read(self, address, length):
        for base,data,row in self.blocks:
            offset = address-base
            if 0 <= offset and offset+length <= len(data):
                return data[offset:offset+length]
        return None

    def words(self, address, count):
        value = self.read(address,count*4)
        return struct.unpack('<'+'I'*count,value) if value is not None else None

    def cstring(self, address, limit=512):
        for base,data,row in self.blocks:
            offset = address-base
            if 0 <= offset < len(data):
                end = data.find(b'\0',offset,min(len(data),offset+limit))
                if end >= 0:
                    try:
                        return data[offset:end].decode('ascii')
                    except UnicodeDecodeError:
                        pass
        return None

    def executable(self, address):
        return any(row['execute'] and base <= address < base+len(data) for base,data,row in self.blocks)


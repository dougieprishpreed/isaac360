"""Disassemble a bounded saved x86 reference range with installed VS2010 tools.

The COFF contains only the requested instruction window, never a restored game
EXE/DLL or standalone program. Raw references are hash-verified and read-only.
Use one CPU and no concurrent analysis/build/emulator worker.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('start',type=lambda s:int(s,16));parser.add_argument('end',type=lambda s:int(s,16))
    args=parser.parse_args();start,end=args.start,args.end
    if not 0<end-start<=32768:raise ValueError('Explicit instruction window must be 1..32768 bytes.')
    blocks=[json.loads(l) for l in (ROOT/'recovered/main/memory_blocks.jsonl').open()]
    block=next(b for b in blocks if b['name']=='.text' and int(b['start'],16)<=start<end<=int(b['end'],16)+1)
    source=ROOT/'recovered/main'/block['file'];raw=source.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=block['sha256']:raise ValueError('Saved reference hash mismatch.')
    base=int(block['start'],16);code=raw[start-base:end-base]
    folder=ROOT/'build/reference_ranges';folder.mkdir(parents=True,exist_ok=True)
    stem=f'{start:08x}_{end:08x}';obj=folder/(stem+'.obj');listing=folder/(stem+'.txt')
    # A single non-executable COFF object, with its saved virtual address so
    # relative branch/call operands retain reference addresses in the listing.
    header=struct.pack('<HHIIIHH',0x14c,1,0,0,0,0,0)
    section=struct.pack('<8sIIIIIIHHI',b'.text\0\0\0',len(code),start,len(code),60,0,0,0,0,0x60000020)
    obj.write_bytes(header+section+code)
    script=folder/(stem+'.cmd')
    script.write_text('@echo off\ncall "%VS100COMNTOOLS%..\\..\\VC\\vcvarsall.bat" x86 >nul\nif errorlevel 1 exit /b 1\n'
        +f'dumpbin /disasm:bytes build\\reference_ranges\\{stem}.obj\n',encoding='ascii')
    result=subprocess.run(['cmd','/d','/c',str(script.relative_to(ROOT))],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if result.returncode:
        print(result.stdout.decode('utf-8',errors='replace'));result.check_returncode()
    listing.write_bytes(result.stdout)
    report=dict(format='generic-saved-instruction-window-v1',start=f'{start:08x}',end_exclusive=f'{end:08x}',
        source=source.relative_to(ROOT).as_posix(),source_sha256=block['sha256'],range_sha256=hashlib.sha256(code).hexdigest(),
        object=obj.relative_to(ROOT).as_posix(),object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),
        listing=listing.relative_to(ROOT).as_posix(),listing_sha256=hashlib.sha256(result.stdout).hexdigest(),
        provenance='Bounded instruction bytes from the existing saved .text reference, wrapped in a non-executable COFF for VS2010 dumpbin. No original executable accessed/restored/run; raw data unchanged.')
    (folder/(stem+'.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(result.stdout.decode('utf-8',errors='replace'))
if __name__=='__main__':main()

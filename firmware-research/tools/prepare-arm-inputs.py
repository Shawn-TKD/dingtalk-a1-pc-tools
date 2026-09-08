"""Find Thumb analysis entry candidates and printable data for Ghidra."""
import json
import re
import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'decompiled' / 'inputs'
OUT.mkdir(parents=True, exist_ok=True)
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
images = [('nuttx_ap.bin', 0x10190000, 0x271800),
          ('nuttx_apc1.bin', 0x10990000, 0x224c00)]
for name, base, code_end in images:
    data = (ROOT / 'unpacked-V1.6.88' / name).read_bytes()
    candidates = {}
    def add(offset, reason):
        if not 0x10 <= offset < code_end or offset % 2:
            return
        decoded = list(md.disasm(data[offset:offset+24], base+offset, count=5))
        if not decoded or decoded[0].mnemonic in ('udf', 'bkpt'):
            return
        if len(decoded) < 3 and decoded[0].mnemonic not in ('bx', 'b', 'b.w'):
            return
        candidates.setdefault(base+offset, set()).add(reason)
    add(0x10, 'image_entry_stub')
    if name == 'nuttx_ap.bin':
        add(0x14, 'reset_entry_literal')
    else:
        add((struct.unpack_from('<I', data, 0x14)[0] & ~1)-base, 'reset_entry_literal')
    for off in range(0x10, code_end-4, 2):
        a, b = struct.unpack_from('<HH', data, off)
        if a & 0xff00 == 0xb500 or (a == 0xe92d and b & 0x4000):
            add(off, 'thumb_push_lr_candidate')
        if a & 0xf800 == 0xf000 and b & 0xd000 == 0xd000:
            s = a >> 10 & 1
            i1 = 1 ^ ((b >> 13 & 1) ^ s)
            i2 = 1 ^ ((b >> 11 & 1) ^ s)
            disp = (s << 24) | (i1 << 23) | (i2 << 22) | ((a & 0x3ff) << 12) | ((b & 0x7ff) << 1)
            if s:
                disp -= 1 << 25
            add(off+4+disp, 'direct_bl_target_candidate')
    for off in range(code_end, len(data)-4, 4):
        ptr = struct.unpack_from('<I', data, off)[0]
        if ptr & 1 and base+16 <= ptr < base+code_end:
            add((ptr & ~1)-base, 'data_thumb_pointer_candidate')
    (OUT/(name+'.seeds.tsv')).write_text('\n'.join(
        f'{addr:08x}\t{",".join(sorted(reasons))}' for addr, reasons in sorted(candidates.items())), encoding='utf-8')
    strings=[]
    for match in re.finditer(rb'[\x20-\x7e]{5,}\x00', data):
        if match.start() >= code_end:
            value=match.group()[:-1].decode('ascii')
            strings.append((base+match.start(), len(value)+1, value))
    (OUT/(name+'.strings.tsv')).write_text('\n'.join(f'{addr:08x}\t{size}\t{value}' for addr,size,value in strings), encoding='utf-8')
    metadata={'image':name,'base':hex(base),'code_end_offset':hex(code_end),'candidate_functions':len(candidates),
              'strings':len(strings),'notes':'Function starts are analysis candidates, not verified symbols. Code/data boundary is provisional.'}
    (OUT/(name+'.json')).write_text(json.dumps(metadata,indent=2),encoding='utf-8')
    print(json.dumps(metadata))

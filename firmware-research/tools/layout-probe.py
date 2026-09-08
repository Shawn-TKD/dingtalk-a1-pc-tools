"""Explore firmware mappings without executing any image."""
import re
import struct
from pathlib import Path
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_LITTLE_ENDIAN, CS_MODE_MCLASS

ROOT = Path(__file__).resolve().parent
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN | CS_MODE_MCLASS)
for name in ('nuttx_ap.bin', 'nuttx_apc1.bin'):
    data = (ROOT/'unpacked-V1.6.88'/name).read_bytes()
    chip = data.find(b'CHIP=best1700')
    base = struct.unpack_from('<I', data, 12)[0] - chip + 1
    print(name, 'base', hex(base), 'header', data[:16].hex())
    for i in md.disasm(data[16:256], base+16):
        print(f'{i.address:08x} {i.mnemonic:8} {i.op_str}')
    for needle in (b'dt_hid_handle_set_working_mode\0', b'dtiot_device_generate_and_set_secret\0', b'nsh_main\0'):
        at = data.find(needle)
        if at < 0:
            continue
        print('SYMBOL', needle, hex(at))
        for alias in (base, base-0x20000000):
            refs = [m.start() for m in re.finditer(re.escape(struct.pack('<I', alias+at)), data)]
            print('references', hex(alias+at), [hex(x) for x in refs[:15]])
            for r in refs[:3]:
                start = max(0, r-16)
                print('words', hex(start), [hex(x) for x in struct.unpack('<12I', data[start:start+48])])

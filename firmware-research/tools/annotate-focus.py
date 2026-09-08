"""Add literal-pool evidence to selected AP pseudocode without changing its code."""
import re
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parent
folder=ROOT/'decompiled/exports/nuttx_ap.bin/functions'
out=ROOT/'decompiled/focus'
out.mkdir(parents=True,exist_ok=True)
blob=(ROOT/'unpacked-V1.6.88/nuttx_ap.bin').read_bytes()

def offset(addr):
    if 0x10190000<=addr<0x10190000+len(blob):
        return addr-0x10190000
    if 0x201d16f0<=addr<0x201dcd74:
        return 0x3aabc0+addr-0x201d16f0
    return None

def describe(addr):
    off=offset(addr)
    if off is None or off+4>len(blob):return 'unmapped/runtime data'
    word=struct.unpack_from('<I',blob,off)[0]
    target=offset(word)
    value=f'word=0x{word:08x}'
    if target is not None:
        end=blob.find(b'\0',target,target+1024)
        if end>target and all(32<=b<127 or b in (9,10,13) for b in blob[target:end]):
            value+=' -> '+repr(blob[target:end].decode('ascii'))
    return value

selected=[]
for p in folder.glob('*.c'):
    if any(x in p.name for x in ('dt_hid_','dtiot_hid_','dt_dbgserial','voice_memo','device_secret','generate_and_set_secret',
                                  'dtiot_hal_os_','103cf5d8_','103c8638_', '101c551c_', '103c8570_',
                                  '103cf47c_', '103cf5d8_', '103cf610_', '103cf64c_',
                                  '103cf688_', '103cf6c4_', '103cf770_', '103cf788_',
                                  '103cf7a4_', '103cf7c0_', '103cf85a_', '103cf886_',
                                  '103cf8ca_', '103cf9e8_', 'dtiot_device_get_bind_status', '103d809c_',
                                  'dtiot_ble_', 'dt_ble_', 'dt_raw_transfer_', 'audio_status', 'dt_button', 'remark', 'voiceprint',
                                  '103c6598_', '103e04e0_', '103e477c_', '103e4964_',
                                  '103d77b0_', '103d7970_', 'dt_wifi_', 'lnv_webserver_',
                                  '103c92fc_', '103cb4a4_', '103d42d4_', '103d54fc_',
                                  '103c7d84_', '103c7dc0_', '103c7df0_',
                                  '103d700c_', '103d7554_', '103d8592_', '103db2e8_',
                                  '103db4c4_', '103dc024_', '103dc910_', '103e1a78_',
                                  '103e8f70_', '103e8ff8_', '103e914c_',
                                  'dt_tcp_', '103ebf08_', '103ebf94_', '103e5286_',
                                  '102eac48_', '102eac50_', '102eb1ec_', '102bfa20_',
                                  '102eade4_', '102eac6c_', '102ead54_', '102eab64_',
                                  '102eb128_', 'dt_ota_', '103c5ec8_', '103c5b60_',
                                  'dt_light_ui_', '103c5276_', 'dt_display_', '103e0598_',
                                  '103e03a0_', '103e007c_', '103e02b8_',
                                  'dt_stream_', 'flush_cache_to_file', 'dt_frame_header_reset',
                                  '1030baa8_', '1030baf4_', '1030bb0c_', '1030bb18_',
                                  '1030bc2c_', '1030bc4c_', '1030cc2c_', '1030d534_', '1030d5cc_',
                                  '1030e0d0_', '1030e0ec_', '1030e18c_', '1030e208_',
                                  '1030e220_', '1030e234_', '1030e2ec_', '1030e318_',
                                  '1030e410_', '1030e44c_', '1030e47c_',
                                  '1030f810_', '101ca7e8_')):
        code=p.read_text(encoding='utf-8')
        syms=sorted(set(re.findall(r'\b(?:DAT_|uRam|iRam|PTR_)([0-9a-f]{8})\b',code)))
        header=['/* Literal-pool annotations from unmodified AP firmware.',
                '   Inferred names may identify inlined operations, not original function boundaries.']
        header += [f'   {sym}: {describe(int(sym,16)).replace("*/","* /")}' for sym in syms]
        header += ['*/','']
        (out/p.name).write_text('\n'.join(header)+code,encoding='utf-8')
        selected.append(p.name)
print('Annotated focus functions:',len(selected))

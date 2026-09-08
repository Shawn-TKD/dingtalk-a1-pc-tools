# DingTalk A1 V1.6.88 firmware reconstruction

This directory publishes reproducible facts and independently written,
functionally compatible source for an owner-controlled DingTalk A1 recorder.
It is not a dump of the vendor source tree and it is not a flashable image.

## Layout

- `reports/`: reviewed findings for BLE, USB, recording, files, buttons,
  vibration, identity, Flash layout, boot and companion-core IPC;
- `analysis/`: generated worklists, semantic reviews and symbol/address match
  tables used to measure progress;
- `tools/`: read-only firmware/Ghidra/ELF analysis helpers;
- `compat-src/`: portable C11 compatible product core and focused tests.

Start with:

1. `compat-src/docs/COMMAND-REGISTRY.md` for the 27 inbound BLE commands;
2. `reports/PRODUCT-RECONSTRUCTION-STATUS.md` for measured coverage;
3. `compat-src/docs/TRACEABILITY.md` for behavior-to-address evidence;
4. `reports/BEST1700-BSP-BRIDGE-ANALYSIS-20260908.md` for the same-chip BSP
   comparison and its licensing/deployment limits.

## Inputs intentionally not published

The repository excludes OTA/installed firmware binaries, full string dumps,
Ghidra projects, recordings, packet captures, device/account configuration,
factory identity, deviceSecret, API keys, access tokens, vendor static
libraries and programmer binaries. Some analysis scripts therefore require
the researcher to provide their own legally obtained image or local reference
BSP before regenerating a table.

## Host build

The compatible core has no vendor dependency:

```sh
cmake -S compat-src -B compat-src/build
cmake --build compat-src/build
ctest --test-dir compat-src/build
```

The strict verification used during reconstruction compiles every source with
`-std=c11 -Wall -Wextra -Wpedantic -Werror`. A target A1 build still needs a
licensed BEST1700 BSP plus the unrecovered `dtiot_2800hp` board layer and a
proven recovery path.

## Object-code name matching

`tools/match-elf-objects.py` compares relocatable ARM ELF functions with a
linked image after masking relocation sites. It reports only unique matches
that pass minimum size and stable-anchor thresholds. Its output is evidence
for function naming, not proof that every ABI or board configuration is equal.

```sh
python -m pip install -r requirements-analysis.txt
python tools/match-elf-objects.py OWN_AP_IMAGE.bin LOCAL_OBJECTS/*.o \
  --base 0x10190000 --output analysis/local-object-matches.tsv
```

Never flash an output from this directory. The current milestone is a
no-flash target link and development-board validation.

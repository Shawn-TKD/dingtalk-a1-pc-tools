# DingTalk A1 V1.6.88 firmware modification status

Date: 2026-09-15. This report consolidates the latest owner-controlled device
work from the local research record. It distinguishes what was observed on
hardware from what is still only a static-analysis hypothesis.

## Scope and publication boundary

The work used a device owned and controlled by the researcher. No firmware
image, account material, device identity, raw partition payload, write command
sequence, or recovery credential is included in this repository. This is an
engineering record, not a guide for changing third-party devices.

The public compatible source remains a no-flash project. A target build needs
a licensed board-support package, the missing board layer, and a tested
recovery procedure before it can be considered deployable.

## What is now evidence-backed

### Flash and installed-image model

- The product uses a 16 MiB NOR flash with separate system, AP, companion-core,
  DSP, user-data, configuration, reserved and factory regions. The full
  read-only model is in `FIRMWARE-LAYOUT-ANALYSIS-20260908.md`.
- The AP application executes in place from flash. Its initialized data is
  copied to RAM, but most code and read-only resources remain flash-resident.
  A modification to a live code area can therefore disrupt the running task.
- The packaged OTA path and an already-installed image are different states.
  Package verification observations must not be interpreted as permission to
  alter installed partitions or as a substitute for a recovery plan.

### Controlled write-path characterization

On an owner-controlled test device, a disposable, known-unused region was
used to characterize the maintenance path before any UI experiment:

1. a complete erase-block-sized test buffer was written and read back;
2. a second pattern requiring cleared and set bits was written and read back;
3. an all-erased pattern was written and read back;
4. the surrounding region was read again to confirm the observed erase
   granularity; and
5. the temporary test area was restored to its prior empty state.

The resulting evidence is consistent with erase-and-rewrite behavior at a
4 KiB granularity for that maintenance path. This is a property of the tested
firmware and test device, not a portable programming interface. It should not
be relied on for other revisions or devices without a fresh, controlled test.

### Temporary UI-resource modification and rollback

A single display-resource block on the owner-controlled device was changed
temporarily after the write-path experiment. The change was deliberately
limited to static welcome-screen text, then verified after reboot and restored
from a pre-change backup. The restoration was read back and matched the
original copy.

This establishes three narrow facts:

- a correctly bounded static display resource can be changed on that device;
- persistence requires a reboot-aware verification step because execute-in-
  place caching can retain old content temporarily; and
- a sector-level rollback copy is essential before a modification experiment.

It does **not** establish a safe general firmware-modification workflow,
arbitrary executable-code patching, or a recovery path for failed boot code.

## Practical lessons from the experiment

| Observation | Engineering implication |
| --- | --- |
| AP is execute-in-place | Treat code-bearing areas as unavailable to ordinary experiments. |
| UI strings are in static resources | A fixed-size replacement can still fail visually if the active font lacks glyphs. |
| Display updates may see cached data | Verify persistence only after a clean restart and an independent readback. |
| A safe rollback was possible for one resource block | Backups must be exact and local to the tested unit; do not assume they transfer across devices. |
| Boot and factory areas are not understood well enough | They remain out of scope for any modification work. |

## What remains unproven

- A complete signed/verified update pipeline suitable for a custom image.
- BootROM and bootloader recovery behavior after corruption.
- A public, owner-safe method to restore a failed AP or companion-core image.
- Board-driver coverage for microphone, power, display, Bluetooth, Wi-Fi and
  audio DSP on a replacement build.
- Compatibility of write behavior across firmware versions and hardware
  revisions.

## Recommended next milestones

1. Keep product development on the host-side SDK and BLE/USB features; those
   produce user value without firmware modification.
2. Use the pre-rendered display bitmap path for experiments requiring custom
   text or graphics. It is a data-path investigation, not a flash change.
3. Finish a no-flash target link with a legally obtained matching BSP.
4. Establish a recovery procedure on a sacrificial or development unit before
   considering any executable or boot-path change.

## Related reports

- `FIRMWARE-LAYOUT-ANALYSIS-20260908.md`
- `BOOT-CHAIN-AB-SLOTS-AND-FLASH-MAP-20260912.md` (local research record;
  not published because it contains device-specific operational detail)
- `DISPLAY-AND-BUTTON-ARCHITECTURE-20260915.md`
- `PRODUCT-RECONSTRUCTION-STATUS.md`

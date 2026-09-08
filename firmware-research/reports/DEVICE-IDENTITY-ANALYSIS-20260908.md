# DingTalk A1 V1.6.88 identity and secret boundaries

This note distinguishes two values that stock logs both call a device secret.
They are not interchangeable.

## Protocol `deviceSecret`

AP `0x103dc658` obtains a 16-byte hardware UID through the product platform
callback at offset `0x58`. It then:

1. formats all 16 bytes as 32 lowercase hexadecimal ASCII characters;
2. computes MD5 over those 32 ASCII bytes;
3. formats the 16-byte digest as 32 lowercase hexadecimal characters.

In formula form:

`deviceSecret = lowercase_hex(MD5(lowercase_hex(hardware_uid_16)))`

The BLE challenge path at `0x103ca86c`/`0x103ca9e0`, BLE authentication at
`0x103cb13c`, USB/HID authentication at `0x103e3214`, and audio encryption setup
all use this hardware-derived value. The getter additionally requires product
activation state `2` (`0x103dc3bc`).

The compatible implementation is `a1_device_secret_derive` in
`compat-src/src/auth.c`. It is tested with a known independent MD5 vector.

## DTIOT binding secret

AP `0x103cf954` separately generates 16 random bytes when the DTIOT product is
unbound, prints them as 32 hexadecimal characters for diagnostics, and persists
the raw bytes in a 64-byte DTIOT slot at offset `0`. This is vendor binding state,
not the protocol `deviceSecret` above.

The DTIOT flash HAL exposes a 64-KiB logical data area. Binding fields use
big-endian four-byte integers and fixed slots including:

- `0x000`: generated DTIOT secret, 64-byte slot;
- `0x200`: bind method, first four bytes significant;
- `0x300`: bind status, stock accepts persisted value `2`, otherwise treats it
  as unbound;
- `0x400`: 32-byte device-name slot;
- `0x100` and `0x500`: additional 64-byte binding identity slots whose exact
  public names are not assigned here without stronger callback evidence.

Stock unbind clears `0x000`, `0x100`, `0x200`, `0x300`, and `0x500`, but does not
erase the hardware UID. Serial number, radio MAC addresses, and BT/BLE names use
factory-section APIs rather than these logical DTIOT slots.

## Reflash consequence

An AP application image update by itself should not mathematically change the
protocol `deviceSecret`, because the derivation input is the hardware UID.
However, this does not yet make arbitrary reflashing recovery-safe: erasing the
factory section, activation state, boot metadata, calibration, or signing data
can still prevent the getter or the whole device from working. A full factory
and boot-region backup plus a verified recovery path remains mandatory before
experimental flashing.

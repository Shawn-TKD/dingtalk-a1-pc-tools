# Security policy

## Intended use

Use this project only with an A1 device and DingTalk account you own or are
explicitly authorized to test. The tools deliberately omit reset, delete,
unbind, OTA, firmware-write, and credential-guessing operations.

## Protect `deviceSecret`

`deviceSecret` is an authentication credential tied to one A1. Treat it like a
password:

- never paste it into an issue, screenshot, terminal recording, or bug report;
- keep `.a1-device.json`, app preference XML, HCI logs, and recordings private;
- do not run the local console on a public interface;
- rotate/rebind the device if you believe the credential was exposed.

The repository's `.gitignore` blocks common sensitive artifacts, but it is not
a substitute for reviewing `git diff --cached` before every push.

## Reporting

Do not publish a vendor vulnerability or another person's device data in a
public issue. Contact the vendor through an appropriate security channel and
share only a minimal, redacted reproduction.

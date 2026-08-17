# Security policy

## Intended use

Use this project only with an A1 device and DingTalk account you own or are
explicitly authorized to test. The tools deliberately omit reset, unbind,
OTA, firmware-write, and credential-guessing operations. Single-recording
deletion is limited to one currently indexed recording and requires an
explicit confirmation dialog. Unbacked recordings show a visibly stronger
irreversible-loss warning because the toolkit cannot restore or upload them afterward.

## Protect `deviceSecret`

`deviceSecret` is an authentication credential tied to one A1. Treat it like a
password:

- never paste it into an issue, screenshot, terminal recording, or bug report;
- keep `.a1-device.json`, `.a1-console-token`, app preference XML, HCI logs, and recordings private;
- use LAN mode only on a trusted private network and keep its random access URL private;
- never expose the console with router port forwarding or directly to the internet;
- rotate/rebind the device if you believe the credential was exposed.

The Windows launcher generates `.a1-console-token` once and reuses it across
restarts. Delete that local file while the console is stopped, then start the
console again to rotate the web access token. This token is independent from
`deviceSecret`.

The repository's `.gitignore` blocks common sensitive artifacts, but it is not
a substitute for reviewing `git diff --cached` before every push.

## Reporting

Do not publish a vendor vulnerability or another person's device data in a
public issue. Contact the vendor through an appropriate security channel and
share only a minimal, redacted reproduction.

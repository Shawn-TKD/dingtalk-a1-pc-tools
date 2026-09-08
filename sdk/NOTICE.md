# Sources and attribution

This independent SDK is derived from the contributors' MIT-licensed
`dingtalk-a1-pc-tools` client code (BLE authentication, file download, live capture,
Ogg framing) and the same owner's `a1-firmware-lab` USB interoperability work.
The algorithms are repackaged with transport separation and event routing.

Protocol research was informed by
[AwHsR15/dingtalk-a1-reverse](https://github.com/AwHsR15/dingtalk-a1-reverse),
including its `main` and `findings/h5-and-processing-architecture` branches.
This package does not copy or relicense that upstream repository's source or prose.
Official firmware, decompiler databases, private credentials and recorded audio are
not included. Firmware ownership remains with its respective owners.

"DingTalk", "A1", and related marks belong to their respective owners. This SDK is
not affiliated with, endorsed by, or a replacement firmware distributed by DingTalk.

Opus packet-duration facts follow RFC 6716; the SDK implements a small independent
framing helper. The optional PyAV/FFmpeg, Bleak, hidapi and cryptography dependencies
retain their own licenses and are not bundled as source in this archive.

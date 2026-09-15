# DingTalk A1 display and button architecture update

Date: 2026-09-15. These findings come from static analysis of V1.6.88 plus
visual checks on an owner-controlled device. They describe user-interface
behavior and intentionally omit device-specific addresses and patch material.

## Display renderer

The firmware contains a named display-content update path and a catalog of
debug messages that exposes the architecture more clearly than an isolated
decompilation:

- the screen is an RGB565 OLED canvas, 240 by 120 pixels;
- UI content is selected from localized state tables rather than hard-coded in
  each event handler;
- the default-language table has 44 entries and the Chinese table has 33
  effective entries;
- the renderer uses several layout families, including a status-text-image
  layout; and
- state resources include string references, icons and image descriptors.

This complements the earlier `DISPLAY-STATE-ANALYSIS-20260908.md`, which
identified system-level states such as boot, recording, transfer, charging
and low-battery conditions.

## Text and bitmap paths are different

The product ships subset bitmap fonts. A string can fit in its reserved
storage and still render with missing-glyph squares. Consequently, arbitrary
names or mixed-language phrases are not reliably expressible by replacing a
firmware string.

The existing user-information display flow also accepts a pre-rendered RGB565
bitmap with a fixed 192 by 32 pixel content area. It is drawn at 1:1 scale by
the status-text-image layout. For owner-facing personalization experiments,
this bitmap route is the reliable visual approach because the desired text is
rendered before it reaches the device. The repository publishes no device
path, transfer procedure or owner asset.

## Button event semantics

Analysis of the button state machine resolved four high-level event classes:

| Event | Observed behavior |
| --- | --- |
| AI key single press | Wakes the display when it is off and emits a UI log; it does not select a new UI state. |
| Record key press | Routes to normal recording start/stop handling. |
| AI key hold | Routes to the voice-memo or interactive-stream behavior selected by connection state. |
| AI key double press | Routes to the existing association/user-information display flow when the required local resource exists. |

The important product consequence is that the single press is an excellent
low-friction wake/acknowledgement input, while the held AI key remains the
natural push-to-talk input for an agent bridge. No custom firmware is required
to build an agent product around that behavior.

## State-machine open questions

Three points remain intentionally unresolved:

1. the complete runtime dispatch from logical UI identifier to layout handler;
2. the producer of the runtime text field used by the status-text-image
   layout; and
3. the test-only state-cycling entry observed in display diagnostics.

They are useful research targets, but they are not prerequisites for the
host-side SDK, live-audio bridge, file export or agent workflow.

## Product implication

The safest near-term architecture is:

```text
A1 physical button -> authenticated host bridge -> speech/agent workflow
                                           -> host notification or existing A1 feedback
```

It keeps irreversible firmware work out of the user workflow. Future
display-personalization research should use a separate owner-controlled test
unit and a verified rollback route.

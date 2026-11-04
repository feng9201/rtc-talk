# BubbleMsg integration

Copied from `D:\work\device_tool\omni_station\base_ui\msg_box` with its close icon.
This path documents provenance only; build and runtime use this repository's copy.

The original BubbleMsg/BubbleMsgWithClose/MulBubbleMsg/SigMulBubbleMsg API and
color scheme are retained. Integration adjustments: plain-text selectable labels,
multiline sizing, resize positioning, floating-point anti-aliased border, and
close-button sizing independent of the application's global button stylesheet.

MainWindow owns one BubbleMsgWithClose per window (no singleton). A failed join
attempt shows a persistent error bubble; close dismisses it, and retry clears the
previous message. Generic device/preview errors outside a join retain their
existing status handling. The error bubble never applies a graphics effect to
the parent window or its OpenGL video widgets.

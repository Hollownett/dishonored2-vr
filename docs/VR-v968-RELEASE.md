# VR v968 (2026-11-03)

v968 is v967 plus one attachment-gate correction found while checking the v965 session's arm counters.

- The one-tick-behind arm publication now also accepts a **moved model frame** (`frame_current(..., exact)` instead of
  the literal `true`). The exact source still must match its model frame; the grace source only has to be the same
  animation, the same epochs and within 100 ms. This is the last gate that could still drop a valid one-behind palette
  after `source_in_grace` accepted it.
- Everything else is v966/v967: the tick-only grace (no flicker fallbacks), the raw native Talk/Pickpocket offer on the
  trigger, and the **Trig** prompt glyph.

DLL: `build/d2vr_render_probe_arm_v968.dll`, SHA-256
`a6f5a43283c46975d8f6e1679ca17a374abf199a3a9a14fb8efddc36ce4d88c2` (14,067,712 bytes).
Launcher: `DishonoredVRLauncher-v968.exe` (Downloads), embedding v968.

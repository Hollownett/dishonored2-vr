# VR v967 (2026-11-03)

v967 is the v966 code rebuilt from the committed tree. The DLL hash changed because a fresh PE timestamp is embedded;
the source and the two fixes are the same as [VR-v966-RELEASE.md](VR-v966-RELEASE.md):

- arm publications one **tick** behind the policy inside the 100 ms grace are accepted (the old gate also required the
  revision to be behind, which never happens; that caused ~3200 hand/weapon fallbacks and the flicker),
- Talk/Pickpocket use the **raw native primary/secondary action** rather than the usually-absent strict receipt, and the
  native prompt's X glyph is painted **Trig** while that offer is live.

DLL: `build/d2vr_render_probe_arm_v967.dll`, SHA-256
`a9cfecb5202d9736f8352a667adc578e4d510fc9ebc412188af039dc495791a2` (14,067,712 bytes).
Launcher: `DishonoredVRLauncher-v967.exe` (Downloads), embedding v967.

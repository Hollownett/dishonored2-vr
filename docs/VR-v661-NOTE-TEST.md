# v661 headset test, 2026-10-28

The user confirms that the latest tested launcher was **v661**. Preserve this
numbered DLL and launcher as the tested baseline; do not relabel its results as
another version.

- DLL SHA256: `8761be8ec7893274522b592b19bf2541cab4203fe3a8ea09d25eb8bc13e66b43`.
- Installed launcher run: PID 20576, started 15:34 in the launcher's timestamps.
- Evidence copied from `%LOCALAPPDATA%/DishonoredVR/artifacts` into
  `artifacts/note-v661-headset-20261028/`.
- The final status records two accepted native shows, 934 reader frames,
  930 ready frames and 1864 eye compositions. These counters establish activity,
  not acceptable hand appearance.

User result: reading draws another pair of hands/sleeves over the existing
player body. The reader must reuse the existing player body and arms. The wrist
must move, and page contact must use that same visible pose.

Source cause: `GameXr` poses imported hand/sleeve geometry and draws
`NoteHandArt` after copying the scene into each eye. The ordinary native avatar
is still present. Native animation phases stop during the note menu, so removing
the second draw alone would leave the original arms frozen.

Repair requirements: retain the original game page, controller placement,
content, pause and controls; feed page grip/finger targets through the existing
body solver; substitute only the exact original models' render skinning
resources in an owned graphics draw scope; restore all bindings after that
draw. Do not invoke the animation scheduler or mutate native model buffers from
Present. Original gameplay movement and turning remain the baseline.

The archive also contains a first-chance access violation recorded in
`vrclient_x64.dll`. This record alone does not establish its cause or whether
the exception was fatal.

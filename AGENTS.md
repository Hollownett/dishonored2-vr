# Development preferences

The user authorizes focused helper agents for this project, with up to five
helpers when the runtime permits. Respect the runtime's concurrent-agent limit.
Prefer GPT-6.1 (`gpt-6.1-sol`) at low or medium reasoning effort. GPT-6 Luna
(`gpt-6-luna`) is appropriate for routine work and routine checks. Keep delegation
bounded and avoid overlapping edits or simultaneous control of the live game.

The current goal is a polished, feature-complete first VR iteration. Preserve
user-accepted locomotion and turning. Research primary public references and
verify native ownership and ABIs before engine changes. Automated or simulated
checks do not establish headset visual quality, comfort, or complete gameplay.

# Headset launch policy

When the user requests VR, launch the reviewed candidate directly with
`-SkipPreflightWait`. Do not run separate headset-availability probes or require
headset-detection confirmation before launching; the user reports that Virtual
Desktop has a detection bug. Preserve required OpenXR initialization and recovery.
Do not equate a loaded DLL with successfully running headset presentation.

# Live game access for helper agents

Helper agents may READ the running game at any time, even while the user plays.
That covers reading its memory from outside the process (ReadProcessMemory-based
tools such as `tools/entities.py` and `tools/sample_threads.py`), the mod's status
and artifact files, and the mod's own in-DLL probes. Verify engine assumptions
(struct layouts, per-view vs shared data, call order) against live memory instead
of relying on static analysis alone.

Without the user's explicit go-ahead in the current conversation, helper agents
must not:
- inject into, write to, suspend, or debug-attach to the game;
- send input to the game;
- launch or close the game;
- touch saves or the game config;
- change the mod's build knobs in `%LOCALAPPDATA%\DishonoredVR\build`.
The coordinating agent owns those actions and checks that the user is not playing
first.

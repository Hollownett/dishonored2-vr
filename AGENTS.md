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

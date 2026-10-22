# Existing Dishonored 2 DLSS integrations

Checked 2026-10-22. Read-only public-source research; no binaries downloaded or installed, and no game, VR, simulation or GPU test launched.

## Luma Framework

The official [Build 758 release](https://github.com/Filoppi/Luma-Framework/releases/tag/latest-758), published 2026-10-21, includes `Luma-Dishonored_2.zip` and a separate test package. The release asset listing supplies SHA-256 `a47152e6e10c233a2ae6db3c09b89d753f24d662d9aaebce8ec689ec2ba6252b` for the normal package; its bytes were not downloaded or verified here.

The [OptiScaler project's Dishonored 2 instructions](https://github.com/optiscaler/OptiScaler/wiki/Dishonored-2) explicitly identify Luma's DLSS input and its Super Resolution selection. OptiScaler is an additional compatibility layer in that setup, not evidence that the unmodified game has DLSS.

Inspected Luma's [game-specific source at c24305e](https://github.com/Filoppi/Luma-Framework/blob/c24305e8c561ed904edfab24d7427c2c97a3d246/Source/Games/Dishonored%202/main.cpp):

- Native TAA interception reads motion at CS `t1`, pre-TAA color at `t2`, output at `u1`, and TAA constant buffers `b1/b2` (lines 591–608).
- Deferred work is split; the preserved prefix executes before delayed SR on the immediate context (lines 940–974).
- The delayed path configures inverted depth, linear/HDR input, automatic exposure and negative render-dimension MV scales. Projection coefficients are converted to pixel jitter, then color/MV/depth/reset data is passed to the SR implementation (lines 1013–1051).

These are useful engine-specific reference mappings, not proof that the VR mod's resource contents, scaling or histories match. The inspected D2 path requests the default SR instance without a logical-eye key; its code alone does not establish stereo or alternate-eye history isolation. Preserve our per-eye state, retained-job ownership and failure containment.

Luma's [custom MIT license](https://github.com/Filoppi/Luma-Framework/blob/c24305e8c561ed904edfab24d7427c2c97a3d246/LICENSE.md) includes attribution requirements and an additional commercial-use permission condition. No code was copied; review applicable terms before reuse. NVIDIA components retain separate terms.

## Dishonored2GraphicalUpgrade

The author's [README at 28b8751](https://github.com/garamond13/ReShade-shaders/blob/28b87514963494f54063a88f45192263e6f4ad1b/Addons/Dishonored2GraphicalUpgrade/README.md) documents optional DLSS/DLAA replacing TAA. It requires ReShade 6.7.3 or newer with full add-on support, the add-on, a DLSS provider DLL and its GraphicalUpgrade directory. It also requires the game's Adaptive Resolution option to be off. This README establishes the advertised integration; its runtime and source implementation were not inspected here.

## Consequence for our plan

Use Luma as the first existing game-specific reference for bindings, jitter/MV conversion, color/depth handling and deferred scheduling. Compare those details against the current VR renderer before implementing new bindings. The SDK remains the API/lifecycle authority. Both existing mods use ReShade; installing or chaining either alongside our injection is a separate compatibility experiment, presently unperformed. Existing flatscreen support does not establish VR image quality, comfort or complete gameplay.

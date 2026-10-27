# v637 test build: hands kept on camera modes, performance mode off by default

Headset status: not run. Built on v636. User feedback on v636: the corpse carry, thumb and wrist are good. Two problems remain:
- in performance mode the right eye shows a double image for the first few minutes;
- a hand (right or left) sometimes vanishes, for example on a blocked attack.

## Hands vanishing (fixed)

The v636 session logged 1780 frames per arm in which the tracked arm pose was refused (`arm_flicker` `why` publish_refused). The refusals happened for both arms at once.

The mod handed **both** arms to the game's animation whenever the native camera left its gameplay mode, unless a known combat state was active (`authored_hands = authored_camera ? 3 : 0`). The session shows 28 such camera episodes of 0.5 to 2.4 s, among them mode 14 (0.5-0.8 s, not scripted) around sprints, slides, landings and fights. On those frames the hands left the controllers and seemed to vanish.

Now both arms follow the camera only into a scripted scene (takedown, finisher, mantle: `NativeActionCamera::takes_hands`) or a cutscene. Player states still take arms through the ownership table (Stun, Stumble, Versus, ...). The knob `build/legacy-authored-hands` restores the old rule.

## Performance mode double image (default changed)

Performance mode draws one eye per frame. In the v636 session it ran from about 13:32 to 13:35, then fell back to rendering both eyes when the engine's view count disagreed with the plan. That matches "the first few minutes", after which the double image was gone.

The pose/image pairing was checked in the sim with `build/alt-pose-trace` and `tools/alt_pose_trace_check.py`:
- 6801 one-eye frames at 138 fps: 0 mismatches;
- 7644 one-eye frames at 105 fps (4400x4700 eyes): 0 mismatches.

Each eye carries its own image and pose. The remaining effect is the nature of alternate-eye rendering: the two eyes show the world one game frame apart. At headset frame rates (about 44 fps paired in this session) this reads as a double image when you move or turn. Head rotation is reprojected, but stick turning, walking and animation are not. UEVR's documentation says the same of AFR: the eyes are not synchronized.

Performance mode is now **off by default** for new installs. An existing install keeps its choice: turn it off in the launcher under Settings → Features → "performance mode".

## Headset checks

1. In the launcher, turn performance mode off, then start. There should be no double image in either eye from the start.
2. Fight: block attacks, get hit while blocking, sprint, slide (sprint + crouch), jump and land, stand up from a crouch. Both hands should stay in your hands.
3. Takedowns, mantles and cutscenes still play the game's own arm animation.

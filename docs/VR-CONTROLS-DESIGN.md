Use **seven illustrated journal pages**, with **sword throw and enemy grab together on the opening page**. Keep the existing physical card size and opening gesture. Replace the dense text columns with large instructional sketches and short, readable text.

This specification incorporates a GPT-6.1 Sol design review. No files were changed.

**1. Visual direction and in-game layout**

The visual language is **the game’s journal mounted inside its pause menu**: dark leather and ink around the page, warm parchment behind illustrations, charcoal contours, ivory text, restrained gold headings, and cyan only for actionable inputs.

Use the imported art as material and ornament. The instructional pictures should depict the actual gesture; a character portrait cannot substitute for demonstrating hand placement.

All coordinates below are `(x, y, width, height)` in the 1920×1200 image.

| Element | Placement | Treatment |
|---|---|---|
| Card | `(16,16,1888,1168)` | Opaque `#100F12`; transparent outer corners, 24 px radius. |
| Background | Card bounds | `dark_page`, centre-cropped, 24% opacity over the solid fill. |
| Text backing | `(48,244,1824,824)` | `menu_background`, centre-cropped at 18%, under an additional dark fill. Texture must remain subordinate to text. |
| Title | `(64,40,1120,68)` | “VR controls and gestures”; 56 px Palatino Linotype, Georgia fallback; ivory. |
| Current page | `(1200,54,656,48)` | Page name and `1 / 7`; right aligned, 32 px Segoe UI. |
| Controller explanation | `(64,116,1792,84)` | 32 px Segoe UI; 40 px line spacing; muted ivory. |
| Header divider | `(64,216,1792,20)` | `divider`, using its existing stretchable-rule treatment; muted gold. |
| Footer divider | `(64,1080,1792,10)` | Same asset, reduced opacity. |
| Seven page selectors | Seven equal 256 px cells, starting `(64,1098)`; height 44 | 26 px labels. Active page uses `highlight_brush`, ivory brush with dark lettering. |
| Navigation reminder | `(64,1148,1792,36)` | 28 px: “Right stick ← / →: pages · Right temple + grip: close”. |

Use these colours consistently:

- Body: `#E8E3D8`
- Secondary text: `#C6C0B4`
- Input phrases: `#7CCADA`
- Section headings and rules: `#CEAE70`
- Illustration ink: `#292723`
- Parchment base: `#CEC0A0`
- Motion/contact accent: muted rust `#94594A`

Do not apply texture, edge filtering, or distressed lettering to instructions.

There are two layout templates:

| Template | Exact layout |
|---|---|
| **Two lessons** | Left column `(64,260,864,804)`; right column `(992,260,864,804)`. Each has a 48 px section heading, illustration `(columnX,324,864,232)`, then text starting at `(columnX,580)`, ending before y=1064. |
| **One illustrated lesson** | Illustration `(64,260,864,804)`; text column `(992,260,864,804)`. Section heading occupies the first 48 px; text starts at y=324. Additional sections continue after measured text plus 28 px. |

Body and bold inputs are **32 px Segoe UI**, with 40 px line spacing and 8 px between bullets. Section headings are 38 px. Never reduce the font to resolve overflow.

| Page | Template | Contents |
|---|---|---|
| 1 — Throw & grab | Two lessons | Sword throw; grabbing living enemies |
| 2 — Sword & stealth | Two lessons | Drawing, blocking and sword handling; silent kills and choking |
| 3 — Moving | One illustrated lesson | Movement, jump, crouch and lean |
| 4 — Hands | One illustrated lesson | Interaction, props, throwing and doors |
| 5 — Bodies & weapons | Two lessons | Body handling; left-hand weapons and powers |
| 6 — Wheel & journal | Two lessons | Wheel and wrist HUD; temples and journal |
| 7 — Menus & settings | One illustrated lesson | Menu controls, settings and cutscenes |

At the existing 1.1×0.6875 m size and 0.85 m distance, the card spans approximately **66° horizontally and 44° vertically**. A 32 px font em occupies approximately **1.24° vertically at the centre**, compared with 0.85° for the present 22 px text. Actual lowercase letters occupy less than that, so these figures justify larger text but do not establish headset readability.

Seven pages allow pictures without shrinking the text or increasing the panel’s angular size. Each page remains a static image; there is no page-turn animation.

Navigation behavior:

- Open on page 1 every time.
- Right-stick horizontal flick changes one page; stop at the first and last pages.
- Trigger at `|x| ≥ 0.65`; rearm after both axes return below `0.25`. No held-stick repeat.
- While the guide is open, its right-stick axes and click belong to the guide. They must not also turn, crouch, zoom, or navigate the underlying pause menu.
- Require neutral before returning that stick to gameplay after closing.
- Preserve existing left-stick movement and all normal controls while the guide is closed.
- Right-temple grip closes the guide from either entry route, including when opened over VR Settings. Preserve the existing shoulder-holster priority and grip consumption.
- Preserve the existing world-fixed placement while open. Changing pages must not reposition the card.

**2. Illustration production**

Generate instructional pictures locally through `PoseRender`; use GDI overlays for objects, arrows and schematic people. No external image service, new rendering package, or guard-body extraction is required.

Extend the renderer’s existing calibration-only interface with explicit hand targets, hand orientation, finger curls, camera and crop. Leave its current calibration presets unchanged.

Use this illustration coordinate convention:

- Metres, floor origin; eye at `(0,0,1.75)`.
- `+X` forward, `+Y` player’s left, `+Z` up.
- Targets refer to `hand_attachment`, converted through the existing `FromWorld` path.
- `D` is the uncurled finger direction; `N` is the palm-facing normal. Build orientation through the renderer’s existing bind-frame mapping.
- Camera azimuth 0° looks at the player from the front; positive azimuth moves toward the player’s left. Elevation is positive above the subject.
- Unspecified hands rest at `L=(0.18,0.22,1.05)`, `R=(0.18,-0.22,1.05)`.

Finger curl values interpolate the existing `Relaxed` and `Closed` tables, in thumb/index/middle/ring/little order:

| Profile | Curl values |
|---|---|
| Open | `(0,0,0,0,0)` |
| Holding | `(0.8,0.85,1,1,1)` |
| Grip alone | `(0.75,0.15,1,1,1)` |
| Pointing | `(0.65,0,1,1,1)` |

These are illustration presets, not changes to live finger behavior.

Produce the following **11 composite illustrations**, each in Corvo and Emily variants:

| Figure ID | Pose, camera and crop | Overlays and teaching purpose |
|---|---|---|
| `sword_throw` | Right hand moves from `(0.12,-0.45,1.62)`, Holding, to `(0.58,-0.18,1.38)`, Open. Preparation: `D=+Z,N=+Y`; release: `D=+X,N=-Z`. Camera azimuth −55°, elevation 8°; waist-up crop. | Two small sequential views: swing, then open hand. Draw an original schematic sword leaving the hand, three successively rotated blade outlines, and a flight arrow. A small wall/floor inset shows a glance and flat landing. |
| `enemy_grab` | Right palm at `(0.58,-0.06,1.38)`, `D=+Z,N=+X`, Grip alone; second pose retracts to `(0.28,-0.16,1.30)`, same orientation. Camera −65°, elevation 6°; chest/forearm crop. | Generic coat-and-head outline facing the player, chest at approximately x=0.63. Palm must visibly meet the chest. First frame shows contact and grip; second shows the pull toward the player and a toppled outline. A small alternate inset shows Grip + trigger. |
| `sword_draw_block` | Draw: R=`(-0.08,-0.27,1.60)`, `D=+Z,N=+Y`, Holding; rear-right camera −140°, elevation 8°. Block: R=`(0.35,-0.08,1.32)`, same orientation, Holding; camera −15°, elevation 5°. Crop shoulders to waist. | Separate draw and block panels. Show the draw route behind the shoulder, clearly away from the temple. Add a schematic blade across the front and two faint alternative blocking angles. |
| `stealth` | Kill: R=`(0.50,-0.05,1.23)`, `D=+X,N=-Z`, Holding. Choke: L=`(0.52,0.08,1.56)`, `D=-Y,N=+X`, Holding. Camera −90°, elevation 8°. | Two side views, with a schematic guard facing away. One shows the blade entering from behind/side; the other shows the left hand at the neck. No frontal-grab imagery in the choke panel. |
| `move_jump_lean` | Jump: L=`(0.03,0.26,2.03)`, R=`(0.03,-0.26,2.03)`, Open, `D=+Z`, palms inward. Camera 0°, elevation 0°; knees-to-fingertips crop. | Upper portion: both empty hands rising above the head. Lower portion: original line diagrams of head lean around a corner and standing/crouched silhouettes. Do not imply that the current arm-only renderer provides head or torso posing. |
| `hands_push` | Door: R=`(0.55,-0.18,1.35)`, Open, `D=+Z,N=+X`. Camera −60°, elevation 10°; large hand/forearm crop. | Main panel: palm touching a hinged door with small and large swing arcs. Lower insets: schematic bottle held by a curled hand; released object with throw arc. |
| `bodies_drag_carry` | Drag: R=`(0.35,-0.24,0.78)`, Holding, `D=+X,N=-Z`; crop to arm. Carry: R=`(0.04,-0.28,1.50)`, Open after release, `D=+Z,N=+Y`; upper-body crop. Camera −75°, elevation 10°. | A schematic prone body has its wrist lifted to the hand, with a drag arrow. Second view places the body across the right shoulder with the hand visibly free. The same original body outline is reused throughout. |
| `left_hip` | L=`(0.06,0.26,0.94)`, Holding, `D=-Z,N=-Y`. Camera +35°, elevation 5°; left shoulder to upper thigh. | Circle the left hip and draw an upward draw arrow. Add a small original pistol/crossbow outline beside the hand, without claiming it is a rendered game weapon. |
| `wheel_watch` | Point: L=`(0.50,0.16,1.25)`, Pointing, `D=+X,N=-Z`. Watch: L=`(0.34,0.13,1.43)`, Open, `D=+Z,N=+X`; back of hand toward the face. Point camera +35°; watch camera +155°, elevation 15°. | Schematic radial wheel and pointing ray; separate enlarged wrist-back view with two simple status bars. Keep button instructions in adjacent text. |
| `temples` | L=`(0.05,0.13,1.71)` or R=`(0.05,-0.13,1.71)`, Holding, `D=+Z`, palm inward. Camera 0°, elevation 0°; head/shoulders crop. | Separate left and right poses. Original book symbol beside the journal hand; small guide-card symbol beside the controls hand. Preserve anatomical left/right when viewed from the front. |
| `menu_point` | R=`(0.52,-0.16,1.30)`, Pointing, `D=+X,N=-Z`. Camera −30°, elevation 8°; upper-body crop. | Hand points at a simple dark menu panel with one brush-highlighted row. Add a small trigger close-up. This is an instructional diagram, not a fabricated screenshot of native settings. |

For the opponent, use an **original faceless ink outline**: head oval, neck, coat collar, torso and articulated limbs. Do not repurpose Corvo’s masked head as a guard or trace the painted fight scene into an invented guard model. The contrast between a detailed player hand and a simplified target makes the contact point clear.

For weapons, use original instructional outlines. `PoseRender` currently does not supply a sword mesh; locating and adding one is unnecessary for this design.

Apply one deterministic sketch treatment during local generation:

1. Render at twice the destination dimensions, retaining alpha, depth and normal buffers.
2. Reduce shaded colour to three sepia values, with a light wash rather than photographic textures.
3. Form contours from the silhouette plus depth/normal discontinuities. Target 2–3 px final contour thickness.
4. Add sparse diagonal hatching to the darkest band; avoid noise on fingers.
5. Render prior positions at 25% ink opacity. Current pose is full opacity.
6. Downsample once with the existing pipeline.

Use charcoal for movement arrows on parchment, rust for the contact point, and small numbered steps where needed. Do not bake explanatory words into the pictures.

Each illustration is composited over `parchment`, centre-cropped and blended to the specified base colour. Add `paper_texture` at 5% opacity. Use `ink_flourish` only along an illustration’s lower edge, at 8%; it must not cross the gesture.

Do not use `ink_splatter`, `story_art`, large portraits, or key art behind instructions. Existing `sketch_corvo` and `sketch_emily` establish the stylistic reference; they are not gesture diagrams.

Generated filenames:

`guide_<figure-id>_<corvo|emily>.png` and `.rgba`

Write them into the existing ignored `artifacts/game-art` directory and register them in `manifest.json`. Store generation version and source identity there so stale illustrations can be regenerated. Preserve all four existing calibration outputs.

Use the current game character in VR. Before character identity is known, use Corvo; selecting Emily invalidates the affected cached pages. The launcher gets a small Corvo/Emily selector above the guide, defaulting to Corvo.

**3. Launcher Controls page**

Keep the existing window backdrop, main navigation and imported-art workflow. Replace the Controls RichTextBox’s visual role with a scrollable collection of illustrated lessons.

At 96-DPI logical dimensions:

- Controls pane starts at x=300, y=176.
- Its width is `min(1040, clientWidth−324)`; height remains `clientHeight−202`.
- Keep a 24 px internal margin.
- Reserve 72 px at the top for title and controller explanation.
- Place seven page-selector buttons below it, 34 px high, wrapping if required.
- Reserve 28 px after the selectors before lesson content.
- Character selector sits alongside the page controls and wraps onto its own row when necessary.

For content widths of at least 880 logical pixels, show two lesson columns, with a 28 px gap. Below that width, stack lessons vertically. Each lesson uses:

- 18 pt serif section title;
- 180 logical px illustration height;
- 11.5 pt Segoe UI body text, measured at current DPI;
- bold cyan inputs and ivory explanations;
- the same parchment and sketch assets as VR.

Use native text at desktop resolution. Do not display the entire VR card as a scaled bitmap.

Keep vertical scrolling for shorter windows, resizing and larger DPI settings. Reset to the top when changing pages. Page controls are normal focusable WinForms buttons; Left/Right changes pages only while focus is within the guide, and mouse wheel scrolls content.

Provide an **“All controls”** text view using the same parsed document and the existing read-only RichTextBox. This retains selectable/copyable text and a straightforward accessible fallback. It is a presentation mode, not a second source of instructions.

Reload the guide’s art when `ImportArt` completes, including when Controls is already visible.

**4. Document syntax and complete replacement text**

Add only two standalone HTML-comment directives:

```text
<!-- vr-page:signature|Throw & grab -->
<!-- vr-figure:sword_throw|Release grip during the swing. -->
```

The page directive starts a page and supplies its ID and display name. A figure directive belongs to the preceding `##` section; its second field is descriptive alternative text.

Both parsers must:

- Recognize these directives before processing ordinary text.
- Accept only known page and figure IDs; never interpret IDs as paths.
- Preserve every heading and bullet when art is absent.
- Ignore unrelated HTML comments.
- Treat malformed directives as a validation error in checks; omit them safely at runtime.

When a figure is missing, draw a flat, untextured illustration area with its alternative text in 32 px ivory. Do not show a broken-image symbol or an import error inside the lesson. The adjacent instructions remain complete. With no imported art at all, use solid dark panels, gold rules and these text alternatives.

The comments disappear naturally in ordinary Markdown. README synchronization copies the title/intro, headings and bullets, demoting headings as it does today; it omits the directives.

The full replacement for `docs/VR-CONTROLS.md` is:

```markdown
# VR controls and gestures

Quest Touch controllers. Grip is the side button under your middle finger; trigger is under your index finger.

<!-- vr-page:signature|Throw & grab -->

## Sword throw
<!-- vr-figure:sword_throw|Swing the sword, release grip, and let it fly. -->
- **Release right grip mid-swing** — throw the sword. It tumbles, glances off walls and lands flat.
- **Hit an unaware guard** — one sword throw kills him. An aware guard takes a normal sword wound.
- **Grip next to the dropped sword** — pick it up. Right grip over your right shoulder calls it back.
- **Leave the sword behind** — it returns by itself after 30 seconds or when you are 12 metres away.

## Grab an enemy
<!-- vr-figure:enemy_grab|Palm on the guard: grip to hold, then pull hard away from him. -->
- **Palm on a living guard + grip** — hold him. He staggers and notices you.
- **Keep gripping and pull hard away from him** — throw him down. He gets up about 4 seconds later.
- **Grip + trigger on his chest, collar or arm, then yank** — also throws him down.

<!-- vr-page:sword|Sword & stealth -->

## Sword on your back
<!-- vr-figure:sword_draw_block|Draw behind the right shoulder; block with the blade in front. -->
- **Right grip over your right shoulder** — draw; keep squeezing to hold.
- **Fist up or down as you draw** — normal or reverse grip.
- **Swing or stab** — hit where the blade touches.
- **Sword in front, any angle** — block. Right trigger blocks too.
- **Release over your right shoulder** — sheathe. Release elsewhere to drop.
- **Blade on a corpse** — cut off limbs and the head.

## Stealth and choking
<!-- vr-figure:stealth|Attack an unaware guard from behind or the side; grip his neck from behind to choke. -->
- **Stab or slash a calm guard from behind or the side** — silent kill, with a strong pulse in your hand.
- **Grip an unaware guard's neck from behind** — keep holding to choke him unconscious. With the sword out, use your left hand.

<!-- vr-page:moving|Moving -->

## Moving
<!-- vr-figure:move_jump_lean|Raise both empty hands to jump; lean your head to look around a corner. -->
- **Left stick** — move where you look; click to sprint.
- **Right stick left / right** — snap turn. Choose smooth turn in VR Settings.
- **Right stick down** — crouch or stand. Crouching in real life crouches too.
- **A** — jump, climb up a ledge or swim up.
- **Throw both hands up over your head** — jump, with hands empty and triggers released.
- **Lean your head sideways** — lean around a corner.
- **Right stick click** — zoom.

<!-- vr-page:hands|Hands -->

## Hands and objects
<!-- vr-figure:hands_push|Touch a door with an open palm to push; grip objects to hold them. -->
- **Grip** — use what your hand points at or touches: doors, keyholes, levers, loot and coins.
- **Grip a bottle, glass or severed part** — pick it up in that hand.
- **Grip a prop** — hold books, crates, chairs or candlesticks. Grip + trigger works too.
- **Let go** — drop it. Release while swinging to throw; a hard throw hits a guard.
- **Open hand** — push light objects or swat a bottle off a table.
- **Open palm on a door** — push slowly to peek, or quickly to swing it wide.

<!-- vr-page:bodies-weapons|Bodies & weapons -->

## Bodies
<!-- vr-figure:bodies_drag_carry|Grip a limb to drag; lift and release at your shoulder to carry. -->
- **Grip a wrist, ankle or head** — drag the body.
- **Lift a body and release at your shoulder** — leave it on your shoulder with your hand free.
- **Grip at that shoulder again** — take it back. Release while swinging to throw it.

## Left-hand weapons and powers
<!-- vr-figure:left_hip|Draw the pistol or crossbow at your left hip. -->
- **Left grip at your left hip** — draw the pistol or crossbow. Keep squeezing to hold; release to holster.
- **Left trigger** — fire or cast, aimed by your left hand.
- **Grenade in your left hand** — hold trigger to cook it; swing your hand to throw.
- **Hold B** — put all weapons away.

<!-- vr-page:wheel-journal|Wheel & journal -->

## Wheel and wrist HUD
<!-- vr-figure:wheel_watch|Point into the wheel; turn the back of your wrist toward your face to read the watch. -->
- **Hold X** — open the Quick Access Wheel. Point with either hand; release X to equip.
- **While the wheel is open** — A uses a mana elixir, B a health elixir; Y hides or shows it.
- **Menu + right stick** — assign the wheel item to a D-pad shortcut.
- **Back of either wrist toward your face** — read your health and mana watch.

## Temples and journal
<!-- vr-figure:temples|Your left temple opens the journal; your right temple opens this guide. -->
- **Left temple + grip** — open the journal for objectives and notes. Holding Menu opens it too.
- **Notes and the Black Market** — appear in your left hand.
- **Right temple + grip** — open this guide. Repeat to close.
- **Right stick left / right while this guide is open** — change its page.

<!-- vr-page:menus-settings|Menus & settings -->

## Menus
<!-- vr-figure:menu_point|Point with the right controller and press trigger to select a menu item. -->
- **Y on a Talk prompt** — talk or trade.
- **Hold Y** — open the pause menu. Hold Y again while paused for VR Settings.
- **Menu** — pause.
- **In menus** — point with the right controller; trigger selects. Grip changes the tab.

## VR Settings and cutscenes
- **Pause, then VR Settings** — adjust resolution, anti-aliasing, sharpening, HUD size and turning; recenter, calibrate or open this guide.
- **Cutscenes** — play in 3D around you. Your hands and body stay when the scene uses your character.
```

Keep the approximate **2.2 m/s** yank threshold in developer notes and checks, rather than player-facing instructions. “Pull hard away from him” teaches the action without suggesting that the player must measure speed.

**5. Implementation boundaries and build order**

Use the existing drawing, asset and overlay paths.

| File | Required change |
|---|---|
| `docs/VR-CONTROLS.md` | Replace with the document above. |
| `README.md` | Update the synchronized text section, excluding rendering directives. |
| `src/controls_guide_image.hpp` | Extend `GuideItem` and `parse_controls_guide` for pages and figures. Add page-aware layout and drawing; return measured bounds and overflow information. Reuse `blit_art`. |
| `launcher/ControlsGuide.cs` — new | C# document parser and guide control, with page selection, responsive lesson layout and text-only mode. |
| `launcher/Program.cs` | Replace visual `LayoutGuide` rendering with the guide control; retain the RichTextBox for All controls. Wire import completion and review flags. |
| `launcher/PoseRender.cs` | Add explicit teaching-pose inputs and `RunGuideFigures`; preserve calibration output behavior. |
| `launcher/GuideFigures.cs` — new | Fixed pose definitions, sketch treatment and original schematic overlays. Reuse the existing rasterizer and PNG/RGBA writers. |
| `src/game_art.hpp` | Add explicit cache invalidation/reload support, including previously missed keys. |
| `src/game_xr.hpp` | Store selected page, cache page pixels and revision; update `ensure_hint_chain`/`hint_layer` upload behavior. |
| `src/dxgi_probe.cpp` | Route guide navigation before right-stick consumers; preserve temple/shoulder precedence and normal closed-guide input. |
| Existing tests, plus one focused navigation check | Cover parsing, layout, page upload revisions, input ownership and document parity. |

The present code marks each swapchain image filled once and uses static overlay content. Pagination must replace that assumption:

- Associate rendered content with `(page, character, artGeneration)`.
- Increment a content revision when any of those changes.
- Track the revision uploaded to **each** swapchain image, rather than a single permanent filled flag.
- Include that revision in the overlay content key.
- Regenerate only the changed page and upload only when needed.
- Keep all swapchain creation/destruction outside the frame interval, as the existing overlay path requires.

For art arriving after launch, publish a generation marker after import and pose generation complete. Check it when opening the guide or changing pages; invalidate art misses and affected page images then. Do not scan asset files every frame. Publish generated files atomically, with the manifest/generation marker last.

Build in this order:

1. **Text and static pagination.** Ship the complete instructions, seven pages, larger type, navigation and solid-colour fallbacks first.
2. **Signature pictures.** Generate `sword_throw` and `enemy_grab`, integrate their manifest entries, and verify both player identities.
3. **Remaining gestures.** Add the other nine composites using the same presets and sketch treatment.
4. **Journal materials and launcher presentation.** Apply imported backgrounds, ornament, responsive lessons and All controls.
5. **Art refresh and acceptance checks.** Exercise missing/partial art, swapchain revisions, DPI, captures and headset reading.

No generated art, posed figures, derived previews or contact sheets enter the repository or release. Package source and generation logic only.

**6. Checks and acceptance evidence**

Extend `tests/vr_controls_guide_check.py` to verify seven page IDs, 11 figure IDs, valid directive placement, nonempty alternative text, README text parity and the existing embedded resource. Add explicit content assertions for grip-alone holding, alternate grip + trigger, sword damage differences, recall and automatic return.

Extend `tests/controls_guide_image_check.cpp` to render every page and report:

- Every parsed bullet drawn exactly once across the seven pages.
- No text or illustration bounds outside their allocated rectangles.
- No clipping, overlap or font reduction.
- Correct opaque interior and transparent outer corners.
- Deterministic output for fixed inputs.

Write RGBA previews and convert them to PNG with a small Python standard-library wrapper using `struct` and `zlib`; no package is necessary. Produce:

- Seven plain pages.
- Seven Corvo pages with imported art.
- Seven Emily pages with imported art.
- A partial-art case with one signature figure absent.

Keep these outputs local. CI can use original synthetic colour/alpha fixtures; it must not depend on installed game assets.

For input and upload checks, verify that a held stick changes one page, boundaries do not wrap, neutral rearms navigation, closing does not leak a turn, and every acquired swapchain image receives the current revision. Reopening and character/art changes must never reveal an older page.

Reuse the existing launcher `PrintWindow` capture path. Add `--guide-page <id>` and `--guide-character corvo|emily` alongside the existing `--page controls`, `--screenshot`, `--exit` and `--no-art` flags. Capture every page with and without art, plus stacked and two-column layouts at 100%, 150% and 200% DPI. Inspect text, gesture crops, anatomical left/right, focus, scrollbars and active-page state.

Final headset acceptance must confirm that the player can read instructions at the retained distance, distinguish release from gripping, identify palm contact before yanking, distinguish temple from shoulder, and change pages without an underlying turn or crouch.

This is a design specification; no renders, tests, launcher captures or headset checks were run. The main remaining risk is headset legibility and gesture clarity, which desktop previews cannot establish.
# Native wrist artwork binding

`NativeWristArtPublisher::observe(read, base, native_prompt.native_api(), before, caller, method, receiver, block, result)` returns three bounded publications. Call it from the existing shared native UI Call observer, after the original returns success (`result == 1`). It installs no hook, makes no AS3 writes and retains no AS3 wrappers.

For each valid publication, the core can call:

```cpp
NativeHudChildDraw::publish(publication.kind, publication.geometry,
    publication.owner, current_pose_epoch, GetTickCount64());
```

The kinds are health, mana and equipment. Health/mana belong on the right wrist; equipment includes the native ammo text and weapon/power art and belongs on the left wrist. Separate captured textures must be composed using their measured alpha bounds; getter width/height are diagnostics, not a guessed pixel crop.

The extracted native ABC is the primary source of the paths. `artifacts/hud-re/hud-scene-interaction-as3.txt`, HUDScene getter m5910, resolves `_masterHUD` as `container.gauges.masterHUD`. `artifacts/hud-re/hud-as3-master.txt`, MasterHUD m5214, assigns public slots `_healthMeter = _container_mc.healthMeter`, `_manaMeter = _container_mc.manaMeter`, and `_equipmentIcon = _container_mc.equipment_mc`. The latter contains the native `ammoCount`, `ic`, `ic_on` and `icShadow` children. Accordingly the publisher resolves only `_masterHUD._healthMeter`, `_masterHUD._manaMeter`, and `_masterHUD._equipmentIcon`, never the entire MasterHUD.

Ownership uses the existing exact native wrist caller, receiver, method-binding, typed element vtable, argument and fresh movie Draw checks. Node resolution uses the already prefix-verified constructor/destructor/Get API and proves native DisplayObject type24 plus ancestry to that movie's root. Ownership is checked again after getters in case the movie changed during resolution. All three children are resolved on any accepted wrist update, so discovery does not require a separate change of each stat.

Limitations requiring live evidence: child paths have not yet been exercised in this game session; actual nonempty completed child captures remain required. Binding freshness is 250 ms and this helper does not manufacture recurring UI calls or indefinitely retain stale nodes. Native blocked mana uses a different public child `_masterHUD._manaMeterBlocked`; it is not captured by this three-slot publisher. Adding that alternative requires an explicit second mana binding/capture slot rather than replacing a same-kind/owner registry entry or capturing all MasterHUD. Original widget suppression likewise remains a separate reversible ownership gate.

Focused MSVC `/W4 /WX` fixture passed: exact individual paths, typed owner/current Draw gate, unchanged getters, constructor/destructor balance, invalid result/caller/visibility/node type/ancestry, movie replacement, and separate node identities. This establishes helper behavior, not headset acceptance.

# Game/render overlap: public references (2026-11-19)

These are web and public-code sources only, with no live-game checks. **UNVERIFIED** marks claims I could not confirm from a primary source.

## 1. Doom 3 BFG `com_smp` (primary: GPL source)

Source: `github.com/id-Software/DOOM-3-BFG/blob/master/neo/`. The files are `framework/common_frame.cpp`, `Common.cpp`, `Common_local.h`, `renderer/RenderSystem.cpp`, `tr_local.h`, `tr_frontend_main.cpp`, `tr_frontend_addmodels.cpp` and `VertexCache.h`.

- **Threads.** `idCommonLocal::Init` starts the `"Game/Draw"` thread (`idGameThread`, `CORE_1B`, below normal priority, 1 MB stack).
  - `idGameThread::Run` runs the game tics, then `commonLocal.Draw()`. Draw calls `game->Draw`, then `RenderScene`, then the renderer **front end**: culling, `R_AddModels` jobs, shadow jobs and command building.
  - The **main thread** keeps the events, the usercmds, the GL **back end** (`RenderCommandBuffers`) and the swap.
  - The file's header comment says command generation "winds through the user interface code, and is potentially hazardous". The overlap is therefore limited to the back end.
- **`idCommonLocal::Frame` order:**
  1. Pump events.
  2. `SwapCommandBuffers()`. Its first half, `_FinishRendering`, blocks on the previous swap and GPU fence. Its second half, `_FinishCommandBuffers`, calls `vertexCache.BeginBackEnd()` to unmap, takes `cmdHead`, then `R_ToggleSmpFrame()`.
  3. Build the usercmds into `userCmdMgr`.
  4. `gameThread.RunGameAndDraw()` signals the thread and returns the *previous* `gameReturn_t`.
  5. `RenderCommandBuffers()` runs the **previous** frame's commands.
  6. `gameThread.WaitForThread()`.
- **Sync point.** One full join per frame. The ownership swap runs at the next `Frame` start while the game thread is idle. `UpdateScreen`, used for loading, serializes: it waits, then runs Draw, Swap and Render.
- **What the back end may read** (`tr_local.h`):
  - `idFrameData` "all of the information needed by the back end… duplicated so the front and back end can run in parallel". There are two copies of 64 MB each, holding `viewDef_t`, `drawSurf_t`, the commands and the shader registers (`R_SetupDrawSurfShader`).
  - `viewEntity_t` and `viewLight_t` are copies: "back end should NOT reference the entityDef, because it can change when running SMP".
  - `drawSurf_t::frontEndGeo` "don't use on the back end".
  - Joints and vertices live in a 2-frame vertex cache.
- **Related.** `idLib::IsMainThread()` is a TLS flag. The [BFG technical note](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/papers/DOOM-3-BFG-Technical-Note.pdf) says "nothing is done lazily anymore". Quake 3 (`tr_scene.c`) takes its snapshot at submission: `RE_AddRefEntityToScene` copies into `backEndData[tr.smpFrame]`.
- **Correction.** [Sanglard](https://fabiensanglard.net/doom3_bfg/threading.php) describes a separate back-end thread. The source shows the back end runs on the main thread.

## 2. id Tech 5 / 6

- **id Tech 5.** In the [SIGGRAPH 2009 slides](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/presentations/2009_siggraph/05-JP_id_Tech_5_Challenges.pdf), slides 26 and 28 say jobs are "given 1 frame of latency… results of jobs show up a frame late". The exception is player physics, because 16 ms of extra delay is undesirable. The slides do not cover the game thread versus render thread arrangement.
- **Carmack 2008.** He reportedly said game logic and rendering run on separate threads ([PCGH](https://www.pcgameshardware.de/Panorama-Thema-233992/News/John-Carmack-kommentiert-Multi-Threading-Faehigkeiten-der-id-Tech-5-Engine-657323/)). **UNVERIFIED**: secondhand, and it says nothing about frame offset.
- **id Tech 6.** I found no primary source on the frame offset or the snapshot.
- **id Tech 7.** Gneiting reportedly said there is "no main or render thread", only jobs. **UNVERIFIED**: secondary coverage only.

## 3. Void Engine / Dishonored 2

- I found nothing public on "Ttark", "Midnight", `r_scheduler*` or Void threading.
- Lineage claims (id Tech 5 versus 6, 20–90% rewritten) conflict and are all secondhand.
- PC patch 1.2 ([Shacknews, 2016-11-22](https://shacknews.com/article/97804/dishonored-2s-12-update-addresses-numerous-pc-performance-issues)) said:
  - "Improved CPU cores utilization"
  - "Framerate capped at 120 FPS to prevent physics issues which occur only above 120fps"
  - It also fixed framerate-dependent mouse sensitivity.

  This suggests the simulation step is tied to the frame.
- A DSOGaming report found the game uses more than 4 threads. **UNVERIFIED**: I saw a snippet only, because the page returned 403.

## 4. Retrofit literature

- **Destiny** ([Tatarchuk GDC 2015](https://advances.realtimerendering.com/destiny/gdc_2015/Tatarchuk_GDC_2015__Destiny_Renderer_web.pdf)):
  - Halo: Reach mirrored the whole game state, about 10 MB per frame.
  - Destiny **extracts** only the dynamic data of visible objects into a double-buffered frame packet of about 1 MB. Static render data is cached at registration.
  - "Extract phase is the *only* time we allow crossing the object system and renderer wall."
  - Simulation of N+1 waits for extract, so the copy is kept raw and transforms are deferred to "prepare".
  - The controller is polled as late as possible.
- **Naughty Dog** ([Gyrling GDC 2015](https://media.gdcvault.com/gdc2015/presentations/Gyrling_Christian_Parallelizing_The_Naughty.pdf)):
  - Game N+1, render N and GPU N−1 run in parallel. The frame went from 25 ms to 15.5 ms.
  - `FrameParams` copies the delta time, camera, skinning matrices and mesh list. There are 16 in rotation.
  - Lifetimes are managed by `HasFrameCompleted(X)` and frame-tagged heaps.
  - Pitfalls: cached TLS addresses, and "which delta time should I use?".
- **Unreal** ([Threaded Rendering](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)):
  - The renderer runs "a frame or two behind".
  - It never touches `UPrimitiveComponent`. It works from scene proxies and copied command payloads.
  - Deletion is deferred through `FRenderCommandFence`.
- **Quake 4 / ETQW** ([Kalra and van Waveren](https://www.slideshare.net/psteinb/threading-game-engines-quake-4-enemy-territory-quake-wars)):
  - Hazards: data races, object lifetime and the GL context.
  - Subsystems that were not thread-safe: models, animation, shadows, deforms, loaders and the vertex cache.
  - Frees are deferred until the back end finishes.
  - "Always implement an option to switch between threaded mode and non-threaded in real-time." They also recommend a lock-step mode.
- **D3D11/DXGI** ([MS Learn](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro), [DXGI](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi)):
  - "Only one thread at a time should use the immediate context", and that same thread should call `Present`.
  - A render thread that is not the message-pump thread can deadlock on mode changes.
- **Latency:**
  - [Carmack 2013](https://danluu.com/latency-mitigation/): buffered queues add a frame. With **view bypass**, the renderer replaces the view with fresh input. Old-view culling may then be invalid, so widen the frustum, and the bypass code must not read game state.
  - [OpenXR](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html): `xrWaitFrame` may be called from any thread, and "every stage in the engine pipeline should use the exact same display time".
  - [Meta](https://developers.meta.com/vr/blog/a-vr-frames-life/): engines re-query poses when rendering starts.

## Conclusions for D2

1. **id never overlaps a front end that reads the live world.** Only the back end overlaps, and it reads `idFrameData`, the vertex cache and immutable resources.
   - If D2's `RenderSingleView` graph includes the front end, overlapping it means snapshotting all of the following:
     - per-eye views
     - entity, light and bone transforms
     - material and shader parameter values
     - particles, decals and dynamic geometry
     - HUD/GUI lists
     - frame time and frame counters
   - It also needs double-buffered frame allocators and dynamic/constant buffers, plus deferred frees.
   - The cheaper option is to cut after visibility and extract, and overlap only recording, `ExecuteCommandList` and `Present`.
2. **The sync point is one full join per frame.** Ownership swaps while the other side is idle. Keep the immediate context, `Present` and the OpenXR begin/end calls on the main thread, as BFG does. Moving the game side to another thread exposes TLS and main-thread assumptions.
3. **Keep the +1 frame off the head pose.** Re-locate views at render time with that frame's predicted display time, and widen the culling margin.
4. **Ship it behind a runtime toggle and a lock-step debug mode.**

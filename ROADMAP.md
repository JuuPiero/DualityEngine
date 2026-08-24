# DualityEngine Roadmap

Planned/deferred work, tracked here so it survives outside any one chat session. Update
this file whenever something below actually gets built, or a new deferred item comes up
-- keep it in sync the same way `README.md`'s "Known limitations" section is.

## Rendering

- [ ] Spritesheet/atlas-based animation (UV sub-rects), as an alternative to the
      fixed-frame-slot flipbook (`SpriteFlipbookComponent`).
- [x] 3D renderer -- unlit only (no lighting yet), a per-screen *exclusive* 2D-or-3D switch
      (`CameraComponent::Projection`, never both composited on one screen in the same frame) --
      see `IRenderer3D`/`OpenGLRenderer3D`/`Citro3DRenderer`. Ended up covering more than the
      original scoped-down pass: a `Material` asset (`Asset/Material.h`/`MaterialLoader` --
      Color + Texture, `.material.json`, referenced by `MeshRendererComponent::Material` via
      `AssetRef`, same graceful-fallback convention as every other asset reference) instead of
      embedding Color/Texture directly, a Translate/Rotate/Scale 3D gizmo in the Scene view's 3D
      pane (`ScenePanel.cpp`'s `DrawAndHitTestGizmo3D`, screen-space-projected axis handles --
      Properties panel's generic vec3 fields still work too), Unity-style Scene view navigation
      for the 3D pane (left = select/gizmo-drag, right-drag = orbit, middle-drag = pan, wheel =
      zoom), and OBJ mesh import (`Asset/MeshLoader.h`, a small hand-rolled parser chosen over a
      full FBX/glTF library specifically because it has zero external dependencies and is
      guaranteed to compile for devkitARM -- `MeshRendererComponent::Mesh`, empty/unresolved
      falls back to the procedural `Primitive`). A two-tier full/lite material system (arbitrary
      shader graph on desktop, a simplified fixed-function/TEV-stage model for the 3DS's
      PICA200 GPU) and real lighting remain open, carried over from the earlier
      Polyphase-Engine research.
- [x] Texture rendering on the actual 3DS build (`BuildPipeline::CookAssets`,
      `AssetDatabase::LoadManifest`, `Citro2DRenderer::LoadTexture`/`DrawQuad`) -- every PNG
      under a project's `Assets/` is converted to `.t3x` via `tex3ds` at "Build for 3DS" time
      and baked into romfs alongside a guid->romfs-path manifest, since `AssetDatabase` has no
      real filesystem to scan on-device the way the Editor scans the desktop project folder.
      The same manifest mechanism incidentally fixed an identical latent gap in audio's GUID
      resolution (`PlaySound`) for free. Found and fixed along the way: a real `cmd.exe /c`
      quoting bug in `BuildPipeline::RunCommand` -- `std::system()`'s command wrapping only
      preserves quotes for a bare single-argument executable path; anything with more than two
      quote characters (like a multi-argument `tex3ds` invocation) needs an extra outer quote
      layer to survive cmd's quote-stripping, confirmed empirically after the naive version
      silently corrupted the command and mis-reported tex3ds as failing with no useful error.

## Physics

- [x] 3D physics -- Bullet Physics 2.87 (`Vendor/bullet3/`, vendored the same way as Box2D:
      just `CMakeLists.txt` + the `src/` modules devkitPro's own build actually compiles with
      `BUILD_BULLET3=OFF`), mirroring the existing `Rigidbody2DComponent`/`BoxCollider2DComponent`/
      `CircleCollider2DComponent` shape exactly: `Rigidbody3DComponent` (IsStatic + opaque
      `RuntimeBody`/`RuntimeCollisionShape`), `BoxCollider3DComponent`, `SphereCollider3DComponent`
      (Offset/Size-or-Radius/Density/Friction/Restitution). One `btDiscreteDynamicsWorld` per
      Scene, stepped in `OnRuntimeUpdate` alongside the existing `b2World`, same +Y-is-down
      gravity convention so a 3D Rigidbody falls the same visual direction as a 2D one. A
      collider's `Offset` (when non-zero) wraps its shape in a one-child `btCompoundShape`,
      since Bullet has no built-in local-offset support the way Box2D's fixtures do.
      `TransformComponent::Rotation` round-trips through Bullet's `btQuaternion` via a hand-derived
      ZYX Tait-Bryan conversion matching `OpenGLRenderer3D`/`Citro3DRenderer`'s own
      `M = T*Rz*Ry*Rx` mesh composition order, verified against a headless test (gravity,
      landing/no-tunneling, static-body immunity, offset/compound-shape cleanup, and exact
      rotation-round-trip fidelity all pass). Confirmed compiling and linking for BOTH desktop
      (MinGW) and devkitARM (3DS cross-compile) -- the latter was the real open risk going in,
      since devkitPro ships `3ds-bulletphysics` as an official portlib but with no worked
      example and no known shipped 3DS game using it, so actual ARM11 runtime performance/
      stability is still unverified on real hardware. Not built: `FixedRotation`-equivalent
      constraints, compound shapes beyond the single-offset-child case, capsule/mesh/convex-hull
      3D colliders, and any collision-event callback surface (matching 2D physics' own scope).

## UI

- [x] In-game UI system -- a basic first cut, not the originally-planned XML+CSS-like system
      (that's still a possible future direction, not ruled out): `UIRectComponent` (Anchor +
      Offset + Size, resolved against a physical Screen's own fixed pixel space, no camera
      involved at all -- see `Renderer/UIRenderer.h`'s `ResolveUIRect`) pairs with a second
      component per widget type, matching Unity's own component-per-concern split so a new
      widget type is "add one component + one loop in UIRenderer.cpp", nothing else. Two widget
      types so far: `UIImageComponent` (Panel/Image, same Color/Texture convention as
      `SpriteRendererComponent`) and `UIButtonComponent` (adds click detection --
      `IsHovered`/`IsPressed`/`WasClicked`, updated once/frame by `UpdateUIInteractions` and
      meant to be polled from a script like `GetKeyDown()`; overrides the paired Image's Color
      with its own Normal/Hover/Pressed color). Always drawn last/on top of the mesh+sprite
      passes (`SceneRenderer.cpp`'s `RenderScreen`), on both platforms. No text/label widget yet
      (needs real font rendering, not built -- see the 3D renderer entry's own "next" list for
      the general shape of what's still open elsewhere), no 9-slice/border scaling, single
      global pointer only (see `UpdateUIInteractions`'s own comment on why a button doesn't
      distinguish which physical screen the pointer is actually over).
- [ ] Text/label UI widget -- needs real font rendering on both backends (citro2d has one
      built in; desktop has none, `OpenGLRenderer2D` is legacy fixed-function with no font atlas
      at all yet). The natural next widget after Panel/Button above.
- [ ] Visual UI Builder (drag-and-drop tool for the above) -- later still, after
      whichever UI system above lands.

## Editor

- [x] Parent/child entity tree (`DualityEngine/Include/DualityEngine/Scene/Components.h`'s
      `HierarchyComponent`, `Scene::SetParent`/`GetWorldTransform`) -- Unity/Cocos-style
      Hierarchy panel with drag-and-drop reparenting (drop onto a node) and sibling
      reordering (drop on the gap between rows), plus "Create Child Entity". Reparenting
      preserves world position by default (Unity's default behavior), and every renderer/
      gizmo/hit-test call site composes local transforms up the parent chain via
      `Scene::GetWorldTransform`. Known limitation: a `Rigidbody2DComponent` does NOT track
      a moving/rotating parent during simulation -- Box2D bodies spawn at their resolved
      world transform but simulate independently afterward (the same real limitation Unity
      documents for non-kinematic parent/child Rigidbodies). Keep physics entities as
      roots, or children of a parent that stays at identity. Not built: a read-only
      "Parent: X" row or "Unparent" button in the Properties panel, and inserting a dragged
      entity as the very first sibling (only "insert after" is supported -- still fully
      expressive, just requires moving the other item down instead in that one case).
- [ ] Project Hub / recent-projects list, as its own separate window (or its own
      subfolder in this repo) -- explicitly deferred until "everything else" is more
      finished. Right now "Open Project" is a plain file-browse dialog only.
- [ ] Inline thumbnail preview on Properties panel asset fields (texture/etc.) --
      currently just shows the resolved filename + a Clear button.
- [ ] Undo/redo.
- [ ] Multi-select (Hierarchy, Scene view).
- [x] Split Scene view (`DualityEditor/Source/Panels/ScenePanel.cpp`) -- two side-by-side
      panes, one per screen, each its own free-roam `SceneViewCamera` (pan/zoom) seeded
      once from that screen's real primary `CameraComponent` so the initial view looks
      WYSIWYG, but casual navigation never mutates the actual gameplay camera (matches
      Unity's Scene-vs-Game separation). Selection and the active gizmo tool are shared
      across both panes; a drag started in one pane keeps using that pane's camera math
      even if the mouse strays into the other (`EditorContext::DraggingGizmoScreen`). Not
      built: a resizable/draggable splitter between the two panes (fixed 50/50 split).
- [x] Screen grouping (`ScreenGroupComponent`, `Scene/Components.h`) -- opt-in tag, add to
      any entity (typically a root "TopGroup"/"BottomGroup") to associate its whole subtree
      with a screen. Drives: the Hierarchy panel's Top Screen/Bottom Screen/Ungrouped
      top-level split (root entities without a `ScreenGroupComponent` or `CameraComponent`
      of their own land in "Ungrouped" rather than being hidden -- the split only reclassifies
      ROOT entities, a subtree's own internal parent/child structure is unaffected), the
      colored per-node marker in the tree, and `Scene::FindEntityInScreen` (searches a
      screen's tagged subtrees, falling back to a scene-wide by-name search if that screen
      has no tagged group yet). Exposed to scripts as `Behaviour::FindEntityInTopScreen`/
      `FindEntityInBottomScreen` via the `EngineServices` bridge (same ABI-safe pattern as
      `PlaySound`/`GetAxis` -- a script on a Top-screen entity can look up a Bottom-screen
      one by name and vice versa, since it's one shared `Scene` either way).
- [x] Perspective camera mode (`CameraComponent::Projection`) -- see the 3D renderer entry
      above; `FovDegrees`/`NearPlane`/`FarPlane` are Perspective-only, `Zoom` stays
      Orthographic-only.
- [x] Async "Build for 3DS" (`DualityEditor/BuildPipeline.h`) -- runs on a detached
      background thread (`BuildFor3DSAsync`) instead of blocking the Editor's UI thread for
      up to a minute; the menu item grays out and shows "(building...)" while
      `BuildPipeline::GetStatus() == Running`, refusing to start a second concurrent build.
      Required making `Log` (`DualityEngine/Core/Log.h`) mutex-guarded, since the build
      thread logs progress/errors through it while ConsolePanel reads it every frame on the
      main thread -- `GetEntries()` now returns a snapshot copy under the lock rather than a
      live reference. Not handled: closing the Editor mid-build leaves the build-3ds.bat
      child process tree to finish or get cleaned up on its own (no job-object-based process
      tracking) -- an accepted, rare edge case.
- [x] Renderer stats overlay (Game panel) -- `IRenderer2D::GetDrawCallCount()` (exact, not
      estimated: both backends are unbatched, one `DrawQuad` == one real draw call), reset
      each `BeginFrame`. FPS is exponentially smoothed in `Application::Run()`. The draw-call
      count shown is snapshotted right after the real Top+Bottom `RenderScreen` passes,
      before the Scene view's own editor-only draws would otherwise inflate it.
- [x] Standalone desktop player (`DualityPlayerDesktop/`) -- the desktop counterpart to
      `DualityPlayer` (3DS): a real GLFW window + `OpenGLRenderer2D`/`3D`, no ImGui/Editor UI
      at all, running the same `Scene`/`SceneSerializer`/`AssetDatabase`/`GameScripts`
      pipeline. `GameScripts` links directly as a real DLL import (unlike `DualityPlayer`'s
      `--whole-archive` workaround, which is 3DS-static-linking-only) -- `run-desktop-player.bat`
      puts it on `PATH` next to glfw3.dll/glew32.dll so it resolves at launch. One real window
      renders both physical screens stacked vertically (Top above Bottom, mirroring how a 3DS
      is held) into their own `glViewport`+`glScissor`-restricted sub-rectangle of the same
      default framebuffer -- scissoring (not just viewport) matters here specifically because
      a screen's own clear would otherwise erase the other screen's already-drawn content;
      the Editor avoids this entirely with two separate offscreen `Framebuffer`s instead, which
      isn't available here since there's only one real window. Still hardcoded to always run
      `SampleProject` (no project picker) and always 2x scale (no window resize handling) --
      both reasonable first-pass cuts, not attempted yet.

## Assets

- [ ] Folder-level `.meta` files -- currently only individual files get one.
- [ ] Upfront recursive asset scan on project load -- `.meta` generation and the
      GUID->path index are currently populated lazily as the Content Browser is
      browsed into each folder, not scanned ahead of time.
- [ ] Prefab system.

## Scripting

- [ ] A `Debug.Log`-equivalent callable from `GameScripts` into the Console panel.
      Still blocked by `GameScripts` deliberately not linking `DualityEngine`'s compiled
      lib on desktop (needed for the DLL hot-reload story) -- but the *mechanism* to fix
      this now exists: `EngineServices` (`DualityEngine/Include/DualityEngine/Scripting/
      EngineServices.h`), the exact bridge built for `Behaviour::GetKey`/`GetAxis`/etc.
      to reach the real `Duality::Input` across that same DLL boundary. Adding a
      `LogInfo`/`LogWarn`/`LogError` function pointer to that same struct (wired the same
      way `Scene.cpp`'s `s_EngineServices` wires the Input adapters) is a small, mostly
      mechanical follow-up now, not a new architecture problem to solve.
- [x] Input system (`DualityEngine/Include/DualityEngine/Input/`), Unity-old-Input-
      Manager-style -- `GetKey`/`GetKeyDown`/`GetKeyUp`/`GetAxis("Horizontal"/"Vertical")`/
      pointer, callable from `Behaviour` scripts on both platforms via the
      `EngineServices` bridge above. Desktop polls GLFW keyboard/mouse in
      `DualityEditor/Source/Window.cpp`; 3DS polls Circle Pad/buttons/touch in
      `DualityPlayer/Source/Main.cpp`. Not built: a configurable axis-mapping system
      like Unity's real Input Manager UI (`GetAxis` only recognizes the two hardcoded
      default axis names) and gamepad support in the desktop Editor (no controller
      polling there, only keyboard/mouse).

## Other

- [x] Audio (`DualityEngine/Include/DualityEngine/Audio/AudioEngine.h`) -- miniaudio on
      desktop (vendored in `Vendor/miniaudio/`, real multi-channel mixing), `ndsp` +
      a hand-rolled `WavLoader` on 3DS (no vendored decoder on real hardware, so only
      16-bit PCM WAV is supported there -- desktop can play anything miniaudio decodes,
      but stick to WAV for anything that needs to also run on-device). Fire-and-forget
      only (`Behaviour::PlaySound(guid, loop=false)`/`StopAllSounds()`) -- no
      per-instance volume/pause/handle control exposed to scripts yet. One-shot playback
      only on 3DS (the whole clip is decoded and queued up front) -- real double-buffered
      streaming for long music tracks is a real follow-up if a clip turns out too big to
      hold in memory whole.
- [x] SD-card save/load (`DualityEngine/Include/DualityEngine/IO/SaveSystem.h`) --
      plain JSON file save/load, header-only (so scripts can call it directly, no
      `EngineServices` entry needed -- see the file's own comment for why). Paths are
      resolved against the process's CWD on both platforms, no separate "user data
      directory" layer yet.
- [x] Real-time clock (`DualityEngine/Include/DualityEngine/Core/DateTime.h`) -- plain
      `<ctime>`, works identically on both platforms with zero platform-specific code.
- [ ] A future pass could extend `EngineServices`' `LogInfo`-follow-up idea (see
      Scripting above) to also cover Audio (`SetVolume`/`Pause`/handle-based control) if
      a real game ends up needing more than fire-and-forget playback.

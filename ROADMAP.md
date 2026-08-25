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
      Color + Texture, `.mat`, referenced by `MeshRendererComponent::Material` via
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
- [x] Extracted `OpenGLRenderer3D`'s inline shader compile/link and VAO/VBO mesh-upload
      boilerplate into two small reusable classes -- `GLShaderProgram`
      (`DualityEngine/Renderer/OpenGL/GLShaderProgram.h`: compile+link, `GetUniformLocation`/
      `GetAttribLocation`, `SetUniformMat4/Vec4/Int`) and `GLVertexArray`
      (`GLVertexArray.h`: VAO+single-VBO, `SetVertexData`, `AddFloatAttribute`) -- user asked
      why the renderer held all of this inline instead of separate classes. Both use explicit
      `Init()`/`Shutdown()` rather than a constructor/destructor, matching every other
      renderer-owned GL resource in this codebase, specifically because `GLVertexArray` is
      stored BY VALUE in `OpenGLRenderer3D::m_Meshes[3]` and a growing
      `std::vector<GLVertexArray> m_ImportedMeshes` -- a naive RAII destructor would double-free
      on every vector reallocation/move. Deliberately does NOT cache uniform locations by name
      internally (callers look one up once via `GetUniformLocation` and keep the `int`, exactly
      `OpenGLRenderer3D`'s pre-existing pattern) and does NOT add an index/element buffer (no
      current consumer uses `glDrawElements`) -- both would be speculative generality with no
      real use yet. Scoped to the desktop OpenGL renderer only: `Citro2DRenderer`/
      `Citro3DRenderer` use citro3d's own completely different shader-binary
      (`shaderProgramInit`/`DVLB_ParseFile`) and `C3D_AttrInfo`/`C3D_BufInfo` APIs, precompiled
      offline via picasso rather than compiled at runtime -- building a real cross-platform
      abstraction over both would be a much bigger RHI-style undertaking, not what was asked.
      The only real duplication left unaddressed (by explicit user choice, offered as a
      separate option): `LoadTexture`/`UnloadAllTextures`/`m_TextureCache` is still repeated
      near-identically across all 4 renderers (2D/3D x desktop/3DS). Verified with a real visual
      check, not just a clean compile: a live Editor screenshot (Scene view, 3D pane) after the
      refactor shows `TestCube3D` and `PhysicsGround3D` still rendering correctly (orange
      `TestOrange.mat` color, correct shape/rotation, gizmo and camera frustum overlay intact).
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
- [x] Collision/trigger event callbacks (2D and 3D) -- `Behaviour::OnCollisionEnter/Exit`
      and `OnTriggerEnter/Exit(Entity other)`, both sides of a touching pair get the
      callback (Unity's own convention). 2D wires a real `Box2DContactListener`
      (`b2World::SetContactListener`) bundled alongside `b2World` in a new
      `Physics2DWorld` (`Scene.cpp`); 3D has no built-in enter/exit callback in Bullet, so
      it's detected by diffing the dispatcher's own contact manifolds against last frame's
      touching-pair set every `OnRuntimeUpdate` (`Physics3DWorld::TouchingPairs`). New
      `IsTrigger` field on all 4 collider components -- 2D uses Box2D's native
      `fixtureDef.isSensor`; 3D has no native sensor concept, so a trigger body instead
      gets `btCollisionObject::CF_NO_CONTACT_RESPONSE` (contacts still generate, physical
      push-apart is suppressed). A pair counts as a trigger callback if EITHER side is a
      trigger, a collision callback only if NEITHER is. Enter+Exit only, no
      OnCollisionStay/OnTriggerStay this pass (see below). Verified against a headless
      test covering 2D/3D x collision/trigger x enter/exit (`Tests/CollisionTriggerTests.cpp`).
      Also landed alongside this: the long-pending `Debug.Log`-from-scripts bridge
      (`Behaviour::LogInfo/LogWarn/LogError`, same `EngineServices` pattern as
      `PlaySound`/`GetAxis` -- see the Scripting section's own now-`[x]`'d entry) and a demo
      script (`GameScripts/Source/CollisionLogBehaviour.cpp`).
- [ ] OnCollisionStay/OnTriggerStay (fires every frame while still touching, not just on
      the enter/exit edge) -- deferred from the pass above; the 3D side would mean not
      re-running the full manifold diff, just also emitting for every pair still present in
      both this frame's and last frame's touching set.
- [ ] Raycast API exposed to scripts (a `Physics.Raycast`-equivalent through the same
      `EngineServices` bridge `PlaySound`/`GetAxis` already use), for both 2D and 3D --
      ground checks, line-of-sight, click-to-select-in-game all need this and nothing like it
      exists yet.
- [ ] Joints/constraints (hinge, spring, fixed) -- neither `b2World` nor the new
      `btDiscreteDynamicsWorld` wires up any joint type yet, just free rigid bodies.

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
- [ ] Duplicate entity (Ctrl+D / Hierarchy context menu) -- no way to clone an entity and its
      components yet, only create-new-empty or drag-reparent.
- [ ] Grid/angle snapping for the Translate/Rotate gizmos, 2D and 3D (hold-a-modifier-key
      convention, matching Unity's Ctrl-to-snap).
- [x] Search/filter box for the Hierarchy panel, Content Browser, and Console panel.
      Hierarchy: while the search box has text, shows a flat filtered list of every
      matching entity scene-wide instead of the normal tree (a tree with drag-drop
      reparenting has no cheap way to hide/show whole subtrees while preserving that,
      so this deliberately doesn't try to filter-in-place -- empty search box shows the
      unchanged original tree). Content Browser: filters files by name within the
      current folder; folders themselves stay visible regardless so navigation still
      works. Console: filters displayed log lines by substring, alongside the existing
      per-level checkboxes. All three use the same small case-insensitive-substring
      local helper, not a shared utility (one line of logic each).
- [x] Properties panel "Lock" toggle (`PropertiesPanel::m_Locked`/`m_LockedEntity`/
      `m_LockedAssetPath`) -- Unity-Inspector-style: pins the panel to whatever was
      selected at the moment it was checked, ignoring further Hierarchy/Scene/Content
      Browser selections until unchecked. User-reported real bug this fixes: dragging a
      Material/Texture from the Content Browser onto an `AssetRef` field starts with a
      plain mouse-down over the thumbnail, and `ContentBrowserPanel`'s click-to-select
      (added for the Asset Inspector work above) fires on that same mouse-down frame --
      so starting the drag immediately swapped Properties away from the very entity/field
      being dragged onto, before the drag distance threshold was even reached. Auto-
      unlocks if the locked `Entity` stops being valid in the current scene (checked via
      `entt::registry::valid()`, documented safe to call on any handle regardless of which
      scene "generation" produced it) -- covers a scene swap happening while locked,
      mirroring this codebase's existing accepted stale-Entity-across-scene-swap risk
      class rather than adding new never-before-attempted protection.
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
- [x] "Build for PC" menu item, right below "Build for 3DS" (`BuildPipeline::BuildForPC`/
      `BuildForPCAsync`) -- user asked for it directly. Saves the scene, then rebuilds the
      `DualityPlayerDesktop` target incrementally in the existing desktop build tree; no romfs/
      asset-cooking step like the 3DS build needs, since `DualityPlayerDesktop` already reads a
      project's `Assets/` directly off disk at its own launch time. Shares `BuildFor3DSAsync`'s
      same `s_Status` flag on purpose, so a PC build and a 3DS build can't run concurrently
      against the same Ninja-generated build tree. Uses a plain `std::system()` call (not
      `RunCommand`'s extra quote-wrap) since `cmake --build "<dir>" --target X` starts with an
      unquoted word (`cmake`), which never triggers the cmd.exe `/c` quoting quirk `RunCommand`
      exists to work around (that quirk only fires when the command *itself* begins with a
      quote character) -- same shape `ScriptEngine::Reload`'s own `cmake --build` call for
      GameScripts already uses successfully.
- [x] Removed the hardcoded `E:\App\devkitPro\tools\bin\tex3ds.exe` path in
      `BuildPipeline::CookAssets` -- user asked "is this OK, is there a more flexible way to
      auto-find the path". Replaced with `FindDevkitProInstallDir`/`FindTex3dsExe`
      (`BuildPipeline.cpp`), reimplementing `devkitpro-path.bat`'s own already-proven 3-step
      resolution order in C++: (1) `DEVKITPRO` env var, only if it resolves to a real Windows
      path (confirmed empirically on this machine that it does NOT -- devkitPro's installer
      sets it Windows-wide to a POSIX-style value, `/opt/devkitpro`, meaningless to a plain
      Windows process); (2) the Windows registry entry `devkitProUpdater` writes on install
      (`HKLM\...\Uninstall\devkitProUpdater`, value `InstallLocation`, checking both the
      WOW6432Node and native views); (3) the documented default `C:\devkitPro`. No hardcoded
      fallback at the end -- an unresolvable install logs a clear one-time warning and skips
      texture cooking for that run rather than silently trying a path that doesn't exist.
      Verified two ways: the registry lookup independently confirmed via PowerShell to resolve
      to the real `E:\App\devkitPro` on this machine, and a scratch program linking directly
      against the real `BuildPipeline.cpp` that called the actual (public) `BuildFor3DS` end to
      end -- including the full `build-3ds.bat` step afterward -- confirming the auto-detected
      path produces a working `.t3x`/build exactly like the old hardcoded path did.
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
- [x] Prefab system (`DualityEngine/Include/DualityEngine/Scene/PrefabSerializer.h`,
      `Behaviour::Instantiate`) -- Unity's Prefab. A `.prefab` asset (same
      GUID/`.meta`/`AssetDatabase` convention as any other custom asset type, e.g.
      `.mat`) holding one entity + its full descendant subtree, reusing
      `SceneSerializer`'s own per-entity component (de)serialization logic (extracted
      into `Source/Scene/EntitySerialization.{h,cpp}` specifically so the two don't
      duplicate the same `FieldValueToJson`/`JsonToFieldValue` dispatch tables) rather
      than a separate serializer from scratch. `PrefabSerializer::Save` collects the
      subtree depth-first (same recursive-children-walk shape as
      `Scene::DestroyEntity`'s own subtree traversal); `Instantiate` creates a brand new
      copy and attaches its root under a caller-chosen live parent (`Entity{}` = scene
      root) via the existing `Scene::SetParent`. Editor UX: Hierarchy panel's per-node
      context menu gained "Create Prefab from Selection" (writes to
      `<project>/Assets/Prefabs/<EntityName>.prefab`); dragging a Prefab asset onto
      the Hierarchy panel's empty-space drop zone instantiates it at the scene root.
      Script-facing `Behaviour::Instantiate(prefabAssetGuid) -> Entity`, same
      `EngineServices` bridge shape as `FindEntityInScreen`. Verified against a headless
      test covering single-entity round-trip, parent/child subtree preservation,
      attaching under a caller-chosen live parent, and graceful failure on a missing
      file (`Tests/PrefabTests.cpp`). A real bug this test suite caught immediately:
      `Tests/Main.cpp` wasn't calling `RegisterBuiltinComponents()` at startup (every
      real entry point does), so `TypeRegistry::All()` was silently empty and nothing
      ever actually serialized -- fixed there, not worked around. Scope cut: no
      prefab-instance link -- an instantiated copy is fully independent afterward, no
      "apply changes back to the prefab" support.
- [x] ScriptableObject (`DualityEngine/Include/DualityEngine/Scripting/ScriptableObject.h`,
      `ScriptableObjectRegistry`, `Asset/ScriptableObjectLoader.h`) -- Unity's
      ScriptableObject: a reusable, Inspector-editable data asset (`.asset`) not
      attached to any Entity. Needed its own non-Entity-coupled reflection path, unlike
      `TypeRegistry` (whose `Has`/`GetPtr`/`AddDefault`/`Remove` are all `Entity`-shaped) --
      solved by noticing `FieldHandle::Get`/`Set` (`Reflection/Field.h`) already operate on
      a plain `void*`, so nothing about the reflection primitives themselves needed to
      change, only a second, `Entity`-free registry (`ScriptableObjectFactoryEntry`:
      `Name`/`Create`/`Destroy`/`Fields`) alongside `TypeRegistry`. A subclass declares its
      own `static std::vector<FieldHandle> Fields()` (same `MakeField()` calls
      `Reflection.cpp` uses for built-in components) and self-registers with
      `REGISTER_SCRIPTABLE_OBJECT` instead of `REGISTER_BEHAVIOUR` -- same
      GameScripts-self-registers/host-reads-it-back-out shape as `ScriptFactoryEntry`, via
      a second exported `GetScriptableObjectFactories()` alongside `GetScriptFactories()`.
      `FieldValueToJson`/`JsonToFieldValue` (previously local to
      `EntitySerialization.cpp`) were extracted into a shared `Reflection/
      FieldSerialization.{h,cpp}` so `ScriptableObjectLoader` reuses the identical
      per-`FieldValue`-alternative dispatch instead of a third copy; the Properties
      panel's own per-field-widget `std::visit` block was extracted the same way, into
      `DualityEditor/FieldEditorWidget.h`, so the new asset inspector (Content Browser:
      click a `.asset` to select it, Properties panel shows its fields, auto-saving
      to disk on every edit) draws identically to a component's fields. Script-facing
      `Behaviour::LoadScriptableObject<T>(assetGuid) -> T*` resolves the guid and hands
      back an opaque pointer the caller `static_cast`s to its own concrete type (same
      no-engine-type-across-the-DLL-ABI rule every other `EngineServices` call follows).
      Content Browser gained a "Create > ScriptableObject > &lt;Type&gt;" right-click menu,
      listing whatever's currently registered. One real lifetime bug caught before it
      shipped: `ScriptEngine::Shutdown()` used to `FreeLibrary` the GameScripts DLL before
      dropping any state -- harmless for `ScriptRegistry`/`ScriptableObjectRegistry` (they
      only ever drop raw function pointers, never call through them), but a
      `ScriptableObjectLoader` cache genuinely OWNS live instances and must destroy them
      (via each entry's own `Destroy` pointer) *before* the DLL that pointer lives in is
      unloaded -- fixed by adding `ScriptableObjectLoader::UnloadAll()` as the first thing
      `Shutdown()` does. Demo type: `GameScripts/Include/GameSettingsData.h`
      (`PlayerSpeed`/`ScorePerCoin`/`GameTitle`), with a real instance checked in at
      `SampleProject/Assets/GameSettings.asset`. Verified via `Tests/
      ScriptableObjectTests.cpp` (type registration, Create-writes-defaults, a save then
      `UnloadAll()` then reload that forces an actual disk re-parse rather than just
      handing back the in-memory object, and graceful failure for a missing file / an
      unregistered class) and a live Editor smoke test (GameScripts load log line reports
      the registered ScriptableObject count). Scope cuts: no "used by" tracking on
      delete/rename, and no `OnEnable`-style lifecycle hook (Unity's ScriptableObject has
      one for editor load/unload bookkeeping; this one is a pure data container for now).
- [x] Unified Asset Inspector + Unity/Cocos-style `.meta` importer settings + real per-type
      extensions (`DualityEditor/AssetInspectorRegistry.h`, `AssetInspectors.cpp`) -- user:
      "nên có 1 class Asset base... để tất cả các loại file tài nguyên... hiển thị đúng cho
      từng loại, và file .meta sẽ lưu data như unity hay cocos, và mấy file tài nguyên đang
      trùng dạng json... nên đặt 1 extension riêng". Three parts:
      - **Extension rename**: every custom JSON asset type now has its own real, single-token
        extension instead of all sharing the ambiguous `.json` (Unity/Cocos convention) --
        Material `.material.json` -> `.mat`, Prefab `.prefab.json` -> `.prefab`,
        ScriptableObject `.asset.json` -> `.asset`, and Scene `Scene.json` -> `Scene.scene`
        (the biggest-blast-radius rename: touched `BuildPipeline`'s romfs copy, both
        `DualityPlayer`/`DualityPlayerDesktop` load paths, "Save Scene As..."'s file dialog
        filter, and every sample/romfs file). Clean break, no back-compat loader for the old
        extensions -- consistent with this project's own "no migration shims for internal
        format changes with zero external consumers" precedent (e.g. the earlier Name/Tag
        split).
      - **AssetInspectorRegistry** (`DualityEditor`-only, NOT `DualityEngine` -- unlike
        `TypeRegistry`, there's no engine-side non-UI consumer for "how to draw this asset's
        Inspector," only the Editor needs it): one `{Extension, DisplayName, DrawInspector}`
        entry per recognized extension, looked up by `PropertiesPanel` when
        `ctx.SelectedAssetPath` is set and `ctx.Selected` is empty (Entity selection still
        wins when both are set). Material and ScriptableObject are fully editable (Material
        gained its own `static std::vector<FieldHandle> Fields()`, so `MaterialLoader` was
        refactored onto the same generic `FieldValueToJson`/`JsonToFieldValue` +
        `DrawFieldWidget` machinery ScriptableObjectLoader already used -- `FieldHandle::Get`/
        `Set` already take a plain `void*`, not an `Entity`, so nothing in the reflection
        system itself needed to change). Prefab/Scene get a read-only summary (entity count +
        root/top-level entity names, parsed directly from their shared `{"Entities": [...]}`
        shape) -- full in-place editing would need a way to run Entity-based field reflection
        against raw JSON with no live `Entity`/`Scene` at all, a bigger feature saved for
        later. `DualityEditor/FieldEditorWidget.h`'s `DrawFieldWidget` (extracted from
        PropertiesPanel's own per-field `std::visit` block) is what lets Material's and
        ScriptableObject's inspectors render identically to a component's fields, with zero
        duplicated widget code. Content Browser gained a "Create > Material" menu item
        alongside the existing "Create > ScriptableObject > <Type>".
      - **Texture/Audio import settings** (`DualityEngine/Asset/{Texture,Audio}ImportSettings.h`)
        -- Unity's TextureImporter/AudioImporter equivalent, stored in the asset's own
        `.meta` under a new `"Importer"` key (preserving the existing `"guid"` key
        untouched) rather than in the asset file itself, since a `.png`/`.wav` is a foreign
        binary format this engine doesn't own. `TextureImportSettings` (FilterMode: Point/
        Bilinear, WrapMode: Clamp/Repeat, GenerateMipmaps) replaces what `GLTextureLoader` had
        hardcoded (GL_LINEAR + GL_CLAMP_TO_EDGE, no mipmaps -- chosen as this struct's
        defaults so an untouched `.meta` changes nothing). `AudioImportSettings` has exactly
        one field, Volume -- deliberately the ONLY one added, since it's the sole setting with
        real, already-existing engine behavior to hook into (no volume control existed
        anywhere before this); resisted inventing e.g. a "force mono" flag with no real
        consumer, per this project's own "no half-finished/speculative fields" rule.
        Desktop applies both fully at runtime (`GLTextureLoader`, and `AudioEngine::Play` --
        which required unifying its old special-cased fire-and-forget/looping miniaudio paths
        into one tracked-`ma_sound` path so `ma_sound_set_volume` has something to act on,
        with one-shot sounds now reaped in `Update()` via `ma_sound_at_end` instead of being
        untracked). **3DS is the interesting case**: `Citro2DRenderer::LoadTexture`/
        `AudioEngine::Play` only ever see the cooked romfs path, with no way back to the
        original `Assets/` source file's `.meta` -- solved by having `BuildPipeline::
        CookAssets` cook a settings-only `.meta` (just the `"Importer"` block, no `"guid"`)
        right next to each `.t3x`/copied `.wav` in romfs, so the on-device code calls the
        *exact same* `TextureImportSettings::Load`/`AudioImportSettings::Load` the desktop
        build does, no special-cased format. GenerateMipmaps is the one setting baked in at
        BUILD time instead (tex3ds's `-m box` flag, confirmed via `tex3ds --help` that mipmaps
        are opt-in/absent by default, so an unedited texture's cook command is byte-for-byte
        unchanged); FilterMode/WrapMode apply at RUNTIME via `C3D_TexSetFilter`/
        `C3D_TexSetWrap` on the `C3D_Tex` inside the loaded `C2D_Image`.
      - Verified via `Tests/MaterialTests.cpp` (save/load round-trip + default-on-missing-file)
        and `Tests/AssetImportSettingsTests.cpp` (both settings types round-trip through Save/
        Load and don't clobber an existing guid), plus a real content-level check (not just
        generic-logic unit tests): a throwaway scratch program loaded the actual renamed
        `SampleProject/Assets/Materials/TestOrange.mat` and `Scene.scene` against the real
        built engine library and confirmed the Color value and all 11 entities came back
        correctly -- this specifically caught that the renamed Material's on-disk shape
        (Color as a `{r,g,b,a}` object) needed migrating to the new generic array shape
        `[r,g,b,a]`, not just a file rename. Full suite (30 tests) and both `build.bat`/
        `build-3ds.bat` clean afterward.
- [ ] Live asset watching / hot-reload for changed textures/materials while the Editor is
      open -- only `GameScripts` hot-reloads today (`ScriptEngine`); editing a texture file
      externally needs an Editor restart (or at least a manual Content Browser refresh) to
      pick up the change.

## Scripting

- [ ] Per-script public/serialized fields shown in the Properties panel, like Unity's
      `[SerializeField]` -- `BehaviourComponent` (`Reflection.cpp`) currently only exposes
      which script class is attached, not that script's own tunable fields. Needs an
      Overrides map so Edit-mode edits to those fields survive Play/Stop (flagged inline in
      `Reflection.cpp`'s own comment already, just not tracked here until now).
- [ ] Coroutines / a delayed-call helper (`Invoke`/`WaitForSeconds`-equivalent) -- right now
      a script can only act every frame from `OnUpdate`, with no built-in way to schedule
      "do X after N seconds" without hand-rolling a timer field.
- [x] A `Debug.Log`-equivalent callable from `GameScripts` into the Console panel --
      `Behaviour::LogInfo/LogWarn/LogError`, wired through `EngineServices` (a
      `LogInfo`/`LogWarn`/`LogError` function pointer each, adapters in `Scene.cpp`
      forwarding to the real `Duality::Log`) exactly the mechanical way this entry
      originally described. Landed alongside the Physics section's collision/trigger
      event work, since a demo script for those otherwise had no way to prove they fired.
- [x] Active/Enable-Disable (`ActiveComponent`, `Scene::IsEffectivelyActive`,
      `Behaviour::SetActive/IsActive/OnEnable/OnDisable`) -- Unity's GameObject.SetActive/
      activeInHierarchy. `ActiveComponent` is mandatory (added by `Scene::CreateEntity`
      alongside Name/Tag/Transform/Hierarchy, so an old scene file missing it entirely
      still gets the default `true` on load); `IsEffectivelyActive` cascades up the
      `HierarchyComponent::Parent` chain the same way `GetWorldTransform` does, so a
      disabled parent implicitly disables its whole subtree without touching each
      child's own flag. `OnEnable`/`OnDisable` fire exactly once per transition
      (edge-detected via a new, unreflected `BehaviourComponent::WasActiveLastFrame`);
      `OnUpdate` simply doesn't run while inactive; `OnCreate` still always runs once at
      Play start regardless of starting Active state (Unity's Awake-always-runs rule).
      Respected by the real render path (`SceneRenderer.cpp`'s sprite/mesh/UI passes,
      `UIRenderer.cpp`'s click hit-testing) -- deliberately NOT by `ScenePanel.cpp`'s
      Editor Scene-view panes, matching Unity's own Scene-view-shows-disabled-objects
      convention. Scope cut: toggling Active during Play does NOT dynamically add/remove
      a Rigidbody's physics body, only whether one existed at Play start.
- [x] SceneManager (`DualityEngine/Include/DualityEngine/Scene/SceneManager.h`,
      `Behaviour::LoadScene`) -- Unity's `SceneManager.LoadScene`. A pure request mailbox
      (static pending-path flag) rather than an owner of the `Scene` itself, since a
      script can't safely tear down the very `Scene` its own call stack is executing
      inside of mid-`OnRuntimeUpdate` -- each real entry point (`DualityPlayer`,
      `DualityPlayerDesktop`, the Editor's own Play-mode tick in `Application.cpp`) polls
      `SceneManager::HasPendingLoad()` once per frame, right after `OnRuntimeUpdate`
      returns, and does the actual Stop/swap/Deserialize/Start dance itself -- staying in
      Play mode throughout (a scene-to-scene transition, not a Stop). The old scene's
      cached textures/meshes are freed on the swap via new `IRenderer2D::
      UnloadAllTextures`/`IRenderer3D::UnloadAllTextures`+`UnloadAllMeshes` (implemented
      on all 4 backends), since those caches otherwise only ever grow for the process's
      whole lifetime. `assetsRelativePath` resolves against the current project's Assets
      root on either platform (no `BuildPipeline` changes needed -- any non-`.png` file
      already rides its generic cook-and-manifest path into 3DS romfs); the Editor's File
      menu gained a companion **"Save Scene As..."** (writes a snapshot to a new file,
      does NOT change which scene "Save Scene"/"Load Scene" operate on) so a project can
      actually accumulate a second scene file to target. No multi-scene *asset* system
      (GUID-based scene references, additive loading) -- just path-based single-scene
      swapping for now.
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

## Testing & Infra

- [x] Automated test suite for engine-level logic (`Tests/`, a small hand-rolled
      `TEST_CASE`/`CHECK`/`CHECK_SOFT` harness -- `TestFramework.h` -- rather than vendoring
      Catch2/GoogleTest, matching this project's own "small dependency-free infra over a
      new library" preference elsewhere, e.g. `MeshLoader`'s hand-rolled OBJ parser).
      Desktop-only `DualityEngineTests` executable (`run-tests.bat`), linking straight
      against `DualityEngine`'s real compiled code -- no mocks. Covers what this session's
      own ad hoc throwaway headless verification programs already exercised (Active/
      Enable-Disable cascading + lifecycle, 2D/3D collision and trigger events, Bullet
      gravity/landing/offset-collider/rotation-round-trip), now a standing, permanent
      suite instead of a one-off script -- plus SceneManager's request-mailbox contract
      and Prefab's save/instantiate round-trip, added once those features existed, and
      later `ScriptableObjectLoader`'s Create/Save/Load round-trip (including a real
      disk re-parse after `UnloadAll()`, not just an in-memory cache hit), plus
      Material's own save/load round-trip and both Texture/AudioImportSettings' `.meta`
      round-trip (`Tests/MaterialTests.cpp`, `Tests/AssetImportSettingsTests.cpp`).
      Already caught one real bug on its own: `Tests/Main.cpp` initially never called
      `RegisterBuiltinComponents()`, so `TypeRegistry::All()` was silently empty and
      Prefab/scene serialization tests were round-tripping nothing -- exactly the kind
      of regression this suite exists to catch, catching itself being unwired correctly.
      Not covered yet: a whole-`Scene` `SceneSerializer` round-trip test (only
      `PrefabSerializer`'s subtree save/load is tested so far, though they now share the
      same underlying `EntitySerialization.cpp` logic) and reflection/`TypeRegistry`
      itself in isolation. Orbit-camera quaternion math isn't included either -- it
      lives in `ScenePanel.cpp`'s anonymous namespace (Editor-only, not linked into
      `DualityEngine`), not a straightforward fit for this suite without a visibility
      refactor.
- [x] CI build verification (`.github/workflows/ci.yml`) -- desktop only for now: MSYS2
      MinGW-w64 setup matching `README.md`'s own documented Prerequisites exactly, then the
      existing `build.bat`/`run-tests.bat` unmodified (so a local run reproduces CI
      precisely). The 3DS cross-compile isn't wired into CI yet -- devkitPro publishes an
      official Docker image (`devkitpro/devkitarm`) that could host it, but that's a
      heavier follow-up, still manual-verification-only for now.

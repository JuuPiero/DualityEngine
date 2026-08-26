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
- [x] Fixed a real OBJ-import bug: `MeshLoader::Load`'s per-face-vertex parser called
      `std::strtok` again internally (to split "1/1/1" on "/") from INSIDE the outer loop that
      was ALSO mid-iteration over the same face line via its own `std::strtok` call --
      `strtok`'s position is one piece of global state shared by every call on the thread, so
      the inner call silently corrupted the outer loop's saved position. Confirmed empirically
      (a standalone repro parsed only 1 of 4 tokens in a quad face): since the triangulation
      loop needs at least 3 face-vertices to emit even one triangle, ANY face with more than 3
      vertices (a quad, the default for e.g. Blender's own unmodified cube export) silently
      contributed zero triangles -- for an all-quad mesh, that's the entire mesh, exactly a
      user-reported "dragged an .obj onto Mesh and nothing rendered" bug. Fixed by replacing
      `ParseFaceVertex`'s strtok-based parsing with a plain manual `strchr` scan (no shared
      state), which also fixes a second latent bug in the same function: strtok collapses "//"
      into a single skipped delimiter, so a `v//vn` face (position + normal, no UV -- what
      Blender exports for a mesh with no UV map) silently shifted the normal's index into the
      texcoord slot instead of leaving it absent. Also added `Log::Warn` for "file could not
      open" and "produced 0 vertices" (previously both totally silent). Verified via
      `Tests/MeshLoaderTests.cpp` (a realistic quad-faced/no-UV cube, a plain triangle mesh, and
      graceful failure on a missing file) and a scratch program confirming the fix against a
      real quad-faced test OBJ.
- [x] 3D collider gizmos (`ScenePanel.cpp`'s `DrawBoxCollider3D`/`DrawSphereCollider3D`) -- the
      Scene view's 3D pane previously had no visualization at all for `BoxCollider3DComponent`/
      `SphereCollider3DComponent` (only the 2D pane's `BoxCollider2D`/`CircleCollider2D`
      wireframes existed). Same green-wireframe, always-drawn-not-just-selected convention as
      the 2D ones, projected via the existing `Projector3D` (the same per-point technique
      `DrawCameraFrustum` already established) -- a box gets its full 12-edge wireframe,
      correctly oriented by the entity's own world rotation (unlike 2D colliders, a 3D
      BoxCollider's shape really does rotate with its body); a sphere gets three orthogonal
      great-circle rings. The SELECTED entity's collider additionally gets one draggable resize
      handle per positive axis (box: 3, one per face; sphere: 1, on the +X equator) --
      dragging edits `Size`/`Radius` directly in the viewport, via a small `DragCollider3DHandle`
      helper built on a plain ImGui `InvisibleButton` (deliberately NOT threaded through
      `EditorContext`'s `DraggingGizmoAxis`/`-Screen` the way the Translate/Rotate/Scale gizmo
      is -- that machinery exists specifically for a drag to track correctly across BOTH
      Top/Bottom panes sharing one selection, which a collider handle never needs, since it's
      only ever drawn in the one pane matching the collider's own effective screen). Verified
      via a live Editor screenshot showing the wireframe box rendering correctly around a real
      `BoxCollider3D`.
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
- [x] `BodyType` (Static/Kinematic/Dynamic, `DualityEngine/Physics/BodyType.h`) replacing the old
      plain `bool IsStatic` on `Rigidbody2D/3DComponent` -- user asked for this "như unity".
      Kinematic fills a real gap a boolean couldn't: moved directly (by a script setting
      Transform, or an animation), still generates full collision response against Dynamic
      bodies, but never affected by gravity/forces itself. Maps onto Box2D's own
      `b2_staticBody`/`b2_kinematicBody`/`b2_dynamicBody` directly; Bullet has no such enum, so
      Static and Kinematic both use mass 0 (its own "not moved by forces" convention) and
      Kinematic additionally gets `btCollisionObject::CF_KINEMATIC_OBJECT` +
      `DISABLE_DEACTIVATION` (Bullet's documented recipe, since a 0-mass body would otherwise be
      eligible to sleep). Needed a genuinely new sync direction to actually work: `OnRuntimeUpdate`
      previously only ever read physics->Transform (Static never moved, so no reverse case
      existed) -- a Kinematic body now gets its current `TransformComponent` pushed into the
      body (`b2Body::SetTransform` / `btRigidBody`'s motion state) right before each step, so a
      script driving one by mutating Transform each frame actually moves the real physics
      body. Clean-break rename, no migration shim -- `SampleProject/Assets/Scene.scene` and its
      romfs copy hand-migrated (`"Is Static": true/false` -> `"Body Type": "Static"/"Dynamic"`).
      Verified via `Tests/RigidbodyAPITests.cpp`: a Kinematic body ignores gravity while
      tracking script-driven Transform changes (both 2D/3D), and a moving Kinematic platform
      still pushes a Dynamic body out of its way (proving real collision response, not a
      trigger-like pass-through).
- [x] `Behaviour::GetVelocity2D/3D`, `SetVelocity2D/3D`, `AddForce2D/3D` (Unity's
      `Rigidbody2D.velocity`/`Rigidbody.velocity`/`AddForce`) -- routed through `EngineServices`
      (`EngineServices.h`/`Scene.cpp`'s adapters) for the same reason `PlaySound`/`GetAxis` are:
      `b2Body`/`btRigidBody` methods aren't header-only (real compiled code in
      `libbox2d.a`/`libBulletDynamics.a`), so a `GameScripts.dll` that doesn't link either
      library can't call them directly. The adapters themselves are thin wrappers around
      `Scene::Registry()` (already public, no new `Scene` method needed) +
      `GetLinearVelocity`/`SetLinearVelocity`/`ApplyForceToCenter` (2D) or
      `getLinearVelocity`/`setLinearVelocity`/`applyCentralForce` + `activate(true)` (3D, since a
      sleeping body otherwise ignores an applied force/velocity change).
- [ ] OnCollisionStay/OnTriggerStay (fires every frame while still touching, not just on
      the enter/exit edge) -- deferred from the pass above; the 3D side would mean not
      re-running the full manifold diff, just also emitting for every pair still present in
      both this frame's and last frame's touching set.
- [x] Raycast API exposed to scripts (`Behaviour::Raycast2D/3D`, `Scene::Raycast2D/3D`
      underneath) -- user asked for it specifically to click a 3D collider on the Bottom
      screen. `RaycastHit2D`/`RaycastHit3D` (`Physics/RaycastHit.h`, Unity's `RaycastHit2D`/
      `RaycastHit` -- falsy `HitEntity` when nothing was hit, same "check before reading"
      convention as every other query in this engine's scripting API) hold the closest hit's
      entity/point/normal/distance. 2D uses `b2World::RayCast` + a `ClosestRayCastCallback2D`
      (`ReportFixture` returning `fraction` clips to progressively closer hits, Box2D's own
      built-in way to get "closest" without hand-tracking it); 3D uses Bullet's own
      `btCollisionWorld::ClosestRayResultCallback` + `rayTest` directly, no custom callback
      needed. Both resolve the hit back to an `entt::entity` via the SAME `uintptr_t`/
      `b2BodyUserData::pointer` -> `uint32_t` -> `entt::entity` narrowing the collision/trigger
      event code already established (`Box2DContactListener::Record`/the Bullet manifold diff)
      -- reading the same userData/userPointer at raycast time instead of contact time, no new
      body-tagging needed. Routed through `EngineServices` (`Raycast2D`/`Raycast3D`, the same
      bridge shape `GetVelocity2D/3D`/`AddForce2D/3D` already use) since `b2World`/
      `btDiscreteDynamicsWorld` aren't header-only.
      - **`Behaviour::ScreenPointToRay3D(screen, screenPoint, outOrigin, outDirection)`** --
        the "click/touch to select a 3D object" half of the ask: converts a screen-local pixel
        point (e.g. `GetPointerPosition()`) into a world-space ray from that screen's real
        Perspective primary `CameraComponent`, ready to feed into `Raycast3D`. Builds the
        camera's forward/right/up from its Euler rotation using the exact same `T*Rz*Ry*Rx`
        composition `OpenGLRenderer3D::ComposeWorldMtx`/`Citro3DRenderer`'s own copy use to
        actually RENDER that camera (a third independent copy of that formula, matching how
        those two already keep their own copies in sync via a "must match" comment -- a
        raycast has to agree with what's actually on screen or a click hits the wrong thing),
        then the same NDC-to-ray formula `ScenePanel.cpp`'s own 3D pick-ray already uses for
        Editor Scene-view click-to-select. Returns false for a screen with no Perspective
        primary camera.
      - **Real, pre-existing gap found and fixed while wiring this up**: `Duality::Input` had
        exactly one global pointer `(x,y)` with NO screen tag at all -- Top (400x240) and
        Bottom (320x240) screen-local pixel ranges overlap, so nothing could tell "was this
        touch/click actually on the screen a raycast/UI button cares about". Fixed by adding
        `Input::SetPointer(down, position, screen)`/`GetPointerScreen()` (`Behaviour::
        GetPointerScreen()` on the scripting side) and updating every platform host that
        resolves pointer state to pass the real screen: 3DS's touch hardware is always Bottom
        (a hardware fact, not resolved code); `DualityPlayerDesktop`'s `MapWindowPointToScreen`
        already resolved which screen but is now fixed to ALSO return it (see its own real bug
        below); the Editor's `Window.cpp` used to set a raw whole-window mouse position every
        frame with no screen resolution at all (meaningless for scripts, since GamePanel's
        Play-mode images don't occupy the whole window) -- replaced with `GamePanel.cpp` itself
        resolving hover + local pixel position against each screen's own rendered `ImGui::
        Image()` rect (the same job `MapWindowPointToScreen` does on
        `DualityPlayerDesktop`), so Play-in-Editor clicking now works correctly too, not just
        the standalone/on-device builds. `UpdateUIInteractions` (`UIRenderer.cpp`) also gained
        an `if (rect.Screen != Input::GetPointerScreen()) continue;` guard, fixing a real latent
        ambiguity bug this same gap caused: a Top-screen button and a Bottom-screen button at
        the same local position could BOTH register hover/click from a single pointer event.
      - **Real, independent bug found and fixed in `DualityPlayerDesktop`'s own
        `MapWindowPointToScreen`**: its result was Y-UP within each screen (0 at that screen's
        BOTTOM edge, growing upward) -- the opposite of every other pixel-space convention in
        this engine (`OpenGLRenderer2D`/`3D`'s own `glOrtho` calls, `Citro2D`/
        `Citro3DRenderer` on device, `UIRectComponent` anchoring -- all Y-down, 0 at the top).
        Caused by mixing GLFW's own Y-down window coordinates with `TopViewport()`/
        `BottomViewport()`'s Y-up OpenGL `glViewport`/`glScissor` convention without
        re-flipping. Rewritten to compute directly in GLFW's native Y-down window space instead
        of reusing those GL-convention viewport rects.
      - Demo: `GameScripts/RaycastDemoBehaviour.cpp` (attached to a "RaycastController" entity,
        no Transform/collider of its own needed) -- on a Bottom-screen click/touch, converts
        the pointer to a ray via `ScreenPointToRay3D` and logs whichever collider `Raycast3D`
        hit (`TestCube3D`/`PhysicsGround3D`/`PhysicsBall3D` in the sample scene).
      - Verified via `Tests/RaycastTests.cpp` (6 tests: 2D/3D hit-with-correct-point/distance,
        miss, graceful failure before Play starts, `ScreenPointToRay3D`'s screen-center ray
        matching the camera's own forward axis exactly, and graceful failure with no camera/an
        Orthographic one) plus the full existing suite (50/50) and clean `build.bat`/
        `build-3ds.bat`.
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
      the general shape of what's still open elsewhere), no 9-slice/border scaling. Still a
      single global `Input` pointer (matches real hardware -- only one touch point/mouse
      cursor at a time), but it's screen-tagged now (`Input::GetPointerScreen()`, see the
      Physics section's Raycast API entry) so a button correctly only reacts to a pointer
      actually over its own screen -- fixed alongside the Raycast work above, since both needed
      the same "which screen was this pointer event for" fact.
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
- [x] "New Project..." menu item, right above "Open Project..." -- `Project::New(directory,
      name)` (`DualityEngine/Project/Project.h`) already existed and did everything needed
      (creates the directory + `Assets/`, writes the `.dproj`) but had never been wired up to
      any Editor UI. Reused `FileDialogs::SaveFile` (the same native dialog "Save Scene As..."
      already uses) instead of adding a new folder-picker dialog -- the chosen path's filename
      (minus extension) becomes both the project's Name and a same-named subfolder created
      under its parent folder, e.g. picking `F:\Projects\MyGame.dproj` creates
      `F:\Projects\MyGame\MyGame.dproj` + `F:\Projects\MyGame\Assets\`, matching
      `SampleProject`'s own existing layout (a dedicated project folder with the `.dproj` at
      its own root) and mirroring how Unity's own "New Project" dialog asks for a location +
      name and creates `location/name/` as the root. `Application::NewProjectFromDialog`
      mirrors `OpenProjectFromDialog`'s body almost exactly (same Play-stop/Scene-reset/
      Scene-view-camera-reseed/`ContentBrowserPanel::SetRootDirectory`/`AssetDatabase::Refresh`
      sequence), including keeping the trailing `SceneSerializer(...).Deserialize(...)` call
      even though a brand new project's `Assets/` is always empty -- it's a harmless no-op
      (logs a normal "could not open" error) and keeps the two functions' shape symmetric
      rather than needing a special case.
- [ ] Project Hub / recent-projects list, as its own separate window (or its own
      subfolder in this repo) -- explicitly deferred until "everything else" is more
      finished. "New Project"/"Open Project" are plain native file dialogs, no recent-project
      history.
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
- [x] Content Browser Explorer-like UX (`ContentBrowserPanel.h`/`.cpp`) -- user wanted it to
      behave more like a real Windows Explorer/folder manager. Inline rename (label becomes
      an `InputText`, `Escape` cancels, `IsItemDeactivated()` commits on both Enter and
      click-away -- same convention `Explorer`/Unity/Unreal use), Create Folder (numbered-
      suffix collision handling matching the existing `CreateMaterialAsset` pattern, then
      immediately enters rename mode per Explorer/Unity convention for a freshly-created
      folder), Delete with a confirmation modal (`BeginPopupModal`, since there's no Recycle
      Bin/undo safety net), and drag-and-drop move (dropping an `ASSET_GUID` payload onto a
      folder icon calls `MoveAssetInto`, which moves the `.meta` sidecar alongside the file
      and re-registers the same guid at its new path via `AssetDatabase::Register` so every
      existing `AssetRef` field keeps resolving with zero fixup needed). Right-click an item
      for Rename/Delete; right-click empty space gained "Create > Folder" above the existing
      Material/ScriptableObject entries. Verified via `build.bat` (clean) and the full test
      suite (unaffected, this is Editor-UI-only code with no engine-side logic) --
      interactive drag/rename/delete behavior itself could only be verified by code review
      and a static (non-interactive) screenshot of the panel rendering correctly, not a live
      click-through: synthetic mouse input in this environment proved unreliable earlier in
      the session (twice landed on an unrelated window instead of the intended target), so
      it was deliberately not attempted again for this feature.
      - **Follow-up, same panel**: "Create > Scene" (writes an empty `.scene` file via
        `SceneSerializer` against a throwaway `Scene()`, same as any other Create entry --
        doesn't open it, matching Unity's own Project-window "Create > Scene" not auto-opening
        either) and double-click a `.scene` file to open it as the active scene (`SceneOps::
        OpenScene`, see the Assets section's Scene Management entry). Also a real click-vs-drag
        bug fix, user-reported: single-click-to-select used to fire on mouse-DOWN
        (`ImGui::IsMouseClicked`), before any drag distance was evaluated -- meaning starting a
        drag (to drop an asset onto an `AssetRef` field) ALSO immediately reselected that asset
        in Properties, the exact pain the "Lock" toggle above exists to work around. Fixed by
        using the `InvisibleButton`'s own return value instead (fires on mouse-RELEASE while
        still hovering the item it was pressed on -- standard ImGui button semantics, which
        already correctly withholds "clicked" once a drag-drop source activates from the same
        item) -- Lock is no longer *required* just to drag-and-drop an asset, though it's still
        useful for pinning Properties while browsing elsewhere.
- [x] Unity/Cocos-style Scene view grid + 3D orientation gizmo (`ScenePanel.cpp`), both panes
      (Top and Bottom) -- user asked directly, referencing Unity/Blender screenshots. 2D pane:
      world-aligned grid lines every `GridCellSize2D(zoom)` units (stepped, not one fixed size,
      so it stays readable across the whole 0.1x-5x zoom range) plus a red X-axis/green Y-axis
      line through the world origin -- drawn via a new `OpenGLRenderer2D::DrawGrid` (real GL
      calls, not a screen-space `ImDrawList` overlay, specifically so it renders BEHIND sprites
      -- an overlay drawn after `ImGui::Image()` would sit on top of the whole already-
      composited framebuffer, including every opaque sprite). 3D pane: an XZ ground-plane grid
      (`DrawGrid3D`, same `Projector3D::Project`-per-line-segment overlay technique
      `DrawCameraFrustum`/`DrawBoxCollider3D` already use, red X/blue Z axis lines) re-centered
      near the orbit camera's own Target every frame, plus a Blender/Unity-style ViewCube-
      equivalent fixed in the pane's own top-right corner (`DrawAndInteractOrientationGizmo3D`)
      showing world X/Y/Z relative to the current camera orientation -- clicking a filled tip
      snaps the orbit camera to look straight down that axis (keeping Target/Distance, only
      Rotation changes; hit-tested BEFORE the pane's own entity-pick-ray so a gizmo click never
      also selects/deselects whatever mesh is underneath it).
      - **Grid stability fix, user-reported** ("dễ bị mất hiển thị các nét vẽ của grid... lúc
        thu phóng thao tác chuột" -- grid lines easily go missing while zooming/panning):
        `Projector3D::Project` has no clipping of its own (a point either projects or reports
        failure), so a LONG grid line (unlike the single points every other gizmo in this pane
        projects) needs its own near-plane clip -- without one, a line with just ONE endpoint
        dipping behind the camera vanished ENTIRELY (both-endpoints-must-project) instead of
        being clipped at the visible edge. Root cause of the *specific* flicker (not just the
        one-time "vanish," which the first clip fixed): clipping right at the bare minimum
        "still technically in front" threshold still allowed a line nearly parallel to the view
        direction (near the horizon) to have a near-zero view-space depth, which blows its
        projected screen coordinates up to an enormous magnitude -- ImGui/the GPU rasterizing
        near-infinite line endpoints, not the clip logic itself, is what actually produced the
        flicker. Fixed with two changes: `ClipSegmentToCameraFront`'s near distance is now a
        meaningful chunk of a grid cell (not a bare epsilon), and a second `IsReasonableScreenPoint`
        check rejects any projected point landing absurdly far outside the viewport regardless.
        Also made the grid patch's `extent` scale with the orbit camera's own `Distance`
        (bounded to 10-40 grid squares) instead of one fixed large size regardless of zoom, so
        lines are less likely to need clipping in the first place.
      - Verified via clean `build.bat`/`build-3ds.bat` + the full test suite (unaffected,
        Editor-rendering-only code) each pass; the interactive zoom/pan behavior itself was
        verified by the user directly (this environment's synthetic-click unreliability, noted
        earlier this session, ruled out automated interactive verification here too).
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
- [x] Async "Reload Scripts" (`DualityEditor/ScriptEngine.h`'s `ReloadAsync`/`ReloadStatus`) --
      user reported clicking it made the Editor "look like it closed". Root cause: `Reload`
      called `std::system(...)` synchronously on the UI thread, freezing the whole ImGui loop
      for the rebuild's duration (confirmed DualityEditor is a console-subsystem app, no
      `WIN32` on its `add_executable` -- so no separate console window pops up during the
      freeze; the freeze itself was the whole problem). Mirrors `BuildFor3DSAsync`'s existing
      detached-thread + `std::atomic<Status>` pattern exactly rather than inventing a second
      one. `GamePanel`'s button relabels to "Reload Scripts (reloading...)" and disables while
      running, same as the Build menu items. Cross-guarded both ways against `BuildPipeline`'s
      own status, since `ScriptEngine::Reload` and `BuildForPC` both run `cmake --build`
      against the exact same Ninja tree (`GameScripts`/`DualityPlayerDesktop` targets) --
      mirrors how `BuildFor3DSAsync`/`BuildForPCAsync` already share one status flag for the
      same "one background build task at a time" reason, even though a 3DS build and a PC
      build target different trees. Play must already be stopped synchronously on the main
      thread before the background thread starts (`Reload`'s `FreeLibrary` would otherwise
      yank `GameScripts.dll` out from under a live `Behaviour*` mid-`OnRuntimeUpdate`) --
      `GamePanel`'s existing `StopPlaying(ctx)` call stays exactly where it was for this reason.
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
- [x] Scene management upgrade (`Scene::Clear`, `SceneSerializer::SerializeToJson`/
      `DeserializeFromJson`, `DualityEditor/SceneOps.h`) -- user-reported bug: "Load Scene"
      didn't clear the existing scene first, it just ADDED every loaded entity alongside
      whatever was already there. Root cause: `MenuBarPanel`'s "Load Scene" called
      `SceneSerializer::Deserialize` directly on the live scene -- `Deserialize` itself never
      clears (by design, callers decide when that's appropriate, e.g. a Prefab's `Instantiate`
      deliberately does NOT clear the target scene), and this one call site was the only one
      that didn't. Fixed at the root by adding `Scene::Clear()` (`m_Registry.clear()` +
      `m_RootEntities.clear()` -- NOT just the registry alone, which would leave
      `m_RootEntities` holding dangling handles into now-destroyed entities) and a shared
      `SceneOps::OpenScene(ctx, path)` free function (stops Play if running, `Clear()`s,
      updates `ctx.ScenePath`, re-seeds the Scene view's free-roam cameras, deserializes) that
      "Load Scene", "Open Scene..." (new file-dialog menu item), the Content Browser's
      double-click-to-open, and the Scene asset inspector's new "Open Scene" button all share --
      one implementation, not four copies of the clear-before-load invariant.
      - **Play->Stop now actually reverts, matching Unity's own guarantee**: previously Play
        just ran `OnRuntimeStart`/`OnRuntimeStop` directly on the live scene, so anything Play
        did -- script-driven Transform edits, physics results, runtime-`Instantiate`d/destroyed
        entities -- silently persisted into Edit mode after Stop. Fixed by adding
        `SceneSerializer::SerializeToJson()`/`DeserializeFromJson()` (in-memory JSON variants,
        no disk I/O -- the existing file-based `Serialize`/`Deserialize` are now thin wrappers
        around these, sharing 100% of the entity-walking logic) so `GamePanel`'s "Play" button
        captures a snapshot (`ctx.PlaySnapshot`, an Application-owned `std::string`) right
        before `OnRuntimeStart()`, and "Stop" (plus "Reload Scripts"' own mid-Play stop, which
        now goes through the identical path) does `OnRuntimeStop()` -- real cleanup, firing
        final `OnDisable`/`OnDestroy` and freeing the physics worlds, on the scene that was
        actually playing -- THEN `Clear()` + `DeserializeFromJson(snapshot)` to restore exactly
        what was there before Play started.
      - Content Browser: "Create > Scene" and double-click-to-open (see its own entry above).
      - Verified via `Tests/SceneManagementTests.cpp` (`Scene::Clear()` resets both the
        registry and root list and leaves the Scene fully reusable afterward; a full in-memory
        serialize/mutate/clear/restore round trip proving a Play-mode mutation doesn't survive;
        graceful failure on a malformed snapshot) plus the full suite (44/44 at the time) and
        clean `build.bat`/`build-3ds.bat`.
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

- [x] Per-script public/serialized fields shown in the Properties panel, like Unity's
      `[SerializeField]` -- user asked for a `DUALITY_PROPERTY() float speed;` per-field
      marker macro, mirroring Unreal's `UPROPERTY()`. Confirmed not achievable in plain C++
      without a code-generation pass scanning the header for that marker (this project has
      none) -- a macro attached to one field declaration has no way to see its own name/type
      or reach into a class-wide field list. Instead built `DUALITY_PROPERTIES(ClassName,
      field1, field2, ...)` (`Reflection/PropertyMacros.h`, included by `Behaviour.h`),
      inspired by the ImGui-ecosystem `ImReflect` library's `IMGUI_REFLECT(Type, field...)`
      shape the user pointed at (github.com/ocornut/imgui/wiki/Useful-Extensions) -- one
      class-level macro listing already-declared field names once, expanding via a portable
      variadic FOR_EACH (GCC/Clang-conformant preprocessor, matching both this project's
      MinGW desktop build and devkitARM's GCC) into the same `static std::vector<FieldHandle>
      Fields()` method ScriptableObject/Material already hand-write. Entirely optional --
      `ScriptRegistrar` detects `T::Fields()` via a `std::void_t` SFINAE check
      (`ScriptRegistration.h`), so every pre-existing script with no `DUALITY_PROPERTIES` line
      keeps compiling with zero Inspector fields, unlike ScriptableObject where `Fields()` is
      mandatory. New `EntityRef` field type (`Reflection/Field.h`) covers "ref to another
      entity"; there's no separate "component reference" type -- a script holds an `EntityRef`
      and calls `Behaviour::ResolveEntityRef(ref).GetComponent<T>()` itself, same as Unity's own
      `[SerializeField] GameObject` fields. `BehaviourComponent::PropertyOverrides`
      (`std::unordered_map<std::string, FieldValue>`, not TypeRegistry-reflected -- a map isn't
      a plain `FieldValue`) holds Edit-mode values; `Scene::OnRuntimeStart` applies them onto
      the freshly-created instance's real fields right before `OnCreate()` (Unity's
      field-init-before-Awake ordering). While Play is running, the Properties panel edits the
      live `BehaviourComponent::Instance` directly instead (lost on Stop, matching Unity); in
      Edit mode (no live instance) it spins up a scratch instance for one frame, seeded from
      `PropertyOverrides`, to render/edit against. `EntitySerialization.cpp` special-cases
      `"PropertyOverrides"` alongside `"Class"` for save/load, same bespoke-field precedent as
      `HierarchyComponent::Parent`. Scope cut, clearly documented rather than silently broken:
      an `EntityRef` value never round-trips through save/load -- a raw `entt` handle isn't
      stable across a reload (entities get freshly assigned handles in creation order), so
      persisting it would silently resolve to the wrong entity; `FieldValueToJson`/
      `JsonToFieldValue` always write/restore it as "unset" instead. Demo:
      `GameScripts/BounceBehaviour` gained `Amplitude`/`Speed`/`Target` (an `EntityRef` --
      when set, bounces relative to that other entity's current Y instead of a fixed height).
      Verified via `Tests/DualityPropertyTests.cpp` (macro generates `Fields()` in declaration
      order, a class with no `DUALITY_PROPERTIES` resolves to `{}` rather than a compile error,
      overrides apply before `OnCreate` sees them, no-override entities keep their compiled-in
      defaults, and a full `SceneSerializer` round trip preserving float/bool overrides while
      confirming `EntityRef` always reloads unset) plus a clean `build.bat`/`build-3ds.bat`.
- [x] **Superseded the entry above**: `DUALITY_PROPERTIES(ClassName, field1, ...)` still made
      every field's name get typed twice (once in the member declaration, once in the macro's
      argument list) and didn't read like a real per-field attribute -- user asked again for
      something closer to Unreal's `UPROPERTY()`. Revisited the earlier "not achievable in
      plain C++" conclusion: it's still true that a bare macro can't see its own attached
      declaration on its own, but a *real code-generation pass* (exactly what Unreal Header
      Tool does, and what Polyphase-Engine's own C# scripting layer does via a Roslyn
      source-rewriter -- confirmed by reading both) solves it properly. Chose this over an
      X-Macro alternative (single-source-of-truth field list, no new build tooling, but an
      unfamiliar syntax) when asked. Built `GameScripts/CodeGen/generate_fields.py` -- a
      deliberately plain regex/line scanner, NOT a real C++ parser (every `FieldValue`-
      supported type is a single token with no embedded spaces, so this is fully reliable) --
      that scans every header under `GameScripts/Include/` for `DUALITY_PROPERTY() <Type>
      <Name> [= <Default>];` and emits `GameScripts/Generated/ScriptFields.generated.cpp`
      (one `ClassName::Fields()` definition per class, `#include`-ing only the headers that
      had a match). `PropertyMacros.h` now defines just `DUALITY_PROPERTY()` (a real no-op at
      compile time) and `DUALITY_PROPERTIES_AUTO()` (a bare `static std::vector<FieldHandle>
      Fields();` declaration the generated `.cpp` then defines out-of-class) -- the old
      FOR_EACH macro machinery was deleted outright rather than kept alongside it. Wired into
      `GameScripts/CMakeLists.txt` as an `add_custom_command` pre-build step, both build
      modes. **Real environment gotcha hit and fixed**: the desktop build (`build.bat`, plain
      cmd.exe) finds a normal `python.exe` via PATH with zero extra config, but devkitPro's own
      bundled MSYS2 (which `build-3ds.bat`'s Makefile-generator configure uses) has no Python
      on ITS PATH at all -- `find_program(... NAMES python python3 PATHS
      "E:/App/Python/Python312")` searches PATH first, falling back to this machine's
      known-good install location, the same "hardcode the one known-good path" lesson this
      file's own devkitPro-path notes already established. Confirmed working end-to-end on
      both `build.bat` and `build-3ds.bat` -- the generated file's content inspected directly
      and matches exactly what a human would have hand-written.
- [x] Multiple scripts per entity, Unity-style Add Component -- `BehaviourComponent` used to be
      a single EnTT component type holding one `ClassName`/`Instance` pair, capping an entity
      at exactly one script; user wanted each registered script class to show up as its own
      addable entry, with an entity able to run several different scripts (or the same one
      twice) at once. Chose to keep `BehaviourComponent` as ONE EnTT component type wrapping a
      `std::vector<ScriptInstance>` (each slot = what the old component's fields used to be)
      rather than building true dynamic per-class EnTT component types -- EnTT has no
      first-class support for runtime-registered component types, and this project's
      reflection is built around compile-time pointer-to-member anyway (unlike scripts' own
      already-type-erased `FieldHandle`), so the dynamic-types route would have been a much
      larger, riskier change for the same user-visible result. `TypeRegistry` entry renamed
      "Behaviour" -> "Scripts" (zero reflected fields of its own now -- fully special-cased in
      `PropertiesPanel.cpp`/`EntitySerialization.cpp`, same "reflected fields + opaque runtime
      state" split `Rigidbody2DComponent`'s `RuntimeBody` already established, just covering
      the whole struct this time). Properties panel renders each slot as its own card (own
      header, own "..." > Remove Script); the Add Component popup gained a nested "Add Script"
      submenu listing `ScriptRegistry::GetAllClassNames()` (new -- the registry previously had
      no enumeration method, only lookup-by-name). One-time on-disk schema break (`"Behaviour"`
      JSON key -> a `"Scripts"` array) for existing saved scenes, same accepted precedent as
      the earlier Name/Tag split -- self-corrects on next Save. Verified via
      `Tests/MultiScriptTests.cpp` (two different scripts on one entity fire
      OnCreate/OnUpdate/OnDestroy independently with genuinely separate instances; the same
      class attached twice also runs as two independent instances) plus a live Editor smoke
      test (two-script entity showing two separate cards in Properties).
- [x] Per-project gameplay scripts -- every script used to live in ONE shared top-level
      `GameScripts/` folder, hand-listed in `GameScripts/CMakeLists.txt`; adding a script for
      `SampleProject/` specifically meant editing engine-repo files that had nothing to do
      with that project. Gave each `Project` its own `Assets/Scripts/` folder (new
      `ProjectConfig::ScriptsDirectory` field, default `"Scripts"`, matching
      `AssetsDirectory`'s own existing string-concat pattern -- `Project::GetScriptsDirectory()`),
      matching Unity's own convention, compiled into the SAME `GameScripts` module alongside the
      engine's shared/demo scripts rather than a second DLL/target (one `ScriptRegistry`, one
      flat class-name namespace -- two modules would mean two registries, two
      `GetScriptFactories()` exports, for no real benefit). `GameScripts/CMakeLists.txt` gained
      a `DUALITY_PROJECT_SCRIPTS_DIR` cache variable (set via `-D` by `ScriptEngine::Reload`/
      `BuildPipeline::BuildFor3DS`, sourced from `Project::GetActive()`) feeding a guarded
      `file(GLOB_RECURSE)` alongside the existing hand-listed sources -- the one deliberate
      GLOB-based compiled source list in this project (every other target hand-lists files),
      justified since the whole point is "no manual file-list editing for project scripts."
      `generate_fields.py` (see the `DUALITY_PROPERTY()` entry above) now scans TWO directories
      -- `GameScripts/Include/` and the active project's own `Scripts/` -- so a project's own
      `DUALITY_PROPERTY()` fields get identical generated-`Fields()` treatment; reshaped its CLI
      from `<include_dir> <output>` to `<output> <scan_dir_1> [scan_dir_2 ...]` and switched its
      `#include` lines to always use each header's full absolute path (works regardless of which
      directories are on `GameScripts`' own include path -- project scripts deliberately live
      flat, `.h` next to `.cpp`, resolved via the compiler's own same-directory quoted-include
      search rather than adding a second include directory). `ScriptEngine::Reload` now
      reconfigures (`cmake -B <dir> -D...`, no `-S` needed -- an already-configured tree reads
      its stored source dir back out of its own `CMakeCache.txt`) before building, since CMake's
      build graph is fixed at configure time but the active Project is only known at Editor
      runtime; `Application::OpenProjectFromDialog`/`NewProjectFromDialog` both call
      `ScriptEngine::ReloadAsync` right after switching projects so the new project's scripts
      compile+load with no extra click (safe to be async even though `Deserialize` runs right
      after -- it only stores `ScriptInstance::ClassName` strings, doesn't need `ScriptRegistry`
      to already know the class until Play actually starts). New Content Browser **"Create >
      Script"** (`ContentBrowserPanel::CreateScriptAsset`) writes a ready-to-edit `.h`/`.cpp`
      pair with the `REGISTER_BEHAVIOUR` boilerplate already in place -- deliberately NOT the
      same `"NewScene (1).scene"`-style numbered suffix every other Create-asset flow uses,
      since a script's filename has to stay a valid C++ identifier (it's also the class name):
      `NewBehaviour`, `NewBehaviour1`, `NewBehaviour2`, ... instead. No `AssetMeta`/
      `AssetDatabase` registration -- scripts are looked up by class name, never referenced by
      GUID like a real asset.
      **Two real, non-obvious build-environment bugs found and fixed** (both confirmed via
      direct `cmake -P`/manual-configure experiments, not guessed): (1) declaring
      `DUALITY_PROJECT_SCRIPTS_DIR` as `CACHE PATH` (the obviously-correct-looking type for "this
      holds a directory") silently corrupted the value under the MSYS-hosted `cmake.exe` the 3DS
      configure uses (`build-3ds.bat`'s own comments explain why it must be that one) -- a
      `PATH`-typed cache variable set via `-D` gets validated/normalized as a path, and that
      cmake's Cygwin/MSYS internals don't recognize a Windows-style `"F:/..."` value as
      already-absolute, so it got the repo source dir silently prepended onto it (confirmed:
      `"/f/.../DualityEngine/F:/..."`). Fixed by declaring it `CACHE STRING` instead, which
      CMake never normalizes. (2) Even with a clean STRING value, `file(GLOB_RECURSE)` and
      `EXISTS` themselves ALSO silently fail (return nothing / false, no error) against a
      Windows-style drive-letter path under that same MSYS-hosted cmake, for the identical
      Cygwin-path-semantics reason -- fixed by deriving a second, MSYS-mount-notation copy
      (`"F:/Workspace/..."` -> `"/f/Workspace/..."`, the exact transform `build-3ds.bat`'s own
      `DKP_MSYS` derivation already uses for `DEVKITPRO`) inside `GameScripts/CMakeLists.txt`
      itself, gated to `CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS"` so the already-correctly-behaving
      desktop (mingw64-native) cmake is untouched, used only for the `file(GLOB)`/`EXISTS` calls
      -- `generate_fields.py`'s own invocation keeps the original Windows-style path unchanged,
      since it runs through a native Windows `python.exe` that needs that form, not MSYS
      notation. Verified end-to-end: a real project script with a `DUALITY_PROPERTY()` field,
      created via the live Content Browser "Create > Script" flow, confirmed compiling into
      both the desktop `GameScripts.dll` AND the STATIC 3DS-linked `GameScripts` library, its
      generated `Fields()` entry inspected directly, and a live Play-mode run showing its
      `OnCreate()` log line in the Console.
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

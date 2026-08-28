# DualityEngine

A from-scratch C++ game engine + Unity/Cocos-Creator-style drag-and-drop
editor targeting the Nintendo 3DS (devkitARM/devkitPro), developed on
Windows. The same `DualityEngine` core (ECS, reflection, scene
serialization, 2D physics via Box2D, 3D physics via Bullet) compiles for
three targets from one source tree:

- **Desktop** -- `DualityEditor.exe`, the drag-and-drop editor (Dear ImGui).
- **Desktop standalone** -- `DualityPlayerDesktop.exe`, a real GLFW window
  running the same project with no Editor UI at all (an "export to
  Windows" build).
- **3DS** -- `DualityPlayer`, the actual game runtime, built as `.3dsx`
  (Homebrew Launcher / `3dslink`) and `.cia` (installable via FBI on real
  hardware/CFW).

Both 2D (sprites, flipbook animation) and 3D (unlit meshes, OBJ import) render
and physics-simulate side by side on the same screen -- a camera's projection
is just a lens property (Unity's own convention), never a switch between
mutually exclusive pipelines.

![Demo](Resource/Engine.png)
![Demo](Resource/Build.png)


New here? `GETTING_STARTED.md` is a hands-on walkthrough (build, tour the Editor,
build a scene, write your first script) -- this file covers architecture, the full
build system, and every feature's details instead. See `ROADMAP.md` for what's
planned but not built yet.

## Prerequisites

1. **devkitPro**, with the 3DS packages installed (`devkitARM`, `citro2d`,
   `citro3d`, `3dstools`, `3ds-cmake`, `general-tools`, etc. -- installed via
   the devkitPro updater/pacman). The `DEVKITPRO` environment variable
   should point at the install (typically `E:\App\devkitPro` or
   `C:\devkitPro`).
2. **A MinGW-w64 toolchain for the desktop build.** devkitPro bundles its
   own MSYS2 install, which does not include this by default -- add it once
   from an MSYS2 shell:
   ```
   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-glfw \
             mingw-w64-x86_64-glew mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
   ```
3. **Citra** (or a real 3DS), to run the `.3dsx`/`.cia` output.

If your machine already has another MSYS2/MinGW/Ninja install, make sure it
doesn't shadow devkitPro's on `PATH` -- `build.bat` explicitly prepends
devkitPro's own `msys2\mingw64\bin` to avoid exactly that conflict.

## Desktop: build and run the Editor

```
build.bat               REM configures + builds the Editor, standalone player, and Tests/
run.bat                 REM launches the Editor (sets PATH so glfw3.dll/glew32.dll resolve)
run-desktop-player.bat  REM launches the standalone player (build.bat's DualityPlayerDesktop.exe)
run-tests.bat           REM runs the automated engine-logic test suite (see "Testing" below)
```

`build.bat` re-configures automatically if the build cache looks stale.

### Using the Editor

On launch, the Editor opens (creating if needed) a fixed sample project at
`SampleProject/` next to the executable. A different project can be created
or opened via the File menu.

- **File menu** (top) -- **New Project...** (pick a location + name via a
  native Save dialog -- e.g. choosing `F:\Projects\MyGame.dproj` creates
  `F:\Projects\MyGame\MyGame.dproj` + `F:\Projects\MyGame\Assets\`, a
  dedicated project folder with the `.dproj` at its own root, matching
  Unity's own "New Project" flow), **Open Project...** (browse for a
  `.dproj` file; swaps the active project/scene/Content Browser root,
  stopping Play first if it's running), **Save Scene** / **Load Scene**
  (JSON round-trip to
  `<project>/Assets/Scene.scene` -- Load Scene clears the current scene
  first, then loads: it used to just add every loaded entity alongside
  whatever was already there, a real reported bug), **Open Scene...**
  (browse for a different `.scene` file and switch to it -- unlike Save
  Scene As... below, this DOES change which scene Save/Load Scene operate
  on afterward), **Save Scene As...** (writes a *snapshot* of the current
  scene to a new file the user picks, without changing which scene Save/
  Load Scene operate on -- this is how a project gets a second scene file
  for `SceneManager`/`Behaviour::LoadScene` to target), and **Build for
  3DS** (saves the scene, then runs `build-3ds.bat`, see below, so the
  on-device build always packages the latest scene), and **Build for PC**
  (saves the scene, then rebuilds `DualityPlayerDesktop` in the existing
  desktop build tree -- no romfs/asset-cooking step needed, since that
  target reads a project's `Assets/` directly off disk at its own launch
  time; run it via `run-desktop-player.bat` once built), and **Build
  Settings...** (a separate window -- see below). Both build actions share
  one build-status flag, so they can't run concurrently against the same
  build tools. The menu bar also shows which scene file is currently active
  (`Scene: <filename>`), since **Open Scene...**/a Content Browser
  double-click/the Scene asset inspector's own "Open Scene" button can all
  change it.
- **Build Settings...** window (File menu) -- Unity's own Build Settings
  dialog: a **Scenes In Build** list (drag a row to reorder, "x" to remove;
  the topmost entry is the Start Scene) plus an auto-scanned **Other Scenes
  In Project** list to add from, and a **Build for 3DS** button + progress
  bar right in the window. Backed by a persisted `ProjectConfig::ScenesInBuild`
  field: once configured, only listed scenes are cooked into a 3DS build and
  the topmost one ships as the on-device boot scene (an empty/unconfigured
  list falls back to the old behavior -- ship every `.scene` found, package
  whatever's currently open).
- **Edit menu** (top) -- **Project Settings...** (Name/Assets Directory/Scripts
  Directory are read-only for this first pass; an **Icon (3DS)** section is
  editable though -- preview + **Browse...** to pick a PNG, persisted as
  `ProjectConfig::IconPath` and threaded into the actual `.cia` build's
  `bannertool makesmdh` step, falling back to the engine's placeholder icon
  when unset) and **Preferences...** (pick an external
  editor executable via **Browse...**, then **Open Project in External
  Editor** launches it with the active project's folder as its argument --
  persisted per-machine, not per-project, at
  `%APPDATA%\DualityEngine\EditorSettings.json`).
- **Hierarchy** (left) -- lists entities in the scene; click one to select
  it. **Create Entity** adds a new empty entity and selects it. A search box
  filters to a flat scene-wide list of matching entities while it has text
  (tree/drag-drop return once it's cleared). Right-click an entity for
  **Create Child Entity** or **Create Prefab from Selection** (writes
  `Assets/Prefabs/<Name>.prefab`); drag a Prefab asset from the Content
  Browser onto empty space in this panel to instantiate it.
- **Properties** (right) -- shows every component on the selected entity,
  fields fully generic (reflection-driven -- adding a new component/field
  never needs editor code changes). Every entity has an **Active** checkbox
  (Unity's `GameObject.SetActive`) alongside Transform/Name/Tag. Each
  non-mandatory component has a "..." button to remove it; **+ Add
  Component** at the bottom lists every component type not already on the
  entity. **Lock** (top, Unity-Inspector-style) pins the panel to whatever's
  currently shown, ignoring further Hierarchy/Scene/Content Browser
  selections until unlocked -- needed because clicking a Content Browser
  thumbnail to start dragging it (e.g. a Material/Texture onto an `AssetRef`
  field here) fires on mouse-down -- fixed to fire on release-while-still-
  hovering instead (`ContentBrowserPanel`'s own thumbnail button), so Lock
  is no longer *required* just to drag-and-drop an asset (still useful for
  pinning Properties while browsing elsewhere).
- **Scene** (center, tabbed with Game) -- one free-roam edit camera per
  screen, independent of any in-scene camera, with a **2D | 3D** toggle per
  pane. Both draw a Unity/Cocos-style grid (world-aligned lines, a red
  X-axis/green Y-or-Z-axis line through the origin) so you always have a
  sense of scale/position. 2D: middle-drag to pan, wheel to zoom, left-click
  a sprite to select it. 3D: left-click/drag to select and gizmo-drag,
  right-drag to orbit (a genuine quaternion orbit -- no gimbal-lock clamp,
  it flips smoothly through the poles like Unity/Blender), middle-drag to
  pan, wheel to zoom; a Blender/Unity-style orientation gizmo sits in the
  pane's top-right corner -- click one of its axis tips to snap the view to
  look straight down that axis. The viewport always exactly fills the
  panel. Camera entities draw a real frustum wireframe (near/far planes,
  FOV) instead of a flat marker, so you can see exactly where a camera is
  looking, and a camera's **Background** color field (Properties, Unity's
  `Camera.backgroundColor`) is what that screen actually clears to whenever
  a camera is present. **Translate / Rotate / Scale** buttons switch the selected
  entity's gizmo mode -- drag a handle to edit its Transform live, visible
  immediately in Scene, Game, and on a real 3DS build. `Transform.Rotation`
  is always in degrees. Colliders
  (`BoxCollider2D`/`CircleCollider2D`/`BoxCollider3D`/`SphereCollider3D`)
  draw as a green wireframe outline for the SELECTED entity only -- an
  editor-only visualization, like Unity's Gizmos, never actually rendered
  in the Game view or on-device. A per-collider **Edit** checkbox in
  Properties (`EditMode`, off by default) gates whether that wireframe's
  resize handles actually respond to a drag -- one handle per axis for 2D
  (`Size.x`/`Size.y`, or a single radius handle) and 3D (box: one per face;
  sphere: one on the equator) alike, editing `Size`/`Radius` directly in
  whichever Scene pane the collider is drawn in.
- **Game** (center, tabbed with Scene) -- exactly what the real
  TopCamera/BottomCamera entities render, stacked to match the console's
  physical layout (Top 400x240 above, Bottom 320x240 below). This is the
  same render pass the 3DS build itself uses. **Play / Stop** runs the full
  runtime lifecycle (`Behaviour` scripts, 2D/3D physics, collision/trigger
  events, UI) against the live scene and, on Stop, reverts every change
  Play made -- script-driven Transform edits, physics results, runtime-
  spawned/destroyed entities -- back to exactly how the scene looked the
  instant Play was pressed, matching Unity's own Play/Stop guarantee.
  **Reload Scripts** rebuilds `GameScripts` and hot-reloads it into the
  running Editor without restarting (stopping -- and reverting -- Play
  first if it's running) -- edit a script's `.cpp`, click this, see the
  change immediately. Runs on a background thread (the button relabels to
  "Reload Scripts (reloading...)" and grays out, same as Build for
  3DS/PC), so the Editor's UI stays responsive during the rebuild instead
  of freezing for its duration. Clicking/touching either screen's image here (while
  Playing) is correctly resolved to that screen's own local pixel
  coordinates, so `Behaviour::GetPointerPosition()`/`ScreenPointToRay3D`
  work the same in Play-in-Editor as they do on `DualityPlayerDesktop` or a
  real 3DS.
- **Console** (bottom, tabbed with Content Browser) -- `Duality::Log`
  output (including `Behaviour::LogInfo/LogWarn/LogError` calls from
  scripts), color-coded and filterable by level (Trace/Info/Warn/Error),
  plus a text search box and a Clear button.
- **Content Browser** (bottom, tabbed with Console) -- browses
  `SampleProject/Assets/` as a grid of icons, with real thumbnails for image
  files, and a search box that filters files by name within the current
  folder (folders themselves stay visible so navigation keeps working).
  Every asset gets a companion `<name>.meta` file (a stable GUID,
  Unity/Unreal-style) generated the first time the Content Browser sees it;
  `.meta` files themselves are hidden from the grid. Drag an asset out onto
  an `AssetRef` field in Properties (a texture, a Material, a Prefab, ...)
  to assign it (referenced by its GUID, so renaming/moving the file later
  doesn't break it); drag a file in from Windows Explorer to import it into
  whatever folder is currently open. Click a file once to select it (fires
  on release, not press, so starting a drag doesn't also reselect the file
  being dragged) --
  Properties shows the right Inspector for its type, dispatched by file
  extension through `AssetInspectorRegistry` (`DualityEditor/
  AssetInspectors.cpp`) the same way `TypeRegistry` dispatches Entity
  components: `.mat`/`.asset` (Material/ScriptableObject) are fully editable,
  auto-saving on every change; `.prefab`/`.scene` show a read-only summary
  (entity count, root/top-level entity names -- editing a Prefab or Scene
  asset in place isn't built yet, see "Known limitations"); a `.scene`'s
  Inspector also has an **Open Scene** button, and double-clicking a
  `.scene` file (instead of single-clicking to select it) opens it directly
  as the active scene; an image or
  `.wav` shows its **Import Settings** (Filter Mode/Wrap Mode/Generate
  Mipmaps for a texture, Volume for audio) -- a Unity/Cocos-style importer
  block written into that asset's own `.meta` file (alongside its existing
  `"guid"` key) rather than the asset itself, since a `.png`/`.wav` is a
  foreign binary format this engine doesn't own. Right-click empty space for
  **Create > Folder / Scene / Material / ScriptableObject > &lt;Type&gt;**
  (the latter listing every class GameScripts currently has
  `REGISTER_SCRIPTABLE_OBJECT`'d); right-click an existing file or folder for
  **Rename** (label becomes an editable text field, Enter or click-away
  commits, Escape cancels) or **Delete** (asks for confirmation first --
  there's no Recycle Bin/undo here). Drag a file onto a folder icon to move
  it there (its `.meta` sidecar moves with it, and its GUID keeps resolving
  to the new path, so nothing referencing it needs fixing up).

### Writing gameplay scripts

Scripts compile into one `GameScripts` module either way -- a hot-reloadable
DLL for desktop Play-in-Editor, or linked statically straight into
`DualityPlayer` for the 3DS build (same source files, no code changes either
way) -- from two possible locations:

- **A project's own scripts** (the common case): right-click in the Content
  Browser (inside `Assets/Scripts/` or anywhere else under the project's
  `Assets/`) and pick **Create > Script**. Writes a ready-to-edit `.h`/`.cpp`
  pair (`NewBehaviour`, `NewBehaviour1`, ... if the plain name is taken --
  unlike Scene/Material's `"NewScene (1)"` numbering, a script's filename has
  to stay a valid C++ identifier, since it also becomes the class name) with
  the `REGISTER_BEHAVIOUR` boilerplate already in place. No `CMakeLists.txt`
  editing needed -- every `.cpp` under the active project's own
  `Assets/Scripts/` (recursively) is picked up automatically the next time
  **Reload Scripts** runs, alongside the engine's own shared scripts below.
- **A shared engine-level script** (for something meant to ship as an
  example/demo alongside the engine itself, like `BounceBehaviour`): add a
  class deriving from `Duality::Behaviour` under `GameScripts/Include/` +
  `Source/` by hand (see `BounceBehaviour.h`/`.cpp` for the pattern), then
  add the new source file to `GameScripts/CMakeLists.txt`'s explicit list.

Either way:

1. Override whichever lifecycle methods the script needs (see below).
2. Register it with `REGISTER_BEHAVIOUR(YourClassName)` at the bottom of its
   `.cpp` file (already there if created via Content Browser).
3. In the Editor, select an entity, click **+ Add Component > Add Script**,
   and pick the class from the submenu (every `REGISTER_BEHAVIOUR`'d class
   shows up there automatically -- project-owned and engine-shared scripts
   both, no distinction once compiled), then click **Reload Scripts**. One
   entity can carry any number of different scripts at once (or the same one
   more than once), matching Unity letting one GameObject carry many
   MonoBehaviours -- each gets its own card in the Properties panel, with
   its own "..." > Remove Script.

### Inspector-editable fields

A script's own public fields can show up in the Properties panel, like
Unity's `[SerializeField]` -- a real per-field attribute, Unreal
`UPROPERTY()`-style:

```cpp
class BounceBehaviour : public Duality::Behaviour {
public:
    DUALITY_PROPERTY() float Amplitude = 40.0f;
    DUALITY_PROPERTY() float Speed = 8.0f;
    DUALITY_PROPERTY() Duality::EntityRef Target;   // drag an entity from the Hierarchy onto this field

    DUALITY_PROPERTIES_AUTO()
    ...
};
```

`DUALITY_PROPERTY()` marks a field; `DUALITY_PROPERTIES_AUTO()` (one line,
no arguments) declares the class's `Fields()` method. Both are entirely
optional -- a script with neither just has no Inspector fields, exactly like
before this feature existed. The actual `Fields()` *definition* is
generated by `GameScripts/CodeGen/generate_fields.py`, a small pre-build
step (wired into `GameScripts/CMakeLists.txt`) that scans every header
under `GameScripts/Include/` for the `DUALITY_PROPERTY()` marker and emits
`GameScripts/Generated/ScriptFields.generated.cpp` -- the same real
code-generation approach Unreal Header Tool and Polyphase-Engine's own C#
scripting layer use, since a bare per-field marker macro genuinely can't
see its own attached declaration's name/type through the preprocessor
alone. There's no separate "component reference" field type: hold an
`EntityRef` and call `ResolveEntityRef(ref).GetComponent<T>()` to reach a
specific component on whatever entity it points at. Edits made in Edit mode
are saved with the scene; edits made while Play is running affect the live
object only and are lost on Stop (matching Unity's own behavior). One
limitation: an `EntityRef` field does NOT survive a scene save/load round
trip -- it always reloads as unset, since a raw handle isn't stable across a
reload (see "Known limitations").

### Lifecycle

- `OnCreate()` -- once, when Play starts (Unity's `Awake`, runs even if the
  entity starts inactive).
- `OnEnable()` / `OnDisable()` -- fire whenever `Scene::IsEffectivelyActive`
  flips for this entity (its own `SetActive` call, or a parent's) -- can
  fire many times across one Play session. `OnUpdate` simply isn't called
  while inactive.
- `OnUpdate(float deltaTime)` -- every frame, while active.
- `OnCollisionEnter(Entity other)` / `OnCollisionExit(Entity other)` --
  fires for BOTH sides of a touching pair (Unity's own convention) when
  neither collider involved is a trigger. Works identically whether `other`
  is a 2D or 3D entity.
- `OnTriggerEnter(Entity other)` / `OnTriggerExit(Entity other)` -- same as
  above, but for a pair where at least one collider has `IsTrigger` set.
- `OnDestroy()` -- once, at Stop (a still-enabled instance gets one final
  `OnDisable()` immediately before this).

### Scripting API

`Behaviour` giữ lifecycle, `GetComponent`, `SetActive`, `ResolveEntityRef`, và
factory helpers (`GetRigidbody2D/3D`, `GetAudioSource`). Các API engine còn lại
là Unity-style static classes trong `DualityEngine/Scripting/` (Scene bind
`ScriptContext` trước mỗi callback):

| Unity | Duality |
|-------|---------|
| `Input` | `Duality::ScriptInput` |
| `Debug.Log` | `Duality::ScriptDebug` |
| `Physics2D` / `Physics` | `Duality::ScriptPhysics2D` / `ScriptPhysics3D` |
| `SceneManager.LoadScene` | `Duality::SceneManager::RequestLoadScene` |
| `Object.Instantiate` | `Duality::ScriptScene::Instantiate` |
| `Rigidbody2D` | `GetRigidbody2D()` → `Rigidbody2D` wrapper |
| `AudioSource` | `GetAudioSource()` hoặc `AudioSourceComponent` + `GetAudioSource().Play()` |

- **Active state** (trên `Behaviour`): `SetActive(bool)` / `IsActive()`.
- **Rigidbody**: `GetRigidbody2D().SetVelocity(v)` / `AddForce(f)` (và `GetRigidbody3D()`).
- **Raycast**: `ScriptPhysics3D::Raycast(origin, dir, maxDist)` và
  `ScriptPhysics3D::ScreenPointToRay(screen, point, outOrigin, outDir)` --
  chỉ hoạt động khi Play đang chạy.
- **Input**: `ScriptInput::GetKeyDown(KeyCode::Space)`, `GetAxis("Horizontal")`,
  `GetPointerDown()`, `GetPointerPosition()`, `GetPointerScreen()`.
- **Audio (one-shot)**: `ScriptAudio::PlaySound(assetGuid, loop)` /
  `StopAllSounds()`.
- **AudioSource (per-entity)**: thêm `Audio Source` component trong Editor
  (Clip/Loop/Volume/Play On Awake), rồi `GetAudioSource().Play()` /
  `Stop()` / `Pause()` / `SetVolume(v)` / `IsPlaying()`.
- **Logging**: `ScriptDebug::LogInfo` / `LogWarn` / `LogError`.
- **Cross-screen lookup**: `ScriptScene::FindEntityInTopScreen(name)` /
  `FindEntityInBottomScreen(name)` -- tìm trong subtree của entity gốc có
  `LayerComponent` TOP/BOTTOM.
- **Scene / Prefabs / Data**: `SceneManager::RequestLoadScene(path)`,
  `ScriptScene::Instantiate(guid)`, `ScriptScene::LoadScriptableObject<T>(guid)`.

**Layers** (thay `Screen Group` cũ): `LayerComponent` với `Default`, `TOP`,
`BOTTOM` -- TOP/BOTTOM map tới hai màn 3DS (citro2d `GFX_TOP`/`GFX_BOTTOM`).
Camera có thêm `Culling Mask` để lọc layer giống Unity.

Header-only, không cần `Behaviour`: `SaveSystem`, `DateTime`, `SceneManager`.

`GameScripts/Source/ApiShowcaseBehaviour.cpp` demo Input/AudioSource/Save/DateTime;
`RaycastDemoBehaviour.cpp` demo `ScriptPhysics3D`.
the sample scene -- move it with WASD/the Circle Pad, hold the pointer to
pull it toward the cursor/touch, press Space/A for a beep, and watch it
spin once a minute, driven by the real clock.
`GameScripts/Source/CollisionLogBehaviour.cpp` is the equivalent living
example for the collision/trigger lifecycle -- attach it to any entity with
a Rigidbody + collider and watch the Console panel as it touches things.
`GameScripts/Include/GameSettingsData.h` is the `ScriptableObject` demo
(`PlayerSpeed`/`ScorePerCoin`/`GameTitle`); `SampleProject/Assets/
GameSettings.asset` is a real instance of it, editable in Properties by
selecting it in the Content Browser.

## 3DS: build and run on-device

```
build-3ds.bat   REM clean configure+build -> build-3ds\DualityPlayer\DualityPlayer.3dsx / .cia
run-3ds.bat     REM launches the .3dsx in Citra
```

`build-3ds.bat` always starts from a clean `build-3ds\` directory (the
devkitPro CMake+Make combination has a reproducible bug where reconfiguring
in place after a `CMakeLists.txt` change corrupts a generated dependency
file -- a full rebuild avoids it and only takes well under a minute).

- `.3dsx` is for the fast iteration loop: Homebrew Launcher, or
  `3dslink -s build-3ds\DualityPlayer\DualityPlayer.3dsx` to push it to a
  real 3DS/Citra over network.
- `.cia` is for installing via FBI on real hardware/CFW. Building it needs
  `makerom.exe`/`bannertool.exe`, fetched once from their official GitHub
  releases into `Tools/` (see `Tools/` below) -- if they're missing, the
  build still succeeds and just skips `.cia`.
- The Editor's own "Build for 3DS" button (`BuildPipeline::CookAssets`)
  needs `tex3ds.exe` to convert project textures to `.t3x` -- located
  automatically (`FindDevkitProInstallDir`/`FindTex3dsExe` in
  `BuildPipeline.cpp`), not a hardcoded path: tries the `DEVKITPRO`
  environment variable first (only if it resolves to a real Windows path --
  devkitPro's own bundled MSYS2 exports it as a POSIX path like
  `/opt/devkitpro` on some installs, which a plain Windows process like the
  Editor can't use directly), then the Windows registry entry
  `devkitProUpdater` writes on install, then the documented default
  `C:\devkitPro` -- same 3-step order `devkitpro-path.bat` already used for
  locating the toolchain itself. If none of those pan out, textures are
  skipped (logged clearly) but everything else still cooks.

`DualityPlayer` loads whatever scene was last packaged into
`DualityPlayer/romfs/Scene.scene` (copied there automatically by the
Editor's "Build for 3DS" button, or by `build-3ds.bat` if you've placed one
there yourself). `Behaviour` scripts, 2D physics (Box2D), and 3D physics
(Bullet) all run for real on-device -- `GameScripts` links `STATIC` into
`DualityPlayer` (vs. the hot-reload `SHARED` DLL used for desktop
Play-in-Editor), the exact same script source files either way. There's no
Play/Stop on a real device: the simulation runs continuously from launch
until START is pressed. `Behaviour::LoadScene`-driven scene transitions
work on-device too, as long as the target scene file lives under the
project's `Assets/` folder (it gets cooked into romfs the same way any
other non-`.png` asset does -- see `BuildPipeline::CookAssets`).

Bullet Physics' real-world ARM11 performance/stability hasn't been
independently verified beyond this project's own testing -- devkitPro
ships `3ds-bulletphysics` as an official portlib, but with no worked
example and no known shipped 3DS game using it.

## Testing

```
run-tests.bat   REM runs Tests/DualityEngineTests.exe (build.bat builds it automatically)
```

`Tests/` is a small, permanent, desktop-only test suite for engine-level
logic -- ECS/Active-Disable lifecycle, 2D/3D collision and trigger events,
Bullet physics (gravity/landing/rotation), `SceneManager`'s request
mailbox, Prefab/ScriptableObject/Material save-load round-tripping, and
Texture/AudioImportSettings' `.meta` round-tripping -- linking directly
against `DualityEngine`'s real compiled code, no mocks. Uses a small
hand-rolled `TEST_CASE`/`CHECK` harness (`Tests/TestFramework.h`) instead
of vendoring Catch2/GoogleTest, matching this project's general preference
for small dependency-free infrastructure over a new library. `.github/
workflows/ci.yml` runs `build.bat` + `run-tests.bat` on every push/PR
(desktop only for now -- the 3DS cross-compile isn't wired into CI yet).

## Repository layout

```
DualityEngine/       Shared engine core (ECS, reflection, scene, renderer
                     interface + OpenGL/citro2d/citro3d backends, 2D physics
                     via Box2D, 3D physics via Bullet, scripting registry)
DualityEditor/       Desktop editor (Dear ImGui) -- Application owns a Window
                     (GLFW/GL/ImGui backend) and every Panel (Scene, Game,
                     Hierarchy, Properties, MenuBar, Content Browser,
                     Console, Build Settings, Project Settings, Preferences);
                     Main.cpp is just Application().Run()
DualityPlayerDesktop/ Standalone desktop player (real GLFW window, no Editor UI)
DualityPlayer/       3DS runtime executable + Packaging/ (icon/banner/RSF for .cia)
GameScripts/         Gameplay scripts, built as a hot-reload DLL (desktop) or
                     linked statically (3DS)
Tests/               Automated engine-logic test suite (see "Testing" above)
Vendor/              Third-party dependencies (vendored as source, see below)
Tools/               makerom.exe / bannertool.exe (fetched from their own
                     releases -- see "3DS: build and run on-device" above)
SampleProject/       The Editor's one (for now) project: Assets/, Scene.scene
References/          Prior-art repos consulted during development (not built)
.github/workflows/   CI (desktop build + automated tests on every push/PR)
```

Third-party dependencies in `Vendor/` (EnTT, GLM, nlohmann/json, ImGui,
stb_image, miniaudio, Box2D, Bullet Physics) are vendored as plain source
files rather than fetched via CMake `FetchContent`, for fast, offline,
reproducible builds. GLFW/GLEW instead come from the mingw-w64 package
repo (prebuilt binaries).

## Known limitations

- No Project Hub / recent-projects list -- New Project/Open Project are
  plain native file dialogs for now, no recent-project history.
- The Project Settings window (Edit menu) is mostly read-only for this first
  pass -- Name/Assets Directory/Scripts Directory are shown but not editable
  (only the Icon picker is). Preferences (Edit menu) covers only a single
  external-editor path so far.
- `Entity` handles held across a "Load Scene"/"Open Project"/"New Project"
  click (e.g. the
  current selection) can go stale, since either replaces the whole registry
  -- `SceneManager.LoadScene` handles this correctly for itself (resets
  `ctx.Selected`), but a script holding an `Entity` from before a scene
  swap does not get any such reset.
- No collision-event Stay variant (`OnCollisionEnter`/`Exit` and
  `OnTriggerEnter`/`Exit` exist; no per-frame-while-touching `OnCollisionStay`/
  `OnTriggerStay` yet), and no physics joints/constraints (hinge, spring,
  fixed) for either Box2D or Bullet.
- A Prefab instance has no link back to its source asset -- editing the
  original `.prefab` later does not update entities already
  instantiated from it (no "Apply"/"Revert" like Unity's prefab instances).
- An `EntityRef` script field (see "Inspector-editable fields") does not
  survive a scene save/load round trip -- it always reloads as unset, since
  the raw handle it stores isn't stable across a reload (entities get freshly
  assigned handles in creation order). It works correctly within one Editor/
  Play session.
- A `ScriptableObject`/Material asset has no "used by" tracking -- deleting
  or renaming one doesn't warn about (or fix up) scripts/other assets whose
  `AssetRef` still points at its old guid, same as every other asset type
  today. The Content Browser's "Create" menu covers Material and
  ScriptableObject only; there's no equivalent for Prefab (still created via
  "Create Prefab from Selection" in the Hierarchy, which needs a source
  entity to begin with) or Scene.
- Prefab/Scene assets show a read-only summary in Properties, not an editable
  one -- editing either in place (outside "Load Scene" for a Scene, or
  instantiating-then-re-saving for a Prefab) would need either a hidden Scene
  to stage the edit in, or generalizing the per-entity serialization to work
  off raw JSON without a live `Entity` at all.
- Texture import settings' Filter Mode/Wrap Mode only take effect after the
  texture is next loaded -- editing them in the Properties panel forces an
  immediate reload on desktop (`UnloadAllTextures()`), but the 3DS build only
  picks up a changed setting on the next "Build for 3DS" (which re-cooks
  every texture's `.t3x` from scratch anyway).
- `TransformComponent::Scale` isn't read by 2D sprite rendering (3D mesh
  rendering does use it as the mesh's own world-space size).
- The in-game UI system covers Panel/Image and Button widgets only -- no
  text/label widget yet (needs real font rendering, not built on desktop),
  no 9-slice/border scaling, and no visual drag-and-drop UI builder.
- No 3D lighting system yet (meshes are unlit; `Material` is currently just
  Color + Texture) and no skeletal/mesh animation for imported OBJ meshes.
  A mesh CAN carry multiple materials now (`MeshRendererComponent::Materials`,
  one real draw call per OBJ `usemtl` submesh group -- see `ROADMAP.md`), but
  each submesh is still unlit, same Color+Texture-only `Material`.

See `ROADMAP.md` at the repo root for the fuller list of planned/deferred
work (kept up to date independently of this section).

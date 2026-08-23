# DualityEngine

A from-scratch C++ game engine + Unity/Cocos-Creator-style drag-and-drop
editor targeting the Nintendo 3DS (devkitARM/devkitPro), developed on
Windows. The same `DualityEngine` core (ECS, reflection, scene
serialization) compiles for two targets from one source tree:

- **Desktop** -- `DualityEditor.exe`, the drag-and-drop editor (Dear ImGui).
- **3DS** -- `DualityPlayer`, the actual game runtime, built as `.3dsx`
  (Homebrew Launcher / `3dslink`) and `.cia` (installable via FBI on real
  hardware/CFW).

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
build.bat      REM configures + builds build-desktop\DualityEditor\DualityEditor.exe
run.bat        REM launches it (sets PATH so glfw3.dll/glew32.dll resolve)
```

`build.bat` re-configures automatically if the build cache looks stale.

### Using the Editor

On launch, the Editor opens (creating if needed) a fixed sample project at
`SampleProject/` next to the executable. There is no "New Project" dialog
yet, but an existing one can be opened via the File menu.

- **File menu** (top) -- **Open Project...** (browse for a `.dproj` file;
  swaps the active project/scene/Content Browser root, stopping Play first
  if it's running), **Save Scene** / **Load Scene** (JSON round-trip to
  `<project>/Assets/Scene.json`), and **Build for 3DS** (saves the scene,
  then runs `build-3ds.bat`, see below, so the on-device build always
  packages the latest scene).
- **Hierarchy** (left) -- lists entities in the scene; click one to select
  it. **Create Entity** adds a new empty entity and selects it.
- **Properties** (right) -- shows every component on the selected entity,
  fields fully generic (reflection-driven -- adding a new component/field
  never needs editor code changes). Each non-mandatory component has a
  "..." button to remove it (Transform/Name/Tag are mandatory, no remove
  option); **+ Add Component** at the bottom lists every component type not
  already on the entity.
- **Scene** (center, tabbed with Game) -- a free-roam editor camera over the
  whole scene, independent of any in-scene camera: middle-drag to pan, mouse
  wheel to zoom, left-click a sprite to select it. The viewport always
  exactly fills the panel (resizes with it, like a real editor viewport).
  Camera entities show as small color-coded markers (cyan = Top, orange =
  Bottom) since they have no sprite of their own. **Translate / Rotate /
  Scale** buttons at the top switch the selected entity's gizmo mode (red X
  / green Y arrows for Translate and Scale, a ring for Rotate) -- drag a
  handle to edit that entity's Transform live, visible immediately in both
  Scene and Game (and on a real 3DS build). `Transform.Rotation` is always
  in degrees. Colliders (`BoxCollider2DComponent`/`CircleCollider2DComponent`)
  draw as green wireframe outlines for every entity that has one, not just
  the selected one -- an editor-only visualization, like Unity's Gizmos,
  never actually rendered in the Game view or on-device.
- **Game** (center, tabbed with Scene) -- exactly what the real
  TopCamera/BottomCamera entities render, stacked to match the console's
  physical layout (Top 400x240 above, Bottom 320x240 below). This is the
  same render pass the 3DS build itself uses. **Play / Stop** runs
  `Behaviour` script lifecycle (`OnCreate`/`OnUpdate`/`OnDestroy`) against
  the live scene; **Reload Scripts** rebuilds `GameScripts` and hot-reloads
  it into the running Editor without restarting -- edit a script's `.cpp`,
  click this, see the change immediately.
- **Console** (bottom, tabbed with Content Browser) -- `Duality::Log`
  output, with per-level (Trace/Info/Warn/Error) filtering and a Clear
  button.
- **Content Browser** (bottom, tabbed with Console) -- browses
  `SampleProject/Assets/` as a grid of icons, with real thumbnails for image
  files. Every asset gets a companion `<name>.meta` file (a stable GUID,
  Unity/Unreal-style) generated the first time the Content Browser sees it;
  `.meta` files themselves are hidden from the grid. Drag an asset out onto
  a `SpriteRendererComponent`/`SpriteFlipbookComponent` texture field in
  Properties to assign it (referenced by its GUID, so renaming/moving the
  file later doesn't break it); drag a file in from Windows Explorer to
  import it into whatever folder is currently open.

### Writing gameplay scripts

Scripts live in `GameScripts/` -- built as a hot-reloadable DLL for desktop
Play-in-Editor, or linked statically straight into `DualityPlayer` for the
3DS build (same source files, no code changes either way). To add one:

1. Add a class deriving from `Duality::Behaviour` (see
   `GameScripts/Include/BounceBehaviour.h`/`Source/BounceBehaviour.cpp` for
   the pattern), overriding `OnCreate`/`OnUpdate`/`OnDestroy` as needed.
2. Register it with `REGISTER_BEHAVIOUR(YourClassName)` at the bottom of its
   `.cpp` file.
3. Add the new source file to `GameScripts/CMakeLists.txt`.
4. In the Editor, set an entity's `BehaviourComponent` "Class" field to the
   class name (a plain string, matched by name -- like picking a
   MonoBehaviour in Unity), then click **Reload Scripts**.

Inside `OnCreate`/`OnUpdate`, a script can read input Unity-old-Input-
Manager-style via methods inherited from `Behaviour`: `GetKey`/
`GetKeyDown`/`GetKeyUp(Duality::KeyCode::...)`, `GetAxis("Horizontal")`/
`GetAxis("Vertical")` (digital ±1 from WASD/arrows on desktop, real analog
values from the Circle Pad on 3DS -- same script code, platform-appropriate
values on each), and `GetPointerDown()`/`GetPointerPosition()` (mouse on
desktop, touchscreen on 3DS). `KeyCode` covers common desktop keys
(`W`/`A`/`S`/`D`, arrows, `Space`/`Enter`/`Escape`) and 3DS buttons
(`GamepadA`/`B`/`X`/`Y`/`L`/`R`, `GamepadStart`/`Select`, D-Pad) -- a
keycode meaningless on the current platform just always reads as not
pressed, rather than being unavailable. `PlaySound(assetGuid, loop=false)`/
`StopAllSounds()` play a WAV asset (referenced by its `.meta` GUID, same as
a `SpriteRendererComponent::Texture` field) through the same real audio
backend the engine itself uses (miniaudio on desktop, `ndsp` on 3DS -- only
16-bit PCM WAV is supported on-device, since there's no vendored decoder for
real hardware). Two more APIs don't need `Behaviour` at all, since they're
header-only with no engine state to synchronize across the DLL boundary:
`Duality::SaveSystem::SaveJson`/`LoadJson(path)` (plain JSON file save/load,
path resolved against the process's working directory on both platforms)
and `Duality::DateTime::Now()` (the real system clock, identical on both
platforms). `GameScripts/Source/ApiShowcaseBehaviour.cpp` exercises every
one of these APIs in one script, attached to the "ApiShowcase" entity in
the sample scene -- move it with WASD/the Circle Pad, hold the pointer to
pull it toward the cursor/touch, press Space/A for a beep, and watch it
spin once a minute, driven by the real clock.

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

`DualityPlayer` loads whatever scene was last packaged into
`DualityPlayer/romfs/Scene.json` (copied there automatically by the
Editor's "Build for 3DS" button, or by `build-3ds.bat` if you've placed one
there yourself). `Behaviour` scripts and Box2D physics both run for real
on-device -- `GameScripts` links `STATIC` into `DualityPlayer` (vs. the
hot-reload `SHARED` DLL used for desktop Play-in-Editor), the exact same
script source files either way. There's no Play/Stop on a real device: the
simulation runs continuously from launch until START is pressed.

## Repository layout

```
DualityEngine/     Shared engine core (ECS, reflection, scene, renderer
                    interface + OpenGL/citro2d backends, scripting registry)
DualityEditor/      Desktop editor (Dear ImGui) -- Application owns a Window
                    (GLFW/GL/ImGui backend) and every Panel (Scene, Game,
                    Hierarchy, Properties, MenuBar, Content Browser,
                    Console); Main.cpp is just Application().Run()
DualityPlayer/       3DS runtime executable + Packaging/ (icon/banner/RSF for .cia)
GameScripts/        Gameplay scripts, built as a hot-reload DLL
Vendor/             Third-party dependencies (vendored as source, see below)
Tools/              makerom.exe / bannertool.exe (fetched from their own
                    releases -- see "3DS: build and run on-device" above)
SampleProject/      The Editor's one (for now) project: Assets/, Scene.json
References/         Prior-art repos consulted during development (not built)
```

Third-party dependencies in `Vendor/` (EnTT, GLM, nlohmann/json, ImGui,
stb_image, miniaudio) are vendored as plain source files rather than
fetched via CMake `FetchContent`, for fast, offline, reproducible builds.
GLFW/GLEW instead come from the mingw-w64 package repo (prebuilt binaries).

## Known limitations

- No "New Project" flow yet, and no Project Hub / recent-projects list --
  Open Project is a plain file-browse dialog for now.
- `Entity` handles held across a "Load Scene"/"Open Project" click (e.g. the
  current selection) can go stale, since either replaces the whole registry.
- `GameScripts` can't call `Duality::Log` yet (the same DLL-boundary reason
  it can't link `DualityEngine`'s compiled lib directly -- see
  `Scripting/EngineServices.h`, the bridge `Behaviour::GetKey`/`GetAxis`/etc.
  use, which a future `LogInfo`/etc. entry could reuse).
- Textured sprites render for real on desktop, but not yet on the actual
  3DS build (citro2d needs pre-converted `.t3x` files via `tex3ds` -- a
  separate future asset-cooking pipeline); on-device they fall back to
  their flat `Color`.
- `TransformComponent::Scale` isn't read by anything yet (editable in
  Properties/the Scale gizmo, but inert -- no renderer or physics code
  applies it).
- The in-game UI system (declarative XML+CSS, à la Unity UI Toolkit) is
  planned but not started; only 2D rendering exists so far, no 3D
  mesh/material/camera pipeline.

See `ROADMAP.md` at the repo root for the fuller list of planned/deferred
work (kept up to date independently of this section).

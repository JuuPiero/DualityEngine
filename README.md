# DualityEngine

A from-scratch C++ game engine + Unity/Cocos-Creator-style drag-and-drop
editor targeting the Nintendo 3DS (devkitARM/devkitPro), developed on
Windows. The same `DualityEngine` core (ECS, reflection, scene
serialization) compiles for two targets from one source tree:

- **Desktop** -- `DualityEditor.exe`, the drag-and-drop editor (Dear ImGui).
- **3DS** -- `DualityPlayer`, the actual game runtime, built as `.3dsx`
  (Homebrew Launcher / `3dslink`) and `.cia` (installable via FBI on real
  hardware/CFW).

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
`SampleProject/` next to the executable -- there is no "New/Open Project"
dialog yet, so this is currently the one project the Editor always works
with.

- **Hierarchy** (left) -- lists entities in the scene; click one to select it.
- **Properties** (right) -- shows every component on the selected entity,
  fields fully generic (reflection-driven -- adding a new component/field
  never needs editor code changes).
- **Scene** (center, tabbed with Game) -- a free-roam editor camera over the
  whole scene, independent of any in-scene camera: right-drag to pan, mouse
  wheel to zoom, left-click a sprite to select it. Camera entities show as
  small color-coded markers (cyan = Top, orange = Bottom) since they have no
  sprite of their own. The selected entity gets a 2D translate gizmo (red X
  / green Y arrows + a yellow free-move square) -- drag a handle to move it.
- **Game** (center, tabbed with Scene) -- exactly what the real
  TopCamera/BottomCamera entities render, stacked to match the console's
  physical layout (Top 400x240 above, Bottom 320x240 below). This is the
  same render pass the 3DS build itself uses.
- **Console** (bottom, tabbed with Content Browser) -- `Duality::Log`
  output, with per-level (Trace/Info/Warn/Error) filtering and a Clear
  button.
- **Content Browser** (bottom, tabbed with Console) -- browses
  `SampleProject/Assets/` as a grid of icons, with real thumbnails for image
  files. Every asset gets a companion `<name>.meta` file (a stable GUID,
  Unity/Unreal-style) generated the first time the Content Browser sees it;
  `.meta` files themselves are hidden from the grid.

Toolbar buttons (top of the Game panel):
- **Play / Stop** -- runs `Behaviour` script lifecycle (`OnCreate`/
  `OnUpdate`/`OnDestroy`) against the live scene.
- **Reload Scripts** -- rebuilds `GameScripts` and hot-reloads it into the
  running Editor, without restarting -- edit a script's `.cpp`, click this,
  see the change immediately.
- **Save Scene / Load Scene** -- JSON round-trip to
  `SampleProject/Assets/Scene.json`.
- **Build for 3DS** -- saves the scene, then runs `build-3ds.bat` (see
  below) so the on-device build always packages the latest scene.

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
DualityEditor/      Desktop editor (Dear ImGui)
DualityPlayer/       3DS runtime executable + Packaging/ (icon/banner/RSF for .cia)
GameScripts/        Gameplay scripts, built as a hot-reload DLL
Vendor/             Third-party dependencies (vendored as source, see below)
Tools/              makerom.exe / bannertool.exe (fetched from their own
                    releases -- see "3DS: build and run on-device" above)
SampleProject/      The Editor's one (for now) project: Assets/, Scene.json
References/         Prior-art repos consulted during development (not built)
```

Third-party dependencies in `Vendor/` (EnTT, GLM, nlohmann/json, ImGui,
stb_image) are vendored as plain source files rather than fetched via CMake
`FetchContent`, for fast, offline, reproducible builds. GLFW/GLEW instead
come from the mingw-w64 package repo (prebuilt binaries).

## Known limitations

- No "New/Open Project" flow -- the Editor always uses `SampleProject/`.
- `Entity` handles held across a "Load Scene" click (e.g. the current
  selection) can go stale, since Load replaces the whole registry.
- `GameScripts` can't call `Duality::Log` yet (it deliberately doesn't link
  `DualityEngine`'s compiled lib -- see Vendor/architecture notes), so
  script code has no `Debug.Log`-equivalent into the Console panel yet.
- Asset `.meta` files carry a GUID but nothing reads it back yet -- no
  component has a real asset-reference field (texture, prefab, ...) to
  resolve by GUID. Also, `.meta` generation only happens for a folder once
  the Content Browser has actually been navigated into it (no upfront
  recursive scan), and folders don't get their own `.meta` yet.
- The in-game UI system (declarative XML+CSS, à la Unity UI Toolkit) is
  planned but not started.

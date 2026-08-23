# DualityEngine Roadmap

Planned/deferred work, tracked here so it survives outside any one chat session. Update
this file whenever something below actually gets built, or a new deferred item comes up
-- keep it in sync the same way `README.md`'s "Known limitations" section is.

## Rendering

- [ ] Spritesheet/atlas-based animation (UV sub-rects), as an alternative to the
      fixed-frame-slot flipbook (`SpriteFlipbookComponent`).
- [ ] 3D renderer. Explicitly deferred (see the DualityEngine project memory's
      Polyphase-Engine research notes) -- the project doesn't even have textured 2D
      sprites yet, so this is intentionally not next. If/when it starts: a two-tier
      full/lite material system (arbitrary shader graph on desktop, a simplified
      fixed-function/TEV-stage model for the 3DS's PICA200 GPU) is the one idea worth
      carrying over from that research, not the reference codebase itself.
- [ ] Texture rendering on the actual 3DS build. citro2d needs pre-converted `.t3x`
      files (via the `tex3ds` tool) -- there's no PNG-loading path on real hardware.
      Desktop Editor already supports real textures; on-device, textured sprites
      currently fall back to their flat `Color`.

## UI

- [ ] In-game UI system. Original plan called for a declarative XML+CSS-like system
      (Unity UI Toolkit-ish); a simpler Cocos-Creator-style Canvas/node hierarchy is an
      acceptable, likely-easier first cut if it gets there faster.
- [ ] Visual UI Builder (drag-and-drop tool for the above) -- later still, after
      whichever UI system above lands.

## Editor

- [ ] Project Hub / recent-projects list, as its own separate window (or its own
      subfolder in this repo) -- explicitly deferred until "everything else" is more
      finished. Right now "Open Project" is a plain file-browse dialog only.
- [ ] Inline thumbnail preview on Properties panel asset fields (texture/etc.) --
      currently just shows the resolved filename + a Clear button.
- [ ] Undo/redo.
- [ ] Multi-select (Hierarchy, Scene view).

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

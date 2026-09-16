# Duality vendor snapshot

- Upstream: https://github.com/assimp/assimp
- Pinned source commit: `747c1d6365ce871ce30897340d97e7cd9b47b9cc`
- Snapshot source date: 2026-09-15
- License: BSD-3-Clause; retain `LICENSE`, `CREDITS`, and upstream notices when distributing.

This is a normal vendored source snapshot, **not** a Git submodule.  It intentionally retains
only the shared core (`code/CApi`, `code/Common`, `code/Geometry`, `code/Material`,
`code/PostProcessing`), OBJ/FBX/glTF/glTF2 and their required shared parsers
(`glTFCommon`, `STEPParser`, `Step`), `include/`, `cmake-modules/`, and the exact third-party
dependencies selected by Assimp's target (`zlib`, `unzip`, `poly2tri`, `openddlparser`,
`Open3DGC`, `clipper`, `pugixml`, `rapidjson`, `stb`, `utf8cpp`, `earcut-hpp`). Upstream tests,
sample models, viewers, documentation, packaging, CI, ports, every other importer, and unused
third-party libraries (including Draco, tinyusdz, Meshlab and GoogleTest) were excluded.

## Duality integration policy

Assimp is an **Editor desktop / asset-cooking** dependency only.  Never add its target to a
Nintendo 3DS configuration or to the runtime Player.  The importer will normalize assets into
Duality's cooked `.dmesh` format; the 3DS player will load that compact format from romfs.

When the desktop importer target is wired, keep the parser surface bounded:

```cmake
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)  # legacy project assets
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE) # .gltf and .glb
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
```

Do not use Assimp's in-memory scene model as Duality's runtime mesh format.  Convert its mesh,
submesh, material-slot, normal, tangent, UV, color, and bounds data at import time instead.

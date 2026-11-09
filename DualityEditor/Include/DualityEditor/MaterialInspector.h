#pragma once

#include <string>

namespace Duality {

    struct EditorContext;

    // Unity-style Material inspector (Content Browser selection of a ".mat"): Shader dropdown,
    // Surface Options (surface type, cull, ZWrite, render queue), the selected shader's
    // properties (color pickers, texture slots with tiling/offset, sliders), a live lit-sphere
    // preview, and per-platform notes. Every edit is saved immediately through MaterialLoader,
    // whose cache refresh makes the Scene and Game views pick it up on the next frame.
    void DrawMaterialInspector(const std::string& path, EditorContext& ctx);

}

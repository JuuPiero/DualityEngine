#pragma once

#include <cstdint>

#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    // Unity-style render layers -- replaces the old ScreenGroupComponent tag. Default
    // layers TOP and BOTTOM map to the 3DS's two physical screens (citro2d GFX_TOP/
    // GFX_BOTTOM render targets); Default (0) means ungrouped and visible on whichever
    // screen's camera actually sees it by position, same as untagged content before.
    enum class Layer : int {
        Default = 0,
        TOP = 1,
        BOTTOM = 2,
    };

    constexpr int LayerCount = 32;
    constexpr uint32_t AllLayersMask = 0xFFFFFFFFu;

    inline uint32_t LayerBit(int layer) {
        return layer >= 0 && layer < LayerCount ? (1u << static_cast<uint32_t>(layer)) : 0u;
    }

    inline const char* LayerName(Layer layer) {
        switch (layer) {
            case Layer::TOP: return "TOP";
            case Layer::BOTTOM: return "BOTTOM";
            default: return "Default";
        }
    }

    inline bool LayerToScreen(Layer layer, Screen& outScreen) {
        if (layer == Layer::TOP) {
            outScreen = Screen::Top;
            return true;
        }
        if (layer == Layer::BOTTOM) {
            outScreen = Screen::Bottom;
            return true;
        }
        return false;
    }

    inline Layer ScreenToLayer(Screen screen) {
        return screen == Screen::Top ? Layer::TOP : Layer::BOTTOM;
    }

}

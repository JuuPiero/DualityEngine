#pragma once

#include <vector>

#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Scripting/ScriptableObject.h"

// A small shared config data asset (Unity ScriptableObject-style) -- a demo of the feature, but
// a real one: several scripts can all reference the same "GameSettings.asset" via an
// AssetRef guid instead of hardcoding these numbers per-entity, and tweaking one value in the
// Properties panel updates every reader at once (see GETTING_STARTED.md's ScriptableObject
// section).
class GameSettingsData : public Duality::ScriptableObject {
public:
    float PlayerSpeed = 5.0f;
    int ScorePerCoin = 10;
    std::string GameTitle = "My Game";

    static std::vector<Duality::FieldHandle> Fields() {
        return {
            Duality::MakeField("PlayerSpeed", &GameSettingsData::PlayerSpeed),
            Duality::MakeField("ScorePerCoin", &GameSettingsData::ScorePerCoin),
            Duality::MakeField("GameTitle", &GameSettingsData::GameTitle),
        };
    }
};

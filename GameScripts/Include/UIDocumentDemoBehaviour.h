#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Spawns a UIDocument (Unity UI Toolkit-style declarative UI, see
// DualityEngine/UI/UIDocument.h) onto the Bottom screen as soon as Play starts -- attach to any
// entity with no Transform/collider of its own needed, drag a ".uidoc" asset onto the
// MenuDocument field in Properties.
class UIDocumentDemoBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;

    DUALITY_PROPERTY() Duality::AssetRef MenuDocument;

    DUALITY_PROPERTIES_AUTO()
};

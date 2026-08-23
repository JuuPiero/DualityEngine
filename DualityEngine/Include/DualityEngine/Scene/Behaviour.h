#pragma once

#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Scene/Scene.h" // Entity's GetComponent<T>/etc. templates are *defined*
                                       // here (after Scene is complete), not in Entity.h -- see
                                       // the same note in Reflection/TypeRegistry.h.
#include "DualityEngine/Scripting/EngineServices.h"

namespace Duality {

    // The MonoBehaviour-equivalent base class for gameplay code, written as
    // plain C++ (no embedded scripting language). A later phase adds
    // reflection-driven public fields (Inspector-editable, like Unity's
    // [SerializeField]) and DLL hot-reload for fast desktop iteration --
    // this first version only wires up the lifecycle itself.
    class Behaviour {
    public:
        virtual ~Behaviour() = default;

        virtual void OnCreate() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnDestroy() {}

        template<typename T>
        T& GetComponent() { return m_Entity.GetComponent<T>(); }

        Entity GetEntity() const { return m_Entity; }

        // Input, Unity-Input-Manager-style -- called bare from inside a
        // script's own OnCreate/OnUpdate (via this inherited method, not a
        // global Input:: static). Forwards through m_Services rather than
        // calling Duality::Input directly: on desktop, this Behaviour may
        // live inside a separately-compiled GameScripts.dll that doesn't
        // link DualityEngine's compiled lib at all (the same reason a
        // Debug.Log-equivalent doesn't reach scripts either -- see
        // ROADMAP.md), so m_Services (wired by Scene right after creating
        // this instance, see EngineServices.h) is the only thing scripts
        // can safely call across that boundary. Returns false/0/{0,0} if
        // m_Services hasn't been set yet.
        bool GetKey(KeyCode key) const { return m_Services && m_Services->GetKey(static_cast<int>(key)); }
        bool GetKeyDown(KeyCode key) const { return m_Services && m_Services->GetKeyDown(static_cast<int>(key)); }
        bool GetKeyUp(KeyCode key) const { return m_Services && m_Services->GetKeyUp(static_cast<int>(key)); }
        float GetAxis(const std::string& axisName) const { return m_Services ? m_Services->GetAxis(axisName.c_str()) : 0.0f; }
        bool GetPointerDown() const { return m_Services && m_Services->GetPointerDown(); }
        glm::vec2 GetPointerPosition() const {
            if (!m_Services)
                return { 0.0f, 0.0f };
            float x = 0.0f, y = 0.0f;
            m_Services->GetPointerPosition(&x, &y);
            return { x, y };
        }

        // `assetGuid` is an AssetRef's Guid (e.g. an AssetRef field's
        // .Guid, or a value read from another component) -- resolved to a
        // real file path on the engine side of m_Services, same reasoning
        // as the Input methods above.
        void PlaySound(const std::string& assetGuid, bool loop = false) const {
            if (m_Services) m_Services->PlaySound(assetGuid.c_str(), loop);
        }
        void StopAllSounds() const { if (m_Services) m_Services->StopAllSounds(); }

        // Cross-screen entity lookup by name -- e.g. a script on a Top-screen entity
        // calling FindEntityInBottomScreen("Paddle") to reach an entity organized
        // under that screen's ScreenGroupComponent (see Components.h). Both entities
        // share the same Scene/Behaviour lifecycle either way -- "screen" here is
        // purely an organizational lookup filter, not a hard boundary. Returns an
        // empty Entity (falsy) if nothing matches or m_Services hasn't been set yet.
        Entity FindEntityInScreen(Screen screen, const std::string& name) const {
            if (!m_Services)
                return Entity{};
            unsigned int handle = 0;
            if (!m_Services->FindEntityInScreen(m_Entity.GetScene(), static_cast<int>(screen), name.c_str(), &handle))
                return Entity{};
            return Entity(static_cast<entt::entity>(handle), m_Entity.GetScene());
        }
        Entity FindEntityInTopScreen(const std::string& name) const { return FindEntityInScreen(Screen::Top, name); }
        Entity FindEntityInBottomScreen(const std::string& name) const { return FindEntityInScreen(Screen::Bottom, name); }

        // Called by Scene right after creating this instance -- not for
        // scripts to call themselves.
        void SetEngineServices(const EngineServices* services) { m_Services = services; }

    private:
        Entity m_Entity;
        const EngineServices* m_Services = nullptr;
        friend class Scene;
    };

}

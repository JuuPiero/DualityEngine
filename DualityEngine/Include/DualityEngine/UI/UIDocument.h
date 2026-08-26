#pragma once

#include <string>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/UI/UIMarkup.h"

namespace Duality {

    class Scene;

    // Unity UI Toolkit-inspired declarative UI -- a .uidoc markup file (HTML/XML-like tags +
    // an embedded/linked CSS-like stylesheet) compiles down into ordinary entities using this
    // engine's EXISTING UIRectComponent/UIImageComponent/UIButtonComponent/UITextComponent (see
    // Components.h) and UIRenderer.cpp's own draw/interaction loops -- there is no separate
    // "UIDocument renderer"; markup is purely an authoring convenience over the same runtime
    // primitives a hand-built UI (clicked together in the Properties panel) already uses.
    //
    // Element vocabulary: <Panel>/<Image> (UIImageComponent), <Button> (+UIButtonComponent),
    // <Text> (UITextComponent -- data only, see its own comment: no font rendering yet).
    // Attributes/style properties: id, class, style (inline CSS), screen ("Top"/"Bottom"),
    // anchor ("top-left".."bottom-right"), x/y (Offset), width/height (Size),
    // color/background-color, src/background-image (a path resolved relative to the .uidoc
    // file's own directory, NOT an asset guid -- authoring a raw guid by hand isn't practical),
    // normal-color/hover-color/pressed-color (Button only), text, behaviour (attaches a
    // BehaviourComponent by class name). UIDocument deliberately does NOT replicate
    // Polyphase-Engine's own `on-click="..."` markup-to-script binding (confirmed dead code
    // there -- recorded but never actually dispatched): a script attached via `behaviour="..."`
    // polls `GetComponent<UIButtonComponent>().WasClicked` itself, the exact same
    // already-working convention every hand-built button in this engine already uses.
    //
    // Nesting: a child element's UIRectComponent resolves relative to its PARENT's resolved
    // rect (see UIRenderer.h's ResolveUIRect), via ordinary HierarchyComponent parenting -- the
    // same scene-graph parent/child relationship regular entities already use, not a separate
    // UI-only hierarchy concept.
    class UIDocument {
    public:
        // Parses `path` (an absolute or working-directory-relative file path) and every
        // stylesheet it references via <style>/<link href="...">, then resolves the full CSS
        // cascade for every element up front -- everything Instantiate needs is ready by the
        // time this returns, no per-Instantiate-call re-parsing. On a parse failure, logs the
        // reason and returns a document with IsLoaded() == false; Instantiate on that is a
        // no-op returning an empty Entity. Prefer UIDocumentLoader::Load (cached by path) over
        // calling this directly, unless you specifically want an uncached reparse.
        static UIDocument Load(const std::string& path);

        bool IsLoaded() const { return m_Loaded; }

        // Read-only access to the parsed (and style-resolved) element tree -- the <ui> root's
        // own Children are the real document content. For introspection (the Editor's .uidoc
        // asset inspector reads this for its tree summary) -- Instantiate is still the only way
        // to turn this into real entities.
        const UIElementNode& GetRoot() const { return m_Root; }

        // Creates one root entity per call (never shared/cached -- same "spawn a fresh
        // independent copy" contract as PrefabSerializer::Instantiate) holding the whole parsed
        // element tree as its descendants, parented under `parent` (Entity{} = scene root).
        // Every element's UIRectComponent::Screen defaults to `screen` unless the markup's own
        // "screen" attribute/style overrides it for that specific element. Returns an empty
        // Entity if this document failed to load.
        Entity Instantiate(Scene& scene, Screen screen, Entity parent = {}) const;

    private:
        bool m_Loaded = false;
        std::string m_DocumentName; // file stem, e.g. "MainMenu" for "MainMenu.uidoc" -- names the root entity
        std::string m_BaseDirectory; // the .uidoc file's own containing folder -- resolves src="..." paths
        UIElementNode m_Root; // the <ui> root's own Children are the real document content, already style-resolved

        void InstantiateElement(Scene& scene, const UIElementNode& node, Screen screen, Entity parent) const;
    };

}

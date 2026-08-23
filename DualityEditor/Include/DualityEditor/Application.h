#pragma once

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "DualityEditor/Event.h"
#include "DualityEditor/Framebuffer.h"
#include "DualityEditor/Panels/ConsolePanel.h"
#include "DualityEditor/Panels/ContentBrowserPanel.h"
#include "DualityEditor/Panels/GamePanel.h"
#include "DualityEditor/Panels/HierarchyPanel.h"
#include "DualityEditor/Panels/MenuBarPanel.h"
#include "DualityEditor/Panels/PropertiesPanel.h"
#include "DualityEditor/Panels/ScenePanel.h"
#include "DualityEditor/SceneGizmo.h"
#include "DualityEditor/Window.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // The Editor's single concrete application. Owns the Window, the demo
    // Scene + its Top/Bottom/Scene framebuffers, and every panel; Run() is
    // the whole former Main.cpp per-frame loop. Deliberately NOT a virtual
    // base class like MyGameEngine's own Application -- there is only ever
    // one concrete app here (the Editor itself), so a subclassing point
    // would be indirection with no second caller. If a real second use
    // case ever needs it, that's the point to add it, not before.
    class Application {
    public:
        Application();

        void Run();

    private:
        void OnEvent(Event& e);
        void SetupDemoScene();
        void OpenProjectFromDialog();

        // Declaration order matters here: m_Window must exist before any
        // GL-dependent member (m_Renderer, the Framebuffers) is
        // constructed, and m_Project must exist before m_ContentBrowser
        // (which needs its assets directory at construction time).
        Window m_Window;
        std::shared_ptr<Project> m_Project;
        Scene m_Scene;
        OpenGLRenderer2D m_Renderer;

        Framebuffer m_TopFramebuffer;
        Framebuffer m_BottomFramebuffer;
        Framebuffer m_SceneFramebuffer;

        std::string m_BuildDirectory;
        std::string m_RepoRoot;
        std::string m_ScenePath;

        Entity m_Selected;
        bool m_IsPlaying = false;
        bool m_Running = true;
        bool m_DockLayoutInitialized = false;
        bool m_RequestOpenProject = false;

        glm::vec2 m_SceneCameraPos{ 0.0f, 0.0f };
        float m_SceneZoom = 1.0f;
        GizmoMode m_ActiveGizmoMode = GizmoMode::Translate;
        GizmoAxis m_DraggingGizmoAxis = GizmoAxis::None;

        MenuBarPanel m_MenuBarPanel;
        HierarchyPanel m_HierarchyPanel;
        PropertiesPanel m_PropertiesPanel;
        ScenePanel m_ScenePanel;
        GamePanel m_GamePanel;
        ContentBrowserPanel m_ContentBrowserPanel;
        ConsolePanel m_ConsolePanel;
    };

}

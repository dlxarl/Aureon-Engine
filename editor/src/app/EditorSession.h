#pragma once
#include "AssetThumbnails.h"
#include "AudioEditorPanel.h"
#include "ModelEditorPanel.h"
#include "TerrainEditorPanel.h"
#include "CodeEditorPanel.h"
#include "ConsolePanel.h"
#include "EditorContext.h"
#include "HierarchyPanel.h"
#include "InspectorPanel.h"
#include "MainBars.h"
#include "ProjectPanel.h"
#include "HeadTrackingPanel.h"
#include "RenderSettingsPanel.h"
#include "ViewportPanel.h"

class ImGuiLayer;
struct GLFWwindow;

// Everything that is on screen for one open project. It draws a frame when asked and knows
// nothing about the event loop, so that tests can run the real UI with scripted input.
class EditorSession {
public:
    // The window and imgui must outlive the session.
    EditorSession(Project& project, GLFWwindow* window, ImGuiLayer& imgui, bool useDefaultLayout);
    ~EditorSession();

    void frame();

    EditorContext& context() { return ctx; }

private:
    GLFWwindow* window;
    ImGuiLayer& imgui;

    EditorContext ctx;
    AssetThumbnails thumbnails;
    HierarchyPanel hierarchy;
    InspectorPanel inspector;
    ProjectPanel projectPanel;
    ConsolePanel consolePanel;
    RenderSettingsPanel renderSettings;
    HeadTrackingPanel headTrackingPanel;
    ViewportPanel viewport;
    CodeEditorPanel codeEditor;
    AudioEditorPanel audioEditor;
    TerrainEditorPanel terrainEditor;
    ModelEditorPanel modelEditor;
    MainBars bars;
    PanelVisibility panels;

    bool layoutPending;
    std::string appliedUiFont;

    bool mouseCaptured = false;
    bool windowWasFocused = false;
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;

    int windowedX = 0, windowedY = 0, windowedWidth = 1600, windowedHeight = 960;

    void applyFullscreen(bool wanted);
    void updateMouseCapture();
    void addMouseLook(GameInput& input);
};

#pragma once
#include "EditorContext.h"
#include "Platform.h"

#include <string>
#include <vector>

// Which panels are open, as toggled from the Window menu.
struct PanelVisibility {
    bool hierarchy = true;
    bool inspector = true;
    bool project = true;
    bool console = true;
    bool renderSettings = true;
    bool headTracking = false;
    bool codeEditor = false;
    bool audioEditor = false;
    bool terrainEditor = false;
    bool modelEditor = false;
};

// The application menu, the toolbar under it and the status bar along the bottom.
class MainBars {
public:
    void draw(EditorContext& ctx, PanelVisibility& panels);

    bool takeLayoutReset();

    // What is left while the game fills the window: the macOS menu bar and the shortcuts.
    void drawFullscreen(EditorContext& ctx, PanelVisibility& panels);

private:
    bool resetLayout = false;

    void drawMenuBar(EditorContext& ctx, PanelVisibility& panels);
    void drawToolbar(EditorContext& ctx);
    void drawStatusBar(EditorContext& ctx);
    void drawOpenSceneItems(EditorContext& ctx);
    void handleShortcuts(EditorContext& ctx);

    // The system menu bar repeats the one drawn at the top of the window.
    void updateNativeMenu(EditorContext& ctx, PanelVisibility& panels);
    void runCommand(int command, EditorContext& ctx, PanelVisibility& panels);
    void togglePlay(EditorContext& ctx, bool fullscreen);
    std::vector<std::string> sceneList(EditorContext& ctx);

    std::vector<std::string> cachedScenes;
    float sceneListAge = 1000.0f;
};

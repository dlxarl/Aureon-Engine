#include "MainBars.h"
#include "HierarchyPanel.h"
#include "ObjectFactory.h"
#include "Theme.h"
#include "Widgets.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {
    const float STATUS_HEIGHT = 24.0f;

    // Identifies a choice in the system menu bar.
    enum Command {
        NewScene = 1, Save, SaveAs, CloseProject, Quit,
        Undo, Redo, Rename, Duplicate, Delete, FocusSelection,
        PlayStop, Pause, PlayFullscreen,
        ShowCodeEditor, ShowAudioEditor, ShowTerrainEditor, ShowModelEditor,
        ShowHierarchy, ShowInspector, ShowProject, ShowConsole, ShowRenderSettings, ShowHeadTracking, ResetLayout,
        Shortcuts,
        OpenSceneBase = 1000,   // plus the index into the scene list
        CreateBase = 2000,      // plus the ObjectType
        CreateEmpty = 1999,
        CreateLightBase = 3100, // plus the LightType
        CreateUiBase = 3000,    // plus the index into UI_ITEMS
    };

    const char* const UI_ITEMS[] = {"Panel", "Text", "Image", "Bar", "Button", "HUD", "Menu"};

    Platform::MenuItem item(const char* title, int command, const char* key = "", bool enabled = true,
                            bool shift = false, bool checked = false) {
        Platform::MenuItem entry;
        entry.title = title;
        entry.command = command;
        entry.key = key;
        entry.enabled = enabled;
        entry.shift = shift;
        entry.checked = checked;
        return entry;
    }

    Platform::MenuItem separator() {
        Platform::MenuItem entry;
        entry.separator = true;
        return entry;
    }

    Platform::MenuItem submenu(const char* title, std::vector<Platform::MenuItem> children) {
        Platform::MenuItem entry;
        entry.title = title;
        entry.children = std::move(children);
        return entry;
    }
}

bool MainBars::takeLayoutReset() {
    const bool requested = resetLayout;
    resetLayout = false;
    return requested;
}

void MainBars::draw(EditorContext& ctx, PanelVisibility& panels) {
    drawMenuBar(ctx, panels);
    drawToolbar(ctx);
    drawStatusBar(ctx);
    handleShortcuts(ctx);
    updateNativeMenu(ctx, panels);
}

void MainBars::drawFullscreen(EditorContext& ctx, PanelVisibility& panels) {
    handleShortcuts(ctx);
    updateNativeMenu(ctx, panels);
}

void MainBars::togglePlay(EditorContext& ctx, bool fullscreen) {
    if (ctx.play.isStopped()) {
        ctx.startPlaying(fullscreen);
    } else if (fullscreen && !ctx.fullscreenPreview && ctx.viewMode == ViewMode::Game) {
        ctx.fullscreenPreview = true;   // already playing: just widen the view
    } else {
        ctx.stopPlaying();
    }
}

std::vector<std::string> MainBars::sceneList(EditorContext& ctx) {
    sceneListAge += ImGui::GetIO().DeltaTime;
    if (sceneListAge > 2.0f) {
        sceneListAge = 0.0f;
        cachedScenes.clear();
        std::error_code ec;
        for (fs::recursive_directory_iterator it(ctx.project.assetsDir(), ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() == ".scene") {
                const std::string assetPath = ctx.assets.toAssetPath(it->path());
                if (!assetPath.empty()) cachedScenes.push_back(assetPath);
            }
        }
        std::sort(cachedScenes.begin(), cachedScenes.end());
    }
    return cachedScenes;
}

void MainBars::updateNativeMenu(EditorContext& ctx, PanelVisibility& panels) {
    if (!Platform::hasNativeDialogs()) return;

    const bool editing = ctx.play.isStopped();
    const bool hasSelection = ctx.getSelected() != nullptr;

    std::vector<Platform::MenuItem> openScene;
    const std::vector<std::string> scenes = sceneList(ctx);
    if (scenes.empty()) openScene.push_back(item("No scenes in this project", 0));
    for (size_t i = 0; i < scenes.size(); i++) {
        openScene.push_back(item(scenes[i].c_str(), OpenSceneBase + (int)i, "", true, false,
                                 scenes[i] == ctx.document.getAssetPath()));
    }

    std::vector<Platform::MenuItem> create;
    create.push_back(item("Empty", CreateEmpty));
    create.push_back(separator());
    for (ObjectType type : {ObjectType::Cube, ObjectType::Sphere, ObjectType::Cylinder, ObjectType::Plane}) {
        create.push_back(item(ObjectFactory::typeName(type), CreateBase + (int)type));
    }
    create.push_back(separator());
    create.push_back(submenu("Light", {item("Point Light", CreateLightBase), item("Spot Light", CreateLightBase + 1), item("Directional Light", CreateLightBase + 2)}));
    create.push_back(item("Terrain", CreateBase + (int)ObjectType::Terrain));
    create.push_back(item("Camera", CreateBase + (int)ObjectType::Camera));
    create.push_back(item("Audio Source", CreateBase + (int)ObjectType::AudioSource));
    create.push_back(submenu("Effects", {item("Particles", CreateBase + (int)ObjectType::Particles),
                                         item("Fire", CreateBase + (int)ObjectType::Fire),
                                         item("Water", CreateBase + (int)ObjectType::Water)}));
    std::vector<Platform::MenuItem> twoD;
    for (int i = 0; i < 7; i++) {
        if (i == 5) twoD.push_back(separator());
        twoD.push_back(item(UI_ITEMS[i], CreateUiBase + i));
    }
    create.push_back(submenu("2D", twoD));
    create.push_back(item("Player", CreateBase + (int)ObjectType::Player, "", ctx.scene().getPlayer() == nullptr));

    std::vector<Platform::MenuItem> menus;
    menus.push_back(submenu("File", {
        item("New scene", NewScene, "n"),
        submenu("Open scene", openScene),
        separator(),
        item("Save", Save, "s", editing),
        item("Save as", SaveAs, "s", editing, true),
        separator(),
        item("Close project", CloseProject),
        item("Quit", Quit),
    }));
    menus.push_back(submenu("Edit", {
        item("Undo", Undo, "z", ctx.canUndo() && editing),
        item("Redo", Redo, "z", ctx.canRedo() && editing, true),
        separator(),
        item("Rename", Rename, "", hasSelection),
        item("Duplicate", Duplicate, "d", hasSelection),
        item("Delete", Delete, "", hasSelection),
        separator(),
        item("Focus selection", FocusSelection, "", hasSelection),
    }));
    menus.push_back(submenu("Create", create));
    menus.push_back(submenu("Play", {
        item(editing ? "Play" : "Stop", PlayStop, "p"),
        item("Pause", Pause, "p", !editing, true, ctx.play.isPaused()),
        item("Play fullscreen", PlayFullscreen, "f", true, true, ctx.fullscreenPreview),
    }));
    menus.push_back(submenu("Tools", {
        item("Code Editor", ShowCodeEditor, "", true, false, panels.codeEditor),
        item("Audio Editor", ShowAudioEditor, "", true, false, panels.audioEditor),
        item("Terrain Editor", ShowTerrainEditor, "", true, false, panels.terrainEditor),
        item("Model Editor", ShowModelEditor, "", true, false, panels.modelEditor),
    }));
    menus.push_back(submenu("Window", {
        item("Hierarchy", ShowHierarchy, "", true, false, panels.hierarchy),
        item("Inspector", ShowInspector, "", true, false, panels.inspector),
        item("Project", ShowProject, "", true, false, panels.project),
        item("Console", ShowConsole, "", true, false, panels.console),
        item("Render Settings", ShowRenderSettings, "", true, false, panels.renderSettings),
        item("Head Tracking", ShowHeadTracking, "", true, false, panels.headTracking),
        separator(),
        item("Reset layout", ResetLayout),
    }));
    menus.push_back(submenu("Help", {item("Keyboard and mouse", Shortcuts)}));

    Platform::setMenuBar("Aureon Editor", menus);

    for (int command : Platform::takeMenuCommands()) runCommand(command, ctx, panels);
}

void MainBars::runCommand(int command, EditorContext& ctx, PanelVisibility& panels) {
    if (command >= CreateLightBase && command < CreateLightBase + 3) {
        ctx.createLight(command - CreateLightBase);
        return;
    }
    if (command >= CreateUiBase && command < CreateUiBase + 100) {
        const int index = command - CreateUiBase;
        if (index >= 0 && index < 7) ctx.createUi(UI_ITEMS[index], InvalidObjectId);
        return;
    }
    if (command >= CreateBase) {
        ctx.createObject((ObjectType)(command - CreateBase), InvalidObjectId);
        return;
    }
    if (command == CreateEmpty) {
        ctx.createObject(ObjectType::Empty, InvalidObjectId);
        return;
    }
    if (command >= OpenSceneBase) {
        const size_t index = (size_t)(command - OpenSceneBase);
        if (index < cachedScenes.size()) ctx.openScene(cachedScenes[index]);
        return;
    }

    switch (command) {
        case NewScene: ctx.newScene(); break;
        case Save: if (ctx.play.isStopped()) ctx.saveScene(); break;
        case SaveAs: if (ctx.play.isStopped()) ctx.saveSceneAs(); break;
        case CloseProject: ctx.closeProject(); break;
        case Quit: ctx.quit(); break;
        case Undo: ctx.undo(); break;
        case Redo: ctx.redo(); break;
        case Rename: if (ctx.getSelected()) ctx.requestRename(ctx.selectedObject); break;
        case Duplicate: ctx.duplicateSelected(); break;
        case Delete: ctx.deleteSelected(); break;
        case FocusSelection: ctx.focusRequested = true; break;
        case PlayStop: togglePlay(ctx, false); break;
        case Pause: ctx.play.togglePause(); break;
        case PlayFullscreen: togglePlay(ctx, true); break;
        case ShowCodeEditor: panels.codeEditor = !panels.codeEditor; break;
        case ShowAudioEditor: panels.audioEditor = !panels.audioEditor; break;
        case ShowTerrainEditor: panels.terrainEditor = !panels.terrainEditor; break;
        case ShowModelEditor: panels.modelEditor = !panels.modelEditor; break;
        case ShowHierarchy: panels.hierarchy = !panels.hierarchy; break;
        case ShowInspector: panels.inspector = !panels.inspector; break;
        case ShowProject: panels.project = !panels.project; break;
        case ShowConsole: panels.console = !panels.console; break;
        case ShowRenderSettings: panels.renderSettings = !panels.renderSettings; break;
        case ShowHeadTracking: panels.headTracking = !panels.headTracking; break;
        case ResetLayout: resetLayout = true; break;
        case Shortcuts: ctx.dialogs.shortcuts(); break;
        default: break;
    }
}

void MainBars::handleShortcuts(EditorContext& ctx) {
    const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;

    // ImGuiMod_Ctrl is the Command key on macOS.
    if (!ctx.textEditorFocused && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, global)) ctx.saveScene();
    if (!ctx.textEditorFocused && !ImGui::GetIO().WantTextInput) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, global) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, global)) {
            ctx.redo();
        } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, global)) {
            ctx.undo();
        }
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, global)) ctx.saveSceneAs();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, global)) ctx.newScene();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, global)) ctx.duplicateSelected();

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, global)) {
        togglePlay(ctx, false);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, global)) ctx.play.togglePause();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F, global)) togglePlay(ctx, true);

    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            ctx.deleteSelected();
        }
    }
}

void MainBars::drawOpenSceneItems(EditorContext& ctx) {
    // Listed when the menu opens, which is rare enough to read the disk for.
    std::vector<std::string> scenes;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(ctx.project.assetsDir(), ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".scene") {
            const std::string assetPath = ctx.assets.toAssetPath(it->path());
            if (!assetPath.empty()) scenes.push_back(assetPath);
        }
    }
    std::sort(scenes.begin(), scenes.end());

    if (scenes.empty()) {
        ImGui::MenuItem("No scenes in this project", nullptr, false, false);
    }
    for (const std::string& scene : scenes) {
        if (ImGui::MenuItem(scene.c_str(), nullptr, scene == ctx.document.getAssetPath())) {
            ctx.openScene(scene);
        }
    }
}

void MainBars::drawMenuBar(EditorContext& ctx, PanelVisibility& panels) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    if (!ImGui::BeginMainMenuBar()) {
        ImGui::PopStyleVar();
        return;
    }

    const bool editing = ctx.play.isStopped();

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New scene", "Cmd+N")) ctx.newScene();
        if (ImGui::BeginMenu("Open scene")) {
            drawOpenSceneItems(ctx);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Cmd+S", false, editing)) ctx.saveScene();
        if (ImGui::MenuItem("Save as", "Shift+Cmd+S", false, editing)) ctx.saveSceneAs();
        ImGui::Separator();
        if (ImGui::MenuItem("Close project")) ctx.closeProject();
        if (ImGui::MenuItem("Quit", "Cmd+Q")) ctx.quit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        const bool hasSelection = ctx.getSelected() != nullptr;
        if (ImGui::MenuItem("Undo", "Cmd+Z", false, ctx.canUndo() && editing)) ctx.undo();
        if (ImGui::MenuItem("Redo", "Shift+Cmd+Z", false, ctx.canRedo() && editing)) ctx.redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2", false, hasSelection)) ctx.requestRename(ctx.selectedObject);
        if (ImGui::MenuItem("Duplicate", "Cmd+D", false, hasSelection)) ctx.duplicateSelected();
        if (ImGui::MenuItem("Delete", "Delete", false, hasSelection)) ctx.deleteSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Focus selection", "F", false, hasSelection)) ctx.focusRequested = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Create")) {
        drawCreateObjectItems(ctx, InvalidObjectId);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Play")) {
        if (ImGui::MenuItem(editing ? "Play" : "Stop", "Cmd+P")) togglePlay(ctx, false);
        if (ImGui::MenuItem("Pause", "Shift+Cmd+P", ctx.play.isPaused(), !editing)) ctx.play.togglePause();
        if (ImGui::MenuItem("Play fullscreen", "Shift+Cmd+F", ctx.fullscreenPreview)) togglePlay(ctx, true);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        ImGui::MenuItem("Code Editor", nullptr, &panels.codeEditor);
        ImGui::MenuItem("Audio Editor", nullptr, &panels.audioEditor);
        ImGui::MenuItem("Terrain Editor", nullptr, &panels.terrainEditor);
        ImGui::MenuItem("Model Editor", nullptr, &panels.modelEditor);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Hierarchy", nullptr, &panels.hierarchy);
        ImGui::MenuItem("Inspector", nullptr, &panels.inspector);
        ImGui::MenuItem("Project", nullptr, &panels.project);
        ImGui::MenuItem("Console", nullptr, &panels.console);
        ImGui::MenuItem("Render Settings", nullptr, &panels.renderSettings);
        ImGui::MenuItem("Head Tracking", nullptr, &panels.headTracking);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) resetLayout = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard and mouse")) ctx.dialogs.shortcuts();
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
    ImGui::PopStyleVar();
}

void MainBars::drawToolbar(EditorContext& ctx) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Panel);
    const bool open = ImGui::BeginViewportSideBar("##toolbar", viewport, ImGuiDir_Up, Theme::ToolbarHeight, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }

    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetContentRegionAvail().y;
    const ImVec2 buttonSize(height, height);
    const float textY = ImGui::GetCursorPosY() + (height - ImGui::GetTextLineHeight()) * 0.5f;
    const float startY = ImGui::GetCursorPosY();

    // Left: where you are, and whether it is saved.
    ImGui::SetCursorPosY(textY);
    ImGui::TextUnformatted(ctx.project.getInfo().name.c_str());
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetCursorPosY(textY);
    Widgets::dimText("/");
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetCursorPosY(textY);
    const std::string scene = ctx.document.getDisplayName() + (ctx.document.isDirty() ? " *" : "");
    Widgets::dimText(scene.c_str());

    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SetCursorPosY(startY);
    ImGui::BeginDisabled(!ctx.play.isStopped());
    if (Widgets::iconButton("##save", Icons::save, false, "Save scene (Cmd+S)", buttonSize)) {
        ctx.saveScene();
    }
    ImGui::EndDisabled();

    // Center: the transport.
    const float transportWidth = buttonSize.x * 3.0f + 4.0f;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x + (width - transportWidth) * 0.5f, startY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));

    if (Widgets::iconButton("##play", Icons::play, !ctx.play.isStopped(), "Play (Cmd+P)", buttonSize)) {
        if (ctx.play.isStopped()) {
            ctx.startPlaying();
        } else if (ctx.play.isPaused()) {
            ctx.play.togglePause();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(ctx.play.isStopped());
    if (Widgets::iconButton("##pause", Icons::pause, ctx.play.isPaused(), "Pause (Shift+Cmd+P)", buttonSize)) {
        ctx.play.togglePause();
    }
    ImGui::SameLine();
    if (Widgets::iconButton("##stop", Icons::stop, false, "Stop", buttonSize)) {
        ctx.stopPlaying();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();

    // Right: which view the main area shows, and the game across the whole screen.
    const float segmentWidth = 58.0f;
    const float fullWidth = 96.0f;
    const float rightY = startY + (height - ImGui::GetFrameHeight()) * 0.5f;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x + width - segmentWidth * 2.0f - fullWidth - 11.0f, rightY));
    if (Widgets::segment("Fullscreen", false, fullWidth, true, true)) togglePlay(ctx, true);
    if (ImGui::IsItemHovered()) Widgets::tooltip("Play with the game filling the screen (Shift+Cmd+F). Esc leaves it.");
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x + width - segmentWidth * 2.0f - 1.0f, rightY));
    if (Widgets::segment("Scene", ctx.viewMode == ViewMode::Scene, segmentWidth, true, false)) {
        ctx.viewMode = ViewMode::Scene;
    }
    ImGui::SameLine(0.0f, 1.0f);
    if (Widgets::segment("Game", ctx.viewMode == ViewMode::Game, segmentWidth, false, true)) {
        ctx.viewMode = ViewMode::Game;
    }

    ImGui::End();
}

void MainBars::drawStatusBar(EditorContext& ctx) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Sunken);
    const bool open = ImGui::BeginViewportSideBar("##status", viewport, ImGuiDir_Down, STATUS_HEIGHT, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }

    if (!ctx.play.isStopped()) {
        ImGui::TextColored(Theme::color(Theme::Accent), ctx.play.isPaused() ? "Paused" : "Playing");
        ImGui::SameLine();
        Widgets::dimText("Changes are undone when you stop.");
    } else if (!ctx.getStatus().empty()) {
        Widgets::dimText(ctx.getStatus().c_str());
    } else {
        ConsoleEntry last;
        if (ctx.console.getLast(last)) Widgets::dimText(last.message.c_str());
    }

    char fps[32];
    std::snprintf(fps, sizeof(fps), "%.0f FPS", ImGui::GetIO().Framerate);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(fps).x);
    Widgets::dimText(fps);

    ImGui::End();
}

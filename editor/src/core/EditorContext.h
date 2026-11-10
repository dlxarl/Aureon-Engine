#pragma once
#include "AssetManager.h"
#include "ConsoleLog.h"
#include "Dialogs.h"
#include "HeadTracking.h"
#include "PlayController.h"
#include "Project.h"
#include "SceneDocument.h"
#include "ScriptBuilder.h"
#include "Terrain.h"
#include "UserSettings.h"

#include <chrono>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct ImFont;

enum class ViewMode { Scene, Game };

// What the user asked to happen to the Editor as a whole.
enum class EditorExit { None, CloseProject, Quit };

// State shared by the panels, and the operations they have in common. A panel changes the
// scene through here so that a menu item and a shortcut do exactly the same thing.
class EditorContext {
public:
    explicit EditorContext(Project& project);

    Project& project;
    AssetManager assets;
    SceneDocument document;
    PlayController play;
    HeadTrackingController headTracking;
    ConsoleLog console;
    Dialogs dialogs;

    ViewMode viewMode = ViewMode::Scene;

    UserSettings settings = UserSettings::load();

    // The Terrain Editor's brush. While enabled, the left mouse button in the Scene view shapes
    // the selected terrain instead of selecting.
    struct TerrainBrushState {
        bool enabled = false;
        TerrainObject::Brush mode = TerrainObject::Brush::Raise;
        float radius = 6.0f;
        float strength = 4.0f;
        float falloff = 0.6f;
        int tile = 0;
        float flattenHeight = 0.0f;   // taken from the ground where the stroke starts
    } terrainBrush;

    // True while a text editing tool has the keyboard, so that Cmd+S saves its file.
    bool textEditorFocused = false;

    // Files that other panels want the tools to open; the tools take them on their next frame.
    struct ToolRequests {
        std::filesystem::path code;
        int codeLine = 1;
        std::filesystem::path audio;
        std::filesystem::path model;
    } toolRequests;

    // Play mode shown across the whole window, without the editor around it.
    bool fullscreenPreview = false;

    // First-person mouse look. While true, the game owns the mouse and the UI ignores it.
    bool wantsMouseCapture = false;
    ImFont* monoFont = nullptr;
    ImFont* gameFont = nullptr;

    // The font for a path under Assets; Inter for none.
    std::function<ImFont*(const std::string&)> fontFor;

    SceneGraph& scene() { return document.getScene(); }

    // Selection. An object and an asset are mutually exclusive; the Inspector shows either.
    ObjectId selectedObject = InvalidObjectId;
    std::string selectedAsset;

    SceneObject* getSelected();
    void select(ObjectId id);
    void selectAsset(const std::string& assetPath);

    // Set by the UI; the Scene view frames the selection on its next frame.
    bool focusRequested = false;
    void requestRename(ObjectId id) { renameRequest = id; }
    ObjectId takeRenameRequest();

    // Objects
    SceneObject* createObject(ObjectType type, ObjectId parent = InvalidObjectId);
    // A 2D element by name: Panel, Text, Image, Bar, Button, or the ready-made HUD and Menu.
    // A light of a type, aimed sensibly: 0 point, 1 spot, 2 directional.
    SceneObject* createLight(int type, ObjectId parent = InvalidObjectId);
    SceneObject* createUi(const std::string& what, ObjectId parent = InvalidObjectId);
    SceneObject* addModel(const std::string& assetPath, glm::vec3 position);
    void deleteSelected();
    void duplicateSelected();
    void setObjectTexture(ObjectId id, const std::string& assetPath);

    // Scene files. Each of these asks about unsaved changes first where that applies.
    void saveScene();
    void saveSceneAs();
    void openScene(const std::string& assetPath);
    void newScene();

    // The scripts the project declares, read from its source files a moment ago.
    const std::vector<ScriptInfo>& availableScripts();

    // Undo for the scene: every settled change, such as a finished drag or an edited value,
    // is a step. Called once a frame.
    void updateUndo();
    void undo();
    void redo();
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    // Play mode. The main view follows it: Game while playing, Scene again after.
    // fullscreen hides every panel and shows only the game, across the whole screen.
    void startPlaying(bool fullscreen = false);
    void stopPlaying();

    // Project
    void closeProject();
    void quit();
    EditorExit getExitRequest() const { return exitRequest; }

    // Graphics settings are written shortly after the last change.
    void graphicsChanged();
    void updateAutosave(float deltaTime);

    // Files dropped on the window, from the operating system.
    std::vector<std::filesystem::path> droppedFiles;

    void setStatus(const std::string& text);
    const std::string& getStatus() const { return status; }

private:
    ObjectId renameRequest = InvalidObjectId;
    EditorExit exitRequest = EditorExit::None;
    std::string status;
    float graphicsSaveDelay = -1.0f;
    std::vector<std::string> undoStack;
    std::vector<std::string> redoStack;
    std::string settledSnapshot;
    unsigned settledRevision = 0;
    unsigned settledEpoch = ~0u;
    std::vector<ScriptInfo> scriptList;
    std::chrono::steady_clock::time_point scriptsScannedAt;

    // Runs action, first offering to save the scene when it has unsaved changes.
    void withSavedScene(std::function<void()> action);
    bool saveSceneNow();
    std::string saveSceneTo(const std::string& text, std::function<void()> afterSave);
    void promptSaveAs(std::function<void()> afterSave);
};

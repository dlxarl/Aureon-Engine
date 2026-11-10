// Runs the real Editor UI in a hidden window, with mouse and keyboard scripted through Dear
// ImGui's input queue. These cover what unit tests cannot: that a click on a row selects it,
// that a drag ends up as a reparent, that the gizmo moves the object.
#include "AssetImporter.h"
#include "EditorSession.h"
#include "GlWindow.h"
#include "ImGuiLayer.h"
#include "MeshShape.h"
#include "ObjectFactory.h"
#include "EditMesh.h"
#include "Player.h"
#include "Terrain.h"
#include "SceneSerializer.h"
#include "TestSupport.h"

#include "imgui_internal.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {
    // Hierarchy rows, from HierarchyPanel: text line + FramePadding.y * 2.
    const float ROW_HEIGHT = 20.0f;
    const float TILE_WIDTH = 92.0f;
    const float TILE_HEIGHT = 96.0f;

    // ImGui reads Command as its logical Ctrl on macOS, so scripted input presses the physical key.
#if defined(__APPLE__)
    const ImGuiKey COMMAND = ImGuiMod_Super;
#else
    const ImGuiKey COMMAND = ImGuiMod_Ctrl;
#endif

    class Driver {
    public:
        explicit Driver(EditorSession& editorSession) : session(editorSession) {}

        void frames(int count = 1) {
            for (int i = 0; i < count; i++) {
                glfwPollEvents();
                session.frame();
            }
        }

        void moveTo(ImVec2 position) {
            ImGui::GetIO().AddMousePosEvent(position.x, position.y);
            frames(2);
        }

        void press(int button = 0) {
            ImGui::GetIO().AddMouseButtonEvent(button, true);
            frames(2);
        }

        void release(int button = 0) {
            ImGui::GetIO().AddMouseButtonEvent(button, false);
            frames(2);
        }

        void click(ImVec2 position, int button = 0) {
            moveTo(position);
            press(button);
            release(button);
        }

        void doubleClick(ImVec2 position) {
            moveTo(position);
            for (int i = 0; i < 2; i++) {
                ImGui::GetIO().AddMouseButtonEvent(0, true);
                session.frame();
                ImGui::GetIO().AddMouseButtonEvent(0, false);
                session.frame();
            }
            frames(2);
        }

        // A hidden window runs far faster than a display refreshes, so anything that
        // depends on game time has to wait for it rather than count frames.
        void runFor(double seconds) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
            while (std::chrono::steady_clock::now() < end) {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                frames(1);
            }
        }

        // Lets ImGui's double-click window pass, so that the next press is a press of its own.
        void pause() {
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
            frames(2);
        }

        void drag(ImVec2 from, ImVec2 to) {
            pause();
            moveTo(from);
            press();
            const int steps = 8;
            for (int i = 1; i <= steps; i++) {
                const float t = (float)i / steps;
                moveTo(ImVec2(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t));
            }
            frames(2);
            release();
        }

        void key(ImGuiKey pressed, ImGuiKey modifier = ImGuiKey_None) {
            ImGuiIO& io = ImGui::GetIO();
            if (modifier != ImGuiKey_None) {
                io.AddKeyEvent(modifier, true);
                frames(1);
            }
            io.AddKeyEvent(pressed, true);
            frames(2);
            io.AddKeyEvent(pressed, false);
            frames(1);
            if (modifier != ImGuiKey_None) {
                io.AddKeyEvent(modifier, false);
                frames(1);
            }
        }

        void type(const char* text) {
            ImGui::GetIO().AddInputCharactersUTF8(text);
            frames(2);
        }

        // The child region a panel draws its contents in, found by name.
        ImGuiWindow* window(const char* nameFragment) {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            for (ImGuiWindow* candidate : g.Windows) {
                if (std::strstr(candidate->Name, nameFragment) && candidate->WasActive) return candidate;
            }
            return nullptr;
        }

        ImVec2 hierarchyRow(int row) {
            ImGuiWindow* tree = window("##tree");
            return ImVec2(tree->Pos.x + 70.0f, tree->Pos.y + 2.0f + ROW_HEIGHT * row + ROW_HEIGHT * 0.5f);
        }

        ImVec2 tile(int index) {
            ImGuiWindow* grid = window("##grid");
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            return ImVec2(grid->Pos.x + 8.0f + (TILE_WIDTH + spacing) * index + TILE_WIDTH * 0.5f,
                          grid->Pos.y + 8.0f + TILE_HEIGHT * 0.5f);
        }

        // Assets is row 0, and its folders follow by name: Audio, Materials, Models...
        ImVec2 folderRow(int row) {
            ImGuiWindow* folders = window("##folders");
            return ImVec2(folders->Pos.x + 60.0f, folders->Pos.y + 4.0f + 26.0f * row + 10.0f);
        }

        ImVec2 viewportCenter() {
            ImGuiWindow* viewport = window("###Viewport");
            return ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                          viewport->Pos.y + (viewport->Size.y + 34.0f + 20.0f) * 0.5f);
        }

    private:
        EditorSession& session;
    };

    ObjectId createAt(EditorContext& ctx, ObjectType type, glm::vec3 position) {
        SceneObject* object = ctx.createObject(type);
        object->setPosition(position);
        return object->id;
    }

    void testSelectionAndHierarchy(Driver& ui, EditorContext& ctx) {
        // The template scene: camera and light are rows 0 and 1.
        const ObjectId parent = createAt(ctx, ObjectType::Cube, glm::vec3(0.0f, 0.5f, 0.0f));
        const ObjectId child = createAt(ctx, ObjectType::Sphere, glm::vec3(5.0f, 0.5f, 0.0f));
        ctx.select(InvalidObjectId);
        ui.frames(2);

        ui.click(ui.hierarchyRow(2));
        CHECK(ctx.selectedObject == parent);

        ui.click(ui.hierarchyRow(3));
        CHECK(ctx.selectedObject == child);

        // Dropping a row on another nests it, keeping the object in place.
        ui.drag(ui.hierarchyRow(3), ui.hierarchyRow(2));
        SceneObject* childObject = ctx.scene().find(child);
        CHECK(childObject && childObject->getParent() && childObject->getParent()->id == parent);
        CHECK(childObject && glm::length(childObject->getWorldPosition() - glm::vec3(5.0f, 0.5f, 0.0f)) < 0.001f);

        // Dropping below the last row moves it back to the top level.
        ImVec2 belowRows = ui.hierarchyRow(8);
        ui.drag(ui.hierarchyRow(3), belowRows);
        CHECK(childObject && childObject->getParent() == nullptr);

        // Dropping on the upper edge of a row puts it before that row.
        ui.drag(ui.hierarchyRow(3), ImVec2(ui.hierarchyRow(2).x, ui.hierarchyRow(2).y - ROW_HEIGHT * 0.4f));
        CHECK(ctx.scene().getRoots()[2]->id == child);
        ctx.scene().reparent(child, InvalidObjectId, 3);

        // Clicking empty space clears the selection.
        ui.click(belowRows);
        CHECK(ctx.selectedObject == InvalidObjectId);
    }

    // The Inspector draws every kind of effect, and what is made there is part of the scene.
    void testEffectObjects(Driver& ui, EditorContext& ctx) {
        for (ObjectType type : {ObjectType::Particles, ObjectType::Fire, ObjectType::Water, ObjectType::AudioSource}) {
            SceneObject* object = ctx.createObject(type);
            CHECK(object && object->getType() == type);
            ctx.select(object->id);
            ui.frames(4);
            CHECK(ctx.selectedObject == object->id);
        }
        CHECK(ctx.scene().getEmitters().size() == 1 && ctx.scene().getFires().size() == 1 && ctx.scene().getWaters().size() == 1 && ctx.scene().getAudioSources().size() == 1);
        for (const SceneObject* object : {(const SceneObject*)ctx.scene().getEmitters()[0].get(), (const SceneObject*)ctx.scene().getFires()[0].get(),
                                          (const SceneObject*)ctx.scene().getWaters()[0].get(),
                                          (const SceneObject*)ctx.scene().getAudioSources()[0].get()}) {
            ctx.scene().remove(object->id);
        }
        ctx.select(InvalidObjectId);
        ui.frames(2);
    }

    void testRename(Driver& ui, EditorContext& ctx) {
        ui.doubleClick(ui.hierarchyRow(2));
        ui.type("Renamed");
        ui.key(ImGuiKey_Enter);
        ui.frames(2);
        CHECK(ctx.scene().getRoots()[2]->name.find("Renamed") != std::string::npos);
    }

    void testShortcuts(Driver& ui, EditorContext& ctx) {
        ctx.select(ctx.scene().getRoots()[2]->id);
        const size_t before = ctx.scene().getObjects().size();

        ui.key(ImGuiKey_D, COMMAND);
        CHECK(ctx.scene().getObjects().size() == before + 1);
        CHECK(ctx.selectedObject != InvalidObjectId);

        ui.moveTo(ui.viewportCenter());
        ui.key(ImGuiKey_Delete);
        CHECK(ctx.scene().getObjects().size() == before);
        CHECK(ctx.selectedObject == InvalidObjectId);

        ui.key(ImGuiKey_S, COMMAND);
        ui.frames(2);
        CHECK(!ctx.document.isDirty());
    }

    void testViewportPickingAndGizmo(Driver& ui, EditorContext& ctx) {
        const ObjectId cube = ctx.scene().getRoots()[2]->id;
        ctx.select(InvalidObjectId);
        ctx.scene().find(cube)->setPosition(glm::vec3(0.0f, 0.5f, 0.0f));
        ui.frames(3);

        // The Scene view camera looks at the origin, so the cube sits under the image center.
        ui.click(ui.viewportCenter());
        ui.frames(2);
        CHECK(ctx.selectedObject == cube);

        // The move gizmo's Y axis points straight up from the object.
        const glm::vec3 start = ctx.scene().find(cube)->position;
        ImVec2 center = ui.viewportCenter();
        ui.drag(ImVec2(center.x, center.y - 25.0f), ImVec2(center.x, center.y - 105.0f));
        const glm::vec3 moved = ctx.scene().find(cube)->position;
        CHECK(moved.y > start.y + 0.1f);
        CHECK(std::abs(moved.x - start.x) < 0.001f);
        CHECK(std::abs(moved.z - start.z) < 0.001f);
        CHECK(ctx.document.isDirty());

        ctx.scene().find(cube)->setPosition(start);
        ui.frames(2);

        // Clicking the sky deselects.
        ImGuiWindow* viewport = ui.window("###Viewport");
        ui.click(ImVec2(viewport->Pos.x + 20.0f, viewport->Pos.y + 70.0f));
        ui.frames(2);
        CHECK(ctx.selectedObject == InvalidObjectId);
    }

    void testAssetBrowser(Driver& ui, EditorContext& ctx, const fs::path& outside) {
        // A file dropped on the window from the operating system lands in the open folder.
        ctx.droppedFiles.push_back(outside / "checker.tga");
        ctx.droppedFiles.push_back(outside / "pyramid.obj");
        ctx.droppedFiles.push_back(outside / "notes.txt");
        ui.frames(3);
        CHECK(ctx.droppedFiles.empty());
        CHECK(fs::exists(ctx.project.assetsDir() / "Textures" / "checker.tga"));
        CHECK(fs::exists(ctx.project.assetsDir() / "Models" / "pyramid.obj"));
        CHECK(!fs::exists(ctx.project.assetsDir() / "notes.txt"));

        // The unsupported file is reported rather than dropped silently.
        ui.key(ImGuiKey_Enter);
        ui.frames(3);

        // Folders sort by name: Audio, Materials, Models, Scenes, Scripts, Textures.
        ui.doubleClick(ui.tile(5));
        ui.frames(3);

        const ObjectId cube = ctx.scene().getRoots()[2]->id;
        ctx.select(InvalidObjectId);
        ctx.scene().find(cube)->setPosition(glm::vec3(0.0f, 0.5f, 0.0f));
        ui.frames(3);

        // Dragging a texture onto an object in the viewport applies it.
        ui.drag(ui.tile(0), ui.viewportCenter());
        ui.frames(2);
        Shape* shape = ctx.scene().findAs<Shape>(cube);
        CHECK(shape && shape->getTexture("texture_albedo") != nullptr);
        if (shape && shape->getTexture("texture_albedo")) {
            CHECK(shape->getTexture("texture_albedo")->assetPath == "Assets/Textures/checker.tga");
        }

        // The thumbnail was cached in the project's Library.
        bool cached = false;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(ctx.project.libraryDir() / "Thumbnails", ec)) {
            cached = cached || entry.path().extension() == ".png";
        }
        CHECK(cached);

        // A model dragged into the viewport lands on the ground under the mouse.
        ui.click(ui.folderRow(3));
        ui.frames(3);

        const size_t before = ctx.scene().getObjects().size();
        ImVec2 dropPoint = ui.viewportCenter();
        dropPoint.y += 60.0f;
        ui.drag(ui.tile(0), dropPoint);
        ui.frames(2);

        CHECK(ctx.scene().getObjects().size() == before + 1);
        auto* mesh = dynamic_cast<MeshShape*>(ctx.getSelected());
        CHECK(mesh != nullptr);
        if (mesh) {
            CHECK(mesh->getAssetPath() == "Assets/Models/pyramid.obj");
            CHECK(std::abs(mesh->position.y) < 0.001f);
            CHECK(glm::length(mesh->position) > 0.1f);
        }

        // The mesh was parsed once, and the result cached in Library.
        bool meshCached = false;
        for (const auto& entry : fs::directory_iterator(ctx.project.libraryDir() / "Meshes", ec)) {
            meshCached = meshCached || entry.path().extension() == ".mesh";
        }
        CHECK(meshCached);
    }

    void testPlayToolbar(Driver& ui, EditorContext& ctx) {
        const ObjectId cube = ctx.scene().getRoots()[2]->id;
        ctx.scene().find(cube)->setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
        ctx.scene().findAs<Shape>(cube)->useGravity = true;
        ctx.scene().findAs<Shape>(cube)->isStatic = false;
        ui.frames(2);

        ui.key(ImGuiKey_P, COMMAND);
        CHECK(ctx.play.isPlaying());
        CHECK(ctx.viewMode == ViewMode::Game);

        ui.frames(60);
        CHECK(ctx.scene().find(cube)->position.y < 5.0f);

        ui.key(ImGuiKey_P, COMMAND);
        ui.frames(2);
        CHECK(ctx.play.isStopped());
        CHECK(ctx.viewMode == ViewMode::Scene);
        CHECK(ctx.scene().find(cube)->position.y == 5.0f);
    }

    // A script made from the PlayerControl template walks a Player that has no game rules,
    // and 2D elements exist in the scene without any of the engine's game.
    void testScripts(Driver& ui, EditorContext& ctx) {
        ctx.document.createNew();
        ui.frames(2);

        SceneObject* floor = ctx.createObject(ObjectType::Cube);
        floor->setPosition(glm::vec3(0.0f, -0.5f, 0.0f));
        floor->setScale(glm::vec3(60.0f, 1.0f, 60.0f));
        static_cast<Shape*>(floor)->isStatic = true;
        ctx.createObject(ObjectType::Camera);
        SceneObject* player = ctx.createObject(ObjectType::Player);
        player->setPosition(glm::vec3(0.0f, 1.2f, 0.0f));
        const ObjectId playerId = player->id;
        SceneObject* hud = ctx.createUi("HUD");
        CHECK(hud && hud->getChildren().size() == 3);
        SceneObject* menu = ctx.createUi("Menu");
        CHECK(menu && menu->getChildren().size() == 1);

        std::string error;
        const ScriptBuilder::Template& source = ScriptBuilder::templates()[0];
        const fs::path file = ScriptBuilder::createFromTemplate(source, "PlayerControl", ctx.project.assetsDir() / "Scripts", error);
        CHECK(!file.empty());
        CHECK(!ScriptBuilder::createFromTemplate(source, "PlayerControl", ctx.project.assetsDir() / "Scripts", error).empty() == false);
        CHECK(ScriptBuilder::createFromTemplate(source, "1bad", ctx.project.assetsDir() / "Scripts", error).empty());
        CHECK(ScriptBuilder::scan(ctx.project).size() == 1 && ScriptBuilder::scan(ctx.project)[0].name == "PlayerControl");

        player->script = "PlayerControl";

        // Without the script the player stands where it is: the engine's game does not move it.
        ui.key(ImGuiKey_P, COMMAND);
        CHECK(ctx.play.isPlaying() && !ctx.play.getGameStatus().hasPlayer);
        ui.runFor(0.4);
        CHECK(ctx.scene().find(playerId)->position.y < 1.3f);
        const glm::vec3 start = ctx.scene().find(playerId)->position;

        ImGui::GetIO().AddKeyEvent(ImGuiKey_W, true);
        ui.runFor(0.5);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_W, false);
        ui.frames(2);
        CHECK(ctx.scene().find(playerId)->position.z < start.z - 0.5f);

        ui.key(ImGuiKey_P, COMMAND);
        ui.frames(2);
        CHECK(ctx.play.isStopped());
        CHECK(ctx.scene().find(playerId)->position.z == start.z);
    }

    // Source files open in the Code Editor, or in the program the user chose instead.
    void testCodeEditor(Driver& ui, EditorContext& ctx) {
        const fs::path file = ctx.project.assetsDir() / "notes.txt";
        std::ofstream(file) << "one\ntwo\n";

        ctx.toolRequests.code = file;
        ui.frames(3);
        CHECK(ctx.toolRequests.code.empty());

        ctx.settings.externalEditor = "cp {file} {file}.opened";
        ctx.toolRequests.code = file;
        ui.frames(3);
        for (int i = 0; i < 50 && !fs::exists(file.string() + ".opened"); i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(fs::exists(file.string() + ".opened"));
        ctx.settings.externalEditor.clear();
        CHECK(isTextAsset("a.cpp") && isTextAsset("B.JSON") && !isTextAsset("a.png"));
    }

    // Cmd+Z takes back the last settled change, and Shift+Cmd+Z brings it back.
    void testUndo(Driver& ui, EditorContext& ctx) {
        ctx.document.createNew();
        ui.frames(3);

        SceneObject* cube = ctx.createObject(ObjectType::Cube);
        const ObjectId id = cube->id;
        ui.frames(3);
        cube->setPosition(glm::vec3(4.0f, 0.0f, 0.0f));
        ctx.document.markDirty();
        ui.frames(3);
        CHECK(ctx.canUndo());

        ui.key(ImGuiKey_Z, COMMAND);
        ui.frames(2);
        CHECK(ctx.scene().find(id) && ctx.scene().find(id)->position.x == 0.0f);
        ui.key(ImGuiKey_Z, COMMAND);
        ui.frames(2);
        CHECK(ctx.scene().find(id) == nullptr);

        ctx.redo();
        ui.frames(2);
        CHECK(ctx.scene().find(id) != nullptr);
        ctx.redo();
        ui.frames(2);
        CHECK(ctx.scene().find(id) && ctx.scene().find(id)->position.x == 4.0f);
        CHECK(!ctx.canRedo());
    }

    // The editors open the files they are asked for and draw them without trouble.
    void testToolEditors(Driver& ui, EditorContext& ctx) {
        std::string error;
        const fs::path model = ctx.project.assetsDir() / "tool_cube.obj";
        CHECK(EditMesh::cube(2.0f).saveObj(model, &error));
        ctx.toolRequests.model = model;
        ui.frames(6);
        CHECK(ctx.toolRequests.model.empty());

        AudioClip clip;
        clip.sampleRate = 8000;
        for (int i = 0; i < 4000; i++) clip.samples.push_back((int16_t)(std::sin(i * 0.1f) * 12000));
        const fs::path sound = ctx.project.assetsDir() / "tool_tone.wav";
        CHECK(saveWav(clip, sound.string(), &error));
        ctx.toolRequests.audio = sound;
        ui.frames(6);
        CHECK(ctx.toolRequests.audio.empty());

        // A terrain made from the menu draws, and the brush shapes it.
        SceneObject* created = ctx.createObject(ObjectType::Terrain);
        auto* terrain = static_cast<TerrainObject*>(created);
        terrain->reset(16, 1.0f);
        ui.frames(3);
        CHECK(terrain->getMesh() && !terrain->needsRebuild());
        terrain->applyBrush(TerrainObject::Brush::Raise, glm::vec2(0.0f), 3.0f, 5.0f, 0.5f, 0.2f);
        CHECK(terrain->needsRebuild());
        ui.frames(3);
        CHECK(!terrain->needsRebuild() && terrain->getMesh()->getBoundsMax().y > 0.5f);
    }

    void testHeadTracking(Driver& ui, EditorContext& ctx) {
        // The mock stands in for AirPods: the Editor feeds it through the same controller every frame.
        ctx.headTracking.setKind(HeadTrackerKind::Mock);
        ui.runFor(1.5);
        const HeadTrackingDiagnostics info = ctx.headTracking.diagnostics();
        CHECK(info.state == HeadTrackingState::Tracking);
        CHECK(info.updateHz > 20.0f);
        CHECK(glm::angle(ctx.headTracking.getRotation()) > 0.001f);   // the head has moved from "ahead"

        ctx.headTracking.setKind(HeadTrackerKind::Off);
        ui.runFor(0.5);
        CHECK(glm::angle(ctx.headTracking.getRotation()) < 0.01f);   // and the view settles back
    }

    void testGameControls(Driver& ui, EditorContext& ctx) {
        auto holdKey = [&](ImGuiKey key, int frames) {
            ImGui::GetIO().AddKeyEvent(key, true);
            ui.frames(frames);
            ImGui::GetIO().AddKeyEvent(key, false);
            ui.frames(2);
        };

        SceneObject* floor = ctx.createObject(ObjectType::Cube);
        floor->setPosition(glm::vec3(0.0f, -20.5f, 0.0f));
        floor->setScale(glm::vec3(60.0f, 1.0f, 60.0f));
        static_cast<Shape*>(floor)->isStatic = true;

        SceneObject* created = ctx.createObject(ObjectType::Player);
        CHECK(!static_cast<Player*>(created)->gameControls);   // a new player does nothing by itself
        static_cast<Player*>(created)->gameControls = true;
        created->setPosition(glm::vec3(0.0f, -18.5f, 0.0f));
        const ObjectId player = created->id;
        ui.frames(2);

        ui.key(ImGuiKey_P, COMMAND);
        CHECK(ctx.play.isPlaying());
        CHECK(ctx.play.getGameStatus().hasPlayer);
        ui.runFor(0.5);

        const glm::vec3 start = ctx.scene().find(player)->position;

        ImGui::GetIO().AddKeyEvent(ImGuiKey_W, true);
        ui.runFor(0.3);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_W, false);
        ui.frames(2);
        const glm::vec3 walked = ctx.scene().find(player)->position;
        CHECK(walked.z < start.z - 0.05f);

        // Space jumps, and does not also press the Play button that took the last click.
        ui.runFor(0.2);
        const float ground = ctx.scene().find(player)->position.y;
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Space, true);
        float highest = ground;
        for (int i = 0; i < 12; i++) {
            ui.runFor(0.016);
            highest = std::max(highest, ctx.scene().find(player)->position.y);
        }
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Space, false);
        ui.frames(2);
        CHECK(highest > ground + 0.05f);
        CHECK(ctx.play.isPlaying());

        // V goes to first person, which takes the mouse from the UI. GLFW ignores scripted
        // cursor moves in a window without focus, so looking itself is tested in render_tests.
        ui.key(ImGuiKey_V);
        CHECK(ctx.play.getGameStatus().firstPerson);
        CHECK(ctx.wantsMouseCapture);
        CHECK(glfwGetInputMode(glfwGetCurrentContext(), GLFW_CURSOR) == GLFW_CURSOR_DISABLED);
        CHECK((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) != 0);

        // Esc gives the mouse back, and clicking the view takes it again.
        ui.key(ImGuiKey_Escape);
        CHECK(!ctx.wantsMouseCapture);
        CHECK(glfwGetInputMode(glfwGetCurrentContext(), GLFW_CURSOR) == GLFW_CURSOR_NORMAL);
        ui.frames(2);
        ui.click(ui.viewportCenter());
        ui.frames(2);
        CHECK(ctx.wantsMouseCapture);

        // Third person keeps mouse look; Esc frees the mouse there too.
        ui.key(ImGuiKey_V);
        ui.frames(2);
        CHECK(!ctx.play.getGameStatus().firstPerson);
        CHECK(ctx.wantsMouseCapture);
        ui.key(ImGuiKey_Escape);
        ui.frames(2);
        CHECK((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) == 0);

        // R starts over without leaving Play mode.
        ui.key(ImGuiKey_R);
        CHECK(ctx.play.isPlaying());
        CHECK(glm::length(ctx.scene().find(player)->position - glm::vec3(0.0f, -18.5f, 0.0f)) < 1.0f);

        // In the Scene view the same keys go back to being the editor's own.
        ctx.viewMode = ViewMode::Scene;
        ui.frames(2);
        const glm::vec3 parked = ctx.scene().find(player)->position;
        holdKey(ImGuiKey_W, 10);
        CHECK(std::abs(ctx.scene().find(player)->position.z - parked.z) < 0.001f);

        ui.key(ImGuiKey_P, COMMAND);
        CHECK(ctx.play.isStopped());
        ctx.select(player);
        ctx.deleteSelected();
        ctx.select(floor->id);
        ctx.deleteSelected();
        ui.frames(2);
    }

    void testUnsavedChangesDialog(Driver& ui, EditorContext& ctx) {
        ctx.createObject(ObjectType::Cube);
        CHECK(ctx.document.isDirty());

        ctx.closeProject();
        ui.frames(3);
        CHECK(ctx.getExitRequest() == EditorExit::None);

        // Cancel keeps the project open and the changes in place.
        ui.key(ImGuiKey_Escape);
        ui.frames(3);
        CHECK(ctx.getExitRequest() == EditorExit::None);
        CHECK(ctx.document.isDirty());

        ctx.closeProject();
        ui.frames(3);
        ImGuiWindow* dialog = ui.window("Unsaved changes");
        CHECK(dialog != nullptr);
        if (dialog) {
            // "Save" is the first button on the dialog's last row.
            ui.click(ImVec2(dialog->Pos.x + 42.0f, dialog->Pos.y + dialog->Size.y - 32.0f));
            ui.frames(3);
        }
        CHECK(!ctx.document.isDirty());
        CHECK(ctx.getExitRequest() == EditorExit::CloseProject);
    }
}

int main() {
    GlWindow::Config config;
    config.width = 1400;
    config.height = 900;
    config.title = "ui tests";
    config.visible = false;
    GlWindow window(config);

    if (!window.isValid()) {
        std::printf("ui_tests: no OpenGL context available, skipping\n");
        return 77;
    }

    TempDir temp;

    const fs::path outside = temp.path / "outside";
    fs::create_directories(outside);
    {
        // An 8x8 TGA, then a pyramid, then a format the importer must refuse.
        std::ofstream texture(outside / "checker.tga", std::ios::binary);
        unsigned char header[18] = {};
        header[2] = 2;
        header[12] = 8;
        header[14] = 8;
        header[16] = 24;
        texture.write(reinterpret_cast<const char*>(header), sizeof(header));
        for (int i = 0; i < 64; i++) {
            const unsigned char pixel[3] = {200, 80, 60};
            texture.write(reinterpret_cast<const char*>(pixel), 3);
        }
        std::ofstream(outside / "pyramid.obj") << "v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv 0 2 0\n"
                                                  "f 1 2 3 4\nf 1 5 2\nf 2 5 3\nf 3 5 4\nf 4 5 1\n";
        std::ofstream(outside / "notes.txt") << "not an asset";
    }

    ProjectCreateOptions options;
    options.name = "UI Tests";
    options.location = temp.path;
    auto project = Project::create(options);
    if (!project) return 1;

    {
        ImGuiLayer imgui(window.handle(), "");
        EditorSession session(*project, window.handle(), imgui, true);
        EditorContext& ctx = session.context();
        Driver ui(session);

        ui.frames(5);

        testSelectionAndHierarchy(ui, ctx);
        testRename(ui, ctx);
        testEffectObjects(ui, ctx);
        testShortcuts(ui, ctx);
        testViewportPickingAndGizmo(ui, ctx);
        testAssetBrowser(ui, ctx, outside);
        testPlayToolbar(ui, ctx);
        testGameControls(ui, ctx);
        testUnsavedChangesDialog(ui, ctx);
        testScripts(ui, ctx);
        testUndo(ui, ctx);
        testCodeEditor(ui, ctx);
        testToolEditors(ui, ctx);
        testHeadTracking(ui, ctx);
    }

    return testSummary("ui_tests");
}

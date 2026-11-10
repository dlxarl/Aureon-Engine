#include "EditorSession.h"
#include "ImGuiLayer.h"
#include "Log.h"
#include "Theme.h"

#include "imgui_internal.h"

#include <GLFW/glfw3.h>

#include <system_error>

namespace fs = std::filesystem;

namespace {
    const float CAMERA_TURN_SPEED = 120.0f;  // degrees per second
    const float MOUSE_TURN_SPEED = 0.25f;    // degrees per point
    const float MOUSE_LOOK_SPEED = 0.12f;    // degrees per point, first person

    GameInput readGameInput(const EditorContext& ctx) {
        GameInput input;
        const ImGuiIO& io = ImGui::GetIO();
        if (ctx.viewMode != ViewMode::Game || io.WantTextInput) return input;

        auto axis = [](ImGuiKey positive, ImGuiKey alternative, ImGuiKey negative, ImGuiKey alternativeNegative) {
            const bool up = ImGui::IsKeyDown(positive) || ImGui::IsKeyDown(alternative);
            const bool down = ImGui::IsKeyDown(negative) || ImGui::IsKeyDown(alternativeNegative);
            return (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f);
        };

        input.moveForward = axis(ImGuiKey_W, ImGuiKey_UpArrow, ImGuiKey_S, ImGuiKey_DownArrow);
        input.moveRight = axis(ImGuiKey_D, ImGuiKey_RightArrow, ImGuiKey_A, ImGuiKey_LeftArrow);
        input.jump = ImGui::IsKeyDown(ImGuiKey_Space);
        input.dive = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_C);
        input.sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
        input.interact = ImGui::IsKeyPressed(ImGuiKey_F, false);
        input.usePotion = ImGui::IsKeyPressed(ImGuiKey_H, false);
        for (int digit = 1; digit <= 9; digit++) {
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + digit - 1), false)) input.number = digit;
        }

        const float keyTurn = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
        input.turnDegrees = keyTurn * CAMERA_TURN_SPEED * io.DeltaTime;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            input.turnDegrees += io.MouseDelta.x * MOUSE_TURN_SPEED;
        }
        return input;
    }

    void onFilesDropped(GLFWwindow* window, int count, const char** paths) {
        auto* ctx = static_cast<EditorContext*>(glfwGetWindowUserPointer(window));
        for (int i = 0; i < count; i++) {
            ctx->droppedFiles.push_back(fs::u8path(paths[i]));
        }
    }

    void buildDefaultLayout(ImGuiID dockspace, ImVec2 size) {
        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, size);

        ImGuiID center = dockspace;
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);

        ImGui::DockBuilderDockWindow("###Viewport", center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Project", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Render Settings", right);
        ImGui::DockBuilderFinish(dockspace);
    }

    void openInitialScene(EditorContext& ctx) {
        const std::string startup = ctx.project.startupScene;

        std::error_code ec;
        if (!startup.empty() && fs::exists(ctx.assets.resolve(startup), ec)) {
            std::string error;
            if (ctx.document.open(startup, &error)) return;
            Log::error("Scene", "Cannot open " + startup + ": " + error);
        } else if (!startup.empty()) {
            Log::warn("Scene", "The startup scene is missing: " + startup);
        }

        ctx.document.createNew();
    }
}

EditorSession::EditorSession(Project& project, GLFWwindow* sessionWindow, ImGuiLayer& imguiLayer, bool useDefaultLayout)
    : window(sessionWindow),
      imgui(imguiLayer),
      ctx(project),
      thumbnails(ctx.assets),
      projectPanel(ctx),
      layoutPending(useDefaultLayout) {
    ctx.monoFont = imgui.monoFont();
    ctx.gameFont = imgui.gameFont();
    ctx.fontFor = [this](const std::string& assetPath) {
        return imgui.fontFor(assetPath.empty() ? std::string() : ctx.assets.resolve(assetPath).string());
    };

    glfwSetWindowUserPointer(window, &ctx);
    glfwSetDropCallback(window, onFilesDropped);

    Log::info("Editor", "Opened project " + project.getInfo().name + " at " + project.getRoot().u8string());
    openInitialScene(ctx);
}

EditorSession::~EditorSession() {
    glfwSetDropCallback(window, nullptr);
    glfwSetWindowUserPointer(window, nullptr);
}

void EditorSession::updateMouseCapture() {
    const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
    if (windowWasFocused && !focused) {
        ctx.wantsMouseCapture = false;
    }
    windowWasFocused = focused;

    const bool stillPlaying = !ctx.play.isStopped() && ctx.viewMode == ViewMode::Game &&
                              ctx.play.getGameStatus().hasPlayer;
    if (!stillPlaying) ctx.wantsMouseCapture = false;

    if (ctx.wantsMouseCapture == mouseCaptured) return;
    mouseCaptured = ctx.wantsMouseCapture;

    glfwSetInputMode(window, GLFW_CURSOR, mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    glfwGetCursorPos(window, &lastCursorX, &lastCursorY);

    ImGuiIO& io = ImGui::GetIO();
    if (mouseCaptured) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
}

void EditorSession::addMouseLook(GameInput& input) {
    if (!mouseCaptured) return;

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    input.turnDegrees += (float)(x - lastCursorX) * MOUSE_LOOK_SPEED;
    input.lookUpDegrees -= (float)(y - lastCursorY) * MOUSE_LOOK_SPEED;
    lastCursorX = x;
    lastCursorY = y;

    // ImGui ignores the mouse during capture, so the button is read directly.
    input.attack = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}

void EditorSession::frame() {
    if (ctx.project.uiFont != appliedUiFont) {
        appliedUiFont = ctx.project.uiFont;
        ctx.gameFont = imgui.setGameFont(appliedUiFont.empty() ? std::string() : ctx.assets.resolve(appliedUiFont).string());
    }
    imgui.beginFrame();
    thumbnails.newFrame();

    ImGuiIO& io = ImGui::GetIO();
    const float deltaTime = io.DeltaTime;

    // Space and the arrows would otherwise also press whichever button has keyboard focus.
    const bool gameHasKeyboard = !ctx.play.isStopped() && ctx.viewMode == ViewMode::Game;
    if (gameHasKeyboard) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !io.WantTextInput) {
            ctx.play.restart();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false) && !io.WantTextInput && ctx.play.getGameStatus().hasPlayer) {
            ctx.play.toggleFirstPerson();
        }
        // The first Esc gives the mouse back; the next one leaves the fullscreen view.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (ctx.wantsMouseCapture) {
                ctx.wantsMouseCapture = false;
            } else {
                ctx.fullscreenPreview = false;
            }
        }
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    // The window follows the flag, so that every way of setting it takes the screen the same way.
    applyFullscreen(ctx.fullscreenPreview && !ctx.play.isStopped() && ctx.viewMode == ViewMode::Game);
    const bool fullscreen = glfwGetWindowMonitor(window) != nullptr && ctx.fullscreenPreview;

    updateMouseCapture();

    GameInput input = readGameInput(ctx);
    addMouseLook(input);
    // The tracker runs whenever it is chosen, so its diagnostics are live; only the Game view uses it.
    const glm::quat head = ctx.headTracking.update(deltaTime);
    if (ctx.viewMode == ViewMode::Game) input.headRotation = head;
    ctx.play.update(deltaTime, input);
    if (ctx.play.isStopped()) ctx.scene().animate(deltaTime);   // fires burn and water moves while editing
    ctx.updateAutosave(deltaTime);
    ctx.updateUndo();

    if (fullscreen) {
        bars.drawFullscreen(ctx, panels);
        viewport.draw(ctx, true);
        ctx.dialogs.draw();
        viewport.render(ctx);
        imgui.endFrame();
        return;
    }

    bars.draw(ctx, panels);

    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const ImGuiID dockspace = ImGui::GetID("##dockspace");

    if (bars.takeLayoutReset()) layoutPending = true;
    if (layoutPending) {
        buildDefaultLayout(dockspace, mainViewport->WorkSize);
        layoutPending = false;
    }

    // Each tab has its own close button; the node-wide one would only repeat it.
    ImGui::DockSpaceOverViewport(dockspace, mainViewport, ImGuiDockNodeFlags_NoCloseButton);

    if (panels.hierarchy) hierarchy.draw(ctx, &panels.hierarchy);
    if (panels.inspector) inspector.draw(ctx, thumbnails, &panels.inspector);
    if (panels.renderSettings) renderSettings.draw(ctx, &panels.renderSettings);
    if (panels.headTracking) headTrackingPanel.draw(ctx, &panels.headTracking);
    projectPanel.importDroppedFiles(ctx);
    if (panels.project) projectPanel.draw(ctx, thumbnails, &panels.project);
    if (panels.console) consolePanel.draw(ctx, &panels.console);
    viewport.draw(ctx);

    ctx.textEditorFocused = false;

    // Files other panels asked the tools to open.
    if (!ctx.toolRequests.code.empty()) {
        if (ctx.settings.externalEditor.empty()) panels.codeEditor = true;
        codeEditor.openFile(ctx, ctx.toolRequests.code, ctx.toolRequests.codeLine);
        ctx.toolRequests.code.clear();
        ctx.toolRequests.codeLine = 1;
    }
    if (panels.audioEditor || !ctx.toolRequests.audio.empty()) {
        if (!ctx.toolRequests.audio.empty()) panels.audioEditor = true;
        audioEditor.draw(ctx, &panels.audioEditor);
    }
    if (panels.modelEditor || !ctx.toolRequests.model.empty()) {
        if (!ctx.toolRequests.model.empty()) panels.modelEditor = true;
        modelEditor.draw(ctx, &panels.modelEditor);
    }
    if (panels.terrainEditor) terrainEditor.draw(ctx, &panels.terrainEditor);
    else ctx.terrainBrush.enabled = false;
    if (panels.codeEditor) codeEditor.draw(ctx, &panels.codeEditor);

    ctx.dialogs.draw();

    viewport.render(ctx);

    imgui.endFrame();
}

void EditorSession::applyFullscreen(bool wanted) {
    const bool isFullscreen = glfwGetWindowMonitor(window) != nullptr;
    if (wanted == isFullscreen) return;

    if (wanted) {
        glfwGetWindowPos(window, &windowedX, &windowedY);
        glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode) return;
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedWidth, windowedHeight, GLFW_DONT_CARE);
    }
}

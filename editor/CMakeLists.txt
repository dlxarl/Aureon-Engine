# Dear ImGui and ImGuizmo do not build themselves in a form that fits, so they are compiled here.
add_library(editor_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
target_include_directories(editor_imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imgui_SOURCE_DIR}/misc/cpp
        ${imguizmo_SOURCE_DIR}/src
)
target_link_libraries(editor_imgui PUBLIC glfw)

# The project system. It stays free of OpenGL and UI code so that it can be tested headlessly.
add_library(editor_core STATIC
        src/project/AssetImporter.cpp
        src/project/Project.cpp
        src/project/RecentProjects.cpp
        src/project/AudioEdit.cpp
        src/project/EditMesh.cpp
        src/project/ScriptBuilder.cpp
        src/project/UserSettings.cpp
        src/project/ScriptTemplates.cpp
        src/project/UserPaths.cpp
)
target_include_directories(editor_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/project
        ${stb_SOURCE_DIR}
)
target_compile_definitions(editor_core PRIVATE ENGINE_VERSION="${PROJECT_VERSION}")
# Where the headers are that project scripts are compiled against.
target_compile_definitions(editor_core PRIVATE
        ENGINE_SCRIPT_INCLUDES="${CMAKE_SOURCE_DIR}/engine/include|${glm_SOURCE_DIR}|${glfw_SOURCE_DIR}/include")
target_link_libraries(editor_core
        PUBLIC engine
        PRIVATE nlohmann_json::nlohmann_json
)

# The Editor proper. A library rather than a part of the executable, so that the UI tests
# can run the same code.
set(EDITOR_SOURCES
        src/app/EditorApp.cpp
        src/app/EditorSession.cpp
        src/app/ImGuiLayer.cpp
        src/app/LauncherApp.cpp
        src/app/Theme.cpp
        src/app/Widgets.cpp

        src/core/ConsoleLog.cpp
        src/core/Dialogs.cpp
        src/core/EditorContext.cpp
        src/core/PlayController.cpp
        src/core/SceneDocument.cpp

        src/panels/AssetThumbnails.cpp
        src/panels/ConsolePanel.cpp
        src/panels/GameHud.cpp
        src/panels/HeadTrackingPanel.cpp
        src/panels/UiLayer.cpp
        src/panels/CodeEditorPanel.cpp
        src/panels/AudioEditorPanel.cpp
        src/panels/TerrainEditorPanel.cpp
        src/panels/ModelEditorPanel.cpp
        src/panels/HierarchyPanel.cpp
        src/panels/InspectorPanel.cpp
        src/panels/MainBars.cpp
        src/panels/ProjectPanel.cpp
        src/panels/RenderSettingsPanel.cpp
        src/panels/ViewportPanel.cpp

        src/viewport/EditorCamera.cpp
        src/viewport/ViewportRenderer.cpp
)

if (APPLE)
    list(APPEND EDITOR_SOURCES src/platform/Platform_mac.mm)
else()
    list(APPEND EDITOR_SOURCES src/platform/Platform_stub.cpp)
endif()

add_library(editor_ui STATIC ${EDITOR_SOURCES})

target_include_directories(editor_ui PUBLIC
        src/app
        src/core
        src/panels
        src/platform
        src/project
        src/viewport
)

target_link_libraries(editor_ui PUBLIC editor_core editor_imgui)

if (APPLE)
    target_link_libraries(editor_ui PUBLIC "-framework Cocoa" "-framework UniformTypeIdentifiers")
endif()

add_executable(Editor MACOSX_BUNDLE src/main.cpp)
target_link_libraries(Editor PRIVATE editor_ui)

if (APPLE)
    set_target_properties(Editor PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER com.github.dlxarl.aureon.editor
            MACOSX_BUNDLE_BUNDLE_NAME "Aureon Editor"
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
endif()

# Renders a scene to a PNG without opening the Editor; the MCP server uses it to see.
add_executable(aureon_shot src/tools_shot.cpp)
target_link_libraries(aureon_shot PRIVATE editor_core)
target_include_directories(aureon_shot PRIVATE ${stb_SOURCE_DIR})
engine_add_resources(aureon_shot)

engine_add_resources(Editor
        EDITOR_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders"
        FONTS
            "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf"
            "${imgui_SOURCE_DIR}/misc/fonts/Cousine-Regular.ttf"
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter.ttf"
)

# Prints what the mock head tracker reports, for checking the pipeline without the Editor. It has no
# AirPods option: macOS holds the program that launched a tool responsible for its motion access, and
# ends the tool if that program has no NSMotionUsageDescription. Only the Editor bundle has one.
add_executable(aureon_headtrack src/tools_headtrack.cpp)
target_link_libraries(aureon_headtrack PRIVATE engine)

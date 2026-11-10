add_library(engine STATIC
        src/glad.c

        src/Log.cpp
        src/Particles.cpp
        src/EffectObjects.cpp
        src/EffectRenderer.cpp
        src/Audio.cpp
        src/GameAudio.cpp
        src/Resources.cpp
        src/GlWindow.cpp

        src/Engine.cpp
        src/Input.cpp
        src/Shader.cpp
        src/Texture.cpp
        src/stb_image_impl.cpp

        src/SceneObject.cpp
        src/Shape.cpp
        src/Cube.cpp
        src/Plane.cpp
        src/Sphere.cpp
        src/Cylinder.cpp
        src/MeshShape.cpp
        src/ObjectFactory.cpp

        src/Skybox.cpp
        src/PostProcessor.cpp
        src/Player.cpp
        src/Physics.cpp
        src/GameSession.cpp
        src/GameActors.cpp
        src/HeightField.cpp
        src/GameDefinition.cpp
        src/GameProgress.cpp
        src/GameClock.cpp
        src/CharacterController.cpp
        src/Frustum.cpp
        src/Colliders.cpp

        src/RenderSettings.cpp
        src/RenderTarget.cpp
        src/Renderer.cpp

        src/LightObject.cpp
        src/RayTrace.cpp
        src/SceneGraph.cpp
        src/Terrain.cpp
        src/ScriptHost.cpp
        src/SceneSerializer.cpp
        src/AssetManager.cpp
        src/ObjLoader.cpp
        src/HeadTracking.cpp
)

target_include_directories(engine
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${glm_SOURCE_DIR}
        PRIVATE
            ${stb_SOURCE_DIR}
)

target_link_libraries(engine
        PUBLIC glfw
        PRIVATE nlohmann_json::nlohmann_json
)

if (APPLE)
    target_link_libraries(engine PUBLIC "-framework AudioToolbox" "-framework CoreAudio")

    # AirPods head tracking. Core Motion stays inside this one file; see HeadTracking.h.
    target_sources(engine PRIVATE src/CoreMotionHeadTracker.mm)
    set_source_files_properties(src/CoreMotionHeadTracker.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(engine PUBLIC "-framework CoreMotion" "-framework Foundation")
endif()

if (WIN32)
    target_link_libraries(engine PUBLIC opengl32 gdi32 user32 winmm shell32)
endif()

set(ENGINE_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders" CACHE INTERNAL "Built-in engine shaders")

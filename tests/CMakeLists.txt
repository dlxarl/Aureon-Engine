add_executable(project_tests project_tests.cpp)
target_link_libraries(project_tests PRIVATE editor_core)
add_test(NAME project_tests COMMAND project_tests)

add_executable(audio_tests audio_tests.cpp)
target_link_libraries(audio_tests PRIVATE engine)
add_test(NAME audio_tests COMMAND audio_tests)

add_executable(head_tracking_tests head_tracking_tests.cpp)
target_link_libraries(head_tracking_tests PRIVATE engine)
add_test(NAME head_tracking_tests COMMAND head_tracking_tests)

# These need a window system. Without one they exit with 77, which CTest reports as a skip.
foreach(test_name render_tests ui_tests)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE editor_ui)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()

# Both executables sit in this directory, so they share one copy of the resources.
engine_add_resources(render_tests
        EDITOR_SHADERS "${CMAKE_SOURCE_DIR}/editor/resources/shaders"
        FONTS
            "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf"
            "${imgui_SOURCE_DIR}/misc/fonts/Cousine-Regular.ttf"
)
add_dependencies(ui_tests render_tests_resources)

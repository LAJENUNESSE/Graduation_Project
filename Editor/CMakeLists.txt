file(GLOB_RECURSE EDITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

list(APPEND EDITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorSceneSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorPanelCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorSelectionGizmoController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorShell.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorViewportController.cpp
)

add_executable(Editor ${EDITOR_SOURCES})

# MSVC: treat source and execution charset as UTF-8 so Chinese literals work correctly
if(MSVC)
    target_compile_options(Editor PRIVATE /utf-8)
endif()

target_link_libraries(Editor PRIVATE Engine)

target_include_directories(Editor PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

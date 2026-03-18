set(EDITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorApp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorAssetDescriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorBootstrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorLayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorPanelCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorRenderController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorSceneSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorSelectionGizmoController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorShell.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorViewportController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Panels/AssetBrowserPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Panels/ConsolePanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Panels/PropertiesPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Panels/PropertiesPanelCustomDrawers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Panels/RenderSettingsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Panels/SceneHierarchyPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scripts/CollisionTestScript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scripts/PlayerController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scripts/RotateScript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UndoSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CommandHistory.cpp
)

add_executable(Editor ${EDITOR_SOURCES})

# MSVC: treat source and execution charset as UTF-8 so Chinese literals work correctly
if(MSVC)
    target_compile_options(Editor PRIVATE /utf-8)
endif()

target_link_libraries(Editor PRIVATE Engine)

target_include_directories(Editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

engine_apply_warnings(Editor)
engine_apply_sanitizers(Editor)


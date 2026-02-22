# ── GLFW ──────────────────────────────────────────────────────────────────────
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

# ── spdlog ────────────────────────────────────────────────────────────────────
add_subdirectory(spdlog)

# ── GLM ───────────────────────────────────────────────────────────────────────
add_subdirectory(glm)

# ── yaml-cpp ──────────────────────────────────────────────────────────────────
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(yaml-cpp)

# ── GLAD (glad2 generated files) ─────────────────────────────────────────────
add_library(glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glad-generated/src/gl.c
)
target_include_directories(glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glad-generated/include
)

# ── stb_image ─────────────────────────────────────────────────────────────────
add_library(stb_image STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/stb_image_impl.cpp
)
target_include_directories(stb_image PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── Dear ImGui ────────────────────────────────────────────────────────────────
add_library(imgui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw glad)

# ── EnTT (header-only) ───────────────────────────────────────────────────────
add_library(entt INTERFACE)
target_include_directories(entt INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/entt/single_include
)

# ── tinyfiledialogs ──────────────────────────────────────────────────────────
add_library(tinyfiledialogs STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyfiledialogs/tinyfiledialogs.c
)
target_include_directories(tinyfiledialogs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyfiledialogs
)

# ── ImGuizmo ──────────────────────────────────────────────────────────────────
add_library(imguizmo STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo/ImGuizmo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo/ImCurveEdit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo/ImGradient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo/ImSequencer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo/GraphEditor.cpp
)
target_include_directories(imguizmo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo
)
target_link_libraries(imguizmo PUBLIC imgui)

# ── Assimp ──────────────────────────────────────────────────────────────────
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
add_subdirectory(assimp)

# ── GLFW ──────────────────────────────────────────────────────────────────────
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

# ── spdlog ────────────────────────────────────────────────────────────────────
add_subdirectory(spdlog)
# MSVC + C++20: bundled fmt v12 的 consteval 格式字符串校验有 bug，
# 关闭 consteval 规避 C7595 错误
if(MSVC)
    target_compile_definitions(spdlog PUBLIC FMT_USE_CONSTEVAL=0)
endif()

# ── GLM ───────────────────────────────────────────────────────────────────────
add_subdirectory(glm)

# ── yaml-cpp ──────────────────────────────────────────────────────────────────
# yaml-cpp 0.9.0 的 dragonbox.h 在 MSVC + C++20 下有 static_assert 失败，
# 强制以 C++17 编译规避此问题（yaml-cpp 不需要 C++20 特性）
set(_SAVED_CXX_STANDARD ${CMAKE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD 17)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(yaml-cpp)
set(CMAKE_CXX_STANDARD ${_SAVED_CXX_STANDARD})

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
set(IMGUI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

# Vulkan backend (optional)
if(ENGINE_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    list(APPEND IMGUI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_vulkan.cpp
    )
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw glad)
if(ENGINE_ENABLE_VULKAN)
    target_link_libraries(imgui PUBLIC Vulkan::Vulkan)
endif()

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

# ── ImGuiColorTextEdit ────────────────────────────────────────────────────────
# 原版 BalazsJako 的单头单源实现，仅依赖 imgui + std::regex，无其他依赖
add_library(ImGuiColorTextEdit STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGuiColorTextEdit/TextEditor.cpp
)
target_include_directories(ImGuiColorTextEdit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGuiColorTextEdit
)
target_link_libraries(ImGuiColorTextEdit PUBLIC imgui)
# MSVC 下 TextEditor.cpp 包含大量 UTF-8 中文注释/字符串，需要 /utf-8
if(MSVC)
    target_compile_options(ImGuiColorTextEdit PRIVATE /utf-8)
endif()

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

# ── Bullet3 ─────────────────────────────────────────────────────────────────
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
add_subdirectory(bullet3)

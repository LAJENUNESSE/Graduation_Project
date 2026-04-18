cmake_minimum_required(VERSION 3.20)
project(GameEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(ProjectWarnings)
include(ProjectSanitizers)

option(ENGINE_ENABLE_STRICT_WARNINGS "Enable stricter warnings for project targets" ON)
option(ENGINE_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(ENGINE_ENABLE_ASAN "Enable AddressSanitizer for project targets" OFF)
option(ENGINE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for project targets" OFF)
option(ENGINE_ENABLE_COMPILER_CACHE "Enable compiler cache launcher when available" ON)
option(ENGINE_ENABLE_MSVC_MP "Enable /MP for MSVC C/C++ compilation" ON)

# Detect and enable compiler cache launcher (sccache/ccache) for local builds.
if(ENGINE_ENABLE_COMPILER_CACHE)
    find_program(ENGINE_COMPILER_CACHE_PROGRAM NAMES sccache ccache)
    if(ENGINE_COMPILER_CACHE_PROGRAM)
        if(NOT CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${ENGINE_COMPILER_CACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
        endif()
        if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${ENGINE_COMPILER_CACHE_PROGRAM}" CACHE STRING "CXX compiler launcher" FORCE)
        endif()
        message(STATUS "[Engine] Compiler cache enabled: ${ENGINE_COMPILER_CACHE_PROGRAM}")
    else()
        message(STATUS "[Engine] Compiler cache not found (sccache/ccache); continuing without launcher")
    endif()
endif()

if(MSVC AND ENGINE_ENABLE_MSVC_MP)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C>:/MP>
        $<$<COMPILE_LANGUAGE:CXX>:/MP>
    )
endif()

add_subdirectory(vendor)
add_subdirectory(Engine)
add_subdirectory(Sandbox)
add_subdirectory(Editor)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ─────────────────────────────────────────────────────────────
# 打包：install 规则 + CPack 配置
#
# 目标安装目录布局（与开发时的项目根布局一致，保证运行时
# Engine::PathUtils::DiscoverProjectRoot 能命中 assets/）：
#
#   {app}/
#   ├── Editor/
#   │   ├── Editor.exe
#   │   ├── *.dll            ← vcpkg applocal 已拷到 TARGET_FILE_DIR
#   │   └── assets/
#   │       └── fonts/       ← 中文字体 (NotoSansSC) 等编辑器专属资产
#   ├── assets/               ← 运行时资产 (shaders/models/textures/scenes)
#   ├── LICENSE
#   └── README.md
# ─────────────────────────────────────────────────────────────

# Editor.exe 本体
install(TARGETS Editor
    RUNTIME DESTINATION Editor
)

# Editor.exe 同目录下的运行时 DLL（vcpkg 应用本地化产物：
# avcodec / avformat / avutil / swresample / swscale / OpenAL32 / fmt / lua 等）
# 使用 generator expression 指向实际构建输出目录（含 config 子目录）
install(DIRECTORY "$<TARGET_FILE_DIR:Editor>/"
    DESTINATION Editor
    FILES_MATCHING
        PATTERN "*.dll"
)

# 编辑器专属资产（中文字体等）—— GetEditorAssetRoot() 解析为 {projectRoot}/Editor/assets
install(DIRECTORY "${CMAKE_SOURCE_DIR}/Editor/assets/"
    DESTINATION Editor/assets
)

# 运行时资产
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
    DESTINATION assets
)

# 许可 / README
install(FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/README.md"
    DESTINATION .
)

# vendor 子项目（glfw / bullet / assimp 等）默认会 install 头文件和静态库到
# include/ 与 lib/，对终端用户完全无用。
# 仅在 CPack staging 目录中清理，避免用户 `cmake --install` 到自定义前缀时误删外部文件。
install(CODE [[
    string(REPLACE "\\" "/" _engine_install_prefix_norm "${CMAKE_INSTALL_PREFIX}")
    if(_engine_install_prefix_norm MATCHES "/_CPack_Packages/")
        message(STATUS "[CPack] Cleaning vendor dev artifacts (include/, lib/) from staging")
        file(REMOVE_RECURSE "${CMAKE_INSTALL_PREFIX}/include")
        file(REMOVE_RECURSE "${CMAKE_INSTALL_PREFIX}/lib")
    else()
        message(STATUS "[Install] Skip cleaning include/lib for non-CPack prefix: ${CMAKE_INSTALL_PREFIX}")
    endif()
]])

# ─── CPack 配置 ───
set(CPACK_PACKAGE_NAME "GameEngineEditor")
set(CPACK_PACKAGE_VENDOR "LAJENUNESSE")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "1")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "3D Game Engine & Visual Editor")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "GameEngineEditor")
set(CPACK_PACKAGE_FILE_NAME "GameEngineEditor-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# 先用 ZIP 验证布局；确认无误后再切换到 Inno Setup
set(CPACK_GENERATOR "ZIP")

include(CPack)

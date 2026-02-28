file(GLOB_RECURSE ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/*.cpp
)

add_library(Engine STATIC ${ENGINE_SOURCES})

# MSVC: treat source and execution charset as UTF-8 so Chinese literals work correctly
if(MSVC)
    target_compile_options(Engine PRIVATE /utf-8)
endif()

find_package(Threads REQUIRED)

# FFmpeg + OpenAL
include(FindPkgConfig)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale libswresample)
pkg_check_modules(OPENAL REQUIRED openal)

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor/bullet3/src
    ${FFMPEG_INCLUDE_DIRS}
    ${OPENAL_INCLUDE_DIRS}
)

target_link_libraries(Engine PUBLIC
    glfw
    glad
    spdlog
    glm
    imgui
    entt
    yaml-cpp
    stb_image
    imguizmo
    tinyfiledialogs
    assimp
    BulletDynamics
    BulletCollision
    LinearMath
    Threads::Threads
    ${FFMPEG_LIBRARIES}
    ${OPENAL_LIBRARIES}
)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(Engine PUBLIC X11::X11)
endif()

target_precompile_headers(Engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engpch.h
)

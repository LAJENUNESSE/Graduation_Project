file(GLOB_RECURSE ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/*.cpp
)

list(APPEND ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/CrashHandler.cpp
)

add_library(Engine STATIC ${ENGINE_SOURCES})

# MSVC: treat source and execution charset as UTF-8 so Chinese literals work correctly
if(MSVC)
    target_compile_options(Engine PRIVATE /utf-8)
endif()

find_package(Threads REQUIRED)

# FFmpeg (vcpkg)
find_package(FFMPEG REQUIRED)

# OpenAL (vcpkg)
find_package(OpenAL CONFIG REQUIRED)

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor/bullet3/src
    ${FFMPEG_INCLUDE_DIRS}
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
    OpenAL::OpenAL
)

target_link_directories(Engine PUBLIC ${FFMPEG_LIBRARY_DIRS})

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(Engine PUBLIC X11::X11)
endif()

target_precompile_headers(Engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engpch.h
)

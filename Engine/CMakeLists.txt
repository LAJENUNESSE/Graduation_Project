file(GLOB_RECURSE ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/*.cpp
)

add_library(Engine STATIC ${ENGINE_SOURCES})

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor/bullet3/src
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
)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(Engine PUBLIC X11::X11)
endif()

target_precompile_headers(Engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engpch.h
)

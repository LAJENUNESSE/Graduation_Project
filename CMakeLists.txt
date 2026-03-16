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
option(ENGINE_ENABLE_CUDA "Enable CUDA compute sidecar for particle system" OFF)

# CUDA Configuration (optional)
if(ENGINE_ENABLE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 75 86 89 100)
    endif()
endif()

add_subdirectory(vendor)
add_subdirectory(Engine)
add_subdirectory(Sandbox)
add_subdirectory(Editor)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()


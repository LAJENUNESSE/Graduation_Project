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

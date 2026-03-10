cmake_minimum_required(VERSION 3.20)
project(GameEngine LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CUDA Configuration
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -arch=sm_86")  # RTX 3050
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(ProjectWarnings)
include(ProjectSanitizers)

option(ENGINE_ENABLE_STRICT_WARNINGS "Enable stricter warnings for project targets" OFF)
option(ENGINE_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(ENGINE_ENABLE_ASAN "Enable AddressSanitizer for project targets" OFF)
option(ENGINE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for project targets" OFF)

add_subdirectory(vendor)
add_subdirectory(Engine)
add_subdirectory(Sandbox)
add_subdirectory(Editor)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

cmake_minimum_required(VERSION 3.20)
project(EngineTestsProject)

# Disable testing inside GoogleTest itself
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Avoid CMP0135 warning
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# EngineTestCore — 轻量静态库，仅编译测试所需的少量 Engine 源文件
# 避免链接完整 Engine 库（OpenGL/OpenAL/Bullet3 等重依赖）
# ---------------------------------------------------------------------------
set(ENGINE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/src)

add_library(EngineTestCore STATIC
    ${ENGINE_SRC_DIR}/Core/UUID.cpp
    ${ENGINE_SRC_DIR}/Core/Log.cpp
    ${ENGINE_SRC_DIR}/Scene/SceneEntityIndex.cpp
    ${ENGINE_SRC_DIR}/Scene/SceneHierarchyService.cpp
    ${ENGINE_SRC_DIR}/Scene/WorldTransformService.cpp
    ${ENGINE_SRC_DIR}/Scene/SceneCamera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Editor/src/CommandHistory.cpp
    ${ENGINE_SRC_DIR}/Renderer/SPHRigidBodyCollector.cpp
)

target_compile_features(EngineTestCore PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(EngineTestCore PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

target_include_directories(EngineTestCore PUBLIC
    ${ENGINE_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Editor/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/entt/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/spdlog/include
)

target_precompile_headers(EngineTestCore PRIVATE ${ENGINE_SRC_DIR}/engpch.h)

target_link_libraries(EngineTestCore PUBLIC spdlog)

# ---------------------------------------------------------------------------
# EngineTests — 主测试可执行文件
# ---------------------------------------------------------------------------
add_executable(EngineTests
    src/TestMain.cpp
    src/TestAssetHandle.cpp
    src/TestSlotMap.cpp
    src/TestTimestep.cpp
    src/TestUUID.cpp
    src/TestEvents.cpp
    src/TestSceneEntityIndex.cpp
    src/TestSceneHierarchy.cpp
    src/TestWorldTransform.cpp
    src/TestSPHKernel.cpp
    src/TestSDFMath.cpp
    src/TestCommandHistory.cpp
    src/TestCudaPoisonState.cpp
    src/TestCudaParticleTypes.cpp
    src/TestSPHCommon.cpp
)

target_compile_features(EngineTests PUBLIC cxx_std_20)

# MSVC specific encoding
if(MSVC)
    target_compile_options(EngineTests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

target_include_directories(EngineTests PRIVATE
    ${ENGINE_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/entt/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/spdlog/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../Engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/Platform
)

target_link_libraries(EngineTests PRIVATE
    gtest
    EngineTestCore
    spdlog
)

# Enable CTest integration for this executable
enable_testing()
include(GoogleTest)
gtest_discover_tests(EngineTests)


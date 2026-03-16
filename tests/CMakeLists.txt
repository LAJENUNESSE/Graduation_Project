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

add_executable(EngineTests
    src/TestAssetHandle.cpp
    src/TestSlotMap.cpp
)

target_compile_features(EngineTests PUBLIC cxx_std_20)

# MSVC specific encoding
if(MSVC)
    target_compile_options(EngineTests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

target_include_directories(EngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/src)

# We link directly to gtest_main, and we don't necessarily need to link to Engine
# if we only test header-only pure logic from Engine/src
target_link_libraries(EngineTests PRIVATE
    gtest_main
)

# Enable CTest integration for this executable
enable_testing()
include(GoogleTest)
gtest_discover_tests(EngineTests)

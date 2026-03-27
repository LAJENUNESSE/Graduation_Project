if(NOT ENGINE_ENABLE_CUDA)
    message(STATUS "[InteropDemo] Skipped: ENGINE_ENABLE_CUDA=OFF")
    return()
endif()

find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(STATUS "[InteropDemo] Skipped: Vulkan SDK/loader not found")
    return()
endif()

find_package(CUDAToolkit REQUIRED)

set(INTEROP_DEMO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanCudaInteropDemo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CudaParticleKernel.cu
    ${CMAKE_SOURCE_DIR}/Engine/Platform/CUDA/VulkanExternalInterop.cpp
)

add_executable(VulkanCudaInteropDemo ${INTEROP_DEMO_SOURCES})

if(MSVC)
    target_compile_options(VulkanCudaInteropDemo PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CudaParticleKernel.cu
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

# Keep this target independent from Engine runtime path.
target_compile_features(VulkanCudaInteropDemo PRIVATE cxx_std_20)

target_include_directories(VulkanCudaInteropDemo PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Engine
    ${CMAKE_SOURCE_DIR}/Engine/Platform
)

target_link_libraries(VulkanCudaInteropDemo PRIVATE
    glfw
    Vulkan::Vulkan
    CUDA::cudart
)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(VulkanCudaInteropDemo PRIVATE X11::X11)
endif()

set_property(TARGET VulkanCudaInteropDemo PROPERTY CUDA_STANDARD 17)
set_property(TARGET VulkanCudaInteropDemo PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)
if(MSVC)
    target_compile_options(VulkanCudaInteropDemo PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--compiler-options=/utf-8,/Zc:preprocessor,/wd4505>)
endif()

engine_apply_warnings(VulkanCudaInteropDemo)
engine_apply_sanitizers(VulkanCudaInteropDemo)

set(ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLFramebuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLGPUTimerQuery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLRendererAPI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLShader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLAsyncReadback.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLIBLGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLStorageBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLTexture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLUniformBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLVertexArray.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engpch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Asset/AssetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Asset/AssetType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Asset/AsyncLoadQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Asset/FileWatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Asset/PathUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Audio/AudioClip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Audio/OpenALAudioEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Application.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/CrashHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/FileDialogs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/FluidBenchmarkConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Layer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/LayerStack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/UUID.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Debug/PerformanceMonitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ImGui/ImGuiBuild.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ImGui/ImGuiLayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Media/FFmpegDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Physics/BulletPhysicsWorld.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Physics/PhysicsDebugDraw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Reflection/AutoInspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Reflection/AutoSerializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Reflection/ComponentRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/EditorCamera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/FluidBenchmarkData.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/FluidRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/FluidSystemGPU.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Framebuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GraphicsContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Material.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Mesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/ParticleSystemGPU.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/PostProcessing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RenderCommand.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RendererAPI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RendererCapabilities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RenderQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/SceneRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/SpatialHashGrid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/SPHCommon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/SPHRigidBodyCollector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GPUAsyncReadback.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/IBLGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/StorageBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/UniformBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/VertexArray.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/SceneEntityIndex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/WorldTransformService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/SceneHierarchyService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Runtime/ResourceLifecycleCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Runtime/SceneRuntimeCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/SceneCamera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/SceneSerializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Systems/AudioSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Systems/GrassRenderSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Systems/LightSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Systems/MeshRenderSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Systems/ShadowSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Systems/SkyboxSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Systems/TerrainRenderSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/Systems/VideoSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Script/LuaScriptEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Script/ScriptRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Terrain/TerrainMeshGenerator.cpp
)

# Vulkan platform sources (conditional)
if(ENGINE_ENABLE_VULKAN)
    list(APPEND ENGINE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanAllocator.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanAsyncReadback.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanBarrierUtil.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanBuffer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanCommandBuffer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanDescriptor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanFramebuffer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanContext.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanIBLGenerator.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanPipeline.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanRenderPass.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanRendererAPI.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanShader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanTexture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanSynchronization.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan/VulkanVertexArray.cpp
    )
endif()

# CUDA platform sources (conditional) — .cu / .cpp 混合，CUDA_CHECK 宏跨 nvcc/MSVC
if(ENGINE_ENABLE_CUDA)
    list(APPEND ENGINE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaGLInteropContext.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaParticlePipeline.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaSPHPipeline.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaTimingHelper.cu
    )
endif()

add_library(Engine STATIC ${ENGINE_SOURCES})

target_compile_features(Engine PUBLIC cxx_std_20)

# MSVC: treat source and execution charset as UTF-8 so Chinese literals work correctly
if(MSVC)
    target_compile_options(Engine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

find_package(Threads REQUIRED)

# FFmpeg (vcpkg)
find_package(FFMPEG REQUIRED)

# Lua (vcpkg)
find_package(Lua REQUIRED)

# OpenAL (vcpkg)
find_package(OpenAL CONFIG REQUIRED)

# Vulkan (optional)
if(ENGINE_ENABLE_VULKAN)
    # 显式请求 shaderc_combined；FindVulkan 只会为已请求的可选组件创建对应 target。
    find_package(Vulkan REQUIRED OPTIONAL_COMPONENTS shaderc_combined)
    target_compile_definitions(Engine PUBLIC ENGINE_ENABLE_VULKAN)
    target_include_directories(Engine PRIVATE ${Vulkan_INCLUDE_DIRS})

    if(TARGET Vulkan::shaderc_combined)
        target_compile_definitions(Engine PRIVATE ENGINE_VULKAN_HAS_SHADERC=1)
        message(STATUS "[Engine] Vulkan runtime shader compilation enabled via Vulkan::shaderc_combined")
    else()
        message(WARNING "[Engine] Vulkan::shaderc_combined not found; VulkanShader runtime GLSL->SPIR-V compile disabled")
    endif()

    # spirv-cross：从 SPIR-V 反射 descriptor binding / push constant 信息（Phase 7 compute 必需）
    # 优先用 Vulkan SDK 自带的 spirv-cross（避免 vcpkg 网络下载），fallback 走 vcpkg 提供的 CONFIG 包。
    # Vulkan SDK 同时提供 Release (spirv-cross-core.lib) 和 Debug (spirv-cross-cored.lib) 两套，
    # **必须按 build config 切换** — 否则 MSVC CRT (_ITERATOR_DEBUG_LEVEL / MD vs MDd) 不匹配链接失败。
    find_library(SPIRV_CROSS_CORE_LIB_RELEASE
        NAMES spirv-cross-core
        HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib"
        NO_DEFAULT_PATH
    )
    find_library(SPIRV_CROSS_CORE_LIB_DEBUG
        NAMES spirv-cross-cored
        HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib"
        NO_DEFAULT_PATH
    )
    if(SPIRV_CROSS_CORE_LIB_RELEASE AND SPIRV_CROSS_CORE_LIB_DEBUG)
        # 多配置生成器 (VS) 和单配置生成器 (Ninja) 都用生成器表达式切换
        target_link_libraries(Engine PRIVATE
            "$<$<CONFIG:Debug>:${SPIRV_CROSS_CORE_LIB_DEBUG}>"
            "$<$<NOT:$<CONFIG:Debug>>:${SPIRV_CROSS_CORE_LIB_RELEASE}>"
        )
        target_compile_definitions(Engine PRIVATE ENGINE_VULKAN_HAS_SPIRV_CROSS=1)
        message(STATUS "[Engine] Vulkan SPIR-V reflection enabled via Vulkan SDK spirv-cross-core")
        message(STATUS "[Engine]   Release: ${SPIRV_CROSS_CORE_LIB_RELEASE}")
        message(STATUS "[Engine]   Debug:   ${SPIRV_CROSS_CORE_LIB_DEBUG}")
    elseif(SPIRV_CROSS_CORE_LIB_RELEASE)
        # 仅 Release 可用：限制为非 Debug 配置链接，避免 Debug build CRT 不匹配
        target_link_libraries(Engine PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:${SPIRV_CROSS_CORE_LIB_RELEASE}>"
        )
        target_compile_definitions(Engine PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:ENGINE_VULKAN_HAS_SPIRV_CROSS=1>"
        )
        message(WARNING "[Engine] Vulkan SDK 仅提供 Release spirv-cross-core.lib；Debug build 将禁用 SPIR-V 反射")
    else()
        find_package(spirv_cross_core CONFIG QUIET)
        if(spirv_cross_core_FOUND)
            target_link_libraries(Engine PRIVATE spirv-cross-core)
            target_compile_definitions(Engine PRIVATE ENGINE_VULKAN_HAS_SPIRV_CROSS=1)
            message(STATUS "[Engine] Vulkan SPIR-V reflection enabled via vcpkg spirv-cross-core")
        else()
            message(WARNING "[Engine] spirv-cross-core not found; VulkanShader reflection disabled (compute pipeline will not work)")
        endif()
    endif()
endif()

target_include_directories(Engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/vendor/bullet3/src
        ${FFMPEG_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIR}
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
    ImGuiColorTextEdit
    tinyfiledialogs
    assimp
    BulletDynamics
    BulletCollision
    LinearMath
    Threads::Threads
    ${FFMPEG_LIBRARIES}
    ${LUA_LIBRARIES}
    OpenAL::OpenAL
    $<$<BOOL:${ENGINE_ENABLE_VULKAN}>:Vulkan::Vulkan>
)

if(ENGINE_ENABLE_VULKAN AND TARGET Vulkan::shaderc_combined)
    target_link_libraries(Engine PUBLIC Vulkan::shaderc_combined)
endif()

# CUDA (optional) — nvcc + MSVC 混合编译
if(ENGINE_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    target_compile_definitions(Engine PUBLIC ENGINE_ENABLE_CUDA=1)
    target_include_directories(Engine PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
    target_link_libraries(Engine PUBLIC CUDA::cudart)
    set_property(TARGET Engine PROPERTY CUDA_STANDARD 17)
    set_property(TARGET Engine PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)
    # .cu 文件跳过预编译头（nvcc 不兼容 engpch.h）
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaParticlePipeline.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaSPHPipeline.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaTimingHelper.cu
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    )
    if(MSVC)
        target_compile_options(Engine PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--compiler-options=/utf-8,/Zc:preprocessor,/wd4505>)
    endif()
endif()

target_link_directories(Engine PUBLIC ${FFMPEG_LIBRARY_DIRS})

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(Engine PUBLIC X11::X11)
endif()

if(WIN32)
    target_link_libraries(Engine PUBLIC dwmapi)
endif()

target_precompile_headers(Engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engpch.h
)

engine_apply_warnings(Engine)
engine_apply_sanitizers(Engine)

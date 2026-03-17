set(ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLFramebuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLGPUTimerQuery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLRendererAPI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/OpenGL/OpenGLShader.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Physics/CollisionMath.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Physics/PhysicsDebugDraw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Physics/PhysicsWorld.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Reflection/AutoInspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Reflection/AutoSerializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Reflection/ComponentRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/EditorCamera.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Script/ScriptRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Terrain/TerrainMeshGenerator.cpp
)

add_library(Engine STATIC ${ENGINE_SOURCES})

target_compile_features(Engine PUBLIC cxx_std_20)

# MSVC: treat source and execution charset as UTF-8 so Chinese literals work correctly
if(MSVC)
    target_compile_options(Engine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

find_package(Threads REQUIRED)

# FFmpeg (vcpkg)
find_package(FFMPEG REQUIRED)

# OpenAL (vcpkg)
find_package(OpenAL CONFIG REQUIRED)

target_include_directories(Engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
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

engine_apply_warnings(Engine)
engine_apply_sanitizers(Engine)

if(ENGINE_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    target_sources(Engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaGLInteropContext.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaParticlePipeline.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaSPHPipeline.cu
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaParticlePipeline.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/CUDA/CudaSPHPipeline.cu
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    )
    target_compile_definitions(Engine PUBLIC ENGINE_ENABLE_CUDA=1)
    target_include_directories(Engine PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
    target_link_libraries(Engine PUBLIC CUDA::cudart)
    set_property(TARGET Engine PROPERTY CUDA_STANDARD 17)
    set_property(TARGET Engine PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)
    if(MSVC)
        target_compile_options(Engine PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--compiler-options=/utf-8,/Zc:preprocessor,/wd4505>)
    endif()
endif()




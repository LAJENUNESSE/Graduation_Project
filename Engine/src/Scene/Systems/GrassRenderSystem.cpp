#include "engpch.h"
#include "Scene/Systems/GrassRenderSystem.h"
#include "Asset/AssetManager.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/RendererCapabilities.h"
#include "Scene/Components.h"
#include "Scene/Runtime/RuntimeComponents.h"
#include "Scene/SceneEntityIndex.h"
#include "Scene/WorldTransformService.h"
#include "Terrain/TerrainMeshGenerator.h"

#include <algorithm>
#include <cstring>

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanBarrierUtil.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDescriptor.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanShader.h"
#include <vulkan/vulkan.h>
#endif

namespace Engine
{

#ifdef ENGINE_ENABLE_VULKAN
    // Vulkan compute 资源（Pimpl，对 .h 隐藏）
    struct GrassRenderSystem::VulkanResources
    {
        bool                           Initialized = false;
        VkDevice                       Device      = VK_NULL_HANDLE;
        Ref<VulkanDescriptorSetLayout> PlacementSetLayout;
        Ref<VulkanDescriptorSetLayout> RenderArgsSetLayout;
        Ref<VulkanDescriptorPool>      DescriptorPool;
        VulkanComputePipelineHandle    PlacementPipeline{};
        VulkanComputePipelineHandle    RenderArgsPipeline{};
    };
#else
    // 非 Vulkan 构建下也需要完整类型，让 unique_ptr 析构合法
    struct GrassRenderSystem::VulkanResources
    {
    };
#endif

    GrassRenderSystem::GrassRenderSystem() : m_VulkanResources(CreateScope<VulkanResources>()) {}
    GrassRenderSystem::~GrassRenderSystem() = default;

    namespace
    {
        bool ContainsToken(const char* str, const char* token)
        {
            return str && token && std::strstr(str, token) != nullptr;
        }

        // Counter buffer layout: 1 x uint32 = 4 bytes
        struct GrassCounterData
        {
            uint32_t grassCount;
        };

        // DrawArraysIndirectCommand: 4 x uint32 = 16 bytes
        struct IndirectDrawCommand
        {
            uint32_t count;
            uint32_t instanceCount;
            uint32_t first;
            uint32_t baseInstance;
        };

        // std140 UBO 镜像 — 与 grass_placement.glsl 中 GrassPlacementParams 块逐字节对应
        struct alignas(16) GrassPlacementParams
        {
            int32_t MaxGrass;
            int32_t HeightmapWidth;
            int32_t HeightmapHeight;
            float   TerrainSize;
            float   HeightScale;
            float   GrassHeight;
            float   GrassWidth;
            float   GrassWindStrength;
        };
        static_assert(sizeof(GrassPlacementParams) == 32, "GrassPlacementParams 必须与 std140 layout 一致");

        static constexpr uint32_t MAX_GRASS_BLADES = 500000;
        // 参数 UBO 绑定点 — 与 grass_placement.glsl 中 layout(std140, binding = 2) 对应
        static constexpr uint32_t GRASS_PARAMS_UBO_BINDING = 2;

        // ---- grass billboard 渲染 UBO（grass_billboard.glsl VULKAN 分支，set0）----
        static constexpr uint32_t GRASS_VS_UBO_BINDING = 1; // GrassVSUBO
        static constexpr uint32_t GRASS_FS_UBO_BINDING = 4; // GrassFSUBO

        // std140 UBO 镜像 — 与 GrassVSUBO 块逐字节对应：3×mat4 + float，数据 196B
        // （buffer 按 P-18 round 256）
        struct GrassVSUBOStd140
        {
            glm::mat4 ViewProjection;
            glm::mat4 Transform;
            glm::mat4 LightSpaceMatrix;
            float     Time;
            float     Pad[3];
        };
        static_assert(sizeof(GrassVSUBOStd140) == 208, "GrassVSUBOStd140 必须与 grass_billboard.glsl std140 布局一致");

        // std140：DirLight{vec3,vec3,float} 结构体步长 48B（vec3 各占 16B 槽）
        struct alignas(16) GrassFSDirLightStd140
        {
            glm::vec3 Direction;
            float     Pad0;
            glm::vec3 Color;
            float     Pad1;
            float     Intensity;
            float     Pad2[3];
        };
        static_assert(sizeof(GrassFSDirLightStd140) == 48, "GrassFSDirLightStd140 必须与 std140 DirLight 布局一致");

        // std140 UBO 镜像 — 与 GrassFSUBO 块逐字节对应：DirLight[2] + 3×int + 2×float，数据 128B
        struct GrassFSUBOStd140
        {
            GrassFSDirLightStd140 DirLights[2];
            int32_t               NumDirLights;
            int32_t               ShadowEnabled;
            int32_t               EntityID;
            float                 ShadowBias;
            float                 AmbientStrength;
            float                 Pad[3];
        };
        static_assert(sizeof(GrassFSUBOStd140) == 128, "GrassFSUBOStd140 必须与 grass_billboard.glsl std140 布局一致");
    } // namespace

    void GrassRenderSystem::Init()
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders)
        {
            ENGINE_WARN("[Grass] Compute shaders not supported, grass system disabled.");
            return;
        }

        m_PlacementShader  = Shader::Create("assets/shaders/grass_placement.glsl");
        m_RenderArgsShader = Shader::Create("assets/shaders/grass_render_args.glsl");
        m_BillboardShader  = Shader::Create("assets/shaders/grass_billboard.glsl");

        auto whiteHandle = AssetManager::Load<Texture2D>("builtin:white");
        m_WhiteTexture   = AssetManager::GetRef<Texture2D>(whiteHandle);

        m_EmptyVAO = VertexArray::Create();

        // Placement 参数 UBO（binding=2）— OpenGL/Vulkan 共用
        m_ParamsUBO = UniformBuffer::Create(sizeof(GrassPlacementParams), GRASS_PARAMS_UBO_BINDING);

        // VMware 兼容检测
        auto& caps = RendererCapabilities::Get();
        bool  vmwareDriver =
            ContainsToken(caps.VendorString.c_str(), "VMware") || ContainsToken(caps.RendererString.c_str(), "SVGA3D");

        const char* forceDirect    = std::getenv("ENGINE_GRASS_DIRECT_DRAW");
        bool        envForceDirect = forceDirect && forceDirect[0] == '1';

        // Vulkan 路径下 VulkanRendererAPI::DrawArraysIndirect 仍是 stub（Phase 8 接通主路径前不实装），
        // 同时 VulkanStorageBuffer::GetRendererID() 恒返回 0，indirect 路径必然不出图。
        // 强制走 DrawArraysInstanced（已实装），GrassCount 在 RebuildGrass Vulkan 分支同步阻塞读回。
        bool vulkanBackend = RendererAPI::GetAPI() == RendererAPI::API::Vulkan;

        m_UseIndirectDraw = !(vmwareDriver || envForceDirect || vulkanBackend);
        if (!m_UseIndirectDraw)
        {
            const char* reason = vulkanBackend  ? "Vulkan backend (DrawArraysIndirect not yet implemented)"
                                 : vmwareDriver ? "VMware compatibility mode"
                                                : "ENGINE_GRASS_DIRECT_DRAW=1";
            ENGINE_WARN("[Grass] Using direct instanced draw fallback ({}).", reason);
        }
    }

    void GrassRenderSystem::Shutdown()
    {
        // 清理异步回读资源
        for (auto& [eid, inst] : m_Instances)
        {
            if (inst.Readback)
                inst.Readback->Reset();
        }
        m_Instances.clear();
        m_Cache.clear();

#ifdef ENGINE_ENABLE_VULKAN
        DestroyVulkanComputeResources();
#endif
    }

    void GrassRenderSystem::UpdateGrassData(entt::registry& reg, float totalTime)
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders)
            return;

        auto view = reg.view<TransformComponent, TerrainComponent>();
        for (auto entity : view)
        {
            auto&    tc        = view.get<TerrainComponent>(entity);
            auto&    transform = view.get<TransformComponent>(entity);
            uint32_t eid       = static_cast<uint32_t>(entity);

            if (!tc.GrassEnabled)
            {
                // 草地禁用时清理
                auto it = m_Instances.find(eid);
                if (it != m_Instances.end())
                {
                    if (it->second.Readback)
                        it->second.Readback->Reset();
                    m_Instances.erase(it);
                }
                m_Cache.erase(eid);
                continue;
            }

            // 脏检测
            auto& cache = m_Cache[eid];
            bool  dirty = cache.GrassEnabled != tc.GrassEnabled || cache.GrassDensity != tc.GrassDensity ||
                         cache.GrassHeight != tc.GrassHeight || cache.GrassWidth != tc.GrassWidth ||
                         cache.GrassWindStrength != tc.GrassWindStrength || cache.TerrainSize != tc.TerrainSize ||
                         cache.HeightScale != tc.HeightScale || cache.HeightmapPath != tc.HeightmapPath ||
                         cache.GrassTexture != tc.GrassTexture;

            if (dirty)
            {
                TerrainMeshData* meshData = nullptr;
                if (reg.all_of<TerrainRuntimeComponent>(entity))
                    meshData = reg.get<TerrainRuntimeComponent>(entity).MeshData.get();
                RebuildGrass(eid, tc, transform, meshData);

                cache.GrassEnabled      = tc.GrassEnabled;
                cache.GrassDensity      = tc.GrassDensity;
                cache.GrassHeight       = tc.GrassHeight;
                cache.GrassWidth        = tc.GrassWidth;
                cache.GrassWindStrength = tc.GrassWindStrength;
                cache.TerrainSize       = tc.TerrainSize;
                cache.HeightScale       = tc.HeightScale;
                cache.HeightmapPath     = tc.HeightmapPath;
                cache.GrassTexture      = tc.GrassTexture;
            }
        }

        // ---- 每帧检查异步回读栅栏，更新 GrassCount（零等待） ----
        // 仅 OpenGL 路径需要：Vulkan 路径在 RebuildGrass 中已同步阻塞读取 GrassCount。
        if (!m_UseIndirectDraw && RendererAPI::GetAPI() != RendererAPI::API::Vulkan)
        {
            for (auto& [eid, inst] : m_Instances)
            {
                if (!inst.Readback || !inst.Readback->IsPending())
                    continue;

                if (inst.Readback->IsReady())
                {
                    GrassCounterData readback{0};
                    inst.Readback->GetData(&readback, sizeof(GrassCounterData));
                    inst.GrassCount = readback.grassCount;
                }
            }
        }
    }

    void GrassRenderSystem::RebuildGrass(uint32_t                  eid,
                                         TerrainComponent&         tc,
                                         const TransformComponent& transform,
                                         TerrainMeshData*          meshData)
    {
        if (!meshData || meshData->HeightData.empty())
        {
            ENGINE_WARN("[Grass] Entity {} has no terrain mesh data, skipping grass rebuild.", eid);
            return;
        }

        uint32_t maxGrass =
            static_cast<uint32_t>(std::min(static_cast<double>(tc.TerrainSize * tc.TerrainSize * tc.GrassDensity),
                                           static_cast<double>(MAX_GRASS_BLADES)));

        if (maxGrass == 0)
        {
            auto it = m_Instances.find(eid);
            if (it != m_Instances.end())
            {
                m_Instances.erase(it);
            }
            return;
        }

        auto& inst = m_Instances[eid];

        // 草叶 SSBO: 80 bytes/blade (5 x vec4)
        uint32_t grassBufSize = maxGrass * 80;
        inst.GrassBuffer      = ShaderStorageBuffer::CreateGPUOnly(grassBufSize, 0);

        // 高度图 SSBO
        uint32_t heightBufSize = static_cast<uint32_t>(meshData->HeightData.size() * sizeof(float));
        inst.HeightBuffer      = ShaderStorageBuffer::CreateGPUOnly(meshData->HeightData.data(), heightBufSize, 1);

        // Counter SSBO (dynamic, 需要 fallback 回读)
        GrassCounterData counter{0};
        inst.CounterBuffer = ShaderStorageBuffer::Create(&counter, sizeof(GrassCounterData), 3);

        // IndirectArgs SSBO
        IndirectDrawCommand cmd{6, 0, 0, 0};
        inst.IndirectArgs = ShaderStorageBuffer::CreateGPUOnly(&cmd, sizeof(IndirectDrawCommand), 4);

        // 准备共用参数（OpenGL/Vulkan 都通过 UBO 上传）
        GrassPlacementParams params{};
        params.MaxGrass          = static_cast<int32_t>(maxGrass);
        params.HeightmapWidth    = meshData->HeightmapWidth;
        params.HeightmapHeight   = meshData->HeightmapHeight;
        params.TerrainSize       = tc.TerrainSize;
        params.HeightScale       = tc.HeightScale;
        params.GrassHeight       = tc.GrassHeight;
        params.GrassWidth        = tc.GrassWidth;
        params.GrassWindStrength = tc.GrassWindStrength;
        m_ParamsUBO->SetData(&params, sizeof(GrassPlacementParams));

#ifdef ENGINE_ENABLE_VULKAN
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            // ---- Vulkan compute 路径 ----
            EnsureVulkanComputeResources();

            // 渲染侧 GrassVSUBO/GrassFSUBO（grass_billboard.glsl VULKAN 分支 set0
            // binding1/4），per-frame-in-flight 双份；数据按 P-18 round 256
            for (auto& ubo : inst.VSUbo)
                ubo = UniformBuffer::Create(256, GRASS_VS_UBO_BINDING);
            for (auto& ubo : inst.FSUbo)
                ubo = UniformBuffer::Create(256, GRASS_FS_UBO_BINDING);

            auto* ctx = VulkanContext::Get();
            ENGINE_CORE_RELEASE_ASSERT(ctx != nullptr, "[Grass][Vulkan] VulkanContext not initialized");
            VkDevice device = ctx->GetDevice();

            // 转回 VulkanStorageBuffer 拿到原生 VkBuffer 句柄
            auto vkGrass     = std::dynamic_pointer_cast<VulkanStorageBuffer>(inst.GrassBuffer);
            auto vkHeight    = std::dynamic_pointer_cast<VulkanStorageBuffer>(inst.HeightBuffer);
            auto vkCounter   = std::dynamic_pointer_cast<VulkanStorageBuffer>(inst.CounterBuffer);
            auto vkIndirect  = std::dynamic_pointer_cast<VulkanStorageBuffer>(inst.IndirectArgs);
            auto vkParamsUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_ParamsUBO);
            ENGINE_CORE_RELEASE_ASSERT(vkGrass && vkHeight && vkCounter && vkIndirect && vkParamsUBO,
                                       "[Grass][Vulkan] SSBO/UBO 转型失败");

            // 由于 RebuildGrass 是低频但累计的（每次场景重建/参数变更都会分配新 set），
            // 这里在 pool 耗尽时自动 Reset；Vulkan 端 EndSingleTimeCommands 走 vkQueueWaitIdle，
            // GPU 已 idle，旧 set 安全可回收。
            auto allocateOrReset = [&](VkDescriptorSetLayout layout) -> VkDescriptorSet
            {
                VkDescriptorSet set = m_VulkanResources->DescriptorPool->Allocate(layout);
                if (set == VK_NULL_HANDLE)
                {
                    ENGINE_CORE_WARN("[Grass][Vulkan] DescriptorPool 耗尽，Reset 后重试");
                    m_VulkanResources->DescriptorPool->Reset();
                    set = m_VulkanResources->DescriptorPool->Allocate(layout);
                }
                ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "[Grass][Vulkan] DescriptorPool 二次分配仍失败");
                return set;
            };

            // 1) Placement descriptor set — bindings: 0=grass, 1=height, 2=params(UBO), 3=counter
            VkDescriptorSet placementSet = allocateOrReset(m_VulkanResources->PlacementSetLayout->GetHandle());
            {
                VulkanDescriptorWriter w;
                w.WriteBuffer(0, vkGrass->GetBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.WriteBuffer(1, vkHeight->GetBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.WriteBuffer(GRASS_PARAMS_UBO_BINDING, vkParamsUBO->GetBuffer(), 0, sizeof(GrassPlacementParams),
                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                w.WriteBuffer(3, vkCounter->GetBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.UpdateSet(device, placementSet);
            }

            // 2) RenderArgs descriptor set — bindings: 3=counter(read), 4=indirect args(write)
            VkDescriptorSet renderArgsSet = allocateOrReset(m_VulkanResources->RenderArgsSetLayout->GetHandle());
            {
                VulkanDescriptorWriter w;
                w.WriteBuffer(3, vkCounter->GetBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.WriteBuffer(4, vkIndirect->GetBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                w.UpdateSet(device, renderArgsSet);
            }

            // 3) 录命令 & 提交 — 用 BeginSingleTimeCommands 拿同步 one-time cmd buffer
            VkCommandBuffer     rawCmd = ctx->BeginSingleTimeCommands();
            VulkanCommandBuffer cmdBuf(rawCmd);

            // Placement dispatch
            cmdBuf.BindComputePipeline(m_VulkanResources->PlacementPipeline.Pipeline);
            cmdBuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->PlacementPipeline.Layout, 0,
                                      {placementSet});
            uint32_t groups = (maxGrass + 63) / 64;
            cmdBuf.Dispatch(groups, 1, 1);

            // Barrier — placement 写 counter/grass，render_args 读 counter
            {
                auto m = ResolveBarrierBits(BarrierBit::ShaderStorage);
                cmdBuf.MemoryBarrier(m.SrcStage, m.DstStage, m.SrcAccess, m.DstAccess);
            }

            // RenderArgs dispatch（写 indirect args buffer）
            cmdBuf.BindComputePipeline(m_VulkanResources->RenderArgsPipeline.Pipeline);
            cmdBuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->RenderArgsPipeline.Layout, 0,
                                      {renderArgsSet});
            cmdBuf.Dispatch(1, 1, 1);

            // Barrier — render_args 写 indirect，后续 DrawArraysIndirect 通过 DRAW_INDIRECT stage 读
            {
                auto m = ResolveBarrierBits(BarrierBit::ShaderStorage | BarrierBit::Command);
                cmdBuf.MemoryBarrier(m.SrcStage, m.DstStage, m.SrcAccess, m.DstAccess);
            }

            ctx->EndSingleTimeCommands(rawCmd); // vkQueueWaitIdle 内部同步

            // 4) 同步阻塞读回 grassCount — 草地构建低频，可接受 GPU stall
            //    AsyncReadback 在 Vulkan 端暂未实装（Phase 7 Step 8 推迟）。
            //    Vulkan 路径下 Init() 强制 m_UseIndirectDraw=false，此分支必然命中。
            if (!m_UseIndirectDraw)
            {
                GrassCounterData readback{0};
                inst.CounterBuffer->GetData(&readback, sizeof(GrassCounterData));
                inst.GrassCount = readback.grassCount;
            }

            return;
        }
#endif

        // ---- OpenGL compute 路径（原有实现） ----
        // Dispatch placement compute
        inst.GrassBuffer->Bind(0);
        inst.HeightBuffer->Bind(1);
        inst.CounterBuffer->Bind(3);

        m_PlacementShader->Bind();
        // 注意：GLSL 使用 layout(std140, binding=2) UBO 后，散装 uniform 接口已废弃。
        // m_ParamsUBO 在上面已 SetData，OpenGL 通过 glBindBufferBase 自动绑到 binding 2。

        uint32_t groups = (maxGrass + 63) / 64;
        RenderCommand::DispatchCompute(groups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        // Dispatch render_args compute
        inst.IndirectArgs->Bind(4);
        m_RenderArgsShader->Bind();
        RenderCommand::DispatchCompute(1);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage | BarrierBit::Command);

        // ---- 异步回读 grassCount（避免 glGetBufferSubData 同步阻塞） ----
        if (!m_UseIndirectDraw)
        {
            if (!inst.Readback)
                inst.Readback = GPUAsyncReadback::Create(sizeof(GrassCounterData));

            inst.Readback->CopyFrom(inst.CounterBuffer, sizeof(GrassCounterData));
        }
    }

#ifdef ENGINE_ENABLE_VULKAN
    void GrassRenderSystem::EnsureVulkanComputeResources()
    {
        if (m_VulkanResources->Initialized)
            return;

        auto* ctx = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(ctx != nullptr, "[Grass][Vulkan] VulkanContext not initialized");
        m_VulkanResources->Device = ctx->GetDevice();
        VkDevice device           = m_VulkanResources->Device;

        // 反射拿到两个 shader 的 binding & shader module
        auto placementVk  = std::dynamic_pointer_cast<VulkanShader>(m_PlacementShader);
        auto renderArgsVk = std::dynamic_pointer_cast<VulkanShader>(m_RenderArgsShader);
        ENGINE_CORE_RELEASE_ASSERT(placementVk && renderArgsVk,
                                   "[Grass][Vulkan] grass shader 转型失败（非 VulkanShader）");

        VkShaderModule placementModule  = placementVk->GetOrCreateShaderModule(device, "compute");
        VkShaderModule renderArgsModule = renderArgsVk->GetOrCreateShaderModule(device, "compute");
        ENGINE_CORE_RELEASE_ASSERT(placementModule != VK_NULL_HANDLE && renderArgsModule != VK_NULL_HANDLE,
                                   "[Grass][Vulkan] 无法创建 grass compute shader module");

        // Descriptor set layouts — 由 SPIR-V 反射结果构建
        m_VulkanResources->PlacementSetLayout =
            VulkanDescriptorSetLayout::CreateFromReflection(device, placementVk->GetReflectedBindings(), 0);
        m_VulkanResources->RenderArgsSetLayout =
            VulkanDescriptorSetLayout::CreateFromReflection(device, renderArgsVk->GetReflectedBindings(), 0);

        // Compute pipelines（草地 shader 无 push constant，descriptor set 0 已覆盖全部输入）
        {
            VulkanComputePipelineDesc desc{};
            desc.ShaderModule = placementModule;
            desc.SetLayouts   = {m_VulkanResources->PlacementSetLayout->GetHandle()};
            // 如果未来 shader 添加 push constant，可在此处填充：
            // for (auto& pc : placementVk->GetReflectedPushConstants())
            //     desc.PushConstants.push_back({pc.Stages, pc.Offset, pc.Size});
            m_VulkanResources->PlacementPipeline = VulkanPipeline::CreateCompute(device, desc);
        }
        {
            VulkanComputePipelineDesc desc{};
            desc.ShaderModule                     = renderArgsModule;
            desc.SetLayouts                       = {m_VulkanResources->RenderArgsSetLayout->GetHandle()};
            m_VulkanResources->RenderArgsPipeline = VulkanPipeline::CreateCompute(device, desc);
        }

        // Descriptor pool — placement(4) + render_args(2)，按场景内地形数量预估上限
        // 每次 RebuildGrass 分配 2 个 set；草地构建低频，给 256 set 容量足够。
        m_VulkanResources->DescriptorPool = VulkanDescriptorPool::CreateDefaultComputePool(device, 256);

        m_VulkanResources->Initialized = true;
    }

    void GrassRenderSystem::DestroyVulkanComputeResources()
    {
        if (!m_VulkanResources || !m_VulkanResources->Initialized)
            return;

        VkDevice device = m_VulkanResources->Device;
        VulkanPipeline::DestroyCompute(device, m_VulkanResources->PlacementPipeline);
        VulkanPipeline::DestroyCompute(device, m_VulkanResources->RenderArgsPipeline);
        m_VulkanResources->DescriptorPool.reset();
        m_VulkanResources->PlacementSetLayout.reset();
        m_VulkanResources->RenderArgsSetLayout.reset();
        m_VulkanResources->Device      = VK_NULL_HANDLE;
        m_VulkanResources->Initialized = false;
    }
#endif // ENGINE_ENABLE_VULKAN

    void GrassRenderSystem::Render(entt::registry&         reg,
                                   const EditorCamera&     camera,
                                   const LightEnvironment& lights,
                                   const ShadowData&       shadow,
                                   const ShadowSettings&   shadowSettings,
                                   float                   totalTime,
                                   const SceneEntityIndex& index,
                                   WorldTransformCache*    cache,
                                   void*                   shadowDepthView)
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders)
            return;
        if (m_Instances.empty())
            return;

        Renderer::BeginScene(camera.GetViewProjection());

        m_BillboardShader->Bind();
        m_BillboardShader->SetMat4("u_ViewProjection", camera.GetViewProjection());
        m_BillboardShader->SetFloat("u_Time", totalTime);

        // 光照
        LightSystem::UploadToShader(m_BillboardShader, lights);

        // 阴影
        m_BillboardShader->SetMat4("u_LightSpaceMatrix", shadow.LightSpaceMatrix);
        bool shadowActive = shadowSettings.Enabled && shadow.HasValidShadowCaster;
        m_BillboardShader->SetInt("u_ShadowEnabled", shadowActive ? 1 : 0);
        m_BillboardShader->SetFloat("u_ShadowBias", shadowSettings.Bias);

        // Vulkan 场景绘制：GrassVSUBO/GrassFSUBO 帧级数据打包（D-13 模板）——散装
        // SetXxx 在 Vulkan 下只是 CPU 缓存且 dispatcher 不为草 shader 打包 Global/
        // Lights ring，矩阵/时间/光照必须按 std140 写入 UBO 走通用 UBO 槽。
        // per-entity 的 Transform/EntityID 在下方循环内补填后上传。
        const bool       vulkanBackend = RendererAPI::GetAPI() == RendererAPI::API::Vulkan;
        uint32_t         frameIndex    = 0;
        GrassVSUBOStd140 vsUbo{};
        GrassFSUBOStd140 fsUbo{};
        if (vulkanBackend)
        {
            vsUbo.ViewProjection   = camera.GetViewProjection();
            vsUbo.LightSpaceMatrix = shadow.LightSpaceMatrix;
            vsUbo.Time             = totalTime;

            const int numDirLights = static_cast<int>(std::min<size_t>(lights.DirLights.size(), 2));
            for (int i = 0; i < numDirLights; ++i)
            {
                fsUbo.DirLights[i].Direction = lights.DirLights[i].Direction;
                fsUbo.DirLights[i].Color     = lights.DirLights[i].Color;
                fsUbo.DirLights[i].Intensity = lights.DirLights[i].Intensity;
            }
            fsUbo.NumDirLights    = numDirLights;
            fsUbo.ShadowEnabled   = shadowActive ? 1 : 0;
            fsUbo.ShadowBias      = shadowSettings.Bias;
            fsUbo.AmbientStrength = lights.AmbientStrength;

            frameIndex = VulkanContext::Get()->GetCurrentFrameIndex();
        }

        // 阴影贴图槽：BindTextureUnit 在 Vulkan 是 no-op，须绑 depth attachment view
        // （CSM 模式下主 shadow map FBO 从未执行 renderpass，绑级联 0，与 GeometryPass 一致）
        if (vulkanBackend)
            RenderCommand::BindTextureView(1, shadowDepthView, nullptr);
        else
            RenderCommand::BindTextureUnit(1, shadow.ShadowMapTextureID);
        m_BillboardShader->SetInt("u_ShadowMap", 1);

        // 禁用面剔除（草叶两面可见）
        RenderCommand::SetCullFace(false);

        auto view = reg.view<TransformComponent, TerrainComponent>();
        for (auto entity : view)
        {
            auto& tc = view.get<TerrainComponent>(entity);
            if (!tc.GrassEnabled)
                continue;

            uint32_t eid = static_cast<uint32_t>(entity);
            auto     it  = m_Instances.find(eid);
            if (it == m_Instances.end())
                continue;

            auto& inst      = it->second;
            auto& transform = view.get<TransformComponent>(entity);

            const glm::mat4 worldTransform = WorldTransformService::ComputeWorldTransform(reg, entity, index, cache);
            m_BillboardShader->SetMat4("u_Transform", worldTransform);
            m_BillboardShader->SetInt("u_EntityID", static_cast<int>(eid));

            if (vulkanBackend)
            {
                // per-entity 数据进 UBO 后 Bind 录入通用 UBO 槽，本实体的
                // DrawArraysInstanced 录制时由 dispatcher 按反射 binding 写 descriptor
                vsUbo.Transform = worldTransform;
                fsUbo.EntityID  = static_cast<int32_t>(eid);
                inst.VSUbo[frameIndex]->SetData(&vsUbo, sizeof(vsUbo));
                inst.FSUbo[frameIndex]->SetData(&fsUbo, sizeof(fsUbo));
                inst.VSUbo[frameIndex]->Bind(GRASS_VS_UBO_BINDING);
                inst.FSUbo[frameIndex]->Bind(GRASS_FS_UBO_BINDING);
            }

            // 绑定草地纹理 (unit 2)
            Texture2D* grassTex = AssetManager::Get<Texture2D>(tc.GrassTexture);
            if (grassTex)
                grassTex->Bind(2);
            else
                m_WhiteTexture->Bind(2);
            m_BillboardShader->SetInt("u_GrassTexture", 2);

            // 绑定 SSBO
            inst.GrassBuffer->Bind(0);

            m_EmptyVAO->Bind();
            if (m_UseIndirectDraw)
                RenderCommand::DrawArraysIndirect(inst.IndirectArgs->GetRendererID());
            else
                RenderCommand::DrawArraysInstanced(6, inst.GrassCount);
        }

        // 恢复面剔除
        RenderCommand::SetCullFace(true);

        Renderer::EndScene();
    }

} // namespace Engine

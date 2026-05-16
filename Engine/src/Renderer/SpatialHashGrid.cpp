#include "engpch.h"
#include "Renderer/SpatialHashGrid.h"
#include "Core/Assert.h"
#include "Platform/Vulkan/VulkanBarrierUtil.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDescriptor.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"

#include <cmath>

namespace Engine
{

    // ============================================================
    // Vulkan 资源（Pimpl，仅 Vulkan 后端使用）
    // ============================================================
    struct SpatialHashGrid::VulkanResources
    {
        // 三个 compute pipeline + 各自的 descriptor set layout
        VulkanComputePipelineHandle    HashPipeline{};
        VulkanComputePipelineHandle    PrefixSumPipeline{};
        VulkanComputePipelineHandle    ScatterPipeline{};
        Ref<VulkanDescriptorSetLayout> HashLayout;
        Ref<VulkanDescriptorSetLayout> PrefixSumLayout;
        Ref<VulkanDescriptorSetLayout> ScatterLayout;

        // 共享的 descriptor pool（每帧 reset 复用）
        // hash 需要 1 个 set，prefix_sum 需要 ≤3 个 set（local/blocksum/propagate 三 pass），
        // scatter 需要 1 个 set，加保险共 8 个 set/帧
        Ref<VulkanDescriptorPool> Pool;

        VkDevice Device = VK_NULL_HANDLE;

        bool Initialized = false;
    };

    // ============================================================
    // 构造 / 析构
    // ============================================================
    SpatialHashGrid::SpatialHashGrid() = default;

    SpatialHashGrid::~SpatialHashGrid()
    {
        if (m_VulkanResources && m_VulkanResources->Initialized)
        {
            VkDevice device = m_VulkanResources->Device;
            VulkanPipeline::DestroyCompute(device, m_VulkanResources->HashPipeline);
            VulkanPipeline::DestroyCompute(device, m_VulkanResources->PrefixSumPipeline);
            VulkanPipeline::DestroyCompute(device, m_VulkanResources->ScatterPipeline);
        }
    }

    void SpatialHashGrid::SetExternalBuffers(Ref<ShaderStorageBuffer> particlePool,
                                             Ref<ShaderStorageBuffer> aliveList,
                                             Ref<ShaderStorageBuffer> pcisphPool)
    {
        m_ExternalParticlePool = std::move(particlePool);
        m_ExternalAliveList    = std::move(aliveList);
        m_ExternalPCISPHPool   = std::move(pcisphPool);
    }

    // ============================================================
    // Init —— SSBO + shader 创建，OpenGL/Vulkan 通用
    // ============================================================
    void SpatialHashGrid::Init(uint32_t maxParticles, uint32_t gridSize, float cellSize)
    {
        if (m_Initialized)
            return;

        m_MaxParticles = maxParticles;
        m_GridSize     = gridSize;
        m_CellSize     = cellSize;

        uint32_t totalCells = gridSize * gridSize * gridSize;
        uint32_t numBlocks  = (totalCells + 511) / 512;

        // Load shaders
        m_HashShader      = Shader::Create("assets/shaders/grid_hash.glsl");
        m_PrefixSumShader = Shader::Create("assets/shaders/grid_prefix_sum.glsl");
        m_ScatterShader   = Shader::Create("assets/shaders/grid_scatter.glsl");

        // Allocate SSBOs (GPU-only immutable: only compute shaders read/write after init)
        // Binding 1 & 4 are shared with DeadList & IndirectArgs respectively.
        // Grid passes temporarily reuse these slots; caller rebinds originals afterward.
        m_CellHash      = ShaderStorageBuffer::CreateGPUOnly(maxParticles * sizeof(uint32_t), 1);
        m_CellCount     = ShaderStorageBuffer::CreateGPUOnly(totalCells * sizeof(uint32_t), 6);
        m_CellStart     = ShaderStorageBuffer::CreateGPUOnly(totalCells * sizeof(uint32_t), 5);
        m_SortedIndices = ShaderStorageBuffer::CreateGPUOnly(maxParticles * sizeof(uint32_t), 7);
        m_BlockSums     = ShaderStorageBuffer::CreateGPUOnly(numBlocks * sizeof(uint32_t), 4);

        m_Initialized = true;
    }

    // ============================================================
    // Build —— 分派到具体后端
    // ============================================================
    void SpatialHashGrid::Build(uint32_t aliveCount, bool usePredictedPos)
    {
        if (!m_Initialized || aliveCount == 0)
            return;

        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
            BuildVulkan(aliveCount, usePredictedPos);
        else
            BuildOpenGL(aliveCount, usePredictedPos);
    }

    // ============================================================
    // OpenGL 路径（原实现，保持不变）
    // ============================================================
    void SpatialHashGrid::BuildOpenGL(uint32_t aliveCount, bool usePredictedPos)
    {
        uint32_t totalCells = GetTotalCells();

        // Bind all grid buffers
        m_CellHash->Bind(1);
        m_CellCount->Bind(6);
        m_CellStart->Bind(5);
        m_SortedIndices->Bind(7);
        m_BlockSums->Bind(4);

        // Clear CellCount to zero (GPU-side, no CPU allocation or upload)
        m_CellCount->ClearToZero();

        // ---- Pass A: Hash + Atomic Scatter Count ----
        m_HashShader->Bind();
        m_HashShader->SetInt("u_AliveCount", static_cast<int>(aliveCount));
        m_HashShader->SetInt("u_GridSize", static_cast<int>(m_GridSize));
        m_HashShader->SetFloat("u_CellSize", m_CellSize);
        m_HashShader->SetInt("u_UsePredictedPos", usePredictedPos ? 1 : 0);

        uint32_t hashGroups = (aliveCount + 255) / 256;
        RenderCommand::DispatchCompute(hashGroups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        // ---- Pass B: Prefix Sum (multi-block Blelloch) ----
        uint32_t blockSize = 512; // 2 * local_size_x (256)
        uint32_t numBlocks = (totalCells + blockSize - 1) / blockSize;

        m_PrefixSumShader->Bind();

        // Step 1: Block-local scan (CellCount → CellStart + BlockSums)
        m_PrefixSumShader->SetInt("u_Mode", 0);
        m_PrefixSumShader->SetInt("u_N", static_cast<int>(totalCells));
        RenderCommand::DispatchCompute(numBlocks);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        // Step 2: Scan block sums (single dispatch, ≤512 blocks fits in 1 workgroup)
        if (numBlocks > 1)
        {
            m_PrefixSumShader->SetInt("u_Mode", 1);
            m_PrefixSumShader->SetInt("u_N", static_cast<int>(numBlocks));
            RenderCommand::DispatchCompute(1);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

            // Step 3: Propagate block sums to CellStart
            m_PrefixSumShader->SetInt("u_Mode", 2);
            m_PrefixSumShader->SetInt("u_N", static_cast<int>(totalCells));
            RenderCommand::DispatchCompute(numBlocks);
            RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
        }

        // ---- Pass C: Scatter Write ----
        // 注意: scatter 使用 atomicAdd 修改 CellStart，scatter 后:
        //   CellStart[h] = original_start[h] + CellCount[h]
        // SPH 邻域查询时用: begin = CellStart[h] - CellCount[h], end = CellStart[h]
        m_ScatterShader->Bind();
        m_ScatterShader->SetInt("u_AliveCount", static_cast<int>(aliveCount));

        uint32_t scatterGroups = (aliveCount + 255) / 256;
        RenderCommand::DispatchCompute(scatterGroups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);
    }

    // ============================================================
    // Vulkan 资源懒初始化
    // ============================================================
    void SpatialHashGrid::InitVulkanResources()
    {
        if (m_VulkanResources && m_VulkanResources->Initialized)
            return;

        if (!m_VulkanResources)
            m_VulkanResources = CreateScope<VulkanResources>();

        auto* ctx = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(ctx, "SpatialHashGrid::InitVulkanResources: VulkanContext is null");
        VkDevice device           = ctx->GetDevice();
        m_VulkanResources->Device = device;

        auto buildPipeline = [&](const Ref<Shader>& shader, VulkanComputePipelineHandle& outHandle,
                                 Ref<VulkanDescriptorSetLayout>& outLayout)
        {
            auto vkShader = std::dynamic_pointer_cast<VulkanShader>(shader);
            ENGINE_CORE_RELEASE_ASSERT(vkShader, "SpatialHashGrid: expected VulkanShader instance on Vulkan backend");

            VkShaderModule module = vkShader->GetOrCreateShaderModule(device, "compute");
            ENGINE_CORE_RELEASE_ASSERT(module != VK_NULL_HANDLE,
                                       "SpatialHashGrid: compute shader module creation failed");

            // 反射 descriptor set 0 → DescriptorSetLayout
            outLayout = VulkanDescriptorSetLayout::CreateFromReflection(device, vkShader->GetReflectedBindings(), 0);

            // 反射 push constant range
            std::vector<VkPushConstantRange> pcRanges;
            for (const auto& pc : vkShader->GetReflectedPushConstants())
            {
                VkPushConstantRange r{};
                r.stageFlags = pc.Stages;
                r.offset     = pc.Offset;
                r.size       = pc.Size;
                pcRanges.push_back(r);
            }

            VulkanComputePipelineDesc desc{};
            desc.ShaderModule  = module;
            desc.SetLayouts    = {outLayout->GetHandle()};
            desc.PushConstants = pcRanges;

            outHandle = VulkanPipeline::CreateCompute(device, desc);
        };

        buildPipeline(m_HashShader, m_VulkanResources->HashPipeline, m_VulkanResources->HashLayout);
        buildPipeline(m_PrefixSumShader, m_VulkanResources->PrefixSumPipeline, m_VulkanResources->PrefixSumLayout);
        buildPipeline(m_ScatterShader, m_VulkanResources->ScatterPipeline, m_VulkanResources->ScatterLayout);

        // 共享 descriptor pool —— 每帧 Build() 末尾 Reset 复用
        m_VulkanResources->Pool = VulkanDescriptorPool::CreateDefaultComputePool(device, 16);

        m_VulkanResources->Initialized = true;
    }

    // ============================================================
    // Vulkan 路径：单次提交三阶段 compute
    // ============================================================
    void SpatialHashGrid::BuildVulkan(uint32_t aliveCount, bool usePredictedPos)
    {
        // 外部 buffer 注入检查（grid_hash 需要 ParticlePool + AliveList，PCISPH 模式下需要 PCISPHPool）
        ENGINE_CORE_RELEASE_ASSERT(m_ExternalParticlePool,
                                   "SpatialHashGrid::BuildVulkan: ParticlePool not set; call SetExternalBuffers first");
        ENGINE_CORE_RELEASE_ASSERT(m_ExternalAliveList,
                                   "SpatialHashGrid::BuildVulkan: AliveList not set; call SetExternalBuffers first");
        if (usePredictedPos)
        {
            ENGINE_CORE_RELEASE_ASSERT(m_ExternalPCISPHPool,
                                       "SpatialHashGrid::BuildVulkan: PCISPHPool required when usePredictedPos=true");
        }

        InitVulkanResources();

        auto*    ctx    = VulkanContext::Get();
        VkDevice device = ctx->GetDevice();

        // 把 cellCount 清零（host-visible mapped memcpy，vkQueueSubmit 隐式保证 host→device 可见）
        m_CellCount->ClearToZero();

        // BarrierBit::ShaderStorage → Vulkan stage/access 四元组（compute→compute 精确匹配）
        const VulkanBarrierMasks ssboMasks = ResolveBarrierBits(BarrierBit::ShaderStorage);

        VkCommandBuffer     cmdHandle = ctx->BeginSingleTimeCommands();
        VulkanCommandBuffer cmd(cmdHandle);

        m_VulkanResources->Pool->Reset();

        // SSBO VkBuffer 句柄（向下转型一次缓存）
        auto bufferOf = [](const Ref<ShaderStorageBuffer>& ssbo) -> VkBuffer
        {
            auto v = std::dynamic_pointer_cast<VulkanStorageBuffer>(ssbo);
            ENGINE_CORE_RELEASE_ASSERT(v, "SpatialHashGrid: SSBO is not a VulkanStorageBuffer");
            return v->GetBuffer();
        };

        VkBuffer particlePoolBuf = bufferOf(m_ExternalParticlePool);
        VkBuffer aliveListBuf    = bufferOf(m_ExternalAliveList);
        VkBuffer pcisphPoolBuf =
            m_ExternalPCISPHPool ? bufferOf(m_ExternalPCISPHPool) : particlePoolBuf; // 占位：未用时绑同一 buffer
        VkBuffer cellHashBuf      = bufferOf(m_CellHash);
        VkBuffer cellCountBuf     = bufferOf(m_CellCount);
        VkBuffer cellStartBuf     = bufferOf(m_CellStart);
        VkBuffer sortedIndicesBuf = bufferOf(m_SortedIndices);
        VkBuffer blockSumsBuf     = bufferOf(m_BlockSums);

        // -------- Pass A: grid_hash --------
        {
            VkDescriptorSet set = m_VulkanResources->Pool->Allocate(m_VulkanResources->HashLayout->GetHandle());
            ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "SpatialHashGrid: hash descriptor allocate failed");

            VulkanDescriptorWriter w;
            w.WriteBuffer(0, particlePoolBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(2, aliveListBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(6, cellCountBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(1, cellHashBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(9, pcisphPoolBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.UpdateSet(device, set);

            cmd.BindComputePipeline(m_VulkanResources->HashPipeline.Pipeline);
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->HashPipeline.Layout, 0, {set});

            struct HashPC
            {
                int32_t AliveCount;
                int32_t GridSize;
                float   CellSize;
                int32_t UsePredictedPos;
            } pc{static_cast<int32_t>(aliveCount), static_cast<int32_t>(m_GridSize), m_CellSize,
                 usePredictedPos ? 1 : 0};
            cmd.PushConstants(m_VulkanResources->HashPipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t hashGroups = (aliveCount + 255) / 256;
            cmd.Dispatch(hashGroups);

            // cellCount + cellHash 是后续 prefix_sum / scatter 的输入
            cmd.MemoryBarrier(ssboMasks.SrcStage, ssboMasks.DstStage, ssboMasks.SrcAccess, ssboMasks.DstAccess);
        }

        // -------- Pass B: grid_prefix_sum 三阶段 --------
        uint32_t totalCells = GetTotalCells();
        uint32_t blockSize  = 512;
        uint32_t numBlocks  = (totalCells + blockSize - 1) / blockSize;

        struct PrefixPC
        {
            int32_t Mode;
            int32_t N;
        };

        auto dispatchPrefix = [&](int32_t mode, int32_t n, uint32_t groups)
        {
            VkDescriptorSet set = m_VulkanResources->Pool->Allocate(m_VulkanResources->PrefixSumLayout->GetHandle());
            ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "SpatialHashGrid: prefix descriptor allocate failed");

            VulkanDescriptorWriter w;
            w.WriteBuffer(5, cellStartBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(6, cellCountBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(4, blockSumsBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.UpdateSet(device, set);

            cmd.BindComputePipeline(m_VulkanResources->PrefixSumPipeline.Pipeline);
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->PrefixSumPipeline.Layout, 0,
                                   {set});

            PrefixPC pc{mode, n};
            cmd.PushConstants(m_VulkanResources->PrefixSumPipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc),
                              &pc);

            cmd.Dispatch(groups);

            // 三阶段之间互相依赖 cellStart / blockSums 的写后读，必须精确 SSBO barrier
            cmd.MemoryBarrier(ssboMasks.SrcStage, ssboMasks.DstStage, ssboMasks.SrcAccess, ssboMasks.DstAccess);
        };

        // Step 1: local scan
        dispatchPrefix(0, static_cast<int32_t>(totalCells), numBlocks);

        if (numBlocks > 1)
        {
            // Step 2: scan block sums
            dispatchPrefix(1, static_cast<int32_t>(numBlocks), 1);
            // Step 3: propagate
            dispatchPrefix(2, static_cast<int32_t>(totalCells), numBlocks);
        }

        // -------- Pass C: grid_scatter --------
        {
            VkDescriptorSet set = m_VulkanResources->Pool->Allocate(m_VulkanResources->ScatterLayout->GetHandle());
            ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "SpatialHashGrid: scatter descriptor allocate failed");

            VulkanDescriptorWriter w;
            w.WriteBuffer(5, cellStartBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(7, sortedIndicesBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.WriteBuffer(1, cellHashBuf, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            w.UpdateSet(device, set);

            cmd.BindComputePipeline(m_VulkanResources->ScatterPipeline.Pipeline);
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_VulkanResources->ScatterPipeline.Layout, 0, {set});

            struct ScatterPC
            {
                int32_t AliveCount;
            } pc{static_cast<int32_t>(aliveCount)};
            cmd.PushConstants(m_VulkanResources->ScatterPipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc),
                              &pc);

            uint32_t scatterGroups = (aliveCount + 255) / 256;
            cmd.Dispatch(scatterGroups);

            // sortedIndices + cellStart 后续被 SPH 邻域查询消费，再加一次 SSBO barrier
            cmd.MemoryBarrier(ssboMasks.SrcStage, ssboMasks.DstStage, ssboMasks.SrcAccess, ssboMasks.DstAccess);
        }

        // EndSingleTimeCommands 同步等待 GPU 完成；本帧 Build() 返回时所有 grid 结果已就绪
        ctx->EndSingleTimeCommands(cmdHandle);
    }

} // namespace Engine

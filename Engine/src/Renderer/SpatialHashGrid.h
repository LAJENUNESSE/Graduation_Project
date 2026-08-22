#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/StorageBuffer.h"

#include <cstdint>

// Vulkan handle forward declaration（避免在通用头里 include vulkan.h）
typedef struct VkCommandBuffer_T* VkCommandBuffer;

namespace Engine
{

    class SpatialHashGrid
    {
    public:
        SpatialHashGrid();
        ~SpatialHashGrid();

        SpatialHashGrid(const SpatialHashGrid&)            = delete;
        SpatialHashGrid& operator=(const SpatialHashGrid&) = delete;

        void Init(uint32_t maxParticles, uint32_t gridSize, float cellSize);

        // 通用入口（OpenGL or Vulkan SingleTime）：低频调用方走这条（IBL / Grass / Rebuild）
        void Build(uint32_t aliveCount, bool usePredictedPos = false);

        // Vulkan 主帧 cmd 录制版本：每帧调用方（粒子 / 流体）录入自己的 frame cmd buffer，
        // 避免 BeginSingleTimeCommands 的 vkQueueWaitIdle stall（D-3）。
        // 同帧多次调用前，调用方必须每帧开头先调一次 ResetFrameResources() 复用 pool。
        void BuildVulkan(VkCommandBuffer cmd, uint32_t aliveCount, bool usePredictedPos = false);

        // 复位 Vulkan 路径下 descriptor pool（每帧首次 BuildVulkan 之前调用一次）。
        // OpenGL 路径下空操作。
        void ResetFrameResources();

        // Vulkan 路径专用：调用方注入外部 SSBO（粒子池 / 活粒子索引 / PCISPH 预测数据）。
        // OpenGL 路径完全忽略此设置；Vulkan 路径在 Build() 前必须设置 ParticlePool 与 AliveList，
        // 若 usePredictedPos=true 则同时需要 PCISPHPool。否则 Build() 会 release-assert。
        void SetExternalBuffers(Ref<ShaderStorageBuffer> particlePool,
                                Ref<ShaderStorageBuffer> aliveList,
                                Ref<ShaderStorageBuffer> pcisphPool = nullptr);

        Ref<ShaderStorageBuffer> GetCellStart() const { return m_CellStart; }
        Ref<ShaderStorageBuffer> GetCellCount() const { return m_CellCount; }
        Ref<ShaderStorageBuffer> GetSortedIndices() const { return m_SortedIndices; }
        Ref<ShaderStorageBuffer> GetCellHash() const { return m_CellHash; }

        uint32_t GetGridSize() const { return m_GridSize; }
        float    GetCellSize() const { return m_CellSize; }
        uint32_t GetTotalCells() const { return m_GridSize * m_GridSize * m_GridSize; }

    private:
        // Grid SSBOs
        Ref<ShaderStorageBuffer> m_CellHash;      // per-particle hash    (binding 1, shared with DeadList)
        Ref<ShaderStorageBuffer> m_CellCount;     // cell histogram       (binding 6)
        Ref<ShaderStorageBuffer> m_CellStart;     // prefix sum result    (binding 5)
        Ref<ShaderStorageBuffer> m_SortedIndices; // reordered indices    (binding 7)
        Ref<ShaderStorageBuffer> m_BlockSums;     // prefix sum 辅助      (binding 4, shared with IndirectArgs)

        // Shaders
        Ref<Shader> m_HashShader;
        Ref<Shader> m_PrefixSumShader;
        Ref<Shader> m_ScatterShader;

        // Vulkan 路径需要外部 buffer（调用方在 Build 前注入）
        Ref<ShaderStorageBuffer> m_ExternalParticlePool;
        Ref<ShaderStorageBuffer> m_ExternalAliveList;
        Ref<ShaderStorageBuffer> m_ExternalPCISPHPool;

        // Vulkan 路径专用资源（lazy 初始化，Pimpl 模式隐藏 Vulkan 类型依赖）
        struct VulkanResources;
        Scope<VulkanResources> m_VulkanResources;

        // 内部 dispatch 实现，按当前 RendererAPI 分派
        void BuildOpenGL(uint32_t aliveCount, bool usePredictedPos);
        void InitVulkanResources();

        uint32_t m_GridSize     = 64;   // 64^3 = 262144 cells
        float    m_CellSize     = 0.2f; // = smoothing radius（27-cell stencil 覆盖搜索半径 h）
        uint32_t m_MaxParticles = 0;

        bool m_Initialized = false;
    };

} // namespace Engine

#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/StorageBuffer.h"

#include <cstdint>

namespace Engine
{

    class SpatialHashGrid
    {
    public:
        void Init(uint32_t maxParticles, uint32_t gridSize, float cellSize);
        void Build(uint32_t aliveCount);

        Ref<ShaderStorageBuffer> GetCellStart() const { return m_CellStart; }
        Ref<ShaderStorageBuffer> GetCellCount() const { return m_CellCount; }
        Ref<ShaderStorageBuffer> GetSortedIndices() const { return m_SortedIndices; }
        Ref<ShaderStorageBuffer> GetCellHash() const { return m_CellHash; }

        uint32_t GetGridSize() const { return m_GridSize; }
        float GetCellSize() const { return m_CellSize; }
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

        uint32_t m_GridSize = 64; // 64^3 = 262144 cells
        float m_CellSize = 0.2f;  // = 2 * smoothing radius
        uint32_t m_MaxParticles = 0;

        bool m_Initialized = false;
    };

} // namespace Engine

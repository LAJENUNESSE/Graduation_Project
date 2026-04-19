#include "engpch.h"
#include "Renderer/SpatialHashGrid.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererAPI.h"

#include <cmath>

namespace Engine
{

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

    void SpatialHashGrid::Build(uint32_t aliveCount, bool usePredictedPos)
    {
        if (!m_Initialized || aliveCount == 0)
            return;

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

} // namespace Engine

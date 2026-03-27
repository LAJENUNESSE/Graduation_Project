#include "CudaParticleKernel.h"

#include <cuda_runtime.h>
#include <math_constants.h>

namespace InteropDemo
{
    namespace
    {
        __device__ inline uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8u) |
                   (static_cast<uint32_t>(b) << 16u) | (static_cast<uint32_t>(a) << 24u);
        }

        __device__ inline void WriteVertex(CudaParticleVertex& out,
                                           float               x,
                                           float               y,
                                           float               u,
                                           float               v,
                                           uint32_t            color)
        {
            out.Pos[0] = x;
            out.Pos[1] = y;
            out.UV[0]  = u;
            out.UV[1]  = v;
            out.Color  = color;
        }

        __device__ inline void WriteQuad(CudaParticleVertex* vertices,
                                         uint32_t            baseVertex,
                                         float               centerX,
                                         float               centerY,
                                         float               halfSize,
                                         uint32_t            color)
        {
            const float x0 = centerX - halfSize;
            const float y0 = centerY - halfSize;
            const float x1 = centerX + halfSize;
            const float y1 = centerY + halfSize;

            // Triangle 1: BL -> BR -> TR
            WriteVertex(vertices[baseVertex + 0], x0, y0, 0.0f, 1.0f, color);
            WriteVertex(vertices[baseVertex + 1], x1, y0, 1.0f, 1.0f, color);
            WriteVertex(vertices[baseVertex + 2], x1, y1, 1.0f, 0.0f, color);

            // Triangle 2: BL -> TR -> TL
            WriteVertex(vertices[baseVertex + 3], x0, y0, 0.0f, 1.0f, color);
            WriteVertex(vertices[baseVertex + 4], x1, y1, 1.0f, 0.0f, color);
            WriteVertex(vertices[baseVertex + 5], x0, y1, 0.0f, 0.0f, color);
        }

        __global__ void UpdateParticlesKernel(CudaParticleVertex* vertices,
                                              uint32_t            particleCount,
                                              float               timeSeconds,
                                              uint32_t            pcisphIterations)
        {
            const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
            if (index >= particleCount)
                return;

            const float t      = static_cast<float>(index) / static_cast<float>(particleCount);
            const float spins  = 8.0f + static_cast<float>(pcisphIterations) * 0.25f;
            const float angle  = t * CUDART_PI_F * spins + timeSeconds * (0.65f + 0.05f * pcisphIterations);
            const float radius = 0.06f + 0.88f * t;

            const float centerX = cosf(angle) * radius;
            const float centerY = sinf(angle) * radius;
            const float sizeWave = 0.5f + 0.5f * sinf(timeSeconds * 3.2f + t * 20.0f);
            const float halfSize = 0.0025f + 0.0035f * sizeWave;

            const float wave = 0.5f + 0.5f * sinf(timeSeconds * 2.4f + t * 10.0f);
            const uint8_t r  = static_cast<uint8_t>(90.0f + 165.0f * wave);
            const uint8_t g  = static_cast<uint8_t>(120.0f + 100.0f * (1.0f - wave));
            const uint8_t b  = static_cast<uint8_t>(30.0f + 180.0f * t);
            const uint32_t color = PackRGBA(r, g, b, 255u);

            const uint32_t baseVertex = index * kVerticesPerParticle;
            WriteQuad(vertices, baseVertex, centerX, centerY, halfSize, color);
        }
    } // namespace

    void LaunchUpdateParticlesKernel(CudaParticleVertex* vertices,
                                     uint32_t            particleCount,
                                     float               timeSeconds,
                                     uint32_t            pcisphIterations,
                                     void*               stream)
    {
        if (!vertices || particleCount == 0)
            return;

        constexpr uint32_t kBlockSize = 256u;
        const uint32_t     blocks     = (particleCount + kBlockSize - 1u) / kBlockSize;
        UpdateParticlesKernel<<<blocks, kBlockSize, 0, static_cast<cudaStream_t>(stream)>>>(
            vertices, particleCount, timeSeconds, pcisphIterations);
    }

} // namespace InteropDemo
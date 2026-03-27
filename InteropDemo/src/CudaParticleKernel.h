#pragma once

#include <cstdint>

namespace InteropDemo
{
    constexpr uint32_t kVerticesPerParticle = 6u;
    struct CudaParticleVertex
    {
        float    Pos[2];
        float    UV[2];
        uint32_t Color;
    };

    // stream is a cudaStream_t cast to void* from C++ side.
    void LaunchUpdateParticlesKernel(CudaParticleVertex* vertices,
                                     uint32_t            particleCount,
                                     float               timeSeconds,
                                     uint32_t            pcisphIterations,
                                     void*               stream);

} // namespace InteropDemo

#include "Platform/CUDA/VulkanExternalSmokeKernel.h"

#include <cuda_runtime.h>

namespace Engine
{
    namespace CudaInterop
    {
        namespace
        {
            __global__ void VulkanExternalSmokeKernel(uint32_t* words, uint32_t wordCount, uint64_t frameIndex)
            {
                const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
                if (i >= wordCount)
                    return;

                const uint32_t frameLo  = static_cast<uint32_t>(frameIndex & 0xFFFFFFFFull);
                const uint32_t frameHi  = static_cast<uint32_t>((frameIndex >> 32ull) & 0xFFFFFFFFull);
                // Use fixed hash/mixing constants (golden-ratio / MurmurHash-style) to generate
                // deterministic but visually noisy data from the index and frame counter.
                const uint32_t mixedA   = 0x9E3779B9u * (i + 1u);
                const uint32_t mixedB   = 0x85EBCA6Bu * (frameLo ^ frameHi);
                words[i] = mixedA ^ mixedB;
            }
        } // namespace

        void LaunchVulkanExternalSmokeKernel(void*    mappedBuffer,
                                             size_t   bufferBytes,
                                             uint64_t frameIndex,
                                             void*    stream)
        {
            if (!mappedBuffer || bufferBytes < sizeof(uint32_t))
                return;

            const uint32_t wordCount = static_cast<uint32_t>(bufferBytes / sizeof(uint32_t));
            constexpr uint32_t blockSize = 256u;
            const uint32_t     blocks    = (wordCount + blockSize - 1u) / blockSize;
            VulkanExternalSmokeKernel<<<blocks, blockSize, 0, static_cast<cudaStream_t>(stream)>>>(
                static_cast<uint32_t*>(mappedBuffer), wordCount, frameIndex);
        }

    } // namespace CudaInterop
} // namespace Engine

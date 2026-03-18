// CUDA 错误处理工具：粘性错误检测 + 全局中毒机制。
// 一旦任何 CUDA API 失败，整个 CUDA 上下文标记为"中毒"，
// 后续所有 CUDA 调用静默跳过，避免级联错误日志刷屏。
//
// 注意：此头文件同时被 .cu（nvcc）和 .cpp（MSVC）包含，
// 因此不能引入 spdlog（nvcc 无法编译 fmt 的某些字面量）。
// 使用 fprintf(stderr, ...) 输出错误信息。
#pragma once

#include "CudaPoisonState.h"

#include <cuda_runtime.h>

namespace Engine
{
    namespace CudaInterop
    {

        // ---- CUDA_CHECK: 包装 cudaMalloc / cudaMemcpy 等 Runtime API ----
        // 返回 true 表示成功，false 表示失败（同时中毒）

#define CUDA_CHECK(call)                                                                                               \
    (                                                                                                                  \
        [&]() -> bool                                                                                                  \
        {                                                                                                              \
            if (::Engine::CudaInterop::IsCudaPoisoned())                                                               \
                return false;                                                                                          \
            cudaError_t err__ = (call);                                                                                \
            if (err__ != cudaSuccess)                                                                                  \
            {                                                                                                          \
                fprintf(stderr, "[CUDA] %s failed: %s (%s:%d)\n", #call, cudaGetErrorString(err__), __FILE__,          \
                        __LINE__);                                                                                     \
                ::Engine::CudaInterop::PoisonCuda(cudaGetErrorString(err__));                                          \
                return false;                                                                                          \
            }                                                                                                          \
            return true;                                                                                               \
        }())

        // ---- CUDA_CHECK_KERNEL: 在 <<<>>> 之后调用 cudaGetLastError() ----
        // cudaGetLastError 是零开销调用（不触发同步），用于检测内核启动参数错误

#define CUDA_CHECK_KERNEL(name)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        cudaError_t err__ = cudaGetLastError();                                                                        \
        if (err__ != cudaSuccess)                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "[CUDA] Kernel '%s' launch failed: %s (%s:%d)\n", name, cudaGetErrorString(err__),         \
                    __FILE__, __LINE__);                                                                               \
            ::Engine::CudaInterop::PoisonCuda(cudaGetErrorString(err__));                                              \
        }                                                                                                              \
    } while (0)

    } // namespace CudaInterop
} // namespace Engine

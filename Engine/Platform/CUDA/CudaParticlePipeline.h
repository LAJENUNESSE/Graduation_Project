#pragma once

// 非 SPH 粒子管线的 CUDA 内核启动包装器。
// 纯 C++ 头——不暴露 CUDA 类型。stream 是 void* (cudaStream_t)。
// 缓冲区指针来自 CudaGLInteropContext::GetMappedPointer()。

#include <cstdint>

namespace Engine
{
    namespace CudaInterop
    {

        // ---- 发射参数（镜像 particle_emit.glsl uniform）----
        struct EmitParams
        {
            float    emitterPos[3];
            float    emitDirection[3];
            float    emitAngle; // 弧度
            float    lifeMin, lifeMax;
            float    speedMin, speedMax;
            float    sizeStart, sizeEnd;
            float    startColor[4];
            float    endColor[4];
            float    time; // 用于 RNG 种子
            uint32_t maxParticles;
            uint32_t emitCount;
        };

        // ---- 模拟参数（镜像 particle_simulate.glsl uniform）----
        struct SimulateParams
        {
            float    deltaTime;
            float    gravity[3];
            float    damping;
            uint32_t maxParticles;
        };

        // 启动发射内核。blockDim=64（与 GLSL local_size_x 匹配）。
        void LaunchEmit(void* particles, void* deadList, void* counter, const EmitParams& params, void* stream);

        // 启动模拟内核。blockDim=256。
        void LaunchSimulate(void*                 particles,
                            void*                 deadList,
                            void*                 aliveList,
                            void*                 counter,
                            const SimulateParams& params,
                            void*                 stream);

        // 启动渲染参数内核。1 个线程：aliveCount -> instanceCount。
        void LaunchRenderArgs(void* counter, void* indirectArgs, void* stream);

        // ---- CUDA event 计时辅助（void* = cudaEvent_t）----
        void* CreateCudaEvent();
        void  DestroyCudaEvent(void* event);
        void  RecordCudaEvent(void* event, void* stream);
        // 查询两个事件之间的 GPU 耗时（毫秒）。调用前必须确保 stop 事件已完成。
        float CudaEventElapsedMs(void* start, void* stop);

    } // namespace CudaInterop
} // namespace Engine

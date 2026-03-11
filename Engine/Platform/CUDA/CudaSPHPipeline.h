#pragma once

// SPH/PCISPH 流体管线的 CUDA 内核启动包装器。
// 纯 C++ 头——不暴露 CUDA 类型。stream 是 void* (cudaStream_t)。
// 缓冲区指针来自 CudaGLInteropContext::GetMappedPointer()。

#include <cstdint>

namespace Engine
{
    namespace CudaInterop
    {

        // SPH 物理参数（对应 GLSL uniform）
        struct SPHParams
        {
            float smoothingRadius, poly6Coeff, spikyCoeff;
            float particleMass, restDensity, gasConstant;
            float viscosity, surfaceTension;
            float deltaTime;
            int gridSize;
            float cellSize;
            int aliveCount;
            float gravity[3];
            float warmupTime; // SPH warm-up 时间（秒），新粒子逐步受 SPH 约束
        };

        // PCISPH 迭代额外参数
        struct PCISPHIterParams
        {
            float pcisphDelta;
            float boundaryStiffness, boundaryDamping;
            int rigidBodyCount;
        };

        // 流体积分参数
        struct SPHSimulateParams
        {
            float deltaTime, damping;
            float gravity[3];
            float boundaryMin[3], boundaryMax[3];
            int useBoundary, particleCount;
        };

        // 流体发射参数
        struct SPHEmitParams
        {
            float emitterPos[3], emitExtents[3];
            float initialVelocity[3];
            float time;
            int particleCount;
        };

        // ---- CUDA-native 网格/PCISPH 缓冲区生命周期 ----
        // 返回不透明上下文指针（持有 d_cellHash/Count/Start/SortedIndices/pcisph/rigidBody）
        void* CreateSPHContext(uint32_t maxParticles, uint32_t gridSize, uint32_t maxRigidBodies);
        void DestroySPHContext(void* ctx);

        // 每帧：从 CPU 上传刚体数据
        void SPHUploadRigidBodies(void* ctx, const void* cpuData, uint32_t count);

        // ---- Grid Build（3 步合 1 调用，内部用 CUB prefix sum）----
        void LaunchSPHGridBuild(void* ctx, void* particles, uint32_t aliveCount, int gridSize, float cellSize,
                                void* stream);

        // ---- WCSPH 路径 ----
        void LaunchSPHDensity(void* ctx, void* particles, const SPHParams& p, void* stream);
        void LaunchSPHForce(void* ctx, void* particles, const SPHParams& p, const PCISPHIterParams& ip, void* stream);

        // ---- PCISPH 路径 ----
        void LaunchPCISPHInit(void* ctx, void* particles, const SPHParams& p, void* stream);
        void LaunchPCISPHPredict(void* ctx, void* particles, float dt, int aliveCount, void* stream);
        void LaunchPCISPHDensity(void* ctx, void* particles, const SPHParams& p, const PCISPHIterParams& ip,
                                 void* stream);
        void LaunchPCISPHForce(void* ctx, void* particles, const SPHParams& p, const PCISPHIterParams& ip,
                               void* stream);
        void LaunchPCISPHApply(void* ctx, void* particles, int aliveCount, void* stream);

        // ---- 积分 + 边界 ----
        void LaunchSPHSimulate(void* particles, const SPHSimulateParams& p, void* stream);

        // ---- 初始发射（填充全部粒子池）----
        void LaunchSPHEmit(void* particles, const SPHEmitParams& p, void* stream);

    } // namespace CudaInterop
} // namespace Engine

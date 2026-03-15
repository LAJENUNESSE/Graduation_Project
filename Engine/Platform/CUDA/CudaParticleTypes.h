#pragma once

// CUDA 和 OpenGL 共享的缓冲区布局唯一真实来源。
// 由 MSVC (.cpp) 和 NVCC (.cu) 编译单元包含。
//
// 规则：
//   - 不使用 glm、float4、供应商特定向量类型
//   - 16 字节字段上显式标注 alignas(16)
//   - 为每个结构体添加 sizeof + offsetof 断言

#include <cstddef> // offsetof
#include <cstdint>

namespace Engine
{
    namespace CudaInterop
    {

        // 16 字节对齐 4 浮点向量——与内存中的 GLSL vec4 / CUDA float4 匹配
        struct alignas(16) Vec4
        {
            float x, y, z, w;
        };

        // 必须与 GLSL GPUParticle (std430, binding 0) 匹配：5 * vec4 = 80 字节
        struct GPUParticle
        {
            Vec4 posAndLife;    // xyz=位置，w=剩余寿命
            Vec4 velAndMaxLife; // xyz=速度，w=最大寿命
            Vec4 startColor;
            Vec4 endColor;
            Vec4 params; // x=初始大小，y=最终大小，z=密度(SPH)，w=压力(SPH)
        };

        static_assert(sizeof(GPUParticle) == 80, "GPUParticle must be 80 bytes");
        static_assert(offsetof(GPUParticle, posAndLife) == 0, "");
        static_assert(offsetof(GPUParticle, velAndMaxLife) == 16, "");
        static_assert(offsetof(GPUParticle, startColor) == 32, "");
        static_assert(offsetof(GPUParticle, endColor) == 48, "");
        static_assert(offsetof(GPUParticle, params) == 64, "");

        // 计数器缓冲区 (std430, binding 3)：4 * uint32 = 16 字节
        struct CounterData
        {
            uint32_t deadCount;
            uint32_t aliveCount;
            uint32_t emitCount;
            uint32_t pad;
        };

        static_assert(sizeof(CounterData) == 16, "CounterData must be 16 bytes");
        static_assert(offsetof(CounterData, deadCount) == 0, "");
        static_assert(offsetof(CounterData, aliveCount) == 4, "");
        static_assert(offsetof(CounterData, emitCount) == 8, "");

        // DrawArraysIndirectCommand (std430, binding 4)：4 * uint32 = 16 字节
        struct IndirectDrawCommand
        {
            uint32_t vertexCount;
            uint32_t instanceCount;
            uint32_t firstVertex;
            uint32_t baseInstance;
        };

        static_assert(sizeof(IndirectDrawCommand) == 16, "IndirectDrawCommand must be 16 bytes");
        static_assert(offsetof(IndirectDrawCommand, vertexCount) == 0, "");
        static_assert(offsetof(IndirectDrawCommand, instanceCount) == 4, "");
        static_assert(offsetof(IndirectDrawCommand, firstVertex) == 8, "");
        static_assert(offsetof(IndirectDrawCommand, baseInstance) == 12, "");

        // PCISPH 辅助数据（binding 1 during PCISPH），3 × vec4 = 48 bytes
        struct alignas(16) PCISPHData
        {
            Vec4 predictedPosAndPressure; // xyz=x*, w=P
            Vec4 predictedVelAndDensity;  // xyz=v*, w=ρ*
            Vec4 nonPressureAccel;        // xyz=a_np, w=unused
        };
        static_assert(sizeof(PCISPHData) == 48, "PCISPHData must be 48 bytes");

        // 刚体边界数据（binding 3 during SPH），7 × vec4 = 112 bytes
        struct alignas(16) RigidBodyData
        {
            Vec4 posAndType; // xyz=center, w=0(box)/1(sphere)
            Vec4 rotCol0;
            Vec4 rotCol1;
            Vec4 rotCol2;
            Vec4 halfExtents;
            Vec4 linearVel;
            Vec4 angularVel;
        };
        static_assert(sizeof(RigidBodyData) == 112, "RigidBodyData must be 112 bytes");

    } // namespace CudaInterop
} // namespace Engine

// CudaParticleTypes.h 的数据布局校验——确保 C++ 端的 struct 与 GLSL 端
// std430 layout 以及 CUDA 内核中的字段访问对齐。static_assert 已经在 header
// 内联，此测试在 EngineTestCore 编译期之外再加一道运行期检查，便于在不开
// ENGINE_ENABLE_CUDA 的情况下捕捉布局漂移。

#include <gtest/gtest.h>

#include "Platform/CUDA/CudaParticleTypes.h"

#include <cstddef>

using namespace Engine::CudaInterop;

// ---- GPUParticle (5 × vec4 = 80B) ----

TEST(CudaParticleTypes, GPUParticleSize)
{
    EXPECT_EQ(sizeof(GPUParticle), 80u);
}

TEST(CudaParticleTypes, GPUParticleFieldOffsets)
{
    EXPECT_EQ(offsetof(GPUParticle, posAndLife), 0u);
    EXPECT_EQ(offsetof(GPUParticle, velAndMaxLife), 16u);
    EXPECT_EQ(offsetof(GPUParticle, startColor), 32u);
    EXPECT_EQ(offsetof(GPUParticle, endColor), 48u);
    EXPECT_EQ(offsetof(GPUParticle, params), 64u);
}

TEST(CudaParticleTypes, GPUParticleAlignment)
{
    // alignas(16)——struct 起始地址和对齐要求 16 字节
    EXPECT_EQ(alignof(GPUParticle), 16u);
}

// ---- CounterData (4 × uint32 = 16B) ----

TEST(CudaParticleTypes, CounterDataSize)
{
    EXPECT_EQ(sizeof(CounterData), 16u);
}

TEST(CudaParticleTypes, CounterDataFieldOffsets)
{
    EXPECT_EQ(offsetof(CounterData, deadCount), 0u);
    EXPECT_EQ(offsetof(CounterData, aliveCount), 4u);
    EXPECT_EQ(offsetof(CounterData, emitCount), 8u);
}

// ---- IndirectDrawCommand (4 × uint32 = 16B) ----

TEST(CudaParticleTypes, IndirectDrawCommandSize)
{
    EXPECT_EQ(sizeof(IndirectDrawCommand), 16u);
}

TEST(CudaParticleTypes, IndirectDrawCommandFieldOffsets)
{
    EXPECT_EQ(offsetof(IndirectDrawCommand, vertexCount), 0u);
    EXPECT_EQ(offsetof(IndirectDrawCommand, instanceCount), 4u);
    EXPECT_EQ(offsetof(IndirectDrawCommand, firstVertex), 8u);
    EXPECT_EQ(offsetof(IndirectDrawCommand, baseInstance), 12u);
}

// ---- PCISPHData (3 × vec4 = 48B) ----

TEST(CudaParticleTypes, PCISPHDataSize)
{
    EXPECT_EQ(sizeof(PCISPHData), 48u);
}

TEST(CudaParticleTypes, PCISPHDataFieldOffsets)
{
    EXPECT_EQ(offsetof(PCISPHData, predictedPosAndPressure), 0u);
    EXPECT_EQ(offsetof(PCISPHData, predictedVelAndDensity), 16u);
    EXPECT_EQ(offsetof(PCISPHData, nonPressureAccel), 32u);
}

// ---- RigidBodyData (7 × vec4 = 112B) ----

TEST(CudaParticleTypes, RigidBodyDataSize)
{
    EXPECT_EQ(sizeof(RigidBodyData), 112u);
}

TEST(CudaParticleTypes, RigidBodyDataFieldOffsets)
{
    EXPECT_EQ(offsetof(RigidBodyData, posAndType), 0u);
    EXPECT_EQ(offsetof(RigidBodyData, rotCol0), 16u);
    EXPECT_EQ(offsetof(RigidBodyData, rotCol1), 32u);
    EXPECT_EQ(offsetof(RigidBodyData, rotCol2), 48u);
    EXPECT_EQ(offsetof(RigidBodyData, halfExtents), 64u);
    EXPECT_EQ(offsetof(RigidBodyData, linearVel), 80u);
    EXPECT_EQ(offsetof(RigidBodyData, angularVel), 96u);
}

// ---- Vec4 (16B, 与 GLSL vec4 / CUDA float4 内存匹配) ----

TEST(CudaParticleTypes, Vec4SizeAndAlignment)
{
    EXPECT_EQ(sizeof(Vec4), 16u);
    EXPECT_EQ(alignof(Vec4), 16u);
}
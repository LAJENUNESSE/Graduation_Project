#include <gtest/gtest.h>
#include "CUDA/CudaParticleTypes.h"

using namespace Engine::CudaInterop;

TEST(CudaParticleTypes, Vec4Alignment)
{
    EXPECT_EQ(alignof(Vec4), 16u);
    EXPECT_EQ(sizeof(Vec4), 16u);
}

TEST(CudaParticleTypes, GPUParticleLayout)
{
    EXPECT_EQ(sizeof(GPUParticle), 80u);
    EXPECT_EQ(offsetof(GPUParticle, posAndLife), 0u);
    EXPECT_EQ(offsetof(GPUParticle, velAndMaxLife), 16u);
    EXPECT_EQ(offsetof(GPUParticle, startColor), 32u);
    EXPECT_EQ(offsetof(GPUParticle, endColor), 48u);
    EXPECT_EQ(offsetof(GPUParticle, params), 64u);
}

TEST(CudaParticleTypes, CounterDataLayout)
{
    EXPECT_EQ(sizeof(CounterData), 16u);
    EXPECT_EQ(offsetof(CounterData, deadCount), 0u);
    EXPECT_EQ(offsetof(CounterData, aliveCount), 4u);
    EXPECT_EQ(offsetof(CounterData, emitCount), 8u);
}

TEST(CudaParticleTypes, IndirectDrawCommandLayout)
{
    EXPECT_EQ(sizeof(IndirectDrawCommand), 16u);
    EXPECT_EQ(offsetof(IndirectDrawCommand, vertexCount), 0u);
    EXPECT_EQ(offsetof(IndirectDrawCommand, instanceCount), 4u);
    EXPECT_EQ(offsetof(IndirectDrawCommand, firstVertex), 8u);
    EXPECT_EQ(offsetof(IndirectDrawCommand, baseInstance), 12u);
}

TEST(CudaParticleTypes, PCISPHDataLayout)
{
    EXPECT_EQ(sizeof(PCISPHData), 48u);
    EXPECT_EQ(alignof(PCISPHData), 16u);
    EXPECT_EQ(offsetof(PCISPHData, predictedPosAndPressure), 0u);
    EXPECT_EQ(offsetof(PCISPHData, predictedVelAndDensity), 16u);
    EXPECT_EQ(offsetof(PCISPHData, nonPressureAccel), 32u);
}

TEST(CudaParticleTypes, RigidBodyDataLayout)
{
    EXPECT_EQ(sizeof(RigidBodyData), 112u);
    EXPECT_EQ(alignof(RigidBodyData), 16u);
    EXPECT_EQ(offsetof(RigidBodyData, posAndType), 0u);
    EXPECT_EQ(offsetof(RigidBodyData, rotCol0), 16u);
    EXPECT_EQ(offsetof(RigidBodyData, rotCol1), 32u);
    EXPECT_EQ(offsetof(RigidBodyData, rotCol2), 48u);
    EXPECT_EQ(offsetof(RigidBodyData, halfExtents), 64u);
    EXPECT_EQ(offsetof(RigidBodyData, linearVel), 80u);
    EXPECT_EQ(offsetof(RigidBodyData, angularVel), 96u);
}

TEST(CudaParticleTypes, Vec4MemberAccess)
{
    Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
    EXPECT_FLOAT_EQ(v.w, 4.0f);
}

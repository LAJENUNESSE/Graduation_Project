#type compute
#version 430 core

// PCISPH 应用: v* → particle.vel

layout(local_size_x = 256) in;

struct GPUParticle
{
    vec4 posAndLife;       // xyz=position, w=remainingLife
    vec4 velAndMaxLife;    // xyz=velocity, w=maxLife
    vec4 startColor;
    vec4 endColor;
    vec4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
};

struct PCISPHData
{
    vec4 predictedPosAndPressure;  // xyz=x*, w=P
    vec4 predictedVelAndDensity;   // xyz=v*, w=ρ*
    vec4 nonPressureAccel;         // xyz=a_np, w=unused
};

#ifdef VULKAN
// Vulkan 路径：所有 SSBO set=0；统一 SPHParams UBO (binding=12) + push constant
layout(std430, set = 0, binding = 0) buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, set = 0, binding = 1) readonly buffer PCISPHBuffer  { PCISPHData  pcisphData[]; };
layout(std430, set = 0, binding = 2) readonly buffer AliveList     { uint aliveIndices[];      };

layout(std140, set = 0, binding = 12) uniform SPHParams
{
    vec4 u_GravityAndSmoothingRadius;
    vec4 u_MassDensityGasViscosity;
    vec4 u_GridParams;
    vec4 u_BoundaryParams;
    vec4 u_SDFCounts;
};

layout(push_constant) uniform PushConstants
{
    uint  u_AliveCountPC;
    float u_DeltaTimePC;
    uint  u_UsePredictedPosPC;
} pc;

#define u_AliveCount         int(pc.u_AliveCountPC)
#define u_DeltaTime          pc.u_DeltaTimePC
#define u_UsePredictedPos    int(pc.u_UsePredictedPosPC)
#else
layout(std430, binding = 0) buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, binding = 1) readonly buffer PCISPHBuffer  { PCISPHData  pcisphData[]; };
layout(std430, binding = 2) readonly buffer AliveList     { uint aliveIndices[];      };

uniform int u_AliveCount;
#endif

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_AliveCount))
        return;

    uint myParticleIdx = aliveIndices[gid];

    // 将最终预测速度和预测位置写回粒子
    particles[myParticleIdx].velAndMaxLife.xyz = pcisphData[gid].predictedVelAndDensity.xyz;
    particles[myParticleIdx].posAndLife.xyz    = pcisphData[gid].predictedPosAndPressure.xyz;
}

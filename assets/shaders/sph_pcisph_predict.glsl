#type compute
#version 430 core

// PCISPH 位置预测: x* = pos + dt * v*

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
layout(std430, set = 0, binding = 0) readonly buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, set = 0, binding = 1) buffer PCISPHBuffer            { PCISPHData  pcisphData[]; };
layout(std430, set = 0, binding = 2) readonly buffer AliveList      { uint aliveIndices[];      };

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
#define u_Gravity            u_GravityAndSmoothingRadius.xyz
#define u_SmoothingRadius    u_GravityAndSmoothingRadius.w
#define u_ParticleMass       u_MassDensityGasViscosity.x
#define u_RestDensity        u_MassDensityGasViscosity.y
#define u_GasConstant        u_MassDensityGasViscosity.z
#define u_Viscosity          u_MassDensityGasViscosity.w
#define u_GridSize           int(u_GridParams.x)
#define u_CellSize           u_GridParams.y
#define u_Poly6Coeff         u_GridParams.z
#define u_SpikyCoeff         u_GridParams.w
#define u_BoundaryStiffness  u_BoundaryParams.x
#define u_BoundaryDamping    u_BoundaryParams.y
#define u_WarmupTime         u_BoundaryParams.z
#define u_SurfaceTension     u_BoundaryParams.w
#define u_RigidBodyCount     int(u_SDFCounts.x)
#define u_MeshSDFCount       int(u_SDFCounts.y)
#define u_MeshSDFVoxelCount  int(u_SDFCounts.z)
#define u_PCISPHDelta        u_SDFCounts.w
#else
layout(std430, binding = 0) readonly buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, binding = 1) buffer PCISPHBuffer            { PCISPHData  pcisphData[]; };
layout(std430, binding = 2) readonly buffer AliveList      { uint aliveIndices[];      };

uniform int   u_AliveCount;
uniform float u_DeltaTime;
#endif

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_AliveCount))
        return;

    uint myParticleIdx = aliveIndices[gid];

    vec3 pos   = particles[myParticleIdx].posAndLife.xyz;
    vec3 vStar = pcisphData[gid].predictedVelAndDensity.xyz;

    pcisphData[gid].predictedPosAndPressure.xyz = pos + u_DeltaTime * vStar;
}

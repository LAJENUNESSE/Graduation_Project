#type compute
#version 430 core

// PCISPH 初始化 — 非压力加速度 (粘性 + 表面张力 + 重力)
// 初始化预测速度 v* = vel + a_np * dt, 压力 P = 0

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
layout(std430, set = 0, binding = 0) buffer ParticlePool    { GPUParticle particles[]; };
layout(std430, set = 0, binding = 1) buffer PCISPHBuffer    { PCISPHData  pcisphData[]; };
layout(std430, set = 0, binding = 2) readonly buffer AliveList     { uint aliveIndices[];     };
layout(std430, set = 0, binding = 5) readonly buffer CellStart     { uint cellStart[];        };
layout(std430, set = 0, binding = 6) readonly buffer CellCount     { uint cellCount[];        };
layout(std430, set = 0, binding = 7) readonly buffer SortedIndices  { uint sortedIndices[];   };
layout(std430, set = 0, binding = 8) readonly buffer SurfaceNormals { vec4 surfaceNormals[]; };

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
layout(std430, binding = 0) buffer ParticlePool   { GPUParticle particles[]; };
layout(std430, binding = 1) buffer PCISPHBuffer    { PCISPHData  pcisphData[]; };
layout(std430, binding = 2) readonly buffer AliveList     { uint aliveIndices[];     };
layout(std430, binding = 5) readonly buffer CellStart     { uint cellStart[];        };
layout(std430, binding = 6) readonly buffer CellCount     { uint cellCount[];        };
layout(std430, binding = 7) readonly buffer SortedIndices  { uint sortedIndices[];   };
layout(std430, binding = 8) readonly buffer SurfaceNormals { vec4 surfaceNormals[]; };

uniform int   u_AliveCount;
uniform float u_SmoothingRadius;   // h
uniform float u_ParticleMass;      // m
uniform float u_Viscosity;         // μ
uniform float u_DeltaTime;
uniform int   u_GridSize;
uniform float u_CellSize;
uniform vec3  u_Gravity;
uniform float u_SurfaceTension;    // γ, 0=关闭
uniform float u_SpikyCoeff;        // -45 / (π * h^6), CPU 预计算
#endif

const float PI = 3.14159265359;

// Spiky kernel gradient: ∇W_spiky = u_SpikyCoeff * (h - |r|)² * (r/|r|)
vec3 spikyGrad(vec3 diff, float dist, float h)
{
    if (dist <= 0.0 || dist >= h) return vec3(0.0);
    float hd = h - dist;
    return u_SpikyCoeff * hd * hd * (diff / dist);
}

// Viscosity kernel Laplacian: ∇²W_visc = -u_SpikyCoeff * (h - |r|)
float viscLaplacian(float dist, float h)
{
    if (dist >= h) return 0.0;
    return -u_SpikyCoeff * (h - dist);
}

// Akinci 表面张力 C_spline
float C_spline(float r, float h)
{
    float q = r / h;
    float coeff = 32.0 / (PI * h * h * h);
    if (q < 0.5)
        return coeff * (2.0 * pow(1.0 - q, 3.0) * pow(q, 3.0) - 1.0 / 64.0);
    else if (q < 1.0)
        return coeff * (pow(1.0 - q, 3.0) * pow(q, 3.0) - 1.0 / 64.0);
    return 0.0;
}

// 空间哈希
uint hashCell(ivec3 cell, int gridSize)
{
    cell = ((cell % gridSize) + gridSize) % gridSize;
    return uint(cell.x + cell.y * gridSize + cell.z * gridSize * gridSize);
}

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_AliveCount))
        return;

    uint myAliveIdx = gid;
    uint myParticleIdx = aliveIndices[myAliveIdx];

    vec3 posI = particles[myParticleIdx].posAndLife.xyz;
    vec3 velI = particles[myParticleIdx].velAndMaxLife.xyz;
    float densityI = particles[myParticleIdx].params.z;
    float h = u_SmoothingRadius;

    // 防止除零
    if (densityI < 0.0001) densityI = 0.0001;

    vec3 fViscosity = vec3(0.0);
    vec3 fSurfTension = vec3(0.0);

    // Akinci 表面法线（从 density pass 预计算）
    vec3 normalI = surfaceNormals[gid].xyz;

    // 遍历 3x3x3 邻域 cells
    ivec3 myCell = ivec3(floor(posI / u_CellSize));

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        ivec3 neighborCell = myCell + ivec3(dx, dy, dz);
        uint cellIdx = hashCell(neighborCell, u_GridSize);

        uint cCount = cellCount[cellIdx];
        if (cCount == 0u) continue;
        uint cEndRaw = cellStart[cellIdx];
        uint cEnd = min(cEndRaw, uint(u_AliveCount));
        uint cBegin = (cCount > cEnd) ? 0u : (cEnd - cCount);

        for (uint s = cBegin; s < cEnd; s++)
        {
            uint neighborAliveIdx = sortedIndices[s];
            if (neighborAliveIdx >= uint(u_AliveCount)) continue;
            uint neighborParticleIdx = aliveIndices[neighborAliveIdx];

            if (neighborParticleIdx == myParticleIdx) continue;

            vec3 posJ = particles[neighborParticleIdx].posAndLife.xyz;
            vec3 velJ = particles[neighborParticleIdx].velAndMaxLife.xyz;
            float densityJ = particles[neighborParticleIdx].params.z;

            if (densityJ < 0.0001) densityJ = 0.0001;

            vec3 diff = posI - posJ;
            float dist = length(diff);

            // 粘性力: f_visc = μ * Σ m_j * (v_j - v_i) / ρ_j * ∇²W_visc
            fViscosity += u_Viscosity * u_ParticleMass * (velJ - velI) / densityJ
                          * viscLaplacian(dist, h);

            // Akinci 2013 表面张力: cohesion + curvature
            if (u_SurfaceTension > 0.0 && dist > 0.001)
            {
                // 内聚力
                fSurfTension += -u_SurfaceTension * u_ParticleMass
                                * C_spline(dist, h) * (diff / dist);
                // 曲率修正: f_curvature = -γ * m_j * (n_i - n_j)
                vec3 normalJ = surfaceNormals[neighborAliveIdx].xyz;
                fSurfTension += -u_SurfaceTension * u_ParticleMass * (normalI - normalJ);
            }
        }
    }

    // 非压力加速度: a_np = (fViscosity + fSurfTension) / ρ_i + gravity
    vec3 a_np = (fViscosity + fSurfTension) / densityI + u_Gravity;

    // 安全限幅
    float maxAccel = 500.0;
    float accelMag = length(a_np);
    if (accelMag > maxAccel)
        a_np *= maxAccel / accelMag;

    // 输出
    pcisphData[gid].nonPressureAccel.xyz = a_np;
    pcisphData[gid].predictedVelAndDensity.xyz = velI + a_np * u_DeltaTime;  // v*
    pcisphData[gid].predictedPosAndPressure.w = 0.0;   // P = 0
    pcisphData[gid].predictedVelAndDensity.w = 0.0;    // ρ* = 0 初始化
}

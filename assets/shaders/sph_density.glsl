#type compute
#version 430 core

// SPH 密度计算 — Poly6 kernel
// 读取邻域粒子，计算每粒子密度 ρ_i 和压力 P_i
// 结果写入 params.z = density, params.w = pressure

layout(local_size_x = 256) in;

struct GPUParticle
{
    vec4 posAndLife;       // xyz=position, w=remainingLife
    vec4 velAndMaxLife;    // xyz=velocity, w=maxLife
    vec4 startColor;
    vec4 endColor;
    vec4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
};

layout(std430, binding = 0) buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, binding = 2) readonly buffer AliveList    { uint aliveIndices[];     };
layout(std430, binding = 5) readonly buffer CellStart    { uint cellStart[];        };
layout(std430, binding = 6) readonly buffer CellCount    { uint cellCount[];        };
layout(std430, binding = 7) readonly buffer SortedIndices { uint sortedIndices[];   };

uniform int   u_AliveCount;
uniform float u_SmoothingRadius;   // h
uniform float u_ParticleMass;      // m
uniform float u_RestDensity;       // ρ_0
uniform float u_GasConstant;       // k
uniform int   u_GridSize;
uniform float u_CellSize;

// Poly6 kernel: W(r, h) = 315 / (64 * π * h^9) * (h² - r²)³
const float PI = 3.14159265359;

float poly6(float r2, float h)
{
    float h2 = h * h;
    if (r2 >= h2) return 0.0;
    float diff = h2 - r2;
    float h9 = h2 * h2 * h2 * h2 * h; // h^9
    return 315.0 / (64.0 * PI * h9) * diff * diff * diff;
}

// 空间哈希：将 3D 坐标映射到 1D cell index
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
    float h = u_SmoothingRadius;

    // 自身贡献
    float density = u_ParticleMass * poly6(0.0, h);

    // 遍历 3x3x3 邻域 cells
    ivec3 myCell = ivec3(floor(posI / u_CellSize));

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        ivec3 neighborCell = myCell + ivec3(dx, dy, dz);
        uint cellIdx = hashCell(neighborCell, u_GridSize);

        // scatter 后: CellStart[h] = original_start + CellCount[h]
        // 所以 begin = CellStart[h] - CellCount[h], end = CellStart[h]
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
            vec3 diff = posI - posJ;
            float r2 = dot(diff, diff);

            density += u_ParticleMass * poly6(r2, h);
        }
    }

    // 状态方程: P = k * (ρ - ρ_0)
    float pressure = u_GasConstant * (density - u_RestDensity);

    // 写入 params.zw (不影响 sizeStart/sizeEnd in params.xy)
    particles[myParticleIdx].params.z = density;
    particles[myParticleIdx].params.w = pressure;
}

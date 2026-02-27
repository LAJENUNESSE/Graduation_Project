#type compute
#version 430 core

// PCISPH 密度预测: 用 x* 计算 ρ*, P += δ * max(0, ρ* - ρ₀)

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

layout(std430, binding = 0) readonly buffer ParticlePool   { GPUParticle particles[]; };
layout(std430, binding = 1) buffer PCISPHBuffer             { PCISPHData  pcisphData[]; };
layout(std430, binding = 2) readonly buffer AliveList       { uint aliveIndices[];      };
layout(std430, binding = 5) readonly buffer CellStart       { uint cellStart[];         };
layout(std430, binding = 6) readonly buffer CellCount       { uint cellCount[];         };
layout(std430, binding = 7) readonly buffer SortedIndices   { uint sortedIndices[];     };

uniform int   u_AliveCount;
uniform float u_SmoothingRadius;   // h
uniform float u_ParticleMass;      // m
uniform float u_RestDensity;       // ρ_0
uniform float u_PCISPHDelta;       // δ
uniform int   u_GridSize;
uniform float u_CellSize;
uniform float u_Poly6Coeff;        // 315 / (64 * π * h^9), CPU 预计算

// Poly6 kernel: W(r, h) = u_Poly6Coeff * (h² - r²)³
float poly6(float r2, float h)
{
    float h2 = h * h;
    if (r2 >= h2) return 0.0;
    float diff = h2 - r2;
    return u_Poly6Coeff * diff * diff * diff;
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

    // 我的预测位置
    vec3 predPosI = pcisphData[gid].predictedPosAndPressure.xyz;
    float h = u_SmoothingRadius;

    // 自身贡献
    float density = u_ParticleMass * poly6(0.0, h);

    // 用原始位置进行 cell 查找（grid 基于原始位置构建）
    vec3 posI = particles[myParticleIdx].posAndLife.xyz;
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

            // 用邻居的预测位置计算距离
            vec3 predPosJ = pcisphData[neighborAliveIdx].predictedPosAndPressure.xyz;
            vec3 diff = predPosI - predPosJ;
            float r2 = dot(diff, diff);

            density += u_ParticleMass * poly6(r2, h);
        }
    }

    // 输出预测密度
    pcisphData[gid].predictedVelAndDensity.w = density;

    // 累加压力: P += δ * max(0, ρ* - ρ₀)
    pcisphData[gid].predictedPosAndPressure.w += u_PCISPHDelta * max(0.0, density - u_RestDensity);
}

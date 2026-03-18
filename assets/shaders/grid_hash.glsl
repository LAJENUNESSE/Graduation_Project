#type compute
#version 430 core

layout(local_size_x = 256) in;

// --- Particle data (binding 0, read-only for position) ---
struct GPUParticle
{
    vec4 posAndLife;       // xyz=position, w=remainingLife
    vec4 velAndMaxLife;    // xyz=velocity, w=maxLife
    vec4 startColor;
    vec4 endColor;
    vec4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
};

// PCISPH 预测数据（binding 9，仅 PCISPH 迭代 1+ grid 重建时使用）
struct PCISPHData
{
    vec4 predictedPosAndPressure; // xyz=predicted position, w=pressure
    vec4 predictedVel;            // xyz=predicted velocity
    vec4 densityError;            // x=predicted density, y=density error
};

layout(std430, binding = 0) readonly buffer ParticlePool { GPUParticle particles[]; };
layout(std430, binding = 9) readonly buffer PCISPHPool   { PCISPHData pcisphData[]; };
layout(std430, binding = 2) readonly buffer AliveList    { uint aliveIndices[];     };
layout(std430, binding = 6) buffer CellCount             { uint cellCount[];        };
layout(std430, binding = 1) buffer CellHash              { uint cellHash[];         };

uniform int   u_AliveCount;
uniform int   u_GridSize;
uniform float u_CellSize;
// 当 u_UsePredictedPos=1 时，从 pcisphData 读取预测位置（PCISPH 迭代 1+ 重建 grid）
uniform int   u_UsePredictedPos;

// 空间哈希：将 3D 坐标映射到 1D cell index
uint hashCell(ivec3 cell)
{
    // Wrap negative coordinates into [0, gridSize)
    cell = ((cell % u_GridSize) + u_GridSize) % u_GridSize;
    return uint(cell.x + cell.y * u_GridSize + cell.z * u_GridSize * u_GridSize);
}

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_AliveCount))
        return;

    uint particleIdx = aliveIndices[gid];

    // 自适应策略：迭代 0 用原始位置（刚构建 grid），迭代 1+ 用预测位置
    vec3 pos;
    if (u_UsePredictedPos != 0)
        pos = pcisphData[particleIdx].predictedPosAndPressure.xyz;
    else
        pos = particles[particleIdx].posAndLife.xyz;

    // 计算所属 cell
    ivec3 cell = ivec3(floor(pos / u_CellSize));
    uint h = hashCell(cell);

    cellHash[gid] = h;

    // Atomic scatter: 统计每个 cell 的粒子数
    atomicAdd(cellCount[h], 1u);
}

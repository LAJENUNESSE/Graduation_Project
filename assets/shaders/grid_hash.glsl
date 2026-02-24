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

layout(std430, binding = 0) readonly buffer ParticlePool { GPUParticle particles[]; };
layout(std430, binding = 2) readonly buffer AliveList    { uint aliveIndices[];     };
layout(std430, binding = 6) buffer CellCount             { uint cellCount[];        };
layout(std430, binding = 1) buffer CellHash              { uint cellHash[];         };

uniform int   u_AliveCount;
uniform int   u_GridSize;
uniform float u_CellSize;

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
    vec3 pos = particles[particleIdx].posAndLife.xyz;

    // 计算所属 cell
    ivec3 cell = ivec3(floor(pos / u_CellSize));
    uint h = hashCell(cell);

    cellHash[gid] = h;

    // Atomic scatter: 统计每个 cell 的粒子数
    atomicAdd(cellCount[h], 1u);
}

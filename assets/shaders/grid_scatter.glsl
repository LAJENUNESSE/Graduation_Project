#type compute
#version 430 core

layout(local_size_x = 256) in;

#ifdef VULKAN
// Vulkan 路径：scatter shader 仅引用 CellHash/CellStart/SortedIndices；
// ParticlePool/AliveList 在 OpenGL 路径下保留以匹配全局 binding 表，
// Vulkan 路径下避免声明未使用 SSBO，简化 descriptor layout 反射结果。
layout(std430, set = 0, binding = 5) buffer CellStart             { uint cellStart[];        };
layout(std430, set = 0, binding = 7) buffer SortedIndices         { uint sortedIndices[];    };
layout(std430, set = 0, binding = 1) readonly buffer CellHash     { uint cellHash[];         };

layout(push_constant) uniform PushConstants
{
    int u_AliveCount;
} pc;
#define u_AliveCount pc.u_AliveCount
#else
struct GPUParticle
{
    vec4 posAndLife;
    vec4 velAndMaxLife;
    vec4 startColor;
    vec4 endColor;
    vec4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
};

layout(std430, binding = 0) readonly buffer ParticlePool { GPUParticle particles[]; };
layout(std430, binding = 2) readonly buffer AliveList    { uint aliveIndices[];     };
layout(std430, binding = 5) buffer CellStart             { uint cellStart[];        };
layout(std430, binding = 7) buffer SortedIndices         { uint sortedIndices[];    };
layout(std430, binding = 1) readonly buffer CellHash     { uint cellHash[];         };

uniform int   u_AliveCount;
#endif

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_AliveCount))
        return;

    uint h = cellHash[gid];

    // Atomically get a write position within this cell's range
    uint pos = atomicAdd(cellStart[h], 1u);

    // Write the alive-list index so SPH can look up the actual particle
    if (pos < uint(u_AliveCount))
        sortedIndices[pos] = gid;
}

#type compute
#version 430 core

layout(local_size_x = 256) in;

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

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_AliveCount))
        return;

    uint h = cellHash[gid];

    // Atomically get a write position within this cell's range
    uint pos = atomicAdd(cellStart[h], 1u);

    // Write the alive-list index so SPH can look up the actual particle
    sortedIndices[pos] = gid;
}

#type compute
#version 430 core

// 流体粒子发射 — 支持两种模式：
//   u_UseLifetime=0: 一次性发射所有粒子（legacy）
//   u_UseLifetime=1: 从 dead list 弹出式发射（lifetime 模式）

layout(local_size_x = 64) in;

struct GPUParticle
{
    vec4 posAndLife;       // xyz=position, w=remainingLife (>0 = alive)
    vec4 velAndMaxLife;    // xyz=velocity, w=maxLife
    vec4 startColor;       // unused for fluid
    vec4 endColor;         // unused for fluid
    vec4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
};

layout(std430, binding = 0)  buffer ParticlePool { GPUParticle particles[]; };
layout(std430, binding = 12) buffer DeadList     { uint deadIndices[];      };
layout(std430, binding = 13) buffer Counters {
    uint deadCount;
    uint aliveCount;
    uint emitCount;
    uint pad;
};

uniform vec3  u_EmitterPos;
uniform vec3  u_EmitExtents;      // 发射区域半尺寸
uniform vec3  u_InitialVelocity;
uniform int   u_ParticleCount;    // max particles (pool size)
uniform float u_Time;
uniform float u_ParticleLifetime; // 寿命（秒），0 = infinite
uniform int   u_UseLifetime;      // 0=legacy one-shot, 1=dead-list mode

uint pcg_hash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand01(inout uint seed)
{
    seed = pcg_hash(seed);
    return float(seed) / 4294967296.0;
}

void main()
{
    uint gid = gl_GlobalInvocationID.x;

    uint particleIdx;
    if (u_UseLifetime != 0)
    {
        // Lifetime 模式：从 dead list 弹出
        if (gid >= emitCount)
            return;

        uint oldDead = atomicAdd(deadCount, 0xFFFFFFFFu); // decrement
        if (oldDead == 0u || oldDead > 0x80000000u)
        {
            atomicAdd(deadCount, 1u); // undo
            return;
        }
        uint deadSlot = oldDead - 1u;
        if (deadSlot >= uint(u_ParticleCount))
        {
            atomicAdd(deadCount, 1u); // undo
            return;
        }
        particleIdx = deadIndices[deadSlot];
        if (particleIdx >= uint(u_ParticleCount))
            return;
    }
    else
    {
        // Legacy 模式：直接按索引发射
        if (gid >= uint(u_ParticleCount))
            return;
        particleIdx = gid;
    }

    uint seed = pcg_hash(gid + uint(u_Time * 1000.0) * 1099u);

    // 在发射区域内随机分布
    float rx = mix(-u_EmitExtents.x, u_EmitExtents.x, rand01(seed));
    float ry = mix(-u_EmitExtents.y, u_EmitExtents.y, rand01(seed));
    float rz = mix(-u_EmitExtents.z, u_EmitExtents.z, rand01(seed));

    vec3 pos = u_EmitterPos + vec3(rx, ry, rz);

    float life = (u_ParticleLifetime > 0.0) ? u_ParticleLifetime : 1.0;

    particles[particleIdx].posAndLife    = vec4(pos, life);
    particles[particleIdx].velAndMaxLife = vec4(u_InitialVelocity, life);
    particles[particleIdx].startColor   = vec4(0.0);
    particles[particleIdx].endColor     = vec4(0.0);
    particles[particleIdx].params       = vec4(0.0);                   // SPH 会写入 .zw
}

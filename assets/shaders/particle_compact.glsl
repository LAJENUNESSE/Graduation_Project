#type compute
#version 430 core

// 重建 alive/dead list —— emit 后、SPH 前调用，确保 SPH 看到最新粒子状态

layout(local_size_x = 256) in;

struct GPUParticle
{
    vec4 posAndLife;
    vec4 velAndMaxLife;
    vec4 startColor;
    vec4 endColor;
    vec4 params;
};

#ifdef VULKAN
layout(std430, set = 0, binding = 0) buffer ParticlePool { GPUParticle particles[]; };
layout(std430, set = 0, binding = 1) buffer DeadList     { uint deadIndices[];      };
layout(std430, set = 0, binding = 2) buffer AliveList    { uint aliveIndices[];     };
layout(std430, set = 0, binding = 3) buffer Counters     {
    uint deadCount;
    uint aliveCount;
    uint emitCount;
    uint simulateCount;
};

layout(push_constant) uniform PushConstants
{
    uint u_MaxParticles;
} pc;
#define MAX_PARTICLES pc.u_MaxParticles
#else
layout(std430, binding = 0) buffer ParticlePool { GPUParticle particles[]; };
layout(std430, binding = 1) buffer DeadList     { uint deadIndices[];      };
layout(std430, binding = 2) buffer AliveList    { uint aliveIndices[];     };
layout(std430, binding = 3) buffer Counters     {
    uint deadCount;
    uint aliveCount;
    uint emitCount;
    uint simulateCount;
};

uniform int u_MaxParticles;
#define MAX_PARTICLES uint(u_MaxParticles)
#endif

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= MAX_PARTICLES)
        return;

    if (particles[gid].posAndLife.w > 0.0)
    {
        uint idx = atomicAdd(aliveCount, 1u);
        aliveIndices[idx] = gid;
    }
    else
    {
        uint idx = atomicAdd(deadCount, 1u);
        deadIndices[idx] = gid;
    }
}

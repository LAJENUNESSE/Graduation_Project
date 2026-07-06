#type compute
#version 430 core

layout(local_size_x = 256) in;

// Must match C++ GPUParticleData: 5 x vec4 = 80 bytes
struct GPUParticle
{
    vec4 posAndLife;       // xyz=position, w=remainingLife
    vec4 velAndMaxLife;    // xyz=velocity, w=maxLife
    vec4 startColor;       // RGBA
    vec4 endColor;         // RGBA
    vec4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH用), w=pressure(SPH用)
};

#ifdef VULKAN
layout(std430, set = 0, binding = 0) buffer ParticlePool { GPUParticle particles[]; };
layout(std430, set = 0, binding = 1) buffer DeadList     { uint deadIndices[];     };
layout(std430, set = 0, binding = 2) buffer AliveList    { uint aliveIndices[];    };
layout(std430, set = 0, binding = 3) buffer CounterBuffer {
    uint deadCount;
    uint aliveCount;
    uint emitCount;
    uint pad;
} counters;

// Simulate 重力/阻尼参数 UBO（>16B push constant 容量）
layout(std140, set = 0, binding = 6) uniform SimParams
{
    vec4 u_GravityAndDamping; // xyz=Gravity, w=Damping
};

layout(push_constant) uniform PushConstants
{
    float u_DeltaTime;
    uint  u_MaxParticles;
    uint  u_Flags;            // 预留：将来 SPH/PCISPH 路径切换标志
} pc;
#define DELTA_TIME    pc.u_DeltaTime
#define MAX_PARTICLES pc.u_MaxParticles
#define GRAVITY       u_GravityAndDamping.xyz
#define DAMPING       u_GravityAndDamping.w
#else
layout(std430, binding = 0) buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, binding = 1) buffer DeadList       { uint deadIndices[];     };
layout(std430, binding = 2) buffer AliveList      { uint aliveIndices[];    };
layout(std430, binding = 3) buffer CounterBuffer  {
    uint deadCount;
    uint aliveCount;
    uint emitCount;
    uint pad;
} counters;

uniform float u_DeltaTime;
uniform vec3  u_Gravity;
uniform float u_Damping;
uniform int   u_MaxParticles;
#define DELTA_TIME    u_DeltaTime
#define MAX_PARTICLES uint(u_MaxParticles)
#define GRAVITY       u_Gravity
#define DAMPING       u_Damping
#endif

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= MAX_PARTICLES)
        return;

    float life = particles[idx].posAndLife.w;

    // Skip already-dead particles (life == 0 means already recycled)
    if (life <= 0.0)
        return;

    // Decrease life
    life -= DELTA_TIME;

    if (life <= 0.0)
    {
        // Particle just died -> set life=0, push to dead list
        particles[idx].posAndLife.w = 0.0;
        uint slot = atomicAdd(counters.deadCount, 1u);
        if (slot < MAX_PARTICLES)
            deadIndices[slot] = idx;
    }
    else
    {
        particles[idx].posAndLife.w = life;
        // Apply gravity
        vec3 vel = particles[idx].velAndMaxLife.xyz;
        vel += GRAVITY * DELTA_TIME;

        // Apply damping (simple multiplicative)
        vel *= DAMPING;

        particles[idx].velAndMaxLife.xyz = vel;

        // Update position
        particles[idx].posAndLife.xyz += vel * DELTA_TIME;

        // Push to alive list for rendering
        uint slot = atomicAdd(counters.aliveCount, 1u);
        if (slot < MAX_PARTICLES)
            aliveIndices[slot] = idx;
    }
}

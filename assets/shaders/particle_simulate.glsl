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

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uint(u_MaxParticles))
        return;

    float life = particles[idx].posAndLife.w;

    // Skip already-dead particles (life == 0 means already recycled)
    if (life <= 0.0)
        return;

    // Decrease life
    life -= u_DeltaTime;

    if (life <= 0.0)
    {
        // Particle just died -> set life=0, push to dead list
        particles[idx].posAndLife.w = 0.0;
        uint slot = atomicAdd(counters.deadCount, 1u);
        deadIndices[slot] = idx;
    }
    else
    {
        particles[idx].posAndLife.w = life;
        // Apply gravity
        vec3 vel = particles[idx].velAndMaxLife.xyz;
        vel += u_Gravity * u_DeltaTime;

        // Apply damping (simple multiplicative)
        vel *= u_Damping;

        particles[idx].velAndMaxLife.xyz = vel;

        // Update position
        particles[idx].posAndLife.xyz += vel * u_DeltaTime;

        // Push to alive list for rendering
        uint slot = atomicAdd(counters.aliveCount, 1u);
        aliveIndices[slot] = idx;
    }
}

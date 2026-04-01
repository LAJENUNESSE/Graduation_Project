#type compute
#version 430 core

layout(local_size_x = 64) in;

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
layout(std430, binding = 3) buffer CounterBuffer  {
    uint deadCount;
    uint aliveCount;
    uint emitCount;
    uint pad;
} counters;

uniform vec3  u_EmitterPos;
uniform vec3  u_EmitDirection;
uniform float u_EmitAngle;      // radians
uniform float u_LifeMin;
uniform float u_LifeMax;
uniform float u_SpeedMin;
uniform float u_SpeedMax;
uniform float u_SizeStart;
uniform float u_SizeEnd;
uniform vec4  u_StartColor;
uniform vec4  u_EndColor;
uniform float u_Time;
uniform int   u_MaxParticles;

// PCG hash for pseudo-random number generation
uint pcg_hash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Returns a float in [0, 1)
float rand(uint seed)
{
    return float(pcg_hash(seed)) / 4294967296.0;
}

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= counters.emitCount)
        return;

    // Atomically pop from dead list
    uint oldDead = atomicAdd(counters.deadCount, 0xFFFFFFFFu); // decrement
    if (oldDead == 0u || oldDead > 0x80000000u)  // 0 or underflowed
    {
        atomicAdd(counters.deadCount, 1u); // undo
        return;
    }
    uint deadSlot = oldDead - 1u;
    if (deadSlot >= uint(u_MaxParticles))
    {
        atomicAdd(counters.deadCount, 1u); // undo
        return;
    }
    uint particleIdx = deadIndices[deadSlot];
    if (particleIdx >= uint(u_MaxParticles))
        return;

    // Generate random seeds
    uint seed = pcg_hash(gid + uint(u_Time * 1000.0) * 1099u);

    // Random life
    float life = mix(u_LifeMin, u_LifeMax, rand(seed));
    life = max(life, 1e-6);
    seed = pcg_hash(seed);

    // Random direction within cone
    float cosAngle = cos(u_EmitAngle);
    float z = mix(cosAngle, 1.0, rand(seed));  seed = pcg_hash(seed);
    float phi = rand(seed) * 6.28318530718;     seed = pcg_hash(seed);
    float sinTheta = sqrt(1.0 - z * z);
    vec3 localDir = vec3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    // Build rotation from (0,0,1) to u_EmitDirection
    vec3 dir = normalize(u_EmitDirection);
    vec3 up  = abs(dir.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent  = normalize(cross(up, dir));
    vec3 binormal = cross(dir, tangent);

    vec3 worldDir = tangent * localDir.x + binormal * localDir.y + dir * localDir.z;

    // Random speed
    float speed = mix(u_SpeedMin, u_SpeedMax, rand(seed));  seed = pcg_hash(seed);

    // Initialize particle
    particles[particleIdx].posAndLife    = vec4(u_EmitterPos, life);
    particles[particleIdx].velAndMaxLife = vec4(worldDir * speed, life);
    particles[particleIdx].startColor    = u_StartColor;
    particles[particleIdx].endColor      = u_EndColor;
    particles[particleIdx].params        = vec4(u_SizeStart, u_SizeEnd, 0.0, 0.0);
}

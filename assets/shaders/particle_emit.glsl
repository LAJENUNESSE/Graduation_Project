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

#ifdef VULKAN
// Vulkan 路径：SSBO 加 set=0，emitter 大参数走 UBO（>16B），小常量走 push constant
layout(std430, set = 0, binding = 0) buffer ParticlePool { GPUParticle particles[]; };
layout(std430, set = 0, binding = 1) buffer DeadList     { uint deadIndices[];     };
layout(std430, set = 0, binding = 3) buffer CounterBuffer {
    uint deadCount;
    uint aliveCount;
    uint emitCount;
    uint pad;
} counters;

// Emitter 参数 UBO：emitter pos / direction / color / range 等大块数据
layout(std140, set = 0, binding = 5) uniform EmitParams
{
    vec4  u_EmitterPosAndAngle;   // xyz=EmitterPos, w=EmitAngle(radians)
    vec4  u_EmitDirAndSpeedMin;   // xyz=EmitDirection, w=SpeedMin
    vec4  u_LifeAndSpeedMax;      // x=LifeMin, y=LifeMax, z=SpeedMax, w=unused
    vec4  u_SizeStartEnd;         // x=SizeStart, y=SizeEnd, z=unused, w=unused
    vec4  u_StartColor;
    vec4  u_EndColor;
};

// 小常量走 push constant（每帧变化 + ≤16B）
layout(push_constant) uniform PushConstants
{
    uint  u_EmitCount;
    uint  u_MaxParticles;
    float u_Time;
    uint  u_Seed;
} pc;

#define EMITTER_POS     u_EmitterPosAndAngle.xyz
#define EMITTER_ANGLE   u_EmitterPosAndAngle.w
#define EMIT_DIR        u_EmitDirAndSpeedMin.xyz
#define SPEED_MIN       u_EmitDirAndSpeedMin.w
#define LIFE_MIN        u_LifeAndSpeedMax.x
#define LIFE_MAX        u_LifeAndSpeedMax.y
#define SPEED_MAX       u_LifeAndSpeedMax.z
#define SIZE_START      u_SizeStartEnd.x
#define SIZE_END        u_SizeStartEnd.y
#define START_COLOR     u_StartColor
#define END_COLOR       u_EndColor
#define MAX_PARTICLES   pc.u_MaxParticles
#define TIME_VAL        pc.u_Time
#else
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

#define EMITTER_POS     u_EmitterPos
#define EMITTER_ANGLE   u_EmitAngle
#define EMIT_DIR        u_EmitDirection
#define SPEED_MIN       u_SpeedMin
#define SPEED_MAX       u_SpeedMax
#define LIFE_MIN        u_LifeMin
#define LIFE_MAX        u_LifeMax
#define SIZE_START      u_SizeStart
#define SIZE_END        u_SizeEnd
#define START_COLOR     u_StartColor
#define END_COLOR       u_EndColor
#define MAX_PARTICLES   uint(u_MaxParticles)
#define TIME_VAL        u_Time
#endif

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
    if (deadSlot >= MAX_PARTICLES)
    {
        atomicAdd(counters.deadCount, 1u); // undo
        return;
    }
    uint particleIdx = deadIndices[deadSlot];
    if (particleIdx >= MAX_PARTICLES)
        return;

    // Generate random seeds
    uint seed = pcg_hash(gid + uint(TIME_VAL * 1000.0) * 1099u);

    // Random life
    float life = mix(LIFE_MIN, LIFE_MAX, rand(seed));
    life = max(life, 1e-6);
    seed = pcg_hash(seed);

    // Random direction within cone
    float cosAngle = cos(EMITTER_ANGLE);
    float z = mix(cosAngle, 1.0, rand(seed));  seed = pcg_hash(seed);
    float phi = rand(seed) * 6.28318530718;     seed = pcg_hash(seed);
    float sinTheta = sqrt(1.0 - z * z);
    vec3 localDir = vec3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    // Build rotation from (0,0,1) to EMIT_DIR
    vec3 dir = normalize(EMIT_DIR);
    vec3 up  = abs(dir.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent  = normalize(cross(up, dir));
    vec3 binormal = cross(dir, tangent);

    vec3 worldDir = tangent * localDir.x + binormal * localDir.y + dir * localDir.z;

    // Random speed
    float speed = mix(SPEED_MIN, SPEED_MAX, rand(seed));  seed = pcg_hash(seed);

    // Initialize particle
    particles[particleIdx].posAndLife    = vec4(EMITTER_POS, life);
    particles[particleIdx].velAndMaxLife = vec4(worldDir * speed, life);
    particles[particleIdx].startColor    = START_COLOR;
    particles[particleIdx].endColor      = END_COLOR;
    particles[particleIdx].params        = vec4(SIZE_START, SIZE_END, 0.0, 0.0);
}

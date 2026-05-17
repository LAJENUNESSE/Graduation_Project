#type compute
#version 430 core

// 流体粒子一次性发射 — 在 EmitExtents 区域内随机分布
// 粒子使用与 ParticleSystemGPU 相同的 80 bytes 结构（兼容现有 SPH 着色器）

layout(local_size_x = 64) in;

struct GPUParticle
{
    vec4 posAndLife;       // xyz=position, w=remainingLife (>0 = alive)
    vec4 velAndMaxLife;    // xyz=velocity, w=maxLife
    vec4 startColor;      // unused for fluid
    vec4 endColor;        // unused for fluid
    vec4 params;          // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
};

#ifdef VULKAN
// Vulkan 路径：SSBO 加 set=0，emitter 大块向量参数走 UBO，小常量走 push constant
layout(std430, set = 0, binding = 0) buffer ParticlePool { GPUParticle particles[]; };

// Emitter 参数 UBO（vec3 走 std140 vec4 占位）
layout(std140, set = 0, binding = 5) uniform EmitParams
{
    vec4 u_EmitterPosV;        // xyz=EmitterPos, w=pad
    vec4 u_EmitExtentsV;       // xyz=EmitExtents, w=pad
    vec4 u_InitialVelocityV;   // xyz=InitialVelocity, w=pad
};

layout(push_constant) uniform PushConstants
{
    uint  u_ParticleCountPC;
    float u_TimePC;
} pc;

#define EPOS         u_EmitterPosV.xyz
#define EEXT         u_EmitExtentsV.xyz
#define EVEL         u_InitialVelocityV.xyz
#define PCOUNT       pc.u_ParticleCountPC
#define TIME_VAL     pc.u_TimePC
#else
layout(std430, binding = 0) buffer ParticlePool { GPUParticle particles[]; };

uniform vec3  u_EmitterPos;
uniform vec3  u_EmitExtents;      // 发射区域半尺寸
uniform vec3  u_InitialVelocity;
uniform int   u_ParticleCount;
uniform float u_Time;

#define EPOS         u_EmitterPos
#define EEXT         u_EmitExtents
#define EVEL         u_InitialVelocity
#define PCOUNT       uint(u_ParticleCount)
#define TIME_VAL     u_Time
#endif

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
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= PCOUNT) return;

    uint seed = pcg_hash(idx + uint(TIME_VAL * 1000.0) * 1099u);

    // 在发射区域内随机分布
    float rx = mix(-EEXT.x, EEXT.x, rand01(seed));
    float ry = mix(-EEXT.y, EEXT.y, rand01(seed));
    float rz = mix(-EEXT.z, EEXT.z, rand01(seed));

    vec3 pos = EPOS + vec3(rx, ry, rz);

    particles[idx].posAndLife    = vec4(pos, 1.0);             // w>0 = alive
    particles[idx].velAndMaxLife = vec4(EVEL, 1.0);
    particles[idx].startColor   = vec4(0.0);
    particles[idx].endColor     = vec4(0.0);
    particles[idx].params       = vec4(0.0);                   // SPH 会写入 .zw
}

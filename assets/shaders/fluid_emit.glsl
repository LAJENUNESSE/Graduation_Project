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

layout(std430, binding = 0) buffer ParticlePool { GPUParticle particles[]; };

uniform vec3  u_EmitterPos;
uniform vec3  u_EmitExtents;      // 发射区域半尺寸
uniform vec3  u_InitialVelocity;
uniform int   u_ParticleCount;
uniform float u_Time;

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
    if (idx >= uint(u_ParticleCount)) return;

    uint seed = pcg_hash(idx + uint(u_Time * 1000.0) * 1099u);

    // 在发射区域内随机分布
    float rx = mix(-u_EmitExtents.x, u_EmitExtents.x, rand01(seed));
    float ry = mix(-u_EmitExtents.y, u_EmitExtents.y, rand01(seed));
    float rz = mix(-u_EmitExtents.z, u_EmitExtents.z, rand01(seed));

    vec3 pos = u_EmitterPos + vec3(rx, ry, rz);

    particles[idx].posAndLife    = vec4(pos, 1.0);             // w>0 = alive
    particles[idx].velAndMaxLife = vec4(u_InitialVelocity, 1.0);
    particles[idx].startColor   = vec4(0.0);
    particles[idx].endColor     = vec4(0.0);
    particles[idx].params       = vec4(0.0);                   // SPH 会写入 .zw
}

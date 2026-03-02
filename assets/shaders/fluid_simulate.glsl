#type compute
#version 430 core

// 流体粒子积分 — 位置/速度更新 + 边界约束
// SPH 力已经在之前的 pass 中写入了 velocity

layout(local_size_x = 256) in;

struct GPUParticle
{
    vec4 posAndLife;
    vec4 velAndMaxLife;
    vec4 startColor;
    vec4 endColor;
    vec4 params;
};

layout(std430, binding = 0) buffer ParticlePool { GPUParticle particles[]; };

uniform float u_DeltaTime;
uniform vec3  u_Gravity;
uniform float u_Damping;
uniform int   u_ParticleCount;
uniform vec3  u_BoundaryMin;
uniform vec3  u_BoundaryMax;
uniform int   u_UseBoundary;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uint(u_ParticleCount)) return;

    vec3 vel = particles[idx].velAndMaxLife.xyz;

    // 叠加重力（PCISPH 模式下外部传入零重力）
    vel += u_Gravity * u_DeltaTime;

    // 速度阻尼
    vel *= u_Damping;

    // 积分位置
    vec3 pos = particles[idx].posAndLife.xyz + vel * u_DeltaTime;

    // 边界约束（位置 clamp + 速度反弹）
    if (u_UseBoundary != 0)
    {
        float restitution = 0.3;
        for (int i = 0; i < 3; i++)
        {
            if (pos[i] < u_BoundaryMin[i])
            {
                pos[i] = u_BoundaryMin[i];
                vel[i] = abs(vel[i]) * restitution;
            }
            else if (pos[i] > u_BoundaryMax[i])
            {
                pos[i] = u_BoundaryMax[i];
                vel[i] = -abs(vel[i]) * restitution;
            }
        }
    }

    particles[idx].posAndLife.xyz = pos;
    particles[idx].velAndMaxLife.xyz = vel;
}

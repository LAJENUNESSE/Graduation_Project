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
uniform int   u_PCISPHMode;  // 0=标准WCSPH, 1=PCISPH（位置已由 apply 写入）
uniform float u_MaxSpeed;    // CFL 速度上限 = h / dt

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uint(u_ParticleCount)) return;

    vec3 vel = particles[idx].velAndMaxLife.xyz;

    // 先叠加重力（PCISPH 模式下外部传入零重力）
    if (u_PCISPHMode == 0)
    {
        vel += u_Gravity * u_DeltaTime;
    }

    // 帧率无关阻尼：pow(damping, dt/0.016667) 使不同帧率行为一致
    vel *= pow(u_Damping, u_DeltaTime / 0.016667);

    // CFL 速度安全限幅
    float speed = length(vel);
    if (speed > u_MaxSpeed)
        vel *= u_MaxSpeed / speed;

    vec3 pos;
    if (u_PCISPHMode == 0)
    {
        // WCSPH：正常积分位置
        pos = particles[idx].posAndLife.xyz + vel * u_DeltaTime;
    }
    else
    {
        // PCISPH：位置已由 pcisph_apply 写入，仅读取
        pos = particles[idx].posAndLife.xyz;
    }

    // 边界约束（穿透深度反射 + 速度反弹）
    if (u_UseBoundary != 0)
    {
        float restitution = 0.3;
        for (int i = 0; i < 3; i++)
        {
            if (pos[i] < u_BoundaryMin[i])
            {
                float penetration = u_BoundaryMin[i] - pos[i];
                pos[i] = u_BoundaryMin[i] + penetration * restitution;
                vel[i] = abs(vel[i]) * restitution;
            }
            else if (pos[i] > u_BoundaryMax[i])
            {
                float penetration = pos[i] - u_BoundaryMax[i];
                pos[i] = u_BoundaryMax[i] - penetration * restitution;
                vel[i] = -abs(vel[i]) * restitution;
            }
        }
    }

    particles[idx].posAndLife.xyz = pos;
    particles[idx].velAndMaxLife.xyz = vel;
}

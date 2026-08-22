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

#ifdef VULKAN
// Vulkan 路径：SSBO 加 set=0，simulate 大块参数走 UBO，小常量走 push constant
layout(std430, set = 0, binding = 0) buffer ParticlePool { GPUParticle particles[]; };

// Simulate 参数 UBO
layout(std140, set = 0, binding = 6) uniform SimParams
{
    vec4 u_GravityAndDamping;     // xyz=Gravity, w=Damping
    vec4 u_BoundaryMinAndUseFlag; // xyz=BoundaryMin, w=UseBoundary(1.0/0.0)
    vec4 u_BoundaryMaxAndMode;    // xyz=BoundaryMax, w=PCISPHMode(1.0/0.0)
};

layout(push_constant) uniform PushConstants
{
    float u_DeltaTimePC;
    uint  u_ParticleCountPC;
} pc;

#define DT_VAL        pc.u_DeltaTimePC
#define PCOUNT_VAL    pc.u_ParticleCountPC
#define GRAVITY_VAL   u_GravityAndDamping.xyz
#define DAMPING_VAL   u_GravityAndDamping.w
#define BMIN_VAL      u_BoundaryMinAndUseFlag.xyz
#define BMAX_VAL      u_BoundaryMaxAndMode.xyz
#define USE_BOUNDARY  (u_BoundaryMinAndUseFlag.w > 0.5)
#define PCISPH_MODE   (u_BoundaryMaxAndMode.w > 0.5)
#else
layout(std430, binding = 0) buffer ParticlePool { GPUParticle particles[]; };

uniform float u_DeltaTime;
uniform vec3  u_Gravity;
uniform float u_Damping;
uniform int   u_ParticleCount;
uniform vec3  u_BoundaryMin;
uniform vec3  u_BoundaryMax;
uniform int   u_UseBoundary;
uniform int   u_PCISPHMode;  // 0=标准WCSPH, 1=PCISPH（位置已由 apply 写入）

#define DT_VAL        u_DeltaTime
#define PCOUNT_VAL    uint(u_ParticleCount)
#define GRAVITY_VAL   u_Gravity
#define DAMPING_VAL   u_Damping
#define BMIN_VAL      u_BoundaryMin
#define BMAX_VAL      u_BoundaryMax
#define USE_BOUNDARY  (u_UseBoundary != 0)
#define PCISPH_MODE   (u_PCISPHMode != 0)
#endif

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= PCOUNT_VAL) return;

    vec3 vel = particles[idx].velAndMaxLife.xyz;

    // 先叠加重力（PCISPH 模式下外部传入零重力）
    if (!PCISPH_MODE)
    {
        vel += GRAVITY_VAL * DT_VAL;
    }

    // 再阻尼（统一与 CUDA FluidSimulateKernel 顺序：先力后阻尼）
    vel *= DAMPING_VAL;

    vec3 pos;
    if (!PCISPH_MODE)
    {
        // WCSPH：正常积分位置
        pos = particles[idx].posAndLife.xyz + vel * DT_VAL;
    }
    else
    {
        // PCISPH：位置已由 pcisph_apply 写入，仅读取
        pos = particles[idx].posAndLife.xyz;
    }

    // 边界约束（穿透深度反射 + 速度反弹）
    if (USE_BOUNDARY)
    {
        float restitution = 0.3;
        for (int i = 0; i < 3; i++)
        {
            if (pos[i] < BMIN_VAL[i])
            {
                float penetration = BMIN_VAL[i] - pos[i];
                pos[i] = BMIN_VAL[i] + penetration * restitution;
                vel[i] = abs(vel[i]) * restitution;
            }
            else if (pos[i] > BMAX_VAL[i])
            {
                float penetration = pos[i] - BMAX_VAL[i];
                pos[i] = BMAX_VAL[i] - penetration * restitution;
                vel[i] = -abs(vel[i]) * restitution;
            }
        }
    }

    // NaN/Inf 防扩散：流体粒子无生命周期无法回收，钳回有限值阻断经密度
    // 计算向邻域传播（边界反射对 NaN 两分支皆 false，拦不住）
    if (any(isnan(pos)) || any(isinf(pos)) || any(isnan(vel)) || any(isinf(vel)))
    {
        pos = USE_BOUNDARY ? 0.5 * (BMIN_VAL + BMAX_VAL) : vec3(0.0);
        vel = vec3(0.0);
    }

    particles[idx].posAndLife.xyz = pos;
    particles[idx].velAndMaxLife.xyz = vel;
}

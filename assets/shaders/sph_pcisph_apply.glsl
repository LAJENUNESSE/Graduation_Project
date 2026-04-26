#type compute
#version 430 core

// PCISPH 应用: v* → particle.vel

layout(local_size_x = 256) in;

struct GPUParticle
{
    vec4 posAndLife;       // xyz=position, w=remainingLife
    vec4 velAndMaxLife;    // xyz=velocity, w=maxLife
    vec4 startColor;
    vec4 endColor;
    vec4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
};

struct PCISPHData
{
    vec4 predictedPosAndPressure;  // xyz=x*, w=P
    vec4 predictedVelAndDensity;   // xyz=v*, w=ρ*
    vec4 nonPressureAccel;         // xyz=a_np, w=unused
};

layout(std430, binding = 0) buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, binding = 1) readonly buffer PCISPHBuffer  { PCISPHData  pcisphData[]; };
layout(std430, binding = 2) readonly buffer AliveList     { uint aliveIndices[];      };

uniform int   u_AliveCount;
uniform float u_MaxSpeed;

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_AliveCount))
        return;

    uint myParticleIdx = aliveIndices[gid];

    // 速度安全限幅 (CFL)
    vec3 vel = pcisphData[gid].predictedVelAndDensity.xyz;
    float speed = length(vel);
    if (speed > u_MaxSpeed)
        vel *= u_MaxSpeed / speed;

    // 将最终预测速度和预测位置写回粒子
    particles[myParticleIdx].velAndMaxLife.xyz = vel;
    particles[myParticleIdx].posAndLife.xyz    = pcisphData[gid].predictedPosAndPressure.xyz;
}

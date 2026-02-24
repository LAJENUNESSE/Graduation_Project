#type compute
#version 430 core

// PCISPH 位置预测: x* = pos + dt * v*

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

layout(std430, binding = 0) readonly buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, binding = 1) buffer PCISPHBuffer            { PCISPHData  pcisphData[]; };
layout(std430, binding = 2) readonly buffer AliveList      { uint aliveIndices[];      };

uniform int   u_AliveCount;
uniform float u_DeltaTime;

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_AliveCount))
        return;

    uint myParticleIdx = aliveIndices[gid];

    vec3 pos   = particles[myParticleIdx].posAndLife.xyz;
    vec3 vStar = pcisphData[gid].predictedVelAndDensity.xyz;

    pcisphData[gid].predictedPosAndPressure.xyz = pos + u_DeltaTime * vStar;
}

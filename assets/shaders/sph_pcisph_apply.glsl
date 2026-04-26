#type compute
#version 430 core

// PCISPH 应用: v* → particle.vel + 最终刚体穿透硬约束

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

struct GPURigidBody
{
    vec4 posAndType;    // xyz=center, w=0(box)/1(sphere)
    vec4 rotCol0;
    vec4 rotCol1;
    vec4 rotCol2;
    vec4 halfExtents;   // box: xyz=半尺寸; sphere: x=radius
    vec4 linearVel;
    vec4 angularVel;
};

layout(std430, binding = 0) buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, binding = 1) readonly buffer PCISPHBuffer  { PCISPHData  pcisphData[]; };
layout(std430, binding = 2) readonly buffer AliveList     { uint aliveIndices[];      };
layout(std430, binding = 3) readonly buffer RigidBodyBuffer { GPURigidBody rigidBodies[]; };

uniform int   u_AliveCount;
uniform float u_MaxSpeed;
uniform int   u_RigidBodyCount;

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

    vec3 pos = pcisphData[gid].predictedPosAndPressure.xyz;

    // 最终刚体穿透硬约束：Apply 阶段是提交粒子位置前的最后检查点
    for (int rb = 0; rb < u_RigidBodyCount; rb++)
    {
        vec3 rbPos = rigidBodies[rb].posAndType.xyz;
        float rbType = rigidBodies[rb].posAndType.w;
        mat3 rbRot = mat3(rigidBodies[rb].rotCol0.xyz,
                          rigidBodies[rb].rotCol1.xyz,
                          rigidBodies[rb].rotCol2.xyz);

        vec3 localPos = transpose(rbRot) * (pos - rbPos);
        float sdf;
        vec3 localNormal;

        if (rbType < 0.5) // Box
        {
            vec3 he = rigidBodies[rb].halfExtents.xyz;
            vec3 d = abs(localPos) - he;
            sdf = length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
            vec3 s = sign(localPos);
            vec3 outside = max(d, 0.0);
            float outsideLen = length(outside);
            if (outsideLen > 1e-6)
                localNormal = s * outside / outsideLen;
            else if (d.x > d.y && d.x > d.z)
                localNormal = vec3(s.x, 0, 0);
            else if (d.y > d.z)
                localNormal = vec3(0, s.y, 0);
            else
                localNormal = vec3(0, 0, s.z);
        }
        else // Sphere
        {
            float radius = rigidBodies[rb].halfExtents.x;
            sdf = length(localPos) - radius;
            localNormal = length(localPos) > 0.001 ? normalize(localPos) : vec3(0, 1, 0);
        }

        if (sdf < 0.0)
        {
            vec3 worldNormal = rbRot * localNormal;
            pos += (-sdf + 0.001) * worldNormal;
            float vn = dot(vel, worldNormal);
            if (vn < 0.0)
                vel -= vn * worldNormal;
        }
    }

    // 将最终位置和速度写回粒子
    particles[myParticleIdx].velAndMaxLife.xyz = vel;
    particles[myParticleIdx].posAndLife.xyz    = pos;
}

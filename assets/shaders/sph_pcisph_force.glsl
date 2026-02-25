#type compute
#version 430 core

// PCISPH 力计算: 压力梯度力 + 刚体边界力

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

layout(std430, binding = 0) readonly buffer ParticlePool   { GPUParticle  particles[];   };
layout(std430, binding = 1) buffer PCISPHBuffer             { PCISPHData   pcisphData[];  };
layout(std430, binding = 2) readonly buffer AliveList       { uint aliveIndices[];        };
layout(std430, binding = 3) readonly buffer RigidBodyBuffer { GPURigidBody rigidBodies[]; };
layout(std430, binding = 5) readonly buffer CellStart       { uint cellStart[];           };
layout(std430, binding = 6) readonly buffer CellCount       { uint cellCount[];           };
layout(std430, binding = 7) readonly buffer SortedIndices   { uint sortedIndices[];       };

uniform int   u_AliveCount;
uniform float u_SmoothingRadius;
uniform float u_ParticleMass;
uniform float u_DeltaTime;
uniform int   u_GridSize;
uniform float u_CellSize;
uniform int   u_RigidBodyCount;
uniform float u_BoundaryStiffness;
uniform float u_BoundaryDamping;

const float PI = 3.14159265359;

// Spiky kernel gradient: ∇W_spiky(r, h) = -45 / (π * h^6) * (h - |r|)² * (r/|r|)
vec3 spikyGrad(vec3 diff, float dist, float h)
{
    if (dist <= 0.0 || dist >= h) return vec3(0.0);
    float h6 = h * h * h * h * h * h;
    float coeff = -45.0 / (PI * h6);
    float hd = h - dist;
    return coeff * hd * hd * (diff / dist);
}

// 空间哈希
uint hashCell(ivec3 cell, int gridSize)
{
    cell = ((cell % gridSize) + gridSize) % gridSize;
    return uint(cell.x + cell.y * gridSize + cell.z * gridSize * gridSize);
}

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_AliveCount))
        return;

    uint myAliveIdx = gid;
    uint myParticleIdx = aliveIndices[myAliveIdx];

    // 用原始位置进行 cell 查找和压力力计算（grid 一致性）
    vec3 posI = particles[myParticleIdx].posAndLife.xyz;
    float pressureI = pcisphData[gid].predictedPosAndPressure.w;
    float densityI  = pcisphData[gid].predictedVelAndDensity.w;
    float h = u_SmoothingRadius;

    // 防止除零
    if (densityI < 0.0001) densityI = 0.0001;

    vec3 fPressure = vec3(0.0);

    ivec3 myCell = ivec3(floor(posI / u_CellSize));

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        ivec3 neighborCell = myCell + ivec3(dx, dy, dz);
        uint cellIdx = hashCell(neighborCell, u_GridSize);

        uint cCount = cellCount[cellIdx];
        if (cCount == 0u) continue;
        uint cEndRaw = cellStart[cellIdx];
        uint cEnd = min(cEndRaw, uint(u_AliveCount));
        uint cBegin = (cCount > cEnd) ? 0u : (cEnd - cCount);

        for (uint s = cBegin; s < cEnd; s++)
        {
            uint neighborAliveIdx = sortedIndices[s];
            if (neighborAliveIdx >= uint(u_AliveCount)) continue;
            uint neighborParticleIdx = aliveIndices[neighborAliveIdx];

            if (neighborParticleIdx == myParticleIdx) continue;

            vec3 posJ = particles[neighborParticleIdx].posAndLife.xyz;
            float pressureJ = pcisphData[neighborAliveIdx].predictedPosAndPressure.w;
            float densityJ  = pcisphData[neighborAliveIdx].predictedVelAndDensity.w;

            if (densityJ < 0.0001) densityJ = 0.0001;

            vec3 diff = posI - posJ;
            float dist = length(diff);

            // 压力力: f_press = -Σ m_j * (P_i + P_j) / (2 * ρ_j) * ∇W_spiky
            fPressure += -u_ParticleMass * (pressureI + pressureJ) / (2.0 * densityJ)
                         * spikyGrad(diff, dist, h);
        }
    }

    // --- 刚体 SDF 边界力 ---
    vec3 fBoundary = vec3(0.0);
    for (int rb = 0; rb < u_RigidBodyCount; rb++)
    {
        vec3 rbPos = rigidBodies[rb].posAndType.xyz;
        float rbType = rigidBodies[rb].posAndType.w;
        mat3 rbRot = mat3(rigidBodies[rb].rotCol0.xyz,
                          rigidBodies[rb].rotCol1.xyz,
                          rigidBodies[rb].rotCol2.xyz);

        vec3 localPos = transpose(rbRot) * (posI - rbPos);
        float sdf;
        vec3 localNormal;

        if (rbType < 0.5) // Box
        {
            vec3 he = rigidBodies[rb].halfExtents.xyz;
            vec3 d = abs(localPos) - he;
            sdf = length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
            vec3 s = sign(localPos);
            if (d.x > d.y && d.x > d.z)
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

        if (sdf < u_SmoothingRadius)
        {
            vec3 worldNormal = rbRot * localNormal;
            vec3 rbVel = rigidBodies[rb].linearVel.xyz
                       + cross(rigidBodies[rb].angularVel.xyz, posI - rbPos);
            vec3 velRel = pcisphData[gid].predictedVelAndDensity.xyz - rbVel;
            float penetration = max(0.0, u_SmoothingRadius - sdf);
            fBoundary += u_BoundaryStiffness * penetration * worldNormal
                       - u_BoundaryDamping * dot(velRel, worldNormal) * worldNormal;
        }
    }

    // 压力加速度
    vec3 a_pressure = (fPressure + fBoundary) / densityI;

    // 安全限幅
    float maxAccel = 500.0;
    float accelMag = length(a_pressure);
    if (accelMag > maxAccel)
        a_pressure *= maxAccel / accelMag;

    // 更新预测速度: v* += a_pressure * dt
    pcisphData[gid].predictedVelAndDensity.xyz += a_pressure * u_DeltaTime;
}

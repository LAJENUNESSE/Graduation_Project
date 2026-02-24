#type compute
#version 430 core

// SPH 力计算 — 压力梯度力 (Spiky kernel) + 粘性力 (Viscosity kernel)
// 直接将 SPH 加速度应用到粒子速度（vel += sphAccel * dt）
// simulate pass 随后照常叠加重力和阻尼

layout(local_size_x = 256) in;

struct GPUParticle
{
    vec4 posAndLife;       // xyz=position, w=remainingLife
    vec4 velAndMaxLife;    // xyz=velocity, w=maxLife
    vec4 startColor;
    vec4 endColor;
    vec4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
};

layout(std430, binding = 0) buffer ParticlePool  { GPUParticle particles[]; };
layout(std430, binding = 2) readonly buffer AliveList    { uint aliveIndices[];     };
layout(std430, binding = 5) readonly buffer CellStart    { uint cellStart[];        };
layout(std430, binding = 6) readonly buffer CellCount    { uint cellCount[];        };
layout(std430, binding = 7) readonly buffer SortedIndices { uint sortedIndices[];   };

uniform int   u_AliveCount;
uniform float u_SmoothingRadius;
uniform float u_ParticleMass;
uniform float u_Viscosity;         // μ
uniform float u_DeltaTime;
uniform int   u_GridSize;
uniform float u_CellSize;

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

// Viscosity kernel Laplacian: ∇²W_visc(r, h) = 45 / (π * h^6) * (h - |r|)
float viscLaplacian(float dist, float h)
{
    if (dist >= h) return 0.0;
    float h6 = h * h * h * h * h * h;
    return 45.0 / (PI * h6) * (h - dist);
}

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

    vec3 posI = particles[myParticleIdx].posAndLife.xyz;
    vec3 velI = particles[myParticleIdx].velAndMaxLife.xyz;
    float densityI  = particles[myParticleIdx].params.z;
    float pressureI = particles[myParticleIdx].params.w;
    float h = u_SmoothingRadius;

    // 防止除零
    if (densityI < 0.0001) densityI = 0.0001;

    vec3 fPressure = vec3(0.0);
    vec3 fViscosity = vec3(0.0);

    ivec3 myCell = ivec3(floor(posI / u_CellSize));

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        ivec3 neighborCell = myCell + ivec3(dx, dy, dz);
        uint cellIdx = hashCell(neighborCell, u_GridSize);

        uint cCount = cellCount[cellIdx];
        if (cCount == 0u) continue;
        // scatter 后: CellStart[h] = original_start + CellCount[h]
        uint cEnd   = cellStart[cellIdx];
        uint cBegin = cEnd - cCount;

        for (uint s = cBegin; s < cEnd; s++)
        {
            uint neighborAliveIdx = sortedIndices[s];
            uint neighborParticleIdx = aliveIndices[neighborAliveIdx];

            if (neighborParticleIdx == myParticleIdx) continue;

            vec3 posJ = particles[neighborParticleIdx].posAndLife.xyz;
            vec3 velJ = particles[neighborParticleIdx].velAndMaxLife.xyz;
            float densityJ  = particles[neighborParticleIdx].params.z;
            float pressureJ = particles[neighborParticleIdx].params.w;

            if (densityJ < 0.0001) densityJ = 0.0001;

            vec3 diff = posI - posJ;
            float dist = length(diff);

            // 压力力: f_press = -Σ m_j * (P_i + P_j) / (2 * ρ_j) * ∇W_spiky
            fPressure += -u_ParticleMass * (pressureI + pressureJ) / (2.0 * densityJ)
                         * spikyGrad(diff, dist, h);

            // 粘性力: f_visc = μ * Σ m_j * (v_j - v_i) / ρ_j * ∇²W_visc
            fViscosity += u_Viscosity * u_ParticleMass * (velJ - velI) / densityJ
                          * viscLaplacian(dist, h);
        }
    }

    // 总 SPH 加速度 = (fPressure + fViscosity) / ρ_i
    vec3 sphAccel = (fPressure + fViscosity) / densityI;

    // 安全限幅：防止参数极端时数值爆炸
    float maxAccel = 500.0;
    float accelMag = length(sphAccel);
    if (accelMag > maxAccel)
        sphAccel *= maxAccel / accelMag;

    // 直接应用到速度（simulate pass 之后会再叠加重力 + 阻尼）
    particles[myParticleIdx].velAndMaxLife.xyz += sphAccel * u_DeltaTime;
}

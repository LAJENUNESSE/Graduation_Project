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
layout(std430, binding = 8) readonly buffer SurfaceNormals { vec4 surfaceNormals[]; };

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

layout(std430, binding = 3) readonly buffer RigidBodyBuffer { GPURigidBody rigidBodies[]; };

struct GPUMeshSDFBody
{
    vec4 posAndType;
    vec4 rotCol0;
    vec4 rotCol1;
    vec4 rotCol2;
    vec4 invScaleAndBlend;
    vec4 localMin;
    vec4 localExtent;
    vec4 gridParams;
};

layout(std430, binding = 10) readonly buffer MeshSDFBuffer { GPUMeshSDFBody meshSDFBodies[]; };
layout(std430, binding = 11) readonly buffer MeshSDFVoxelBuffer { float meshSDFVoxels[]; };

uniform int   u_AliveCount;
uniform float u_SmoothingRadius;
uniform float u_ParticleMass;
uniform float u_Viscosity;         // μ
uniform float u_DeltaTime;
uniform int   u_GridSize;
uniform float u_CellSize;
uniform float u_SurfaceTension;     // γ, 0=关闭
uniform int   u_RigidBodyCount;     // 0=无刚体
uniform int   u_MeshSDFCount;       // 0=无 Mesh SDF
uniform int   u_MeshSDFVoxelCount;  // 体素总数
uniform float u_BoundaryStiffness;
uniform float u_BoundaryDamping;
uniform float u_SpikyCoeff;         // -45 / (π * h^6), CPU 预计算
uniform float u_WarmupTime;         // SPH warm-up 时间 (秒), 新粒子逐步受 SPH 约束

const float PI = 3.14159265359;

// Spiky kernel gradient: ∇W_spiky = u_SpikyCoeff * (h - |r|)² * (r/|r|)
vec3 spikyGrad(vec3 diff, float dist, float h)
{
    if (dist <= 0.0 || dist >= h) return vec3(0.0);
    float hd = h - dist;
    return u_SpikyCoeff * hd * hd * (diff / dist);
}

// Viscosity kernel Laplacian: ∇²W_visc = -u_SpikyCoeff * (h - |r|)
float viscLaplacian(float dist, float h)
{
    if (dist >= h) return 0.0;
    return -u_SpikyCoeff * (h - dist);
}

// Akinci C_spline 表面张力核
float C_spline(float r, float h)
{
    float q = r / h;
    float coeff = 32.0 / (PI * h * h * h);
    if (q < 0.5)
        return coeff * (2.0 * (1.0 - q) * (1.0 - q) * (1.0 - q) * q * q * q - 1.0 / 64.0);
    else if (q < 1.0)
        return coeff * ((1.0 - q) * (1.0 - q) * (1.0 - q) * q * q * q - 1.0 / 64.0);
    return 0.0;
}

uint hashCell(ivec3 cell, int gridSize)
{
    cell = ((cell % gridSize) + gridSize) % gridSize;
    return uint(cell.x + cell.y * gridSize + cell.z * gridSize * gridSize);
}

// NVIDIA/部分驱动在某些路径下对后置函数定义解析更严格，
// 显式前置声明可避免“undefined variable/function”误判。
float sampleMeshSDF(int bodyIndex, vec3 localPos);
vec3 estimateMeshSDFNormal(int bodyIndex, vec3 localPos);

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
    vec3 fSurfaceTension = vec3(0.0);

    // Akinci 表面法线（从 density pass 预计算）
    vec3 normalI = surfaceNormals[gid].xyz;

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

            // Akinci 2013 表面张力: cohesion + curvature
            if (u_SurfaceTension > 0.0 && dist > 0.001)
            {
                // 内聚力
                fSurfaceTension += -u_SurfaceTension * u_ParticleMass * C_spline(dist, h)
                                   * (diff / dist);
                // 曲率修正: f_curvature = -γ * m_j * (n_i - n_j)
                vec3 normalJ = surfaceNormals[neighborAliveIdx].xyz;
                fSurfaceTension += -u_SurfaceTension * u_ParticleMass * (normalI - normalJ);
            }
        }
    }

    // 刚体 SDF 边界力
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
            vec3 outside = max(d, 0.0);
            float outsideLen = length(outside);
            if (outsideLen > 1e-6)
                localNormal = s * outside / outsideLen;
            else if (d.x > d.y && d.x > d.z)
                localNormal = vec3(s.x, 0.0, 0.0);
            else if (d.y > d.z)
                localNormal = vec3(0.0, s.y, 0.0);
            else
                localNormal = vec3(0.0, 0.0, s.z);
        }
        else // Sphere
        {
            float radius = rigidBodies[rb].halfExtents.x;
            sdf = length(localPos) - radius;
            localNormal = length(localPos) > 0.001
                ? normalize(localPos) : vec3(0.0, 1.0, 0.0);
        }

        if (sdf < h)
        {
            vec3 worldNormal = rbRot * localNormal;
            vec3 rbVel = rigidBodies[rb].linearVel.xyz
                       + cross(rigidBodies[rb].angularVel.xyz, posI - rbPos);
            vec3 velRel = velI - rbVel;

            if (sdf < 0.0)
            {
                // Hard projection: push position back to surface
                posI += (-sdf + 0.001) * worldNormal;
                particles[myParticleIdx].posAndLife.xyz = posI;
                // Kill inward velocity component
                float vn = dot(velRel, worldNormal);
                if (vn < 0.0)
                {
                    velI -= vn * worldNormal;
                    particles[myParticleIdx].velAndMaxLife.xyz = velI;
                }
            }

            float penetration = max(0.0, h - sdf);
            fBoundary += u_BoundaryStiffness * penetration * worldNormal
                       - u_BoundaryDamping * dot(velRel, worldNormal) * worldNormal;
        }
    }

    // Mesh 体素 SDF 边界力
    for (int mb = 0; mb < u_MeshSDFCount; mb++)
    {
        if (u_MeshSDFVoxelCount <= 0)
            break;

        vec3 mbPos = meshSDFBodies[mb].posAndType.xyz;
        mat3 mbRot = mat3(meshSDFBodies[mb].rotCol0.xyz,
                          meshSDFBodies[mb].rotCol1.xyz,
                          meshSDFBodies[mb].rotCol2.xyz);

        vec3 localPos = transpose(mbRot) * (posI - mbPos);
        float sdf = sampleMeshSDF(mb, localPos);
        vec3 localNormal = estimateMeshSDFNormal(mb, localPos);

        if (sdf < h)
        {
            vec3 worldNormal = mbRot * localNormal;
            vec3 mbVel = vec3(0.0);
            float blend = clamp(meshSDFBodies[mb].invScaleAndBlend.w, 0.0, 1.0);
            vec3 velRel = velI - mbVel;

            if (sdf < 0.0)
            {
                posI += (-sdf + 0.001) * worldNormal;
                particles[myParticleIdx].posAndLife.xyz = posI;
                float vn = dot(velRel, worldNormal);
                if (vn < 0.0)
                {
                    velI -= vn * worldNormal;
                    particles[myParticleIdx].velAndMaxLife.xyz = velI;
                }
            }

            float penetration = max(0.0, h - sdf);
            vec3 f = u_BoundaryStiffness * penetration * worldNormal
                   - u_BoundaryDamping * dot(velRel, worldNormal) * worldNormal;
            fBoundary += f * blend;
        }
    }

    // SPH warm-up: 新生粒子逐步受压力影响，避免爆发时密度冲击导致闪烁
    float life    = particles[myParticleIdx].posAndLife.w;
    float maxLife = particles[myParticleIdx].velAndMaxLife.w;
    float age     = maxLife - life;
    float warmup  = (u_WarmupTime > 0.0) ? clamp(age / u_WarmupTime, 0.0, 1.0) : 1.0;

    // SPH 加速度 = (fPressure * warmup + fViscosity + fSurfaceTension) / ρ_i
    // fBoundary 作为直接加速度，不除密度——保证碰撞力足够抵抗高速粒子
    vec3 sphAccel = (fPressure * warmup + fViscosity + fSurfaceTension) / densityI + fBoundary;

    // 安全限幅：防止参数极端时数值爆炸
    float maxAccel = 500.0;
    float accelMag = length(sphAccel);
    if (accelMag > maxAccel)
        sphAccel *= maxAccel / accelMag;

    // 直接应用到速度（simulate pass 之后会再叠加重力 + 阻尼）
    particles[myParticleIdx].velAndMaxLife.xyz += sphAccel * u_DeltaTime;
}
float sampleMeshSDF(int bodyIndex, vec3 localPos)
{
    float resolutionF = meshSDFBodies[bodyIndex].gridParams.x;
    int   resolution  = max(int(resolutionF), 1);
    int   voxelOffset = int(meshSDFBodies[bodyIndex].gridParams.y);
    int   voxelCount  = int(meshSDFBodies[bodyIndex].gridParams.z);
    vec3  localMin    = meshSDFBodies[bodyIndex].localMin.xyz;
    vec3  localExtent = max(meshSDFBodies[bodyIndex].localExtent.xyz, vec3(1e-5));

    vec3 uvw = clamp((localPos - localMin) / localExtent, vec3(0.0), vec3(1.0));
    vec3 g   = uvw * vec3(float(resolution - 1));

    ivec3 i0 = ivec3(floor(g));
    ivec3 i1 = min(i0 + ivec3(1), ivec3(resolution - 1));
    vec3  t  = fract(g);

    int plane = resolution * resolution;
    int idx000 = i0.z * plane + i0.y * resolution + i0.x;
    int idx100 = i0.z * plane + i0.y * resolution + i1.x;
    int idx010 = i0.z * plane + i1.y * resolution + i0.x;
    int idx110 = i0.z * plane + i1.y * resolution + i1.x;
    int idx001 = i1.z * plane + i0.y * resolution + i0.x;
    int idx101 = i1.z * plane + i0.y * resolution + i1.x;
    int idx011 = i1.z * plane + i1.y * resolution + i0.x;
    int idx111 = i1.z * plane + i1.y * resolution + i1.x;

    idx000 = clamp(idx000, 0, max(voxelCount - 1, 0));
    idx100 = clamp(idx100, 0, max(voxelCount - 1, 0));
    idx010 = clamp(idx010, 0, max(voxelCount - 1, 0));
    idx110 = clamp(idx110, 0, max(voxelCount - 1, 0));
    idx001 = clamp(idx001, 0, max(voxelCount - 1, 0));
    idx101 = clamp(idx101, 0, max(voxelCount - 1, 0));
    idx011 = clamp(idx011, 0, max(voxelCount - 1, 0));
    idx111 = clamp(idx111, 0, max(voxelCount - 1, 0));

    float c000 = meshSDFVoxels[voxelOffset + idx000];
    float c100 = meshSDFVoxels[voxelOffset + idx100];
    float c010 = meshSDFVoxels[voxelOffset + idx010];
    float c110 = meshSDFVoxels[voxelOffset + idx110];
    float c001 = meshSDFVoxels[voxelOffset + idx001];
    float c101 = meshSDFVoxels[voxelOffset + idx101];
    float c011 = meshSDFVoxels[voxelOffset + idx011];
    float c111 = meshSDFVoxels[voxelOffset + idx111];

    float c00 = mix(c000, c100, t.x);
    float c10 = mix(c010, c110, t.x);
    float c01 = mix(c001, c101, t.x);
    float c11 = mix(c011, c111, t.x);
    float c0  = mix(c00, c10, t.y);
    float c1  = mix(c01, c11, t.y);
    return mix(c0, c1, t.z);
}

vec3 estimateMeshSDFNormal(int bodyIndex, vec3 localPos)
{
    vec3 localExtent = max(meshSDFBodies[bodyIndex].localExtent.xyz, vec3(1e-5));
    float resolution = max(meshSDFBodies[bodyIndex].gridParams.x, 1.0);
    vec3 e = localExtent / resolution;
    float dx = sampleMeshSDF(bodyIndex, localPos + vec3(e.x, 0.0, 0.0)) -
               sampleMeshSDF(bodyIndex, localPos - vec3(e.x, 0.0, 0.0));
    float dy = sampleMeshSDF(bodyIndex, localPos + vec3(0.0, e.y, 0.0)) -
               sampleMeshSDF(bodyIndex, localPos - vec3(0.0, e.y, 0.0));
    float dz = sampleMeshSDF(bodyIndex, localPos + vec3(0.0, 0.0, e.z)) -
               sampleMeshSDF(bodyIndex, localPos - vec3(0.0, 0.0, e.z));
    vec3 n = vec3(dx, dy, dz);
    return (dot(n, n) > 1e-10) ? normalize(n) : vec3(0.0, 1.0, 0.0);
}

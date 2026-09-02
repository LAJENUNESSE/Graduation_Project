#include "SPHCommon.hlsli"

RWStructuredBuffer<GPUParticle>   particles       : register(u0);
StructuredBuffer<uint>            aliveIndices    : register(t0);
StructuredBuffer<uint>            cellStart       : register(t1);
StructuredBuffer<uint>            cellCount       : register(t2);
StructuredBuffer<uint>            sortedIndices   : register(t3);
StructuredBuffer<float4>          surfaceNormals  : register(t4);
StructuredBuffer<GPURigidBody>    rigidBodies     : register(t5);
StructuredBuffer<GPUMeshSDFBody>  meshSDFBodies   : register(t6);
StructuredBuffer<float>           meshSDFVoxels   : register(t7);

float sampleMeshSDF(int bodyIndex, float3 localPos)
{
    float resolutionF = meshSDFBodies[bodyIndex].gridParams.x;
    int resolution = max((int)resolutionF, 1);
    int voxelOffset = (int)meshSDFBodies[bodyIndex].gridParams.y;
    int voxelCount = (int)meshSDFBodies[bodyIndex].gridParams.z;
    float3 localMin = meshSDFBodies[bodyIndex].localMin.xyz;
    float3 localExtent = max(meshSDFBodies[bodyIndex].localExtent.xyz, float3(1e-5f, 1e-5f, 1e-5f));

    float3 uvw = saturate((localPos - localMin) / localExtent);
    float3 g = uvw * (float)(resolution - 1);

    int3 i0 = (int3)floor(g);
    int3 i1 = min(i0 + 1, resolution - 1);
    float3 t = frac(g);

    int plane = resolution * resolution;
    int idx000 = i0.z * plane + i0.y * resolution + i0.x;
    int idx100 = i0.z * plane + i0.y * resolution + i1.x;
    int idx010 = i0.z * plane + i1.y * resolution + i0.x;
    int idx110 = i0.z * plane + i1.y * resolution + i1.x;
    int idx001 = i1.z * plane + i0.y * resolution + i0.x;
    int idx101 = i1.z * plane + i0.y * resolution + i1.x;
    int idx011 = i1.z * plane + i1.y * resolution + i0.x;
    int idx111 = i1.z * plane + i1.y * resolution + i1.x;

    int maxIdx = max(voxelCount - 1, 0);
    idx000 = clamp(idx000, 0, maxIdx);
    idx100 = clamp(idx100, 0, maxIdx);
    idx010 = clamp(idx010, 0, maxIdx);
    idx110 = clamp(idx110, 0, maxIdx);
    idx001 = clamp(idx001, 0, maxIdx);
    idx101 = clamp(idx101, 0, maxIdx);
    idx011 = clamp(idx011, 0, maxIdx);
    idx111 = clamp(idx111, 0, maxIdx);

    float c000 = meshSDFVoxels[voxelOffset + idx000];
    float c100 = meshSDFVoxels[voxelOffset + idx100];
    float c010 = meshSDFVoxels[voxelOffset + idx010];
    float c110 = meshSDFVoxels[voxelOffset + idx110];
    float c001 = meshSDFVoxels[voxelOffset + idx001];
    float c101 = meshSDFVoxels[voxelOffset + idx101];
    float c011 = meshSDFVoxels[voxelOffset + idx011];
    float c111 = meshSDFVoxels[voxelOffset + idx111];

    float c00 = lerp(c000, c100, t.x);
    float c10 = lerp(c010, c110, t.x);
    float c01 = lerp(c001, c101, t.x);
    float c11 = lerp(c011, c111, t.x);
    float c0  = lerp(c00, c10, t.y);
    float c1  = lerp(c01, c11, t.y);
    return lerp(c0, c1, t.z);
}

float3 estimateMeshSDFNormal(int bodyIndex, float3 localPos)
{
    float3 localExtent = max(meshSDFBodies[bodyIndex].localExtent.xyz, float3(1e-5f, 1e-5f, 1e-5f));
    float resolution = max(meshSDFBodies[bodyIndex].gridParams.x, 1.0f);
    float3 e = localExtent / resolution;
    float dx = sampleMeshSDF(bodyIndex, localPos + float3(e.x, 0.0f, 0.0f)) -
               sampleMeshSDF(bodyIndex, localPos - float3(e.x, 0.0f, 0.0f));
    float dy = sampleMeshSDF(bodyIndex, localPos + float3(0.0f, e.y, 0.0f)) -
               sampleMeshSDF(bodyIndex, localPos - float3(0.0f, e.y, 0.0f));
    float dz = sampleMeshSDF(bodyIndex, localPos + float3(0.0f, 0.0f, e.z)) -
               sampleMeshSDF(bodyIndex, localPos - float3(0.0f, 0.0f, e.z));
    float3 n = float3(dx, dy, dz);
    return (dot(n, n) > 1e-10f) ? normalize(n) : float3(0.0f, 1.0f, 0.0f);
}

[numthreads(256, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint gid = dispatchThreadId.x;
    if (gid >= u_AliveCount)
        return;

    uint myAliveIdx = gid;
    uint myParticleIdx = aliveIndices[myAliveIdx];

    float3 posI = particles[myParticleIdx].posAndLife.xyz;
    float3 velI = particles[myParticleIdx].velAndMaxLife.xyz;
    float densityI  = particles[myParticleIdx].params.z;
    float pressureI = particles[myParticleIdx].params.w;
    float h = u_SmoothingRadius;

    if (densityI < 0.0001f) densityI = 0.0001f;

    float3 fPressure = float3(0.0f, 0.0f, 0.0f);
    float3 fViscosity = float3(0.0f, 0.0f, 0.0f);
    float3 fSurfaceTension = float3(0.0f, 0.0f, 0.0f);

    float3 normalI = surfaceNormals[gid].xyz;

    int3 myCell = (int3)floor(posI / u_CellSize);

    [unroll]
    for (int dz = -1; dz <= 1; dz++)
    {
        [unroll]
        for (int dy = -1; dy <= 1; dy++)
        {
            [unroll]
            for (int dx = -1; dx <= 1; dx++)
            {
                int3 neighborCell = myCell + int3(dx, dy, dz);
                uint cellIdx = hashCell(neighborCell, u_GridSize);

                uint cCount = cellCount[cellIdx];
                if (cCount == 0u) continue;
                uint cEndRaw = cellStart[cellIdx];
                uint cEnd = min(cEndRaw, u_AliveCount);
                uint cBegin = (cCount > cEnd) ? 0u : (cEnd - cCount);

                for (uint s = cBegin; s < cEnd; s++)
                {
                    uint neighborAliveIdx = sortedIndices[s];
                    if (neighborAliveIdx >= u_AliveCount) continue;
                    uint neighborParticleIdx = aliveIndices[neighborAliveIdx];

                    if (neighborParticleIdx == myParticleIdx) continue;

                    float3 posJ = particles[neighborParticleIdx].posAndLife.xyz;
                    float3 velJ = particles[neighborParticleIdx].velAndMaxLife.xyz;
                    float densityJ  = particles[neighborParticleIdx].params.z;
                    float pressureJ = particles[neighborParticleIdx].params.w;

                    if (densityJ < 0.0001f) densityJ = 0.0001f;

                    float3 diff = posI - posJ;
                    float dist = length(diff);

                    fPressure += -u_ParticleMass * (pressureI + pressureJ) / (2.0f * densityJ)
                                 * spikyGrad(diff, dist, h);

                    fViscosity += u_Viscosity * u_ParticleMass * (velJ - velI) / densityJ
                                  * viscLaplacian(dist, h);

                    if (u_SurfaceTension > 0.0f && dist > 0.001f)
                    {
                        fSurfaceTension += -u_SurfaceTension * u_ParticleMass * C_spline(dist, h) * (diff / dist);
                        float3 normalJ = surfaceNormals[neighborAliveIdx].xyz;
                        fSurfaceTension += -u_SurfaceTension * u_ParticleMass * (normalI - normalJ);
                    }
                }
            }
        }
    }

    // Rigid body SDF boundary force
    float3 fBoundary = float3(0.0f, 0.0f, 0.0f);
    for (int rb = 0; rb < u_RigidBodyCount; rb++)
    {
        float3 rbPos = rigidBodies[rb].posAndType.xyz;
        float rbType = rigidBodies[rb].posAndType.w;

        float3 localPos = worldToLocal(posI - rbPos, rigidBodies[rb].rotCol0, rigidBodies[rb].rotCol1, rigidBodies[rb].rotCol2);
        float sdf;
        float3 localNormal;

        if (rbType < 0.5f) // Box
        {
            float3 he = rigidBodies[rb].halfExtents.xyz;
            float3 d = abs(localPos) - he;
            sdf = length(max(d, 0.0f)) + min(max(d.x, max(d.y, d.z)), 0.0f);
            float3 s = sign(localPos);
            float3 outside = max(d, 0.0f);
            float outsideLen = length(outside);
            if (outsideLen > 1e-6f)
                localNormal = s * outside / outsideLen;
            else if (d.x > d.y && d.x > d.z)
                localNormal = float3(s.x, 0.0f, 0.0f);
            else if (d.y > d.z)
                localNormal = float3(0.0f, s.y, 0.0f);
            else
                localNormal = float3(0.0f, 0.0f, s.z);
        }
        else // Sphere
        {
            float radius = rigidBodies[rb].halfExtents.x;
            sdf = length(localPos) - radius;
            localNormal = (length(localPos) > 0.001f) ? normalize(localPos) : float3(0.0f, 1.0f, 0.0f);
        }

        if (sdf < h)
        {
            float3 worldNormal = localToWorld(localNormal, rigidBodies[rb].rotCol0, rigidBodies[rb].rotCol1, rigidBodies[rb].rotCol2);
            float3 rbVel = rigidBodies[rb].linearVel.xyz + cross(rigidBodies[rb].angularVel.xyz, posI - rbPos);
            float3 velRel = velI - rbVel;

            if (sdf < 0.0f)
            {
                posI += (-sdf + 0.001f) * worldNormal;
                particles[myParticleIdx].posAndLife.xyz = posI;
                float vn = dot(velRel, worldNormal);
                if (vn < 0.0f)
                {
                    velI -= vn * worldNormal;
                    particles[myParticleIdx].velAndMaxLife.xyz = velI;
                }
            }

            float penetration = max(0.0f, h - sdf);
            fBoundary += u_BoundaryStiffness * penetration * worldNormal
                       - u_BoundaryDamping * dot(velRel, worldNormal) * worldNormal;
        }
    }

    // Mesh voxel SDF boundary force
    for (int mb = 0; mb < u_MeshSDFCount; mb++)
    {
        if (u_MeshSDFVoxelCount <= 0)
            break;

        float3 mbPos = meshSDFBodies[mb].posAndType.xyz;
        float3 localPos = worldToLocal(posI - mbPos, meshSDFBodies[mb].rotCol0, meshSDFBodies[mb].rotCol1, meshSDFBodies[mb].rotCol2);
        float sdf = sampleMeshSDF(mb, localPos);
        float3 localNormal = estimateMeshSDFNormal(mb, localPos);

        if (sdf < h)
        {
            float3 worldNormal = localToWorld(localNormal, meshSDFBodies[mb].rotCol0, meshSDFBodies[mb].rotCol1, meshSDFBodies[mb].rotCol2);
            float3 mbVel = float3(0.0f, 0.0f, 0.0f);
            float blend = saturate(meshSDFBodies[mb].invScaleAndBlend.w);
            float3 velRel = velI - mbVel;

            if (sdf < 0.0f)
            {
                posI += (-sdf + 0.001f) * worldNormal;
                particles[myParticleIdx].posAndLife.xyz = posI;
                float vn = dot(velRel, worldNormal);
                if (vn < 0.0f)
                {
                    velI -= vn * worldNormal;
                    particles[myParticleIdx].velAndMaxLife.xyz = velI;
                }
            }

            float penetration = max(0.0f, h - sdf);
            float3 f = u_BoundaryStiffness * penetration * worldNormal
                     - u_BoundaryDamping * dot(velRel, worldNormal) * worldNormal;
            fBoundary += f * blend;
        }
    }

    float life = particles[myParticleIdx].posAndLife.w;
    float maxLife = particles[myParticleIdx].velAndMaxLife.w;
    float age = maxLife - life;
    float warmup = (u_WarmupTime > 0.0f) ? saturate(age / u_WarmupTime) : 1.0f;

    float3 sphAccel = (fPressure * warmup + fViscosity + fSurfaceTension) / densityI + fBoundary;

    float maxAccel = 500.0f;
    float accelMag = length(sphAccel);
    if (accelMag > maxAccel)
        sphAccel *= maxAccel / accelMag;

    particles[myParticleIdx].velAndMaxLife.xyz += sphAccel * u_DeltaTime;
}

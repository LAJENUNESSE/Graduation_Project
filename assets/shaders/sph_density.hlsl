#include "SPHCommon.hlsli"

RWStructuredBuffer<GPUParticle>   particles      : register(u0);
RWStructuredBuffer<float4>        surfaceNormals : register(u1);
StructuredBuffer<uint>            aliveIndices   : register(t0);
StructuredBuffer<uint>            cellStart      : register(t1);
StructuredBuffer<uint>            cellCount      : register(t2);
StructuredBuffer<uint>            sortedIndices  : register(t3);

[numthreads(256, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint gid = dispatchThreadId.x;
    if (gid >= u_AliveCount)
        return;

    uint myAliveIdx = gid;
    uint myParticleIdx = aliveIndices[myAliveIdx];

    float3 posI = particles[myParticleIdx].posAndLife.xyz;
    float h = u_SmoothingRadius;

    // Self contribution
    float density = u_ParticleMass * poly6(0.0f, h);

    // Akinci surface normal: n_i = h * sum((m_j / rho_j) * grad W_spiky(r_ij))
    float3 surfaceNormal = float3(0.0f, 0.0f, 0.0f);

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
                    float3 diff = posI - posJ;
                    float r2 = dot(diff, diff);

                    density += u_ParticleMass * poly6(r2, h);

                    if (u_SurfaceTension > 0.0f)
                    {
                        float dist = sqrt(r2);
                        float densityJ = particles[neighborParticleIdx].params.z;
                        if (densityJ < 0.0001f) densityJ = 0.0001f;
                        surfaceNormal += (u_ParticleMass / densityJ) * spikyGrad(diff, dist, h);
                    }
                }
            }
        }
    }

    // Equation of state: P = k * (rho - rho_0), clamped to prevent abnormal attraction
    float pressure = max(0.0f, u_GasConstant * (density - u_RestDensity));

    particles[myParticleIdx].params.z = density;
    particles[myParticleIdx].params.w = pressure;

    surfaceNormals[gid] = (u_SurfaceTension > 0.0f) ? float4(h * surfaceNormal, 0.0f) : float4(0.0f, 0.0f, 0.0f, 0.0f);
}

#include "SPHCommon.hlsli"

StructuredBuffer<GPUParticle>   particles       : register(t0);
RWStructuredBuffer<PCISPHData>  pcisphData      : register(u0);
StructuredBuffer<uint>          aliveIndices    : register(t1);
StructuredBuffer<uint>          cellStart       : register(t2);
StructuredBuffer<uint>          cellCount       : register(t3);
StructuredBuffer<uint>          sortedIndices   : register(t4);
StructuredBuffer<float4>        surfaceNormals  : register(t5);

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
    float densityI = particles[myParticleIdx].params.z;
    float h = u_SmoothingRadius;

    if (densityI < 0.0001f) densityI = 0.0001f;

    float3 fViscosity = float3(0.0f, 0.0f, 0.0f);
    float3 fSurfTension = float3(0.0f, 0.0f, 0.0f);

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
                    float densityJ = particles[neighborParticleIdx].params.z;

                    if (densityJ < 0.0001f) densityJ = 0.0001f;

                    float3 diff = posI - posJ;
                    float dist = length(diff);

                    fViscosity += u_Viscosity * u_ParticleMass * (velJ - velI) / densityJ
                                  * viscLaplacian(dist, h);

                    if (u_SurfaceTension > 0.0f && dist > 0.001f)
                    {
                        fSurfTension += -u_SurfaceTension * u_ParticleMass * C_spline(dist, h) * (diff / dist);
                        float3 normalJ = surfaceNormals[neighborAliveIdx].xyz;
                        fSurfTension += -u_SurfaceTension * u_ParticleMass * (normalI - normalJ);
                    }
                }
            }
        }
    }

    float3 a_np = (fViscosity + fSurfTension) / densityI + u_Gravity;

    float maxAccel = 500.0f;
    float accelMag = length(a_np);
    if (accelMag > maxAccel)
        a_np *= maxAccel / accelMag;

    pcisphData[gid].nonPressureAccel.xyz = a_np;
    pcisphData[gid].predictedVelAndDensity.xyz = velI + a_np * u_DeltaTime;
    pcisphData[gid].predictedPosAndPressure.w = 0.0f;
    pcisphData[gid].predictedVelAndDensity.w = 0.0f;
}

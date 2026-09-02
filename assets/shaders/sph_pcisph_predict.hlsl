#include "SPHCommon.hlsli"

StructuredBuffer<GPUParticle>   particles    : register(t0);
RWStructuredBuffer<PCISPHData>  pcisphData   : register(u0);
StructuredBuffer<uint>          aliveIndices : register(t1);

[numthreads(256, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint gid = dispatchThreadId.x;
    if (gid >= u_AliveCount)
        return;

    uint myParticleIdx = aliveIndices[gid];

    float3 pos   = particles[myParticleIdx].posAndLife.xyz;
    float3 vStar = pcisphData[gid].predictedVelAndDensity.xyz;

    pcisphData[gid].predictedPosAndPressure.xyz = pos + u_DeltaTime * vStar;
}

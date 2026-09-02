#include "SPHCommon.hlsli"

RWStructuredBuffer<GPUParticle> particles    : register(u0);
StructuredBuffer<PCISPHData>    pcisphData   : register(t0);
StructuredBuffer<uint>          aliveIndices : register(t1);

[numthreads(256, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint gid = dispatchThreadId.x;
    if (gid >= u_AliveCount)
        return;

    uint myParticleIdx = aliveIndices[gid];

    particles[myParticleIdx].velAndMaxLife.xyz = pcisphData[gid].predictedVelAndDensity.xyz;
    particles[myParticleIdx].posAndLife.xyz    = pcisphData[gid].predictedPosAndPressure.xyz;
}

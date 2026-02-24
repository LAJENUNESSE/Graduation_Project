#type compute
#version 430 core

// Multi-mode Blelloch exclusive prefix sum
// u_Mode=0: Local scan — CellCount → CellStart + BlockSums
// u_Mode=1: Scan block sums — BlockSums in-place
// u_Mode=2: Propagate — add BlockSums to CellStart (skip block 0)
layout(local_size_x = 256) in;

layout(std430, binding = 5) buffer CellStart { uint cellStart[]; };
layout(std430, binding = 6) buffer CellCount { uint cellCount[]; };
layout(std430, binding = 4) buffer BlockSums { uint blockSums[]; };

uniform int u_Mode;
uniform int u_N;

shared uint temp[512];

void main()
{
    uint tid = gl_LocalInvocationID.x;
    uint blockOffset = gl_WorkGroupID.x * 512u;
    uint ai = tid;
    uint bi = tid + 256u;
    uint globalAI = blockOffset + ai;
    uint globalBI = blockOffset + bi;

    // ---- Mode 2: Propagate block sums ----
    if (u_Mode == 2)
    {
        if (gl_WorkGroupID.x == 0u) return;
        uint blockAdd = blockSums[gl_WorkGroupID.x];
        if (globalAI < uint(u_N)) cellStart[globalAI] += blockAdd;
        if (globalBI < uint(u_N)) cellStart[globalBI] += blockAdd;
        return;
    }

    // ---- Mode 0/1: Blelloch scan ----
    // Load input
    if (u_Mode == 0)
    {
        temp[ai] = (globalAI < uint(u_N)) ? cellCount[globalAI] : 0u;
        temp[bi] = (globalBI < uint(u_N)) ? cellCount[globalBI] : 0u;
    }
    else // u_Mode == 1
    {
        temp[ai] = (globalAI < uint(u_N)) ? blockSums[globalAI] : 0u;
        temp[bi] = (globalBI < uint(u_N)) ? blockSums[globalBI] : 0u;
    }

    // Up-sweep (reduce)
    uint offset = 1u;
    for (uint d = 256u; d > 0u; d >>= 1u)
    {
        barrier();
        if (tid < d)
        {
            uint a = offset * (2u * tid + 1u) - 1u;
            uint b = offset * (2u * tid + 2u) - 1u;
            temp[b] += temp[a];
        }
        offset <<= 1u;
    }

    // Save block total, then clear last element
    barrier();
    if (tid == 0u)
    {
        if (u_Mode == 0)
            blockSums[gl_WorkGroupID.x] = temp[511];
        temp[511] = 0u;
    }

    // Down-sweep
    for (uint d = 1u; d <= 256u; d <<= 1u)
    {
        offset >>= 1u;
        barrier();
        if (tid < d)
        {
            uint a = offset * (2u * tid + 1u) - 1u;
            uint b = offset * (2u * tid + 2u) - 1u;
            uint t = temp[a];
            temp[a] = temp[b];
            temp[b] += t;
        }
    }

    barrier();

    // Write output
    if (u_Mode == 0)
    {
        if (globalAI < uint(u_N)) cellStart[globalAI] = temp[ai];
        if (globalBI < uint(u_N)) cellStart[globalBI] = temp[bi];
    }
    else // u_Mode == 1
    {
        if (globalAI < uint(u_N)) blockSums[globalAI] = temp[ai];
        if (globalBI < uint(u_N)) blockSums[globalBI] = temp[bi];
    }
}

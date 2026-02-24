#type compute
#version 430 core

layout(local_size_x = 1) in;

layout(std430, binding = 3) buffer CounterBuffer {
    uint deadCount;
    uint aliveCount;
    uint emitCount;
    uint pad;
} counters;

// DrawArraysIndirectCommand: count, instanceCount, first, baseInstance
layout(std430, binding = 4) buffer IndirectArgs {
    uint vertexCount;      // = 6 (billboard quad)
    uint instanceCount;    // = aliveCount
    uint firstVertex;      // = 0
    uint baseInstance;     // = 0
} drawCmd;

void main()
{
    drawCmd.instanceCount = counters.aliveCount;
}

#type compute
#version 430 core

layout(local_size_x = 1) in;

#ifdef VULKAN
layout(std430, set = 0, binding = 3) buffer CounterBuffer {
    uint deadCount;
    uint aliveCount;
    uint emitCount;
    uint pad;
} counters;

// DrawArraysIndirectCommand: count, instanceCount, first, baseInstance
layout(std430, set = 0, binding = 4) buffer IndirectArgs {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint baseInstance;
} drawCmd;

layout(push_constant) uniform PushConstants
{
    uint u_MaxParticles;
} pc;
#else
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
#endif

void main()
{
    drawCmd.instanceCount = counters.aliveCount;
}

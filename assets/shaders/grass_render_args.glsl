#type compute
#version 430 core

layout(local_size_x = 1) in;

layout(std430, binding = 3) readonly buffer Counter { uint grassCount; };

// DrawArraysIndirectCommand
layout(std430, binding = 4) writeonly buffer IndirectArgs
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint baseInstance;
};

void main()
{
    vertexCount   = 6u;
    instanceCount = grassCount;
    firstVertex   = 0u;
    baseInstance   = 0u;
}

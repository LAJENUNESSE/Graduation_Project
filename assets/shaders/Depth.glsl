#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;

#ifdef VULKAN
// Vulkan path：两个矩阵合计 128B，恰等于 push constant 最小保证
layout(push_constant) uniform DepthPushConstants
{
    mat4 u_LightSpaceMatrix;
    mat4 u_Transform;
};
#else
uniform mat4 u_LightSpaceMatrix;
uniform mat4 u_Transform;
#endif

void main() {
    gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
}

#type fragment
#version 330 core
void main() {
    // Depth is written automatically
}

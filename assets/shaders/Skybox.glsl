#type vertex
#version 330 core
// GLSL 330 下插值变量显式 location 需要本扩展（GLSL 410 起内建；
// Vulkan 分支不需要——由运行时编译统一提升到 450）
#ifndef VULKAN
#extension GL_ARB_separate_shader_objects : enable
#endif

// GLSL 330 下 VS 插值输出的显式 location 需要本扩展（GLSL 410 起内建；
// Vulkan 分支不需要——由运行时编译统一提升到 450）
#ifndef VULKAN
#extension GL_ARB_separate_shader_objects : enable
#endif
layout(location = 0) in vec3 a_Position;

#ifdef VULKAN
// Vulkan path：单一矩阵走 push constant（64B ≤ 最小保证 128B）
layout(push_constant) uniform SkyboxPushConstants
{
    mat4 u_ViewProjection;
};
#else
uniform mat4 u_ViewProjection;
#endif

layout(location = 0) out vec3 v_TexCoords;

void main() {
    v_TexCoords = a_Position;
    vec4 pos = u_ViewProjection * vec4(a_Position, 1.0);
    gl_Position = pos.xyww;
}

#type fragment
#version 330 core
// GLSL 330 下插值变量显式 location 需要本扩展（GLSL 410 起内建；
// Vulkan 分支不需要——由运行时编译统一提升到 450）
#ifndef VULKAN
#extension GL_ARB_separate_shader_objects : enable
#endif
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

#ifdef VULKAN
// Vulkan path：显式 layout，由 VulkanShader 反射建 descriptor set layout
layout(set = 0, binding = 0) uniform samplerCube u_Skybox;
#else
uniform samplerCube u_Skybox;  // unit 0 由 cpp 端 Bind 指定
#endif

layout(location = 0) in vec3 v_TexCoords;

void main() {
    vec4 texColor = texture(u_Skybox, v_TexCoords);
    texColor.rgb = pow(texColor.rgb, vec3(2.2));  // sRGB -> Linear
    o_Color = texColor;
    o_EntityID = -1;
}

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
layout(location = 1) in vec3 a_Color;

#ifdef VULKAN
layout(push_constant) uniform DebugLinePC
{
    mat4 u_ViewProjection;
};
#else
uniform mat4 u_ViewProjection;
#endif

layout(location = 0) out vec3 v_Color;

void main() {
    v_Color = a_Color;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
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

layout(location = 0) in vec3 v_Color;

void main() {
    o_Color = vec4(v_Color, 1.0);
    o_EntityID = -1;
}

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
layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 0) out vec2 v_TexCoord;
void main() {
    v_TexCoord = a_TexCoord;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

#type fragment
#version 330 core
// GLSL 330 下插值变量显式 location 需要本扩展（GLSL 410 起内建；
// Vulkan 分支不需要——由运行时编译统一提升到 450）
#ifndef VULKAN
#extension GL_ARB_separate_shader_objects : enable
#endif
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

#ifdef VULKAN
layout(set = 0, binding = 0) uniform sampler2D u_HDRBuffer;
layout(push_constant) uniform BrightnessExtractPC
{
    float u_Threshold;
};
#else
uniform sampler2D u_HDRBuffer;
uniform float u_Threshold;
#endif

void main() {
    vec3 color = texture(u_HDRBuffer, v_TexCoord).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    FragColor = (brightness > u_Threshold) ? vec4(color, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}

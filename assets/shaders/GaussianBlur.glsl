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
layout(set = 0, binding = 0) uniform sampler2D u_Image;
layout(push_constant) uniform GaussianBlurPC
{
    int u_Horizontal;
};
#else
uniform sampler2D u_Image;
uniform int u_Horizontal;
#endif

// 5-tap Gaussian weights
const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    vec2 texOffset = 1.0 / textureSize(u_Image, 0);
    vec3 result = texture(u_Image, v_TexCoord).rgb * weight[0];

    if (u_Horizontal != 0) {
        for (int i = 1; i < 5; ++i) {
            result += texture(u_Image, v_TexCoord + vec2(texOffset.x * float(i), 0.0)).rgb * weight[i];
            result += texture(u_Image, v_TexCoord - vec2(texOffset.x * float(i), 0.0)).rgb * weight[i];
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            result += texture(u_Image, v_TexCoord + vec2(0.0, texOffset.y * float(i))).rgb * weight[i];
            result += texture(u_Image, v_TexCoord - vec2(0.0, texOffset.y * float(i))).rgb * weight[i];
        }
    }
    FragColor = vec4(result, 1.0);
}

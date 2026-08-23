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
// Vulkan path：显式 layout + 小参数 push constant
layout(set = 0, binding = 0) uniform sampler2D u_HDRBuffer;
layout(set = 0, binding = 1) uniform sampler2D u_BloomBlur;

layout(push_constant) uniform ToneMappingPushConstants
{
    float u_BloomStrength;
    int   u_ToneMappingMode;
    int   u_GammaCorrection;
    int   u_BloomEnabled;
};
#else
uniform sampler2D u_HDRBuffer;
uniform sampler2D u_BloomBlur;
uniform float u_BloomStrength;
uniform int u_ToneMappingMode;
uniform int u_GammaCorrection;
uniform int u_BloomEnabled;
#endif

// ACES Filmic Tone Mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdrColor = texture(u_HDRBuffer, v_TexCoord).rgb;

    // Add bloom
    if (u_BloomEnabled != 0) {
        vec3 bloomColor = texture(u_BloomBlur, v_TexCoord).rgb;
        hdrColor += bloomColor * u_BloomStrength;
    }

    // Tone mapping
    vec3 mapped;
    if (u_ToneMappingMode == 0) {
        // Reinhard
        mapped = hdrColor / (hdrColor + vec3(1.0));
    } else {
        // ACES
        mapped = ACESFilm(hdrColor);
    }

    // Gamma correction
    if (u_GammaCorrection != 0)
        mapped = pow(mapped, vec3(1.0 / 2.2));

    FragColor = vec4(mapped, 1.0);
}

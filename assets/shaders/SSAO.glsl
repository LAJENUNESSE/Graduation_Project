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
layout(location = 0) out float o_Occlusion;

layout(location = 0) in vec2 v_TexCoord;

#ifdef VULKAN
layout(set = 0, binding = 0) uniform sampler2D u_DepthTexture;
layout(set = 0, binding = 1) uniform sampler2D u_NoiseTexture;

// std140：相机 + SSAO 参数 + 64 个采样核
#define MAX_KERNEL_SIZE 64
layout(std140, set = 0, binding = 2) uniform SSAOParamsUBO
{
    mat4  u_Projection;
    mat4  u_InvProjection;
    vec2  u_ScreenSize;
    int   u_KernelSize;
    float u_Radius;
    float u_Bias;
    float u_Intensity;
    vec3  u_Samples[MAX_KERNEL_SIZE];   // std140 stride 16
};
#else
// 场景深度纹理（depth-only）
uniform sampler2D u_DepthTexture;

// 噪声纹理（4x4 小纹理平铺）
uniform sampler2D u_NoiseTexture;

// 相机参数
uniform mat4 u_Projection;
uniform mat4 u_InvProjection;
uniform vec2 u_ScreenSize;

// SSAO 参数
uniform int u_KernelSize;
uniform float u_Radius;
uniform float u_Bias;
uniform float u_Intensity;

// 采样核（view-space 半球方向）
#define MAX_KERNEL_SIZE 64
uniform vec3 u_Samples[MAX_KERNEL_SIZE];
#endif

// 从深度值重建 view-space 位置
vec3 ReconstructViewPos(vec2 uv)
{
    float depth = texture(u_DepthTexture, uv).r;
    // NDC
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = u_InvProjection * ndc;
    return viewPos.xyz / viewPos.w;
}

void main()
{
    vec3 fragPos = ReconstructViewPos(v_TexCoord);

    // 如果深度太远（天空等），直接返回 1.0（无遮蔽）
    float rawDepth = texture(u_DepthTexture, v_TexCoord).r;
    if (rawDepth >= 1.0)
    {
        o_Occlusion = 1.0;
        return;
    }

    // 通过临近像素差分近似 view-space 法线
    vec3 fragPosRight = ReconstructViewPos(v_TexCoord + vec2(1.0 / u_ScreenSize.x, 0.0));
    vec3 fragPosUp    = ReconstructViewPos(v_TexCoord + vec2(0.0, 1.0 / u_ScreenSize.y));
    vec3 normal = normalize(cross(fragPosRight - fragPos, fragPosUp - fragPos));

    // 噪声旋转向量（平铺 4x4 噪声纹理）
    vec2 noiseScale = u_ScreenSize / 4.0;
    vec3 randomVec = normalize(texture(u_NoiseTexture, v_TexCoord * noiseScale).xyz * 2.0 - 1.0);

    // Gram-Schmidt 构建 TBN
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // 半球采样累加遮蔽
    float occlusion = 0.0;
    for (int i = 0; i < u_KernelSize; ++i)
    {
        // 从切线空间转换到 view-space
        vec3 sampleDir = TBN * u_Samples[i];
        vec3 samplePos = fragPos + sampleDir * u_Radius;

        // 将采样点投影到屏幕空间
        vec4 offset = u_Projection * vec4(samplePos, 1.0);
        offset.xy /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;

        // 获取采样点处的实际深度
        float sampleDepth = ReconstructViewPos(offset.xy).z;

        // 范围检查 + 遮蔽判定
        float rangeCheck = smoothstep(0.0, 1.0, u_Radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + u_Bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(u_KernelSize)) * u_Intensity;
    o_Occlusion = clamp(occlusion, 0.0, 1.0);
}

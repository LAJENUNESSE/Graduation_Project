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
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoords;
layout(location = 3) in vec3 a_Tangent;

#ifdef VULKAN
// Vulkan path：per-draw 变换走 push constant（112B ≤ 最小保证 128B，SPEC D-13 模板）
layout(push_constant) uniform PBRPushConstants
{
    mat4 u_Transform;
    mat3 u_NormalMatrix;
};

// pass 级相机/阴影矩阵走全局 UBO（std140，set0 binding0）。
// 声明与 fragment stage 逐字段一致（pipeline layout 兼容性要求）；
// vertex 只消费前 3 个矩阵，其余成员由 fragment 激活。
layout(std140, set = 0, binding = 0) uniform PBRGlobalUBO
{
    mat4  u_ViewProjection;
    mat4  u_LightSpaceMatrix;
    mat4  u_ViewMatrix;
    vec3  u_ViewPos;
    mat4  u_CascadeLightSpaceMatrices[4];   // std140 stride 64
    float u_CascadeSplitDepths[4];          // std140 stride 16
    float u_CascadeTexelWorldSize[4];       // std140 stride 16
    int   u_NumDirLights;
    int   u_NumPointLights;
    int   u_NumSpotLights;
    int   u_CascadeCount;
    int   u_ShadowEnabled;
    int   u_CSMEnabled;
    int   u_IBLEnabled;
    int   u_IBLDebugMode;
    int   u_SSAOEnabled;
    float u_AmbientStrength;
    float u_IBLIntensity;
    float u_ShadowBias;
};
#else
uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;
uniform mat3 u_NormalMatrix;
uniform mat4 u_LightSpaceMatrix;
uniform mat4 u_ViewMatrix;  // CSM: 用于计算 view-space Z
#endif

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec3 v_FragPos;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) out vec4 v_FragPosLightSpace;
layout(location = 4) out mat3 v_TBN;
layout(location = 7) out float v_ViewZ;  // CSM: view-space Z（v_TBN@4 连占 3 槽）

void main() {
    mat3 normalMatrix = u_NormalMatrix;
    v_Normal = normalMatrix * a_Normal;
    v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
    v_TexCoord = a_TexCoords;
    v_FragPosLightSpace = u_LightSpaceMatrix * vec4(v_FragPos, 1.0);

    // CSM: 计算 view-space Z（负值，因为相机朝 -Z）
    vec4 viewPos = u_ViewMatrix * vec4(v_FragPos, 1.0);
    v_ViewZ = -viewPos.z;

    // TBN matrix for normal mapping
    vec3 T = normalize(normalMatrix * a_Tangent);
    vec3 N = normalize(v_Normal);
    T = normalize(T - dot(T, N) * N);  // Gram-Schmidt orthogonalization
    vec3 B = cross(N, T);
    v_TBN = mat3(T, B, N);

    gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
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

const float PI = 3.14159265359;

#define MAX_DIR_LIGHTS   2
#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS  4
#define CSM_MAX_CASCADES 4

struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
};

struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
    float constant;
    float linear;
    float quadratic;
};

struct SpotLight {
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float constant;
    float linear;
    float quadratic;
    float innerCutoff;
    float outerCutoff;
};

#ifdef VULKAN
// ==================== Vulkan path：显式 layout ====================
// 全局 UBO（set0 binding0，与 vertex stage 共享布局；std140 偏移见 CPU 端打包代码）
layout(std140, set = 0, binding = 0) uniform PBRGlobalUBO
{
    mat4  u_ViewProjection;
    mat4  u_LightSpaceMatrix;
    mat4  u_ViewMatrix;
    vec3  u_ViewPos;
    mat4  u_CascadeLightSpaceMatrices[CSM_MAX_CASCADES];   // std140 stride 64
    float u_CascadeSplitDepths[CSM_MAX_CASCADES];          // std140 stride 16
    float u_CascadeTexelWorldSize[CSM_MAX_CASCADES];       // std140 stride 16
    int   u_NumDirLights;
    int   u_NumPointLights;
    int   u_NumSpotLights;
    int   u_CascadeCount;
    int   u_ShadowEnabled;
    int   u_CSMEnabled;
    int   u_IBLEnabled;
    int   u_IBLDebugMode;
    int   u_SSAOEnabled;
    float u_AmbientStrength;
    float u_IBLIntensity;
    float u_ShadowBias;
};

// 光照结构 UBO（std140：DirLight 48B / PointLight 48B / SpotLight 80B）
layout(std140, set = 0, binding = 14) uniform PBRLightsUBO
{
    DirLight   u_DirLights[MAX_DIR_LIGHTS];
    PointLight u_PointLights[MAX_POINT_LIGHTS];
    SpotLight  u_SpotLights[MAX_SPOT_LIGHTS];
};

// 材质参数 UBO（set1 binding0，每 draw 一份）
layout(std140, set = 1, binding = 0) uniform PBRMaterialUBO
{
    vec4  u_Color;
    vec2  u_Tiling;
    float u_Metallic;
    float u_Roughness;
    int   u_HasTexture;
    int   u_HasNormalMap;
    int   u_HasMetallicMap;
    int   u_HasRoughnessMap;
    int   u_HasAOMap;
    int   u_EntityID;
};

// 材质纹理（set1，combined image sampler）
layout(set = 1, binding = 1) uniform sampler2D u_DiffuseTexture;
layout(set = 1, binding = 2) uniform sampler2D u_NormalMap;
layout(set = 1, binding = 3) uniform sampler2D u_MetallicMap;
layout(set = 1, binding = 4) uniform sampler2D u_RoughnessMap;
layout(set = 1, binding = 5) uniform sampler2D u_AOMap;

// 全局纹理（set0）
layout(set = 0, binding = 1)  uniform sampler2D u_ShadowMap;
layout(set = 0, binding = 9)  uniform sampler2D u_SSAOTexture;
layout(set = 0, binding = 13) uniform sampler2D u_CascadeShadowMaps[CSM_MAX_CASCADES];

// IBL 三件套（沿用既有 set0 binding 6/7/8 约定）
layout(set = 0, binding = 6) uniform samplerCube u_IrradianceMap;
layout(set = 0, binding = 7) uniform samplerCube u_PrefilterMap;
layout(set = 0, binding = 8) uniform sampler2D u_BRDF_LUT;
#else
// ==================== OpenGL path：texture unit 即时绑定 ====================
uniform vec4 u_Color;
uniform int u_EntityID;
uniform vec3 u_ViewPos;
uniform float u_AmbientStrength;

// Textures
uniform sampler2D u_DiffuseTexture;  // unit 0 - Albedo
uniform int u_HasTexture;
uniform vec2 u_Tiling;

// PBR parameters
uniform float u_Metallic;
uniform float u_Roughness;
uniform sampler2D u_MetallicMap;    // unit 3
uniform sampler2D u_RoughnessMap;   // unit 4
uniform sampler2D u_AOMap;           // unit 5
uniform int u_HasMetallicMap;
uniform int u_HasRoughnessMap;
uniform int u_HasAOMap;

uniform int u_NumDirLights;
uniform int u_NumPointLights;
uniform int u_NumSpotLights;

uniform DirLight   u_DirLights[MAX_DIR_LIGHTS];
uniform PointLight u_PointLights[MAX_POINT_LIGHTS];
uniform SpotLight  u_SpotLights[MAX_SPOT_LIGHTS];

// Shadow mapping
uniform sampler2D u_ShadowMap;       // unit 1
uniform int u_ShadowEnabled;
uniform float u_ShadowBias;

// CSM (Cascaded Shadow Maps)
uniform int u_CSMEnabled;
uniform int u_CascadeCount;
uniform mat4 u_CascadeLightSpaceMatrices[CSM_MAX_CASCADES];
uniform float u_CascadeSplitDepths[CSM_MAX_CASCADES];
uniform sampler2D u_CascadeShadowMaps[CSM_MAX_CASCADES];  // unit 10~13
uniform float u_CascadeTexelWorldSize[CSM_MAX_CASCADES];

// Normal mapping
uniform sampler2D u_NormalMap;        // unit 2
uniform int u_HasNormalMap;

// IBL (Image-Based Lighting)
// OpenGL path：texture unit 6/7/8 由 cpp 端 SetInt 指定
uniform samplerCube u_IrradianceMap;  // unit 6
uniform samplerCube u_PrefilterMap;   // unit 7
uniform sampler2D u_BRDF_LUT;         // unit 8
uniform int u_IBLEnabled;
uniform float u_IBLIntensity;
uniform int u_IBLDebugMode;   // 0=正常, 1=Irradiance, 2=Prefilter, 3=BRDF LUT, 4=法线

// SSAO
uniform sampler2D u_SSAOTexture;      // unit 9
uniform int u_SSAOEnabled;
#endif

layout(location = 0) in vec3 v_Normal;
layout(location = 1) in vec3 v_FragPos;
layout(location = 2) in vec2 v_TexCoord;
layout(location = 3) in vec4 v_FragPosLightSpace;
layout(location = 4) in mat3 v_TBN;
layout(location = 7) in float v_ViewZ;

// ---- PBR Functions ----

// GGX/Trowbridge-Reitz Normal Distribution
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

// Schlick-GGX Geometry (single direction)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's Geometry (both directions)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness (用于 IBL 环境光)
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---- Shadow Function ----

float CalcShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float slopeFactor = 1.0 - dot(normal, lightDir);
    float bias = u_ShadowBias + u_ShadowBias * 10.0 * slopeFactor;

    // 3x3 PCF
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
            float d = texture(u_ShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (projCoords.z - bias > d) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

// CSM 阴影计算：对指定级联纹理进行 PCF 采样
float CalcCSMShadowForCascade(int cascadeIdx, vec3 fragPos, vec3 normal, vec3 lightDir)
{
    vec4 fragPosLS = u_CascadeLightSpaceMatrices[cascadeIdx] * vec4(fragPos, 1.0);
    vec3 projCoords = fragPosLS.xyz / fragPosLS.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0) return 0.0;

    float slopeFactor = 1.0 - dot(normal, lightDir);
    float bias = u_ShadowBias + u_ShadowBias * 10.0 * slopeFactor;
    // 远级联使用更大的 bias —— 按 texel 世界尺寸比例缩放
    float texelScale = (u_CascadeTexelWorldSize[0] > 0.0)
        ? u_CascadeTexelWorldSize[cascadeIdx] / u_CascadeTexelWorldSize[0]
        : float(cascadeIdx + 1);
    bias *= max(texelScale, 1.0);

    // 3x3 PCF
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(u_CascadeShadowMaps[cascadeIdx], 0);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
            float d = texture(u_CascadeShadowMaps[cascadeIdx], projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (projCoords.z - bias > d) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

// 选择合适的级联并计算阴影
float CalcCSMShadow(vec3 fragPos, vec3 normal, vec3 lightDir)
{
    // 根据 view-space Z 选择级联
    for (int i = 0; i < u_CascadeCount; i++)
    {
        if (v_ViewZ < u_CascadeSplitDepths[i])
        {
            return CalcCSMShadowForCascade(i, fragPos, normal, lightDir);
        }
    }
    // 超出所有级联范围，无阴影
    return 0.0;
}

// ---- Per-light PBR radiance ----

vec3 CalcPBRLight(vec3 L, vec3 radiance, vec3 N, vec3 V,
                  vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    // Cook-Torrance specular BRDF
    float D = DistributionGGX(N, H, roughness);
    vec3  F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float G = GeometrySmith(N, V, L, roughness);

    vec3 numerator = D * F * G;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.001;
    vec3 specular = numerator / denominator;

    // Energy conservation: diffuse = (1 - specular) * (1 - metallic)
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo + specular) * radiance * NdotL;
}

void main() {
    // Normal: use normal map if available, otherwise vertex normal
    vec3 N;
    if (u_HasNormalMap != 0) {
        N = texture(u_NormalMap, v_TexCoord * u_Tiling).rgb;
        N = N * 2.0 - 1.0;
        N = normalize(v_TBN * N);
    } else {
        N = normalize(v_Normal);
    }
    vec3 V = normalize(u_ViewPos - v_FragPos);

    // Albedo (sRGB -> Linear)
    vec4 texColor = (u_HasTexture != 0) ? texture(u_DiffuseTexture, v_TexCoord * u_Tiling) : vec4(1.0);
    if (u_HasTexture != 0)
        texColor.rgb = pow(texColor.rgb, vec3(2.2));
    vec3 albedo = texColor.rgb * u_Color.rgb;

    // PBR parameters (uniform or texture)
    float metallic = u_Metallic;
    if (u_HasMetallicMap != 0)
        metallic = texture(u_MetallicMap, v_TexCoord * u_Tiling).r;

    float roughness = u_Roughness;
    if (u_HasRoughnessMap != 0)
        roughness = texture(u_RoughnessMap, v_TexCoord * u_Tiling).r;

    float ao = 1.0;
    if (u_HasAOMap != 0)
        ao = texture(u_AOMap, v_TexCoord * u_Tiling).r;

    // F0: dielectric = 0.04, metallic = albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Shadow calculation for first directional light
    float shadow = 0.0;
    if (u_ShadowEnabled != 0 && u_NumDirLights > 0)
    {
        vec3 lightDir = normalize(-u_DirLights[0].direction);
        if (u_CSMEnabled != 0 && u_CascadeCount > 0)
        {
            shadow = CalcCSMShadow(v_FragPos, N, lightDir);
        }
        else
        {
            shadow = CalcShadow(v_FragPosLightSpace, N, lightDir);
        }
    }

    // Accumulate lighting
    vec3 Lo = vec3(0.0);

    // Directional lights
    if (u_NumDirLights > 0) {
        vec3 L = normalize(-u_DirLights[0].direction);
        vec3 radiance = u_DirLights[0].color * u_DirLights[0].intensity;
        Lo += (1.0 - shadow) * CalcPBRLight(L, radiance, N, V, albedo, metallic, roughness, F0);
    }
    for (int i = 1; i < u_NumDirLights; i++) {
        vec3 L = normalize(-u_DirLights[i].direction);
        vec3 radiance = u_DirLights[i].color * u_DirLights[i].intensity;
        Lo += (1.0 - shadow) * CalcPBRLight(L, radiance, N, V, albedo, metallic, roughness, F0);
    }

    // Point lights
    for (int i = 0; i < u_NumPointLights; i++) {
        vec3 L = normalize(u_PointLights[i].position - v_FragPos);
        float distance = length(u_PointLights[i].position - v_FragPos);
        float attenuation = 1.0 / (u_PointLights[i].constant + u_PointLights[i].linear * distance + u_PointLights[i].quadratic * distance * distance);
        vec3 radiance = u_PointLights[i].color * u_PointLights[i].intensity * attenuation;
        Lo += CalcPBRLight(L, radiance, N, V, albedo, metallic, roughness, F0);
    }

    // Spot lights
    for (int i = 0; i < u_NumSpotLights; i++) {
        vec3 L = normalize(u_SpotLights[i].position - v_FragPos);
        float distance = length(u_SpotLights[i].position - v_FragPos);
        float attenuation = 1.0 / (u_SpotLights[i].constant + u_SpotLights[i].linear * distance + u_SpotLights[i].quadratic * distance * distance);
        float theta = dot(L, normalize(-u_SpotLights[i].direction));
        float epsilon = u_SpotLights[i].innerCutoff - u_SpotLights[i].outerCutoff;
        float spotIntensity = (abs(epsilon) < 0.0001) ? step(u_SpotLights[i].innerCutoff, theta) : clamp((theta - u_SpotLights[i].outerCutoff) / epsilon, 0.0, 1.0);
        vec3 radiance = u_SpotLights[i].color * u_SpotLights[i].intensity * attenuation * spotIntensity;
        Lo += CalcPBRLight(L, radiance, N, V, albedo, metallic, roughness, F0);
    }

    // Ambient (IBL or fallback)
    vec3 ambient;

    // SSAO 遮蔽因子
    float ssaoFactor = 1.0;
    if (u_SSAOEnabled != 0)
    {
        // 从屏幕空间坐标采样 SSAO 纹理
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(u_SSAOTexture, 0));
        ssaoFactor = texture(u_SSAOTexture, screenUV).r;
    }

    if (u_IBLEnabled != 0)
    {
        // IBL 漫反射：从 irradiance map 采样
        vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kS = F;
        vec3 kD = (1.0 - kS) * (1.0 - metallic);

        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuse = irradiance * albedo;

        // IBL 镜面反射：从 prefiltered env map 采样
        vec3 R = reflect(-V, N);
        const float MAX_REFLECTION_LOD = 4.0;
        vec3 prefilteredColor = textureLod(u_PrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
        vec2 brdf = texture(u_BRDF_LUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
        vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

        ambient = (kD * diffuse + specular) * ao * ssaoFactor * u_IBLIntensity;
    }
    else
    {
        ambient = vec3(u_AmbientStrength) * albedo * ao * ssaoFactor;
    }

    // Output linear HDR (tone mapping + gamma done in post-processing)
    vec3 color = ambient + Lo;

    // IBL 调试模式
    if (u_IBLDebugMode == 1) {
        vec3 dbg = (u_IBLEnabled != 0) ? texture(u_IrradianceMap, N).rgb : vec3(0.0);
        o_Color = vec4(dbg, 1.0); o_EntityID = u_EntityID; return;
    } else if (u_IBLDebugMode == 2) {
        vec3 R = reflect(-V, N);
        vec3 dbg = (u_IBLEnabled != 0) ? textureLod(u_PrefilterMap, R, roughness * 4.0).rgb : vec3(0.0);
        o_Color = vec4(dbg, 1.0); o_EntityID = u_EntityID; return;
    } else if (u_IBLDebugMode == 3) {
        vec2 dbg = (u_IBLEnabled != 0) ? texture(u_BRDF_LUT, vec2(max(dot(N, V), 0.0), roughness)).rg : vec2(0.0);
        o_Color = vec4(dbg, 0.0, 1.0); o_EntityID = u_EntityID; return;
    } else if (u_IBLDebugMode == 4) {
        o_Color = vec4(N * 0.5 + 0.5, 1.0); o_EntityID = u_EntityID; return;
    }

    o_Color = vec4(color, u_Color.a * texColor.a);
    o_EntityID = u_EntityID;
}

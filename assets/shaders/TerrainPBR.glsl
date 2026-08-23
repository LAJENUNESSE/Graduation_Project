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
// 矩阵合计 240B 超 PC 保证 → 全局 UBO（std140）
layout(std140, set = 0, binding = 0) uniform TerrainVSUBO
{
    mat4 u_ViewProjection;
    mat4 u_Transform;
    mat4 u_LightSpaceMatrix;
};
// NormalMatrix 单独走 PC（48B）
layout(push_constant) uniform TerrainPC
{
    mat3 u_NormalMatrix;
};
#else
uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;
uniform mat3 u_NormalMatrix;
uniform mat4 u_LightSpaceMatrix;
#endif

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec3 v_FragPos;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) out vec4 v_FragPosLightSpace;
layout(location = 4) out mat3 v_TBN;

void main() {
    mat3 normalMatrix = u_NormalMatrix;
    v_Normal = normalMatrix * a_Normal;
    v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
    v_TexCoord = a_TexCoords;
    v_FragPosLightSpace = u_LightSpaceMatrix * vec4(v_FragPos, 1.0);

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
// ---- samplers：set0 显式 binding ----
layout(set = 0, binding = 1) uniform sampler2D u_ShadowMap;
layout(set = 0, binding = 6) uniform sampler2D u_Splatmap;
layout(set = 0, binding = 7) uniform sampler2D u_Layer0Albedo;
layout(set = 0, binding = 8) uniform sampler2D u_Layer1Albedo;
layout(set = 0, binding = 9) uniform sampler2D u_Layer2Albedo;
layout(set = 0, binding = 10) uniform sampler2D u_Layer3Albedo;
layout(set = 0, binding = 11) uniform sampler2D u_Layer0Normal;
layout(set = 0, binding = 12) uniform sampler2D u_Layer1Normal;
layout(set = 0, binding = 13) uniform sampler2D u_Layer2Normal;
layout(set = 0, binding = 14) uniform sampler2D u_Layer3Normal;

// std140：材质/层参数 + 光照（本阶段地形不接线，仅保证编译与后续接入布局稳定）
struct TDirLight   { vec3 direction; vec3 color; float intensity; };
struct TPointLight { vec3 position; vec3 color; float intensity; float constant; float linear; float quadratic; };
struct TSpotLight  { vec3 position; vec3 direction; vec3 color; float intensity; float constant; float linear; float quadratic; float innerCutoff; float outerCutoff; };

layout(std140, set = 0, binding = 15) uniform TerrainFSUBO
{
    mat4  u_LightSpaceMatrix;
    vec3  u_ViewPos;
    float u_AmbientStrength;
    float u_LayerTiling[4];      // std140 stride 16
    float u_LayerMetallic[4];
    float u_LayerRoughness[4];
    int   u_HasLayer0Normal;
    int   u_HasLayer1Normal;
    int   u_HasLayer2Normal;
    int   u_HasLayer3Normal;
    int   u_ShadowEnabled;
    int   u_EntityID;
    float u_ShadowBias;
    int   u_NumDirLights;
    int   u_NumPointLights;
    int   u_NumSpotLights;
};

layout(std140, set = 0, binding = 16) uniform TerrainLightsUBO
{
    TDirLight   u_DirLights[MAX_DIR_LIGHTS];
    TPointLight u_PointLights[MAX_POINT_LIGHTS];
    TSpotLight  u_SpotLights[MAX_SPOT_LIGHTS];
};
#else
uniform int u_EntityID;
uniform vec3 u_ViewPos;
uniform float u_AmbientStrength;

// Shadow mapping
uniform sampler2D u_ShadowMap;       // unit 1
uniform int u_ShadowEnabled;
uniform float u_ShadowBias;

// Splatmap
uniform sampler2D u_Splatmap;        // unit 6

// Layer albedo textures
uniform sampler2D u_Layer0Albedo;    // unit 7
uniform sampler2D u_Layer1Albedo;    // unit 8
uniform sampler2D u_Layer2Albedo;    // unit 9
uniform sampler2D u_Layer3Albedo;    // unit 10

// Layer normal textures
uniform sampler2D u_Layer0Normal;    // unit 11
uniform sampler2D u_Layer1Normal;    // unit 12
uniform sampler2D u_Layer2Normal;    // unit 13
uniform sampler2D u_Layer3Normal;    // unit 14

// Per-layer parameters
uniform float u_LayerTiling[4];
uniform float u_LayerMetallic[4];
uniform float u_LayerRoughness[4];
uniform int u_HasLayer0Normal;
uniform int u_HasLayer1Normal;
uniform int u_HasLayer2Normal;
uniform int u_HasLayer3Normal;

// Lights
uniform int u_NumDirLights;
uniform int u_NumPointLights;
uniform int u_NumSpotLights;

uniform DirLight   u_DirLights[MAX_DIR_LIGHTS];
uniform PointLight u_PointLights[MAX_POINT_LIGHTS];
uniform SpotLight  u_SpotLights[MAX_SPOT_LIGHTS];
#endif

layout(location = 0) in vec3 v_Normal;
layout(location = 1) in vec3 v_FragPos;
layout(location = 2) in vec2 v_TexCoord;
layout(location = 3) in vec4 v_FragPosLightSpace;
layout(location = 4) in mat3 v_TBN;

// ---- PBR Functions ----

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

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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

// ---- Per-light PBR radiance ----

vec3 CalcPBRLight(vec3 L, vec3 radiance, vec3 N, vec3 V,
                  vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float D = DistributionGGX(N, H, roughness);
    vec3  F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float G = GeometrySmith(N, V, L, roughness);

    vec3 numerator = D * F * G;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.001;
    vec3 specular = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo + specular) * radiance * NdotL;
}

void main() {
    // Sample splatmap using global UV
    vec4 splat = texture(u_Splatmap, v_TexCoord);
    float sumW = splat.r + splat.g + splat.b + splat.a;

    // Fallback: if splatmap is all zero, default to Layer0
    if (sumW < 0.001)
        splat = vec4(1.0, 0.0, 0.0, 0.0);
    else
        splat /= sumW;  // Normalize weights

    // Blend albedo, metallic, roughness, and tangent-space normal from 4 layers
    vec3 albedo = vec3(0.0);
    float metallic = 0.0;
    float roughness = 0.0;
    vec3 normalTS = vec3(0.0);

    // Layer 0 (splat.r)
    vec2 uv0 = v_TexCoord * u_LayerTiling[0];
    albedo += splat.r * pow(texture(u_Layer0Albedo, uv0).rgb, vec3(2.2));
    metallic += splat.r * u_LayerMetallic[0];
    roughness += splat.r * u_LayerRoughness[0];
    normalTS += splat.r * ((u_HasLayer0Normal != 0) ? (texture(u_Layer0Normal, uv0).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0));

    // Layer 1 (splat.g)
    vec2 uv1 = v_TexCoord * u_LayerTiling[1];
    albedo += splat.g * pow(texture(u_Layer1Albedo, uv1).rgb, vec3(2.2));
    metallic += splat.g * u_LayerMetallic[1];
    roughness += splat.g * u_LayerRoughness[1];
    normalTS += splat.g * ((u_HasLayer1Normal != 0) ? (texture(u_Layer1Normal, uv1).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0));

    // Layer 2 (splat.b)
    vec2 uv2 = v_TexCoord * u_LayerTiling[2];
    albedo += splat.b * pow(texture(u_Layer2Albedo, uv2).rgb, vec3(2.2));
    metallic += splat.b * u_LayerMetallic[2];
    roughness += splat.b * u_LayerRoughness[2];
    normalTS += splat.b * ((u_HasLayer2Normal != 0) ? (texture(u_Layer2Normal, uv2).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0));

    // Layer 3 (splat.a)
    vec2 uv3 = v_TexCoord * u_LayerTiling[3];
    albedo += splat.a * pow(texture(u_Layer3Albedo, uv3).rgb, vec3(2.2));
    metallic += splat.a * u_LayerMetallic[3];
    roughness += splat.a * u_LayerRoughness[3];
    normalTS += splat.a * ((u_HasLayer3Normal != 0) ? (texture(u_Layer3Normal, uv3).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0));

    // Transform blended tangent-space normal to world space
    normalTS = normalize(normalTS);
    vec3 N = normalize(v_TBN * normalTS);
    vec3 V = normalize(u_ViewPos - v_FragPos);

    // F0: dielectric = 0.04, metallic = albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Shadow calculation for first directional light
    float shadow = 0.0;
    if (u_ShadowEnabled != 0 && u_NumDirLights > 0)
    {
        vec3 lightDir = normalize(-u_DirLights[0].direction);
        shadow = CalcShadow(v_FragPosLightSpace, N, lightDir);
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
        Lo += CalcPBRLight(L, radiance, N, V, albedo, metallic, roughness, F0);
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

    // Ambient (simple, non-IBL)
    vec3 ambient = vec3(u_AmbientStrength) * albedo;

    // Output linear HDR (tone mapping + gamma done in post-processing)
    vec3 color = ambient + Lo;

    // DEBUG: 可视化 splatmap 权重 (取消注释下行开启调试)
    // color = vec3(splat.r, splat.g, splat.b) + vec3(splat.a);

    o_Color = vec4(color, 1.0);
    o_EntityID = u_EntityID;
}

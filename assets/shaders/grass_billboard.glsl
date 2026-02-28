#type vertex
#version 430 core

struct GrassBlade
{
    vec4 posAndAngle;   // xyz=世界坐标, w=Y轴旋转角
    vec4 sizeAndUV;     // x=草高, y=草宽, zw=地形UV
    vec4 color;         // xyz=色调, w=alpha
    vec4 normal;        // xyz=地形法线, w=unused
    vec4 windParams;    // x=风力强度, y=风相位偏移, zw=unused
};

layout(std430, binding = 0) readonly buffer GrassPool { GrassBlade blades[]; };

uniform mat4  u_ViewProjection;
uniform mat4  u_Transform;
uniform float u_Time;

out vec3  v_WorldPos;
out vec2  v_TexCoord;
out vec3  v_Color;
out vec3  v_Normal;
out float v_HeightFactor;
out vec4  v_FragPosLightSpace;

uniform mat4 u_LightSpaceMatrix;

// 6 顶点展开 quad (两个三角形)
const vec2 quadOffsets[6] = vec2[](
    vec2(-0.5, 0.0), vec2( 0.5, 0.0), vec2( 0.5, 1.0),
    vec2(-0.5, 0.0), vec2( 0.5, 1.0), vec2(-0.5, 1.0)
);

const vec2 quadTexCoords[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main()
{
    GrassBlade blade = blades[gl_InstanceID];

    vec3  grassRoot = blade.posAndAngle.xyz;
    float angle     = blade.posAndAngle.w;
    float height    = blade.sizeAndUV.x;
    float width     = blade.sizeAndUV.y;
    float windStr   = blade.windParams.x;
    float windPhase = blade.windParams.y;

    vec2 offset = quadOffsets[gl_VertexID];
    float heightFactor = offset.y;  // 0=底部, 1=顶部

    // 风动画：只影响顶部
    float windOffset = sin(u_Time * 2.0 + windPhase + grassRoot.x * 0.1) * windStr * heightFactor * heightFactor;

    // 局部坐标 (Y轴旋转 billboard)
    float cosA = cos(angle);
    float sinA = sin(angle);
    vec3 localPos = vec3(
        (offset.x * width + windOffset) * cosA,
        offset.y * height,
        (offset.x * width + windOffset) * sinA
    );

    vec4 worldPos4 = u_Transform * vec4(grassRoot + localPos, 1.0);
    v_WorldPos = worldPos4.xyz;

    gl_Position = u_ViewProjection * worldPos4;
    v_TexCoord = quadTexCoords[gl_VertexID];
    v_Color = blade.color.xyz;
    v_Normal = blade.normal.xyz;
    v_HeightFactor = heightFactor;
    v_FragPosLightSpace = u_LightSpaceMatrix * worldPos4;
}

#type fragment
#version 430 core

in vec3  v_WorldPos;
in vec2  v_TexCoord;
in vec3  v_Color;
in vec3  v_Normal;
in float v_HeightFactor;
in vec4  v_FragPosLightSpace;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out int  o_EntityID;

uniform sampler2D u_GrassTexture;   // unit 2
uniform sampler2D u_ShadowMap;      // unit 1
uniform int   u_ShadowEnabled;
uniform float u_ShadowBias;
uniform int   u_EntityID;

// 光照 (与 LightSystem::UploadToShader 匹配)
struct DirLight
{
    vec3 direction;
    vec3 color;
    float intensity;
};
uniform DirLight u_DirLights[2];
uniform int   u_NumDirLights;
uniform float u_AmbientStrength;

// PCF 3x3 阴影
float CalcShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float slopeFactor = 1.0 - dot(normal, lightDir);
    float bias = u_ShadowBias + u_ShadowBias * 10.0 * slopeFactor;

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

void main()
{
    vec4 texColor = texture(u_GrassTexture, v_TexCoord);
    if (texColor.a < 0.3) discard;

    vec3 baseColor = texColor.rgb * v_Color;

    // 底部 AO 近似
    float ao = mix(0.5, 1.0, v_HeightFactor);

    // Half-Lambert 光照
    vec3 N = normalize(v_Normal);
    vec3 lighting = vec3(u_AmbientStrength) * ao;

    for (int i = 0; i < u_NumDirLights; i++)
    {
        vec3 L = normalize(-u_DirLights[i].direction);
        float NdotL = dot(N, L) * 0.5 + 0.5;  // Half-Lambert
        vec3 diffuse = u_DirLights[i].color * u_DirLights[i].intensity * NdotL;

        float shadow = 0.0;
        if (u_ShadowEnabled == 1 && i == 0)
            shadow = CalcShadow(v_FragPosLightSpace, N, L);

        lighting += (1.0 - shadow) * diffuse * ao;
    }

    FragColor = vec4(baseColor * lighting, texColor.a);
    o_EntityID = u_EntityID;
}

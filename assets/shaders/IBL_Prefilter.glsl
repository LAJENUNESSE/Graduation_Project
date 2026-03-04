#type compute
#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;

// 输入环境 cubemap
uniform samplerCube u_EnvironmentMap;

// 输出 prefiltered map（6 个面横向排列）
layout(rgba16f, binding = 0) writeonly uniform image2D u_OutputPrefilter;

uniform int u_FaceSize;      // 当前 mip 级别的每面分辨率
uniform float u_Roughness;   // 当前 mip 对应的 roughness (0.0 ~ 1.0)

const float PI = 3.14159265359;

// Van der Corput 序列
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// 根据面索引和 UV 坐标计算 cubemap 方向
vec3 CubeMapDirection(int face, vec2 uv)
{
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;

    switch (face)
    {
    case 0: return normalize(vec3( 1.0,   -v,   -u));  // +X
    case 1: return normalize(vec3(-1.0,   -v,    u));  // -X
    case 2: return normalize(vec3(   u,  1.0,    v));  // +Y
    case 3: return normalize(vec3(   u, -1.0,   -v));  // -Y
    case 4: return normalize(vec3(   u,   -v,  1.0));  // +Z
    case 5: return normalize(vec3(  -u,   -v, -1.0));  // -Z
    }
    return vec3(0.0);
}

void main()
{
    ivec2 texCoord = ivec2(gl_GlobalInvocationID.xy);

    int totalWidth = u_FaceSize * 6;
    if (texCoord.x >= totalWidth || texCoord.y >= u_FaceSize)
        return;

    int face = texCoord.x / u_FaceSize;
    int localX = texCoord.x - face * u_FaceSize;
    vec2 uv = (vec2(localX, texCoord.y) + 0.5) / float(u_FaceSize);

    vec3 N = CubeMapDirection(face, uv);
    vec3 R = N;
    vec3 V = R;

    const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0;
    vec3 prefilteredColor = vec3(0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, u_Roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            prefilteredColor += texture(u_EnvironmentMap, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / max(totalWeight, 0.001);
    imageStore(u_OutputPrefilter, texCoord, vec4(prefilteredColor, 1.0));
}

#type compute
#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;

#ifdef VULKAN
// 输出 prefiltered cube（6-layer cube-compatible 2D array，每 mip 一个 2D_ARRAY storage view）
layout(set = 0, binding = 0, rgba16f) writeonly uniform image2DArray u_OutputPrefilter;
// Vulkan 路径直接采样 cubemap（VulkanTextureCubemap 已实装）
layout(set = 0, binding = 1) uniform samplerCube u_EnvCube;

// uniform 通过 push constant 传递
layout(push_constant) uniform PushConstants
{
    int   u_FaceSize;     // 当前 mip 的面分辨率
    int   u_EnvFaceSize;  // Vulkan 路径未使用，保留以保 PC 布局对齐
    float u_Roughness;    // 与 mip 级对应（mip / (mipLevels-1)）
} pc;
#define u_FaceSize    pc.u_FaceSize
#define u_EnvFaceSize pc.u_EnvFaceSize
#define u_Roughness   pc.u_Roughness

#else
// ★ 用 imageLoad 替代 texture() 采样
layout(rgba8, binding = 1) readonly uniform image2D u_EnvAtlas;
uniform int u_EnvFaceSize;  // 环境贴图单面分辨率

// 输出 prefiltered map（6 个面横向排列）
layout(rgba16f, binding = 0) writeonly uniform image2D u_OutputPrefilter;

uniform int u_FaceSize;
uniform float u_Roughness;
#endif

const float PI = 3.14159265359;

#ifdef VULKAN
// Vulkan 路径：直接 cubemap 采样
vec3 SampleEnvMap(vec3 dir)
{
    return texture(u_EnvCube, dir).rgb;
}
#else
// OpenGL 路径：从 2D atlas 用 imageLoad 读取
vec3 SampleEnvMap(vec3 dir)
{
    vec3 a = abs(dir);
    int face;
    float ma, sc, tc;

    if (a.x >= a.y && a.x >= a.z)
    {
        if (dir.x > 0.0) { face = 0; sc = -dir.z; tc = -dir.y; ma = dir.x; }
        else              { face = 1; sc =  dir.z; tc = -dir.y; ma = -dir.x; }
    }
    else if (a.y >= a.x && a.y >= a.z)
    {
        if (dir.y > 0.0) { face = 2; sc = dir.x; tc =  dir.z; ma = dir.y; }
        else              { face = 3; sc = dir.x; tc = -dir.z; ma = -dir.y; }
    }
    else
    {
        if (dir.z > 0.0) { face = 4; sc =  dir.x; tc = -dir.y; ma = dir.z; }
        else              { face = 5; sc = -dir.x; tc = -dir.y; ma = -dir.z; }
    }

    float s = 0.5 * (sc / ma + 1.0);
    float t = 0.5 * (tc / ma + 1.0);

    int px = face * u_EnvFaceSize + clamp(int(s * float(u_EnvFaceSize)), 0, u_EnvFaceSize - 1);
    int py = clamp(int(t * float(u_EnvFaceSize)), 0, u_EnvFaceSize - 1);

    return imageLoad(u_EnvAtlas, ivec2(px, py)).rgb;
}
#endif

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

    vec3 up = abs(N.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
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

// GGX 重要性采样主体：face + uv → prefiltered color（GL/Vulkan 共享）
vec3 ComputePrefilter(int face, vec2 uv)
{
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
            prefilteredColor += SampleEnvMap(L) * NdotL;
            totalWeight += NdotL;
        }
    }

    return prefilteredColor / max(totalWeight, 0.001);
}

#ifdef VULKAN
void main()
{
    // face 来自 dispatch 第三维（gz=6）；每个 mip 级单独 dispatch 一次
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    int   face  = int(gl_GlobalInvocationID.z);
    if (texel.x >= u_FaceSize || texel.y >= u_FaceSize || face >= 6)
        return;

    vec2 uv = (vec2(texel) + 0.5) / float(u_FaceSize);
    imageStore(u_OutputPrefilter, ivec3(texel, face), vec4(ComputePrefilter(face, uv), 1.0));
}
#else
void main()
{
    ivec2 texCoord = ivec2(gl_GlobalInvocationID.xy);

    int totalWidth = u_FaceSize * 6;
    if (texCoord.x >= totalWidth || texCoord.y >= u_FaceSize)
        return;

    int face = texCoord.x / u_FaceSize;
    int localX = texCoord.x - face * u_FaceSize;
    vec2 uv = (vec2(localX, texCoord.y) + 0.5) / float(u_FaceSize);

    imageStore(u_OutputPrefilter, texCoord, vec4(ComputePrefilter(face, uv), 1.0));
}
#endif

#type compute
#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;

#ifdef VULKAN
// 输出 irradiance map（6 个面横向排列）
layout(set = 0, binding = 0, rgba16f) writeonly uniform image2D u_OutputIrradiance;
// ★ 用 imageLoad 替代 texture() 采样（texture() 在某些驱动的 compute shader 中返回零）
layout(set = 0, binding = 1, rgba8) readonly uniform image2D u_EnvAtlas;

// uniform 通过 push constant 传递（Vulkan 不允许 free uniform）
layout(push_constant) uniform PushConstants
{
    int u_FaceSize;     // 输出面分辨率
    int u_EnvFaceSize;  // 环境贴图单面分辨率
} pc;
#define u_FaceSize    pc.u_FaceSize
#define u_EnvFaceSize pc.u_EnvFaceSize

#else
// ★ 用 imageLoad 替代 texture() 采样（texture() 在某些驱动的 compute shader 中返回零）
layout(rgba8, binding = 1) readonly uniform image2D u_EnvAtlas;
uniform int u_EnvFaceSize;  // 环境贴图单面分辨率

// 输出 irradiance map（6 个面横向排列）
layout(rgba16f, binding = 0) writeonly uniform image2D u_OutputIrradiance;

uniform int u_FaceSize;  // 输出面分辨率
#endif

const float PI = 3.14159265359;

// 从 2D atlas 用 imageLoad 读取环境贴图
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

// 根据输出面索引和 UV 坐标计算 cubemap 方向
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

    // 半球卷积
    vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    vec3 irradiance = vec3(0.0);
    float sampleDelta = 0.05;
    float nrSamples = 0.0;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            irradiance += SampleEnvMap(sampleVec) * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }

    irradiance = PI * irradiance / nrSamples;

    imageStore(u_OutputIrradiance, texCoord, vec4(irradiance, 1.0));
}

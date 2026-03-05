#type compute
#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;

// 输入环境 cubemap
uniform samplerCube u_EnvironmentMap;

// 输出 irradiance map（6 个面存储在一张 2D 纹理中，每面 faceSize x faceSize）
// 布局：6 个面横向排列，总宽度 = faceSize * 6
layout(rgba16f, binding = 0) writeonly uniform image2D u_OutputIrradiance;

uniform int u_FaceSize;  // 每个面的分辨率（如 32）

const float PI = 3.14159265359;

// 根据面索引和 UV 坐标计算 cubemap 方向
vec3 CubeMapDirection(int face, vec2 uv)
{
    // uv 范围 [0,1] -> [-1,1]
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

    // 总图像尺寸 = (faceSize * 6, faceSize)
    int totalWidth = u_FaceSize * 6;
    if (texCoord.x >= totalWidth || texCoord.y >= u_FaceSize)
        return;

    // 确定面索引和面内 UV
    int face = texCoord.x / u_FaceSize;
    int localX = texCoord.x - face * u_FaceSize;
    vec2 uv = (vec2(localX, texCoord.y) + 0.5) / float(u_FaceSize);

    vec3 N = CubeMapDirection(face, uv);
    vec3 irradiance = vec3(0.0);

    // 半球卷积采样
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    float sampleDelta = 0.025;
    float nrSamples = 0.0;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // 球面坐标转换到笛卡尔坐标（切线空间）
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // 切线空间转换到世界空间
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            irradiance += texture(u_EnvironmentMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }

    irradiance = PI * irradiance / nrSamples;
    imageStore(u_OutputIrradiance, texCoord, vec4(irradiance, 1.0));
}

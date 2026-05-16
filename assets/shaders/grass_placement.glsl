#type compute
#version 430 core

layout(local_size_x = 64) in;

// GPU 草叶数据结构: 5 x vec4 = 80 bytes
struct GrassBlade
{
    vec4 posAndAngle;   // xyz=世界坐标, w=Y轴旋转角
    vec4 sizeAndUV;     // x=草高, y=草宽, zw=地形UV
    vec4 color;         // xyz=色调随机, w=alpha
    vec4 normal;        // xyz=地形法线, w=unused
    vec4 windParams;    // x=风力强度, y=风相位偏移, zw=unused
};

layout(std430, binding = 0) writeonly buffer GrassPool { GrassBlade blades[]; };
layout(std430, binding = 1) readonly  buffer HeightMap { float heights[]; };
layout(std430, binding = 3)           buffer Counter   { uint grassCount; };

// std140 UBO，避免 Vulkan 不接受散装 uniform；OpenGL 4.3 用 glBindBufferBase 绑到 binding 2 即可。
// 顺序按字节排布；int/float 在 std140 中均为 4 字节、无 padding。
layout(std140, binding = 2) uniform GrassPlacementParams
{
    int   u_MaxGrass;
    int   u_HeightmapWidth;
    int   u_HeightmapHeight;
    float u_TerrainSize;
    float u_HeightScale;
    float u_GrassHeight;
    float u_GrassWidth;
    float u_GrassWindStrength;
};

// PCG hash
uint pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randomFloat(uint seed)
{
    return float(pcg(seed)) / 4294967295.0;
}

// 双线性插值采样高度图
float sampleHeight(float u, float v)
{
    float fx = u * float(u_HeightmapWidth - 1);
    float fy = v * float(u_HeightmapHeight - 1);

    int x0 = int(floor(fx));
    int y0 = int(floor(fy));
    int x1 = min(x0 + 1, u_HeightmapWidth - 1);
    int y1 = min(y0 + 1, u_HeightmapHeight - 1);

    float sx = fx - float(x0);
    float sy = fy - float(y0);

    float h00 = heights[y0 * u_HeightmapWidth + x0];
    float h10 = heights[y0 * u_HeightmapWidth + x1];
    float h01 = heights[y1 * u_HeightmapWidth + x0];
    float h11 = heights[y1 * u_HeightmapWidth + x1];

    float h0 = mix(h00, h10, sx);
    float h1 = mix(h01, h11, sx);
    return mix(h0, h1, sy);
}

// 有限差分计算地形法线
vec3 calcNormal(float u, float v)
{
    float eps = 1.0 / float(max(u_HeightmapWidth, u_HeightmapHeight));
    float hL = sampleHeight(max(u - eps, 0.0), v) * u_HeightScale;
    float hR = sampleHeight(min(u + eps, 1.0), v) * u_HeightScale;
    float hD = sampleHeight(u, max(v - eps, 0.0)) * u_HeightScale;
    float hU = sampleHeight(u, min(v + eps, 1.0)) * u_HeightScale;

    float step = eps * u_TerrainSize;
    vec3 n = normalize(vec3(hL - hR, 2.0 * step, hD - hU));
    return n;
}

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_MaxGrass)) return;

    // 随机 UV 坐标
    uint seed = gid * 3u + 17u;
    float u = randomFloat(seed);
    float v = randomFloat(seed + 1u);

    // 采样高度
    float h = sampleHeight(u, v) * u_HeightScale;

    // 计算法线，过滤陡峭面
    vec3 normal = calcNormal(u, v);
    if (normal.y < 0.5) return;

    // 世界坐标（地形中心在原点）
    float wx = (u - 0.5) * u_TerrainSize;
    float wz = (v - 0.5) * u_TerrainSize;

    // 随机参数
    float angle = randomFloat(seed + 2u) * 6.283185;
    float sizePert = 0.8 + randomFloat(seed + 3u) * 0.4;   // 0.8~1.2
    float greenVar = 0.7 + randomFloat(seed + 4u) * 0.3;   // 0.7~1.0
    float windPhase = randomFloat(seed + 5u) * 6.283185;

    // 写入
    uint idx = atomicAdd(grassCount, 1u);
    if (idx >= uint(u_MaxGrass)) return;

    blades[idx].posAndAngle = vec4(wx, h, wz, angle);
    blades[idx].sizeAndUV   = vec4(u_GrassHeight * sizePert, u_GrassWidth * sizePert, u, v);
    blades[idx].color       = vec4(0.3 * greenVar, 0.6 * greenVar, 0.15 * greenVar, 1.0);
    blades[idx].normal      = vec4(normal, 0.0);
    blades[idx].windParams  = vec4(u_GrassWindStrength, windPhase, 0.0, 0.0);
}

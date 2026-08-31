#type vertex
#version 430 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 v_TexCoord;

void main()
{
    gl_Position = vec4(a_Position, 0.0, 1.0);
    v_TexCoord = a_TexCoord;
}

#type fragment
#version 430 core

// Screen-Space Fluid: 双边高斯模糊平滑深度
// 保留流体边缘的深度不连续性

layout(location = 0) in vec2 v_TexCoord;

#ifdef VULKAN
layout(set = 0, binding = 0) uniform sampler2D u_DepthTexture;
// dispatcher 的通用 PC 打包只覆盖 ≥64B 的 mat4 级布局（128B 通用块），
// 小参数块走 std140 UBO（FluidRenderer 每次绘制打包上传，binding 1）
layout(std140, set = 0, binding = 1) uniform FluidSmoothParams
{
    vec2  u_ScreenSize;
    float u_FilterRadius;
    float u_DepthFalloff;
    int   u_Horizontal;
};
#else
uniform sampler2D u_DepthTexture;
uniform vec2  u_ScreenSize;
uniform float u_FilterRadius;
uniform float u_DepthFalloff;
uniform int   u_Horizontal;
#endif

layout(location = 0) out float FragSmoothedDepth;

void main()
{
    float centerDepth = texture(u_DepthTexture, v_TexCoord).r;

    // 背景（极大深度值）直接返回
    if (centerDepth > 1e9)
    {
        FragSmoothedDepth = centerDepth;
        return;
    }

    vec2 dir = u_Horizontal != 0
               ? vec2(1.0 / u_ScreenSize.x, 0.0)
               : vec2(0.0, 1.0 / u_ScreenSize.y);

    float sum = 0.0;
    float wsum = 0.0;

    int radius = int(u_FilterRadius);
    float sigma = u_FilterRadius * 0.5;
    float sigma2 = 2.0 * sigma * sigma;

    for (int i = -radius; i <= radius; i++)
    {
        vec2 sampleUV = v_TexCoord + dir * float(i);
        float sampleDepth = texture(u_DepthTexture, sampleUV).r;

        if (sampleDepth > 1e9) continue;

        // 空间权重
        float spatialW = exp(-float(i * i) / sigma2);

        // 深度权重（双边）
        float depthDiff = (sampleDepth - centerDepth) * u_DepthFalloff;
        float depthW = exp(-depthDiff * depthDiff);

        float w = spatialW * depthW;
        sum += sampleDepth * w;
        wsum += w;
    }

    FragSmoothedDepth = (wsum > 0.0) ? sum / wsum : centerDepth;
}

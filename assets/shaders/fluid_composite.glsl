#type vertex
#version 430 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;

out vec2 v_TexCoord;

void main()
{
    gl_Position = vec4(a_Position, 0.0, 1.0);
    v_TexCoord = a_TexCoord;
}

#type fragment
#version 430 core

// Screen-Space Fluid: 最终合成
// 法线重建 + Fresnel + 折射 + 厚度吸收 + 高光

in vec2 v_TexCoord;

uniform sampler2D u_FluidDepth;      // slot 0: 平滑后流体深度 (R32F, view-space Z)
uniform sampler2D u_FluidThickness;  // slot 1: 流体厚度 (R16F)
uniform sampler2D u_SceneColor;      // slot 2: 场景颜色拷贝
uniform sampler2D u_SceneDepth;      // slot 3: 场景深度

uniform mat4 u_InvProjection;
uniform mat4 u_InvView;
uniform vec2 u_ScreenSize;

uniform vec3  u_FluidColor;
uniform vec3  u_AbsorptionColor;
uniform float u_AbsorptionScale;
uniform float u_FresnelPower;
uniform float u_RefractionStrength;
uniform float u_Reflectivity;

layout(location = 0) out vec4 FragColor;

// 从平滑深度图用有限差分重建 view-space 法线
vec3 reconstructNormal(vec2 uv)
{
    vec2 texelSize = 1.0 / u_ScreenSize;

    float dL = texture(u_FluidDepth, uv - vec2(texelSize.x, 0.0)).r;
    float dR = texture(u_FluidDepth, uv + vec2(texelSize.x, 0.0)).r;
    float dT = texture(u_FluidDepth, uv + vec2(0.0, texelSize.y)).r;
    float dB = texture(u_FluidDepth, uv - vec2(0.0, texelSize.y)).r;

    // 跳过背景采样
    float center = texture(u_FluidDepth, uv).r;
    if (dL > 1e9) dL = center;
    if (dR > 1e9) dR = center;
    if (dT > 1e9) dT = center;
    if (dB > 1e9) dB = center;

    float dx = (dR - dL) * 0.5;
    float dy = (dT - dB) * 0.5;

    // view-space 法线（Z 轴指向相机）
    return normalize(vec3(-dx * u_ScreenSize.x * 0.02,
                          -dy * u_ScreenSize.y * 0.02,
                          1.0));
}

void main()
{
    float fluidDepth = texture(u_FluidDepth, v_TexCoord).r;
    vec4 sceneColor  = texture(u_SceneColor, v_TexCoord);

    // 没有流体像素，直接输出场景颜色
    if (fluidDepth > 1e9)
    {
        FragColor = sceneColor;
        return;
    }

    float thickness = texture(u_FluidThickness, v_TexCoord).r;

    // 重建法线
    vec3 viewNormal = reconstructNormal(v_TexCoord);

    // Schlick Fresnel
    float cosTheta = max(dot(viewNormal, vec3(0.0, 0.0, 1.0)), 0.0);
    float fresnel = u_Reflectivity + (1.0 - u_Reflectivity)
                    * pow(1.0 - cosTheta, u_FresnelPower);

    // 折射: 偏移 UV 采样背景
    vec2 refractOffset = viewNormal.xy * u_RefractionStrength;
    vec2 refractUV = clamp(v_TexCoord + refractOffset, 0.001, 0.999);
    vec3 refractedColor = texture(u_SceneColor, refractUV).rgb;

    // Beer's Law 厚度吸收
    vec3 absorption = exp(-u_AbsorptionColor * thickness * u_AbsorptionScale);
    refractedColor *= absorption;

    // 反射（简化: 使用流体颜色）
    vec3 reflectedColor = u_FluidColor * 0.6;

    // 混合反射和折射
    vec3 fluidFinalColor = mix(refractedColor, reflectedColor, fresnel);

    // 流体自身颜色（基于厚度）
    fluidFinalColor += u_FluidColor * thickness * 0.5;

    // Phong 高光
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 halfVec = normalize(lightDir + vec3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(viewNormal, halfVec), 0.0), 64.0);
    fluidFinalColor += vec3(1.0) * spec * 0.4;

    FragColor = vec4(fluidFinalColor, 1.0);
}

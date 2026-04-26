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
uniform float u_RefractiveIndex;

layout(location = 0) out vec4 FragColor;

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 DielectricF0(float eta)
{
    float f0 = pow((eta - 1.0) / (eta + 1.0), 2.0);
    return vec3(f0);
}

// 从 view-space Z（流体深度贴图中为负值）重建 view-space 位置
vec3 viewPosFromDepth(vec2 uv, float viewSpaceZ)
{
    if (isnan(viewSpaceZ) || isinf(viewSpaceZ) || abs(viewSpaceZ) < 1e-5 || viewSpaceZ > 1e9 || viewSpaceZ < -1e9)
        return vec3(0.0, 0.0, 0.0);

    vec2 ndc = uv * 2.0 - 1.0;
    vec4 viewRay = u_InvProjection * vec4(ndc, 0.0, 1.0);

    if (abs(viewRay.w) < 1e-6)
        return vec3(0.0, 0.0, viewSpaceZ);
    viewRay.xyz /= viewRay.w;

    if (abs(viewRay.z) < 1e-6)
        return vec3(0.0, 0.0, viewSpaceZ);

    float scale = viewSpaceZ / viewRay.z;
    vec3 viewPos = viewRay.xyz * scale;
    viewPos.z = viewSpaceZ;
    return viewPos;
}

// 从平滑深度图用有限差分重建 view-space 法线
vec3 reconstructNormal(vec2 uv)
{
    vec2 texelSize = 1.0 / u_ScreenSize;

    float depthC = texture(u_FluidDepth, uv).r;
    if (isnan(depthC) || isinf(depthC) || abs(depthC) < 1e-5 || depthC > 1e9 || depthC < -1e9)
        return vec3(0.0, 0.0, 1.0);

    float depthR = texture(u_FluidDepth, uv + vec2(texelSize.x, 0.0)).r;
    float depthU = texture(u_FluidDepth, uv + vec2(0.0, texelSize.y)).r;

    // 背景回退
    if (depthR > 1e9) depthR = depthC;
    if (depthU > 1e9) depthU = depthC;

    vec3 posC = viewPosFromDepth(uv, depthC);
    vec3 posR = viewPosFromDepth(uv + vec2(texelSize.x, 0.0), depthR);
    vec3 posU = viewPosFromDepth(uv + vec2(0.0, texelSize.y), depthU);

    if (any(isnan(posC)) || any(isinf(posC)) || any(isnan(posR)) || any(isinf(posR))
        || any(isnan(posU)) || any(isinf(posU)))
    {
        return vec3(0.0, 0.0, 1.0);
    }

    vec3 dX = posR - posC;
    vec3 dY = posU - posC;
    vec3 c  = cross(dX, dY);
    float c2 = dot(c, c);
    if (c2 <= 1e-12 || any(isnan(c)) || any(isinf(c)))
        return vec3(0.0, 0.0, 1.0);

    vec3 n = c * inversesqrt(c2);
    // 确保法线朝向相机（view-space Z 正方向）
    return (n.z < 0.0) ? -n : n;
}

void main()
{
    float fluidDepth = texture(u_FluidDepth, v_TexCoord).r;
    vec4 sceneColor  = texture(u_SceneColor, v_TexCoord);

    // 没有流体像素，直接输出场景颜色
    if (isnan(fluidDepth) || isinf(fluidDepth) || abs(fluidDepth) < 1e-5 || fluidDepth > 1e9 || fluidDepth < -1e9)
    {
        FragColor = sceneColor;
        return;
    }

    // 场景深度遮挡：将 depth buffer [0,1] 转为 view-space Z，与流体深度比较
    float sceneDepthRaw = texture(u_SceneDepth, v_TexCoord).r;
    float ndcZ          = sceneDepthRaw * 2.0 - 1.0; // OpenGL [0,1] → NDC [-1,1]
    vec4  sceneClip     = u_InvProjection * vec4(0.0, 0.0, ndcZ, 1.0);
    float sceneViewZ    = sceneClip.z / sceneClip.w;
    // fluidDepth 和 sceneViewZ 都是 view-space 负值，更负 = 更远
    if (fluidDepth < sceneViewZ)
    {
        FragColor = sceneColor;
        return;
    }

    float rawThickness = texture(u_FluidThickness, v_TexCoord).r;
    if (rawThickness <= 1e-6 || isnan(rawThickness) || isinf(rawThickness))
    {
        FragColor = sceneColor;
        return;
    }
    float thickness = clamp(rawThickness, 0.0, 8.0);

    // 重建法线
    vec3 viewNormal = reconstructNormal(v_TexCoord);
    if (any(isnan(viewNormal)) || any(isinf(viewNormal)))
    {
        FragColor = sceneColor;
        return;
    }

    float cosTheta = max(dot(viewNormal, vec3(0.0, 0.0, 1.0)), 0.0);
    float eta = (u_RefractiveIndex > 0.0) ? u_RefractiveIndex : 1.333;
    vec3  F0 = DielectricF0(eta);
    vec3  fresnelRGB = FresnelSchlick(cosTheta, F0);
    fresnelRGB = mix(vec3(u_Reflectivity), fresnelRGB, clamp(u_FresnelPower / 5.0, 0.0, 1.0));

    float grazingBoost = clamp(1.0 - cosTheta, 0.0, 1.0);
    float offsetScale = 1.0 / (1.0 + thickness * 0.25);
    vec2  refractOffset = viewNormal.xy * u_RefractionStrength * (0.5 + 1.5 * grazingBoost) * offsetScale;
    refractOffset = clamp(refractOffset, vec2(-0.03), vec2(0.03));
    vec2  refractUV = clamp(v_TexCoord + refractOffset, 0.001, 0.999);
    vec3  backgroundColor = texture(u_SceneColor, refractUV).rgb;
    if (any(isnan(backgroundColor)) || any(isinf(backgroundColor)))
        backgroundColor = sceneColor.rgb;

    vec3 expArg = clamp(-u_AbsorptionColor * thickness * u_AbsorptionScale, vec3(-80.0), vec3(80.0));
    vec3 transmittance = exp(expArg);
    vec3 refractedColor = backgroundColor * transmittance + u_FluidColor * (1.0 - transmittance);

    vec2 reflectOffset = viewNormal.xy * u_RefractionStrength * (-0.6 * (0.5 + grazingBoost)) * offsetScale;
    reflectOffset = clamp(reflectOffset, vec2(-0.03), vec2(0.03));
    vec2 reflectUV = clamp(v_TexCoord + reflectOffset, 0.001, 0.999);
    vec3 reflectedColor = texture(u_SceneColor, reflectUV).rgb;
    if (any(isnan(reflectedColor)) || any(isinf(reflectedColor)))
        reflectedColor = sceneColor.rgb;

    vec3 fluidFinalColor = mix(refractedColor, reflectedColor, fresnelRGB);

    float specularAtten = 1.0 - clamp(thickness * 0.35, 0.0, 0.9);
    vec3  lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3  halfVec = normalize(lightDir + vec3(0.0, 0.0, 1.0));
    float specPower = mix(64.0, 8.0, clamp(thickness * 2.0, 0.0, 1.0));
    float spec = pow(max(dot(viewNormal, halfVec), 0.0), specPower);
    fluidFinalColor += reflectedColor * spec * specularAtten * 0.25;

    float bodyTint = 1.0 - exp(-thickness * 0.35);
    fluidFinalColor = mix(fluidFinalColor, u_FluidColor, bodyTint * 0.2);
    if (any(isnan(fluidFinalColor)) || any(isinf(fluidFinalColor)))
        fluidFinalColor = sceneColor.rgb;
    fluidFinalColor = clamp(fluidFinalColor, vec3(0.0), vec3(32.0));

    FragColor = vec4(fluidFinalColor, 1.0);
}

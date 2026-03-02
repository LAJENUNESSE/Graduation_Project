#type vertex
#version 430 core

// Screen-Space Fluid: Sphere Impostor 深度渲染
// 每个粒子渲染为 billboard quad，fragment 中做球面深度修正

struct GPUParticle
{
    vec4 posAndLife;
    vec4 velAndMaxLife;
    vec4 startColor;
    vec4 endColor;
    vec4 params;
};

layout(std430, binding = 0) readonly buffer ParticlePool { GPUParticle particles[]; };

uniform mat4  u_View;
uniform mat4  u_Projection;
uniform float u_ParticleRadius;

out vec3 v_ViewPos;
out vec2 v_TexCoord;

const vec2 quadOffsets[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0),  vec2(-1.0, 1.0)
);

void main()
{
    uint particleIdx = gl_InstanceID;
    vec3 worldPos = particles[particleIdx].posAndLife.xyz;

    vec4 viewPos = u_View * vec4(worldPos, 1.0);
    v_ViewPos = viewPos.xyz;

    vec3 cameraRight = vec3(u_View[0][0], u_View[1][0], u_View[2][0]);
    vec3 cameraUp    = vec3(u_View[0][1], u_View[1][1], u_View[2][1]);

    vec2 offset = quadOffsets[gl_VertexID];
    v_TexCoord  = offset * 0.5 + 0.5;

    vec3 vertexWorldPos = worldPos
                        + cameraRight * offset.x * u_ParticleRadius
                        + cameraUp    * offset.y * u_ParticleRadius;

    gl_Position = u_Projection * u_View * vec4(vertexWorldPos, 1.0);
}

#type fragment
#version 430 core

in vec3 v_ViewPos;
in vec2 v_TexCoord;

uniform mat4  u_Projection;
uniform float u_ParticleRadius;

layout(location = 0) out float FragDepth;

void main()
{
    vec2 ndc = v_TexCoord * 2.0 - 1.0;
    float r2 = dot(ndc, ndc);
    if (r2 > 1.0) discard;

    // 球面上的 Z 偏移（靠近相机方向为正）
    float z = sqrt(1.0 - r2);

    // 球面上点的 view-space 位置
    vec3 sphereViewPos = v_ViewPos;
    sphereViewPos.z += u_ParticleRadius * z;

    // 输出 view-space Z（负值，越大越近）
    FragDepth = sphereViewPos.z;

    // 更新 GL 深度缓冲
    vec4 clipPos = u_Projection * vec4(sphereViewPos, 1.0);
    float ndcDepth = clipPos.z / clipPos.w;
    gl_FragDepth = ndcDepth * 0.5 + 0.5;
}

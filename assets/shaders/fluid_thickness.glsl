#type vertex
#version 430 core

// Screen-Space Fluid: 厚度累加渲染
// 每个粒子的球形贡献通过加法混合叠加到 R16F

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

out vec2 v_TexCoord;

const vec2 quadOffsets[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0),  vec2(-1.0, 1.0)
);

void main()
{
    uint particleIdx = gl_InstanceID;
    vec3 worldPos = particles[particleIdx].posAndLife.xyz;

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

in vec2 v_TexCoord;

layout(location = 0) out float FragThickness;

void main()
{
    vec2 ndc = v_TexCoord * 2.0 - 1.0;
    float r2 = dot(ndc, ndc);
    if (r2 > 1.0) discard;

    // 球的厚度贡献: 中心最厚，边缘为零
    FragThickness = sqrt(1.0 - r2) * 0.1;
}

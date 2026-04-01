#type vertex
#version 430 core

// Must match compute shader layout: 5 x vec4 = 80 bytes
struct GPUParticle
{
    vec4 posAndLife;       // xyz=position, w=remainingLife
    vec4 velAndMaxLife;    // xyz=velocity, w=maxLife
    vec4 startColor;       // RGBA
    vec4 endColor;         // RGBA
    // 渲染只读取 params.xy（大小插值），params.zw 由 SPH 着色器写入密度/压力，渲染不使用
    vec4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH用), w=pressure(SPH用)
};

layout(std430, binding = 0) readonly buffer ParticlePool { GPUParticle particles[]; };
layout(std430, binding = 2) readonly buffer AliveList    { uint aliveIndices[]; };

uniform mat4 u_View;
uniform mat4 u_Projection;

out vec4 v_Color;
out vec2 v_TexCoord;

const vec2 quadOffsets[6] = vec2[](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5)
);

const vec2 quadTexCoords[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main()
{
    uint particleIdx = aliveIndices[gl_InstanceID];
    GPUParticle p = particles[particleIdx];

    // Life fraction: 0 at birth, 1 at death
    float life    = p.posAndLife.w;
    float maxLife = p.velAndMaxLife.w;
    float t       = 1.0 - clamp(life / max(maxLife, 1e-6), 0.0, 1.0);

    // Interpolate color and size over lifetime
    v_Color    = mix(p.startColor, p.endColor, t);
    float size = mix(p.params.x, p.params.y, t);

    // Extract camera right/up from view matrix columns
    vec3 cameraRight = vec3(u_View[0][0], u_View[1][0], u_View[2][0]);
    vec3 cameraUp    = vec3(u_View[0][1], u_View[1][1], u_View[2][1]);

    vec2 offset  = quadOffsets[gl_VertexID];
    vec3 worldPos = p.posAndLife.xyz
                  + cameraRight * offset.x * size
                  + cameraUp    * offset.y * size;

    gl_Position = u_Projection * u_View * vec4(worldPos, 1.0);
    v_TexCoord  = quadTexCoords[gl_VertexID];
}

#type fragment
#version 430 core

in vec4 v_Color;
in vec2 v_TexCoord;

layout(location = 0) out vec4 FragColor;

void main()
{
    // Soft circle falloff
    float dist  = length(v_TexCoord - vec2(0.5));
    float alpha = 1.0 - smoothstep(0.3, 0.5, dist);

    FragColor = v_Color * vec4(1.0, 1.0, 1.0, alpha);
}

#version 450

layout(location = 0) out vec3 vColor;

void main()
{
    const vec2 positions[3] = vec2[3](
        vec2(0.0, -0.5),
        vec2(0.5, 0.5),
        vec2(-0.5, 0.5)
    );

    const vec3 colors[3] = vec3[3](
        vec3(1.0, 0.2, 0.2),
        vec3(0.2, 1.0, 0.2),
        vec3(0.2, 0.4, 1.0)
    );

    uint index = uint(gl_VertexIndex) % 3u;
    gl_Position = vec4(positions[index], 0.0, 1.0);
    vColor = colors[index];
}

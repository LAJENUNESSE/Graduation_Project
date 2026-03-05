#type vertex
#version 330 core
layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;

out vec2 v_TexCoord;

void main() {
    v_TexCoord = a_TexCoord;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

#type fragment
#version 330 core
layout(location = 0) out float o_Occlusion;

in vec2 v_TexCoord;

uniform sampler2D u_SSAOInput;

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(u_SSAOInput, 0));

    // 4x4 简单盒滤波（双边模糊的简化版）
    float result = 0.0;
    for (int x = -2; x < 2; ++x)
    {
        for (int y = -2; y < 2; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(u_SSAOInput, v_TexCoord + offset).r;
        }
    }

    o_Occlusion = result / 16.0;
}

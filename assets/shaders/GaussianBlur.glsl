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
layout(location = 0) out vec4 FragColor;
in vec2 v_TexCoord;

uniform sampler2D u_Image;
uniform int u_Horizontal;

// 5-tap Gaussian weights
const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    vec2 texOffset = 1.0 / textureSize(u_Image, 0);
    vec3 result = texture(u_Image, v_TexCoord).rgb * weight[0];

    if (u_Horizontal != 0) {
        for (int i = 1; i < 5; ++i) {
            result += texture(u_Image, v_TexCoord + vec2(texOffset.x * float(i), 0.0)).rgb * weight[i];
            result += texture(u_Image, v_TexCoord - vec2(texOffset.x * float(i), 0.0)).rgb * weight[i];
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            result += texture(u_Image, v_TexCoord + vec2(0.0, texOffset.y * float(i))).rgb * weight[i];
            result += texture(u_Image, v_TexCoord - vec2(0.0, texOffset.y * float(i))).rgb * weight[i];
        }
    }
    FragColor = vec4(result, 1.0);
}

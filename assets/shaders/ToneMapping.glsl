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

uniform sampler2D u_HDRBuffer;
uniform sampler2D u_BloomBlur;
uniform float u_BloomStrength;
uniform int u_ToneMappingMode;
uniform int u_GammaCorrection;
uniform int u_BloomEnabled;

// ACES Filmic Tone Mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdrColor = texture(u_HDRBuffer, v_TexCoord).rgb;

    // Add bloom
    if (u_BloomEnabled != 0) {
        vec3 bloomColor = texture(u_BloomBlur, v_TexCoord).rgb;
        hdrColor += bloomColor * u_BloomStrength;
    }

    // Tone mapping
    vec3 mapped;
    if (u_ToneMappingMode == 0) {
        // Reinhard
        mapped = hdrColor / (hdrColor + vec3(1.0));
    } else {
        // ACES
        mapped = ACESFilm(hdrColor);
    }

    // Gamma correction
    if (u_GammaCorrection != 0)
        mapped = pow(mapped, vec3(1.0 / 2.2));

    FragColor = vec4(mapped, 1.0);
}

#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProjection;

out vec3 v_TexCoords;

void main() {
    v_TexCoords = a_Position;
    vec4 pos = u_ViewProjection * vec4(a_Position, 1.0);
    gl_Position = pos.xyww;
}

#type fragment
#version 330 core
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

in vec3 v_TexCoords;
uniform samplerCube u_Skybox;

void main() {
    vec4 texColor = texture(u_Skybox, v_TexCoords);
    texColor.rgb = pow(texColor.rgb, vec3(2.2));  // sRGB -> Linear
    o_Color = texColor;
    o_EntityID = -1;
}

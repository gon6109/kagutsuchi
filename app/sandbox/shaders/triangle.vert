#version 450

layout(location = 0) out vec3 color;

const vec2 Positions[3] = vec2[](
    vec2(0.0, -0.65),
    vec2(0.65, 0.55),
    vec2(-0.65, 0.55)
);

const vec3 Colors[3] = vec3[](
    vec3(1.0, 0.25, 0.2),
    vec3(0.2, 0.85, 0.45),
    vec3(0.2, 0.55, 1.0)
);

void main()
{
    gl_Position = vec4(Positions[gl_VertexIndex], 0.0, 1.0);
    color = Colors[gl_VertexIndex];
}

#version 440
layout(location = 0) out vec2 uv;
layout(std140, binding = 1) uniform Present { vec4 params; vec4 clearColor; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
    uv = vec2(position.x, 1.0 - position.y);
    if (params.x > 0.5) uv.y = 1.0 - uv.y;
}

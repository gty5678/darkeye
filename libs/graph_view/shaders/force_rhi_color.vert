#version 440

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aLocal;
layout(location = 2) in float aShape;
layout(location = 3) in vec4 aColor;

layout(std140, binding = 0) uniform Transform
{
    mat4 uMvp;
};

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vLocal;
layout(location = 2) out float vShape;

void main()
{
    vColor = aColor;
    vLocal = aLocal;
    vShape = aShape;
    gl_Position = uMvp * vec4(aPos, 0.0, 1.0);
}



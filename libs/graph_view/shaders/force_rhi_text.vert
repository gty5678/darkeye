#version 440

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aRangeScale;

layout(std140, binding = 0) uniform TextUniforms
{
    mat4 uMvp;
    float uScreenPxRange;
};

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vRangeScale;

void main()
{
    vUv = aUv;
    vColor = aColor;
    vRangeScale = aRangeScale;
    gl_Position = uMvp * vec4(aPos, 0.0, 1.0);
}



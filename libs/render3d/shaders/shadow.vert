#version 440
#extension GL_GOOGLE_include_directive : enable
#include "lighting.glsl"
layout(location=0) in vec3 position;
layout(location=3) in vec2 texcoord;
layout(location=0) out vec2 uv;
layout(std140,binding=0) uniform Object {
    mat4 mvp; mat4 model; mat4 normalMatrix;
    vec4 baseColor; vec4 eye; vec4 material; vec4 flags;
};
void main()
{
    uv=texcoord;
    gl_Position=frame.lightViewProjection*model*vec4(position,1);
}

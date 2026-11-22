#version 440
#extension GL_GOOGLE_include_directive : enable
#include "lighting.glsl"
layout(location=0) in vec3 position;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 texcoord;
layout(location=0) out vec2 uv;
layout(location=1) out vec3 viewNormal;
layout(location=2) out vec3 viewPosition;
layout(std140,binding=0) uniform Object {
    mat4 mvp; mat4 model; mat4 normalMatrix;
    vec4 baseColor; vec4 eye; vec4 material; vec4 flags;
};
void main()
{
    uv=texcoord;
    viewNormal=mat3(frame.view)*mat3(normalMatrix)*normal;
    viewPosition=(frame.view*model*vec4(position,1)).xyz;
    gl_Position=mvp*vec4(position,1);
}

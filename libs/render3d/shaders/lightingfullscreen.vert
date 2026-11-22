#version 440
#extension GL_GOOGLE_include_directive : enable
#include "lighting.glsl"
layout(location=0) out vec2 uv;
void main()
{
    vec2 p=vec2(float((gl_VertexIndex<<1)&2),float(gl_VertexIndex&2));
    gl_Position=vec4(p*2.0-1.0,0,1);
    uv=textureUv(vec2(p.x,1.0-p.y));
}

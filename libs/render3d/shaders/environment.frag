#version 440
#extension GL_GOOGLE_include_directive : enable
#include "lighting.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 fragColor;
layout(binding=9) uniform sampler2D radianceMap;
void main()
{
    vec2 top=textureUv(uv);
    vec4 viewRay=frame.inverseProjection*vec4(top.x*2.0-1.0,1.0-top.y*2.0,1,1);
    vec3 worldRay=transpose(mat3(frame.view))*normalize(viewRay.xyz/viewRay.w);
    fragColor=vec4(texture(radianceMap,environmentUv(worldRay)).rgb*frame.environment.x,1);
}

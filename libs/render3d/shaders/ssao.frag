#version 440
#extension GL_GOOGLE_include_directive : enable
#include "lighting.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 fragColor;
layout(binding=0) uniform sampler2D normalDepth;
vec3 reconstruct(vec2 tex,float depth)
{
    vec2 top=textureUv(tex);
    vec4 ray=frame.inverseProjection*vec4(top.x*2.0-1.0,1.0-top.y*2.0,1,1);
    return ray.xyz*(depth/max(-ray.z,0.0001));
}
void main()
{
    vec4 data=texture(normalDepth,uv);
    if(data.w<=0.0) { fragColor=vec4(1); return; }
    vec3 n=normalize(data.xyz),p=reconstruct(uv,data.w);
    vec3 tangent=normalize(cross(abs(n.y)<0.99?vec3(0,1,0):vec3(1,0,0),n));
    mat3 basis=mat3(tangent,cross(n,tangent),n);
    // A deterministic hemisphere kernel: no temporal noise or idle redraws.
    float occlusion=0.0,weight=0.0;
    for(int i=0;i<24;++i) {
        float f=(float(i)+0.5)/24.0,angle=float(i)*2.39996323;
        float z=0.15+0.85*f,r=sqrt(1.0-z*z);
        vec3 offset=basis*vec3(cos(angle)*r,sin(angle)*r,z)*frame.ao.x*(0.2+0.8*f*f);
        vec3 test=p+offset;
        vec4 clip=frame.projection*vec4(test,1);
        if(clip.w<=0.0) continue;
        vec2 projected=vec2(clip.x/clip.w*0.5+0.5,0.5-clip.y/clip.w*0.5);
        if(any(lessThan(projected,vec2(0))) || any(greaterThan(projected,vec2(1)))) continue;
        vec4 neighbor=texture(normalDepth,textureUv(projected));
        if(neighbor.w<=0.0) continue;
        float range=1.0-smoothstep(frame.ao.x*0.5,frame.ao.x,abs(neighbor.w-data.w));
        occlusion+=(neighbor.w < -test.z-frame.ao.x*0.025?1.0:0.0)*range;
        weight+=1.0;
    }
    float ao=clamp(1.0-frame.ao.y*occlusion/max(weight,1.0),0.15,1.0);
    fragColor=vec4(ao,ao,ao,1);
}

#version 440
layout(location=0) in vec2 uv;
layout(location=1) in vec3 viewNormal;
layout(location=2) in vec3 viewPosition;
layout(location=0) out vec4 fragColor;
layout(binding=1) uniform sampler2D colorTexture;
layout(std140,binding=0) uniform Object {
    mat4 mvp; mat4 model; mat4 normalMatrix;
    vec4 baseColor; vec4 eye; vec4 material; vec4 flags;
};
void main()
{
    float alpha=texture(colorTexture,uv).a*baseColor.a;
    if((flags.y>0.5 && flags.y<1.5 && alpha<material.z) || (flags.y>1.5 && alpha<0.99)) discard;
    vec3 n=normalize(viewNormal);
    if(dot(n,-viewPosition)<0.0) n=-n;
    fragColor=vec4(n,-viewPosition.z);
}

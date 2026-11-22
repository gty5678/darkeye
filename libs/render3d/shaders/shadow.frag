#version 440
layout(location=0) in vec2 uv;
layout(location=0) out vec4 fragColor;
layout(binding=1) uniform sampler2D colorTexture;
layout(std140,binding=0) uniform Object {
    mat4 mvp; mat4 model; mat4 normalMatrix;
    vec4 baseColor; vec4 eye; vec4 material; vec4 flags;
};
void main()
{
    float alpha=texture(colorTexture,uv).a*baseColor.a;
    if((flags.y>0.5 && flags.y<1.5 && alpha<material.z) || (flags.y>1.5 && alpha<0.5)) discard;
    // Receiver uses a 3x3 PCF footprint. Offset the color depth as well as
    // depth testing: hardware raster depth bias alone cannot bias this R32F output.
    float slope=max(abs(dFdx(gl_FragCoord.z)),abs(dFdy(gl_FragCoord.z)));
    fragColor=vec4(min(1.0,gl_FragCoord.z+max(0.0005,2.0*slope)));
}

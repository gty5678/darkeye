#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D linearScene;
layout(std140,binding=1) uniform Present { vec4 params; vec4 clearColor; };
vec3 linearToSrgb(vec3 c)
{
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main()
{
    vec4 color = texture(linearScene, uv);
    vec3 straight = color.a > 0.00001 ? color.rgb / color.a : vec3(0.0);
    straight*=params.z;
    if(params.y>0.5) {
        vec3 x=max(straight,vec3(0));
        straight=clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
    }
    vec4 scene = vec4(linearToSrgb(straight) * color.a, color.a);
    fragColor = scene + vec4(clearColor.rgb * clearColor.a, clearColor.a) * (1.0 - color.a);
}

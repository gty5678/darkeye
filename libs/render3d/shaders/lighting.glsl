layout(std140, binding = 7) uniform Frame {
    mat4 view;
    mat4 projection;
    mat4 inverseProjection;
    mat4 inverseViewProjection;
    mat4 lightViewProjection;
    vec4 enabled; // IBL, HDR background, AO, shadows
    vec4 environment; // intensity, rotation radians, max specular mip, zero-to-one clip
    vec4 viewport; // width, height, framebuffer Y up, shadow UV Y flip
    vec4 ao; // radius, strength, tone map, exposure
    vec4 keyLight; // world direction toward source, intensity
} frame;
const float PI = 3.14159265359;
vec2 environmentUv(vec3 d)
{
    float c=cos(frame.environment.y),s=sin(frame.environment.y);
    d=vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
    return vec2(atan(d.z,d.x)/(2.0*PI)+0.5,acos(clamp(d.y,-1.0,1.0))/PI);
}
vec2 textureUv(vec2 topDown)
{
    return vec2(topDown.x,frame.viewport.z>0.5?1.0-topDown.y:topDown.y);
}

#version 440
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 texcoord;
layout(location = 4) in vec4 tangent;
layout(location = 0) out vec3 vertexColor;
layout(location = 1) out vec2 uv;
layout(location = 2) out vec3 worldPosition;
layout(location = 3) out vec3 worldNormal;
layout(location = 4) out vec4 worldTangent;
layout(std140, binding = 0) uniform Object {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColor;
    vec4 eye;
    vec4 material;
    vec4 flags;
    vec4 surface;
    vec4 emission;
    vec4 transmission; // factor, IOR, model-space thickness, inverse attenuation distance
    vec4 attenuation; // linear absorption color
    vec4 sparkle; // time, seed, enabled, reserved
};
void main()
{
    vertexColor = color;
    uv = texcoord;
    worldPosition = (model * vec4(position, 1.0)).xyz;
    worldNormal = (normalMatrix * vec4(normal, 0.0)).xyz;
    worldTangent=vec4((model*vec4(tangent.xyz,0.0)).xyz,tangent.w*flags.w);
    gl_Position = mvp * vec4(position, 1.0);
}

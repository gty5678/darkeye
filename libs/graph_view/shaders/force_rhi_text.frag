#version 440

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vRangeScale;

layout(std140, binding = 0) uniform TextUniforms
{
    mat4 uMvp;
    float uScreenPxRange;
};

layout(binding = 1) uniform sampler2D uAtlas;

layout(location = 0) out vec4 fragColor;

float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

void main()
{
    vec3 msdf = texture(uAtlas, vUv).rgb;
    float sd = median(msdf.r, msdf.g, msdf.b);
    float screenPxDist = uScreenPxRange * vRangeScale * (sd - 0.5);
    float alpha = clamp(screenPxDist + 0.5, 0.0, 1.0) * vColor.a;
    fragColor = vec4(vColor.rgb, alpha);
}



#version 440

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vLocal;
layout(location = 2) in float vShape;
layout(location = 0) out vec4 fragColor;

void main()
{
    float coverage = 1.0;
    if (vShape > 0.5 && vShape < 1.5) {
        float dist = abs(vLocal.x);
        float edge = min(fwidth(dist), 0.5);
        coverage = 1.0 - smoothstep(1.0 - edge, 1.0, dist);
    } else if (vShape >= 1.5) {
        float dist = length(vLocal);
        float edge = fwidth(dist);
        coverage = 1.0 - smoothstep(1.0 - edge, 1.0 + edge, dist);
    }

    float alpha = vColor.a * coverage;
    if (alpha < 0.001) {
        discard;
    }
    fragColor = vec4(vColor.rgb, alpha);
}



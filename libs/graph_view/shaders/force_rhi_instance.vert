#version 440
layout(location = 0) in vec4 positions;
layout(location = 1) in vec4 shape;
layout(location = 2) in vec4 color;
layout(std140, binding = 0) uniform Transform { mat4 uMvp; vec4 options; };
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vLocal;
layout(location = 2) out float vShape;
void main() {
    int v = gl_VertexIndex % 9;
    vec2 x = positions.xy;
    vColor = color;
    vLocal = vec2(0.0);
    vShape = shape.z;
    if (shape.z >= 1.5) {
        const vec2 corners[6] = vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
        if (v < 6) { vLocal = corners[v] * 1.2; x += vLocal * shape.x; }
        else vColor.a = 0.0;
    } else {
        vec2 delta = positions.zw - positions.xy;
        float len = length(delta);
        vec2 dir = delta / max(len, 1e-6);
        vec2 normal = vec2(-dir.y, dir.x);
        vec2 a = positions.xy + dir * shape.x;
        vec2 b = positions.zw - dir * shape.y;
        if (len <= shape.x + shape.y + 1e-5) vColor.a = 0.0;
        if (v < 6) {
            const int ends[6] = int[6](0,0,1,0,1,1);
            const float sides[6] = float[6](1,-1,-1,1,-1,1);
            x = mix(a, b, float(ends[v])) + normal * sides[v] * options.x;
            vLocal = vec2(sides[v], 0.0);
        } else {
            vShape = 0.0;
            if (options.z < 0.5) vColor.a = 0.0;
            x = v == 6 ? b : b - dir * 4.0 * options.y + normal * (v == 7 ? 1.5 : -1.5) * options.y;
        }
    }
    gl_Position = uMvp * vec4(x, 0.0, 1.0);
}

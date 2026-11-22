#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D linearScene;
void main()
{
    // Keep premultiplied linear color; display conversion happens only once.
    fragColor = texture(linearScene, uv);
}

#version 450

layout(binding = 0) uniform sampler2D atlasSampler;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Negative UVs mark untextured overlay geometry (Renderer2D::Render).
    outColor = fragUV.x < 0.0 ? fragColor : texture(atlasSampler, fragUV) * fragColor;
}

#version 450
/* Positions (R32G32_SFLOAT) and colors (R8G8B8A8_UNORM) from an interleaved vertex buffer. */
layout(location = 0) in vec2 pos;
layout(location = 1) in vec4 color;
layout(location = 0) out vec3 col;
void main() { gl_Position = vec4(pos, 0.0, 1.0); col = color.rgb; }

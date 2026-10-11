#version 450
/* Reads gl_PrimitiveID from a plain vertex shader (no GS): primitive 0 -> red, others -> blue. */
layout(location = 0) out vec4 o;
void main() { o = gl_PrimitiveID == 0 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 0.0, 1.0, 1.0); }

#version 450
/* Like discard.frag but without a depth export: depth comes from the rasterizer (0.5). */
layout(location = 0) out vec4 o;
void main() {
   if (gl_FragCoord.x < 32.0)
      discard;
   o = vec4(1.0, 1.0, 0.0, 1.0);
}

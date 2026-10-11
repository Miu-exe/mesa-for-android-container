#version 450
/* Writes depth and discards the left half of a 64x64 target: whole waves take the discard exit
 * while the shader has a depth export. */
layout(location = 0) out vec4 o;
void main() {
   if (gl_FragCoord.x < 32.0)
      discard;
   gl_FragDepth = 0.25;
   o = vec4(1.0, 1.0, 0.0, 1.0);
}

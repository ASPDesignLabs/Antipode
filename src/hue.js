/* Antipode — shared hue math (JS + GLSL).
 * The whole game is one rule: the deadly color is your hue + 180 degrees.
 * Hues are normalized 0..1 everywhere, so the complement is always fract(h + 0.5).
 */
(function (AP) {
  'use strict';

  AP.modes = [];
  AP.complement = function (h) { return (h + 0.5) % 1; };

  // Prepended to every fragment shader by the engine.
  AP.GLSL_HUE = `
vec3 hsv2rgb(vec3 c){
  vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);
  return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}
float complementOf(float h){ return fract(h + 0.5); }
// circular distance between two hues, 0..0.5
float hueDist(float a, float b){
  float d = abs(fract(a) - fract(b));
  return min(d, 1.0 - d);
}
// 1.0 when hue h is (within tol of) the complement of ref, else 0.0
float isComplement(float h, float ref, float tol){
  return 1.0 - smoothstep(tol * 0.7, tol, hueDist(h, complementOf(ref)));
}
`;
})(window.AP = window.AP || {});

// Bloom step 1: half-resolution box downsample that keeps only bright parts.
// params[0] = (threshold, soft knee, unused, unused)
void main() {
    vec2 t = 1.0 / vec2(textureSize(u_texture0, 0));
    vec3 c = (texture(u_texture0, v_uv + t * vec2(-1.0, -1.0)).rgb +
              texture(u_texture0, v_uv + t * vec2( 1.0, -1.0)).rgb +
              texture(u_texture0, v_uv + t * vec2(-1.0,  1.0)).rgb +
              texture(u_texture0, v_uv + t * vec2( 1.0,  1.0)).rgb) * 0.25;
    float br = max(c.r, max(c.g, c.b));
    float threshold = material.params[0].x;
    float knee = threshold * material.params[0].y + 0.00001;
    float soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contrib = max(soft, br - threshold) / max(br, 0.00001);
    o_color = vec4(c * contrib, 1.0);
}

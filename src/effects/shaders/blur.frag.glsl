// One direction of a 9-tap Gaussian blur using linear filtering.
// params[0].xy = step direction in pixels.
void main() {
    vec2 d = material.params[0].xy / vec2(textureSize(u_texture0, 0));
    vec4 c = texture(u_texture0, v_uv) * 0.2270270270;
    c += texture(u_texture0, v_uv + d * 1.3846153846) * 0.3162162162;
    c += texture(u_texture0, v_uv - d * 1.3846153846) * 0.3162162162;
    c += texture(u_texture0, v_uv + d * 3.2307692308) * 0.0702702703;
    c += texture(u_texture0, v_uv - d * 3.2307692308) * 0.0702702703;
    o_color = c;
}

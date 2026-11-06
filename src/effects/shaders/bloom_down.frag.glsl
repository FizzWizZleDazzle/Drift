// Bloom downsample (dual filter). params[0].x = spread.
void main() {
    vec2 h = 0.5 / vec2(textureSize(u_texture0, 0)) * material.params[0].x;
    vec3 c = texture(u_texture0, v_uv).rgb * 4.0;
    c += texture(u_texture0, v_uv - h).rgb;
    c += texture(u_texture0, v_uv + h).rgb;
    c += texture(u_texture0, v_uv + vec2(h.x, -h.y)).rgb;
    c += texture(u_texture0, v_uv - vec2(h.x, -h.y)).rgb;
    o_color = vec4(c * 0.125, 1.0);
}

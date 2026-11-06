// Bloom upsample (dual filter), added onto the next larger level.
// params[0].x = spread.
void main() {
    vec2 h = 0.5 / vec2(textureSize(u_texture0, 0)) * material.params[0].x;
    vec3 c = texture(u_texture0, v_uv + vec2(-h.x * 2.0, 0.0)).rgb;
    c += texture(u_texture0, v_uv + vec2(-h.x, h.y)).rgb * 2.0;
    c += texture(u_texture0, v_uv + vec2(0.0, h.y * 2.0)).rgb;
    c += texture(u_texture0, v_uv + vec2(h.x, h.y)).rgb * 2.0;
    c += texture(u_texture0, v_uv + vec2(h.x * 2.0, 0.0)).rgb;
    c += texture(u_texture0, v_uv + vec2(h.x, -h.y)).rgb * 2.0;
    c += texture(u_texture0, v_uv + vec2(0.0, -h.y * 2.0)).rgb;
    c += texture(u_texture0, v_uv + vec2(-h.x, -h.y)).rgb * 2.0;
    o_color = vec4(c / 12.0, 1.0);
}

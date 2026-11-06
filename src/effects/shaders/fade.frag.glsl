// params[0] = color, params[1].x = amount
void main() {
    vec4 c = texture(u_texture0, v_uv);
    o_color = vec4(mix(c.rgb, material.params[0].rgb, clamp(material.params[1].x, 0.0, 1.0)), c.a);
}

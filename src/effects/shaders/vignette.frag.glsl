// params[0] = (intensity, radius, softness, unused), params[1] = color
void main() {
    vec4 c = texture(u_texture0, v_uv);
    vec4 p = material.params[0];
    float d = length(v_uv - vec2(0.5));
    float v = smoothstep(p.y - p.z, p.y, d) * p.x;
    o_color = vec4(mix(c.rgb, material.params[1].rgb, v), c.a);
}

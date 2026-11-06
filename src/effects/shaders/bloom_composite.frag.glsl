// Bloom last step: scene (texture0) plus the blurred bright parts (texture1).
// params[0] = (tint.rgb * intensity, unused)
void main() {
    vec4 scene = texture(u_texture0, v_uv);
    vec3 glow = texture(u_texture1, v_uv).rgb * material.params[0].rgb;
    o_color = vec4(scene.rgb + glow, scene.a);
}

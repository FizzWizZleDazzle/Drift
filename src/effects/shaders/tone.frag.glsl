// Grayscale and sepia. params[0] = (grayscale amount, sepia amount, unused, unused)
void main() {
    vec4 c = texture(u_texture0, v_uv);
    float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 rgb = mix(c.rgb, vec3(l), material.params[0].x);
    vec3 sepia = vec3(dot(c.rgb, vec3(0.393, 0.769, 0.189)),
                      dot(c.rgb, vec3(0.349, 0.686, 0.168)),
                      dot(c.rgb, vec3(0.272, 0.534, 0.131)));
    rgb = mix(rgb, min(sepia, vec3(1.0)), material.params[0].y);
    o_color = vec4(rgb, c.a);
}

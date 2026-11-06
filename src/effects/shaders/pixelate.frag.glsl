// params[0].x = pixel size in scene pixels.
void main() {
    vec2 size = vec2(textureSize(u_texture0, 0));
    vec2 cell = max(material.params[0].x, 1.0) / size;
    vec2 uv = (floor(v_uv / cell) + 0.5) * cell;
    o_color = texture(u_texture0, uv);
}

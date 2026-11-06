// params[0] = (curvature, scanline darkness, scanline spacing px, aberration px)
// params[1] = (vignette, unused, unused, unused)
void main() {
    vec4 p = material.params[0];
    vec2 size = vec2(textureSize(u_texture0, 0));
    vec2 q = v_uv * 2.0 - 1.0;
    q *= 1.0 + dot(q, q) * p.x * 0.25;
    vec2 uv = q * 0.5 + 0.5;
    vec2 shift = (uv - 0.5) * 2.0 * p.w / size;
    float r = texture(u_texture0, uv + shift).r;
    float g = texture(u_texture0, uv).g;
    float b = texture(u_texture0, uv - shift).b;
    float scan = 1.0 - p.y * (0.5 - 0.5 * cos(uv.y * size.y / max(p.z, 1.0) * 6.2831853));
    float edge = clamp(16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y), 0.0, 1.0);
    float vig = mix(1.0, pow(edge, 0.35), material.params[1].x);
    float inside = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    o_color = vec4(vec3(r, g, b) * scan * vig * inside, 1.0);
}

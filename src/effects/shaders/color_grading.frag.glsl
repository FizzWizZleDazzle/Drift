// params[0] = (brightness, contrast, saturation, LUT strength)
// params[1] = tint, params[2].x = LUT size N (0 = no LUT); LUT in texture1.
vec3 lookup(vec3 c, float n) {
    c = clamp(c, 0.0, 1.0);
    float b = c.b * (n - 1.0);
    float s0 = floor(b);
    float s1 = min(s0 + 1.0, n - 1.0);
    vec2 inSlice = (c.rg * (n - 1.0) + 0.5) / vec2(n * n, n);
    vec3 a = texture(u_texture1, inSlice + vec2(s0 / n, 0.0)).rgb;
    vec3 d = texture(u_texture1, inSlice + vec2(s1 / n, 0.0)).rgb;
    return mix(a, d, b - s0);
}

void main() {
    vec4 src = texture(u_texture0, v_uv);
    vec4 p = material.params[0];
    vec3 c = src.rgb * p.x;
    c = (c - 0.5) * p.y + 0.5;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l), c, p.z);
    c *= material.params[1].rgb;
    float n = max(material.params[2].x, 2.0);
    vec3 graded = lookup(c, n);
    float useLut = material.params[2].x > 1.0 ? p.w : 0.0;
    o_color = vec4(mix(c, graded, useLut), src.a);
}

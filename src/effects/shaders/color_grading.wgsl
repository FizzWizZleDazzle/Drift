// params[0] = (brightness, contrast, saturation, LUT strength)
// params[1] = tint, params[2].x = LUT size N (0 = no LUT); LUT in texture1.
fn lookup(cin: vec3<f32>, n: f32) -> vec3<f32> {
    let c = clamp(cin, vec3<f32>(0.0), vec3<f32>(1.0));
    let b = c.b * (n - 1.0);
    let s0 = floor(b);
    let s1 = min(s0 + 1.0, n - 1.0);
    let inSlice = (c.rg * (n - 1.0) + 0.5) / vec2<f32>(n * n, n);
    let a = textureSample(texture1, sampler1, inSlice + vec2<f32>(s0 / n, 0.0)).rgb;
    let d = textureSample(texture1, sampler1, inSlice + vec2<f32>(s1 / n, 0.0)).rgb;
    return mix(a, d, b - s0);
}

fn fragment(in: FragmentInput) -> vec4<f32> {
    let src = textureSample(texture0, sampler0, in.uv);
    let p = material.params[0];
    var c = src.rgb * p.x;
    c = (c - 0.5) * p.y + 0.5;
    let l = dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
    c = mix(vec3<f32>(l), c, p.z);
    c = c * material.params[1].rgb;
    let n = max(material.params[2].x, 2.0);
    let graded = lookup(c, n);
    let useLut = select(0.0, p.w, material.params[2].x > 1.0);
    return vec4<f32>(mix(c, graded, useLut), src.a);
}

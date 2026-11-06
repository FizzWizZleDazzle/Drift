// params[0] = (curvature, scanline darkness, scanline spacing px, aberration px)
// params[1] = (vignette, unused, unused, unused)
fn fragment(in: FragmentInput) -> vec4<f32> {
    let p = material.params[0];
    let size = vec2<f32>(textureDimensions(texture0));
    var q = in.uv * 2.0 - 1.0;
    q = q * (1.0 + dot(q, q) * p.x * 0.25);
    let uv = q * 0.5 + 0.5;
    let shift = (uv - 0.5) * 2.0 * p.w / size;
    let r = textureSample(texture0, sampler0, uv + shift).r;
    let g = textureSample(texture0, sampler0, uv).g;
    let b = textureSample(texture0, sampler0, uv - shift).b;
    let scan = 1.0 - p.y * (0.5 - 0.5 * cos(uv.y * size.y / max(p.z, 1.0) * 6.2831853));
    let edge = clamp(16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y), 0.0, 1.0);
    let vig = mix(1.0, pow(edge, 0.35), material.params[1].x);
    let inside = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    return vec4<f32>(vec3<f32>(r, g, b) * scan * vig * inside, 1.0);
}

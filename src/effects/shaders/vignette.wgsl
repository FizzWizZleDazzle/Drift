// params[0] = (intensity, radius, softness, unused), params[1] = color
fn fragment(in: FragmentInput) -> vec4<f32> {
    let c = textureSample(texture0, sampler0, in.uv);
    let p = material.params[0];
    let d = length(in.uv - vec2<f32>(0.5, 0.5));
    let v = smoothstep(p.y - p.z, p.y, d) * p.x;
    return vec4<f32>(mix(c.rgb, material.params[1].rgb, v), c.a);
}

// params[0] = color, params[1].x = amount
fn fragment(in: FragmentInput) -> vec4<f32> {
    let c = textureSample(texture0, sampler0, in.uv);
    return vec4<f32>(mix(c.rgb, material.params[0].rgb, clamp(material.params[1].x, 0.0, 1.0)), c.a);
}

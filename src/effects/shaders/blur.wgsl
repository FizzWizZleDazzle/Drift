// One direction of a 9-tap Gaussian blur using linear filtering.
// params[0].xy = step direction in pixels.
fn fragment(in: FragmentInput) -> vec4<f32> {
    let d = material.params[0].xy / vec2<f32>(textureDimensions(texture0));
    var c = textureSample(texture0, sampler0, in.uv) * 0.2270270270;
    c += textureSample(texture0, sampler0, in.uv + d * 1.3846153846) * 0.3162162162;
    c += textureSample(texture0, sampler0, in.uv - d * 1.3846153846) * 0.3162162162;
    c += textureSample(texture0, sampler0, in.uv + d * 3.2307692308) * 0.0702702703;
    c += textureSample(texture0, sampler0, in.uv - d * 3.2307692308) * 0.0702702703;
    return c;
}

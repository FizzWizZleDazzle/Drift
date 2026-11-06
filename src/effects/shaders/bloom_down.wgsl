// Bloom downsample (dual filter). params[0].x = spread.
fn fragment(in: FragmentInput) -> vec4<f32> {
    let h = 0.5 / vec2<f32>(textureDimensions(texture0)) * material.params[0].x;
    var c = textureSample(texture0, sampler0, in.uv).rgb * 4.0;
    c += textureSample(texture0, sampler0, in.uv - h).rgb;
    c += textureSample(texture0, sampler0, in.uv + h).rgb;
    c += textureSample(texture0, sampler0, in.uv + vec2<f32>(h.x, -h.y)).rgb;
    c += textureSample(texture0, sampler0, in.uv - vec2<f32>(h.x, -h.y)).rgb;
    return vec4<f32>(c * 0.125, 1.0);
}

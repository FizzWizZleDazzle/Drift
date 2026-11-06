// Bloom upsample (dual filter), added onto the next larger level.
// params[0].x = spread.
fn fragment(in: FragmentInput) -> vec4<f32> {
    let h = 0.5 / vec2<f32>(textureDimensions(texture0)) * material.params[0].x;
    var c = textureSample(texture0, sampler0, in.uv + vec2<f32>(-h.x * 2.0, 0.0)).rgb;
    c += textureSample(texture0, sampler0, in.uv + vec2<f32>(-h.x, h.y)).rgb * 2.0;
    c += textureSample(texture0, sampler0, in.uv + vec2<f32>(0.0, h.y * 2.0)).rgb;
    c += textureSample(texture0, sampler0, in.uv + vec2<f32>(h.x, h.y)).rgb * 2.0;
    c += textureSample(texture0, sampler0, in.uv + vec2<f32>(h.x * 2.0, 0.0)).rgb;
    c += textureSample(texture0, sampler0, in.uv + vec2<f32>(h.x, -h.y)).rgb * 2.0;
    c += textureSample(texture0, sampler0, in.uv + vec2<f32>(0.0, -h.y * 2.0)).rgb;
    c += textureSample(texture0, sampler0, in.uv + vec2<f32>(-h.x, -h.y)).rgb * 2.0;
    return vec4<f32>(c / 12.0, 1.0);
}

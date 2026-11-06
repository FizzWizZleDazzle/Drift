// Bloom step 1: half-resolution box downsample that keeps only bright parts.
// params[0] = (threshold, soft knee, unused, unused)
fn fragment(in: FragmentInput) -> vec4<f32> {
    let t = 1.0 / vec2<f32>(textureDimensions(texture0));
    let c = (textureSample(texture0, sampler0, in.uv + t * vec2<f32>(-1.0, -1.0)).rgb +
             textureSample(texture0, sampler0, in.uv + t * vec2<f32>( 1.0, -1.0)).rgb +
             textureSample(texture0, sampler0, in.uv + t * vec2<f32>(-1.0,  1.0)).rgb +
             textureSample(texture0, sampler0, in.uv + t * vec2<f32>( 1.0,  1.0)).rgb) * 0.25;
    let br = max(c.r, max(c.g, c.b));
    let threshold = material.params[0].x;
    let knee = threshold * material.params[0].y + 0.00001;
    var soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    let contrib = max(soft, br - threshold) / max(br, 0.00001);
    return vec4<f32>(c * contrib, 1.0);
}

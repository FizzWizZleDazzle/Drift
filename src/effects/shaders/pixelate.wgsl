// params[0].x = pixel size in scene pixels.
fn fragment(in: FragmentInput) -> vec4<f32> {
    let size = vec2<f32>(textureDimensions(texture0));
    let cell = max(material.params[0].x, 1.0) / size;
    let uv = (floor(in.uv / cell) + 0.5) * cell;
    return textureSample(texture0, sampler0, uv);
}

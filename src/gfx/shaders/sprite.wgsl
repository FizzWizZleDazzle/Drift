fn fragment(in: FragmentInput) -> vec4<f32> {
    return textureSample(texture0, sampler0, in.uv) * in.color;
}

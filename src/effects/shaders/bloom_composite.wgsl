// Bloom last step: scene (texture0) plus the blurred bright parts (texture1).
// params[0] = (tint.rgb * intensity, unused)
fn fragment(in: FragmentInput) -> vec4<f32> {
    let scene = textureSample(texture0, sampler0, in.uv);
    let glow = textureSample(texture1, sampler1, in.uv).rgb * material.params[0].rgb;
    return vec4<f32>(scene.rgb + glow, scene.a);
}

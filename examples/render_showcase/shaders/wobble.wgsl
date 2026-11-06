// Sprite shader: a gentle color wave across the world, scaled by params[0].x.
fn fragment(in: FragmentInput) -> vec4<f32> {
    let c = textureSample(texture0, sampler0, in.uv) * in.color;
    let wave = 0.5 + 0.5 * sin(in.world.x * 0.01 + in.world.y * 0.013 + frame.time.x * 2.0);
    let tint = mix(vec3<f32>(1.0), vec3<f32>(0.6, 1.0, 0.9), wave * material.params[0].x);
    return vec4<f32>(c.rgb * tint, c.a);
}

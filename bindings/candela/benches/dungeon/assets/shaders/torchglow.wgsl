fn hash(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(127.1, 311.7))) * 43758.5453);
}

fn fragment(in: FragmentInput) -> vec4<f32> {
    let t = frame.time.x;
    let size = frame.time.zw;
    let look = material.params[0];
    let tint = material.params[1];

    var uv = in.uv;
    for (var i = 2; i < 8; i = i + 1) {
        let torch = material.params[i];
        if (torch.w > 0.5) {
            let d = (in.uv - torch.xy) * vec2<f32>(size.x / size.y, 1.0);
            let above = clamp(-d.y / torch.z, 0.0, 1.0);
            let near = 1.0 - smoothstep(0.0, torch.z, length(vec2<f32>(d.x * 2.2, d.y)));
            let heat = above * near * look.w;
            uv.x = uv.x + sin(in.uv.y * 140.0 - t * 9.0 + torch.x * 40.0) * 0.0035 * heat;
            uv.y = uv.y + cos(in.uv.x * 110.0 - t * 7.0) * 0.0018 * heat;
        }
    }

    var c = textureSample(texture0, sampler0, uv);
    let rgb = mix(c.rgb, c.rgb * tint.rgb, tint.a);

    let d = length(in.uv - vec2<f32>(0.5, 0.5));
    let v = smoothstep(look.y - 0.35, look.y, d) * look.x;
    let grain = (hash(in.uv * size + vec2<f32>(t * 61.0, t * 37.0)) - 0.5) * look.z;

    return vec4<f32>(rgb * (1.0 - v) + vec3<f32>(grain), c.a);
}

// Grayscale and sepia. params[0] = (grayscale amount, sepia amount, unused, unused)
fn fragment(in: FragmentInput) -> vec4<f32> {
    let c = textureSample(texture0, sampler0, in.uv);
    let l = dot(c.rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
    var rgb = mix(c.rgb, vec3<f32>(l), material.params[0].x);
    let sepia = vec3<f32>(dot(c.rgb, vec3<f32>(0.393, 0.769, 0.189)),
                          dot(c.rgb, vec3<f32>(0.349, 0.686, 0.168)),
                          dot(c.rgb, vec3<f32>(0.272, 0.534, 0.131)));
    rgb = mix(rgb, min(sepia, vec3<f32>(1.0)), material.params[0].y);
    return vec4<f32>(rgb, c.a);
}

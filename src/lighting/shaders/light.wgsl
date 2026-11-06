// One 2D light, drawn additively into a light map in world space.
// params[0] = (position.x, position.y, radius, falloff exponent)
// params[1] = (color.rgb * intensity, unused)
// params[2] = (direction.x, direction.y, cos outer angle, cos inner angle);
//             outer < -1 marks a point light (no cone)
fn fragment(in: FragmentInput) -> vec4<f32> {
    let p = material.params[0];
    let d = in.world - p.xy;
    let dist = length(d);
    var att = pow(clamp(1.0 - dist / p.z, 0.0, 1.0), p.w);
    let s = material.params[2];
    if (s.z > -1.5) {
        let c = dot(d / max(dist, 0.0001), s.xy);
        att = att * smoothstep(s.z, s.w, c);
    }
    return vec4<f32>(material.params[1].rgb * att, 1.0);
}

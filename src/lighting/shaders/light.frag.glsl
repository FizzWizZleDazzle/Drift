// One 2D light, drawn additively into a light map in world space.
// params[0] = (position.x, position.y, radius, falloff exponent)
// params[1] = (color.rgb * intensity, unused)
// params[2] = (direction.x, direction.y, cos outer angle, cos inner angle);
//             outer < -1 marks a point light (no cone)
void main() {
    vec4 p = material.params[0];
    vec2 d = v_world - p.xy;
    float dist = length(d);
    float att = pow(clamp(1.0 - dist / p.z, 0.0, 1.0), p.w);
    vec4 s = material.params[2];
    if (s.z > -1.5) {
        float c = dot(d / max(dist, 0.0001), s.xy);
        att *= smoothstep(s.z, s.w, c);
    }
    o_color = vec4(material.params[1].rgb * att, 1.0);
}

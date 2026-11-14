#version 300 es

precision highp float;
precision highp int;

struct FrameUniforms {
    mat4x4 view_proj;
    vec4 time;
};
struct MaterialParams {
    vec4 params[8];
};
struct VertexInput {
    vec2 position;
    vec2 uv;
    vec4 color;
};
struct FragmentInput {
    vec4 clip;
    vec2 uv;
    vec4 color;
    vec2 world;
};
layout(std140) uniform FrameUniforms_block_0Vertex { FrameUniforms _group_0_binding_0_vs; };

layout(location = 0) in vec2 _p2vs_location0;
layout(location = 1) in vec2 _p2vs_location1;
layout(location = 2) in vec4 _p2vs_location2;
smooth out vec2 _vs2fs_location0;
smooth out vec4 _vs2fs_location1;
smooth out vec2 _vs2fs_location2;

float hash(vec2 p) {
    return fract((sin(dot(p, vec2(127.1, 311.7))) * 43758.547));
}

void main() {
    VertexInput v = VertexInput(_p2vs_location0, _p2vs_location1, _p2vs_location2);
    FragmentInput out_ = FragmentInput(vec4(0.0), vec2(0.0), vec4(0.0), vec2(0.0));
    mat4x4 _e5 = _group_0_binding_0_vs.view_proj;
    out_.clip = (_e5 * vec4(v.position, 0.0, 1.0));
    out_.uv = v.uv;
    out_.color = v.color;
    out_.world = v.position;
    FragmentInput _e17 = out_;
    gl_Position = _e17.clip;
    _vs2fs_location0 = _e17.uv;
    _vs2fs_location1 = _e17.color;
    _vs2fs_location2 = _e17.world;
    return;
}


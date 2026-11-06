// Drift shader prelude (WGSL). Prepended to every shader before compilation.
// A shader supplies one function:
//
//   fn fragment(in: FragmentInput) -> vec4<f32>
//
// in.uv     texture coordinate of texture0
// in.color  vertex color (sprite tint)
// in.world  world-space position of the fragment
// frame.time.x = seconds since start, .y = frame delta, .zw = target size in pixels
// material.params[0..7] = values set with Material::params

struct FrameUniforms {
    view_proj: mat4x4<f32>,
    time: vec4<f32>,
};

struct MaterialParams {
    params: array<vec4<f32>, 8>,
};

@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(0) @binding(1) var<uniform> material: MaterialParams;

@group(1) @binding(0) var texture0: texture_2d<f32>;
@group(1) @binding(1) var sampler0: sampler;
@group(1) @binding(2) var texture1: texture_2d<f32>;
@group(1) @binding(3) var sampler1: sampler;
@group(1) @binding(4) var texture2: texture_2d<f32>;
@group(1) @binding(5) var sampler2: sampler;
@group(1) @binding(6) var texture3: texture_2d<f32>;
@group(1) @binding(7) var sampler3: sampler;

struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) color: vec4<f32>,
};

struct FragmentInput {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
    @location(2) world: vec2<f32>,
};

@vertex
fn vs_main(v: VertexInput) -> FragmentInput {
    var out: FragmentInput;
    out.clip = frame.view_proj * vec4<f32>(v.position, 0.0, 1.0);
    out.uv = v.uv;
    out.color = v.color;
    out.world = v.position;
    return out;
}

@fragment
fn fs_main(in: FragmentInput) -> @location(0) vec4<f32> {
    return fragment(in);
}

// Drift fragment prelude (GLSL). Prepended to raw GLSL fragment shaders that
// do not start with #version. The shader supplies main() and writes o_color.
precision highp float;

layout(std140) uniform FrameUniforms {
    mat4 view_proj;
    vec4 time;
} frame;

layout(std140) uniform MaterialParams {
    vec4 params[8];
} material;

uniform sampler2D u_texture0;
uniform sampler2D u_texture1;
uniform sampler2D u_texture2;
uniform sampler2D u_texture3;

in vec2 v_uv;
in vec4 v_color;
in vec2 v_world;

layout(location = 0) out vec4 o_color;

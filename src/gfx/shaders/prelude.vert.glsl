// Drift built-in vertex stage (GLSL). The backend prepends #version.
layout(std140) uniform FrameUniforms {
    mat4 view_proj;
    vec4 time;
} frame;

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

out vec2 v_uv;
out vec4 v_color;
out vec2 v_world;

void main() {
    v_uv = a_uv;
    v_color = a_color;
    v_world = a_position;
    gl_Position = frame.view_proj * vec4(a_position, 0.0, 1.0);
}

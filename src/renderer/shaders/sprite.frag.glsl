#version 450

layout(location = 0) in vec2 frag_texcoord;
layout(location = 1) in vec4 frag_color;

layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D tex_sampler;

void main() {
    vec4 tex_color = texture(tex_sampler, frag_texcoord);
    out_color = tex_color * frag_color;
}

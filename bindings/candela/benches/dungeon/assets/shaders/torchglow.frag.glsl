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
layout(std140) uniform FrameUniforms_block_0Fragment { FrameUniforms _group_0_binding_0_fs; };

layout(std140) uniform MaterialParams_block_1Fragment { MaterialParams _group_0_binding_1_fs; };

uniform highp sampler2D _group_1_binding_0_fs;

smooth in vec2 _vs2fs_location0;
smooth in vec4 _vs2fs_location1;
smooth in vec2 _vs2fs_location2;
layout(location = 0) out vec4 _fs2p_location0;

float hash(vec2 p) {
    return fract((sin(dot(p, vec2(127.1, 311.7))) * 43758.547));
}

vec4 fragment(FragmentInput in_1) {
    vec2 uv = vec2(0.0);
    int i = 2;
    vec4 c = vec4(0.0);
    float t = _group_0_binding_0_fs.time.x;
    vec4 _e7 = _group_0_binding_0_fs.time;
    vec2 size = _e7.zw;
    vec4 look = _group_0_binding_1_fs.params[0];
    vec4 tint = _group_0_binding_1_fs.params[1];
    uv = in_1.uv;
    bool loop_init = true;
    while(true) {
        if (!loop_init) {
            int _e96 = i;
            i = (_e96 + 1);
        }
        loop_init = false;
        int _e21 = i;
        if ((_e21 < 8)) {
        } else {
            break;
        }
        {
            int _e26 = i;
            vec4 torch = _group_0_binding_1_fs.params[_e26];
            if ((torch.w > 0.5)) {
                vec2 d = ((in_1.uv - torch.xy) * vec2((size.x / size.y), 1.0));
                float above = clamp((-(d.y) / torch.z), 0.0, 1.0);
                float near = (1.0 - smoothstep(0.0, torch.z, length(vec2((d.x * 2.2), d.y))));
                float heat = ((above * near) * look.w);
                float _e64 = uv.x;
                uv.x = (_e64 + ((sin((((in_1.uv.y * 140.0) - (t * 9.0)) + (torch.x * 40.0))) * 0.0035) * heat));
                float _e83 = uv.y;
                uv.y = (_e83 + ((cos(((in_1.uv.x * 110.0) - (t * 7.0))) * 0.0018) * heat));
            }
        }
    }
    vec2 _e101 = uv;
    vec4 _e102 = texture(_group_1_binding_0_fs, vec2(_e101));
    c = _e102;
    vec4 _e104 = c;
    vec4 _e106 = c;
    vec3 rgb = mix(_e104.xyz, (_e106.xyz * tint.xyz), tint.w);
    float d_1 = length((in_1.uv - vec2(0.5, 0.5)));
    float v_1 = (smoothstep((look.y - 0.35), look.y, d_1) * look.x);
    float _e133 = hash(((in_1.uv * size) + vec2((t * 61.0), (t * 37.0))));
    float grain = ((_e133 - 0.5) * look.z);
    float _e144 = c.w;
    return vec4(((rgb * (1.0 - v_1)) + vec3(grain)), _e144);
}

void main() {
    FragmentInput in_ = FragmentInput(gl_FragCoord, _vs2fs_location0, _vs2fs_location1, _vs2fs_location2);
    vec4 _e1 = fragment(in_);
    _fs2p_location0 = _e1;
    return;
}


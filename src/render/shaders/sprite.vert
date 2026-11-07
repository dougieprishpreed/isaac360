#version 450
// The sprite renderer's vertex stage (sprite_vk.cpp): output pixels to clip space with a vec4 in the push
// constants (a mat4 outside a block member is miscompiled by xost, docs/XSDK_PLAN.md).
layout(push_constant) uniform Frame { vec4 xform; } frame;
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
layout(location = 3) in vec4 a_add;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_col;
layout(location = 2) out vec4 v_add;
void main() {
    gl_Position = vec4(a_pos * frame.xform.xy + frame.xform.zw, 0.0, 1.0);
    v_uv = a_uv;
    v_col = a_col;
    v_add = a_add;
}

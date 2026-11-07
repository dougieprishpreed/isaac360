#version 450
// The original XDK sprite shader: texture times vertex colour plus an additive colour (menu tints).
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_col;
layout(location = 2) in vec4 v_add;
layout(location = 0) out vec4 o_col;
void main() {
    o_col = texture(u_tex, v_uv) * v_col + vec4(v_add.rgb, 0.0);
}

struct VIn { float2 position : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; float3 add : COLOR1; };
struct VOut { float4 position : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; float3 add : COLOR1; };
VOut VSMain(VIn v) {
    VOut o;
    o.position = float4(v.position.x / 640.0 - 1.0, 1.0 - v.position.y / 360.0, 0, 1);
    o.uv = v.uv; o.color = v.color; o.add = v.add;
    return o;
}
sampler2D atlas : register(s0);
float4 PSMain(VOut v) : COLOR0 { return tex2D(atlas, v.uv) * v.color + float4(v.add, 0); }

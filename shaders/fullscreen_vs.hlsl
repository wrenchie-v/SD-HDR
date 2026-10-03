void main(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0)
{
    float2 p = float2((id << 1) & 2, id & 2);
    uv = p;
    pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

#pragma once

// Embedded source avoids a dependency on loose shader files or client packs.
// c0..c39 reproduce vertex processing used by the existing material pipeline;
// c40..c255 contain 72 affine bone transforms (three float4 columns each).
inline constexpr char GPUSkinningShaderSource[] = R"hlsl(
row_major float4x4 WVP : register(c0);
row_major float4x4 WorldView : register(c4);
float4 NormalColumns[3] : register(c8);
row_major float4x4 Texture0 : register(c11);
row_major float4x4 Texture1 : register(c15);
float4 InitialColor : register(c19);
float4 FogParams : register(c20); // start, end, density, D3DFOG_* mode
float4 Flags : register(c21); // normalize normals, range fog, lighting, unused
float4 TextureModes : register(c22);
float4 Reserved : register(c23);
float4 Lights[12] : register(c24); // position/type, diffuse, attenuation/range
// c36..c39 are deliberately free for compiler-generated literal constants.
float4 Bones[216] : register(c40);

struct Input
{
    float3 position : POSITION0;
    float4 weights : BLENDWEIGHT0;
    float4 indices : BLENDINDICES0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};
struct Output
{
    float4 position : POSITION0;
    float4 color : COLOR0;
    float4 specular : COLOR1;
    float fog : FOG;
    float4 uv : TEXCOORD0;
    float4 uv1 : TEXCOORD1;
};
float3 SafeNormalize(float3 value)
{
    return value * rsqrt(max(dot(value, value), 1e-20));
}
float4 TextureSource(float mode, Input input, float3 viewPosition, float3 viewNormal)
{
    // FFP extends a two-component input as (u,v,1,0) for texture
    // transformations; generated three-component coordinates use w=1.
    if (mode < 0.5) return float4(input.uv, 1, 0);
    if (mode < 1.5) return float4(input.uv1, 1, 0);
    if (mode < 2.5) return float4(viewNormal, 1);
    if (mode < 3.5) return float4(viewPosition, 1);
    return float4(reflect(SafeNormalize(viewPosition), SafeNormalize(viewNormal)), 1);
}
Output main(Input input)
{
    Output output;
    float3 position = 0, normal = 0;
    [unroll] for (int influence = 0; influence < 4; ++influence)
    {
        int index = (int)input.indices[influence] * 3;
        float4 p = float4(input.position, 1);
        float4 n = float4(input.normal, 0);
        position += input.weights[influence] * float3(
            dot(p, Bones[index]), dot(p, Bones[index + 1]), dot(p, Bones[index + 2]));
        normal += input.weights[influence] * float3(
            dot(n, Bones[index]), dot(n, Bones[index + 1]), dot(n, Bones[index + 2]));
    }
    output.position = mul(float4(position, 1), WVP);
    float3 viewPosition = mul(float4(position, 1), WorldView).xyz;
    float3 viewNormal = float3(dot(normal, NormalColumns[0].xyz),
        dot(normal, NormalColumns[1].xyz), dot(normal, NormalColumns[2].xyz));
    if (Flags.x > 0.5) viewNormal = SafeNormalize(viewNormal);
    float3 color = InitialColor.rgb;
    [unroll] for (int light = 0; light < 4; ++light)
    {
        float4 source = Lights[light * 3];
        if (source.w > 0.5 && Flags.z > 0.5)
        {
            float3 direction = source.xyz;
            float attenuation = 1;
            if (source.w > 1.5)
            {
                float3 delta = source.xyz - viewPosition;
                float distance = length(delta);
                direction = SafeNormalize(delta);
                float4 params = Lights[light * 3 + 2];
                attenuation = distance <= params.w ? rcp(max(
                    params.x + params.y * distance + params.z * distance * distance, 1e-20)) : 0;
            }
            color += attenuation * Lights[light * 3 + 1].rgb * max(dot(viewNormal, direction), 0);
        }
    }
    output.color = float4(saturate(color), InitialColor.a);
    output.specular = 0;
    float distance = Flags.y > 0.5 ? length(viewPosition) : abs(viewPosition.z);
    float fog = 1;
    if (FogParams.w > 2.5)
        fog = (FogParams.y - distance) / max(FogParams.y - FogParams.x, 1e-20);
    else if (FogParams.w > 1.5)
        fog = exp(-pow(FogParams.z * distance, 2));
    else if (FogParams.w > 0.5)
        fog = exp(-FogParams.z * distance);
    output.fog = saturate(fog);
    output.uv = mul(TextureSource(TextureModes.x, input, viewPosition, viewNormal), Texture0);
    output.uv1 = mul(TextureSource(TextureModes.y, input, viewPosition, viewNormal), Texture1);
    // With a programmable VS, the fixed-function pixel combiner projects by
    // the fourth output component. COUNT2/COUNT3 describe the original FFP
    // vertex coordinates, so move their divisor into w without dividing at
    // the vertices (which would change perspective interpolation).
    if (TextureModes.z > 0.5)
        output.uv.w = TextureModes.z < 2.5 ? output.uv.y :
            (TextureModes.z < 3.5 ? output.uv.z : output.uv.w);
    if (TextureModes.w > 0.5)
        output.uv1.w = TextureModes.w < 2.5 ? output.uv1.y :
            (TextureModes.w < 3.5 ? output.uv1.z : output.uv1.w);
    return output;
}
)hlsl";

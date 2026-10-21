
cbuffer CBSSAO : register(b0)
{
	matrix matProjection;
	matrix matInvProjection;
	float4 vSSAOParams;     // x=radius, y=bias, z=intensity
	float4 vTexelSize;      // x=1/ssaoW, y=1/ssaoH, z=1/fullW, w=1/fullH
	float4 vSampleKernel[16];
};

Texture2D<float> texDepth : register(t0);
Texture2D<float2> texNoise : register(t1);
SamplerState samPoint : register(s1);

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

float3 GetViewPos(float2 uv)
{
	float depth = texDepth.SampleLevel(samPoint, uv, 0);
	// NDC: x,y in [-1,1], z = depth [0,1]
	float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
	clipPos.y = -clipPos.y;
	float4 viewPos = mul(clipPos, matInvProjection);
	return viewPos.xyz / viewPos.w;
}

float3 ReconstructNormal(float2 uv)
{
    float3 center = GetViewPos(uv);
    float3 dx1 = GetViewPos(uv + float2(vTexelSize.z, 0)) - center;
    float3 dx2 = center - GetViewPos(uv - float2(vTexelSize.z, 0));
    float3 dy1 = GetViewPos(uv + float2(0, vTexelSize.w)) - center;
    float3 dy2 = center - GetViewPos(uv - float2(0, vTexelSize.w));
    float3 dx = abs(dx1.z) < abs(dx2.z) ? dx1 : dx2;
    float3 dy = abs(dy1.z) < abs(dy2.z) ? dy1 : dy2;
    float3 n = cross(dx, dy);
    float n2 = dot(n, n);
    if (!all(isfinite(n)) || n2 < 0.000001f) return normalize(-center);
    n *= rsqrt(n2);
    // Screen Y points down. Orient the hemisphere toward the visible camera side.
    if (dot(n, -center) < 0.0f) n = -n;
    return n;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float depth = texDepth.SampleLevel(samPoint, input.TexCoord, 0);
    if (!isfinite(depth) || depth >= 0.9999f || depth <= 0.0f) return float4(1, 0, 0, 1);
    float3 viewPos = GetViewPos(input.TexCoord);
    if (!all(isfinite(viewPos))) return float4(1, 0, 0, 1);
    float3 normal = ReconstructNormal(input.TexCoord);
    float2 noiseUV = input.TexCoord / (vTexelSize.xy * 4.0f);
    float2 noiseValue = texNoise.SampleLevel(samPoint, noiseUV, 0) * 2.0f - 1.0f;
    float3 randomVector = float3(noiseValue, 0);
    float3 tangent = randomVector - normal * dot(randomVector, normal);
    if (dot(tangent, tangent) < 0.000001f)
        tangent = cross(normal, abs(normal.z) < 0.9f ? float3(0, 0, 1) : float3(0, 1, 0));
    tangent = normalize(tangent);
    float3 bitangent = cross(normal, tangent);
    float3x3 tbn = float3x3(tangent, bitangent, normal);
    float radius = max(vSSAOParams.x, 0.001f);
    float bias = max(vSSAOParams.y, 0.0f);
    float occlusion = 0.0f;
    [unroll] for (int i = 0; i < 16; ++i)
    {
        float3 samplePosition = viewPos + mul(vSampleKernel[i].xyz, tbn) * radius;
        float4 projected = mul(float4(samplePosition, 1), matProjection);
        if (projected.w <= 0.0001f) continue;
        float2 sampleUV = projected.xy / projected.w;
        sampleUV = sampleUV * float2(0.5f, -0.5f) + 0.5f;
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f)) continue;
        float sampledDepth = texDepth.SampleLevel(samPoint, sampleUV, 0);
        if (!isfinite(sampledDepth) || sampledDepth >= 0.9999f || sampledDepth <= 0.0f) continue;
        float3 sampledPosition = GetViewPos(sampleUV);
        if (!all(isfinite(sampledPosition))) continue;
        float rangeWeight = smoothstep(0.0f, 1.0f, radius / max(abs(viewPos.z - sampledPosition.z), 0.001f));
        // RH view space: surfaces closer to the camera have greater (less negative) Z.
        occlusion += (sampledPosition.z >= samplePosition.z + bias ? 1.0f : 0.0f) * rangeWeight;
    }
    float ao = 1.0f - occlusion / 16.0f * saturate(vSSAOParams.z);
    return float4(saturate(ao), 0, 0, 1);
}

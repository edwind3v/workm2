
Texture2D    texScene    : register(t0);
Texture2D    texBloom    : register(t1);
Texture2D    texGodRays  : register(t2);
Texture2D    texSSAO     : register(t3);
SamplerState samLinear : register(s0);

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
	float3 scene = texScene.Sample(samLinear, input.TexCoord).rgb;
	float3 bloom = texBloom.Sample(samLinear, input.TexCoord).rgb;
	float3 godRays = texGodRays.Sample(samLinear, input.TexCoord).rgb;
	float4 aoSample = texSSAO.Sample(samLinear, input.TexCoord);
	// AO shades contacts, never suppresses the complete scene exposure.
	float ao = (aoSample.a > 0.5 && isfinite(aoSample.r)) ? clamp(aoSample.r, 0.5, 1.0) : 1.0;
	return float4(scene * ao + bloom + godRays, 1.0);
}

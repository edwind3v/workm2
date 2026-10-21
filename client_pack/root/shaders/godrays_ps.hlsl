
cbuffer CBGodRays : register(b0)
{
	float4 vLightScreenPos;  // xy = light position in screen UV space, z = intensity, w = decay
	float4 vRayParams;       // x = density, y = weight, z = exposure, w = numSamples
	float4 vRayColor;        // rgb = light color, a = unused
};

Texture2D    texScene : register(t0);  // Scene color/occlusion texture
SamplerState samLinear : register(s0);

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 lightPos = vLightScreenPos.xy;
    float intensity = max(vLightScreenPos.z, 0.0f);
    float decay = clamp(vLightScreenPos.w, 0.0f, 0.995f);
    int samples = clamp((int)vRayParams.w, 1, 128);
    float2 delta = (input.TexCoord - lightPos) * saturate(vRayParams.x) / float(samples);
    float2 uv = input.TexCoord;
    float illumination = 1.0f;
    float normalization = 0.0f;
    float3 color = 0.0f;
    [loop] for (int i = 0; i < samples; ++i)
    {
        uv -= delta;
        float inBounds = (all(uv >= 0.0f) && all(uv <= 1.0f)) ? 1.0f : 0.0f;
        float sourceMask = 1.0f - smoothstep(0.015f, 0.06f, length(uv - lightPos));
        float3 source = texScene.SampleLevel(samLinear, saturate(uv), 0).rgb;
        float brightness = smoothstep(0.65f, 0.95f, dot(source, float3(0.2126f, 0.7152f, 0.0722f)));
        color += source * (illumination * sourceMask * brightness * inBounds);
        normalization += illumination;
        illumination *= decay;
    }
    // Normalize the integral: increasing sample count must not brighten the screen.
    color /= max(normalization, 0.0001f);
    color *= saturate(vRayParams.y) * saturate(vRayParams.z) * intensity;
    color *= saturate(vRayColor.rgb);
    return float4(min(color, 0.2f), 1.0f);
}


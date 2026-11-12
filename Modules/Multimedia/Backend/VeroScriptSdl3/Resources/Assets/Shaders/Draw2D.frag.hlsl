// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

Texture2D Image : register(t0, space2);
Texture2D Mask : register(t1, space2);
SamplerState Sampling : register(s0, space2);
SamplerState MaskSampling : register(s1, space2);
cbuffer Parameters : register(b0, space3)
{
	float4 Tint; float4 Options; float4 Filter; float4 EffectColor; float4 Rows[4]; float4 Bias;
};
float4 PremultipliedSample(float2 P, bool TransparentBorder)
{
	float4 V = Image.SampleLevel(Sampling,TransparentBorder ? saturate(P) : P,0);
	if (Options.w == 0) V.rgb *= V.a;
	return !TransparentBorder || (all(P >= 0) && all(P <= 1)) ? V : float4(0,0,0,0);
}
float4 EffectSample(float2 UV)
{
	if (Options.z == 4) UV -= Rows[0].xy;
	float4 V = PremultipliedSample(UV,Options.z == 2 || Options.z == 4);
	if ((Options.z == 2 || Options.z == 4) && Filter.w > 0)
	{
		V = float4(0,0,0,0); float Total = 0;
		[unroll] for (int I=-6; I<=6; ++I)
		{
			float Weight = exp(-0.125*float(I*I));
			V += PremultipliedSample(UV+float(I)*Filter.xy,true)*Weight;
			Total += Weight;
		}
		V /= Total;
	}
	if (Options.z == 1)
	{
		float Alpha = Mask.Sample(MaskSampling,UV).a;
		V *= Filter.z > 0 ? 1-Alpha : Alpha;
	}
	else if (Options.z == 3)
	{
		float4 Straight = float4(V.a > 0 ? V.rgb/V.a : float3(0,0,0),V.a);
		V = saturate(float4(dot(Rows[0],Straight),dot(Rows[1],Straight),
							dot(Rows[2],Straight),dot(Rows[3],Straight))+Bias);
		V.rgb *= V.a;
	}
	else if (Options.z == 4)
		V = float4(EffectColor.rgb*EffectColor.a*V.a,EffectColor.a*V.a);
	if (Options.x == 0) V.rgb = V.a > 0 ? V.rgb/V.a : float3(0,0,0);
	return V;
}
float4 main(float4 Position : SV_Position, float2 UV : TEXCOORD0, float4 Color : TEXCOORD1) : SV_Target0
{
	if (Options.y > 0 && Options.x == 0)
		Color.rgb = Color.a > 0 ? Color.rgb / Color.a : float3(0, 0, 0);
	Color *= Tint;
	if (Options.x > 0)
		Color.rgb *= Options.y > 0 ? Tint.a : Color.a;
	return (Options.z > 0 ? EffectSample(UV) : Image.Sample(Sampling, UV)) * Color;
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <metal_stdlib>
using namespace metal;
struct Parameters { float4x4 Transform; float4 Viewport; };
struct FragmentParameters
{
	float4 Tint; float4 Options; float4 Filter; float4 EffectColor; float4 Rows[4]; float4 Bias;
};
struct Input { float2 Position [[attribute(0)]]; float2 UV [[attribute(1)]]; float4 Color [[attribute(2)]]; };
struct Output { float4 Position [[position]]; float2 UV; float4 Color; };
vertex Output vertex_main(Input V [[stage_in]], constant Parameters& P [[buffer(0)]])
{
	Output O;
	float4 Q = P.Transform * float4(V.Position,0,1);
	O.Position = float4(2*Q.x/P.Viewport.x-Q.w,Q.w-2*Q.y/P.Viewport.y,0,Q.w);
	O.UV = V.UV; O.Color = V.Color;
	return O;
}
float4 premultiplied_sample(float2 UV, texture2d<float> Image, sampler Sampling, constant FragmentParameters& P, bool TransparentBorder)
{
	float4 V = Image.sample(Sampling,TransparentBorder ? clamp(UV,0.0f,1.0f) : UV,level(0.0f));
	if (P.Options.w == 0) V.rgb *= V.a;
	return !TransparentBorder || (all(UV >= 0) && all(UV <= 1)) ? V : float4(0);
}
float4 effect_sample(float2 UV, texture2d<float> Image, sampler Sampling,
					 texture2d<float> Mask, sampler MaskSampling, constant FragmentParameters& P)
{
	if (P.Options.z == 4) UV -= P.Rows[0].xy;
	float4 V = premultiplied_sample(UV,Image,Sampling,P,P.Options.z == 2 || P.Options.z == 4);
	if ((P.Options.z == 2 || P.Options.z == 4) && P.Filter.w > 0)
	{
		V = float4(0); float Total = 0;
		for (int I=-6; I<=6; ++I)
		{
			float Weight = exp(-0.125*float(I*I));
			V += premultiplied_sample(UV+float(I)*P.Filter.xy,Image,Sampling,P,true)*Weight;
			Total += Weight;
		}
		V /= Total;
	}
	if (P.Options.z == 1)
	{
		float Alpha = Mask.sample(MaskSampling,UV).a;
		V *= P.Filter.z > 0 ? 1-Alpha : Alpha;
	}
	else if (P.Options.z == 3)
	{
		float4 Straight = float4(V.a > 0 ? V.rgb/V.a : float3(0),V.a);
		V = clamp(float4(dot(P.Rows[0],Straight),dot(P.Rows[1],Straight),
						 dot(P.Rows[2],Straight),dot(P.Rows[3],Straight))+P.Bias,0.0f,1.0f);
		V.rgb *= V.a;
	}
	else if (P.Options.z == 4)
		V = float4(P.EffectColor.rgb*P.EffectColor.a*V.a,P.EffectColor.a*V.a);
	if (P.Options.x == 0) V.rgb = V.a > 0 ? V.rgb/V.a : float3(0);
	return V;
}
fragment float4 fragment_main(Output V [[stage_in]], texture2d<float> Image [[texture(0)]],
	sampler Sampling [[sampler(0)]], texture2d<float> Mask [[texture(1)]], sampler MaskSampling [[sampler(1)]],
	constant FragmentParameters& P [[buffer(0)]])
{
	float4 Color = V.Color;
	if (P.Options.y > 0 && P.Options.x == 0)
		Color.rgb = Color.a > 0 ? Color.rgb / Color.a : float3(0);
	Color *= P.Tint;
	if (P.Options.x > 0)
		Color.rgb *= P.Options.y > 0 ? P.Tint.a : Color.a;
	return (P.Options.z > 0 ? effect_sample(V.UV,Image,Sampling,Mask,MaskSampling,P) : Image.sample(Sampling,V.UV)) * Color;
}

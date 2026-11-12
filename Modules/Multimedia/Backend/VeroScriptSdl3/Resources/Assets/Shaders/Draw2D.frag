// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#version 450
layout(location=0) in vec2 UV;
layout(location=1) in vec4 Color;
layout(set=2,binding=0) uniform sampler2D Image;
layout(set=2,binding=1) uniform sampler2D Mask;
layout(location=0) out vec4 Result;
layout(set=3,binding=0,std140) uniform Parameters
{
	vec4 Tint; vec4 Options; vec4 Filter; vec4 EffectColor; vec4 Rows[4]; vec4 Bias;
};
vec4 PremultipliedSample(vec2 P, bool TransparentBorder)
{
	vec4 V = textureLod(Image,TransparentBorder ? clamp(P,0,1) : P,0);
	if (Options.w == 0) V.rgb *= V.a;
	return !TransparentBorder || (all(greaterThanEqual(P,vec2(0))) && all(lessThanEqual(P,vec2(1)))) ? V : vec4(0);
}
vec4 EffectSample()
{
	vec2 Position = UV - (Options.z == 4 ? Rows[0].xy : vec2(0));
	vec4 V = PremultipliedSample(Position,Options.z == 2 || Options.z == 4);
	if ((Options.z == 2 || Options.z == 4) && Filter.w > 0)
	{
		V = vec4(0); float Total = 0;
		for (int I=-6; I<=6; ++I)
		{
			float Weight = exp(-0.125*float(I*I));
			V += PremultipliedSample(Position+float(I)*Filter.xy,true)*Weight;
			Total += Weight;
		}
		V /= Total;
	}
	if (Options.z == 1)
	{
		float Alpha = texture(Mask,UV).a;
		V *= Filter.z > 0 ? 1-Alpha : Alpha;
	}
	else if (Options.z == 3)
	{
		vec4 Straight = vec4(V.a > 0 ? V.rgb/V.a : vec3(0),V.a);
		V = clamp(vec4(dot(Rows[0],Straight),dot(Rows[1],Straight),
					   dot(Rows[2],Straight),dot(Rows[3],Straight))+Bias,0,1);
		V.rgb *= V.a;
	}
	else if (Options.z == 4)
		V = vec4(EffectColor.rgb*EffectColor.a*V.a,EffectColor.a*V.a);
	if (Options.x == 0) V.rgb = V.a > 0 ? V.rgb/V.a : vec3(0);
	return V;
}
void main()
{
	vec4 VertexColor = Color;
	if (Options.y > 0 && Options.x == 0)
		VertexColor.rgb = VertexColor.a > 0 ? VertexColor.rgb / VertexColor.a : vec3(0);
	VertexColor *= Tint;
	if (Options.x > 0)
		VertexColor.rgb *= Options.y > 0 ? Tint.a : VertexColor.a;
	Result = (Options.z > 0 ? EffectSample() : texture(Image,UV)) * VertexColor;
}

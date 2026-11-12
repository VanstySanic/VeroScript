// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

in vec2 UV;
in vec4 Color;
uniform sampler2D Image;
uniform sampler2D Mask;
out vec4 Result;
layout(std140) uniform FragmentParameters
{
	vec4 Tint; vec4 Options; vec4 Filter; vec4 EffectColor; vec4 Rows[4]; vec4 Bias; vec4 OutputOptions;
};
vec4 PremultipliedSample(vec2 P, bool TransparentBorder)
{
	vec4 V = textureLod(Image,TransparentBorder ? clamp(P,0.0,1.0) : P,0.0);
	if (Options.w == 0.0) V.rgb *= V.a;
	return !TransparentBorder || (all(greaterThanEqual(P,vec2(0.0))) && all(lessThanEqual(P,vec2(1.0)))) ? V : vec4(0.0);
}
vec4 EffectSample()
{
	vec2 Position = UV - (Options.z == 4.0 ? Rows[0].xy : vec2(0.0));
	vec4 V = PremultipliedSample(Position,Options.z == 2.0 || Options.z == 4.0);
	if ((Options.z == 2.0 || Options.z == 4.0) && Filter.w > 0.0)
	{
		V = vec4(0.0); float Total = 0.0;
		for (int I=-6; I<=6; ++I)
		{
			float Weight = exp(-0.125*float(I*I));
			V += PremultipliedSample(Position+float(I)*Filter.xy,true)*Weight;
			Total += Weight;
		}
		V /= Total;
	}
	if (Options.z == 1.0)
	{
		float Alpha = texture(Mask,UV).a;
		V *= Filter.z > 0.0 ? 1.0-Alpha : Alpha;
	}
	else if (Options.z == 3.0)
	{
		vec4 Straight = vec4(V.a > 0.0 ? V.rgb/V.a : vec3(0.0),V.a);
		V = clamp(vec4(dot(Rows[0],Straight),dot(Rows[1],Straight),
					   dot(Rows[2],Straight),dot(Rows[3],Straight))+Bias,0.0,1.0);
		V.rgb *= V.a;
	}
	else if (Options.z == 4.0)
		V = vec4(EffectColor.rgb*EffectColor.a*V.a,EffectColor.a*V.a);
	if (Options.x == 0.0) V.rgb = V.a > 0.0 ? V.rgb/V.a : vec3(0.0);
	return V;
}
void main()
{
	vec4 VertexColor = Color;
	if (Options.y > 0.0 && Options.x == 0.0)
		VertexColor.rgb = VertexColor.a > 0.0 ? VertexColor.rgb / VertexColor.a : vec3(0.0);
	VertexColor *= Tint;
	if (Options.x > 0.0)
		VertexColor.rgb *= Options.y > 0.0 ? Tint.a : VertexColor.a;
	Result = (Options.z > 0.0 ? EffectSample() : texture(Image,UV)) * VertexColor;
	if (OutputOptions.x > 0.0)
	{
		vec3 Linear = clamp(Result.rgb,0.0,1.0);
		Result.rgb = mix(1.055*pow(Linear,vec3(1.0/2.4))-0.055,12.92*Linear,lessThanEqual(Linear,vec3(0.0031308)));
	}
}

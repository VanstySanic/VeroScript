// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

struct VertexParameters { Transform: mat4x4f, Viewport: vec4f, }
struct FragmentParameters {
	Tint: vec4f, Options: vec4f, Filter: vec4f, EffectColor: vec4f,
	Rows: array<vec4f,4>, Bias: vec4f,
}
@group(0) @binding(0) var<uniform> Vertex: VertexParameters;
@group(0) @binding(1) var<uniform> Fragment: FragmentParameters;
@group(0) @binding(2) var Image: texture_2d<f32>;
@group(0) @binding(3) var Sampling: sampler;
@group(0) @binding(4) var Mask: texture_2d<f32>;
@group(0) @binding(5) var MaskSampling: sampler;
struct VertexInput { @location(0) Position: vec2f, @location(1) UV: vec2f, @location(2) Color: vec4f, }
struct VertexOutput { @builtin(position) Position: vec4f, @location(0) UV: vec2f, @location(1) Color: vec4f, }
@vertex fn vertex_main(V: VertexInput) -> VertexOutput {
	let P = Vertex.Transform*vec4f(V.Position,0,1);
	var O: VertexOutput;
	O.Position=vec4f(2*P.x/Vertex.Viewport.x-P.w,P.w-2*P.y/Vertex.Viewport.y,0,P.w);
	O.UV=V.UV; O.Color=V.Color;
	return O;
}
fn premultiplied_sample(UV: vec2f, TransparentBorder: bool) -> vec4f {
	var Position=UV;
	if (TransparentBorder) { Position=clamp(UV,vec2f(0),vec2f(1)); }
	var V=textureSampleLevel(Image,Sampling,Position,0);
	if (Fragment.Options.w==0) { V=vec4f(V.rgb*V.a,V.a); }
	if (TransparentBorder && (any(UV<vec2f(0)) || any(UV>vec2f(1)))) { return vec4f(0); }
	return V;
}
fn effect_sample(UV: vec2f) -> vec4f {
	let P=Fragment;
	var Position=UV;
	if (P.Options.z==4) { Position-=P.Rows[0].xy; }
	let Spatial=P.Options.z==2 || P.Options.z==4;
	var V=premultiplied_sample(Position,Spatial);
	if (Spatial && P.Filter.w>0) {
		V=vec4f(0); var Total=0.0;
		for (var I:i32=-6; I<=6; I++) {
			let Weight=exp(-0.125*f32(I*I));
			V+=premultiplied_sample(Position+f32(I)*P.Filter.xy,true)*Weight;
			Total+=Weight;
		}
		V/=Total;
	}
	if (P.Options.z==1) {
		var Alpha=textureSampleLevel(Mask,MaskSampling,UV,0).a;
		if (P.Filter.z>0) { Alpha=1-Alpha; }
		V*=Alpha;
	}
	else if (P.Options.z==3)
	{
		var Straight=vec4f(0,0,0,V.a);
		if (V.a>0) { Straight=vec4f(V.rgb/V.a,V.a); }
		V=clamp(vec4f(dot(P.Rows[0],Straight),dot(P.Rows[1],Straight),
			dot(P.Rows[2],Straight),dot(P.Rows[3],Straight))+P.Bias,vec4f(0),vec4f(1));
		V=vec4f(V.rgb*V.a,V.a);
	}
	else if (P.Options.z==4)
	{
		V=vec4f(P.EffectColor.rgb*P.EffectColor.a*V.a,P.EffectColor.a*V.a);
	}
	if (P.Options.x==0) {
		var Rgb=vec3f(0);
		if (V.a>0) { Rgb=V.rgb/V.a; }
		V=vec4f(Rgb,V.a);
	}
	return V;
}
@fragment fn fragment_main(V: VertexOutput) -> @location(0) vec4f {
	let P=Fragment;
	var Color=V.Color;
	if (P.Options.y>0 && P.Options.x==0) {
		var Rgb=vec3f(0);
		if (Color.a>0) { Rgb=Color.rgb/Color.a; }
		Color=vec4f(Rgb,Color.a);
	}
	Color*=P.Tint;
	if (P.Options.x>0) {
		var Alpha=Color.a;
		if (P.Options.y>0) { Alpha=P.Tint.a; }
		Color=vec4f(Color.rgb*Alpha,Color.a);
	}
	var Texel=textureSampleLevel(Image,Sampling,V.UV,0);
	if (P.Options.z>0) { Texel=effect_sample(V.UV); }
	return Texel*Color;
}

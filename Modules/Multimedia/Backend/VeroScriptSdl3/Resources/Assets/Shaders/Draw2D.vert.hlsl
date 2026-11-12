// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

cbuffer Parameters : register(b0, space1)
{
	column_major float4x4 Transform;
	float4 Viewport;
};
struct Input { float2 Position : TEXCOORD0; float2 UV : TEXCOORD1; float4 Color : TEXCOORD2; };
struct Output { float4 Position : SV_Position; float2 UV : TEXCOORD0; float4 Color : TEXCOORD1; };
Output main(Input V)
{
	Output O;
	float4 P = mul(Transform, float4(V.Position, 0, 1));
	O.Position = float4(2 * P.x / Viewport.x - P.w, P.w - 2 * P.y / Viewport.y, 0, P.w);
	O.UV = V.UV;
	O.Color = V.Color;
	return O;
}

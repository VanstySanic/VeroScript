// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#version 450
layout(location=0) in vec2 Position;
layout(location=1) in vec2 UV;
layout(location=2) in vec4 Color;
layout(set=1,binding=0,std140) uniform Parameters { mat4 Transform; vec4 Viewport; };
layout(location=0) out vec2 OutUV;
layout(location=1) out vec4 OutColor;
void main()
{
	vec4 P = Transform * vec4(Position,0,1);
	gl_Position = vec4(2*P.x/Viewport.x-P.w,P.w-2*P.y/Viewport.y,0,P.w);
	OutUV = UV;
	OutColor = Color;
}

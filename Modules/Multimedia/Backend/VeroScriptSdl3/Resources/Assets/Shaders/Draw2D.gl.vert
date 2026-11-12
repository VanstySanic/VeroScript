// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

layout(location=0) in vec2 Position;
layout(location=1) in vec2 TexCoord;
layout(location=2) in vec4 VertexColor;
out vec2 UV;
out vec4 Color;
layout(std140) uniform VertexParameters
{
	mat4 Transform;
	vec4 Viewport;
};
void main()
{
	vec4 P = Transform * vec4(Position,0.0,1.0);
	float Y = 2.0*P.y/Viewport.y-P.w;
	gl_Position = vec4(2.0*P.x/Viewport.x-P.w,Viewport.z > 0.0 ? -Y : Y,0.0,P.w);
	UV = TexCoord;
	Color = VertexColor;
}

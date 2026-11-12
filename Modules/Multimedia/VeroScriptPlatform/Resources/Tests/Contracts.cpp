// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPlatformApi.h"
#if VS_TEST_GRAPHICS
#include "VeroScriptGraphicsComposition2D.h"
#endif
#include <iostream>
#include <limits>

int main()
{
	using namespace VeroScript;
	std::string Error;
	Platform::FWindowOptions Window;
	if (!Platform::ValidateWindowOptions(Window, Error))
	{
		return 1;
	}
	Window.Width = std::numeric_limits<std::int64_t>::max();
	if (Platform::ValidateWindowOptions(Window, Error) || Error.empty())
	{
		return 2;
	}
	Window.Width = -1;
	if (Platform::ValidateWindowOptions(Window, Error))
	{
		return 3;
	}
	Window.Width = 64;
	Window.Title = std::string("a\0b", 3);
	if (Platform::ValidateWindowOptions(Window, Error))
	{
		return 4;
	}
	for (auto Type : {Platform::EEventType::MouseEntered, Platform::EEventType::MouseLeft})
	{
		Platform::FEvent Boundary;
		Boundary.Type = Type;
		Boundary.WindowId = 1;
		if (!Platform::ValidateEvent(Boundary, Error) ||
			Platform::GetInputDeviceKind(Boundary) != Platform::EInputDeviceKind::Mouse)
		{
			return 70;
		}
		Boundary.DeviceId = 1;
		if (Platform::ValidateEvent(Boundary, Error))
		{
			return 71;
		}
		Boundary.DeviceId = 0;
		Boundary.WindowId = 0;
		if (Platform::ValidateEvent(Boundary, Error))
		{
			return 72;
		}
	}
	Platform::FEvent Event;
	Event.Type = Platform::EEventType::PointerMove;
	Event.X = std::numeric_limits<double>::quiet_NaN();
	if (Platform::ValidateEvent(Event, Error))
	{
		return 5;
	}
	if (Platform::ValidateWait(-1, Error) || Platform::ValidateWait(51, Error) || !Platform::ValidateWait(50, Error))
	{
		return 6;
	}
	#if VS_TEST_GRAPHICS
	Graphics::FTextureOptions Texture;
	std::uint64_t Bytes = 42;
	Texture.Width = std::numeric_limits<std::int64_t>::max();
	if (Graphics::ValidateTextureOptions(Texture, 1024, Bytes, Error) || Bytes != 42)
	{
		return 7;
	}
	Texture.Width = 8;
	Texture.Height = 8;
	if (!Graphics::ValidateTextureOptions(Texture, 256, Bytes, Error) || Bytes != 256)
	{
		return 8;
	}
	if (Graphics::ValidateTextureOptions(Texture, 255, Bytes, Error))
	{
		return 9;
	}
	Graphics::FColor Color;
	Color.A = std::numeric_limits<float>::infinity();
	if (Graphics::ValidateColor(Color, Error))
	{
		return 10;
	}
	Graphics::FDrawList2D List;
	if (List.PopClip(Error) || List.PushClip({0, 0, -1, 1}, Error) ||
		Graphics::ValidateRect({std::numeric_limits<std::int64_t>::max(), 0, 1, 1}, Error))
	{
		return 11;
	}
	Graphics::FTransform2D Transform;
	Transform.Values[0] = std::numeric_limits<float>::quiet_NaN();
	if (List.SetTransform(Transform, Error))
	{
		return 12;
	}
	if (Graphics::ValidateSamplerOptions({static_cast<Graphics::EFilter>(99)}, Error))
	{
		return 13;
	}
	if (List.SetVertexColorMode(static_cast<Graphics::EVertexColorMode>(99), Error) ||
		!List.SetVertexColorMode(Graphics::EVertexColorMode::Premultiplied, Error))
	{
		return 15;
	}
	const std::vector<Graphics::FVertex2D> Vertices{{0, 0}, {1, 0}, {0, 1}};
	if (!Graphics::ValidateGeometry2D(Vertices, {0, 1, 2}, 1024, Error) ||
		Graphics::ValidateGeometry2D(Vertices, {0, 1, 3}, 1024, Error) ||
		Graphics::ValidateGeometry2D(Vertices, {0, 1, 2}, 1, Error))
	{
		return 14;
	}
	Graphics::FComposition2D Composition;
	for (unsigned I = 0; I < 32768; ++I)
	{
		if (!Composition.AddBlur(1, Error))
		{
			return 16;
		}
	}
	if (Composition.GetCommands().size() != 65536 || !Composition.IsBalanced() || Composition.AddBlur(1, Error) ||
		Composition.GetCommands().size() != 65536)
	{
		return 17;
	}
	Composition.Clear();
	if (!Composition.AddEffect({}, Error) || !Composition.AddBlur(1, Error) || Composition.GetCommands().size() != 3 ||
		Composition.AddBlur(-1, Error) || Composition.GetCommands().size() != 3)
	{
		return 18;
	}
	#endif
	std::cout << "Public contract boundaries passed\n";
	return 0;
}

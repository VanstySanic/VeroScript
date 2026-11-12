// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include "VeroScriptGraphicsComposition2D.h"
#include <algorithm>
#include <cmath>
#include <iostream>

int main(int Count, char** Args)
{
	using namespace VeroScript;
	using namespace Graphics;
	std::string Error;
	unsigned Failures = 0;
	const auto Check = [&](bool Value, const char* Message)
	{
		if (!Value)
		{
			++Failures;
			std::cerr << Message << ": " << Error << '\n';
		}
	};
	auto Service = Sdl3::CreateGraphics({}, Error);
	FDeviceOptions DeviceOptions;
	DeviceOptions.Driver = Count > 1 ? Args[1] : "";
	auto Device = Service ? Service->CreateDevice(DeviceOptions, Error) : nullptr;
	if (!Device)
	{
		std::cout << "GPU unavailable: " << Error;
		return Count > 1 ? 1 : 77;
	}
	auto Renderer = Device->CreateRenderer2D(Error);
	auto Compositor = CreateCompositor2D(Device, {}, Error);
	auto Target = Device->CreateTexture({16, 16}, Error);
	if (!Renderer || !Compositor || !Target)
	{
		std::cerr << Error;
		return 1;
	}
	auto Quad = Renderer->CreateGeometry({{0, 0, 0, 0}, {16, 0, 1, 0}, {16, 16, 1, 1}, {0, 16, 0, 1}},
										 {0, 1, 2, 0, 2, 3}, Error);
	auto Center = Renderer->CreateGeometry({{6, 6}, {10, 6}, {10, 10}, {6, 10}}, {0, 1, 2, 0, 2, 3}, Error);
	if (!Quad || !Center)
	{
		return 1;
	}
	FDrawList2D Red, Blue, Left, Top, Dot;
	Check(Red.SetTint({1, 0, 0, 1}, Error) && Red.AddGeometry(Quad, Error), "red geometry");
	Check(Blue.SetTint({0, 0, 1, 1}, Error) && Blue.AddGeometry(Quad, Error), "blue geometry");
	Check(Left.PushClip({0, 0, 8, 16}, Error) && Left.AddGeometry(Quad, Error), "left mask");
	Check(Top.PushClip({0, 0, 16, 8}, Error) && Top.AddGeometry(Quad, Error), "top mask");
	Check(Dot.SetTint({1, 0, 0, 1}, Error) && Dot.AddGeometry(Center, Error), "small source");
	std::vector<std::uint8_t> Pixels;
	const auto Read = [&]
	{
		return Device->ReadTexture(Target, Pixels, Error);
	};
	const auto Pixel = [&](int X, int Y, int R, int G, int B, int A)
	{
		const auto I = static_cast<std::size_t>((Y * 16 + X) * 4);
		return Pixels.size() == 1024 && std::abs(Pixels[I] - R) <= 2 && std::abs(Pixels[I + 1] - G) <= 2 &&
			   std::abs(Pixels[I + 2] - B) <= 2 && std::abs(Pixels[I + 3] - A) <= 2;
	};
	FComposition2D Frame;
	FRenderOptions2D TranslucentClear;
	TranslucentClear.ClearColor = {1, 0.5f, 0, 0.5f};
	Check(Compositor->RenderTexture(Target, Frame, TranslucentClear, Error) && Read() && Pixel(4, 4, 128, 64, 0, 128),
		  "composition clear premultiplies straight linear RGBA");
	TranslucentClear.bClear = false;
	Check(Compositor->RenderTexture(Target, Frame, TranslucentClear, Error) && Read() && Pixel(4, 4, 128, 64, 0, 128),
		  "composition load preserves premultiplied pixels");
	bool bPresented = true;
	Check(!Compositor->RenderSurface({}, 16, 16, Frame, {}, bPresented, Error) && !bPresented,
		  "composition failure clears presentation output");
	bPresented = true;
	Check(!Renderer->RenderSurface({}, Red, {}, bPresented, Error) && !bPresented,
		  "renderer failure clears presentation output");
	bPresented = true;
	Check(!Device->Present({}, {}, bPresented, Error) && !bPresented, "device failure clears presentation output");
	auto Srgb = Device->CreateTexture({4, 1, ETextureFormat::Rgba8Srgb}, Error);
	const std::vector<std::uint8_t> DarkPixels{1, 3, 7, 255, 11, 15, 21, 255, 31, 63, 95, 255, 127, 191, 255, 255};
	std::vector<std::uint8_t> Encoded;
	const auto PreservesDarkPixels = [&]
	{
		if (Encoded.size() != DarkPixels.size()) return false;
		for (std::size_t I = 0; I < Encoded.size(); ++I)
		{
			// sRGB conversion can round by one encoded unit across GPU drivers.
			// A linear 8-bit intermediate still fails this dark-color check.
			if (std::abs(int(Encoded[I]) - int(DarkPixels[I])) > (I % 4 == 3 ? 0 : 1)) return false;
		}
		return true;
	};
	FRenderOptions2D Load;
	Load.bClear = false;
	Check(Srgb && Device->UpdateTexture(Srgb, 0, 0, 4, 1, DarkPixels, Error) &&
			  Compositor->RenderTexture(Srgb, Frame, Load, Error) && Device->ReadTexture(Srgb, Encoded, Error) &&
			  PreservesDarkPixels(),
		  "default composition load preserves sRGB dark colors");
	FEffect2D Identity;
	Identity.Type = EEffect2D::ColorMatrix;
	Check(Frame.AddEffect(Identity, Error) && Srgb && Device->UpdateTexture(Srgb, 0, 0, 4, 1, DarkPixels, Error) &&
			  Compositor->RenderTexture(Srgb, Frame, Load, Error) && Device->ReadTexture(Srgb, Encoded, Error) &&
			  PreservesDarkPixels(),
		  "identity composition effect preserves sRGB dark colors");
	Frame.Clear();
	if (Srgb)
	{
		Check(Srgb->Close(Error), "close sRGB precision target");
	}
	auto Pattern = Device->CreateTexture({2, 1}, Error);
	auto WhiteMask = Device->CreateTexture({1, 1}, Error);
	auto Repeated = Renderer->CreateGeometry({{0, 0, -1, 0}, {16, 0, 2, 0}, {16, 16, 2, 1}, {0, 16, -1, 1}},
											 {0, 1, 2, 0, 2, 3}, Error);
	Check(Pattern && WhiteMask && Repeated &&
			  Device->UpdateTexture(Pattern, 0, 0, 2, 1, {255, 0, 0, 255, 0, 0, 255, 255}, Error) &&
			  Device->ClearTexture(WhiteMask, {1, 1, 1, 1}, Error),
		  "effect sampler resources");
	for (const auto Address : {EAddressMode::Clamp, EAddressMode::Repeat, EAddressMode::Mirror})
	{
		auto Sampler = Device->CreateSampler({EFilter::Nearest, Address, EAddressMode::Clamp}, Error);
		FDrawList2D Draw;
		Draw.SetTexture(Pattern, Sampler);
		Check(Sampler && Draw.AddGeometry(Repeated, Error) && Renderer->RenderTexture(Target, Draw, {}, Error) &&
				  Read(),
			  "render effect sampler reference");
		const auto Reference = Pixels;
		for (const auto Type : {EEffect2D::ColorMatrix, EEffect2D::AlphaMask})
		{
			Identity.Type = Type;
			Identity.Mask = WhiteMask;
			Draw.Clear();
			Draw.SetTexture(Pattern, Sampler);
			Check(Draw.SetEffect(Identity, Error) && Draw.AddGeometry(Repeated, Error) &&
					  Renderer->RenderTexture(Target, Draw, {}, Error) && Read() && Pixels == Reference,
				  "nonspatial effects preserve source sampler addressing");
		}
		if (Sampler)
		{
			Check(Sampler->Close(Error), "close effect sampler");
		}
	}
	if (Repeated)
	{
		Check(Repeated->Close(Error), "close repeated geometry");
	}
	if (Pattern)
	{
		Check(Pattern->Close(Error), "close effect source");
	}
	if (WhiteMask)
	{
		Check(WhiteMask->Close(Error), "close effect mask");
	}
	Check(Frame.BeginLayer(0.5f, Error) && Frame.AddDraw(Red, Error) && Frame.AddDraw(Blue, Error) &&
			  Frame.EndLayer(Error) && Compositor->RenderTexture(Target, Frame, {}, Error) && Read() &&
			  Pixel(4, 4, 0, 0, 128, 128),
		  "group opacity applies once after overlapping children");
	const auto PoolBytes = Compositor->GetPooledBytes();
	Check(Compositor->RenderTexture(Target, Frame, {}, Error) && Compositor->GetPooledBytes() == PoolBytes,
		  "same-size frame reuses temporary textures");
	Frame.Clear();
	Check(Frame.PushMask(Left, false, Error) && Frame.PushMask(Top, false, Error) && Frame.AddDraw(Red, Error) &&
			  Frame.PopMask(Error) && Frame.PopMask(Error) && Compositor->RenderTexture(Target, Frame, {}, Error) &&
			  Read() && Pixel(4, 4, 255, 0, 0, 255) && Pixel(12, 4, 0, 0, 0, 0) && Pixel(4, 12, 0, 0, 0, 0),
		  "nested masks intersect");
	Frame.Clear();
	Check(Frame.PushMask(Left, true, Error) && Frame.AddDraw(Red, Error) && Frame.PopMask(Error) &&
			  Compositor->RenderTexture(Target, Frame, {}, Error) && Read() && Pixel(4, 4, 0, 0, 0, 0) &&
			  Pixel(12, 4, 255, 0, 0, 255),
		  "inverse mask");
	Frame.Clear();
	FEffect2D Matrix;
	Matrix.Type = EEffect2D::ColorMatrix;
	Matrix.Matrix = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
	Check(Frame.AddDraw(Red, Error) && Frame.AddEffect(Matrix, Error) &&
			  Compositor->RenderTexture(Target, Frame, {}, Error) && Read() && Pixel(4, 4, 0, 255, 0, 255),
		  "typed color matrix");
	Frame.Clear();
	Check(Frame.AddDraw(Dot, Error) && Frame.AddBlur(3, Error) && Compositor->RenderTexture(Target, Frame, {}, Error) &&
			  Read(),
		  "separable blur execution");
	Check(Pixels[(7 * 16 + 5) * 4 + 3] > 0 && Pixels[(7 * 16 + 5) * 4 + 3] < Pixels[(8 * 16 + 8) * 4 + 3] &&
			  std::abs(Pixels[(7 * 16 + 5) * 4] - Pixels[(7 * 16 + 5) * 4 + 3]) <= 2,
		  "blur preserves premultiplied alpha");
	Frame.Clear();
	FEffect2D Shadow;
	Shadow.Type = EEffect2D::Shadow;
	Shadow.Radius = 1;
	Shadow.OffsetX = 4;
	Shadow.Color = {0, 1, 0, 1};
	Check(Frame.AddDraw(Dot, Error) && Frame.AddEffect(Shadow, Error) &&
			  Compositor->RenderTexture(Target, Frame, {}, Error) && Read() && Pixel(7, 7, 255, 0, 0, 255) &&
			  Pixels[(7 * 16 + 12) * 4 + 1] > 20,
		  "shadow offset and original content");
	Frame.Clear();
	Check(Frame.AddDraw(Dot, Error) && Frame.AddBackdropBlur(Left, 3, Error) &&
			  Compositor->RenderTexture(Target, Frame, {}, Error) && Read() && Pixels[(7 * 16 + 5) * 4 + 3] > 0 &&
			  Pixel(9, 7, 255, 0, 0, 255) && Pixel(11, 7, 0, 0, 0, 0),
		  "masked backdrop blur preserves outside pixels");
	const auto Saved = Pixels;
	Check(Frame.BeginLayer(1, Error) && !Compositor->RenderTexture(Target, Frame, {}, Error) && Read() &&
			  Pixels == Saved && !Frame.PopMask(Error),
		  "unbalanced or mismatched scopes preserve target");
	Frame.Clear();
	Check(Frame.AddDraw(Red, Error) && Frame.BeginLayer(1, Error) && Frame.AddDraw(Blue, Error) &&
			  Frame.EndLayer(Error),
		  "budget test composition");
	FCompositorOptions2D Limits;
	Limits.MaxPooledBytes = 1024;
	auto Limited = CreateCompositor2D(Device, Limits, Error);
	Check(Limited && !Limited->RenderTexture(Target, Frame, {}, Error) && Read() && Pixels == Saved,
		  "temporary budget failure preserves final target");
	if (Limited)
	{
		Check(Limited->Close(Error), "close limited compositor");
	}
	Check(Compositor->Trim(Error) && Compositor->GetPooledBytes() == 0, "trim releases cached textures");
	Frame.Clear();
	Check(Frame.AddDraw(Red, Error) && Compositor->RenderTexture(Target, Frame, {}, Error),
		  "populate resources before recovery");
	FDeviceOptions BadOptions;
	BadOptions.Driver = std::string(1, '\0');
	Check(!Service->RecoverDevice(Device, BadOptions, Error) && Device->IsOpen(),
		  "failed replacement creation preserves the previous device");
	const auto Generation = Device->GetGeneration();
	auto Replacement = Service->RecoverDevice(Device, DeviceOptions, Error);
	Check(Replacement && Replacement->GetGeneration() != Generation && !Device->IsOpen() && !Target->IsOpen() &&
			  !Quad->IsOpen() && !Renderer->IsOpen() && !Compositor->IsOpen(),
		  "device replacement invalidates the old resource generation");
	Check(!Renderer->RenderTexture(Target, Red, {}, Error) && Device->Invalidate(Error),
		  "invalidated device rejects submissions and repeated invalidation is safe");
	Check(Compositor->Close(Error), "compositor cleanup remains legal after device invalidation");
	Check(Target->Close(Error) && Quad->Close(Error) && Center->Close(Error) && Renderer->Close(Error) &&
			  Device->Close(Error),
		  "close composition resources");
	if (Replacement)
	{
		auto Fresh = Replacement->CreateTexture({2, 2}, Error);
		Check(Fresh && Replacement->ClearTexture(Fresh, {0, 1, 0, 1}, Error) &&
				  !Replacement->ClearTexture(Target, {}, Error),
			  "replacement uses fresh resources only");
		if (Fresh)
		{
			Check(Fresh->Close(Error), "close replacement texture");
		}
		Check(Replacement->Close(Error), "close replacement device");
	}
	std::cout << "Composition failures: " << Failures << '\n';
	return Failures ? 1 : 0;
}

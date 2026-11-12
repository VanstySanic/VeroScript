// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include "VeroScriptGraphics2D.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

int main(int Count, char** Args)
{
	using namespace VeroScript;
	using namespace Graphics;
	std::string Error;
	auto Service = Sdl3::CreateGraphics({}, Error);
	if (!Service)
	{
		std::cerr << Error;
		return 1;
	}
	FDeviceOptions DeviceOptions;
	DeviceOptions.Driver = Count > 1 ? Args[1] : "";
	auto Device = Service->CreateDevice(DeviceOptions, Error);
	if (!Device)
	{
		std::cout << "GPU unavailable: " << Error;
		return Count > 1 ? 1 : 77;
	}
	int Failures = 0;
	const auto Check = [&](bool Value, const char* Name)
	{
		if (!Value)
		{
			++Failures;
			std::cerr << Name << ": " << Error << '\n';
		}
	};
	auto Renderer = Device->CreateRenderer2D(Error);
	Check(static_cast<bool>(Renderer), "renderer creation");
	if (!Renderer)
	{
		return 1;
	}
	auto Target = Device->CreateTexture({16, 16}, Error);
	Check(static_cast<bool>(Target), "render target");
	if (!Target)
	{
		return 1;
	}
	const std::vector<std::uint32_t> Indices{0, 1, 2, 0, 2, 3};
	const auto Quad = [](FColor Color = {1, 1, 1, 1}, float UV = 1)
	{
		return std::vector<FVertex2D>{
			{0, 0, 0, 0, Color}, {8, 0, UV, 0, Color}, {8, 8, UV, UV, Color}, {0, 8, 0, UV, Color}};
	};
	auto Red = Renderer->CreateGeometry(Quad({1, 0, 0, 1}), Indices, Error);
	auto White = Renderer->CreateGeometry(Quad(), Indices, Error);
	if (!Red || !White)
	{
		std::cerr << Error;
		return 1;
	}
	Check(!Renderer->CreateGeometry(Quad(), {0, 1, 99}, Error), "invalid vertex index");
	Check(!Renderer->CreateGeometry(Quad(), {0, 1}, Error), "incomplete triangle");
	std::vector<std::uint8_t> Pixels;
	const auto Pixel = [&](int X, int Y, int R, int G, int B, int A = 255)
	{
		const auto Offset = static_cast<std::size_t>((Y * 16 + X) * 4);
		return Pixels.size() == 16 * 16 * 4 && std::abs(Pixels[Offset] - R) <= 2 &&
			   std::abs(Pixels[Offset + 1] - G) <= 2 && std::abs(Pixels[Offset + 2] - B) <= 2 &&
			   std::abs(Pixels[Offset + 3] - A) <= 2;
	};
	FDrawList2D List;
	FTransform2D Transform;
	Transform.Values[12] = 2;
	Transform.Values[13] = 3;
	Check(List.SetTransform(Transform, Error) && List.PushClip({1, 1, 10, 10}, Error) &&
			  List.PushClip({5, 4, 4, 4}, Error) && List.AddGeometry(Red, Error),
		  "build clipped geometry");
	Check(List.PopClip(Error) && List.PopClip(Error) && !List.PopClip(Error), "clip stack restoration and underflow");
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error),
		  "render geometry and readback");
	Check(Pixel(5, 4, 255, 0, 0) && Pixel(8, 7, 255, 0, 0) && Pixel(4, 4, 0, 0, 0, 0) && Pixel(9, 7, 0, 0, 0, 0),
		  "transformed geometry, nested clip and top-left coordinates");
	if (auto* Submission = dynamic_cast<IRenderSubmission2D*>(Renderer.get()))
	{
		const auto Before = Pixels;
		FDrawList2D Empty;
		FRenderOptions2D Blue;
		Blue.ClearColor = {0, 0, 1, 1};
		Check(Submission->BeginSubmission(Error) && !Submission->BeginSubmission(Error) &&
			Renderer->RenderTexture(Target, Empty, Blue, Error) && Submission->CancelSubmission(Error) &&
			Device->ReadTexture(Target, Pixels, Error) && Pixels == Before,
			"cancelled submission modified the target or nested begin lost the scope");
		auto CopyTarget = Device->CreateTexture({16, 16}, Error);
		FDrawList2D Copy;
		Copy.SetTexture(Target);
		Check(CopyTarget && Copy.AddGeometry(White, Error) && Submission->BeginSubmission(Error) &&
			Renderer->RenderTexture(Target, Empty, Blue, Error) &&
			Renderer->RenderTexture(CopyTarget, Copy, {}, Error) && Submission->EndSubmission(Error) &&
			Device->ReadTexture(CopyTarget, Pixels, Error) && Pixel(3, 3, 0, 0, 255),
			"batched producer/consumer passes lost ordering");
		Check(!CopyTarget || CopyTarget->Close(Error), "close batch copy target");
		Check(Submission->BeginSubmission(Error), "begin geometry upload batch");
		std::vector<std::shared_ptr<IGeometry2D>> Batch;
		for (int Index = 0; Index < 2048; ++Index)
		{
			auto Geometry = Renderer->CreateGeometry(Quad(Index == 0 ? FColor{1, 0, 0, 1} : FColor{0, 1, 0, 1}), Indices, Error);
			Check(bool(Geometry), "batched geometry upload");
			Batch.push_back(std::move(Geometry));
		}
		FDrawList2D Batched;
		Check(Batched.AddGeometry(Batch.front(), Error), "first batched geometry");
		FTransform2D Right;
		Right.Values[12] = 8;
		Check(Batched.SetTransform(Right, Error) && Batched.AddGeometry(Batch.back(), Error) &&
			Renderer->RenderTexture(Target, Batched, {}, Error) && Submission->EndSubmission(Error) &&
			Device->ReadTexture(Target, Pixels, Error) && Pixel(1, 1, 255, 0, 0) && Pixel(9, 1, 0, 255, 0),
			"staging range reuse or wrap overwrote earlier geometry uploads");
		Batched.Clear();
		for (const auto &Geometry : Batch)
		{
			Check(Geometry->Close(Error), "close batched geometry");
		}
		Check(Submission->BeginSubmission(Error), "begin persistent geometry batch");
		auto Persistent = Renderer->CreateGeometry(Quad({0, 0, 1, 1}), Indices, Error);
		FDrawList2D PersistentDraw;
		Check(Persistent && Submission->CancelSubmission(Error) && PersistentDraw.AddGeometry(Persistent, Error) &&
			Renderer->RenderTexture(Target, PersistentDraw, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			Pixel(1, 1, 0, 0, 255) && Persistent->Close(Error),
			"cancelling drawing discarded a successfully created geometry");
	}
	FTransform2D Invalid;
	Invalid.Values[0] = std::numeric_limits<float>::quiet_NaN();
	Check(!List.SetTransform(Invalid, Error) && !List.PushClip({0, 0, -1, 1}, Error), "invalid state rejected");
	auto Texture = Device->CreateTexture({2, 2}, Error);
	std::vector<std::uint8_t> Texels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
	Check(Texture && Device->UpdateTexture(Texture, 0, 0, 2, 2, Texels, Error), "texture upload");
	auto Nearest = Device->CreateSampler({EFilter::Nearest}, Error);
	List.Clear();
	List.SetTexture(Texture, Nearest);
	Check(List.AddGeometry(White, Error), "textured command");
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error),
		  "nearest sampling");
	Check(Pixel(1, 1, 255, 0, 0) && Pixel(6, 1, 0, 255, 0) && Pixel(1, 6, 0, 0, 255) && Pixel(6, 6, 255, 255, 255),
		  "texture orientation and UV mapping");
	Check(!Device->UpdateTexture(Texture, 0, 0, 2, 2, {1}, Error) &&
			  !Device->UpdateTexture(Texture, -1, 0, 1, 1, {1, 2, 3, 4}, Error),
		  "invalid upload rejected");
	Check(Device->UpdateTexture(Texture, 1, 0, 1, 1, {255, 255, 0, 255}, Error), "partial upload");
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixel(6, 1, 255, 255, 0),
		  "partial upload pixels");
	auto Repeated = Renderer->CreateGeometry(Quad({1, 1, 1, 1}, 2), Indices, Error);
	auto Repeat = Device->CreateSampler({EFilter::Nearest, EAddressMode::Repeat, EAddressMode::Repeat}, Error);
	List.Clear();
	List.SetTexture(Texture, Repeat);
	Check(List.AddGeometry(Repeated, Error), "repeat command");
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixel(1, 1, 255, 0, 0) && Pixel(5, 1, 255, 0, 0),
		  "repeat address sampling");
	List.Clear();
	List.SetTexture(Texture);
	List.AddGeometry(White, Error);
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixels[(1 * 16 + 3) * 4 + 1] > 30 && Pixels[(1 * 16 + 3) * 4 + 1] < 220,
		  "linear sampling interpolates");
	auto Translucent = Renderer->CreateGeometry(Quad({1, 0, 0, 0.5f}), Indices, Error);
	FRenderOptions2D Options;
	Options.ClearColor = {0, 0, 1, 1};
	for (auto Blend : {EBlendMode::StraightAlpha, EBlendMode::PremultipliedAlpha})
	{
		List.Clear();
		List.SetBlendMode(Blend, Error);
		List.SetTint({1, 1, 1, 0.5f}, Error);
		List.AddGeometry(Translucent, Error);
		Check(Renderer->RenderTexture(Target, List, Options, Error) && Device->ReadTexture(Target, Pixels, Error) &&
				  Pixel(1, 1, 64, 0, 191),
			  "straight and premultiplied alpha with tint");
	}
	List.Clear();
	List.SetTexture(Target);
	List.AddGeometry(White, Error);
	const auto Saved = Pixels;
	Check(!Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixels == Saved,
		  "feedback rejected before target clear");
	List.Clear();
	List.AddGeometry(Red, Error);
	Options = {};
	Options.bClear = false;
	Options.bViewport = true;
	Options.Viewport = {8, 8, 8, 8};
	Check(Renderer->RenderTexture(Target, List, Options, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixel(9, 9, 255, 0, 0) && Pixel(1, 1, 64, 0, 191),
		  "viewport and load preservation");
	Options = {};
	Options.ClearColor = {0, 0, 1, 1};
	auto GradientVertices = Quad();
	GradientVertices[0].Color = GradientVertices[3].Color = {1, 0, 0, 0};
	GradientVertices[1].Color = GradientVertices[2].Color = {0, 1, 0, 1};
	auto Gradient = Renderer->CreateGeometry(GradientVertices, Indices, Error);
	List.Clear();
	List.AddGeometry(Gradient, Error);
	Check(Renderer->RenderTexture(Target, List, Options, Error) && Device->ReadTexture(Target, Pixels, Error),
		  "gradient straight alpha");
	const auto StraightGradient = Pixels;
	List.Clear();
	List.SetBlendMode(EBlendMode::PremultipliedAlpha, Error);
	List.AddGeometry(Gradient, Error);
	Check(Renderer->RenderTexture(Target, List, Options, Error) && Device->ReadTexture(Target, Pixels, Error),
		  "gradient premultiplied alpha");
	bool SameGradient = Pixels.size() == StraightGradient.size();
	for (std::size_t I = 0; SameGradient && I < Pixels.size(); ++I)
	{
		SameGradient = std::abs(Pixels[I] - StraightGradient[I]) <= 1;
	}
	Check(SameGradient, "premultiplication occurs after interpolation");
	auto PremultipliedGradient = Renderer->CreateGeometry(
		{{0, 0, 0, 0, {1, 0, 0, 1}}, {32, 0, 0, 0, {0, 0, 0, 0}}, {0, 32, 0, 0, {0, 0, 0, 0}}}, {0, 1, 2}, Error);
	if (!PremultipliedGradient)
	{
		std::cerr << Error;
		return 1;
	}
	List.Clear();
	Check(List.SetVertexColorMode(EVertexColorMode::Premultiplied, Error) &&
			  List.SetBlendMode(EBlendMode::PremultipliedAlpha, Error) &&
			  List.AddGeometry(PremultipliedGradient, Error),
		  "premultiplied vertex input command");
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixel(8, 8, 120, 0, 0, 120),
		  "premultiplied vertex interpolation avoids a second alpha multiply");
	List.Clear();
	Check(List.SetVertexColorMode(EVertexColorMode::Premultiplied, Error) &&
			  List.SetBlendMode(EBlendMode::PremultipliedAlpha, Error) && List.SetTint({0.5f, 1, 1, 0.5f}, Error) &&
			  List.AddGeometry(PremultipliedGradient, Error),
		  "premultiplied vertex tint command");
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixel(8, 8, 30, 0, 0, 60),
		  "straight tint alpha scales premultiplied vertex RGB exactly once");
	List.Clear();
	Check(List.SetVertexColorMode(EVertexColorMode::Premultiplied, Error) &&
			  List.AddGeometry(PremultipliedGradient, Error),
		  "premultiplied input with straight output command");
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixel(8, 8, 120, 0, 0, 120),
		  "straight blending converts premultiplied input after interpolation");
	List.Clear();
	Check(List.SetVertexColorMode(EVertexColorMode::Premultiplied, Error) &&
			  List.SetBlendMode(EBlendMode::Opaque, Error) && List.SetTint({0.5f, 1, 1, 0}, Error) &&
			  List.AddGeometry(PremultipliedGradient, Error),
		  "premultiplied input with opaque output command");
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixel(8, 8, 128, 0, 0, 0),
		  "opaque mode preserves straight tint color when tint alpha is zero");
	List.Clear();
	Check(List.AddGeometry(PremultipliedGradient, Error) &&
			  List.GetCommands().back().VertexColorMode == EVertexColorMode::Straight,
		  "clear restores straight vertex mode");
	auto Layer = Device->CreateTexture({8, 8}, Error);
	List.Clear();
	List.AddGeometry(Translucent, Error);
	Check(Renderer->RenderTexture(Layer, List, {}, Error), "transparent layer drawing");
	List.Clear();
	List.SetTexture(Layer, Nearest);
	List.SetBlendMode(EBlendMode::PremultipliedAlpha, Error);
	List.AddGeometry(White, Error);
	Check(Renderer->RenderTexture(Target, List, Options, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixel(1, 1, 128, 0, 127),
		  "premultiplied layer composition");
	auto Srgb = Device->CreateTexture({8, 8, ETextureFormat::Rgba8Srgb}, Error);
	auto LinearColor = Renderer->CreateGeometry(Quad({0.25f, 0.5f, 0.75f, 0.25f}), Indices, Error);
	List.Clear();
	List.SetBlendMode(EBlendMode::Opaque, Error);
	List.AddGeometry(LinearColor, Error);
	std::vector<std::uint8_t> Encoded;
	Check(Renderer->RenderTexture(Srgb, List, {}, Error) && Device->ReadTexture(Srgb, Encoded, Error) &&
			  Encoded.size() == 256 && std::abs(Encoded[0] - 137) <= 2 && std::abs(Encoded[1] - 188) <= 2 &&
			  std::abs(Encoded[2] - 225) <= 2 && std::abs(Encoded[3] - 64) <= 2,
		  "linear vertex color to sRGB attachment and opaque mode");
	auto SrgbImage = Device->CreateTexture({1, 1, ETextureFormat::Rgba8Srgb}, Error);
	Check(SrgbImage && Device->UpdateTexture(SrgbImage, 0, 0, 1, 1, {128, 128, 128, 255}, Error), "sRGB source upload");
	List.Clear();
	List.SetTexture(SrgbImage, Nearest);
	List.AddGeometry(White, Error);
	Check(Renderer->RenderTexture(Target, List, {}, Error) && Device->ReadTexture(Target, Pixels, Error) &&
			  Pixel(1, 1, 55, 55, 55),
		  "sRGB sampling decodes before blending");
	// Recycle geometry while earlier uploads/draws are still queued. Read only
	// after the batch, so cycling must preserve every submitted buffer version.
	std::vector<std::shared_ptr<ITexture>> QueuedTargets;
	for (int Index = 0; Index < 24; ++Index)
	{
		auto Image = Device->CreateTexture({16, 16}, Error);
		const bool Triangle = Index % 2 != 0;
		auto Vertices = Quad(Index % 3 == 0 ? FColor{1, 0, 0, 1} :
			Index % 3 == 1 ? FColor{0, 1, 0, 1} : FColor{0, 0, 1, 1});
		if (Index % 4 == 0)
		{
			Vertices.push_back(Vertices.back());
		}
		auto Geometry = Renderer->CreateGeometry(Vertices, Triangle ? std::vector<std::uint32_t>{0, 1, 2} : Indices, Error);
		FDrawList2D Queued;
		Check(Image && Geometry && Geometry->GetVertexCount() == static_cast<std::int64_t>(Vertices.size()) &&
			Geometry->GetIndexCount() == (Triangle ? 3 : 6) && Queued.AddGeometry(Geometry, Error) &&
			Renderer->RenderTexture(Image, Queued, {}, Error), "queued geometry upload and draw");
		Check(Geometry && Geometry->Close(Error), "queued geometry release");
		QueuedTargets.push_back(std::move(Image));
	}
	for (std::size_t Index = 0; Index < QueuedTargets.size(); ++Index)
	{
		Check(Device->ReadTexture(QueuedTargets[Index], Pixels, Error) &&
			Pixel(6, 2, Index % 3 == 0 ? 255 : 0, Index % 3 == 1 ? 255 : 0, Index % 3 == 2 ? 255 : 0) &&
			(Index % 2 == 0 || Pixel(2, 6, 0, 0, 0, 0)), "recycled buffers changed an earlier queued frame");
		Check(QueuedTargets[Index]->Close(Error), "queued target cleanup");
	}
	auto Other = Service->CreateDevice(DeviceOptions, Error);
	auto OtherRenderer = Other ? Other->CreateRenderer2D(Error) : nullptr;
	Check(OtherRenderer && !OtherRenderer->RenderTexture(Target, List, {}, Error), "foreign target rejected");
	if (OtherRenderer)
	{
		OtherRenderer->Close(Error);
	}
	if (Other)
	{
		Other->Close(Error);
	}
	bool ForeignAccepted = true;
	std::thread Worker(
		[&]
		{
			std::string E;
			ForeignAccepted = Renderer->RenderTexture(Target, List, {}, E);
		});
	Worker.join();
	Check(!ForeignAccepted, "wrong-thread submission rejected");
	auto Platform = Sdl3::CreatePlatform({}, Error);
	if (Platform)
	{
		Platform::FWindowOptions W;
		W.Width = 64;
		W.Height = 64;
		W.GraphicsApi = Device->GetDriver();
		auto Window = Platform->CreateWindow(W, Error);
		auto Surface = Window ? Device->CreateSurface(Window, Error) : nullptr;
		Check(static_cast<bool>(Surface), "2D window surface");
		if (Surface)
		{
			bool Presented = false;
			Check(Renderer->RenderSurface(Surface, List, {}, Presented, Error), "2D window draw");
			FRenderOptions2D LoadOptions;
			LoadOptions.bClear = false;
			Check(!Renderer->RenderSurface(Surface, List, LoadOptions, Presented, Error), "swapchain load rejected");
			for (const auto Size : {32, 96, 64})
			{
				Check(Window->SetSize(Size, Size, Error) &&
						  Renderer->RenderSurface(Surface, List, {}, Presented, Error),
					  "surface rendering after window resize");
			}
			Check(Surface->Close(Error), "surface close releases window claim");
			Surface = Device->CreateSurface(Window, Error);
			Check(Surface && Renderer->RenderSurface(Surface, List, {}, Presented, Error),
				  "closed surface can be recreated");
			Check(!Window->Close(Error) && Window->IsOpen() && Surface && Surface->IsOpen(),
				  "window close rejects a live surface and preserves ownership");
			Check(Surface && Surface->Close(Error) && Window->Close(Error),
				  "surface and window close in dependency order");
		}
		if (Window)
		{
			Window->Close(Error);
		}
	}
	Check(!Device->Close(Error), "device protects renderer resources");
	List.Clear();
	Gradient->Close(Error);
	PremultipliedGradient->Close(Error);
	Layer->Close(Error);
	Srgb->Close(Error);
	SrgbImage->Close(Error);
	LinearColor->Close(Error);
	Repeat->Close(Error);
	Nearest->Close(Error);
	Texture->Close(Error);
	Red->Close(Error);
	White->Close(Error);
	Repeated->Close(Error);
	Translucent->Close(Error);
	auto RetiredRenderer = Device->CreateRenderer2D(Error);
	auto RetiredGeometry = RetiredRenderer ? RetiredRenderer->CreateGeometry(Quad(), Indices, Error) : nullptr;
	auto RetiredSampler = Device->CreateSampler({}, Error);
	std::thread Retire(
		[R = std::move(RetiredRenderer), G = std::move(RetiredGeometry), S = std::move(RetiredSampler)]() mutable
		{
			G.reset();
			S.reset();
			R.reset();
		});
	Retire.join();
	Check(Renderer->Close(Error) && Renderer->Close(Error), "idempotent renderer close");
	Check(!Renderer->RenderTexture(Target, List, {}, Error), "closed renderer rejected");
	Target->Close(Error);
	Check(Device->WaitIdle(Error) && Device->Close(Error), "2D resource retirement and device close");
	std::cout << "2D drawing failures: " << Failures << '\n';
	return Failures ? 1 : 0;
}

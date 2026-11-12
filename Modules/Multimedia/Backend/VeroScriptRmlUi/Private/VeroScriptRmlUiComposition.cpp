// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUiComposition.h"
#include <algorithm>
#include <limits>

namespace VeroScript::RmlUi
{
	namespace
	{
		struct FSubmissionGuard
		{
			Graphics::IRenderSubmission2D* Submission;
			~FSubmissionGuard()
			{
				if (Submission)
				{
					// Cleanup must also cover provider exceptions and failed submission.
					try
					{
						std::string Ignored;
						Submission->CancelSubmission(Ignored);
					}
					catch (...)
					{
					}
				}
			}
		};
	}

	bool FCompositionFrame::Initialize(const FOptions& InOptions, std::shared_ptr<Graphics::IRenderer2D> InRenderer,
	    std::shared_ptr<Graphics::ISampler> InSampler, std::string& Error)
	{
		Options = InOptions;
		Renderer = std::move(InRenderer);
		Sampler = std::move(InSampler);
		if (!Options.Device)
		{
			return true;
		}
		const auto Caps = Options.Device->GetCapabilities();
		if (!Caps.bClipMasks || !Caps.bFilters)
		{
			return true;
		}
		if (Options.MaxCompositionBytes < 8 || Options.MaxCompositionBytes > static_cast<std::uint64_t>(INT64_MAX))
		{
			Error = "rmlui.invalid_composition_budget";
			return false;
		}
		bAvailable = true;
		return true;
	}

	void FCompositionFrame::Begin(std::int64_t InWidth, std::int64_t InHeight)
	{
		Cancel();
		Width = InWidth;
		Height = InHeight;
	}

	bool FCompositionFrame::IsAvailable() const noexcept
	{
		return bAvailable && Renderer && Renderer->IsOpen();
	}

	bool FCompositionFrame::HasWork() const noexcept
	{
		return LayerCount > 1 || !Commands.empty();
	}

	bool FCompositionFrame::AddDraw(const Graphics::FDrawList2D& Draws, std::string& Error,
	    const std::vector<FClipMask>& Masks)
	{
		if (Draws.GetCommands().empty())
		{
			return true;
		}
		if (Commands.size() >= 100000)
		{
			Error = "rmlui.composition_command_limit";
			return false;
		}
		FCommand Command;
		Command.Destination = Stack.back();
		Command.Draws = Draws;
		Command.Masks = Masks;
		Commands.push_back(std::move(Command));
		return true;
	}

	std::uintptr_t FCompositionFrame::Push(std::string& Error)
	{
		if (!IsAvailable())
		{
			Error = "rmlui.layers_unavailable";
			return 0;
		}
		if (Stack.size() >= 32 || LayerCount >= 128)
		{
			Error = "rmlui.layer_limit";
			return 0;
		}
		const auto Handle = LayerCount++;
		Stack.push_back(Handle);
		return Handle;
	}

	bool FCompositionFrame::Pop(std::string& Error)
	{
		if (Stack.size() <= 1)
		{
			Error = "rmlui.unbalanced_layers";
			return false;
		}
		Stack.pop_back();
		return true;
	}

	bool FCompositionFrame::Capture(std::shared_ptr<Graphics::ITexture> Target, const Graphics::FRect& Region,
	    bool Clip, const Graphics::FRect& Scissor, std::string& Error)
	{
		if (!IsAvailable() || !Target || !Target->IsOpen() || Commands.size() >= 100000 ||
		    !Graphics::ValidateRect(Region, Error))
		{
			Error = "rmlui.invalid_layer_capture";
			return false;
		}
		FCommand Command;
		Command.Source = Stack.back();
		Command.CaptureTarget = std::move(Target);
		Command.CaptureRegion = Region;
		Command.bClip = Clip;
		Command.Scissor = Scissor;
		Commands.push_back(std::move(Command));
		return true;
	}

	bool FCompositionFrame::Composite(std::uintptr_t Source, std::uintptr_t Destination, bool Replace,
	    std::vector<Graphics::FEffect2D> Effects, bool Clip,
	    const Graphics::FRect& Scissor, std::string& Error,
	    const std::vector<FClipMask>& Masks)
	{
		if (!IsAvailable() || Source >= LayerCount || Destination >= LayerCount || Commands.size() >= 100000)
		{
			Error = "rmlui.invalid_layer_composition";
			return false;
		}
		FCommand Command;
		Command.bComposite = true;
		Command.Source = Source;
		Command.Destination = Destination;
		Command.bReplace = Replace;
		Command.Effects = std::move(Effects);
		Command.bClip = Clip;
		Command.Masks = Masks;
		Command.Scissor = Scissor;
		Commands.push_back(std::move(Command));
		return true;
	}

	bool FCompositionFrame::Submit(bool& Presented, std::string& Error)
	{
		Presented = false;
		if (Stack.size() != 1)
		{
			Error = "rmlui.unbalanced_layers";
			return false;
		}
		if (!Prepare(Error))
		{
			return false;
		}
		auto* Submission = dynamic_cast<Graphics::IRenderSubmission2D*>(Renderer.get());
		if (Submission && !Submission->BeginSubmission(Error)) return false;
		FSubmissionGuard Guard{Submission};
		std::vector<bool> Initialized(LayerCount, false);
		Graphics::FRenderOptions2D Load;
		Load.bClear = false;
		for (const auto& Command : Commands)
		{
			if (Command.CaptureTarget)
			{
				if (!ClearLayer(Command.Source, Initialized, Error))
				{
					return false;
				}
				Graphics::FTransform2D Translation;
				Translation.Values[12] = -static_cast<float>(Command.CaptureRegion.X);
				Translation.Values[13] = -static_cast<float>(Command.CaptureRegion.Y);
				Graphics::FDrawList2D Shifted;
				Shifted.SetTexture(Pool[Command.Source], Sampler);
				if (!Shifted.SetTransform(Translation, Error) ||
				    !Shifted.SetBlendMode(Graphics::EBlendMode::Opaque, Error) ||
				    !Shifted.SetVertexColorMode(Graphics::EVertexColorMode::Premultiplied, Error))
				{
					return false;
				}
				if (Command.bClip)
				{
					auto Clip = Command.Scissor;
					Clip.X -= Command.CaptureRegion.X;
					Clip.Y -= Command.CaptureRegion.Y;
					if (!Shifted.PushClip(Clip, Error))
					{
						return false;
					}
				}
				if (!Shifted.AddGeometry(Fullscreen, Error) ||
				    !Renderer->RenderTexture(Command.CaptureTarget, Shifted, {}, Error))
				{
					return false;
				}
				continue;
			}
			if (!ClearLayer(Command.Destination, Initialized, Error))
			{
				return false;
			}
			if (!Command.bComposite)
			{
				if (!RenderDraw(Pool[Command.Destination], Command.Draws, Command.Masks, Load, Submission, Error))
				{
					return false;
				}
				continue;
			}
			if (!ClearLayer(Command.Source, Initialized, Error))
			{
				return false;
			}
			auto Source = Pool[Command.Source];
			Graphics::FDrawList2D Draws;
			// A single color filter needs only one shader pass. Feedback still uses
			// scratch storage; spatial filters and ordered chains retain the compositor.
			const bool SingleColor = Command.Effects.size() == 1 &&
			    Command.Effects.front().Type == Graphics::EEffect2D::ColorMatrix;
			const bool InlineColor = Command.Source != Command.Destination && SingleColor;
			Graphics::FEffect2D Color;
			if (SingleColor)
			{
				Color = Command.Effects.front();
				Color.bSourcePremultiplied = true;
			}
			if (Command.Source == Command.Destination || (!Command.Effects.empty() && !InlineColor))
			{
				if (SingleColor)
				{
					if (!Quad(Draws, Source, true, false, {}, Error, &Color) ||
					    !Renderer->RenderTexture(Pool[LayerCount], Draws, {}, Error))
					{
						return false;
					}
				}
				else
				{
					if (!Quad(Draws, Source, true, false, {}, Error))
					{
						return false;
					}
					Graphics::FComposition2D Composition;
					if (!Composition.AddDraw(Draws, Error))
					{
						return false;
					}
					for (const auto& Effect : Command.Effects)
					{
						if (!Composition.AddEffect(Effect, Error))
						{
							return false;
						}
					}
					if ((Submission && !Submission->EndSubmission(Error)) ||
					    !Compositor->RenderTexture(Pool[LayerCount], Composition, {}, Error) ||
					    (Submission && !Submission->BeginSubmission(Error)))
					{
						return false;
					}
				}
				Source = Pool[LayerCount];
			}
			if (!Quad(Draws, Source, Command.bReplace, Command.bClip, Command.Scissor, Error,
			        InlineColor ? &Color : nullptr) ||
			    !RenderDraw(Pool[Command.Destination], Draws, Command.Masks, Load, Submission, Error))
			{
				return false;
			}
		}
		if (!ClearLayer(0, Initialized, Error))
		{
			return false;
		}
		Graphics::FDrawList2D Final;
		if (!Quad(Final, Pool[0], Options.RenderOptions.bClear, false, {}, Error))
		{
			return false;
		}
		bool TargetPresented = false;
		const bool Success = Options.Texture
		    ? Renderer->RenderTexture(Options.Texture, Final, Options.RenderOptions, Error)
		    : Renderer->RenderSurface(Options.Surface, Final, Options.RenderOptions, TargetPresented, Error);
		if (!Success) return false;
		if (Submission && Options.Surface && !TargetPresented &&
		    std::none_of(Commands.begin(), Commands.end(), [](const auto& Command)
		{
			return bool(Command.CaptureTarget);
		}))
		{
			// No swapchain image was available. Scratch layers are rebuilt next
			// frame, so do not queue unused GPU work. Persistent captures must
			// still complete, including masks RmlUi may reuse on a later frame.
			return Submission->CancelSubmission(Error);
		}
		if (Submission && !Submission->EndSubmission(Error)) return false;
		Presented = TargetPresented;
		return true;
	}

	void FCompositionFrame::Cancel()
	{
		Commands.clear();
		Stack.assign(1, 0);
		LayerCount = 1;
		bDirectMasks = false;
	}

	bool FCompositionFrame::Close(std::string& Error)
	{
		Cancel();
		if (Fullscreen && !Fullscreen->Close(Error))
		{
			return false;
		}
		Fullscreen.reset();
		for (auto& Texture : Pool)
		{
			if (!Texture->Close(Error))
			{
				return false;
			}
		}
		Pool.clear();
		if (Compositor && !Compositor->Close(Error))
		{
			return false;
		}
		Compositor.reset();
		bAvailable = false;
		Renderer.reset();
		Sampler.reset();
		return true;
	}

	bool FCompositionFrame::Prepare(std::string& Error)
	{
		if (!Compositor)
		{
			Graphics::FCompositorOptions2D Limits;
			Limits.MaxPooledBytes = Options.MaxCompositionBytes / 2;
			Compositor = Graphics::CreateCompositor2D(Options.Device, Limits, Error);
			if (!Compositor)
			{
				return false;
			}
		}
		if (Width < 1 || Height < 1 ||
		    static_cast<std::uint64_t>(Width) > UINT64_MAX / static_cast<std::uint64_t>(Height) / 4)
		{
			Error = "rmlui.invalid_composition_extent";
			return false;
		}
		const auto Bytes = static_cast<std::uint64_t>(Width) * static_cast<std::uint64_t>(Height) * 4;
		if (Bytes > (Options.MaxCompositionBytes / 2) / (LayerCount + 1))
		{
			Error = "rmlui.composition_budget_exceeded";
			return false;
		}
		if (PoolWidth != Width || PoolHeight != Height)
		{
			for (auto& Texture : Pool)
			{
				if (!Texture->Close(Error))
				{
					return false;
				}
			}
			Pool.clear();
			if (Fullscreen && !Fullscreen->Close(Error))
			{
				return false;
			}
			Fullscreen.reset();
			PoolWidth = Width;
			PoolHeight = Height;
		}
		const bool Masked = std::any_of(Commands.begin(), Commands.end(),
		    [](const auto& Command)
		{
			return !Command.Masks.empty();
		});
		// Three bounded scratch targets preserve isolation and the original
		// inner-to-outer mask order without nesting the general layer compositor.
		bDirectMasks = Masked && Bytes <= (Options.MaxCompositionBytes / 2) / (LayerCount + 4);
		while (Pool.size() < LayerCount + (bDirectMasks ? 4 : 1))
		{
			auto Texture = Options.Device->CreateTexture({Width, Height, Graphics::ETextureFormat::Rgba8Srgb}, Error);
			if (!Texture)
			{
				return false;
			}
			Pool.push_back(std::move(Texture));
		}
		if (!Fullscreen)
		{
			const auto W = static_cast<float>(Width), H = static_cast<float>(Height);
			Fullscreen = Renderer->CreateGeometry({{0, 0, 0, 0}, {W, 0, 1, 0}, {W, H, 1, 1}, {0, H, 0, 1}},
			    {0, 1, 2, 0, 2, 3}, Error);
			if (!Fullscreen)
			{
				return false;
			}
		}
		return true;
	}

	bool FCompositionFrame::Quad(Graphics::FDrawList2D& Draws, std::shared_ptr<Graphics::ITexture> Texture,
	    bool Replace, bool Clip, const Graphics::FRect& Scissor, std::string& Error,
	    const Graphics::FEffect2D* Effect)
	{
		Draws.Clear();
		Draws.SetTexture(std::move(Texture), Sampler);
		if (!Draws.SetBlendMode(Replace ? Graphics::EBlendMode::Opaque : Graphics::EBlendMode::PremultipliedAlpha,
		        Error) ||
		    !Draws.SetVertexColorMode(Graphics::EVertexColorMode::Premultiplied, Error) ||
		    (Effect && !Draws.SetEffect(*Effect, Error)))
		{
			return false;
		}
		if (Clip && !Draws.PushClip(Scissor, Error))
		{
			return false;
		}
		if (!Draws.AddGeometry(Fullscreen, Error))
		{
			return false;
		}
		return !Clip || Draws.PopClip(Error);
	}

	bool FCompositionFrame::ClearLayer(std::size_t Index, std::vector<bool>& Initialized, std::string& Error)
	{
		if (Initialized[Index])
		{
			return true;
		}
		Graphics::FDrawList2D Empty;
		Graphics::FRenderOptions2D Clear;
		if (Index == 0 && Options.RenderOptions.bClear)
		{
			Clear.ClearColor = Options.RenderOptions.ClearColor;
			Clear.ClearColor.R *= Clear.ClearColor.A;
			Clear.ClearColor.G *= Clear.ClearColor.A;
			Clear.ClearColor.B *= Clear.ClearColor.A;
		}
		if (!Renderer->RenderTexture(Pool[Index], Empty, Clear, Error))
		{
			return false;
		}
		Initialized[Index] = true;
		return true;
	}

	bool FCompositionFrame::RenderDraw(const std::shared_ptr<Graphics::ITexture>& Target,
	    const Graphics::FDrawList2D& Draws, const std::vector<FClipMask>& Masks,
	    const Graphics::FRenderOptions2D& DrawOptions,
	    Graphics::IRenderSubmission2D* Submission, std::string& Error)
	{
		if (Masks.empty())
		{
			return Renderer->RenderTexture(Target, Draws, DrawOptions, Error);
		}
		if (bDirectMasks)
		{
			auto Source = Pool[LayerCount + 1];
			auto Temporary = Pool[LayerCount + 2];
			const auto& MaskTexture = Pool[LayerCount + 3];
			if (!Renderer->RenderTexture(Source, Draws, {}, Error))
			{
				return false;
			}
			for (std::size_t Index = Masks.size(); Index > 0; --Index)
			{
				const auto& Mask = Masks[Index - 1];
				if (!Renderer->RenderTexture(MaskTexture, Mask.Draws, {}, Error))
				{
					return false;
				}
				Graphics::FEffect2D Effect;
				Effect.Type = Graphics::EEffect2D::AlphaMask;
				Effect.Mask = MaskTexture;
				Effect.bInvert = Mask.bInvert;
				Graphics::FDrawList2D MaskedDraw;
				if (!Quad(MaskedDraw, Source, false, false, {}, Error, &Effect) ||
				    !Renderer->RenderTexture(Index > 1 ? Temporary : Target, MaskedDraw,
				        Index > 1 ? Graphics::FRenderOptions2D{} : DrawOptions, Error))
				{
					return false;
				}
				std::swap(Source, Temporary);
			}
			return true;
		}
		Graphics::FComposition2D Masked;
		for (const auto& Mask : Masks)
		{
			if (!Masked.PushMask(Mask.Draws, Mask.bInvert, Error))
			{
				return false;
			}
		}
		if (!Masked.AddDraw(Draws, Error))
		{
			return false;
		}
		for (std::size_t I = 0; I < Masks.size(); ++I)
		{
			if (!Masked.PopMask(Error))
			{
				return false;
			}
		}
		// The general compositor owns another renderer. Submit this renderer's
		// producer passes before it samples them, then resume the local scope.
		if (Submission && !Submission->EndSubmission(Error)) return false;
		if (!Compositor->RenderTexture(Target, Masked, DrawOptions, Error)) return false;
		return !Submission || Submission->BeginSubmission(Error);
	}
} // namespace VeroScript::RmlUi

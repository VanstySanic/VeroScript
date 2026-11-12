// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGraphicsComposition2D.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

namespace VeroScript::Graphics
{
	void FComposition2D::Clear()
	{
		Commands.clear();
		Scopes.clear();
		DrawCount = 0;
	}

	bool FComposition2D::AddDraw(const FDrawList2D& Draws, std::string& Error)
	{
		if (Commands.size() >= 65536 || Draws.GetCommands().size() > 65536 - DrawCount)
		{
			Error = "graphics.resource_limit: composition command limit exceeded";
			return false;
		}
		FCompositionCommand2D Command;
		Command.Draws = Draws;
		Commands.push_back(std::move(Command));
		DrawCount += Draws.GetCommands().size();
		Error.clear();
		return true;
	}

	bool FComposition2D::BeginLayer(float Opacity, std::string& Error)
	{
		if (!std::isfinite(Opacity) || Opacity < 0 || Opacity > 1)
		{
			Error = "graphics.invalid_opacity: expected a finite value in 0..1";
			return false;
		}
		FCompositionCommand2D Command;
		Command.Type = ECompositionCommand2D::BeginLayer;
		Command.Opacity = Opacity;
		return Begin(std::move(Command), false, Error);
	}

	bool FComposition2D::EndLayer(std::string& Error)
	{
		return End(false, Error);
	}

	bool FComposition2D::PushMask(const FDrawList2D& Mask, bool bInvert, std::string& Error)
	{
		FCompositionCommand2D Command;
		Command.Type = ECompositionCommand2D::BeginMask;
		Command.Draws = Mask;
		Command.bInvert = bInvert;
		return Begin(std::move(Command), true, Error);
	}

	bool FComposition2D::PopMask(std::string& Error)
	{
		return End(true, Error);
	}

	bool FComposition2D::AddEffect(const FEffect2D& Effect, std::string& Error)
	{
		if (!ValidateEffect2D(Effect, Error))
		{
			return false;
		}
		if (Commands.size() >= 65536)
		{
			Error = "graphics.resource_limit: composition command limit exceeded";
			return false;
		}
		FCompositionCommand2D Command;
		Command.Type = ECompositionCommand2D::Effect;
		Command.Effect = Effect;
		Commands.push_back(std::move(Command));
		Error.clear();
		return true;
	}

	bool FComposition2D::AddBlur(float Radius, std::string& Error)
	{
		FEffect2D Effect;
		Effect.Type = EEffect2D::Blur;
		Effect.Radius = Radius;
		if (!ValidateEffect2D(Effect, Error) || Commands.size() > 65534)
		{
			if (Error.empty())
			{
				Error = "graphics.resource_limit: composition command limit exceeded";
			}
			return false;
		}
		if (Commands.capacity() - Commands.size() < 2)
		{
			// Reserve both passes atomically without reallocating for every appended blur.
			const auto Capacity = std::min<std::size_t>(Commands.capacity(), 32768) * 2;
			Commands.reserve(std::max(Commands.size() + 2, Capacity));
		}
		AddEffect(Effect, Error);
		Effect.bVertical = true;
		return AddEffect(Effect, Error);
	}

	bool FComposition2D::AddBackdropBlur(const FDrawList2D& Mask, float Radius, std::string& Error)
	{
		FCompositionCommand2D Command;
		Command.Type = ECompositionCommand2D::BackdropBlur;
		Command.Effect.Type = EEffect2D::Blur;
		Command.Effect.Radius = Radius;
		if (!ValidateEffect2D(Command.Effect, Error))
		{
			return false;
		}
		if (Commands.size() >= 65536 || Mask.GetCommands().size() > 65536 - DrawCount)
		{
			Error = "graphics.resource_limit: composition command limit exceeded";
			return false;
		}
		Command.Draws = Mask;
		Commands.push_back(std::move(Command));
		DrawCount += Mask.GetCommands().size();
		Error.clear();
		return true;
	}

	bool FComposition2D::IsBalanced() const noexcept
	{
		return Scopes.empty();
	}

	const std::vector<FCompositionCommand2D>& FComposition2D::GetCommands() const noexcept
	{
		return Commands;
	}

	bool FComposition2D::Begin(FCompositionCommand2D Command, bool bMask, std::string& Error)
	{
		if (Commands.size() >= 65536 || Scopes.size() >= 64 || Command.Draws.GetCommands().size() > 65536 - DrawCount)
		{
			Error = "graphics.resource_limit: composition scope or command limit exceeded";
			return false;
		}
		Scopes.reserve(Scopes.size() + 1);
		const auto Count = Command.Draws.GetCommands().size();
		Commands.push_back(std::move(Command));
		Scopes.push_back(bMask);
		DrawCount += Count;
		Error.clear();
		return true;
	}

	bool FComposition2D::End(bool bMask, std::string& Error)
	{
		if (Scopes.empty() || Scopes.back() != bMask || Commands.size() >= 65536)
		{
			Error = "graphics.invalid_composition: mismatched layer or mask scope";
			return false;
		}
		FCompositionCommand2D Command;
		Command.Type = bMask ? ECompositionCommand2D::EndMask : ECompositionCommand2D::EndLayer;
		Commands.push_back(std::move(Command));
		Scopes.pop_back();
		Error.clear();
		return true;
	}

	namespace
	{
		class FCompositor final : public ICompositor2D
		{
		public:
			//~ Begin Graphics::ICompositor2D Interface
			bool IsOpen() const noexcept override
			{
				return bOpen && Device && Device->IsOpen();
			}
			std::int64_t GetPooledBytes() const noexcept override
			{
				return static_cast<std::int64_t>(PooledBytes);
			}
			bool RenderTexture(const std::shared_ptr<ITexture>& Target, const FComposition2D& Composition,
			    const FRenderOptions2D& Options, std::string& Error) override
			{
				if (!Check(Error) || !Target || !Target->IsOpen())
				{
					if (Error.empty())
					{
						Error = "graphics.invalid_target: expected an open composition texture";
					}
					return false;
				}
				const auto Size = Target->GetOptions();
				return Render(Target, {}, Size.Width, Size.Height, Composition, Options, nullptr, Error);
			}
			bool RenderSurface(const std::shared_ptr<ISurface>& Target, std::int64_t Width, std::int64_t Height,
			    const FComposition2D& Composition, const FRenderOptions2D& Options, bool& bPresented,
			    std::string& Error) override
			{
				bPresented = false;
				if (!Check(Error) || !Target || !Target->IsOpen() || !Options.bClear)
				{
					if (Error.empty())
					{
						Error = "graphics.invalid_target: composition surfaces require an open target and clear";
					}
					return false;
				}
				return Render({}, Target, Width, Height, Composition, Options, &bPresented, Error);
			}
			bool Trim(std::string& Error) override
			{
				if (!CheckThread(Error))
				{
					return false;
				}
				for (auto It = Pool.begin(); It != Pool.end();)
				{
					if (!It->Texture->Close(Error))
					{
						return false;
					}
					PooledBytes -= It->Bytes;
					It = Pool.erase(It);
				}
				Error.clear();
				return true;
			}
			bool Close(std::string& Error) override
			{
				if (!CheckThread(Error))
				{
					return false;
				}
				if (!bOpen)
				{
					Error.clear();
					return true;
				}
				if (!Trim(Error) || (Quad && !Quad->Close(Error)) || !Renderer->Close(Error))
				{
					return false;
				}
				Quad.reset();
				Renderer.reset();
				Device.reset();
				bOpen = false;
				Error.clear();
				return true;
			}
			//~ End Graphics::ICompositor2D Interface

			FCompositor(std::shared_ptr<IDevice> InDevice, std::shared_ptr<IRenderer2D> InRenderer,
			    FCompositorOptions2D InOptions)
			    : Device(std::move(InDevice)), Renderer(std::move(InRenderer)), Limits(InOptions)
			{
			}

		private:
			struct FPooled
			{
				std::shared_ptr<ITexture> Texture;
				std::uint64_t Bytes = 0;
				bool bActive = false;
			};
			struct FLayer
			{
				std::shared_ptr<ITexture> Texture;
				const FCompositionCommand2D* Begin = nullptr;
			};
			struct FFrameGuard
			{
				~FFrameGuard()
				{
					for (auto& Entry : Owner.Pool)
					{
						Entry.bActive = false;
					}
				}
				FCompositor& Owner;
			};
			bool CheckThread(std::string& Error) const
			{
				if (std::this_thread::get_id() != OwnerThread)
				{
					Error = "graphics.wrong_thread: composition requires its owner thread";
					return false;
				}
				return true;
			}
			bool Check(std::string& Error) const
			{
				if (!CheckThread(Error))
				{
					return false;
				}
				if (!bOpen || !Device->IsOpen())
				{
					Error = "graphics.closed_compositor: composition resources are closed";
					return false;
				}
				Error.clear();
				return true;
			}
			std::shared_ptr<ITexture> Acquire(std::int64_t Width, std::int64_t Height, std::string& Error)
			{
				const FTextureOptions Options{Width, Height, Limits.IntermediateFormat};
				std::uint64_t Bytes = 0;
				if (!ValidateTextureOptions(Options, Limits.MaxPooledBytes, Bytes, Error))
				{
					return {};
				}
				for (auto& Entry : Pool)
				{
					const auto Size = Entry.Texture->GetOptions();
					if (!Entry.bActive && Size.Width == Width && Size.Height == Height)
					{
						Entry.bActive = true;
						return Entry.Texture;
					}
				}
				for (auto It = Pool.begin(); Bytes > Limits.MaxPooledBytes - PooledBytes && It != Pool.end();)
				{
					if (It->bActive)
					{
						++It;
						continue;
					}
					if (!It->Texture->Close(Error))
					{
						return {};
					}
					PooledBytes -= It->Bytes;
					It = Pool.erase(It);
				}
				if (Bytes > Limits.MaxPooledBytes - PooledBytes)
				{
					Error = "graphics.resource_limit: composition temporary texture budget exceeded";
					return {};
				}
				auto Texture = Device->CreateTexture(Options, Error);
				if (!Texture)
				{
					return {};
				}
				Pool.push_back({Texture, Bytes, true});
				PooledBytes += Bytes;
				return Texture;
			}
			void Release(const std::shared_ptr<ITexture>& Texture)
			{
				for (auto& Entry : Pool)
				{
					if (Entry.Texture == Texture)
					{
						Entry.bActive = false;
						return;
					}
				}
			}
			bool DrawQuad(const std::shared_ptr<ITexture>& Target, const std::shared_ptr<ITexture>& Source,
			    const FEffect2D& Effect, float Opacity, bool bClear, std::string& Error,
			    const FRect* Clip = nullptr, EBlendMode Blend = EBlendMode::PremultipliedAlpha)
			{
				FDrawList2D Draws;
				Draws.SetTexture(Source);
				if (!Draws.SetBlendMode(Blend, Error) || !Draws.SetTint({1, 1, 1, Opacity}, Error) ||
				    !Draws.SetEffect(Effect, Error) || (Clip && !Draws.PushClip(*Clip, Error)) ||
				    !Draws.AddGeometry(Quad, Error))
				{
					return false;
				}
				FRenderOptions2D Options;
				Options.bClear = bClear;
				return Renderer->RenderTexture(Target, Draws, Options, Error);
			}
			bool Render(const std::shared_ptr<ITexture>& Target, const std::shared_ptr<ISurface>& Surface,
			    std::int64_t Width, std::int64_t Height, const FComposition2D& Composition,
			    const FRenderOptions2D& Options, bool* bPresented, std::string& Error)
			{
				if (!Composition.IsBalanced() || !ValidateColor(Options.ClearColor, Error) ||
				    !ValidateRect({0, 0, Width, Height}, Error) || Width <= 0 || Height <= 0)
				{
					Error = "graphics.invalid_composition: unbalanced scopes or invalid target dimensions";
					return false;
				}
				const FRect Viewport = Options.bViewport ? Options.Viewport : FRect{0, 0, Width, Height};
				if (!ValidateRect(Viewport, Error) || Viewport.X < 0 || Viewport.Y < 0 || Viewport.Width <= 0 ||
				    Viewport.Height <= 0 || Viewport.X + Viewport.Width > Width ||
				    Viewport.Y + Viewport.Height > Height)
				{
					Error = "graphics.invalid_viewport: composition viewport is outside the target";
					return false;
				}
				std::int64_t Depth = 0;
				for (const auto& Command : Composition.GetCommands())
				{
					if (Command.Type == ECompositionCommand2D::BeginLayer ||
					    Command.Type == ECompositionCommand2D::BeginMask)
					{
						if (++Depth > Limits.MaxDepth)
						{
							Error = "graphics.resource_limit: composition depth exceeds the configured limit";
							return false;
						}
					}
					else if (Command.Type == ECompositionCommand2D::EndLayer ||
					    Command.Type == ECompositionCommand2D::EndMask)
					{
						--Depth;
					}
				}
				if (!Quad || QuadWidth != Width || QuadHeight != Height)
				{
					const auto W = static_cast<float>(Width), H = static_cast<float>(Height);
					auto Candidate = Renderer->CreateGeometry({{0, 0, 0, 0}, {W, 0, 1, 0}, {W, H, 1, 1}, {0, H, 0, 1}},
					    {0, 1, 2, 0, 2, 3}, Error);
					if (!Candidate || (Quad && !Quad->Close(Error)))
					{
						return false;
					}
					Quad = std::move(Candidate);
					QuadWidth = Width;
					QuadHeight = Height;
				}
				FFrameGuard Guard{*this};
				auto Root = Acquire(Width, Height, Error);
				auto ClearColor = Options.ClearColor;
				ClearColor.R *= ClearColor.A;
				ClearColor.G *= ClearColor.A;
				ClearColor.B *= ClearColor.A;
				if (!Root || !(Options.bClear ? Device->ClearTexture(Root, ClearColor, Error) : DrawQuad(Root, Target, {}, 1, true, Error)))
				{
					return false;
				}
				std::vector<FLayer> Stack{{Root, nullptr}};
				FRenderOptions2D DrawOptions = Options;
				DrawOptions.bClear = false;
				for (const auto& Command : Composition.GetCommands())
				{
					switch (Command.Type)
					{
					case ECompositionCommand2D::Draw:
						{
							if (!Renderer->RenderTexture(Stack.back().Texture, Command.Draws, DrawOptions, Error))
							{
								return false;
							}
							break;
						}
					case ECompositionCommand2D::BeginLayer:
					case ECompositionCommand2D::BeginMask:
						{
							auto Layer = Acquire(Width, Height, Error);
							if (!Layer || !Device->ClearTexture(Layer, {0, 0, 0, 0}, Error))
							{
								return false;
							}
							Stack.push_back({std::move(Layer), &Command});
							break;
						}
					case ECompositionCommand2D::EndLayer:
					case ECompositionCommand2D::EndMask:
						{
							const auto Layer = Stack.back();
							Stack.pop_back();
							FEffect2D Effect;
							if (Command.Type == ECompositionCommand2D::EndMask)
							{
								Effect.Type = EEffect2D::AlphaMask;
								Effect.bInvert = Layer.Begin->bInvert;
								Effect.Mask = Acquire(Width, Height, Error);
								FRenderOptions2D MaskOptions = Options;
								MaskOptions.bClear = true;
								MaskOptions.ClearColor = {0, 0, 0, 0};
								if (!Effect.Mask ||
								    !Renderer->RenderTexture(Effect.Mask, Layer.Begin->Draws, MaskOptions, Error))
								{
									return false;
								}
							}
							if (!DrawQuad(Stack.back().Texture, Layer.Texture, Effect, Layer.Begin->Opacity, false,
							        Error))
							{
								return false;
							}
							Release(Effect.Mask);
							Release(Layer.Texture);
							break;
						}
					case ECompositionCommand2D::Effect:
						{
							std::shared_ptr<ITexture> Horizontal;
							auto Source = Stack.back().Texture;
							auto Effect = Command.Effect;
							Effect.bSourcePremultiplied = true;
							if (Effect.Type == EEffect2D::Shadow)
							{
								Horizontal = Acquire(Width, Height, Error);
								FEffect2D Blur;
								Blur.Type = EEffect2D::Blur;
								Blur.Radius = Effect.Radius;
								if (!Horizontal || !DrawQuad(Horizontal, Source, Blur, 1, true, Error))
								{
									return false;
								}
								Source = Horizontal;
								Effect.bVertical = true;
							}
							auto Destination = Acquire(Width, Height, Error);
							if (!Destination || !DrawQuad(Destination, Source, Effect, 1, true, Error) ||
							    (Horizontal && !DrawQuad(Destination, Stack.back().Texture, {}, 1, false, Error)))
							{
								return false;
							}
							Release(Horizontal);
							Release(Stack.back().Texture);
							Stack.back().Texture = std::move(Destination);
							break;
						}
					case ECompositionCommand2D::BackdropBlur:
						{
							auto Horizontal = Acquire(Width, Height, Error);
							auto Effect = Command.Effect;
							if (!Horizontal || !DrawQuad(Horizontal, Stack.back().Texture, Effect, 1, true, Error))
							{
								return false;
							}
							auto Blurred = Acquire(Width, Height, Error);
							Effect.bVertical = true;
							if (!Blurred || !DrawQuad(Blurred, Horizontal, Effect, 1, true, Error))
							{
								return false;
							}
							Release(Horizontal);
							Effect = {};
							Effect.Type = EEffect2D::AlphaMask;
							Effect.Mask = Acquire(Width, Height, Error);
							FRenderOptions2D MaskOptions = Options;
							MaskOptions.bClear = true;
							MaskOptions.ClearColor = {0, 0, 0, 0};
							if (!Effect.Mask ||
							    !Renderer->RenderTexture(Effect.Mask, Command.Draws, MaskOptions, Error))
							{
								return false;
							}
							auto Destination = Acquire(Width, Height, Error);
							Effect.bInvert = true;
							if (!Destination || !DrawQuad(Destination, Stack.back().Texture, Effect, 1, true, Error))
							{
								return false;
							}
							Effect.bInvert = false;
							if (!DrawQuad(Destination, Blurred, Effect, 1, false, Error, nullptr, EBlendMode::Additive))
							{
								return false;
							}
							Release(Effect.Mask);
							Release(Blurred);
							Release(Stack.back().Texture);
							Stack.back().Texture = std::move(Destination);
							break;
						}

					default:
						{
							break;
						}
					}
				}
				FDrawList2D Final;
				Final.SetTexture(Stack.front().Texture);
				if (!Final.SetBlendMode(EBlendMode::Opaque, Error) || !Final.PushClip(Viewport, Error) ||
				    !Final.AddGeometry(Quad, Error))
				{
					return false;
				}
				FRenderOptions2D FinalOptions = Options;
				FinalOptions.ClearColor = ClearColor;
				FinalOptions.bViewport = false;
				return Target ? Renderer->RenderTexture(Target, Final, FinalOptions, Error)
				              : Renderer->RenderSurface(Surface, Final, FinalOptions, *bPresented, Error);
			}

			std::shared_ptr<IDevice> Device;
			std::shared_ptr<IRenderer2D> Renderer;
			FCompositorOptions2D Limits;
			std::vector<FPooled> Pool;
			std::shared_ptr<IGeometry2D> Quad;
			std::int64_t QuadWidth = 0, QuadHeight = 0;
			std::uint64_t PooledBytes = 0;
			std::thread::id OwnerThread = std::this_thread::get_id();
			bool bOpen = true;
		};
	}
	std::shared_ptr<ICompositor2D> CreateCompositor2D(std::shared_ptr<IDevice> Device,
	    const FCompositorOptions2D& Options, std::string& Error)
	{
		if (!Device || !Device->IsOpen() || !Options.MaxPooledBytes ||
		    Options.MaxPooledBytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
		    Options.MaxDepth < 1 || Options.MaxDepth > 64 || Options.IntermediateFormat > ETextureFormat::Rgba8Srgb)
		{
			Error = "graphics.invalid_compositor: invalid device, texture budget, format or depth";
			return {};
		}
		const auto Caps = Device->GetCapabilities();
		if (!Caps.bGuiBasic || !Caps.bClipMasks || !Caps.bFilters)
		{
			Error = "graphics.unsupported_compositor: device requires basic drawing, masks and filter support";
			return {};
		}
		auto Renderer = Device->CreateRenderer2D(Error);
		if (!Renderer)
		{
			return {};
		}
		Error.clear();
		return std::make_shared<FCompositor>(std::move(Device), std::move(Renderer), Options);
	}
}

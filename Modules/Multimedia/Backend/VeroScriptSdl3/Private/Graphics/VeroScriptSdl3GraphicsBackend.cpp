// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3GraphicsBackend.h"
#include <limits>
#include <chrono>

namespace VeroScript::Sdl3
{
	using namespace Graphics;
	namespace
	{
		struct FBackendRetirement final : FRetirement
		{
			void Release() noexcept override
			{
				Native.reset();
			}
			std::unique_ptr<IGraphicsBackend> Native;
		};
		struct FBackendState
		{
			FBackendState(std::shared_ptr<FRuntime> R, FDeviceOptions O, std::unique_ptr<IGraphicsBackend> N)
			    : Runtime(std::move(R)), Options(std::move(O)), Retirement(std::make_unique<FBackendRetirement>())
			{
				Retirement->Runtime = Runtime;
				Retirement->Native = std::move(N);
				Driver = Retirement->Native->GetDriver();
			}
			~FBackendState()
			{
				Runtime->Retire(std::move(Retirement));
			}
			bool Check(std::string& Error)
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				if (!bOpen->load())
				{
					Error = "graphics.closed_device: device is closed or invalidated";
					return false;
				}
				const bool Success = Retirement->Native->Poll(Error);
				if (Retirement->Native->IsLost())
				{
					bOpen->store(false);
				}
				return Success;
			}
			std::shared_ptr<FRuntime> Runtime;
			FDeviceOptions Options;
			std::unique_ptr<FBackendRetirement> Retirement;
			std::string Driver;
			std::uint64_t Generation = 0, Resources = 0;
			std::shared_ptr<std::atomic<bool>> bOpen = std::make_shared<std::atomic<bool>>(true);
		};
		template <class T>
		struct FResourceRetirement : FRetirement
		{
			~FResourceRetirement() override
			{
				Release();
			}
			void Release() noexcept override
			{
				if (Native)
				{
					Native.reset();
					--State->Resources;
				}
			}
			std::shared_ptr<FBackendState> State;
			std::unique_ptr<T> Native;
		};
		template <class T>
		std::unique_ptr<FResourceRetirement<T>> Retain(const std::shared_ptr<FBackendState>& State,
		    std::unique_ptr<T> Native)
		{
			auto Lease = std::make_unique<FResourceRetirement<T>>();
			Lease->State = State;
			Lease->Runtime = State->Runtime;
			Lease->Native = std::move(Native);
			++State->Resources;
			return Lease;
		}
		class FBackendTexture final : public ITexture
		{
		public:
			~FBackendTexture() override
			{
				Lease->Runtime->Retire(std::move(Lease));
			}
			bool IsOpen() const noexcept override
			{
				return bOpen.load() && Lease->State->bOpen->load();
			}
			FTextureOptions GetOptions() const noexcept override
			{
				return Options;
			}
			bool Close(std::string& Error) override
			{
				if (!Lease->Runtime->CheckThread(Error))
				{
					return false;
				}
				Lease->Release();
				bOpen.store(false);
				Error.clear();
				return true;
			}
			FBackendTexture(std::unique_ptr<FResourceRetirement<ITextureData>> L, FTextureOptions O)
			    : Lease(std::move(L)), Options(O)
			{
			}
			std::unique_ptr<FResourceRetirement<ITextureData>> Lease;
			FTextureOptions Options;
			std::atomic<bool> bOpen{true};
		};
		class FBackendSampler final : public ISampler
		{
		public:
			~FBackendSampler() override
			{
				Lease->Runtime->Retire(std::move(Lease));
			}
			bool IsOpen() const noexcept override
			{
				return bOpen.load() && Lease->State->bOpen->load();
			}
			FSamplerOptions GetOptions() const noexcept override
			{
				return Options;
			}
			bool Close(std::string& Error) override
			{
				if (!Lease->Runtime->CheckThread(Error))
				{
					return false;
				}
				Lease->Release();
				bOpen.store(false);
				Error.clear();
				return true;
			}
			FBackendSampler(std::unique_ptr<FResourceRetirement<ISamplerData>> L, FSamplerOptions O)
			    : Lease(std::move(L)), Options(O)
			{
			}
			std::unique_ptr<FResourceRetirement<ISamplerData>> Lease;
			FSamplerOptions Options;
			std::atomic<bool> bOpen{true};
		};
		class FBackendGeometry final : public IGeometry2D
		{
		public:
			~FBackendGeometry() override
			{
				Lease->Runtime->Retire(std::move(Lease));
			}
			bool IsOpen() const noexcept override
			{
				return bOpen.load() && Lease->State->bOpen->load();
			}
			std::int64_t GetVertexCount() const noexcept override
			{
				return VertexCount;
			}
			std::int64_t GetIndexCount() const noexcept override
			{
				return IndexCount;
			}
			bool Close(std::string& Error) override
			{
				if (!Lease->Runtime->CheckThread(Error))
				{
					return false;
				}
				Lease->Release();
				bOpen.store(false);
				Error.clear();
				return true;
			}
			FBackendGeometry(std::unique_ptr<FResourceRetirement<IGeometryData>> L, std::int64_t V, std::int64_t I)
			    : Lease(std::move(L)), VertexCount(V), IndexCount(I)
			{
			}
			std::unique_ptr<FResourceRetirement<IGeometryData>> Lease;
			std::int64_t VertexCount, IndexCount;
			std::atomic<bool> bOpen{true};
		};
		struct FBackendSurfaceRetirement final : FResourceRetirement<ISurfaceData>
		{
			~FBackendSurfaceRetirement() override
			{
				Release();
			}
			void Release() noexcept override
			{
				FResourceRetirement::Release();
				if (Window)
				{
					if (Window->Claim == this)
					{
						Window->Claim = nullptr;
					}
					--Window->Claims;
					Window.reset();
				}
			}
			std::shared_ptr<FWindowState> Window;
		};
		class FBackendSurface final : public ISurface
		{
		public:
			~FBackendSurface() override
			{
				Lease->Runtime->Retire(std::move(Lease));
			}
			bool IsOpen() const noexcept override
			{
				return bOpen.load() && Lease->State->bOpen->load() && Lease->Window && Lease->Window->bOpen.load();
			}
			std::uint64_t GetWindowId() const noexcept override
			{
				return WindowId;
			}
			bool Close(std::string& Error) override
			{
				if (!Lease->Runtime->CheckThread(Error))
				{
					return false;
				}
				Lease->Release();
				bOpen.store(false);
				Error.clear();
				return true;
			}
			explicit FBackendSurface(std::unique_ptr<FBackendSurfaceRetirement> L)
			    : Lease(std::move(L)), WindowId(Lease->Window->Id)
			{
			}
			std::unique_ptr<FBackendSurfaceRetirement> Lease;
			std::uint64_t WindowId;
			std::atomic<bool> bOpen{true};
		};
		class FBackendRenderer final : public IRenderer2D
		{
		public:
			bool IsOpen() const noexcept override
			{
				return bOpen && State->bOpen->load();
			}
			std::shared_ptr<IGeometry2D> CreateGeometry(const std::vector<FVertex2D>& V,
			    const std::vector<std::uint32_t>& I,
			    std::string& Error) override
			{
				if (!Check(Error) || !ValidateGeometry2D(V, I, State->Options.MaxGeometryBytes, Error))
				{
					return {};
				}
				auto Native = State->Retirement->Native->CreateGeometry(V, I, Error);
				if (!Native)
				{
					return {};
				}
				return std::make_shared<FBackendGeometry>(Retain(State, std::move(Native)), V.size(), I.size());
			}
			bool RenderTexture(const std::shared_ptr<ITexture>& Target, const FDrawList2D& List,
			    const FRenderOptions2D& Options, std::string& Error) override
			{
				if (!Check(Error))
				{
					return false;
				}
				auto Texture = std::dynamic_pointer_cast<FBackendTexture>(Target);
				if (!Texture || !Texture->IsOpen() || Texture->Lease->State != State)
				{
					Error = "graphics.invalid_target: expected an open texture from this device";
					return false;
				}
				return Render(Texture.get(), nullptr, Texture->Options.Width, Texture->Options.Height, List, Options,
				    Error);
			}
			bool RenderSurface(const std::shared_ptr<ISurface>& Target, const FDrawList2D& List,
			    const FRenderOptions2D& Options, bool& bPresented, std::string& Error) override
			{
				bPresented = false;
				if (!Check(Error))
				{
					return false;
				}
				auto Surface = std::dynamic_pointer_cast<FBackendSurface>(Target);
				if (!Surface || !Surface->IsOpen() || Surface->Lease->State != State || !Options.bClear)
				{
					Error = "graphics.invalid_target: expected an open surface and clear";
					return false;
				}
				int Width = 0, Height = 0;
				if (!SDL_GetWindowSizeInPixels(Surface->Lease->Window->Retirement->Native, &Width, &Height))
				{
					return SdlError("query graphics surface", Error);
				}
				if (Width <= 0 || Height <= 0)
				{
					Error.clear();
					return true;
				}
				return Render(nullptr, Surface.get(), Width, Height, List, Options, Error, &bPresented);
			}
			bool Close(std::string& Error) override
			{
				if (!State->Runtime->CheckThread(Error))
				{
					return false;
				}
				if (White && !White->Close(Error))
				{
					return false;
				}
				if (Sampler && !Sampler->Close(Error))
				{
					return false;
				}
				White.reset();
				Sampler.reset();
				bOpen = false;
				Error.clear();
				return true;
			}
			FBackendRenderer(std::shared_ptr<FBackendState> S, std::shared_ptr<FBackendTexture> W,
			    std::shared_ptr<FBackendSampler> P)
			    : State(std::move(S)), White(std::move(W)), Sampler(std::move(P))
			{
			}

		private:
			bool Check(std::string& Error) const
			{
				if (!State->Check(Error))
				{
					return false;
				}
				if (!bOpen)
				{
					Error = "graphics.closed_renderer: renderer is closed";
					return false;
				}
				return true;
			}
			bool Render(FBackendTexture* Target, FBackendSurface* Surface, std::int64_t W, std::int64_t H,
			    const FDrawList2D& List, const FRenderOptions2D& Options, std::string& Error,
			    bool* Presented = nullptr)
			{
				if (!ValidateColor(Options.ClearColor, Error) || !ValidateRect({0, 0, W, H}, Error))
				{
					return false;
				}
				const auto Viewport = Options.bViewport ? Options.Viewport : FRect{0, 0, W, H};
				if (!ValidateRect(Viewport, Error) || Viewport.X < 0 || Viewport.Y < 0 || Viewport.Width <= 0 ||
				    Viewport.Height <= 0 || Viewport.X + Viewport.Width > W || Viewport.Y + Viewport.Height > H)
				{
					Error = "graphics.invalid_viewport: viewport must be nonempty and inside the target";
					return false;
				}
				std::vector<FBackendDraw> Draws;
				Draws.reserve(List.GetCommands().size());
				for (const auto& Draw : List.GetCommands())
				{
					auto G = std::dynamic_pointer_cast<FBackendGeometry>(Draw.Geometry);
					auto T = Draw.Texture ? std::dynamic_pointer_cast<FBackendTexture>(Draw.Texture) : White;
					auto M = Draw.Effect.Type == EEffect2D::AlphaMask
					    ? std::dynamic_pointer_cast<FBackendTexture>(Draw.Effect.Mask)
					    : White;
					auto S = Draw.Sampler ? std::dynamic_pointer_cast<FBackendSampler>(Draw.Sampler) : Sampler;
					if (!G || !G->IsOpen() || G->Lease->State != State || !T || !T->IsOpen() ||
					    T->Lease->State != State || T.get() == Target || !M || !M->IsOpen() ||
					    M->Lease->State != State || M.get() == Target || !S || !S->IsOpen() || S->Lease->State != State)
					{
						Error = "graphics.invalid_draw: closed, foreign or feedback resource";
						return false;
					}
					if (!ValidateEffect2D(Draw.Effect, Error))
					{
						return false;
					}
					const auto Clip = Draw.bClip ? IntersectRects(Viewport, Draw.Clip) : Viewport;
					if (!Clip.Width || !Clip.Height)
					{
						continue;
					}
					Draws.push_back({&Draw, G->Lease->Native.get(), T->Lease->Native.get(), M->Lease->Native.get(),
					    S->Lease->Native.get(), T->Options, Clip});
				}
				bool bPresented = false;
				const bool bSuccess = State->Retirement->Native->Render(
				    Target ? Target->Lease->Native.get() : nullptr, Surface ? Surface->Lease->Native.get() : nullptr,
				    Options, Viewport, Draws, bPresented, Error);
				if (Presented)
				{
					*Presented = bSuccess && bPresented;
				}
				return bSuccess;
			}
			std::shared_ptr<FBackendState> State;
			std::shared_ptr<FBackendTexture> White;
			std::shared_ptr<FBackendSampler> Sampler;
			bool bOpen = true;
		};
		class FBackendDevice final : public IDevice
		{
		public:
			bool IsOpen() const noexcept override
			{
				return State->bOpen->load();
			}
			std::string GetDriver() const override
			{
				return State->Driver;
			}
			FCapabilities GetCapabilities() const noexcept override
			{
				FCapabilities Capabilities{true, true, true, true, true, true, true, true};
				#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
				// This backend keeps one EAGL context owner per window. Recovery
				// cannot prepare its replacement while the previous device is alive.
				Capabilities.bDeviceRecovery = State->Driver != "opengles";
				#endif
				return Capabilities;
			}
			std::shared_ptr<ITexture> CreateTexture(const FTextureOptions& O, std::string& E) override
			{
				std::uint64_t Bytes = 0;
				if (!State->Check(E) || !ValidateTextureOptions(O, State->Options.MaxTextureBytes, Bytes, E))
				{
					return {};
				}
				auto Native = State->Retirement->Native->CreateTexture(O, E);
				if (!Native)
				{
					return {};
				}
				return std::make_shared<FBackendTexture>(Retain(State, std::move(Native)), O);
			}
			std::shared_ptr<ISurface> CreateSurface(std::shared_ptr<Platform::IWindow> W, std::string& E) override
			{
				if (!State->Check(E))
				{
					return {};
				}
				auto Window = std::dynamic_pointer_cast<FWindow>(W);
				if (!Window || Window->GetState()->Runtime != State->Runtime || !Window->GetState()->Check(E))
				{
					E = "graphics.incompatible_window: expected a live window from this SDL runtime";
					return {};
				}
				auto S = Window->GetState();
				if (S->Claims || (S->OpenGlContextClaims && State->Driver != "opengles"))
				{
					E = "graphics.surface_in_use: window already has a surface";
					return {};
				}
				auto Lease = std::make_unique<FBackendSurfaceRetirement>();
				Lease->Runtime = State->Runtime;
				Lease->State = State;
				Lease->Native = State->Retirement->Native->CreateSurface(S->Retirement->Native, E);
				if (!Lease->Native)
				{
					return {};
				}
				Lease->Window = S;
				++S->Claims;
				++State->Resources;
				S->Claim = Lease.get();
				return std::make_shared<FBackendSurface>(std::move(Lease));
			}
			bool ClearTexture(const std::shared_ptr<ITexture>& T, const FColor& C, std::string& E) override
			{
				FRenderOptions2D O;
				O.ClearColor = C;
				return RendererClear(T, {}, O, nullptr, E);
			}
			bool Present(const std::shared_ptr<ISurface>& S, const FColor& C, bool& P, std::string& E) override
			{
				P = false;
				FRenderOptions2D O;
				O.ClearColor = C;
				return RendererClear({}, S, O, &P, E);
			}
			bool ReadTexture(const std::shared_ptr<ITexture>& T, std::vector<std::uint8_t>& R, std::string& E) override
			{
				if (!State->Check(E))
				{
					return false;
				}
				auto Resource = Texture(T, E);
				if (!Resource)
				{
					return false;
				}
				return State->Retirement->Native->ReadTexture(*Resource->Lease->Native, R, E);
			}
			bool WaitIdle(std::string& E) override
			{
				if (!State->Check(E))
				{
					return false;
				}
				State->Runtime->Drain();
				return State->Retirement->Native->WaitIdle(E);
			}
			bool Close(std::string& E) override
			{
				if (!State->Runtime->CheckThread(E))
				{
					return false;
				}
				State->Runtime->Drain();
				if (State->Resources)
				{
					E = "graphics.resources_alive: close graphics resources first";
					return false;
				}
				State->Retirement->Release();
				State->bOpen->store(false);
				E.clear();
				return true;
			}
			std::shared_ptr<ISampler> CreateSampler(const FSamplerOptions& O, std::string& E) override
			{
				if (!State->Check(E) || !ValidateSamplerOptions(O, E))
				{
					return {};
				}
				auto Native = State->Retirement->Native->CreateSampler(O, E);
				if (!Native)
				{
					return {};
				}
				return std::make_shared<FBackendSampler>(Retain(State, std::move(Native)), O);
			}
			bool UpdateTexture(const std::shared_ptr<ITexture>& T, std::int64_t X, std::int64_t Y, std::int64_t W,
			    std::int64_t H, const std::vector<std::uint8_t>& P, std::string& E) override
			{
				if (!State->Check(E))
				{
					return false;
				}
				auto R = Texture(T, E);
				if (!R)
				{
					return false;
				}
				if (X < 0 || Y < 0 || W <= 0 || H <= 0 || X > R->Options.Width || Y > R->Options.Height ||
				    W > R->Options.Width - X || H > R->Options.Height - Y ||
				    static_cast<std::uint64_t>(W) * H * 4 != P.size())
				{
					E = "graphics.invalid_upload: region or byte count is invalid";
					return false;
				}
				return State->Retirement->Native->UpdateTexture(*R->Lease->Native, {X, Y, W, H}, P, E);
			}
			std::shared_ptr<IRenderer2D> CreateRenderer2D(std::string& E) override
			{
				auto White = std::dynamic_pointer_cast<FBackendTexture>(CreateTexture({}, E));
				if (!White || !UpdateTexture(White, 0, 0, 1, 1, {255, 255, 255, 255}, E))
				{
					return {};
				}
				auto Sampler = std::dynamic_pointer_cast<FBackendSampler>(CreateSampler({}, E));
				if (!Sampler)
				{
					return {};
				}
				return std::make_shared<FBackendRenderer>(State, std::move(White), std::move(Sampler));
			}
			std::uint64_t GetGeneration() const noexcept override
			{
				return State->Generation;
			}
			bool Invalidate(std::string& E) override
			{
				if (!State->Runtime->CheckThread(E))
				{
					return false;
				}
				if (!State->Retirement->Native)
				{
					E = "graphics.closed_device: device is closed";
					return false;
				}
				State->bOpen->store(false);
				E.clear();
				return true;
			}
			std::shared_ptr<IReadbackRequest> ReadTextureAsync(const std::shared_ptr<ITexture>& T,
			    std::string& E) override
			{
				if (State->Driver != "webgpu")
				{
					return IDevice::ReadTextureAsync(T, E);
				}
				if (!State->Check(E))
				{
					return {};
				}
				auto R = Texture(T, E);
				if (!R)
				{
					return {};
				}
				return State->Retirement->Native->ReadTextureAsync(*R->Lease->Native, E);
			}
			bool Poll(std::string& E) override
			{
				return State->Check(E);
			}
			explicit FBackendDevice(std::shared_ptr<FBackendState> S) : State(std::move(S))
			{
			}

		private:
			std::shared_ptr<FBackendTexture> Texture(const std::shared_ptr<ITexture>& T, std::string& E)
			{
				auto R = std::dynamic_pointer_cast<FBackendTexture>(T);
				if (!R || !R->IsOpen() || R->Lease->State != State)
				{
					E = "graphics.invalid_texture: expected an open texture from this device";
					return {};
				}
				return R;
			}
			bool RendererClear(const std::shared_ptr<ITexture>& T, const std::shared_ptr<ISurface>& S,
			    const FRenderOptions2D& O, bool* P, std::string& E)
			{
				if (!State->Check(E) || !ValidateColor(O.ClearColor, E))
				{
					return false;
				}
				auto Texture = T ? this->Texture(T, E) : nullptr;
				auto Surface = S ? std::dynamic_pointer_cast<FBackendSurface>(S) : nullptr;
				std::int64_t Width = 0, Height = 0;
				if (T)
				{
					if (!Texture)
					{
						return false;
					}
					Width = Texture->Options.Width;
					Height = Texture->Options.Height;
				}
				else
				{
					if (!Surface || !Surface->IsOpen() || Surface->Lease->State != State)
					{
						E = "graphics.invalid_surface: expected an open surface from this device";
						return false;
					}
					int W = 0, H = 0;
					if (!SDL_GetWindowSizeInPixels(Surface->Lease->Window->Retirement->Native, &W, &H))
					{
						return SdlError("query surface size", E);
					}
					Width = W;
					Height = H;
					if (W <= 0 || H <= 0)
					{
						E.clear();
						return true;
					}
				}
				bool Presented = false;
				const bool Success = State->Retirement->Native->Render(Texture ? Texture->Lease->Native.get() : nullptr,
				    Surface ? Surface->Lease->Native.get() : nullptr,
				    O, {0, 0, Width, Height}, {}, Presented, E);
				if (P)
				{
					*P = Success && Presented;
				}
				return Success;
			}
			std::shared_ptr<FBackendState> State;
		};
	}
	FVertexUniform2D MakeVertexUniform(const FBackendDraw& Draw, const FRect& Viewport)
	{
		return {Draw.Command->Transform,
		    {static_cast<float>(Viewport.Width), static_cast<float>(Viewport.Height), 0, 0}};
	}
	FFragmentUniform2D MakeFragmentUniform(const FBackendDraw& Draw)
	{
		FFragmentUniform2D U{};
		const auto& D = *Draw.Command;
		const auto& E = D.Effect;
		U.Tint = D.Tint;
		U.EffectColor = E.Color;
		U.Options[0] = D.Blend == EBlendMode::PremultipliedAlpha || D.Blend == EBlendMode::Additive ? 1.0f : 0.0f;
		U.Options[1] = D.VertexColorMode == EVertexColorMode::Premultiplied ? 1.0f : 0.0f;
		U.Options[2] = static_cast<float>(E.Type);
		U.Options[3] = E.bSourcePremultiplied ? 1.0f : 0.0f;
		U.Filter[0] = E.bVertical ? 0 : E.Radius / (6 * static_cast<float>(Draw.Source.Width));
		U.Filter[1] = E.bVertical ? E.Radius / (6 * static_cast<float>(Draw.Source.Height)) : 0;
		U.Filter[2] = E.bInvert ? 1.0f : 0.0f;
		U.Filter[3] = E.Radius;
		for (unsigned Row = 0; Row < 4; ++Row)
		{
			for (unsigned Col = 0; Col < 4; ++Col)
			{
				U.Rows[Row][Col] = E.Matrix[Row * 5 + Col];
			}
			U.Bias[Row] = E.Matrix[Row * 5 + 4];
		}
		if (E.Type == EEffect2D::Shadow)
		{
			U.Rows[0][0] = E.OffsetX / static_cast<float>(Draw.Source.Width);
			U.Rows[0][1] = E.OffsetY / static_cast<float>(Draw.Source.Height);
		}
		return U;
	}
	std::shared_ptr<IDevice> CreateBackendDevice(const std::shared_ptr<FRuntime>& Runtime,
	    const FDeviceOptions& Options,
	    std::unique_ptr<IGraphicsBackend> Native, std::string& Error)
	{
		if (!Native)
		{
			return {};
		}
		auto State = std::make_shared<FBackendState>(Runtime, Options, std::move(Native));
		State->Retirement->Native->SetLossFlag(State->bOpen);
		State->Generation = Runtime->NextDeviceGeneration(Error);
		if (!State->Generation)
		{
			return {};
		}
		Error.clear();
		return std::make_shared<FBackendDevice>(std::move(State));
	}
	std::shared_ptr<IDevice> CreateAdditionalDevice(const std::shared_ptr<FRuntime>& Runtime,
	    const FDeviceOptions& Options, std::string& Error)
	{
		std::unique_ptr<IGraphicsBackend> Backend;
		if (Options.Driver == "direct3d11" || Options.Driver == "direct3d11-warp")
		{
			Backend = CreateD3d11Backend(Options, Error);
		}
		else if (Options.Driver == "opengl" || Options.Driver == "opengles")
		{
			Backend = CreateOpenGlBackend(Runtime, Options, Error);
		}
		else if (Options.Driver == "webgpu")
		{
			#if defined(__EMSCRIPTEN__)
			Error = "graphics.async_required: use CreateDeviceAsync and return to the browser event loop";
			return {};
			#else
			auto Request = CreateWebGpuDeviceAsync(Runtime, Options, Error);
			if (!Request)
			{
				return {};
			}
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
			while (Request->Poll(Error) == ERequestStatus::Pending)
			{
				if (std::chrono::steady_clock::now() >= Deadline)
				{
					Request->Cancel(Error);
					Error = "graphics.timeout: WebGPU device creation timed out";
					return {};
				}
				SDL_Delay(1);
			}
			return Request->GetDevice(Error);
			#endif
		}
		else
		{
			Error = "graphics.unknown_driver: unrecognized additional graphics driver";
			return {};
		}
		return CreateBackendDevice(Runtime, Options, std::move(Backend), Error);
	}
}

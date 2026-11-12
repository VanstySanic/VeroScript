// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3GraphicsBackend.h"

#if VEROSCRIPT_SDL3_WEBGPU
#include <webgpu/webgpu.h>
#if !defined(__EMSCRIPTEN__)
#include <webgpu/wgpu.h>
#endif
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif

namespace VeroScript::Sdl3
{
	using namespace Graphics;
	namespace
	{
		#include "../VeroScriptSdl3Shaders.inl"
		WGPUStringView Text(const char* Value)
		{
			return {Value, WGPU_STRLEN};
		}
		std::string Message(WGPUStringView Value)
		{
			return Value.data
			    ? std::string(Value.data, Value.length == WGPU_STRLEN ? std::strlen(Value.data) : Value.length)
			    : std::string{};
		}
		struct FWebGpuErrors
		{
			void Report(WGPUStringView V) noexcept
			{
				try
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					Error = "graphics.webgpu_failure: " + Message(V);
				}
				catch (...)
				{
					Lost.store(true);
				}
			}
			std::mutex Mutex;
			std::string Error;
			std::atomic<bool> Lost{false};
			std::weak_ptr<std::atomic<bool>> OpenFlag;
		};
		template <class T>
		struct FCallbackRetirement final : FRetirement
		{
			FCallbackRetirement(std::shared_ptr<T> S, const std::shared_ptr<FRuntime>& R) : State(std::move(S))
			{
				Runtime = R;
				++Runtime->PendingCallbacks;
			}
			~FCallbackRetirement() override
			{
				--Runtime->PendingCallbacks;
			}
			void Release() noexcept override
			{
				State.reset();
			}
			std::shared_ptr<T> State;
		};
		struct FWebGpuRetirement final : FRetirement
		{
			void Release() noexcept override
			{
				if (Device)
				{
					wgpuDeviceDestroy(Device);
					wgpuDeviceRelease(Device);
					Device = nullptr;
				}
				if (Queue)
				{
					wgpuQueueRelease(Queue);
					Queue = nullptr;
				}
				if (Adapter)
				{
					wgpuAdapterRelease(Adapter);
					Adapter = nullptr;
				}
				if (Instance)
				{
					wgpuInstanceRelease(Instance);
					Instance = nullptr;
				}
			}
			bool Check(std::string& E)
			{
				wgpuInstanceProcessEvents(Instance);
				#if !defined(__EMSCRIPTEN__)
				if (Device)
				{
					wgpuDevicePoll(Device, false, nullptr);
				}
				#endif
				std::lock_guard<std::mutex> Lock(Errors->Mutex);
				if (Errors->Lost.load())
				{
					E = "graphics.device_lost: WebGPU device was lost";
					return false;
				}
				if (!Errors->Error.empty())
				{
					E = std::move(Errors->Error);
					Errors->Error.clear();
					return false;
				}
				E.clear();
				return true;
			}
			WGPUInstance Instance = nullptr;
			WGPUAdapter Adapter = nullptr;
			std::atomic<WGPUDevice> Device{nullptr};
			WGPUQueue Queue = nullptr;
			WGPULimits Limits = WGPU_LIMITS_INIT;
			std::shared_ptr<FWebGpuErrors> Errors = std::make_shared<FWebGpuErrors>();
		};
		struct FContext final : INativePump
		{
			explicit FContext(const std::shared_ptr<FRuntime>& R) : Native(std::make_unique<FWebGpuRetirement>())
			{
				Native->Runtime = R;
			}
			~FContext()
			{
				Native->Runtime->Defer(std::move(Native));
			}
			void BeginShutdown() noexcept
			{
				if (Native->Device && !bShutdownRequested)
				{
					bShutdownRequested = true;
					wgpuDeviceDestroy(Native->Device);
				}
			}
			void PollNativeCallbacks() noexcept override
			{
				wgpuInstanceProcessEvents(Native->Instance);
				#if !defined(__EMSCRIPTEN__)
				if (Native->Device)
				{
					wgpuDevicePoll(Native->Device, false, nullptr);
				}
				#endif
			}
			std::unique_ptr<FWebGpuRetirement> Native;
			FDeviceOptions Options;
			bool bShutdownRequested = false;
		};
		struct FTexture final : ITextureData
		{
			~FTexture() override
			{
				if (View)
				{
					wgpuTextureViewRelease(View);
				}
				if (Texture)
				{
					wgpuTextureRelease(Texture);
				}
			}
			WGPUTexture Texture = nullptr;
			WGPUTextureView View = nullptr;
			FTextureOptions Options;
		};
		struct FSampler final : ISamplerData
		{
			~FSampler() override
			{
				if (Sampler)
				{
					wgpuSamplerRelease(Sampler);
				}
			}
			WGPUSampler Sampler = nullptr;
		};
		struct FGeometry final : IGeometryData
		{
			~FGeometry() override
			{
				if (Vertices)
				{
					wgpuBufferRelease(Vertices);
				}
				if (Indices)
				{
					wgpuBufferRelease(Indices);
				}
			}
			WGPUBuffer Vertices = nullptr, Indices = nullptr;
			std::uint32_t Count = 0;
		};
		struct FSurface final : ISurfaceData
		{
			~FSurface() override
			{
				if (Surface)
				{
					if (Width)
					{
						wgpuSurfaceUnconfigure(Surface);
					}
					wgpuSurfaceRelease(Surface);
				}
				#if defined(__APPLE__)
				if (MetalView)
				{
					SDL_Metal_DestroyView(MetalView);
				}
				#endif
			}
			WGPUSurface Surface = nullptr;
			SDL_Window* Window = nullptr;
			WGPUTextureFormat Format = WGPUTextureFormat_Undefined;
			std::uint32_t Width = 0, Height = 0;
			#if defined(__APPLE__)
			SDL_MetalView MetalView = nullptr;
			#endif
		};
		WGPUTextureFormat Format(ETextureFormat V)
		{
			return V == ETextureFormat::Rgba8Srgb ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
		}
		struct FReadbackRetirement final : FRetirement
		{
			void Release() noexcept override
			{
				if (Buffer)
				{
					wgpuBufferRelease(Buffer);
					Buffer = nullptr;
				}
				Context.reset();
			}
			std::shared_ptr<FContext> Context;
			WGPUBuffer Buffer = nullptr;
		};
		struct FReadbackState
		{
			~FReadbackState()
			{
				if (Lease)
				{
					Lease->Runtime->Defer(std::move(Lease));
				}
			}
			std::unique_ptr<FReadbackRetirement> Lease;
			std::atomic<int> Mapped{0};
			std::atomic<bool> bMappingActive{true};
			std::string Error;
		};
		class FReadback final : public IReadbackRequest
		{
		public:
			ERequestStatus Poll(std::string& E) override
			{
				if (!State->Lease->Runtime->CheckThread(E))
				{
					return ERequestStatus::Failed;
				}
				State->Lease->Runtime->Drain();
				if (Status == ERequestStatus::Pending)
				{
					if (!State->Lease->Context->Native->Check(E))
					{
						Error = E;
						Status = ERequestStatus::Failed;
					}
					else if (State->Mapped.load() == 1)
					{
						const auto Bytes = static_cast<std::size_t>(Pitch) * Height;
						const auto* Data = static_cast<const std::uint8_t*>(
						    wgpuBufferGetConstMappedRange(State->Lease->Buffer, 0, Bytes));
						if (!Data)
						{
							Error = "graphics.webgpu_failure: mapped readback is unavailable";
							Status = ERequestStatus::Failed;
						}
						else
						{
							Pixels.resize(static_cast<std::size_t>(Width) * Height * 4);
							for (std::uint32_t Y = 0; Y < Height; ++Y)
							{
								std::memcpy(Pixels.data() + static_cast<std::size_t>(Y) * Width * 4,
								    Data + static_cast<std::size_t>(Y) * Pitch,
								    static_cast<std::size_t>(Width) * 4);
							}
							State->bMappingActive.store(false);
							wgpuBufferUnmap(State->Lease->Buffer);
							Status = ERequestStatus::Succeeded;
						}
					}
					else if (State->Mapped.load() == -1)
					{
						Error = State->Error;
						Status = ERequestStatus::Failed;
					}
				}
				E = Error;
				return Status;
			}
			bool GetPixels(std::vector<std::uint8_t>& Out, std::string& E) const override
			{
				if (!State->Lease->Runtime->CheckThread(E))
				{
					return false;
				}
				if (Status != ERequestStatus::Succeeded)
				{
					E = Error.empty() ? "graphics.request_not_ready: poll readback to completion first" : Error;
					return false;
				}
				Out = Pixels;
				E.clear();
				return true;
			}
			bool Cancel(std::string& E) override
			{
				if (!State->Lease->Runtime->CheckThread(E))
				{
					return false;
				}
				// Track our map ownership: the pinned native library does not implement GetMapState.
				if (State->bMappingActive.exchange(false) && !State->Lease->Context->bShutdownRequested)
				{
					wgpuBufferUnmap(State->Lease->Buffer);
				}
				Status = ERequestStatus::Cancelled;
				Pixels.clear();
				Error.clear();
				E.clear();
				return true;
			}
			FReadback(std::shared_ptr<FReadbackState> S, std::uint32_t W, std::uint32_t H, std::uint32_t P)
			    : State(std::move(S)), Width(W), Height(H), Pitch(P)
			{
			}

		private:
			std::shared_ptr<FReadbackState> State;
			std::vector<std::uint8_t> Pixels;
			std::uint32_t Width, Height, Pitch;
			ERequestStatus Status = ERequestStatus::Pending;
			std::string Error;
		};
		class FWebGpu final : public IGraphicsBackend
		{
		public:
			~FWebGpu() override
			{
				for (auto& P : Pipelines)
				{
					wgpuRenderPipelineRelease(P.second);
				}
				if (Layout)
				{
					wgpuPipelineLayoutRelease(Layout);
				}
				if (Bindings)
				{
					wgpuBindGroupLayoutRelease(Bindings);
				}
				if (Shader)
				{
					wgpuShaderModuleRelease(Shader);
				}
				Context->BeginShutdown();
			}
			const char* GetDriver() const noexcept override
			{
				return "webgpu";
			}
			std::unique_ptr<ITextureData> CreateTexture(const FTextureOptions& O, std::string& E) override
			{
				if (O.Width > Context->Native->Limits.maxTextureDimension2D ||
				    O.Height > Context->Native->Limits.maxTextureDimension2D)
				{
					E = "graphics.resource_limit: texture exceeds WebGPU dimensions";
					return {};
				}
				auto T = std::make_unique<FTexture>();
				T->Options = O;
				WGPUTextureDescriptor D = WGPU_TEXTURE_DESCRIPTOR_INIT;
				D.size = {static_cast<std::uint32_t>(O.Width), static_cast<std::uint32_t>(O.Height), 1};
				D.format = Format(O.Format);
				D.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
				    WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
				T->Texture = wgpuDeviceCreateTexture(Context->Native->Device, &D);
				if (!T->Texture)
				{
					NativeFailure("create texture", E);
					return {};
				}
				T->View = wgpuTextureCreateView(T->Texture, nullptr);
				if (!T->View)
				{
					NativeFailure("create texture view", E);
					return {};
				}
				if (!Poll(E))
				{
					return {};
				}
				return T;
			}
			std::unique_ptr<ISamplerData> CreateSampler(const FSamplerOptions& O, std::string& E) override
			{
				auto S = std::make_unique<FSampler>();
				WGPUSamplerDescriptor D = WGPU_SAMPLER_DESCRIPTOR_INIT;
				D.minFilter = D.magFilter =
				    O.Filter == EFilter::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
				const auto Address = [](EAddressMode A)
				{
					return A == EAddressMode::Repeat ? WGPUAddressMode_Repeat
					    : A == EAddressMode::Mirror  ? WGPUAddressMode_MirrorRepeat
					                                 : WGPUAddressMode_ClampToEdge;
				};
				D.addressModeU = Address(O.AddressU);
				D.addressModeV = Address(O.AddressV);
				S->Sampler = wgpuDeviceCreateSampler(Context->Native->Device, &D);
				if (!S->Sampler)
				{
					NativeFailure("create sampler", E);
					return {};
				}
				if (!Poll(E))
				{
					return {};
				}
				return S;
			}
			std::unique_ptr<IGeometryData> CreateGeometry(const std::vector<FVertex2D>& V,
			    const std::vector<std::uint32_t>& I, std::string& E) override
			{
				auto G = std::make_unique<FGeometry>();
				G->Count = static_cast<std::uint32_t>(I.size());
				G->Vertices = Buffer(V.data(), V.size() * sizeof(FVertex2D), WGPUBufferUsage_Vertex, E);
				if (!G->Vertices)
				{
					return {};
				}
				G->Indices = Buffer(I.data(), I.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index, E);
				if (!G->Indices)
				{
					return {};
				}
				if (!Poll(E))
				{
					return {};
				}
				return G;
			}
			std::unique_ptr<ISurfaceData> CreateSurface(SDL_Window* W, std::string& E) override;
			bool UpdateTexture(ITextureData& T, const FRect& R, const std::vector<std::uint8_t>& P,
			    std::string& E) override
			{
				WGPUTexelCopyTextureInfo Destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
				Destination.texture = static_cast<FTexture&>(T).Texture;
				Destination.origin = {static_cast<std::uint32_t>(R.X), static_cast<std::uint32_t>(R.Y), 0};
				WGPUTexelCopyBufferLayout TransferLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
				TransferLayout.bytesPerRow = static_cast<std::uint32_t>(R.Width) * 4;
				TransferLayout.rowsPerImage = static_cast<std::uint32_t>(R.Height);
				WGPUExtent3D Size{static_cast<std::uint32_t>(R.Width), static_cast<std::uint32_t>(R.Height), 1};
				wgpuQueueWriteTexture(Context->Native->Queue, &Destination, P.data(), P.size(), &TransferLayout, &Size);
				return Poll(E);
			}
			bool ReadTexture(ITextureData& T, std::vector<std::uint8_t>& Out, std::string& E) override
			{
				#if defined(__EMSCRIPTEN__)
				(void)T;
				(void)Out;
				E = "graphics.async_required: use ReadTextureAsync in a browser";
				return false;
				#else
				auto Request = ReadTextureAsync(T, E);
				if (!Request)
				{
					return false;
				}
				const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
				while (Request->Poll(E) == ERequestStatus::Pending)
				{
					if (std::chrono::steady_clock::now() >= Deadline)
					{
						Request->Cancel(E);
						E = "graphics.timeout: WebGPU readback timed out";
						return false;
					}
					SDL_Delay(1);
				}
				return Request->GetPixels(Out, E);
				#endif
			}
			bool Render(ITextureData*, ISurfaceData*, const FRenderOptions2D&, const FRect&,
			    const std::vector<FBackendDraw>&, bool&, std::string&) override;
			bool WaitIdle(std::string& E) override
			{
				#if defined(__EMSCRIPTEN__)
				E = "graphics.async_required: browser submissions complete through the host event loop";
				return false;
				#else
				wgpuDevicePoll(Context->Native->Device, true, nullptr);
				return Poll(E);
				#endif
			}
			bool Poll(std::string& E) override
			{
				return Context->Native->Check(E);
			}
			bool IsLost() const noexcept override
			{
				return Context->Native->Errors->Lost.load();
			}
			void SetLossFlag(std::weak_ptr<std::atomic<bool>> Flag) override
			{
				std::lock_guard<std::mutex> Lock(Context->Native->Errors->Mutex);
				Context->Native->Errors->OpenFlag = std::move(Flag);
				if (IsLost())
				{
					if (const auto Open = Context->Native->Errors->OpenFlag.lock())
					{
						Open->store(false);
					}
				}
			}
			std::shared_ptr<IReadbackRequest> ReadTextureAsync(ITextureData&, std::string&) override;
			explicit FWebGpu(std::shared_ptr<FContext> C) : Context(std::move(C))
			{
			}
			bool Initialize(std::string& E);

		private:
			bool NativeFailure(const char* Operation, std::string& E)
			{
				if (Poll(E))
				{
					E = std::string("graphics.webgpu_failure: ") + Operation;
				}
				return false;
			}
			WGPUBuffer Buffer(const void* Data, std::size_t Size, WGPUBufferUsage Usage, std::string& E)
			{
				if (Size > Context->Native->Limits.maxBufferSize)
				{
					E = "graphics.resource_limit: buffer exceeds WebGPU limits";
					return nullptr;
				}
				WGPUBufferDescriptor D = WGPU_BUFFER_DESCRIPTOR_INIT;
				D.size = Size;
				D.usage = Usage;
				D.mappedAtCreation = true;
				auto B = wgpuDeviceCreateBuffer(Context->Native->Device, &D);
				if (!B)
				{
					NativeFailure("create buffer", E);
					return nullptr;
				}
				auto* M = wgpuBufferGetMappedRange(B, 0, Size);
				if (!M)
				{
					wgpuBufferRelease(B);
					NativeFailure("map initial buffer", E);
					return nullptr;
				}
				if (Data)
				{
					std::memcpy(M, Data, Size);
				}
				wgpuBufferUnmap(B);
				return B;
			}
			WGPURenderPipeline Pipeline(WGPUTextureFormat, EBlendMode, std::string&);
			std::shared_ptr<FContext> Context;
			WGPUShaderModule Shader = nullptr;
			WGPUBindGroupLayout Bindings = nullptr;
			WGPUPipelineLayout Layout = nullptr;
			std::map<std::pair<WGPUTextureFormat, EBlendMode>, WGPURenderPipeline> Pipelines;
		};
		bool FWebGpu::Initialize(std::string& E)
		{
			WGPUShaderSourceWGSL Source = WGPU_SHADER_SOURCE_WGSL_INIT;
			Source.code = Text(WebGpuSource);
			WGPUShaderModuleDescriptor D = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
			D.nextInChain = &Source.chain;
			Shader = wgpuDeviceCreateShaderModule(Context->Native->Device, &D);
			if (!Shader)
			{
				return NativeFailure("create shader module", E);
			}
			std::array<WGPUBindGroupLayoutEntry, 6> Entries{};
			for (unsigned I = 0; I < 6; ++I)
			{
				Entries[I] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
				Entries[I].binding = I;
				Entries[I].visibility = I == 0 ? WGPUShaderStage_Vertex : WGPUShaderStage_Fragment;
			}
			for (unsigned I = 0; I < 2; ++I)
			{
				Entries[I].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
				Entries[I].buffer.type = WGPUBufferBindingType_Uniform;
				Entries[I].buffer.hasDynamicOffset = true;
				Entries[I].buffer.minBindingSize = I == 0 ? sizeof(FVertexUniform2D) : sizeof(FFragmentUniform2D);
			}
			for (unsigned I : {2u, 4u})
			{
				Entries[I].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
				Entries[I].texture.sampleType = WGPUTextureSampleType_Float;
				Entries[I].texture.viewDimension = WGPUTextureViewDimension_2D;
			}
			for (unsigned I : {3u, 5u})
			{
				Entries[I].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
				Entries[I].sampler.type = WGPUSamplerBindingType_Filtering;
			}
			WGPUBindGroupLayoutDescriptor B = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
			B.entryCount = Entries.size();
			B.entries = Entries.data();
			Bindings = wgpuDeviceCreateBindGroupLayout(Context->Native->Device, &B);
			if (!Bindings)
			{
				return NativeFailure("create bind group layout", E);
			}
			WGPUPipelineLayoutDescriptor P = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
			P.bindGroupLayoutCount = 1;
			P.bindGroupLayouts = &Bindings;
			Layout = wgpuDeviceCreatePipelineLayout(Context->Native->Device, &P);
			if (!Layout)
			{
				return NativeFailure("create pipeline layout", E);
			}
			return Poll(E);
		}
		WGPURenderPipeline FWebGpu::Pipeline(WGPUTextureFormat Target, EBlendMode Blend, std::string& E)
		{
			const auto Key = std::make_pair(Target, Blend);
			const auto Found = Pipelines.find(Key);
			if (Found != Pipelines.end())
			{
				return Found->second;
			}
			WGPUVertexAttribute A[3]{WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT,
			    WGPU_VERTEX_ATTRIBUTE_INIT};
			for (unsigned I = 0; I < 3; ++I)
			{
				A[I].shaderLocation = I;
				A[I].offset = I == 0 ? 0 : I == 1 ? 8
				                                  : 16;
				A[I].format = I == 2 ? WGPUVertexFormat_Float32x4 : WGPUVertexFormat_Float32x2;
			}
			WGPUVertexBufferLayout V = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
			V.arrayStride = sizeof(FVertex2D);
			V.attributeCount = 3;
			V.attributes = A;
			WGPUBlendState B = WGPU_BLEND_STATE_INIT;
			B.color.srcFactor = Blend == EBlendMode::StraightAlpha ? WGPUBlendFactor_SrcAlpha : WGPUBlendFactor_One;
			B.color.dstFactor = Blend == EBlendMode::Additive ? WGPUBlendFactor_One : WGPUBlendFactor_OneMinusSrcAlpha;
			B.alpha.srcFactor = WGPUBlendFactor_One;
			B.alpha.dstFactor = B.color.dstFactor;
			WGPUColorTargetState C = WGPU_COLOR_TARGET_STATE_INIT;
			C.format = Target;
			C.blend = Blend == EBlendMode::Opaque ? nullptr : &B;
			WGPUFragmentState F = WGPU_FRAGMENT_STATE_INIT;
			F.module = Shader;
			F.entryPoint = Text("fragment_main");
			F.targetCount = 1;
			F.targets = &C;
			WGPURenderPipelineDescriptor D = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
			D.layout = Layout;
			D.vertex.module = Shader;
			D.vertex.entryPoint = Text("vertex_main");
			D.vertex.bufferCount = 1;
			D.vertex.buffers = &V;
			D.fragment = &F;
			auto Result = wgpuDeviceCreateRenderPipeline(Context->Native->Device, &D);
			if (!Result)
			{
				NativeFailure("create render pipeline", E);
				return nullptr;
			}
			if (!Poll(E))
			{
				wgpuRenderPipelineRelease(Result);
				return nullptr;
			}
			try
			{
				Pipelines.emplace(Key, Result);
			}
			catch (...)
			{
				wgpuRenderPipelineRelease(Result);
				throw;
			}
			return Result;
		}
		std::unique_ptr<ISurfaceData> FWebGpu::CreateSurface(SDL_Window* Window, std::string& E)
		{
			auto S = std::make_unique<FSurface>();
			S->Window = Window;
			const auto Props = SDL_GetWindowProperties(Window);
			WGPUSurfaceDescriptor D = WGPU_SURFACE_DESCRIPTOR_INIT;
			#if defined(__EMSCRIPTEN__)
			const std::string Selector =
			    SDL_GetStringProperty(Props, SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "#canvas");
			WGPUEmscriptenSurfaceSourceCanvasHTMLSelector Native =
			    WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
			Native.selector = Text(Selector.c_str());
			D.nextInChain = &Native.chain;
			#elif defined(_WIN32)
			WGPUSurfaceSourceWindowsHWND Native = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
			Native.hwnd = SDL_GetPointerProperty(Props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
			Native.hinstance = SDL_GetPointerProperty(Props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
			if (!Native.hwnd)
			{
				E = "graphics.incompatible_window: expected a Win32 surface";
				return {};
			}
			D.nextInChain = &Native.chain;
			#elif defined(__ANDROID__)
			WGPUSurfaceSourceAndroidNativeWindow Native = WGPU_SURFACE_SOURCE_ANDROID_NATIVE_WINDOW_INIT;
			Native.window = SDL_GetPointerProperty(Props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
			D.nextInChain = &Native.chain;
			#elif defined(__APPLE__)
			S->MetalView = SDL_Metal_CreateView(Window);
			if (!S->MetalView)
			{
				SdlError("create WebGPU Metal view", E);
				return {};
			}
			WGPUSurfaceSourceMetalLayer Native = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
			Native.layer = SDL_Metal_GetLayer(S->MetalView);
			D.nextInChain = &Native.chain;
			#elif defined(__linux__)
			WGPUSurfaceSourceXlibWindow X11 = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
			WGPUSurfaceSourceWaylandSurface Wayland = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
			X11.display = SDL_GetPointerProperty(Props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
			X11.window = static_cast<std::uint64_t>(SDL_GetNumberProperty(Props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
			Wayland.display = SDL_GetPointerProperty(Props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
			Wayland.surface = SDL_GetPointerProperty(Props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
			if (Wayland.display && Wayland.surface)
			{
				D.nextInChain = &Wayland.chain;
			}
			else if (X11.display && X11.window)
			{
				D.nextInChain = &X11.chain;
			}
			else
			{
				E = "graphics.incompatible_window: expected an X11 or Wayland surface";
				return {};
			}
			#else
			(void)Props;
			E = "graphics.unsupported_surface: WebGPU native window bridge is unavailable on this platform";
			return {};
			#endif
			S->Surface = wgpuInstanceCreateSurface(Context->Native->Instance, &D);
			if (!S->Surface)
			{
				NativeFailure("create surface", E);
				return {};
			}
			if (!Poll(E))
			{
				return {};
			}
			WGPUSurfaceCapabilities C = WGPU_SURFACE_CAPABILITIES_INIT;
			if (wgpuSurfaceGetCapabilities(S->Surface, Context->Native->Adapter, &C) != WGPUStatus_Success)
			{
				E = "graphics.unsupported_surface: adapter cannot present this window";
				return {};
			}
			for (std::size_t I = 0; I < C.formatCount; ++I)
			{
				const auto F = C.formats[I];
				if (F == WGPUTextureFormat_BGRA8UnormSrgb || F == WGPUTextureFormat_RGBA8UnormSrgb)
				{
					S->Format = F;
					break;
				}
				if (S->Format == WGPUTextureFormat_Undefined &&
				    (F == WGPUTextureFormat_BGRA8Unorm || F == WGPUTextureFormat_RGBA8Unorm))
				{
					S->Format = F;
				}
			}
			wgpuSurfaceCapabilitiesFreeMembers(C);
			if (S->Format == WGPUTextureFormat_Undefined)
			{
				E = "graphics.unsupported_surface: no compatible RGBA8 format";
				return {};
			}
			return S;
		}
		bool FWebGpu::Render(ITextureData* Target, ISurfaceData* Surface, const FRenderOptions2D& O, const FRect& V,
		    const std::vector<FBackendDraw>& Draws, bool& Presented, std::string& E)
		{
			struct FFrame
			{
				~FFrame()
				{
					if (Pass)
					{
						wgpuRenderPassEncoderEnd(Pass);
						wgpuRenderPassEncoderRelease(Pass);
					}
					if (Commands)
					{
						wgpuCommandBufferRelease(Commands);
					}
					if (Encoder)
					{
						wgpuCommandEncoderRelease(Encoder);
					}
					for (auto B : Groups)
					{
						wgpuBindGroupRelease(B);
					}
					if (Uniforms)
					{
						wgpuBufferRelease(Uniforms);
					}
					if (View)
					{
						wgpuTextureViewRelease(View);
					}
					if (Image)
					{
						wgpuTextureRelease(Image);
					}
				}
				WGPUCommandEncoder Encoder = nullptr;
				WGPUCommandBuffer Commands = nullptr;
				WGPURenderPassEncoder Pass = nullptr;
				WGPUBuffer Uniforms = nullptr;
				WGPUTexture Image = nullptr;
				WGPUTextureView View = nullptr;
				std::vector<WGPUBindGroup> Groups;
			} Frame;
			Presented = false;
			auto* S = Surface ? &static_cast<FSurface&>(*Surface) : nullptr;
			const auto BaseFormat = S ? S->Format : Format(static_cast<FTexture*>(Target)->Options.Format);
			const auto RenderFormat = BaseFormat == WGPUTextureFormat_BGRA8Unorm ? WGPUTextureFormat_BGRA8UnormSrgb
			    : S && BaseFormat == WGPUTextureFormat_RGBA8Unorm
			    ? WGPUTextureFormat_RGBA8UnormSrgb
			    : BaseFormat;
			std::vector<WGPURenderPipeline> Pipes;
			Pipes.reserve(Draws.size());
			for (const auto& D : Draws)
			{
				auto P = Pipeline(RenderFormat, D.Command->Blend, E);
				if (!P)
				{
					return false;
				}
				Pipes.push_back(P);
			}
			const auto Alignment = Context->Native->Limits.minUniformBufferOffsetAlignment;
			const std::size_t Stride = ((sizeof(FFragmentUniform2D) + Alignment - 1) / Alignment) * Alignment;
			if (!Draws.empty())
			{
				std::vector<std::uint8_t> Data(Draws.size() * Stride * 2);
				for (std::size_t I = 0; I < Draws.size(); ++I)
				{
					const auto U = MakeVertexUniform(Draws[I], V);
					const auto F = MakeFragmentUniform(Draws[I]);
					std::memcpy(Data.data() + I * Stride * 2, &U, sizeof(U));
					std::memcpy(Data.data() + I * Stride * 2 + Stride, &F, sizeof(F));
				}
				Frame.Uniforms = Buffer(Data.data(), Data.size(), WGPUBufferUsage_Uniform, E);
				if (!Frame.Uniforms)
				{
					return false;
				}
				Frame.Groups.reserve(Draws.size());
				for (const auto& D : Draws)
				{
					WGPUBindGroupEntry B[6]{};
					for (unsigned I = 0; I < 6; ++I)
					{
						B[I] = WGPU_BIND_GROUP_ENTRY_INIT;
						B[I].binding = I;
					}
					B[0].buffer = B[1].buffer = Frame.Uniforms;
					B[0].size = sizeof(FVertexUniform2D);
					B[1].size = sizeof(FFragmentUniform2D);
					B[2].textureView = static_cast<FTexture*>(D.Texture)->View;
					B[4].textureView = static_cast<FTexture*>(D.Mask)->View;
					B[3].sampler = B[5].sampler = static_cast<FSampler*>(D.Sampler)->Sampler;
					WGPUBindGroupDescriptor G = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
					G.layout = Bindings;
					G.entryCount = 6;
					G.entries = B;
					auto Group = wgpuDeviceCreateBindGroup(Context->Native->Device, &G);
					if (!Group)
					{
						return NativeFailure("create bind group", E);
					}
					if (!Poll(E))
					{
						wgpuBindGroupRelease(Group);
						return false;
					}
					Frame.Groups.push_back(Group);
				}
			}
			WGPUTextureView View = nullptr;
			if (S)
			{
				int W = 0, H = 0;
				if (!SDL_GetWindowSizeInPixels(S->Window, &W, &H))
				{
					return SdlError("query WebGPU surface", E);
				}
				if (W <= 0 || H <= 0 || (SDL_GetWindowFlags(S->Window) & SDL_WINDOW_MINIMIZED))
				{
					E.clear();
					return true;
				}
				if (S->Width != static_cast<std::uint32_t>(W) || S->Height != static_cast<std::uint32_t>(H))
				{
					WGPUSurfaceConfiguration C = WGPU_SURFACE_CONFIGURATION_INIT;
					C.device = Context->Native->Device;
					C.format = BaseFormat;
					C.width = static_cast<std::uint32_t>(W);
					C.height = static_cast<std::uint32_t>(H);
					if (RenderFormat != BaseFormat)
					{
						C.viewFormatCount = 1;
						C.viewFormats = &RenderFormat;
					}
					wgpuSurfaceConfigure(S->Surface, &C);
					if (!Poll(E))
					{
						return false;
					}
					S->Width = C.width;
					S->Height = C.height;
				}
				WGPUSurfaceTexture Image = WGPU_SURFACE_TEXTURE_INIT;
				wgpuSurfaceGetCurrentTexture(S->Surface, &Image);
				Frame.Image = Image.texture;
				if (Image.status == WGPUSurfaceGetCurrentTextureStatus_Timeout ||
				    Image.status == WGPUSurfaceGetCurrentTextureStatus_Outdated)
				{
					if (Image.status == WGPUSurfaceGetCurrentTextureStatus_Outdated)
					{
						S->Width = 0;
					}
					E.clear();
					return true;
				}
				if (!Image.texture || (Image.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && Image.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal))
				{
					E = "graphics.surface_lost: recreate the WebGPU surface or device";
					return false;
				}
				if (Image.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
				{
					S->Width = 0;
				}
				WGPUTextureViewDescriptor D = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
				D.format = RenderFormat;
				Frame.View = wgpuTextureCreateView(Image.texture, &D);
				View = Frame.View;
			}
			else
			{
				View = static_cast<FTexture*>(Target)->View;
			}
			if (!View)
			{
				return NativeFailure("create target view", E);
			}
			Frame.Encoder = wgpuDeviceCreateCommandEncoder(Context->Native->Device, nullptr);
			if (!Frame.Encoder)
			{
				return NativeFailure("create render command encoder", E);
			}
			WGPURenderPassColorAttachment Color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
			Color.view = View;
			Color.loadOp = O.bClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
			Color.storeOp = WGPUStoreOp_Store;
			Color.clearValue = {O.ClearColor.R, O.ClearColor.G, O.ClearColor.B, O.ClearColor.A};
			WGPURenderPassDescriptor D = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
			D.colorAttachmentCount = 1;
			D.colorAttachments = &Color;
			Frame.Pass = wgpuCommandEncoderBeginRenderPass(Frame.Encoder, &D);
			if (!Frame.Pass)
			{
				return NativeFailure("begin render pass", E);
			}
			wgpuRenderPassEncoderSetViewport(Frame.Pass, static_cast<float>(V.X), static_cast<float>(V.Y),
			    static_cast<float>(V.Width), static_cast<float>(V.Height), 0, 1);
			for (std::size_t I = 0; I < Draws.size(); ++I)
			{
				const auto& Draw = Draws[I];
				const auto& G = static_cast<FGeometry&>(*Draw.Geometry);
				const std::uint32_t Offsets[]{static_cast<std::uint32_t>(I * Stride * 2),
				    static_cast<std::uint32_t>(I * Stride * 2 + Stride)};
				wgpuRenderPassEncoderSetPipeline(Frame.Pass, Pipes[I]);
				wgpuRenderPassEncoderSetBindGroup(Frame.Pass, 0, Frame.Groups[I], 2, Offsets);
				wgpuRenderPassEncoderSetScissorRect(
				    Frame.Pass, static_cast<std::uint32_t>(Draw.Clip.X), static_cast<std::uint32_t>(Draw.Clip.Y),
				    static_cast<std::uint32_t>(Draw.Clip.Width), static_cast<std::uint32_t>(Draw.Clip.Height));
				wgpuRenderPassEncoderSetVertexBuffer(Frame.Pass, 0, G.Vertices, 0, WGPU_WHOLE_SIZE);
				wgpuRenderPassEncoderSetIndexBuffer(Frame.Pass, G.Indices, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
				wgpuRenderPassEncoderDrawIndexed(Frame.Pass, G.Count, 1, 0, 0, 0);
			}
			wgpuRenderPassEncoderEnd(Frame.Pass);
			wgpuRenderPassEncoderRelease(Frame.Pass);
			Frame.Pass = nullptr;
			Frame.Commands = wgpuCommandEncoderFinish(Frame.Encoder, nullptr);
			if (!Frame.Commands)
			{
				return NativeFailure("finish render command encoder", E);
			}
			if (!Poll(E))
			{
				return false;
			}
			wgpuQueueSubmit(Context->Native->Queue, 1, &Frame.Commands);
			#if !defined(__EMSCRIPTEN__)
			if (S && wgpuSurfacePresent(S->Surface) != WGPUStatus_Success)
			{
				E = "graphics.webgpu_failure: present surface";
				return false;
			}
			#endif
			if (!Poll(E))
			{
				return false;
			}
			Presented = S != nullptr;
			return true;
		}
		std::shared_ptr<IReadbackRequest> FWebGpu::ReadTextureAsync(ITextureData& T, std::string& E)
		{
			auto& Source = static_cast<FTexture&>(T);
			const auto Width = static_cast<std::uint32_t>(Source.Options.Width),
			           Height = static_cast<std::uint32_t>(Source.Options.Height);
			const auto Pitch = (static_cast<std::uint64_t>(Width) * 4 + 255) & ~std::uint64_t{255};
			const auto Bytes = Pitch * Height;
			if (Bytes > Context->Options.MaxTextureBytes || Bytes > Context->Native->Limits.maxBufferSize)
			{
				E = "graphics.resource_limit: aligned WebGPU readback exceeds byte limits";
				return {};
			}
			auto State = std::make_shared<FReadbackState>();
			State->Lease = std::make_unique<FReadbackRetirement>();
			State->Lease->Runtime = Context->Native->Runtime;
			State->Lease->Context = Context;
			WGPUBufferDescriptor B = WGPU_BUFFER_DESCRIPTOR_INIT;
			B.size = Bytes;
			B.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
			State->Lease->Buffer = wgpuDeviceCreateBuffer(Context->Native->Device, &B);
			if (!State->Lease->Buffer)
			{
				NativeFailure("create readback buffer", E);
				return {};
			}
			if (!Poll(E))
			{
				return {};
			}
			auto Encoder = wgpuDeviceCreateCommandEncoder(Context->Native->Device, nullptr);
			if (!Encoder)
			{
				NativeFailure("create readback command encoder", E);
				return {};
			}
			WGPUTexelCopyTextureInfo From = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
			From.texture = Source.Texture;
			WGPUTexelCopyBufferInfo To = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
			To.buffer = State->Lease->Buffer;
			To.layout.bytesPerRow = static_cast<std::uint32_t>(Pitch);
			To.layout.rowsPerImage = Height;
			WGPUExtent3D Size{Width, Height, 1};
			wgpuCommandEncoderCopyTextureToBuffer(Encoder, &From, &To, &Size);
			auto Commands = wgpuCommandEncoderFinish(Encoder, nullptr);
			wgpuCommandEncoderRelease(Encoder);
			if (!Commands)
			{
				NativeFailure("finish readback command encoder", E);
				return {};
			}
			if (!Poll(E))
			{
				wgpuCommandBufferRelease(Commands);
				return {};
			}
			wgpuQueueSubmit(Context->Native->Queue, 1, &Commands);
			wgpuCommandBufferRelease(Commands);
			WGPUBufferMapCallbackInfo Callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
			Callback.mode = WGPUCallbackMode_AllowProcessEvents;
			Callback.userdata1 = new FCallbackRetirement<FReadbackState>(State, Context->Native->Runtime);
			Callback.callback = [](WGPUMapAsyncStatus S, WGPUStringView V, void* P, void*) noexcept
			{
				std::unique_ptr<FCallbackRetirement<FReadbackState>> Owner(
				    static_cast<FCallbackRetirement<FReadbackState>*>(P));
				auto& R = *Owner->State;
				try
				{
					if (S != WGPUMapAsyncStatus_Success)
					{
						R.Error = "graphics.webgpu_readback: " + Message(V);
					}
				}
				catch (...)
				{
					R.Lease->Context->Native->Errors->Lost.store(true);
				}
				R.Mapped.store(S == WGPUMapAsyncStatus_Success ? 1 : -1);
				if (S != WGPUMapAsyncStatus_Success)
				{
					R.bMappingActive.store(false);
				}
				auto Runtime = Owner->Runtime;
				Runtime->Defer(std::move(Owner));
			};
			wgpuBufferMapAsync(State->Lease->Buffer, WGPUMapMode_Read, 0, Bytes, Callback);
			E.clear();
			return std::make_shared<FReadback>(std::move(State), Width, Height, static_cast<std::uint32_t>(Pitch));
		}
		struct FCreationRetirement final : FRetirement
		{
			void Release() noexcept override
			{
				if (!bTransferred)
				{
					Context->BeginShutdown();
				}
				Context.reset();
			}
			std::shared_ptr<FContext> Context;
			bool bTransferred = false;
		};
		struct FCreation
		{
			~FCreation()
			{
				if (Lease)
				{
					Lease->Runtime->Retire(std::move(Lease));
				}
			}
			std::unique_ptr<FCreationRetirement> Lease;
			std::shared_ptr<FContext> Context;
			std::atomic<int> AdapterReady{0}, DeviceReady{0};
			std::string Error;
		};
		class FDeviceRequest final : public IDeviceRequest
		{
		public:
			ERequestStatus Poll(std::string& E) override
			{
				if (!Runtime->CheckThread(E))
				{
					return ERequestStatus::Failed;
				}
				Runtime->Drain();
				if (Status != ERequestStatus::Pending)
				{
					E = Error;
					return Status;
				}
				auto& C = *State->Context->Native;
				if (!C.Check(E))
				{
					Error = E;
					Status = ERequestStatus::Failed;
					State.reset();
					return Status;
				}
				if (State->AdapterReady.load() == -1 || State->DeviceReady.load() == -1)
				{
					Error = State->Error;
					E = Error;
					Status = ERequestStatus::Failed;
					State.reset();
					return Status;
				}
				if (State->AdapterReady.load() == 1 && !bDeviceRequested)
				{
					auto DeviceCallback = std::make_unique<FCallbackRetirement<FCreation>>(State, Runtime);
					WGPUDeviceDescriptor D = WGPU_DEVICE_DESCRIPTOR_INIT;
					D.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
					#if defined(__EMSCRIPTEN__)
					auto LostCallback = std::make_unique<FCallbackRetirement<FContext>>(State->Context, Runtime);
					D.deviceLostCallbackInfo.userdata1 = LostCallback.get();
					D.deviceLostCallbackInfo.callback =
					    [](WGPUDevice const*, WGPUDeviceLostReason Reason, WGPUStringView V, void* P, void*) noexcept
					{
						std::unique_ptr<FCallbackRetirement<FContext>> Owner(
						    static_cast<FCallbackRetirement<FContext>*>(P));
						auto& Errors = Owner->State->Native->Errors;
						if (Reason != WGPUDeviceLostReason_Destroyed)
						{
							Errors->Lost.store(true);
							Errors->Report(V);
							std::lock_guard<std::mutex> Lock(Errors->Mutex);
							if (const auto Open = Errors->OpenFlag.lock())
							{
								Open->store(false);
							}
						}
						// Retain callback code and error data through destruction and the final callback return.
						auto Runtime = Owner->Runtime;
						Runtime->Defer(std::move(Owner));
					};
					#else
					// Pinned wgpu-native reports loss through its error sink, can repeat it, and does not
					// invoke a final Destroyed callback. Keep userdata until all native handles retire.
					D.deviceLostCallbackInfo.userdata1 = C.Errors.get();
					D.deviceLostCallbackInfo.callback =
					    [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView V, void* P, void*) noexcept
					{
						auto& Errors = *static_cast<FWebGpuErrors*>(P);
						Errors.Lost.store(true);
						Errors.Report(V);
						std::lock_guard<std::mutex> Lock(Errors.Mutex);
						if (const auto Open = Errors.OpenFlag.lock())
						{
							Open->store(false);
						}
					};
					#endif
					D.uncapturedErrorCallbackInfo.userdata1 = C.Errors.get();
					D.uncapturedErrorCallbackInfo.callback =
					    [](WGPUDevice const*, WGPUErrorType, WGPUStringView V, void* P, void*) noexcept
					{
						static_cast<FWebGpuErrors*>(P)->Report(V);
					};
					WGPURequestDeviceCallbackInfo Callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
					Callback.mode = WGPUCallbackMode_AllowProcessEvents;
					Callback.userdata1 = DeviceCallback.get();
					Callback.callback =
					    [](WGPURequestDeviceStatus S, WGPUDevice Device, WGPUStringView V, void* P, void*) noexcept
					{
						std::unique_ptr<FCallbackRetirement<FCreation>> Owner(
						    static_cast<FCallbackRetirement<FCreation>*>(P));
						auto& F = *Owner->State;
						F.Context->Native->Device = Device;
						try
						{
							if (S != WGPURequestDeviceStatus_Success)
							{
								F.Error = "graphics.webgpu_device: " + Message(V);
							}
						}
						catch (...)
						{
							F.Context->Native->Errors->Lost.store(true);
						}
						F.DeviceReady.store(S == WGPURequestDeviceStatus_Success ? 1 : -1);
						auto Runtime = Owner->Runtime;
						Runtime->Defer(std::move(Owner));
					};
					// Commit only after every callback allocation has succeeded.
					bDeviceRequested = true;
					DeviceCallback.release();
					#if defined(__EMSCRIPTEN__)
					LostCallback.release();
					#endif
					wgpuAdapterRequestDevice(C.Adapter, &D, Callback);
				}
				if (State->DeviceReady.load() == 1)
				{
					// GetQueue returns an owned reference; retain it only once across retries.
					if (!C.Queue)
					{
						C.Queue = wgpuDeviceGetQueue(C.Device);
					}
					if (wgpuDeviceGetLimits(C.Device, &C.Limits) != WGPUStatus_Success)
					{
						Error = "graphics.webgpu_failure: query device limits";
						Status = ERequestStatus::Failed;
					}
					else
					{
						auto Backend = std::make_unique<FWebGpu>(State->Context);
						if (Backend->Initialize(Error))
						{
							Device = CreateBackendDevice(C.Runtime, State->Context->Options, std::move(Backend), Error);
						}
						Status = Device ? ERequestStatus::Succeeded : ERequestStatus::Failed;
						State->Lease->bTransferred = bool(Device);
					}
				}
				if (Status != ERequestStatus::Pending)
				{
					State.reset();
				}
				E = Error;
				return Status;
			}
			std::shared_ptr<IDevice> GetDevice(std::string& E) const override
			{
				if (!Runtime->CheckThread(E))
				{
					return {};
				}
				if (Status != ERequestStatus::Succeeded)
				{
					E = Error.empty() ? "graphics.request_not_ready: poll device creation to completion first" : Error;
					return {};
				}
				E.clear();
				return Device;
			}
			bool Cancel(std::string& E) override
			{
				if (!Runtime->CheckThread(E))
				{
					return false;
				}
				Status = ERequestStatus::Cancelled;
				State.reset();
				Device.reset();
				Error.clear();
				E.clear();
				return true;
			}
			explicit FDeviceRequest(std::shared_ptr<FCreation> F)
			    : Runtime(F->Context->Native->Runtime), State(std::move(F))
			{
			}

		private:
			std::shared_ptr<FRuntime> Runtime;
			std::shared_ptr<FCreation> State;
			std::shared_ptr<IDevice> Device;
			ERequestStatus Status = ERequestStatus::Pending;
			std::string Error;
			bool bDeviceRequested = false;
		};
	}
	std::shared_ptr<IDeviceRequest> CreateWebGpuDeviceAsync(const std::shared_ptr<FRuntime>& Runtime,
	    const FDeviceOptions& Options, std::string& Error)
	{
		auto State = std::make_shared<FCreation>();
		State->Context = std::make_shared<FContext>(Runtime);
		State->Context->Options = Options;
		State->Lease = std::make_unique<FCreationRetirement>();
		State->Lease->Runtime = Runtime;
		State->Lease->Context = State->Context;
		WGPUInstanceDescriptor D = WGPU_INSTANCE_DESCRIPTOR_INIT;
		State->Context->Native->Instance = wgpuCreateInstance(&D);
		if (!State->Context->Native->Instance)
		{
			Error = "graphics.webgpu_failure: create instance";
			return {};
		}
		Runtime->NativePumps.push_back(State->Context);
		WGPURequestAdapterOptions O = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
		WGPURequestAdapterCallbackInfo Callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
		Callback.mode = WGPUCallbackMode_AllowProcessEvents;
		Callback.userdata1 = new FCallbackRetirement<FCreation>(State, Runtime);
		Callback.callback = [](WGPURequestAdapterStatus S, WGPUAdapter A, WGPUStringView V, void* P, void*) noexcept
		{
			std::unique_ptr<FCallbackRetirement<FCreation>> Owner(static_cast<FCallbackRetirement<FCreation>*>(P));
			auto& F = *Owner->State;
			F.Context->Native->Adapter = A;
			try
			{
				if (S != WGPURequestAdapterStatus_Success)
				{
					F.Error = "graphics.webgpu_adapter: " + Message(V);
				}
			}
			catch (...)
			{
				F.Context->Native->Errors->Lost.store(true);
			}
			F.AdapterReady.store(S == WGPURequestAdapterStatus_Success ? 1 : -1);
			auto Runtime = Owner->Runtime;
			Runtime->Defer(std::move(Owner));
		};
		wgpuInstanceRequestAdapter(State->Context->Native->Instance, &O, Callback);
		Error.clear();
		return std::make_shared<FDeviceRequest>(std::move(State));
	}
}
#else
namespace VeroScript::Sdl3
{
	std::shared_ptr<Graphics::IDeviceRequest>
	CreateWebGpuDeviceAsync(const std::shared_ptr<FRuntime>&, const Graphics::FDeviceOptions&, std::string& Error)
	{
		Error = "graphics.unsupported_driver: WebGPU is disabled in this build";
		return {};
	}
}
#endif

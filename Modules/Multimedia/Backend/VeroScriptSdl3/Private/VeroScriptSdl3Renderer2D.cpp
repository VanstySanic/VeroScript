// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3Renderer2D.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <limits>

namespace VeroScript::Sdl3
{
	namespace
	{
		#include "VeroScriptSdl3Shaders.inl"
		using namespace Graphics;
		struct FGeometryBuffers
		{
			SDL_GPUBuffer* Vertices = nullptr;
			SDL_GPUBuffer* Indices = nullptr;
			Uint32 VertexBytes = 0, IndexBytes = 0;
		};
		class FGeometryPool
		{
		public:
			explicit FGeometryPool(SDL_GPUDevice* InDevice, std::uint64_t InLimit)
				: Device(InDevice), Limit(std::min<std::uint64_t>(InLimit, 8 * 1024 * 1024))
			{
			}

			FGeometryBuffers Take(Uint32 VertexBytes, Uint32 IndexBytes)
			{
				auto Best = Buffers.end();
				for (auto It = Buffers.begin(); It != Buffers.end(); ++It)
				{
					if (It->VertexBytes >= VertexBytes && It->IndexBytes >= IndexBytes &&
						(Best == Buffers.end() || std::uint64_t(It->VertexBytes) + It->IndexBytes <
						 std::uint64_t(Best->VertexBytes) + Best->IndexBytes))
					{
						Best = It;
						if (It->VertexBytes == VertexBytes && It->IndexBytes == IndexBytes)
						{
							break;
						}
					}
				}
				if (Best == Buffers.end())
				{
					return {};
				}
				const auto Result = *Best;
				Bytes -= std::uint64_t(Result.VertexBytes) + Result.IndexBytes;
				*Best = Buffers.back();
				Buffers.pop_back();
				return Result;
			}

			bool Put(FGeometryBuffers Value) noexcept
			{
				const auto Size = std::uint64_t(Value.VertexBytes) + Value.IndexBytes;
				if (!bOpen || !Value.Vertices || !Value.Indices || Size > Limit)
				{
					return false;
				}
				try
				{
					// Keep a complete large GUI frame reusable; the byte budget still
					// bounds retained buffers, including larger text and paint meshes.
					while (!Buffers.empty() && (Buffers.size() >= 4096 || Size > Limit - Bytes))
					{
						const auto Old = Buffers.back();
						SDL_ReleaseGPUBuffer(Device, Old.Vertices);
						SDL_ReleaseGPUBuffer(Device, Old.Indices);
						Bytes -= std::uint64_t(Old.VertexBytes) + Old.IndexBytes;
						Buffers.pop_back();
					}
					Buffers.push_back(Value);
					Bytes += Size;
					return true;
				}
				catch (...)
				{
					return false;
				}
			}

			void Close() noexcept
			{
				bOpen = false;
				for (const auto& Value : Buffers)
				{
					SDL_ReleaseGPUBuffer(Device, Value.Vertices);
					SDL_ReleaseGPUBuffer(Device, Value.Indices);
				}
				Buffers.clear();
				Bytes = 0;
			}

		private:
			SDL_GPUDevice* Device;
			std::uint64_t Limit, Bytes = 0;
			std::vector<FGeometryBuffers> Buffers;
			bool bOpen = true;
		};
		struct FGeometryRetirement final : FRetirement
		{
			void Release() noexcept override
			{
				if (const auto Owner = bCounted ? Pool.lock() : nullptr; Owner &&
					Owner->Put({Vertices, Indices, VertexBytes, IndexBytes}))
				{
					Vertices = nullptr;
					Indices = nullptr;
				}
				if (Vertices)
				{
					SDL_ReleaseGPUBuffer(Device->Retirement->Native, Vertices);
					Vertices = nullptr;
				}
				if (Indices)
				{
					SDL_ReleaseGPUBuffer(Device->Retirement->Native, Indices);
					Indices = nullptr;
				}
				if (bCounted)
				{
					--Device->Resources;
					bCounted = false;
				}
			}
			std::shared_ptr<FDeviceState> Device;
			SDL_GPUBuffer* Vertices = nullptr;
			SDL_GPUBuffer* Indices = nullptr;
			Uint32 VertexBytes = 0, IndexBytes = 0;
			std::weak_ptr<FGeometryPool> Pool;
			bool bCounted = false;
		};
		class FGeometry final : public IGeometry2D
		{
		public:
			//~ Begin Graphics::IGeometry2D Interface
			~FGeometry() override
			{
				Retirement->Runtime->Retire(std::move(Retirement));
			}
			bool IsOpen() const noexcept override
			{
				return bOpen.load() && Retirement->Device->bOpen.load();
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
				if (!Retirement->Runtime->CheckThread(Error))
				{
					return false;
				}
				Retirement->Release();
				bOpen.store(false);
				Error.clear();
				return true;
			}
			//~ End Graphics::IGeometry2D Interface

			explicit FGeometry(const std::shared_ptr<FDeviceState>& State)
				: Retirement(std::make_unique<FGeometryRetirement>())
			{
				Retirement->Runtime = State->Runtime;
				Retirement->Device = State;
			}
			std::unique_ptr<FGeometryRetirement> Retirement;
			std::int64_t VertexCount = 0, IndexCount = 0;
			std::atomic<bool> bOpen{false};
		};
		struct FRendererRetirement final : FRetirement
		{
			void Release() noexcept override
			{
				if (GeometrySubmission)
				{
					SDL_SubmitGPUCommandBuffer(GeometrySubmission->Take());
				}
				GeometrySubmission.reset();
				Submission.reset();
				if (Pool)
				{
					Pool->Close();
					Pool.reset();
				}
				if (Upload)
				{
					SDL_ReleaseGPUTransferBuffer(Device->Retirement->Native, Upload);
					Upload = nullptr;
					UploadBytes = 0;
				}
				for (auto& Entry : Pipelines)
				{
					SDL_ReleaseGPUGraphicsPipeline(Device->Retirement->Native, Entry.second);
				}
				Pipelines.clear();
				if (Vertex)
				{
					SDL_ReleaseGPUShader(Device->Retirement->Native, Vertex);
					Vertex = nullptr;
				}
				if (Fragment)
				{
					SDL_ReleaseGPUShader(Device->Retirement->Native, Fragment);
					Fragment = nullptr;
				}
				White.reset();
				Sampler.reset();
				if (bCounted)
				{
					--Device->Resources;
					bCounted = false;
				}
			}
			std::shared_ptr<FDeviceState> Device;
			std::unique_ptr<FCommand> Submission;
			std::unique_ptr<FCommand> GeometrySubmission;
			bool bHasRenderWork = false;
			std::shared_ptr<FGeometryPool> Pool;
			SDL_GPUTransferBuffer* Upload = nullptr;
			Uint32 UploadBytes = 0;
			Uint32 UploadOffset = 0;
			SDL_GPUShader* Vertex = nullptr;
			SDL_GPUShader* Fragment = nullptr;
			std::shared_ptr<FTexture> White;
			std::shared_ptr<ISampler> Sampler;
			std::map<std::pair<SDL_GPUTextureFormat, EBlendMode>, SDL_GPUGraphicsPipeline*> Pipelines;
			bool bCounted = false;
		};
		struct FPreparedDraw
		{
			FGeometry* Geometry = nullptr;
			SDL_GPUTexture* Texture = nullptr;
			SDL_GPUTexture* Mask = nullptr;
			SDL_GPUSampler* Sampler = nullptr;
			SDL_GPUGraphicsPipeline* Pipeline = nullptr;
			SDL_Rect Clip{};
			const FDraw2D* Command = nullptr;
		};
		class FRenderer2D final : public IRenderer2D, public IRenderSubmission2D
		{
		public:
			//~ Begin Graphics::IRenderer2D Interface
			~FRenderer2D() override
			{
				Retirement->Runtime->Retire(std::move(Retirement));
			}
			bool IsOpen() const noexcept override
			{
				return bOpen.load() && Retirement->Device->bOpen.load();
			}
			std::shared_ptr<IGeometry2D> CreateGeometry(const std::vector<FVertex2D>& Vertices,
														const std::vector<std::uint32_t>& Indices,
														std::string& Error) override
			{
				if (!Check(Error) ||
					!ValidateGeometry2D(Vertices, Indices, Retirement->Device->Options.MaxGeometryBytes, Error))
				{
					return {};
				}
				auto Geometry = std::make_shared<FGeometry>(Retirement->Device);
				Geometry->VertexCount = static_cast<std::int64_t>(Vertices.size());
				Geometry->IndexCount = static_cast<std::int64_t>(Indices.size());
				auto* Device = Retirement->Device->Retirement->Native;
				const auto VertexBytes = static_cast<Uint32>(Vertices.size() * sizeof(FVertex2D));
				const auto IndexBytes = static_cast<Uint32>(Indices.size() * sizeof(std::uint32_t));
				Geometry->Retirement->Pool = Retirement->Pool;
				const auto Reused = Retirement->Pool->Take(VertexBytes, IndexBytes);
				Geometry->Retirement->Vertices = Reused.Vertices;
				Geometry->Retirement->Indices = Reused.Indices;
				Geometry->Retirement->VertexBytes = Reused.VertexBytes;
				Geometry->Retirement->IndexBytes = Reused.IndexBytes;
				if (!Reused.Vertices)
				{
					SDL_GPUBufferCreateInfo Info{};
					Info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
					Info.size = VertexBytes;
					Geometry->Retirement->Vertices = SDL_CreateGPUBuffer(Device, &Info);
					Info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
					Info.size = IndexBytes;
					Geometry->Retirement->Indices = SDL_CreateGPUBuffer(Device, &Info);
					Geometry->Retirement->VertexBytes = VertexBytes;
					Geometry->Retirement->IndexBytes = IndexBytes;
				}
				if (!Geometry->Retirement->Vertices || !Geometry->Retirement->Indices)
				{
					SdlError("create geometry buffers", Error);
					return {};
				}
				if (Retirement->UploadBytes < VertexBytes + IndexBytes)
				{
					SDL_GPUTransferBufferCreateInfo Transfer{};
					Transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
					Transfer.size = std::max(VertexBytes + IndexBytes, static_cast<Uint32>(
						std::min<std::uint64_t>(256 * 1024, Retirement->Device->Options.MaxGeometryBytes)));
					auto* Upload = SDL_CreateGPUTransferBuffer(Device, &Transfer);
					if (!Upload)
					{
						SdlError("create geometry transfer", Error);
						return {};
					}
					if (Retirement->Upload)
					{
						SDL_ReleaseGPUTransferBuffer(Device, Retirement->Upload);
					}
					Retirement->Upload = Upload;
					Retirement->UploadBytes = Transfer.size;
					Retirement->UploadOffset = 0;
				}
				// Disjoint ranges within a submission share staging storage. Cycle
				// only at its start or when full, preserving all earlier copy sources.
				const bool bCycle = !Retirement->Submission || !Retirement->UploadOffset ||
					VertexBytes + IndexBytes > Retirement->UploadBytes - Retirement->UploadOffset;
				if (bCycle)
				{
					Retirement->UploadOffset = 0;
				}
				const auto Offset = Retirement->UploadOffset;
				auto* Data = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(Device, Retirement->Upload, bCycle));
				if (!Data)
				{
					SdlError("map geometry transfer", Error);
					return {};
				}
				std::memcpy(Data + Offset, Vertices.data(), VertexBytes);
				std::memcpy(Data + Offset + VertexBytes, Indices.data(), IndexBytes);
				SDL_UnmapGPUTransferBuffer(Device, Retirement->Upload);
				Retirement->UploadOffset = Offset + VertexBytes + IndexBytes;
				if (Retirement->Submission && !Retirement->GeometrySubmission)
				{
					auto Pending = std::make_unique<FCommand>(SDL_AcquireGPUCommandBuffer(Device));
					if (!Pending->Native)
					{
						SdlError("acquire geometry batch", Error);
						return {};
					}
					Retirement->GeometrySubmission = std::move(Pending);
				}
				FCommand Local{Retirement->Submission ? nullptr : SDL_AcquireGPUCommandBuffer(Device)};
				auto& Command = Retirement->Submission ? *Retirement->GeometrySubmission : Local;
				if (!Command.Native)
				{
					SdlError("acquire geometry upload", Error);
					return {};
				}
				auto* Pass = SDL_BeginGPUCopyPass(Command.Native);
				if (!Pass)
				{
					SdlError("begin geometry copy", Error);
					return {};
				}
				SDL_GPUTransferBufferLocation Source{Retirement->Upload, Offset};
				SDL_GPUBufferRegion Target{Geometry->Retirement->Vertices, 0, VertexBytes};
				SDL_UploadToGPUBuffer(Pass, &Source, &Target, true);
				Source.offset = Offset + VertexBytes;
				Target = {Geometry->Retirement->Indices, 0, IndexBytes};
				SDL_UploadToGPUBuffer(Pass, &Source, &Target, true);
				SDL_EndGPUCopyPass(Pass);
				if (!Retirement->Submission && !SDL_SubmitGPUCommandBuffer(Command.Take()))
				{
					SdlError("submit geometry upload", Error);
					return {};
				}
				++Retirement->Device->Resources;
				Geometry->Retirement->bCounted = true;
				Geometry->bOpen.store(true);
				Error.clear();
				return Geometry;
			}
			bool RenderTexture(const std::shared_ptr<ITexture>& Target, const FDrawList2D& List,
							   const FRenderOptions2D& Options, std::string& Error) override
			{
				if (!Check(Error))
				{
					return false;
				}
				const auto Texture = std::dynamic_pointer_cast<FTexture>(Target);
				if (!Texture || !Texture->IsOpen() || Texture->Retirement->Device != Retirement->Device)
				{
					Error = "graphics.invalid_target: expected an open texture from this device";
					return false;
				}
				const auto Format = Texture->Options.Format == ETextureFormat::Rgba8Srgb
										? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
										: SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
				std::vector<FPreparedDraw> Draws;
				FRect Viewport;
				if (!Prepare(List, Options, Texture->Options.Width, Texture->Options.Height, Format, Texture.get(),
							 Draws, Viewport, Error))
				{
					return false;
				}
				FCommand Local{Retirement->Submission ? nullptr : SDL_AcquireGPUCommandBuffer(Retirement->Device->Retirement->Native)};
				auto& Command = Retirement->Submission ? *Retirement->Submission : Local;
				if (!Command.Native)
				{
					return SdlError("acquire 2D texture command", Error);
				}
				if (!Record(Command, Texture->Retirement->Native, Options, Draws, Viewport, Error))
				{
					return false;
				}
				if (!Retirement->Submission && !SDL_SubmitGPUCommandBuffer(Command.Take()))
				{
					return SdlError("submit 2D texture", Error);
				}
				Error.clear();
				return true;
			}
			bool RenderSurface(const std::shared_ptr<ISurface>& Target, const FDrawList2D& List,
							   const FRenderOptions2D& Options, bool& bPresented, std::string& Error) override
			{
				bPresented = false;
				if (!Check(Error))
				{
					return false;
				}
				const auto Surface = std::dynamic_pointer_cast<FSurface>(Target);
				if (!Surface || !Surface->IsOpen() || Surface->Retirement->Device != Retirement->Device)
				{
					Error = "graphics.invalid_target: expected an open surface from this device";
					return false;
				}
				auto* Window = Surface->Retirement->Window->Retirement->Native;
				auto* Device = Retirement->Device->Retirement->Native;
				int Width = 0, Height = 0;
				if (!SDL_GetWindowSizeInPixels(Window, &Width, &Height))
				{
					return SdlError("query render surface", Error);
				}
				if (Width <= 0 || Height <= 0)
				{
					bPresented = false;
					Error.clear();
					return true;
				}
				std::vector<FPreparedDraw> Draws;
				FRect Viewport;
				const auto Format = SDL_GetGPUSwapchainTextureFormat(Device, Window);
				if (!Options.bClear)
				{
					Error = "graphics.invalid_load: swapchain images require a clear";
					return false;
				}
				if (!Prepare(List, Options, Width, Height, Format, nullptr, Draws, Viewport, Error))
				{
					return false;
				}
				FCommand Local{Retirement->Submission ? nullptr : SDL_AcquireGPUCommandBuffer(Device)};
				auto& Command = Retirement->Submission ? *Retirement->Submission : Local;
				if (!Command.Native)
				{
					return SdlError("acquire 2D surface command", Error);
				}
				SDL_GPUTexture* Texture = nullptr;
				Uint32 PixelWidth = 0, PixelHeight = 0;
				if (!SDL_AcquireGPUSwapchainTexture(Command.Native, Window, &Texture, &PixelWidth, &PixelHeight))
				{
					return SdlError("acquire 2D swapchain", Error);
				}
				if (!Texture)
				{
					bPresented = false;
					Error.clear();
					return true;
				}
				Command.bSwapchainAcquired = true;
				// Acquisition requires submission; clear a resized image and refresh geometry next frame.
				if (PixelWidth != static_cast<Uint32>(Width) || PixelHeight != static_cast<Uint32>(Height))
				{
					if (!Record(Command, Texture, Options, {}, {0, 0, PixelWidth, PixelHeight}, Error))
					{
						return false;
					}
					if (!Retirement->Submission && !SDL_SubmitGPUCommandBuffer(Command.Take()))
					{
						return SdlError("submit resized 2D surface", Error);
					}
					bPresented = true;
					Error.clear();
					return true;
				}
				if (!Record(Command, Texture, Options, Draws, Viewport, Error))
				{
					return false;
				}
				if (!Retirement->Submission && !SDL_SubmitGPUCommandBuffer(Command.Take()))
				{
					return SdlError("submit 2D surface", Error);
				}
				bPresented = true;
				Error.clear();
				return true;
			}
			bool Close(std::string& Error) override
			{
				if (!Retirement->Runtime->CheckThread(Error))
				{
					return false;
				}
				Retirement->Release();
				bOpen.store(false);
				Error.clear();
				return true;
			}
			//~ End Graphics::IRenderer2D Interface

			//~ Begin Graphics::IRenderSubmission2D Interface
			bool BeginSubmission(std::string& Error) override
			{
				if (!Check(Error)) return false;
				if (Retirement->Submission)
				{
					Error = "graphics.invalid_operation: a render submission is already active";
					return false;
				}
				auto Command = std::make_unique<FCommand>(SDL_AcquireGPUCommandBuffer(Retirement->Device->Retirement->Native));
				if (!Command->Native) return SdlError("acquire 2D submission", Error);
				Retirement->Submission = std::move(Command);
				Retirement->bHasRenderWork = false;
				Retirement->UploadOffset = 0;
				Error.clear();
				return true;
			}
			bool EndSubmission(std::string& Error) override
			{
				if (!Check(Error)) return false;
				if (!Retirement->Submission)
				{
					Error = "graphics.invalid_operation: no render submission is active";
					return false;
				}
				auto Command = std::move(Retirement->Submission);
				if (!SubmitGeometry(Error)) return false;
				if ((Retirement->bHasRenderWork || Command->bSwapchainAcquired) &&
					!SDL_SubmitGPUCommandBuffer(Command->Take()))
				{
					return SdlError("submit 2D frame", Error);
				}
				Error.clear();
				return true;
			}
			bool CancelSubmission(std::string& Error) override
			{
				if (!Retirement->Runtime->CheckThread(Error)) return false;
				// Geometry creation remains persistent when drawing is cancelled.
				const bool bUploaded = SubmitGeometry(Error);
				Retirement->Submission.reset();
				if (!bUploaded) return false;
				Error.clear();
				return true;
			}
			//~ End Graphics::IRenderSubmission2D Interface

			explicit FRenderer2D(const std::shared_ptr<FDeviceState>& State)
				: Retirement(std::make_unique<FRendererRetirement>())
			{
				Retirement->Runtime = State->Runtime;
				Retirement->Device = State;
				Retirement->Pool = std::make_shared<FGeometryPool>(State->Retirement->Native, State->Options.MaxGeometryBytes);
			}
			std::unique_ptr<FRendererRetirement> Retirement;
			std::atomic<bool> bOpen{false};

		private:
			bool SubmitGeometry(std::string& Error)
			{
				auto Command = std::move(Retirement->GeometrySubmission);
				return !Command || SDL_SubmitGPUCommandBuffer(Command->Take()) || SdlError("submit geometry batch", Error);
			}

			bool Check(std::string& Error) const
			{
				if (!Retirement->Device->Check(Error))
				{
					return false;
				}
				if (!IsOpen())
				{
					Error = "graphics.closed_renderer: renderer is closed";
					return false;
				}
				return true;
			}
			SDL_GPUGraphicsPipeline* Pipeline(SDL_GPUTextureFormat Format, EBlendMode Blend, std::string& Error)
			{
				const auto Key = std::make_pair(Format, Blend);
				const auto Found = Retirement->Pipelines.find(Key);
				if (Found != Retirement->Pipelines.end())
				{
					return Found->second;
				}
				SDL_GPUVertexBufferDescription Buffer{0, sizeof(FVertex2D), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
				SDL_GPUVertexAttribute Attributes[] = {
					{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(FVertex2D, X)},
					{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(FVertex2D, U)},
					{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(FVertex2D, Color)}};
				SDL_GPUColorTargetDescription Color{};
				Color.format = Format;
				Color.blend_state.enable_blend = Blend != EBlendMode::Opaque;
				Color.blend_state.src_color_blendfactor =
					Blend == EBlendMode::StraightAlpha ? SDL_GPU_BLENDFACTOR_SRC_ALPHA : SDL_GPU_BLENDFACTOR_ONE;
				Color.blend_state.dst_color_blendfactor =
					Blend == EBlendMode::Additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
				Color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
				Color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
				Color.blend_state.dst_alpha_blendfactor =
					Blend == EBlendMode::Additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
				Color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
				SDL_GPUGraphicsPipelineCreateInfo Info{};
				Info.vertex_shader = Retirement->Vertex;
				Info.fragment_shader = Retirement->Fragment;
				Info.vertex_input_state = {&Buffer, 1, Attributes, 3};
				Info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
				Info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
				Info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
				Info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
				Info.target_info.color_target_descriptions = &Color;
				Info.target_info.num_color_targets = 1;
				auto* Device = Retirement->Device->Retirement->Native;
				const auto Delete = [Device](SDL_GPUGraphicsPipeline* P)
				{
					SDL_ReleaseGPUGraphicsPipeline(Device, P);
				};
				std::unique_ptr<SDL_GPUGraphicsPipeline, decltype(Delete)> Result(
					SDL_CreateGPUGraphicsPipeline(Device, &Info), Delete);
				if (!Result)
				{
					SdlError("create 2D pipeline", Error);
					return nullptr;
				}
				Retirement->Pipelines.emplace(Key, Result.get());
				return Result.release();
			}
			bool Prepare(const FDrawList2D& List, const FRenderOptions2D& Options, std::int64_t Width,
						 std::int64_t Height, SDL_GPUTextureFormat Format, FTexture* Target,
						 std::vector<FPreparedDraw>& Draws, FRect& Viewport, std::string& Error)
			{
				if (!ValidateColor(Options.ClearColor, Error) || !ValidateRect({0, 0, Width, Height}, Error))
				{
					return false;
				}
				Viewport = Options.bViewport ? Options.Viewport : FRect{0, 0, Width, Height};
				if (!ValidateRect(Viewport, Error) || Viewport.X < 0 || Viewport.Y < 0 || Viewport.Width <= 0 ||
					Viewport.Height <= 0 || Viewport.X + Viewport.Width > Width ||
					Viewport.Y + Viewport.Height > Height)
				{
					Error = "graphics.invalid_viewport: viewport must be nonempty and inside the target";
					return false;
				}
				Draws.reserve(List.GetCommands().size());
				for (const auto& Draw : List.GetCommands())
				{
					const auto Geometry = std::dynamic_pointer_cast<FGeometry>(Draw.Geometry);
					const auto Texture =
						Draw.Texture ? std::dynamic_pointer_cast<FTexture>(Draw.Texture) : Retirement->White;
					if (!Geometry || !Geometry->IsOpen() || Geometry->Retirement->Device != Retirement->Device ||
						!Texture || !Texture->IsOpen() || Texture->Retirement->Device != Retirement->Device ||
						Texture.get() == Target)
					{
						Error = "graphics.invalid_draw: closed, foreign or feedback resource";
						return false;
					}
					if (!ValidateEffect2D(Draw.Effect, Error))
					{
						return false;
					}
					const auto Mask = Draw.Effect.Type == EEffect2D::AlphaMask
										  ? std::dynamic_pointer_cast<FTexture>(Draw.Effect.Mask)
										  : Retirement->White;
					if (!Mask || !Mask->IsOpen() || Mask->Retirement->Device != Retirement->Device ||
						Mask.get() == Target)
					{
						Error = "graphics.invalid_mask: expected an open non-feedback mask from this device";
						return false;
					}
					auto* Sampler =
						GetNativeSampler(Draw.Sampler ? Draw.Sampler : Retirement->Sampler, Retirement->Device, Error);
					if (!Sampler)
					{
						return false;
					}
					auto* Pipe = Pipeline(Format, Draw.Blend, Error);
					if (!Pipe)
					{
						return false;
					}
					const auto Clip = Draw.bClip ? IntersectRects(Viewport, Draw.Clip) : Viewport;
					if (!Clip.Width || !Clip.Height)
					{
						continue;
					}
					Draws.push_back({Geometry.get(),
									 Texture->Retirement->Native,
									 Mask->Retirement->Native,
									 Sampler,
									 Pipe,
									 {static_cast<int>(Clip.X), static_cast<int>(Clip.Y), static_cast<int>(Clip.Width),
									  static_cast<int>(Clip.Height)},
									 &Draw});
				}
				Error.clear();
				return true;
			}
			bool Record(FCommand& Command, SDL_GPUTexture* Target, const FRenderOptions2D& Options,
						const std::vector<FPreparedDraw>& Draws, const FRect& Viewport, std::string& Error)
			{
				Retirement->bHasRenderWork = true;
				SDL_GPUColorTargetInfo Color{};
				Color.texture = Target;
				Color.clear_color = {Options.ClearColor.R, Options.ClearColor.G, Options.ClearColor.B,
									 Options.ClearColor.A};
				Color.load_op = Options.bClear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
				Color.store_op = SDL_GPU_STOREOP_STORE;
				auto* Pass = SDL_BeginGPURenderPass(Command.Native, &Color, 1, nullptr);
				if (!Pass)
				{
					return SdlError("begin 2D render pass", Error);
				}
				SDL_GPUViewport NativeViewport{static_cast<float>(Viewport.X),
											   static_cast<float>(Viewport.Y),
											   static_cast<float>(Viewport.Width),
											   static_cast<float>(Viewport.Height),
											   0,
											   1};
				SDL_SetGPUViewport(Pass, &NativeViewport);
				for (const auto& Draw : Draws)
				{
					struct alignas(16) FUniform
					{
						FTransform2D Transform;
						float Viewport[4];
					};
					static_assert(sizeof(FUniform) == 80 && sizeof(FVertex2D) == 32);
					FUniform Uniform{Draw.Command->Transform,
									 {static_cast<float>(Viewport.Width), static_cast<float>(Viewport.Height), 0, 0}};
					SDL_PushGPUVertexUniformData(Command.Native, 0, &Uniform, sizeof(Uniform));
					struct alignas(16) FFragmentUniform
					{
						FColor Tint;
						float Options[4];
						float Filter[4];
						FColor EffectColor;
						float Rows[4][4];
						float Bias[4];
					};
					static_assert(sizeof(FFragmentUniform) == 144);
					const auto& Effect = Draw.Command->Effect;
					FFragmentUniform Fragment{};
					Fragment.Tint = Draw.Command->Tint;
					Fragment.Options[0] = Draw.Command->Blend == EBlendMode::PremultipliedAlpha ||
												  Draw.Command->Blend == EBlendMode::Additive
											  ? 1.0f
											  : 0.0f;
					Fragment.Options[1] =
						Draw.Command->VertexColorMode == EVertexColorMode::Premultiplied ? 1.0f : 0.0f;
					Fragment.Options[2] = static_cast<float>(Effect.Type);
					Fragment.Options[3] = Effect.bSourcePremultiplied ? 1.0f : 0.0f;
					const auto Source = Draw.Command->Texture ? Draw.Command->Texture : Retirement->White;
					const auto Size = Source->GetOptions();
					Fragment.Filter[0] = Effect.bVertical ? 0 : Effect.Radius / (6 * static_cast<float>(Size.Width));
					Fragment.Filter[1] = Effect.bVertical ? Effect.Radius / (6 * static_cast<float>(Size.Height)) : 0;
					Fragment.Filter[2] = Effect.bInvert ? 1.0f : 0.0f;
					Fragment.Filter[3] = Effect.Radius;
					Fragment.EffectColor = Effect.Color;
					for (unsigned Row = 0; Row < 4; ++Row)
					{
						for (unsigned Column = 0; Column < 4; ++Column)
						{
							Fragment.Rows[Row][Column] = Effect.Matrix[Row * 5 + Column];
						}
						Fragment.Bias[Row] = Effect.Matrix[Row * 5 + 4];
					}
					if (Effect.Type == EEffect2D::Shadow)
					{
						Fragment.Rows[0][0] = Effect.OffsetX / static_cast<float>(Size.Width);
						Fragment.Rows[0][1] = Effect.OffsetY / static_cast<float>(Size.Height);
					}
					SDL_PushGPUFragmentUniformData(Command.Native, 0, &Fragment, sizeof(Fragment));
					SDL_BindGPUGraphicsPipeline(Pass, Draw.Pipeline);
					SDL_SetGPUScissor(Pass, &Draw.Clip);
					SDL_GPUBufferBinding Vertex{Draw.Geometry->Retirement->Vertices, 0},
						Index{Draw.Geometry->Retirement->Indices, 0};
					SDL_BindGPUVertexBuffers(Pass, 0, &Vertex, 1);
					SDL_BindGPUIndexBuffer(Pass, &Index, SDL_GPU_INDEXELEMENTSIZE_32BIT);
					SDL_GPUTextureSamplerBinding Images[]{{Draw.Texture, Draw.Sampler}, {Draw.Mask, Draw.Sampler}};
					SDL_BindGPUFragmentSamplers(Pass, 0, Images, 2);
					SDL_DrawGPUIndexedPrimitives(Pass, static_cast<Uint32>(Draw.Geometry->IndexCount), 1, 0, 0, 0);
				}
				SDL_EndGPURenderPass(Pass);
				return true;
			}
		};
	}
	std::shared_ptr<Graphics::IRenderer2D> CreateRenderer2DImplementation(const std::shared_ptr<FDeviceState>& State,
																		  std::string& Error)
	{
		if (!State->Check(Error))
		{
			return {};
		}
		auto Renderer = std::make_shared<FRenderer2D>(State);
		auto& Resources = *Renderer->Retirement;
		auto* Device = State->Retirement->Native;
		const auto Formats = SDL_GetGPUShaderFormats(Device);
		SDL_GPUShaderCreateInfo Info{};
		if (Formats & SDL_GPU_SHADERFORMAT_SPIRV)
		{
			Info.format = SDL_GPU_SHADERFORMAT_SPIRV;
		}
		else if (Formats & SDL_GPU_SHADERFORMAT_DXBC)
		{
			Info.format = SDL_GPU_SHADERFORMAT_DXBC;
		}
		else if (Formats & SDL_GPU_SHADERFORMAT_MSL)
		{
			Info.format = SDL_GPU_SHADERFORMAT_MSL;
		}
		else
		{
			Error = "graphics.unsupported_shader: no embedded 2D shader format is supported";
			return {};
		}
		for (bool bVertex : {true, false})
		{
			Info.stage = bVertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
			Info.entrypoint = "main";
			Info.num_uniform_buffers = 1;
			Info.num_samplers = bVertex ? 0 : 2;
			if (Info.format == SDL_GPU_SHADERFORMAT_SPIRV)
			{
				Info.code = bVertex ? VertSpirv : FragSpirv;
				Info.code_size = bVertex ? sizeof(VertSpirv) : sizeof(FragSpirv);
			}
			else if (Info.format == SDL_GPU_SHADERFORMAT_DXBC)
			{
				Info.code = bVertex ? VertDxbc : FragDxbc;
				Info.code_size = bVertex ? sizeof(VertDxbc) : sizeof(FragDxbc);
			}
			else
			{
				Info.code = reinterpret_cast<const Uint8*>(MetalSource);
				Info.code_size = sizeof(MetalSource) - 1;
				Info.entrypoint = bVertex ? "vertex_main" : "fragment_main";
			}
			auto* Shader = SDL_CreateGPUShader(Device, &Info);
			if (!Shader)
			{
				SdlError("create embedded 2D shader", Error);
				return {};
			}
			if (bVertex)
			{
				Resources.Vertex = Shader;
			}
			else
			{
				Resources.Fragment = Shader;
			}
		}
		Resources.Sampler = CreateSamplerImplementation(State, {}, Error);
		if (!Resources.Sampler)
		{
			return {};
		}
		Resources.White = std::make_shared<FTexture>(State, FTextureOptions{});
		SDL_GPUTextureCreateInfo White{};
		White.type = SDL_GPU_TEXTURETYPE_2D;
		White.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		White.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
		White.width = White.height = White.layer_count_or_depth = White.num_levels = 1;
		White.sample_count = SDL_GPU_SAMPLECOUNT_1;
		Resources.White->Retirement->Native = SDL_CreateGPUTexture(Device, &White);
		if (!Resources.White->Retirement->Native)
		{
			SdlError("create solid color texture", Error);
			return {};
		}
		++State->Resources;
		Resources.White->bOpen.store(true);
		if (!UpdateTextureImplementation(State, Resources.White, 0, 0, 1, 1, {255, 255, 255, 255}, Error))
		{
			return {};
		}
		++State->Resources;
		Resources.bCounted = true;
		Renderer->bOpen.store(true);
		Error.clear();
		return Renderer;
	}
}

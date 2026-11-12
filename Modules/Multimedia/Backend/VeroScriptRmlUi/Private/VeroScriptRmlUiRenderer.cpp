// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUiRenderer.h"
#include "VeroScriptImage.h"
#include "VeroScriptRmlUiEffects.h"
#include <RmlUi/Core/Matrix4.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <algorithm>
#include <cmath>
#include <exception>

namespace VeroScript::RmlUi
{
	namespace
	{
		template <typename T>
		bool CloseResources(std::vector<std::shared_ptr<T>>& Resources, std::string& Error)
		{
			while (!Resources.empty())
			{
				// RmlUi also releases resources from noexcept destructors. Never unwind through those callbacks.
				try
				{
					if (!Resources.back()->Close(Error))
					{
						if (Error.empty())
						{
							Error = "rmlui.resource_close_failed";
						}
						return false;
					}
				}
				catch (const std::exception& Exception)
				{
					Error = std::string("rmlui.resource_close_exception: ") + Exception.what();
					return false;
				}
				catch (...)
				{
					Error = "rmlui.resource_close_exception";
					return false;
				}
				Resources.pop_back();
			}
			return true;
		}
	} // namespace

	Rml::CompiledGeometryHandle FRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> Vertices,
	    Rml::Span<const int> Indices)
	{
		if (!Failure.empty())
		{
			return 0;
		}
		if (!CanRender())
		{
			Fail("rmlui.rendering_unavailable");
			return 0;
		}
		std::vector<Graphics::FVertex2D> Converted;
		std::vector<std::uint32_t> ConvertedIndices;
		Converted.reserve(Vertices.size());
		ConvertedIndices.reserve(Indices.size());
		for (const auto& Vertex : Vertices)
		{
			// Keep premultiplied colors through interpolation, including fully transparent vertices.
			const auto Color = ConvertColor(Vertex.colour);
			Converted.push_back({Vertex.position.x, Vertex.position.y, Vertex.tex_coord.x, Vertex.tex_coord.y, Color});
		}
		for (const int Index : Indices)
		{
			if (Index < 0)
			{
				Fail("rmlui.invalid_geometry_index");
				return 0;
			}
			ConvertedIndices.push_back(static_cast<std::uint32_t>(Index));
		}
		auto Geometry = CreateGeometry(Converted, ConvertedIndices);
		if (!Geometry)
		{
			Fail("rmlui.geometry_failed");
			return 0;
		}
		const auto Handle = NextHandle++;
		Geometries.emplace(Handle, std::move(Geometry));
		GeometrySources.emplace(Handle, std::make_pair(Vertices, Indices));
		return Handle;
	}

	void FRenderer::RenderGeometry(Rml::CompiledGeometryHandle Geometry, Rml::Vector2f Translation,
	    Rml::TextureHandle Texture)
	{
		if (!Failure.empty())
		{
			return;
		}
		const auto Found = Geometries.find(Geometry);
		if (Found == Geometries.end())
		{
			Fail("rmlui.unknown_geometry");
			return;
		}
		std::shared_ptr<Graphics::ITexture> Image;
		if (Texture)
		{
			const auto TextureIt = Textures.find(Texture);
			if (TextureIt == Textures.end())
			{
				Fail("rmlui.unknown_texture");
				return;
			}
			Image = TextureIt->second;
		}
		auto Matrix = Transform;
		for (std::size_t Row = 0; Row < 4; ++Row)
		{
			Matrix.Values[12 + Row] +=
			    Transform.Values[Row] * Translation.x + Transform.Values[4 + Row] * Translation.y;
		}
		if (!Draws.SetTransform(Matrix, Failure) ||
		    !Draws.SetBlendMode(Graphics::EBlendMode::PremultipliedAlpha, Failure) ||
		    !Draws.SetVertexColorMode(Graphics::EVertexColorMode::Premultiplied, Failure))
		{
			return;
		}
		Draws.SetTexture(std::move(Image), Sampler);
		if (bScissor && !Draws.PushClip(Scissor, Failure))
		{
			return;
		}
		const bool bAdded = Draws.AddGeometry(Found->second, Failure);
		if (bScissor)
		{
			std::string PopError;
			if (!Draws.PopClip(PopError) && Failure.empty())
			{
				Failure = PopError;
			}
		}
		if (!bAdded)
		{
			Fail("rmlui.draw_failed");
		}
	}

	void FRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle Geometry)
	{
		for (auto& Shader : Shaders)
		{
			const auto Cached = Shader.second.Geometry.find(Geometry);
			if (Cached != Shader.second.Geometry.end())
			{
				const auto Value = Cached->second;
				Shader.second.Geometry.erase(Cached);
				ReleaseTexture(Value.Texture);
				ReleaseGeometry(Value.Geometry);
			}
		}
		GeometrySources.erase(Geometry);
		const auto Found = Geometries.find(Geometry);
		if (Found == Geometries.end())
		{
			return;
		}
		std::string Error;
		ReleasedGeometries.push_back(std::move(Found->second));
		Geometries.erase(Found);
		if (Draws.GetCommands().empty() && !Composition.HasWork() && Masks.empty() && !CloseReleased(Error))
		{
			Fail(Error.empty() ? "rmlui.resource_close_failed" : Error.c_str());
		}
	}

	Rml::TextureHandle FRenderer::LoadTexture(Rml::Vector2i& Dimensions, const Rml::String& Source)
	{
		try
		{
			const auto Handle = LoadImageTexture(Dimensions, Source);
			if (!Handle)
			{
				Fail("rmlui.image_load_failed");
				ResourceFailures[Source] = Failure;
			}
			else
			{
				ResourceFailures.erase(Source);
			}
			return Handle;
		}
		catch (const std::exception& Exception)
		{
			Dimensions = {};
			Fail((std::string("rmlui.image_exception: ") + Exception.what()).c_str());
			ResourceFailures[Source] = Failure;
			return 0;
		}
		catch (...)
		{
			Dimensions = {};
			Fail("rmlui.image_exception");
			ResourceFailures[Source] = Failure;
			return 0;
		}
	}

	Rml::TextureHandle FRenderer::GenerateTexture(Rml::Span<const Rml::byte> Source, Rml::Vector2i Dimensions)
	{
		if (!Failure.empty())
		{
			return 0;
		}
		if (!CanRender() || Dimensions.x <= 0 || Dimensions.y <= 0 ||
		    static_cast<std::uint64_t>(Dimensions.x) * static_cast<std::uint64_t>(Dimensions.y) * 4 != Source.size())
		{
			Fail("rmlui.invalid_texture");
			return 0;
		}
		auto Texture =
		    Options.Device->CreateTexture({Dimensions.x, Dimensions.y, Graphics::ETextureFormat::Rgba8Unorm}, Failure);
		if (!Texture)
		{
			Fail("rmlui.texture_failed");
			return 0;
		}
		std::vector<std::uint8_t> Bytes(Source.begin(), Source.end());
		for (std::size_t Index = 0; Index < Bytes.size(); Index += 4)
		{
			const auto Color = ConvertColor({Bytes[Index], Bytes[Index + 1], Bytes[Index + 2], Bytes[Index + 3]});
			Bytes[Index] = static_cast<std::uint8_t>(std::lround(Color.R * 255));
			Bytes[Index + 1] = static_cast<std::uint8_t>(std::lround(Color.G * 255));
			Bytes[Index + 2] = static_cast<std::uint8_t>(std::lround(Color.B * 255));
		}
		if (!Options.Device->UpdateTexture(Texture, 0, 0, Dimensions.x, Dimensions.y, Bytes, Failure))
		{
			ReleasedTextures.push_back(std::move(Texture));
			std::string Ignored;
			CloseReleased(Ignored);
			return 0;
		}
		const auto Handle = NextHandle++;
		Textures.emplace(Handle, std::move(Texture));
		return Handle;
	}

	void FRenderer::ReleaseTexture(Rml::TextureHandle Texture)
	{
		if (MetadataTextures.erase(Texture))
		{
			return;
		}
		const auto Found = Textures.find(Texture);
		if (Found == Textures.end())
		{
			return;
		}
		std::string Error;
		ReleasedTextures.push_back(std::move(Found->second));
		Textures.erase(Found);
		if (Draws.GetCommands().empty() && !Composition.HasWork() && Masks.empty() && !CloseReleased(Error))
		{
			Fail(Error.empty() ? "rmlui.resource_close_failed" : Error.c_str());
		}
	}

	void FRenderer::EnableScissorRegion(bool bEnable)
	{
		bScissor = bEnable;
	}

	void FRenderer::SetScissorRegion(Rml::Rectanglei Region)
	{
		Scissor = {Region.Left(), Region.Top(), Region.Width(), Region.Height()};
	}

	void FRenderer::SetTransform(const Rml::Matrix4f* Value)
	{
		Transform = {};
		if (!Value)
		{
			return;
		}
		if (!Options.Device || !Options.Device->GetCapabilities().bGuiTransform)
		{
			Fail("rmlui.transform_unavailable");
			return;
		}
		for (int Column = 0; Column < 4; ++Column)
		{
			for (int Row = 0; Row < 4; ++Row)
			{
				Transform.Values[static_cast<std::size_t>(Column * 4 + Row)] = Value->GetColumn(Column)[Row];
			}
		}
	}

	void FRenderer::EnableClipMask(bool bEnable)
	{
		if (bEnable && !CanComposite())
		{
			Fail("rmlui.clip_masks_unavailable");
			return;
		}
		if (bMask != bEnable && FlushDraws())
		{
			bMask = bEnable;
			if (bEnable && Masks.empty())
			{
				Masks.emplace_back();
			}
		}
	}

	void FRenderer::RenderToClipMask(Rml::ClipMaskOperation Operation, Rml::CompiledGeometryHandle Geometry,
	    Rml::Vector2f Translation)
	{
		if (!CanComposite())
		{
			Fail("rmlui.clip_masks_unavailable");
			return;
		}
		if (!FlushDraws())
		{
			return;
		}
		if (Operation != Rml::ClipMaskOperation::Intersect)
		{
			Masks.clear();
		}
		if (Masks.size() >= 24)
		{
			Fail("rmlui.clip_mask_depth");
			return;
		}
		RenderGeometry(Geometry, Translation, 0);
		FClipMask Mask;
		Mask.Draws = std::move(Draws);
		Mask.bInvert = Operation == Rml::ClipMaskOperation::SetInverse;
		Draws = {};
		Masks.push_back(std::move(Mask));
	}

	Rml::LayerHandle FRenderer::PushLayer()
	{
		if (!FlushDraws())
		{
			return 0;
		}
		return Composition.Push(Failure);
	}

	void FRenderer::CompositeLayers(Rml::LayerHandle Source, Rml::LayerHandle Destination, Rml::BlendMode Blend,
	    Rml::Span<const Rml::CompiledFilterHandle> Handles)
	{
		if (!FlushDraws())
		{
			return;
		}
		std::vector<Graphics::FEffect2D> Effects;
		for (const auto Handle : Handles)
		{
			const auto Found = Filters.find(Handle);
			if (Found == Filters.end())
			{
				Fail("rmlui.unknown_filter");
				return;
			}
			Effects.insert(Effects.end(), Found->second.begin(), Found->second.end());
		}
		Composition.Composite(Source, Destination, Blend == Rml::BlendMode::Replace, std::move(Effects), bScissor,
		    Scissor, Failure, bMask ? Masks : std::vector<FClipMask>{});
	}

	void FRenderer::PopLayer()
	{
		if (FlushDraws())
		{
			Composition.Pop(Failure);
		}
	}

	Rml::TextureHandle FRenderer::SaveLayerAsTexture()
	{
		return CaptureLayer(false);
	}

	Rml::CompiledFilterHandle FRenderer::SaveLayerAsMaskImage()
	{
		const auto Image = CaptureLayer(true);
		if (!Image)
		{
			return 0;
		}
		Graphics::FEffect2D Effect;
		Effect.Type = Graphics::EEffect2D::AlphaMask;
		Effect.Mask = Textures.at(Image);
		const auto Handle = NextHandle++;
		Filters.emplace(Handle, std::vector<Graphics::FEffect2D>{Effect});
		FilterTextures.emplace(Handle, Image);
		return Handle;
	}

	Rml::CompiledFilterHandle FRenderer::CompileFilter(const Rml::String& Name, const Rml::Dictionary& Parameters)
	{
		if (!Failure.empty())
		{
			return 0;
		}
		if (!CanComposite())
		{
			Fail("rmlui.filters_unavailable");
			EffectFailure = Failure;
			return 0;
		}
		std::vector<Graphics::FEffect2D> Effects;
		if (!CompileEffects(Name, Parameters, Effects, Failure))
		{
			EffectFailure = Failure;
			return 0;
		}
		const auto Handle = NextHandle++;
		Filters.emplace(Handle, std::move(Effects));
		return Handle;
	}

	void FRenderer::ReleaseFilter(Rml::CompiledFilterHandle Filter)
	{
		Filters.erase(Filter);
		const auto Found = FilterTextures.find(Filter);
		if (Found != FilterTextures.end())
		{
			const auto Image = Found->second;
			FilterTextures.erase(Found);
			ReleaseTexture(Image);
		}
	}

	Rml::CompiledShaderHandle FRenderer::CompileShader(const Rml::String& Name, const Rml::Dictionary& Parameters)
	{
		if (!Failure.empty())
		{
			return 0;
		}
		FShader Shader;
		if (!CompileGradient(Name, Parameters, Shader.Gradient, Failure))
		{
			EffectFailure = Failure;
			return 0;
		}
		const auto Handle = NextHandle++;
		Shaders.emplace(Handle, std::move(Shader));
		return Handle;
	}

	void FRenderer::RenderShader(Rml::CompiledShaderHandle Shader, Rml::CompiledGeometryHandle Geometry,
	    Rml::Vector2f Translation, Rml::TextureHandle)
	{
		if (!Failure.empty())
		{
			return;
		}
		const auto Found = Shaders.find(Shader);
		const auto Source = GeometrySources.find(Geometry);
		if (Found == Shaders.end() || Source == GeometrySources.end())
		{
			Fail("rmlui.unknown_shader_geometry");
			return;
		}
		auto Cached = Found->second.Geometry.find(Geometry);
		if (Cached == Found->second.Geometry.end())
		{
			std::vector<Graphics::FVertex2D> Vertices;
			std::vector<std::uint32_t> Indices;
			Vertices.reserve(Source->second.first.size());
			for (const auto& V : Source->second.first)
			{
				Vertices.push_back({V.position.x, V.position.y, V.tex_coord.x, V.tex_coord.y, ConvertColor(V.colour)});
			}
			for (const int I : Source->second.second)
			{
				Indices.push_back(static_cast<std::uint32_t>(I));
			}
			std::vector<std::uint8_t> Pixels;
			std::int32_t W = 0, H = 0;
			Rml::TextureHandle Image = 0;
			if (!TryLinearGradient(Found->second.Gradient, Vertices))
			{
				if (!RasterizeGradient(Found->second.Gradient, Vertices, W, H, Pixels, Failure))
				{
					return;
				}
				Image = GenerateTexture({Pixels.data(), Pixels.size()}, {W, H});
				if (!Image)
				{
					return;
				}
			}
			auto Compiled = CreateGeometry(Vertices, Indices);
			if (!Compiled)
			{
				ReleaseTexture(Image);
				Fail("rmlui.gradient_geometry_failed");
				return;
			}
			const auto Handle = NextHandle++;
			Geometries.emplace(Handle, std::move(Compiled));
			Cached = Found->second.Geometry.emplace(Geometry, FShaderGeometry{Handle, Image}).first;
		}
		RenderGeometry(Cached->second.Geometry, Translation, Cached->second.Texture);
	}

	void FRenderer::ReleaseShader(Rml::CompiledShaderHandle Shader)
	{
		const auto Found = Shaders.find(Shader);
		if (Found == Shaders.end())
		{
			return;
		}
		auto Resources = std::move(Found->second.Geometry);
		Shaders.erase(Found);
		for (const auto& Entry : Resources)
		{
			ReleaseGeometry(Entry.second.Geometry);
			ReleaseTexture(Entry.second.Texture);
		}
	}

	FRenderer::FRenderer(FOptions InOptions) : Options(std::move(InOptions))
	{
	}

	bool FRenderer::Initialize(std::string& Error)
	{
		Error.clear();
		if (!Options.Device)
		{
			if (Options.Texture || Options.Surface)
			{
				Error = "rmlui.device_required";
				return false;
			}
			return true;
		}
		if (!Options.Device->IsOpen() || !Options.Device->GetCapabilities().bGuiBasic ||
		    static_cast<bool>(Options.Texture) == static_cast<bool>(Options.Surface) ||
		    (Options.Texture && !Options.Texture->IsOpen()) || (Options.Surface && !Options.Surface->IsOpen()))
		{
			Error = "rmlui.invalid_render_target: exactly one open target and a GUI-capable device are required";
			return false;
		}
		Renderer = Options.Device->CreateRenderer2D(Error);
		if (!Renderer)
		{
			return false;
		}
		Sampler = Options.Device->CreateSampler({}, Error);
		if (!Sampler)
		{
			// The owning backend closes partial initialization, retaining it if closing needs a retry.
			return false;
		}
		if (Options.Texture)
		{
			Width = Options.Texture->GetOptions().Width;
			Height = Options.Texture->GetOptions().Height;
		}
		return Composition.Initialize(Options, Renderer, Sampler, Error);
	}

	bool FRenderer::ValidateViewport(const Gui::FViewport& Viewport, std::string& Error)
	{
		Error.clear();
		if (Options.RenderOptions.bViewport)
		{
			if (!Graphics::ValidateRect(Options.RenderOptions.Viewport, Error))
			{
				return false;
			}
			if (Options.RenderOptions.Viewport.Width != Viewport.PixelWidth ||
			    Options.RenderOptions.Viewport.Height != Viewport.PixelHeight)
			{
				Error = "rmlui.viewport_size_mismatch";
				return false;
			}
		}
		else if (Options.Texture)
		{
			const auto Size = Options.Texture->GetOptions();
			if (Size.Width != Viewport.PixelWidth || Size.Height != Viewport.PixelHeight)
			{
				Error = "rmlui.target_size_mismatch";
				return false;
			}
		}
		Width = Viewport.PixelWidth;
		Height = Viewport.PixelHeight;
		return true;
	}

	void FRenderer::BeginFrame()
	{
		Draws.Clear();
		Masks.clear();
		bMask = false;
		Composition.Begin(Width, Height);
		Transform = {};
		bScissor = false;
		Failure = EffectFailure;
		std::string CleanupError;
		if (!CloseReleased(CleanupError) && Failure.empty())
		{
			Fail(CleanupError.empty() ? "rmlui.resource_close_failed" : CleanupError.c_str());
		}
	}

	std::shared_ptr<Graphics::IGeometry2D> FRenderer::CreateGeometry(
	    const std::vector<Graphics::FVertex2D>& Vertices, const std::vector<std::uint32_t>& Indices)
	{
		if (bBuildingDraws && !UploadSubmission)
		{
			auto* Submission = dynamic_cast<Graphics::IRenderSubmission2D*>(Renderer.get());
			if (Submission && !Submission->BeginSubmission(Failure)) return {};
			UploadSubmission = Submission;
		}
		return Renderer->CreateGeometry(Vertices, Indices, Failure);
	}

	bool FRenderer::BuildDraws(Rml::Context& Context, std::string& Error)
	{
		const auto DiscardUploads = [&](Graphics::IRenderSubmission2D& Submission)
		{
			std::string Ignored;
			try { Submission.CancelSubmission(Ignored); }
			catch (...)
			{ /* Keep invalidating failed uploads even if provider cleanup throws. */
			}
			Rml::ReleaseCompiledGeometry(this);
			while (!Painters.empty())
			{
				ReleasePaint(Painters.begin()->first);
			}
		};
		const auto FinishUploads = [&]()
		{
			auto* Submission = UploadSubmission;
			UploadSubmission = nullptr;
			if (!Submission) return true;
			try
			{
				if (Submission->EndSubmission(Error)) return true;
			}
			catch (...)
			{
				DiscardUploads(*Submission);
				throw;
			}
			// Failed submission must not leave either RmlUi or custom paint
			// caching uninitialized buffers, and the provider must be retryable.
			DiscardUploads(*Submission);
			return false;
		};
		bool bRendered = false;
		bBuildingDraws = true;
		try
		{
			bRendered = Context.Render();
		}
		catch (...)
		{
			bBuildingDraws = false;
			// Compilation callbacks may have published persistent geometry already.
			// Finish those uploads even if a later paint callback throws.
			FinishUploads();
			throw;
		}
		bBuildingDraws = false;
		if (!FinishUploads()) return false;
		if (!bRendered)
		{
			Error = "rmlui.render_failed";
		}
		return bRendered;
	}

	bool FRenderer::Submit(bool& bPresented, std::string& Error)
	{
		bPresented = false;
		Error.clear();
		const auto Finish = [&](bool bSuccess)
		{
			Draws.Clear();
			Composition.Cancel();
			Masks.clear();
			bMask = false;
			std::string CleanupError;
			if (!CloseReleased(CleanupError))
			{
				if (bSuccess || Error.empty())
				{
					Error = CleanupError.empty() ? "rmlui.resource_close_failed" : CleanupError;
				}
				return false;
			}
			return bSuccess;
		};
		if (!Failure.empty())
		{
			Error = Failure;
			return Finish(false);
		}
		if (!CanRender())
		{
			Error = "rmlui.layout_only: rendering requires a graphics device and target";
			return Finish(false);
		}
		if (Composition.HasWork() || !Masks.empty())
		{
			if (!FlushDraws())
			{
				Error = Failure;
				return Finish(false);
			}
			return Finish(Composition.Submit(bPresented, Error));
		}
		bool bSuccess = Options.Texture
		    ? Renderer->RenderTexture(Options.Texture, Draws, Options.RenderOptions, Error)
		    : Renderer->RenderSurface(Options.Surface, Draws, Options.RenderOptions, bPresented, Error);
		return Finish(bSuccess);
	}

	bool FRenderer::Close(std::string& Error)
	{
		Error.clear();
		Draws.Clear();
		Composition.Cancel();
		Masks.clear();
		Shaders.clear();
		Painters.clear();
		GeometrySources.clear();
		Filters.clear();
		FilterTextures.clear();
		MetadataTextures.clear();
		if (!Composition.Close(Error))
		{
			return false;
		}
		if (!CloseReleased(Error))
		{
			return false;
		}
		for (auto& Item : Geometries)
		{
			if (!Item.second->Close(Error))
			{
				return false;
			}
		}
		Geometries.clear();
		for (auto& Item : Textures)
		{
			if (!Item.second->Close(Error))
			{
				return false;
			}
		}
		Textures.clear();
		if (Sampler && !Sampler->Close(Error))
		{
			return false;
		}
		Sampler.reset();
		if (Renderer && !Renderer->Close(Error))
		{
			return false;
		}
		Renderer.reset();
		return true;
	}

	bool FRenderer::CanRender() const noexcept
	{
		return Renderer && Renderer->IsOpen();
	}

	bool FRenderer::CanComposite() const noexcept
	{
		return Composition.IsAvailable();
	}

	bool FRenderer::SetResourceReader(Gui::FResourceReader Reader, std::string& Error)
	{
		Error.clear();
		if (!Textures.empty() || !MetadataTextures.empty())
		{
			Error = "rmlui.resources_already_in_use";
			return false;
		}
		Options.ReadResource = std::move(Reader);
		return true;
	}

	bool FRenderer::CheckResources(const std::vector<Gui::FBackendNode>& Nodes, std::string& Error) const
	{
		if (!Failure.empty())
		{
			Error = Failure;
			return false;
		}
		if (ResourceFailures.empty())
		{
			return true;
		}
		std::set<std::string> References;
		for (const auto& Node : Nodes)
		{
			References.insert(Node.Properties.Image);
			for (const auto* Style : {&Node.Properties.BackgroundImage, &Node.Properties.MaskImage})
			{
				std::string Resolved, Unused;
				Gui::ResolveGuiStyleImages(
				    {}, *Style,
				    [&](const auto&, const auto& Reference, auto& Identity, auto&)
				{
					References.insert(Reference);
					Identity = Reference;
					return true;
				},
				    Resolved, Unused);
			}
		}
		for (const auto& Failure : ResourceFailures)
		{
			if (References.count(Failure.first))
			{
				Error = Failure.second;
				return false;
			}
		}
		return true;
	}

	void FRenderer::Fail(const char* Message)
	{
		if (Failure.empty())
		{
			Failure = Message;
		}
	}

	Rml::TextureHandle FRenderer::LoadImageTexture(Rml::Vector2i& Dimensions, const Rml::String& Source)
	{
		Dimensions = {};
		if (!Failure.empty())
		{
			return 0;
		}
		std::vector<std::uint8_t> Bytes, Pixels;
		std::int32_t W = 0, H = 0;
		const auto& Read = Options.ReadResource;
		if (!(Read ? Read(Source, Bytes, Failure) : Gui::ReadGuiResourceFile(Source, Bytes, Failure)))
		{
			return 0;
		}
		if (Bytes.size() > 64 * 1024 * 1024)
		{
			Fail("rmlui.image_source_limit");
			return 0;
		}
		bool Decoded = false;
		if (Options.DecodeImage)
		{
			Gui::FDecodedImage Result;
			Decoded = Options.DecodeImage(Source, Bytes, Result, Failure);
			W = Result.Width;
			H = Result.Height;
			Pixels = std::move(Result.Rgba);
			if (!Decoded && Failure.empty())
			{
				Gui::FDecodedImage DecodedImage;
				if (!Options.Images)
				{
					Failure = "rmlui.image_service_required";
					return 0;
				}
				Decoded = Options.Images->Decode(Bytes, DecodedImage, Failure);
				W = DecodedImage.Width;
				H = DecodedImage.Height;
				Pixels = std::move(DecodedImage.Rgba);
			}
		}
		else
		{
			Gui::FDecodedImage DecodedImage;
			if (!Options.Images)
			{
				Failure = "rmlui.image_service_required";
				return 0;
			}
			Decoded = Options.Images->Decode(Bytes, DecodedImage, Failure);
			W = DecodedImage.Width;
			H = DecodedImage.Height;
			Pixels = std::move(DecodedImage.Rgba);
		}
		if (!Decoded || W < 1 || H < 1 || W > 16384 || H > 16384 ||
		    static_cast<std::uint64_t>(W) * H > 16 * 1024 * 1024 ||
		    Pixels.size() != static_cast<std::uint64_t>(W) * H * 4)
		{
			if (Failure.empty())
			{
				Fail("rmlui.image_load_failed");
			}
			return 0;
		}
		if (!CanRender())
		{
			const auto Handle = NextHandle++;
			MetadataTextures.insert(Handle);
			Dimensions = {W, H};
			return Handle;
		}
		for (std::size_t I = 0; I < Pixels.size(); I += 4)
		{
			for (std::size_t Channel = 0; Channel < 3; ++Channel)
			{
				Pixels[I + Channel] =
				    static_cast<std::uint8_t>((static_cast<unsigned>(Pixels[I + Channel]) * Pixels[I + 3] + 127) / 255);
			}
		}
		const auto Handle = GenerateTexture({Pixels.data(), Pixels.size()}, {W, H});
		if (Handle)
		{
			Dimensions = {W, H};
		}
		return Handle;
	}

	bool FRenderer::CloseReleased(std::string& Error)
	{
		return CloseResources(ReleasedGeometries, Error) && CloseResources(ReleasedTextures, Error);
	}

	bool FRenderer::FlushDraws()
	{
		if (!Failure.empty() || !Composition.AddDraw(Draws, Failure, bMask ? Masks : std::vector<FClipMask>{}))
		{
			return false;
		}
		Draws.Clear();
		return true;
	}

	Rml::TextureHandle FRenderer::CaptureLayer(bool FullSize)
	{
		if (!CanComposite() || !FlushDraws())
		{
			Fail("rmlui.layer_capture_unavailable");
			return 0;
		}
		Graphics::FRect Region{0, 0, Width, Height};
		if (!FullSize && bScissor)
		{
			Region = Graphics::IntersectRects(Region, Scissor);
		}
		if (Region.Width < 1 || Region.Height < 1)
		{
			Fail("rmlui.empty_capture");
			return 0;
		}
		auto Texture =
		    Options.Device->CreateTexture({Region.Width, Region.Height, Graphics::ETextureFormat::Rgba8Srgb}, Failure);
		if (!Texture)
		{
			Fail("rmlui.capture_texture_failed");
			return 0;
		}
		const auto Handle = NextHandle++;
		Textures.emplace(Handle, Texture);
		if (!Composition.Capture(std::move(Texture), Region, bScissor, Scissor, Failure))
		{
			ReleaseTexture(Handle);
			return 0;
		}
		return Handle;
	}

	Graphics::FColor FRenderer::ConvertColor(Rml::ColourbPremultiplied Color) const
	{
		Graphics::FColor Result{Color.red / 255.0f, Color.green / 255.0f, Color.blue / 255.0f, Color.alpha / 255.0f};
		if (Result.A > 0)
		{
			const auto Decode = [Alpha = Result.A](float Value)
			{
				const float Straight = std::clamp(Value / Alpha, 0.0f, 1.0f);
				return Alpha *
				    (Straight <= 0.04045f ? Straight / 12.92f : std::pow((Straight + 0.055f) / 1.055f, 2.4f));
			};
			Result.R = Decode(Result.R);
			Result.G = Decode(Result.G);
			Result.B = Decode(Result.B);
		}
		return Result;
	}
} // namespace VeroScript::RmlUi

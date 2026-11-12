// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRmlUi.h"
#include "VeroScriptRmlUiComposition.h"
#include "VeroScriptRmlUiGradient.h"
#include <RmlUi/Core/RenderInterface.h>
#include <map>
#include <vector>
#include <set>

namespace VeroScript::RmlUi
{
	class FRenderer final : public Rml::RenderInterface
	{
	public:
		//~ Begin Rml::RenderInterface Interface
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> Vertices,
													Rml::Span<const int> Indices) override;
		void RenderGeometry(Rml::CompiledGeometryHandle Geometry, Rml::Vector2f Translation,
							Rml::TextureHandle Texture) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle Geometry) override;
		Rml::TextureHandle LoadTexture(Rml::Vector2i& Dimensions, const Rml::String& Source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> Source, Rml::Vector2i Dimensions) override;
		void ReleaseTexture(Rml::TextureHandle Texture) override;
		void EnableScissorRegion(bool bEnable) override;
		void SetScissorRegion(Rml::Rectanglei Region) override;
		void SetTransform(const Rml::Matrix4f* Transform) override;
		void EnableClipMask(bool bEnable) override;
		void RenderToClipMask(Rml::ClipMaskOperation Operation, Rml::CompiledGeometryHandle Geometry,
							  Rml::Vector2f Translation) override;
		Rml::LayerHandle PushLayer() override;
		void CompositeLayers(Rml::LayerHandle Source, Rml::LayerHandle Destination, Rml::BlendMode Blend,
							 Rml::Span<const Rml::CompiledFilterHandle> Filters) override;
		void PopLayer() override;
		Rml::TextureHandle SaveLayerAsTexture() override;
		Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
		Rml::CompiledFilterHandle CompileFilter(const Rml::String& Name, const Rml::Dictionary& Parameters) override;
		void ReleaseFilter(Rml::CompiledFilterHandle Filter) override;
		Rml::CompiledShaderHandle CompileShader(const Rml::String& Name, const Rml::Dictionary& Parameters) override;
		void RenderShader(Rml::CompiledShaderHandle Shader, Rml::CompiledGeometryHandle Geometry,
						  Rml::Vector2f Translation, Rml::TextureHandle Texture) override;
		void ReleaseShader(Rml::CompiledShaderHandle Shader) override;
		//~ End Rml::RenderInterface Interface

		explicit FRenderer(FOptions Options);
		bool Initialize(std::string& Error);
		bool ValidateViewport(const Gui::FViewport& Viewport, std::string& Error);
		void BeginFrame();
		bool BuildDraws(Rml::Context& Context, std::string& Error);
		bool Submit(bool& bPresented, std::string& Error);
		bool Close(std::string& Error);
		bool CanRender() const noexcept;
		bool CanComposite() const noexcept;
		bool HasEffectFailure() const noexcept
		{
			return !EffectFailure.empty();
		}
		void ResetEffectFailure()
		{
			EffectFailure.clear();
			Failure.clear();
		}
		bool SetResourceReader(Gui::FResourceReader Reader, std::string& Error);
		bool CheckResources(const std::vector<Gui::FBackendNode>& Nodes, std::string& Error) const;
		void RenderPaint(std::uint64_t Identity, const Gui::FPaintCallback& Callback, const Gui::FRect& Bounds,
						 Rml::Vector2f Offset, double Scale);
		void ReleasePaint(std::uint64_t Identity);

	private:
		struct FShaderGeometry
		{
			Rml::CompiledGeometryHandle Geometry = 0;
			Rml::TextureHandle Texture = 0;
			Rml::CompiledGeometryHandle Clip = 0;
		};
		struct FShader
		{
			FGradient Gradient;
			std::map<Rml::CompiledGeometryHandle, FShaderGeometry> Geometry;
		};
		struct FPainter
		{
			std::vector<Gui::FPaintMesh> Meshes;
			std::vector<FShaderGeometry> Resources;
			double Scale = 1;
			bool bReady = false;
			bool bUnbatched = false;
		};
		void ReleasePaintResource(FShaderGeometry& Resource);
		bool CompilePaint(std::uint64_t Identity, const std::vector<Gui::FPaintMesh>& Meshes, double Scale,
						  bool& bGeometryFailed);
		std::shared_ptr<Graphics::IGeometry2D> CreateGeometry(const std::vector<Graphics::FVertex2D>& Vertices,
		    const std::vector<std::uint32_t>& Indices);
		void Fail(const char* Message);
		Rml::TextureHandle LoadImageTexture(Rml::Vector2i& Dimensions, const Rml::String& Source);
		bool CloseReleased(std::string& Error);
		bool FlushDraws();
		Rml::TextureHandle CaptureLayer(bool FullSize);
		Graphics::FColor ConvertColor(Rml::ColourbPremultiplied Color) const;

		FOptions Options;
		std::shared_ptr<Graphics::IRenderer2D> Renderer;
		Graphics::IRenderSubmission2D* UploadSubmission = nullptr;
		bool bBuildingDraws = false;
		std::shared_ptr<Graphics::ISampler> Sampler;
		std::map<Rml::CompiledGeometryHandle, std::shared_ptr<Graphics::IGeometry2D>> Geometries;
		// RmlUi guarantees immutable source spans until ReleaseGeometry; retain no extra copy of text meshes.
		std::map<Rml::CompiledGeometryHandle, std::pair<Rml::Span<const Rml::Vertex>, Rml::Span<const int>>>
			GeometrySources;
		std::map<Rml::CompiledShaderHandle, FShader> Shaders;
		std::map<std::uint64_t, FPainter> Painters;
		std::map<Rml::TextureHandle, std::shared_ptr<Graphics::ITexture>> Textures;
		std::set<Rml::TextureHandle> MetadataTextures;
		std::map<std::string, std::string> ResourceFailures;
		std::vector<std::shared_ptr<Graphics::IGeometry2D>> ReleasedGeometries;
		std::vector<std::shared_ptr<Graphics::ITexture>> ReleasedTextures;
		Graphics::FDrawList2D Draws;
		FCompositionFrame Composition;
		std::map<Rml::CompiledFilterHandle, std::vector<Graphics::FEffect2D>> Filters;
		std::map<Rml::CompiledFilterHandle, Rml::TextureHandle> FilterTextures;
		std::int64_t Width = 0, Height = 0;
		Graphics::FTransform2D Transform;
		Graphics::FRect Scissor;
		std::uintptr_t NextHandle = 1;
		std::string Failure;
		std::string EffectFailure;
		bool bScissor = false;
		bool bMask = false;
		std::vector<FClipMask> Masks;
	};
} // namespace VeroScript::RmlUi

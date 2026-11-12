// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUiPaint.h"
#include <exception>
#include "VeroScriptRmlUiRenderer.h"
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementDocument.h>
#include <cmath>
#include <algorithm>
#include <utility>

namespace VeroScript::RmlUi
{
	namespace
	{
		bool SamePaintState(const Gui::FPaintMesh& A, const Gui::FPaintMesh& B)
		{
			return A.Image == B.Image && A.bClip == B.bClip &&
			    (!A.bClip || (A.Clip.X == B.Clip.X && A.Clip.Y == B.Clip.Y && A.Clip.Width == B.Clip.Width && A.Clip.Height == B.Clip.Height));
		}
		bool SamePaintMesh(const Gui::FPaintMesh& A, const Gui::FPaintMesh& B)
		{
			return SamePaintState(A, B) && A.Indices == B.Indices && A.Vertices.size() == B.Vertices.size() &&
			    std::equal(A.Vertices.begin(), A.Vertices.end(), B.Vertices.begin(), [](const auto& X, const auto& Y)
			{
				return X.X == Y.X && X.Y == Y.Y && X.U == Y.U && X.V == Y.V &&
				    Gui::PropertyValuesEqual(X.Color, Y.Color);
			});
		}
		std::vector<Gui::FPaintMesh> BatchPaint(const std::vector<Gui::FPaintMesh>& Meshes)
		{
			std::vector<Gui::FPaintMesh> Result;
			for (const auto& Mesh : Meshes)
			{
				if (Result.empty() || !SamePaintState(Result.back(), Mesh))
				{
					Result.push_back(Mesh);
					continue;
				}
				auto& Batch = Result.back();
				// FPaintContext bounds the complete canvas to 16 MiB, below uint32 index range.
				const auto Offset = static_cast<std::uint32_t>(Batch.Vertices.size());
				Batch.Vertices.insert(Batch.Vertices.end(), Mesh.Vertices.begin(), Mesh.Vertices.end());
				for (const auto Index : Mesh.Indices)
				{
					Batch.Indices.push_back(Offset + Index);
				}
			}
			return Result;
		}
	} // namespace

	FGuiElement::~FGuiElement()
	{
		if (Renderer)
		{
			Renderer->ReleasePaint(Identity);
		}
	}

	FGuiElement::FGuiElement(const Rml::String& Tag) : Element(Tag)
	{
	}

	void FGuiElement::Configure(FRenderer& InRenderer, std::uint64_t InIdentity, double InScale,
	    Gui::FPaintCallback InCallback)
	{
		Renderer = &InRenderer;
		Identity = InIdentity;
		Scale = InScale;
		Callback = std::move(InCallback);
	}

	bool FGuiElement::SynchronizeFlow(Rml::ElementDocument& Document,
	    const std::vector<Rml::Element*>& Items, double Gap, std::string& Error)
	{
		const auto Count = Gap > 0 && Items.size() > 1 ? Items.size() - 1 : 0;
		while (FlowGaps.size() > Count)
		{
			RemoveChild(FlowGaps.back());
			FlowGaps.pop_back();
		}
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			if (Index == FlowGaps.size())
			{
				auto Element = Document.CreateElement("vsgui-flow-gap");
				if (!Element || !Element->SetProperty("display", "block"))
				{
					Error = "rmlui.flow_gap_failed";
					return false;
				}
				FlowGaps.push_back(InsertBefore(std::move(Element), Items[Index + 1]));
			}
			auto* Element = FlowGaps[Index];
			if (Element->GetNextSibling() != Items[Index + 1])
			{
				InsertBefore(RemoveChild(Element), Items[Index + 1]);
			}
			const Rml::Property Height(static_cast<float>(Gap), Rml::Unit::DP);
			const auto* Previous = Element->GetLocalProperty(Rml::PropertyId::Height);
			if ((!Previous || *Previous != Height) && !Element->SetProperty(Rml::PropertyId::Height, Height))
			{
				Error = "rmlui.flow_gap_failed";
				return false;
			}
		}
		return true;
	}

	void FGuiElement::OnRender()
	{
		if (Renderer && Callback)
		{
			const auto Size = GetBox().GetSize(Rml::BoxArea::Content);
			Renderer->RenderPaint(Identity, Callback, {0, 0, Size.x / Scale, Size.y / Scale},
			    GetAbsoluteOffset(Rml::BoxArea::Content), Scale);
		}
	}
	void RegisterGuiElement()
	{
		static auto* Instancer = new Rml::ElementInstancerGeneric<FGuiElement>;
		Rml::Factory::RegisterElementInstancer("vsgui-widget", Instancer);
	}
	void FRenderer::RenderPaint(std::uint64_t Identity, const Gui::FPaintCallback& Callback, const Gui::FRect& Bounds,
	    Rml::Vector2f Offset, double Scale)
	try
	{
		if (!Failure.empty()) return;
		Gui::FPaintContext Canvas(Bounds);
		if (!Callback(Canvas, Failure) || !Canvas.Finish(Failure))
		{
			if (Failure.empty())
			{
				Fail("rmlui.paint_failed");
			}
			return;
		}
		if (Canvas.GetMeshes().empty() && !Painters.count(Identity)) return;
		const auto Previous = Painters.find(Identity);
		const bool bBatch = Previous == Painters.end() || !Previous->second.bUnbatched;
		const auto& Original = Canvas.GetMeshes();
		std::vector<Gui::FPaintMesh> Batched;
		const auto* Meshes = &Original;
		// Most control skins contain only one mesh. Borrow the canvas unless
		// adjacent compatible commands actually require a combined mesh.
		if (bBatch && std::adjacent_find(Original.begin(), Original.end(), SamePaintState) != Original.end())
		{
			Batched = BatchPaint(Original);
			Meshes = &Batched;
		}
		bool bGeometryFailed = false;
		if (!CompilePaint(Identity, *Meshes, Scale, bGeometryFailed))
		{
			if (!bGeometryFailed || !bBatch || Meshes->size() == Original.size()) return;
			// A host may impose a geometry limit smaller than a batch. Retry the original
			// commands without invoking OnPaint twice, then retain that choice for this painter.
			Failure.clear();
			ReleasePaint(Identity);
			if (!Failure.empty()) return;
			if (!CompilePaint(Identity, Canvas.GetMeshes(), Scale, bGeometryFailed)) return;
			Painters.at(Identity).bUnbatched = true;
		}
		const auto Found = Painters.find(Identity);
		for (std::size_t I = 0; I < Found->second.Meshes.size(); ++I)
		{
			const auto& Resource = Found->second.Resources[I];
			if (!Resource.Clip)
			{
				RenderGeometry(Resource.Geometry, Offset, Resource.Texture);
				continue;
			}
			if (!CanComposite())
			{
				Fail("rmlui.transformed_clip_unavailable");
				return;
			}
			if (!FlushDraws())
			{
				return;
			}
			const auto PreviousMasks = Masks;
			const bool MaskEnabled = bMask;
			RenderToClipMask(MaskEnabled && !Masks.empty() ? Rml::ClipMaskOperation::Intersect
			                                               : Rml::ClipMaskOperation::Set,
			    Resource.Clip, Offset);
			bMask = true;
			RenderGeometry(Resource.Geometry, Offset, Resource.Texture);
			FlushDraws();
			Masks = PreviousMasks;
			bMask = MaskEnabled;
		}
	}
	catch (const std::exception& Exception)
	{
		Fail((std::string("rmlui.paint_exception: ") + Exception.what()).c_str());
	}
	catch (...)
	{
		Fail("rmlui.paint_exception");
	}

	void FRenderer::ReleasePaint(std::uint64_t Identity)
	{
		const auto Found = Painters.find(Identity);
		if (Found == Painters.end())
		{
			return;
		}
		auto Resources = std::move(Found->second.Resources);
		Painters.erase(Found);
		for (auto& Resource : Resources)
		{
			ReleasePaintResource(Resource);
		}
	}

	void FRenderer::ReleasePaintResource(FShaderGeometry& Resource)
	{
		if (Resource.Geometry)
		{
			ReleaseGeometry(std::exchange(Resource.Geometry, 0));
		}
		if (Resource.Clip)
		{
			ReleaseGeometry(std::exchange(Resource.Clip, 0));
		}
		if (Resource.Texture)
		{
			ReleaseTexture(std::exchange(Resource.Texture, 0));
		}
	}

	bool FRenderer::CompilePaint(std::uint64_t Identity, const std::vector<Gui::FPaintMesh>& Meshes, double Scale,
	    bool& bGeometryFailed)
	{
		bGeometryFailed = false;
		auto Found = Painters.find(Identity);
		if (Found == Painters.end() || !Found->second.bReady || Found->second.Scale != Scale)
		{
			ReleasePaint(Identity);
			Found = Painters.emplace(Identity, FPainter{}).first;
			Found->second.Scale = Scale;
		}
		auto& Cache = Found->second;
		const bool bReady = Cache.bReady;
		Cache.bReady = false;
		while (Cache.Resources.size() > Meshes.size())
		{
			ReleasePaintResource(Cache.Resources.back());
			Cache.Resources.pop_back();
		}
		Cache.Resources.resize(Meshes.size());
		Cache.Meshes.resize(Meshes.size());
		for (std::size_t I = 0; I < Meshes.size(); ++I)
		{
			if (bReady && Cache.Resources[I].Geometry && SamePaintMesh(Cache.Meshes[I], Meshes[I]))
			{
				continue;
			}
			// Geometry animation does not change the image resource's identity.
			const auto Texture = bReady && Cache.Meshes[I].Image == Meshes[I].Image
			    ? std::exchange(Cache.Resources[I].Texture, 0)
			    : 0;
			ReleasePaintResource(Cache.Resources[I]);
			Cache.Resources[I].Texture = Texture;
			Cache.Meshes[I] = Meshes[I];
			const auto& Mesh = Cache.Meshes[I];
			{
				auto Vertices = Mesh.Vertices;
				for (auto& Vertex : Vertices)
				{
					Vertex.X *= static_cast<float>(Scale);
					Vertex.Y *= static_cast<float>(Scale);
					Vertex.Color.R *= Vertex.Color.A;
					Vertex.Color.G *= Vertex.Color.A;
					Vertex.Color.B *= Vertex.Color.A;
				}
				auto Geometry = CreateGeometry(Vertices, Mesh.Indices);
				if (!Geometry)
				{
					bGeometryFailed = true;
					Fail("rmlui.paint_geometry_failed");
					return false;
				}
				const auto Handle = NextHandle++;
				Geometries.emplace(Handle, std::move(Geometry));
				Cache.Resources[I].Geometry = Handle;
				const auto& Clip = Mesh.Clip;
				// A mesh contained by its local clip needs no mask, including after a shared transform.
				const bool bNeedsClip = Mesh.bClip && std::any_of(Mesh.Vertices.begin(), Mesh.Vertices.end(), [&](const auto& Vertex)
				{
					return Vertex.X < Clip.X || Vertex.Y < Clip.Y ||
					    Vertex.X > Clip.X + Clip.Width ||
					    Vertex.Y > Clip.Y + Clip.Height;
				});
				if (bNeedsClip)
				{
					const auto& R = Clip;
					const auto X = static_cast<float>(R.X * Scale), Y = static_cast<float>(R.Y * Scale),
					           W = static_cast<float>(R.Width * Scale), H = static_cast<float>(R.Height * Scale);
					const Graphics::FColor White{1, 1, 1, 1};
					auto ClipGeometry = CreateGeometry({{X, Y, 0, 0, White},
					                                                 {X + W, Y, 0, 0, White},
					                                                 {X + W, Y + H, 0, 0, White},
					                                                 {X, Y + H, 0, 0, White}},
					    {0, 1, 2, 0, 2, 3});
					if (!ClipGeometry)
					{
						Fail("rmlui.paint_clip_failed");
						return false;
					}
					const auto ClipHandle = NextHandle++;
					Geometries.emplace(ClipHandle, std::move(ClipGeometry));
					Cache.Resources[I].Clip = ClipHandle;
				}
				if (!Mesh.Image.empty() && !Cache.Resources[I].Texture)
				{
					Rml::Vector2i Dimensions;
					const auto Image = LoadTexture(Dimensions, Mesh.Image);
					if (!Image)
					{
						return false;
					}
					Cache.Resources[I].Texture = Image;
				}
			}
		}
		Cache.bReady = true;
		return Failure.empty();
	}
} // namespace VeroScript::RmlUi

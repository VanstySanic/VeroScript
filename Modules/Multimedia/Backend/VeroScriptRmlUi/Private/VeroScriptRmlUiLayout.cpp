// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUiLayout.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <algorithm>
#include <map>

namespace VeroScript::RmlUi
{
	namespace
	{
		bool SetDimension(Rml::Element &Element, Rml::PropertyId Id, double Value)
		{
			const Rml::Property Next(static_cast<float>(Value), Rml::Unit::DP);
			const auto *Previous = Element.GetLocalProperty(Id);
			if (Previous && *Previous == Next)
			{
				return false;
			}
			Element.SetProperty(Id, Next);
			return true;
		}
		bool Settle(Rml::Context &Context, bool bChanged, std::string &Error)
		{
			if (bChanged && !Context.Update())
			{
				Error = "rmlui.update_failed";
				return false;
			}
			return true;
		}
	} // namespace

	bool UpdateContainerLayouts(Rml::Context &Context, const FLayoutElements &Elements,
	    const std::vector<Gui::FBackendNode> &Nodes,
	    const std::unordered_map<std::uint64_t, std::size_t> &NodeIndices,
	    const Gui::FViewport &Viewport,
	    std::string &Error)
	{
		if (std::none_of(Nodes.begin(), Nodes.end(),
		        [](const auto &Node)
		{
			return Node.Control.Kind == Gui::EControlKind::Grid ||
			    Node.Control.Kind == Gui::EControlKind::Splitter;
		}))
		{
			return true;
		}
		std::unordered_map<std::uint64_t, std::vector<const Gui::FBackendNode *>> Children;
		std::vector<std::size_t> Depth;
		Depth.reserve(Nodes.size());
		std::map<std::size_t, std::vector<const Gui::FBackendNode *>> Levels;
		for (const auto &Node : Nodes)
		{
			Children[Node.Parent].push_back(&Node);
			const bool bContainer =
			    Node.Control.Kind == Gui::EControlKind::Grid || Node.Control.Kind == Gui::EControlKind::Splitter;
			Depth.push_back((Node.Parent ? Depth[NodeIndices.at(Node.Parent)] : 0) + (bContainer ? 1 : 0));
			if (bContainer)
			{
				Levels[Depth.back()].push_back(&Node);
			}
		}
		// A splitter assigns both dimensions of its panes. Direct child containers
		// can use those definite content sizes without formatting the subtree first.
		std::unordered_map<std::uint64_t, Rml::Vector2f> PendingSizes;
		bool bPendingLayout = false;
		for (const auto &Level : Levels)
		{
			if (bPendingLayout && std::any_of(Level.second.begin(), Level.second.end(),
			        [&](const auto* Parent) { return !PendingSizes.count(Parent->Id); }))
			{
				if (!Settle(Context, true, Error)) return false;
				PendingSizes.clear();
				bPendingLayout = false;
			}
			bool bChanged = false;
			for (const auto *Parent : Level.second)
			{
				const auto Pending = PendingSizes.find(Parent->Id);
				const auto Size = Pending != PendingSizes.end() ? Pending->second :
				    Elements.at(Parent->Id)->GetBox().GetSize(Rml::BoxArea::Content) /
				        static_cast<float>(Viewport.DisplayScale);
				std::size_t ChildIndex = 0;
				if (Parent->Control.Kind == Gui::EControlKind::Grid)
				{
					const auto &O = Parent->Control;
					const double Width = std::max(0.0, (Size.x - Parent->Properties.Gap * (O.Columns - 1)) / O.Columns);
					auto &Element = *Elements.at(Parent->Id);
					// Literal image/text content participates in the same cell flow as authored children.
					for (int Index = 0; Index < Element.GetNumChildren(); ++Index)
					{
						bChanged = SetDimension(*Element.GetChild(Index), Rml::PropertyId::Width, Width) || bChanged;
					}
					continue;
				}
				for (const auto *Child : Children[Parent->Id])
				{
					if (Gui::IsFloatingControl(Child->Control.Kind) ||
					    Child->Properties.Visibility == Gui::EVisibility::Collapsed)
					{
						continue;
					}
					auto &Element = *Elements.at(Child->Id);
					const auto &O = Parent->Control;
					{
						const double Divider = std::min(O.DividerSize, double(O.bVertical ? Size.y : Size.x));
						const double Space = std::max(0.0, (O.bVertical ? Size.y : Size.x) - Divider);
						const bool bFirst = ChildIndex++ == 0;
						const double Extent = Space * (bFirst ? O.Value : 1 - O.Value);
						const double Offset = bFirst ? 0 : Space * O.Value + Divider;
						const double Width = O.bVertical ? Size.x : Extent;
						const double Height = O.bVertical ? Extent : Size.y;
						bChanged =
						    SetDimension(Element, Rml::PropertyId::Width, Width) || bChanged;
						bChanged =
						    SetDimension(Element, Rml::PropertyId::Height, Height) || bChanged;
						bChanged = SetDimension(Element, Rml::PropertyId::Left,
						               Parent->Properties.Padding.Left + (O.bVertical ? 0 : Offset)) ||
						    bChanged;
						bChanged = SetDimension(Element, Rml::PropertyId::Top,
						               Parent->Properties.Padding.Top + (O.bVertical ? Offset : 0)) ||
						    bChanged;
						if (Child->Control.Kind == Gui::EControlKind::Grid ||
						    Child->Control.Kind == Gui::EControlKind::Splitter)
						{
							const auto& Box = Element.GetBox();
							const float Scale = static_cast<float>(Viewport.DisplayScale);
							PendingSizes[Child->Id] = {
							    std::max(0.0f, static_cast<float>(Width) * Scale -
							        Box.GetSizeAcross(Rml::BoxDirection::Horizontal, Rml::BoxArea::Border, Rml::BoxArea::Padding)) / Scale,
							    std::max(0.0f, static_cast<float>(Height) * Scale -
							        Box.GetSizeAcross(Rml::BoxDirection::Vertical, Rml::BoxArea::Border, Rml::BoxArea::Padding)) / Scale};
						}
					}
				}
			}
			bPendingLayout = bPendingLayout || bChanged;
		}
		return Settle(Context, bPendingLayout, Error);
	}

	bool PositionFloatingControls(Rml::Context &Context, const FLayoutElements &Elements,
	    const std::vector<Gui::FBackendNode> &Nodes, const Gui::FViewport &Viewport,
	    std::string &Error)
	{
		if (std::none_of(Nodes.begin(), Nodes.end(),
		        [](const auto &Node)
		{
			return Gui::IsFloatingControl(Node.Control.Kind) && Node.Control.bOpen;
		}))
		{
			return true;
		}
		const float Scale = static_cast<float>(Viewport.DisplayScale);
		const double Width = Viewport.PixelWidth / Viewport.DisplayScale,
		             Height = Viewport.PixelHeight / Viewport.DisplayScale;
		std::unordered_map<std::uint64_t, std::size_t> Depth;
		std::map<std::size_t, std::vector<const Gui::FBackendNode *>> Levels;
		bool bChanged = false;
		for (const auto &Node : Nodes)
		{
			const bool bFloating = Gui::IsFloatingControl(Node.Control.Kind);
			Depth[Node.Id] = (Node.Parent ? Depth.at(Node.Parent) : 0) + (bFloating ? 1 : 0);
			if (bFloating && Node.Control.bOpen)
			{
				Levels[Depth.at(Node.Id)].push_back(&Node);
				auto &Element = *Elements.at(Node.Id);
				bChanged = SetDimension(Element, Rml::PropertyId::MaxWidth, Width) || bChanged;
				bChanged = SetDimension(Element, Rml::PropertyId::MaxHeight, Height) || bChanged;
				const auto &Authored = Node.Properties.Width;
				Rml::Property Desired = Authored.Unit == Gui::ELengthUnit::Auto ? Rml::Property(0, Rml::Unit::KEYWORD)
				                                                                : Rml::Property(float(Authored.Value), Authored.Unit == Gui::ELengthUnit::Percent ? Rml::Unit::PERCENT : Rml::Unit::DP);
				if (Node.Control.bMatchAnchorWidth && Node.Parent)
				{
					Desired = Rml::Property(Elements.at(Node.Parent)->GetBox().GetSize(Rml::BoxArea::Border).x / Scale, Rml::Unit::DP);
				}
				const auto *Previous = Element.GetLocalProperty(Rml::PropertyId::Width);
				if (!Previous || *Previous != Desired)
				{
					Element.SetProperty(Rml::PropertyId::Width, Desired);
					bChanged = true;
				}
			}
		}
		if (!Settle(Context, bChanged, Error))
		{
			return false;
		}
		// Scroll offsets have already settled. Nested portals use their parent's completed position.
		for (const auto &Level : Levels)
		{
			bChanged = false;
			for (const auto *Node : Level.second)
			{
				auto &Element = *Elements.at(Node->Id);
				const auto Anchor = Node->Parent
				    ? Elements.at(Node->Parent)->GetAbsoluteOffset(Rml::BoxArea::Border) / Scale
				    : Rml::Vector2f();
				const auto AnchorSize = Node->Parent
				    ? Elements.at(Node->Parent)->GetBox().GetSize(Rml::BoxArea::Border) / Scale
				    : Rml::Vector2f();
				const auto Size = Element.GetBox().GetSize(Rml::BoxArea::Border) / Scale;
				double X = Anchor.x + Node->Control.OffsetX, Y = Anchor.y + AnchorSize.y + Node->Control.OffsetY;
				if (Node->Control.Placement == Gui::FControlOptions::EPlacement::Center)
				{
					X = (Width - Size.x) * .5 + Node->Control.OffsetX;
					Y = (Height - Size.y) * .5 + Node->Control.OffsetY;
				}
				else if (Node->Control.Placement == Gui::FControlOptions::EPlacement::Right)
				{
					X = Anchor.x + AnchorSize.x + Node->Control.OffsetX;
					Y = Anchor.y + Node->Control.OffsetY;
					if (X + Size.x > Width && Anchor.x - Size.x - Node->Control.OffsetX >= 0)
					{
						X = Anchor.x - Size.x - Node->Control.OffsetX;
					}
				}
				else if (Y + Size.y > Height && Anchor.y - Size.y - Node->Control.OffsetY >= 0)
				{
					Y = Anchor.y - Size.y - Node->Control.OffsetY;
				}
				bChanged =
				    SetDimension(Element, Rml::PropertyId::Left, std::clamp(X, 0.0, std::max(0.0, Width - Size.x))) ||
				    bChanged;
				bChanged =
				    SetDimension(Element, Rml::PropertyId::Top, std::clamp(Y, 0.0, std::max(0.0, Height - Size.y))) ||
				    bChanged;
			}
			if (!Settle(Context, bChanged, Error))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::RmlUi

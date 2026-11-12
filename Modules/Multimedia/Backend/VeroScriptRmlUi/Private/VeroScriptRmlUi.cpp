// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUi.h"
#include "Font/VeroScriptRmlUiFontBridge.h"
#include "VeroScriptRmlUiContent.h"
#include "VeroScriptRmlUiControls.h"
#include "VeroScriptRmlUiLayout.h"
#include "VeroScriptRmlUiPaint.h"
#include "VeroScriptRmlUiRenderer.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Filter.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/StyleSheetTypes.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace VeroScript::RmlUi
{
	namespace
	{
		class FSystem final : public Rml::SystemInterface
		{
		public:
			void JoinPath(Rml::String &Translated, const Rml::String &, const Rml::String &Path) override
			{
				// Gui has already resolved filesystem and opaque resource identities. Preserve their root.
				Translated = Path;
			}
			double GetElapsedTime() override
			{
				return Time;
			}
			void SetClipboardText(const Rml::String &Text) override
			{
				try
				{
					if (!ClipboardServices || !ClipboardServices->WriteClipboard)
					{
						ClipboardError = "gui.clipboard_unavailable";
						return;
					}
					if (!ClipboardServices->WriteClipboard(Text, ClipboardError) && ClipboardError.empty())
					{
						ClipboardError = "gui.clipboard_write_failed";
					}
				}
				catch (...)
				{
					ClipboardError = "gui.clipboard_write_failed";
				}
			}
			void GetClipboardText(Rml::String &Text) override
			{
				try
				{
					if (!ClipboardServices || !ClipboardServices->ReadClipboard)
					{
						ClipboardError = "gui.clipboard_unavailable";
						return;
					}
					if (!ClipboardServices->ReadClipboard(Text, ClipboardError) && ClipboardError.empty())
					{
						ClipboardError = "gui.clipboard_read_failed";
					}
					if (!Gui::ValidateGuiText(Text) || Text.size() > 1024 * 1024)
					{
						ClipboardError = "gui.text_limit_or_encoding";
					}
					if (!ClipboardError.empty())
					{
						Text.clear();
					}
				}
				catch (...)
				{
					Text.clear();
					ClipboardError = "gui.clipboard_read_failed";
				}
			}
			const Gui::FTextInputServices *ClipboardServices = nullptr;
			std::string ClipboardError;
			double Time = 0;
		};

		// Markup is compiled by Gui. Font and image bytes use the configured resource reader.
		class FFiles final : public Rml::FileInterface
		{
		public:
			//~ Begin Rml::FileInterface Interface
			Rml::FileHandle Open(const Rml::String &) override
			{
				return 0;
			}
			void Close(Rml::FileHandle) override
			{
			}
			size_t Read(void *, size_t, Rml::FileHandle) override
			{
				return 0;
			}
			bool Seek(Rml::FileHandle, long, int) override
			{
				return false;
			}
			size_t Tell(Rml::FileHandle) override
			{
				return 0;
			}
			//~ End Rml::FileInterface Interface
		};

		struct FRuntime
		{
			std::mutex Mutex;
			std::thread::id Thread;
			std::size_t References = 0;
			std::uint64_t NextContext = 1;
			FSystem System;
			FFiles Files;
		};

		// Intentionally process-lived registry: retired resources must not run during static destruction.
		FRuntime &Runtime()
		{
			static auto *Value = new FRuntime;
			return *Value;
		}

		bool UsesVerticalFlow(const Gui::FBackendNode &Node)
		{
			return Node.Parent && Node.Properties.Layout == Gui::ELayout::Vertical &&
			    Node.Control.Kind != Gui::EControlKind::Grid && Node.Control.Kind != Gui::EControlKind::Wrap &&
			    Node.Control.Kind != Gui::EControlKind::Splitter && !Gui::IsTextControl(Node.Control.Kind);
		}

		Rml::Property Length(const Gui::FLength &Value)
		{
			if (Value.Unit == Gui::ELengthUnit::Auto)
			{
				return {0, Rml::Unit::KEYWORD};
			}
			return {static_cast<float>(Value.Value),
			    Value.Unit == Gui::ELengthUnit::Percent ? Rml::Unit::PERCENT : Rml::Unit::DP};
		}
		bool OnlyOpacityChanged(const Gui::FResolvedProperties& Value, const Gui::FResolvedProperties& Before)
		{
			if (Value.Opacity == Before.Opacity || Value.Text != Before.Text) return false;
			auto Comparable = Before;
			Comparable.Opacity = Value.Opacity;
			return Gui::ResolvedPropertiesEqual(Value, Comparable);
		}

		Rml::byte EncodeColorChannel(float Linear)
		{
			const float Encoded =
			    Linear <= 0.0031308f ? 12.92f * Linear : 1.055f * std::pow(Linear, 1.0f / 2.4f) - 0.055f;
			return static_cast<Rml::byte>(std::lround(std::clamp(Encoded, 0.0f, 1.0f) * 255));
		}

		class FBackend final : public Gui::IBackendContext
		{
		public:
			//~ Begin Gui::IBackendContext Interface
			~FBackend() override;
			Gui::FCapabilities GetCapabilities() const noexcept override
			{
				Gui::FCapabilities Value;
				Value.bLayout = true;
				Value.bRendering = Renderer.CanRender();
				Value.bGroupOpacity = Renderer.CanComposite();
				Value.bText = true;
				Value.bScrolling = true;
				Value.bTextEditing = true;
				Value.bExtendedControls = true;
				Value.bClipMasks = Renderer.CanComposite();
				Value.bFilters = Renderer.CanComposite();
				return Value;
			}

			std::optional<bool> Contains(std::uint64_t Id, double X, double Y) const override
			{
				if (Thread != std::this_thread::get_id() || !bReady)
				{
					return false;
				}
				const auto Found = Elements.find(Id);
				if (Found == Elements.end())
				{
					return false;
				}
				auto *Element = Found->second;
				Rml::Vector2f Point(static_cast<float>(X * Viewport.DisplayScale),
				    static_cast<float>(Y * Viewport.DisplayScale));
				if (!Element->Project(Point) || !Element->IsPointWithinElement(Point))
				{
					return false;
				}
				for (auto *Ancestor = Element; Ancestor && Ancestor != Document; Ancestor = Ancestor->GetParentNode())
				{
					const auto AncestorId = static_cast<FGuiElement *>(Ancestor)->GetIdentity();
					const auto Index = SnapshotIndices.find(AncestorId);
					if (Index == SnapshotIndices.end())
					{
						return false;
					}
					const auto &Node = Snapshot[Index->second];
					if (!Node.Properties.bClipContent && Node.Control.Kind != Gui::EControlKind::ScrollView)
					{
						continue;
					}
					auto Local = Rml::Vector2f(static_cast<float>(X * Viewport.DisplayScale),
					    static_cast<float>(Y * Viewport.DisplayScale));
					if (!Ancestor->Project(Local) || !Ancestor->IsPointWithinElement(Local))
					{
						return false;
					}
					const auto Origin = Ancestor->GetAbsoluteOffset(Rml::BoxArea::Border);
					const auto Size = Ancestor->GetBox().GetSize(Rml::BoxArea::Border);
					const float Radius =
					    std::min(static_cast<float>(Node.Properties.CornerRadius * Viewport.DisplayScale),
					        std::min(Size.x, Size.y) * .5f);
					if (Radius > 0)
					{
						const float Px = Local.x - Origin.x, Py = Local.y - Origin.y;
						const float Dx = Px - std::clamp(Px, Radius, Size.x - Radius),
						            Dy = Py - std::clamp(Py, Radius, Size.y - Radius);
						if (Dx * Dx + Dy * Dy > Radius * Radius)
						{
							return false;
						}
					}
				}
				return true;
			}

			bool Synchronize(const Gui::FViewport &Value, const std::vector<Gui::FBackendNode> &Nodes,
			    std::string &Error) override
			{
				if (!Check(Error))
				{
					return false;
				}
				const bool bPreviousSynchronized = bSynchronized;
				bSynchronized = bReady = false;
				if (!Gui::ValidateViewport(Value, Error) || !Renderer.ValidateViewport(Value, Error))
				{
					return false;
				}
				// Validate the entire snapshot before changing the RmlUi tree.
				if (Nodes.size() > 100000)
				{
					Error = "rmlui.tree_limit";
					return false;
				}
				// Reuse validated ancestry only when every ordered identity and parent still matches.
				bool bSameTopology = bPreviousSynchronized && Snapshot.size() == Nodes.size();
				for (std::size_t I = 0; bSameTopology && I < Nodes.size(); ++I)
				{
					bSameTopology = Snapshot[I].Id == Nodes[I].Id && Snapshot[I].Parent == Nodes[I].Parent;
				}
				std::unordered_map<std::uint64_t, std::size_t> NewIndices;
				std::vector<std::size_t> Depth;
				if (!bSameTopology)
				{
					NewIndices.reserve(Nodes.size());
					Depth.reserve(Nodes.size());
				}
				const auto &NodeIndices = bSameTopology ? SnapshotIndices : NewIndices;
				bool bGroupOpacity = false;
				std::size_t ValidationIndex = 0;
				for (const auto &Node : Nodes)
				{
					if (!bSameTopology &&
					    (!Node.Id || NewIndices.count(Node.Id) || (!Node.Parent && !NewIndices.empty()) ||
					        (Node.Parent && !NewIndices.count(Node.Parent))))
					{
						Error = "rmlui.invalid_snapshot";
						return false;
					}
					// Unchanged values were validated by the last successful snapshot.
					// Properties and controls are still checked independently of the topology.
					const auto *Previous = bPreviousSynchronized && ValidationIndex < Snapshot.size() &&
					        Snapshot[ValidationIndex].Id == Node.Id
					    ? &Snapshot[ValidationIndex]
					    : nullptr;
					++ValidationIndex;
					if (((!Previous || !Gui::ResolvedPropertiesEqual(Previous->Properties, Node.Properties)) &&
					        !Gui::ValidateResolvedProperties(Node.Properties, Error)) ||
					    ((!Previous || !(Previous->Control == Node.Control)) &&
					        !Gui::ValidateControlOptions(Node.Control, Error)))
					{
						return false;
					}
					const auto ParentIndex = Node.Parent ? NodeIndices.at(Node.Parent) : 0;
					if (!bSameTopology)
					{
						const auto NodeDepth = Node.Parent ? Depth[ParentIndex] + 1 : 1;
						if (NodeDepth > 256)
						{
							Error = "rmlui.tree_limit";
							return false;
						}
						Depth.push_back(NodeDepth);
						NewIndices.emplace(Node.Id, ValidationIndex - 1);
					}
					if (Node.Parent && Nodes[ParentIndex].Properties.Opacity != 1)
					{
						bGroupOpacity = true;
					}
				}
				Context->SetDimensions({static_cast<int>(Value.PixelWidth), static_cast<int>(Value.PixelHeight)});
				Context->SetDensityIndependentPixelRatio(static_cast<float>(Value.DisplayScale));
				bool bStructureChanged = !bPreviousSynchronized || Snapshot.size() != Nodes.size();
				if (Renderer.HasEffectFailure())
				{
					bool Changed = Snapshot.size() != Nodes.size();
					for (std::size_t I = 0; !Changed && I < Nodes.size(); ++I)
					{
						Changed = Snapshot[I].Id != Nodes[I].Id || Snapshot[I].Parent != Nodes[I].Parent ||
						    !Gui::ResolvedPropertiesEqual(Snapshot[I].Properties, Nodes[I].Properties);
					}
					if (Changed)
					{
						Renderer.ResetEffectFailure();
						bStructureChanged = true;
					}
				}
				for (std::size_t Index = 0; !bStructureChanged && Index < Nodes.size(); ++Index)
				{
					bStructureChanged = Snapshot[Index].Id != Nodes[Index].Id ||
					    Snapshot[Index].Parent != Nodes[Index].Parent ||
					    Snapshot[Index].Control.Kind != Nodes[Index].Control.Kind;
				}
				if (bStructureChanged || !Document)
				{
					Controls.BeforeRebuild();
					if (Document)
					{
						Context->UnloadDocument(Document);
					}
					Elements.clear();
					Backdrops.clear();
					PortalRanks.clear();
					Contents.clear();
					Styles.clear();
					Document = Context->CreateDocument();
					if (!Document)
					{
						Error = "rmlui.document_creation_failed";
						return false;
					}
					Document->SetProperty("width", "100%");
					// RmlUi instances decorators through the document stylesheet, even for inline properties.
					auto Sheet = Rml::Factory::InstanceStyleSheetString("vsgui-widget { font-size: 16px; }");
					if (!Sheet)
					{
						Error = "rmlui.internal_stylesheet_failed";
						return false;
					}
					Document->SetStyleSheetContainer(std::move(Sheet));
					Document->SetProperty("height", "100%");
					Document->SetProperty("margin", "0px");
					for (const auto &Node : Nodes)
					{
						if (Gui::IsFloatingControl(Node.Control.Kind))
						{
							auto Backdrop = Document->CreateElement("div");
							if (!Backdrop)
							{
								Error = "rmlui.element_creation_failed";
								return false;
							}
							Backdrop->SetProperty("position", "fixed");
							Backdrop->SetProperty("left", "0px");
							Backdrop->SetProperty("top", "0px");
							Backdrop->SetProperty("width", "100%");
							Backdrop->SetProperty("height", "100%");
							Backdrop->SetProperty("background-color", "rgba(0,0,0,115)");
							Backdrop->SetProperty("pointer-events", "none");
							Backdrop->SetProperty("display", "none");
							Backdrops.emplace(Node.Id, Document->AppendChild(std::move(Backdrop)));
						}
						auto Element = Document->CreateElement("vsgui-widget");
						if (!Element)
						{
							Error = "rmlui.element_creation_failed";
							return false;
						}
						auto *Parent = Node.Parent && !Gui::IsFloatingControl(Node.Control.Kind)
						    ? Elements.at(Node.Parent)
						    : Document;
						Elements.emplace(Node.Id, Parent->AppendChild(std::move(Element)));
					}
					Document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
				}
				std::vector<std::uint8_t> Hidden(Nodes.size());
				std::vector<std::pair<std::uint64_t, std::vector<std::uint64_t>>> Portals;
				for (const auto &Node : Nodes)
				{
					if (!Gui::IsFloatingControl(Node.Control.Kind))
					{
						continue;
					}
					std::vector<std::uint64_t> Order;
					for (auto Ancestor = &Node; Ancestor;
					     Ancestor = Ancestor->Parent ? &Nodes[NodeIndices.at(Ancestor->Parent)] : nullptr)
					{
						if (Ancestor->Control.bModal && Ancestor->Control.bOpen)
						{
							Order.push_back(Ancestor->Control.ModalOrder);
						}
					}
					std::sort(Order.rbegin(), Order.rend());
					Portals.emplace_back(Node.Id, std::move(Order));
				}
				std::stable_sort(Portals.begin(), Portals.end(), [](const auto &A, const auto &B)
				{
					return A.second < B.second;
				});
				int PortalRank = 1;
				for (const auto &Portal : Portals)
				{
					if (!PortalRanks.count(Portal.first) || PortalRanks.at(Portal.first) != PortalRank)
					{
						Elements.at(Portal.first)->SetProperty(Rml::PropertyId::ZIndex, Rml::Property(float(PortalRank), Rml::Unit::NUMBER));
						Backdrops.at(Portal.first)->SetProperty(Rml::PropertyId::ZIndex, Rml::Property(float(PortalRank), Rml::Unit::NUMBER));
						PortalRanks[Portal.first] = PortalRank;
					}
					++PortalRank;
				}
				std::vector<std::pair<Rml::PropertyId, Rml::Property>> Values;
				Values.reserve(40);
				std::size_t NodeIndex = 0;
				for (const auto &Node : Nodes)
				{
					const auto &Props = Node.Properties;
					static_cast<FGuiElement *>(Elements.at(Node.Id))
					    ->Configure(Renderer, Node.Id, Value.DisplayScale, Node.Paint);
					const auto *Parent = Node.Parent ? &Nodes[NodeIndices.at(Node.Parent)] : nullptr;
					const bool bFloating = Gui::IsFloatingControl(Node.Control.Kind);
					const bool bClosed = bFloating && !Node.Control.bOpen;
					const bool bOverlayChild = Parent && !bFloating &&
					    (Parent->Control.Kind == Gui::EControlKind::Splitter ||
					        (Parent->Properties.Layout == Gui::ELayout::Overlay &&
					            Parent->Control.Kind != Gui::EControlKind::Grid &&
					            Parent->Control.Kind != Gui::EControlKind::Wrap));
					const bool bHidden = Props.Visibility != Gui::EVisibility::Visible || bClosed ||
					    (Node.Parent && Hidden[NodeIndices.at(Node.Parent)]);
					Hidden[NodeIndex] = bHidden;
					const auto *PreviousNode = bStructureChanged ? nullptr : &Snapshot[NodeIndex];
					const auto *PreviousParent = Parent && !bStructureChanged
					    ? &Snapshot[SnapshotIndices.at(Node.Parent)]
					    : nullptr;
					if (bFloating)
					{
						const bool Show = Node.Control.bModal && !bHidden;
						if (!PreviousNode || PreviousNode->Control.bModal != Node.Control.bModal ||
						    NodeIndex >= EffectiveHidden.size() || EffectiveHidden[NodeIndex] != bHidden)
						{
							Backdrops.at(Node.Id)->SetProperty("display", Show ? "block" : "none");
						}
					}
					const bool bSameParent = !Parent ||
					    (PreviousParent && Parent->Properties.Layout == PreviousParent->Properties.Layout &&
					        Parent->Properties.Padding == PreviousParent->Properties.Padding);
					const bool bSameAppearance = !bStructureChanged &&
					    Gui::ResolvedPropertiesEqual(Props, Snapshot[NodeIndex].Properties) &&
					    bSameParent &&
					    NodeIndex < EffectiveHidden.size() && EffectiveHidden[NodeIndex] == bHidden;
					const auto &PreviousControl = bStructureChanged ? Node.Control : Snapshot[NodeIndex].Control;
					const bool bOpacityOnly = !bSameAppearance && !bStructureChanged && bSameParent &&
					    (!Parent || Parent->Control.Kind == PreviousParent->Control.Kind) &&
					    NodeIndex < EffectiveHidden.size() && EffectiveHidden[NodeIndex] == bHidden &&
					    Node.Control.Kind == PreviousControl.Kind &&
					    (Node.Control.Kind != Gui::EControlKind::Splitter ||
					        (Node.Control.bVertical == PreviousControl.bVertical &&
					            Node.Control.DividerSize == PreviousControl.DividerSize)) &&
					    OnlyOpacityChanged(Props, Snapshot[NodeIndex].Properties);
					const bool bSameControl = Node.Control == PreviousControl;
					const bool bSame = bSameAppearance && bSameControl;
					++NodeIndex;
					if (bSame)
					{
						continue;
					}
					// Value/text updates do not change the element's authored appearance.
					// Splitter direction and divider size also affect the root's flex CSS.
					if (bSameAppearance &&
					    (Node.Control.Kind != Gui::EControlKind::Splitter ||
					        (Node.Control.bVertical == PreviousControl.bVertical &&
					            Node.Control.DividerSize == PreviousControl.DividerSize)))
					{
						if (!Controls.Synchronize(*Document, *Elements.at(Node.Id), Node, Error))
						{
							return false;
						}
						continue;
					}
					// Typed properties avoid the host C locale's decimal parser and repeated CSS parsing.
					using Rml::Property;
					using Rml::PropertyId;
					using Rml::Unit;
					if (bOpacityOnly)
					{
						Property Opacity(Renderer.CanComposite() ? 1.0f : static_cast<float>(Props.Opacity), Unit::NUMBER);
						auto& Cached = Styles[Node.Id];
						const auto Found = std::lower_bound(Cached.begin(), Cached.end(), PropertyId::Opacity,
						    [](const auto& Entry, PropertyId Id) { return Entry.first < Id; });
						if (Found == Cached.end() || Found->first != PropertyId::Opacity || Found->second != Opacity)
						{
							if (!Elements.at(Node.Id)->SetProperty(PropertyId::Opacity, Opacity))
							{
								Error = "rmlui.property_rejected: " + std::to_string(static_cast<unsigned>(PropertyId::Opacity));
								return false;
							}
							if (Found != Cached.end() && Found->first == PropertyId::Opacity)
							{
								Found->second = std::move(Opacity);
							}
							else
							{
								Cached.insert(Found, {PropertyId::Opacity, std::move(Opacity)});
							}
						}
					}
					else
					{
						Values = {
						    {PropertyId::BoxSizing, Property(Rml::Style::BoxSizing::BorderBox)},
						    {PropertyId::FlexShrink, {0.0f, Unit::NUMBER}},
						    {PropertyId::AlignItems, Property(Rml::Style::AlignItems::Stretch)},
						    {PropertyId::AlignContent, Property(Rml::Style::AlignContent::FlexStart)},
						    {PropertyId::Display,
						        Property(Props.Visibility == Gui::EVisibility::Collapsed ? Rml::Style::Display::None
						                : bClosed                                        ? Rml::Style::Display::None
						                : UsesVerticalFlow(Node)                         ? Rml::Style::Display::Block
						                                                                 : Rml::Style::Display::Flex)},
						    {PropertyId::Visibility,
						        Property(bHidden ? Rml::Style::Visibility::Hidden : Rml::Style::Visibility::Visible)},
						    {PropertyId::Position, Property(bOverlayChild || bFloating ? Rml::Style::Position::Absolute : Rml::Style::Position::Relative)},
						    {PropertyId::FlexWrap, Property(Node.Control.Kind == Gui::EControlKind::Grid || Node.Control.Kind == Gui::EControlKind::Wrap ? Rml::Style::FlexWrap::Wrap : Rml::Style::FlexWrap::Nowrap)},
						    {PropertyId::FlexDirection, Property(Node.Control.Kind == Gui::EControlKind::Grid || (Node.Control.Kind == Gui::EControlKind::Splitter ? !Node.Control.bVertical : Props.Layout == Gui::ELayout::Horizontal) ? Rml::Style::FlexDirection::Row : Rml::Style::FlexDirection::Column)},
						    {PropertyId::Width, !Node.Parent && Props.Width.Unit == Gui::ELengthUnit::Auto ? Property(100.0f, Unit::PERCENT) : Length(Props.Width)},
						    {PropertyId::Height, !Node.Parent && Props.Height.Unit == Gui::ELengthUnit::Auto ? Property(100.0f, Unit::PERCENT) : Length(Props.Height)},
						    {PropertyId::PaddingTop, {static_cast<float>(Props.Padding.Top), Unit::DP}},
						    {PropertyId::PaddingRight, {static_cast<float>(Props.Padding.Right), Unit::DP}},
						    {PropertyId::PaddingBottom, {static_cast<float>(Props.Padding.Bottom), Unit::DP}},
						    {PropertyId::PaddingLeft, {static_cast<float>(Props.Padding.Left), Unit::DP}},
						    {PropertyId::RowGap, {static_cast<float>(Props.Gap), Unit::DP}},
						    {PropertyId::ColumnGap, {static_cast<float>(Props.Gap), Unit::DP}},
						    {PropertyId::Opacity,
						        {Renderer.CanComposite() ? 1.0f : static_cast<float>(Props.Opacity), Unit::NUMBER}},
						    {PropertyId::BackgroundColor,
						        {Rml::Colourb{EncodeColorChannel(Props.Background.R), EncodeColorChannel(Props.Background.G),
						             EncodeColorChannel(Props.Background.B),
						             static_cast<Rml::byte>(std::lround(Props.Background.A * 255))},
						            Unit::COLOUR}}};
						const auto SetValue = [&](PropertyId Id, Property Value)
						{
							const auto Found = std::find_if(Values.begin(), Values.end(),
							    [&](const auto& Entry) { return Entry.first == Id; });
							if (Found == Values.end())
							{
								Values.emplace_back(Id, std::move(Value));
							}
							else
							{
								Found->second = std::move(Value);
							}
						};
						if (Node.Control.Kind == Gui::EControlKind::Splitter)
						{
							SetValue(PropertyId::RowGap, Property(static_cast<float>(Node.Control.DividerSize), Unit::DP));
							SetValue(PropertyId::ColumnGap, Property(static_cast<float>(Node.Control.DividerSize), Unit::DP));
						}
						const Rml::Colourb BorderColor{EncodeColorChannel(Props.BorderColor.R),
						    EncodeColorChannel(Props.BorderColor.G),
						    EncodeColorChannel(Props.BorderColor.B),
						    static_cast<Rml::byte>(std::lround(Props.BorderColor.A * 255))};
						for (const auto Side : {PropertyId::BorderTopWidth, PropertyId::BorderRightWidth,
						         PropertyId::BorderBottomWidth, PropertyId::BorderLeftWidth})
						{
							SetValue(Side, Property(static_cast<float>(Props.BorderWidth), Unit::DP));
						}
						for (const auto Side : {PropertyId::BorderTopColor, PropertyId::BorderRightColor,
						         PropertyId::BorderBottomColor, PropertyId::BorderLeftColor})
						{
							SetValue(Side, Property(BorderColor, Unit::COLOUR));
						}
						// SynchronizeContent owns the combined authored filters and group opacity.
						for (const auto Side : {PropertyId::Left, PropertyId::Right, PropertyId::Top, PropertyId::Bottom})
						{
							SetValue(Side, Property(0, Unit::KEYWORD));
						}
						if (bOverlayChild)
						{
							const auto &Insets = Parent->Properties.Padding;
							SetValue(PropertyId::Left, Property(static_cast<float>(Insets.Left), Unit::DP));
							SetValue(PropertyId::Right, Property(static_cast<float>(Insets.Right), Unit::DP));
							SetValue(PropertyId::Top, Property(static_cast<float>(Insets.Top), Unit::DP));
							SetValue(PropertyId::Bottom, Property(static_cast<float>(Insets.Bottom), Unit::DP));
						}
						// Preserve property order while reusing contiguous cache storage.
						std::sort(Values.begin(), Values.end(), [](const auto& A, const auto& B) { return A.first < B.first; });
						auto &Previous = Styles[Node.Id];
						for (const auto &Property : Values)
						{
							const auto Found = std::lower_bound(Previous.begin(), Previous.end(), Property.first,
							    [](const auto& Entry, PropertyId Id) { return Entry.first < Id; });
							if ((Found == Previous.end() || Found->first != Property.first || Found->second != Property.second) &&
							    !Elements.at(Node.Id)->SetProperty(Property.first, Property.second))
							{
								Error = "rmlui.property_rejected: " + std::to_string(static_cast<unsigned>(Property.first));
								return false;
							}
						}
						Previous.swap(Values);
					}
					if (!SynchronizeContent(*Document, *Elements.at(Node.Id), Contents[Node.Id], Props,
					        PreviousNode ? &PreviousNode->Properties : nullptr,
					        Renderer.CanComposite(), Error))
					{
						return false;
					}
					if (!Controls.Synchronize(*Document, *Elements.at(Node.Id), Node, Error))
					{
						return false;
					}
					if (Node.Control.Kind == Gui::EControlKind::ScrollView)
					{
						Elements.at(Node.Id)->SetProperty("overflow-x", "hidden");
						Elements.at(Node.Id)->SetProperty("overflow-y", "hidden");
					}
				}
				// Plain vertical flow avoids repeated flex sizing of vertical subtrees.
				// Private spacers preserve Gap for both fixed and automatic heights.
				for (const auto &Node : Nodes)
				{
					auto *Element = static_cast<FGuiElement *>(Elements.at(Node.Id));
					std::vector<Rml::Element *> Items;
					const bool bBlock = UsesVerticalFlow(Node);
					if (bBlock && Node.Properties.Gap > 0)
					{
						for (int Index = 0; Index < Element->GetNumChildren(); ++Index)
						{
							auto *Child = Element->GetChild(Index);
							if (Child->GetTagName() == "vsgui-flow-gap")
							{
								continue;
							}
							if (const auto *Widget = dynamic_cast<FGuiElement *>(Child))
							{
								const auto *Source = &Nodes[NodeIndices.at(Widget->GetIdentity())];
								if (Source->Properties.Visibility == Gui::EVisibility::Collapsed)
								{
									continue;
								}
							}
							Items.push_back(Child);
						}
					}
					if (!Element->SynchronizeFlow(*Document, Items, bBlock ? Node.Properties.Gap : 0, Error))
					{
						return false;
					}
				}
				Controls.AfterRebuild(Nodes);
				Snapshot = Nodes;
				if (!bSameTopology)
				{
					SnapshotIndices = std::move(NewIndices);
				}
				EffectiveHidden = std::move(Hidden);
				Viewport = Value;
				bRequiresGroupOpacity = bGroupOpacity;
				bSynchronized = true;
				return true;
			}

			bool Update(double ElapsedSeconds, std::vector<Gui::FLayoutBox> &Layout, std::string &Error) override
			{
				if (!Check(Error))
				{
					return false;
				}
				bReady = false;
				if (!bSynchronized)
				{
					Error = "rmlui.synchronize_required";
					return false;
				}
				if (!std::isfinite(ElapsedSeconds) || ElapsedSeconds < Time)
				{
					Error = "rmlui.invalid_time";
					return false;
				}
				Runtime().System.Time = ElapsedSeconds;
				Renderer.BeginFrame();
				if (!Context->Update())
				{
					Error = "rmlui.update_failed";
					return false;
				}
				if (!UpdateContainerLayouts(*Context, Elements, Snapshot, SnapshotIndices, Viewport, Error))
				{
					return false;
				}
				bool bScrolled = false;
				for (const auto &Node : Snapshot)
				{
					if (Node.Control.Kind != Gui::EControlKind::ScrollView)
					{
						continue;
					}
					auto *Element = Elements.at(Node.Id);
					const auto X = Element->GetScrollLeft(), Y = Element->GetScrollTop();
					Element->SetScrollLeft(
					    static_cast<float>(std::min(Node.Control.ScrollX,
					                           std::max(0.0, Element->GetScrollWidth() / Viewport.DisplayScale)) *
					        Viewport.DisplayScale));
					Element->SetScrollTop(
					    static_cast<float>(std::min(Node.Control.ScrollY,
					                           std::max(0.0, Element->GetScrollHeight() / Viewport.DisplayScale)) *
					        Viewport.DisplayScale));
					bScrolled = bScrolled || X != Element->GetScrollLeft() || Y != Element->GetScrollTop();
				}
				if (bScrolled && !Context->Update())
				{
					Error = "rmlui.update_failed";
					return false;
				}
				if (!PositionFloatingControls(*Context, Elements, Snapshot, Viewport, Error) ||
				    !Controls.UpdateInputArea(Viewport.DisplayScale, Error))
				{
					return false;
				}
				std::vector<Gui::FLayoutBox> Result;
				if (!Renderer.CheckResources(Snapshot, Error))
				{
					return false;
				}
				Result.reserve(Snapshot.size());
				std::vector<std::uint8_t> Collapsed;
				Collapsed.reserve(Snapshot.size());
				for (const auto &Node : Snapshot)
				{
					Gui::FLayoutBox Box;
					Box.Id = Node.Id;
					const bool bCollapsed = Node.Properties.Visibility == Gui::EVisibility::Collapsed ||
					    (Gui::IsFloatingControl(Node.Control.Kind) && !Node.Control.bOpen) ||
					    (Node.Parent && Collapsed[SnapshotIndices.at(Node.Parent)]);
					Collapsed.push_back(bCollapsed);
					if (!bCollapsed)
					{
						auto *Element = Elements.at(Node.Id);
						if (!Node.Properties.Text.empty() && !Element->GetFontFaceHandle())
						{
							Error = "rmlui.font_unavailable: " + Node.Properties.FontFamily;
							return false;
						}
						const auto BorderPosition = Element->GetAbsoluteOffset(Rml::BoxArea::Border);
						const auto ContentPosition = Element->GetAbsoluteOffset(Rml::BoxArea::Content);
						const auto BorderSize = Element->GetBox().GetSize(Rml::BoxArea::Border);
						const auto ContentSize = Element->GetBox().GetSize(Rml::BoxArea::Content);
						const double Scale = Viewport.DisplayScale;
						Box.Border = {BorderPosition.x / Scale, BorderPosition.y / Scale, BorderSize.x / Scale,
						    BorderSize.y / Scale};
						Box.Content = {ContentPosition.x / Scale, ContentPosition.y / Scale,
						    std::max(0.0, ContentSize.x / Scale), std::max(0.0, ContentSize.y / Scale)};
						Box.ScrollWidth =
						    Box.Content.Width +
						    std::max(0.0, static_cast<double>(Element->GetScrollWidth() - Element->GetClientWidth())) /
						        Scale;
						Box.ScrollHeight =
						    Box.Content.Height + std::max(0.0, static_cast<double>(Element->GetScrollHeight() - Element->GetClientHeight())) / Scale;
						Box.ScrollX = Element->GetScrollLeft() / Scale;
						Box.ScrollY = Element->GetScrollTop() / Scale;
					}
					Result.push_back(Box);
				}
				Layout = std::move(Result);
				Time = ElapsedSeconds;
				bReady = true;
				return true;
			}

			bool Render(bool &bPresented, std::string &Error) override
			{
				bPresented = false;
				if (!Check(Error))
				{
					return false;
				}
				if (!bReady)
				{
					Error = "rmlui.update_required";
					return false;
				}
				if (!Renderer.CanRender())
				{
					Error = "rmlui.layout_only";
					return false;
				}
				if (bRequiresGroupOpacity && !Renderer.CanComposite())
				{
					Error = "rmlui.group_opacity_unavailable: subtree opacity requires layer compositing";
					return false;
				}
				Runtime().System.Time = Time;
				Renderer.BeginFrame();
				if (!Renderer.BuildDraws(*Context, Error))
				{
					return false;
				}
				return Renderer.Submit(bPresented, Error);
			}

			bool Close(std::string &Error) override
			{
				Error.clear();
				if (Thread != std::this_thread::get_id())
				{
					Error = "rmlui.wrong_thread";
					return false;
				}
				if (Context)
				{
					if (!Controls.Focus(0, TextServices, Error))
					{
						return false;
					}
					Controls.BeforeRebuild();
					Rml::RemoveContext(Name);
					Context = nullptr;
					Document = nullptr;
					Elements.clear();
					Backdrops.clear();
					PortalRanks.clear();
					Styles.clear();
					Snapshot.clear();
					Rml::ReleaseRenderManagers();
				}
				if (!Renderer.Close(Error))
				{
					return false;
				}
				if (bAcquired)
				{
					auto &Shared = Runtime();
					std::lock_guard<std::mutex> Guard(Shared.Mutex);
					if (--Shared.References == 0)
					{
						Rml::Shutdown();
						ReleaseFontSources();
						Shared.Thread = {};
					}
					bAcquired = false;
				}
				return true;
			}
			bool FocusTextControl(std::uint64_t Id, const Gui::FTextInputServices &Services,
			    std::string &Error) override
			{
				if (!Check(Error))
				{
					return false;
				}
				TextServices = Services;
				return Controls.Focus(Id, Services, Error) && Controls.UpdateInputArea(Viewport.DisplayScale, Error);
			}
			bool EditText(std::uint64_t Id, const Platform::FEvent &Input, Gui::FTextEditResult &Result,
			    std::string &Error) override
			{
				Result = {};
				if (!Check(Error))
				{
					return false;
				}
				auto &System = Runtime().System;
				struct FScope
				{
					FSystem &System;
					const Gui::FTextInputServices *Previous;
					~FScope()
					{
						System.ClipboardServices = Previous;
					}
				} Scope{System, System.ClipboardServices};
				System.ClipboardServices = &TextServices;
				System.ClipboardError.clear();
				const bool bEdited = Controls.Edit(Id, Input, *Context, Viewport.DisplayScale, Result, System.ClipboardError, Error);
				if (Result.bChanged)
				{
					// Track committed edits so resetting to an earlier value cannot match a stale snapshot.
					const auto Found = SnapshotIndices.find(Id);
					if (Found != SnapshotIndices.end())
					{
						Snapshot[Found->second].Control.Text = Result.Text;
					}
				}
				if (!bEdited)
				{
					return false;
				}
				if (!System.ClipboardError.empty())
				{
					Error = System.ClipboardError;
					return false;
				}
				return true;
			}
			bool IsTextComposing(std::uint64_t Id) const override
			{
				return Controls.IsComposing(Id);
			}
			bool SetTextSelection(std::uint64_t Id, std::int64_t Start, std::int64_t End, std::string &Error) override
			{
				return Check(Error) && Controls.Select(Id, Start, End, Error);
			}
			//~ End Gui::IBackendContext Interface

			bool SetResourceReader(Gui::FResourceReader Reader, std::string &Error) override
			{
				if (!Check(Error) || !Renderer.SetResourceReader(Reader, Error))
				{
					return false;
				}
				ReadResource = std::move(Reader);
				return true;
			}

			bool LoadFont(const Gui::FFontFace &Font, std::string &Error) override
			{
				if (!Check(Error) || !LoadFontSource(Font, ReadResource, Error))
				{
					return false;
				}
				Snapshot.clear();
				bSynchronized = bReady = false;
				return true;
			}

			explicit FBackend(const FOptions &Options)
			    : Thread(std::this_thread::get_id()), Renderer(Options), ReadResource(Options.ReadResource),
			      Fonts(Options.Fonts), TextLayouts(Options.TextLayouts)
			{
			}

			bool Initialize(std::string &Error)
			{
				auto &Shared = Runtime();
				{
					std::lock_guard<std::mutex> Guard(Shared.Mutex);
					if (Shared.References && Shared.Thread != Thread)
					{
						Error = "rmlui.runtime_thread_busy";
						return false;
					}
					if (!Shared.References && Rml::GetSystemInterface())
					{
						Error = "rmlui.runtime_already_owned";
						return false;
					}
					if (!UseFontServices(Fonts, TextLayouts, Error))
					{
						return false;
					}
					const bool bFirst = !Shared.References;
					if (bFirst)
					{
						Rml::SetSystemInterface(&Shared.System);
						Rml::SetFileInterface(&Shared.Files);
						Rml::SetFontEngineInterface(GetFontEngine());
						if (!Rml::Initialise())
						{
							ReleaseFontServices();
							Error = "rmlui.initialization_failed";
							return false;
						}
						Shared.Thread = Thread;
					}
					// Acquire before calling the selected font service so failure or an
					// exception follows the same owned shutdown path as a live context.
					++Shared.References;
					bAcquired = true;
					if (bFirst)
					{
						if (!InitializeDefaultFont(Error))
						{
							return false;
						}
						RegisterGuiElement();
					}
					Name = "veroscript-gui-" + std::to_string(Shared.NextContext++);
				}
				if (!Renderer.Initialize(Error))
				{
					return false;
				}
				Context = Rml::CreateContext(Name, {1, 1}, &Renderer, &Controls);
				if (!Context)
				{
					Error = "rmlui.context_creation_failed";
					return false;
				}
				Context->EnableMouseCursor(false);
				return true;
			}

		private:
			bool Check(std::string &Error) const
			{
				Error.clear();
				if (Thread != std::this_thread::get_id())
				{
					Error = "rmlui.wrong_thread";
				}
				else if (!Context)
				{
					Error = "rmlui.context_closed";
				}
				return Error.empty();
			}

			std::thread::id Thread;
			FRenderer Renderer;
			FControls Controls;
			Gui::FTextInputServices TextServices;
			Rml::Context *Context = nullptr;
			Rml::ElementDocument *Document = nullptr;
			std::unordered_map<std::uint64_t, Rml::Element *> Elements;
			std::unordered_map<std::uint64_t, Rml::Element *> Backdrops;
			std::unordered_map<std::uint64_t, int> PortalRanks;
			std::unordered_map<std::uint64_t, FContentNodes> Contents;
			Gui::FResourceReader ReadResource;
			std::shared_ptr<Font::IService> Fonts;
			std::shared_ptr<Font::ITextLayoutService> TextLayouts;
			std::vector<std::uint8_t> EffectiveHidden;
			std::unordered_map<std::uint64_t, std::vector<std::pair<Rml::PropertyId, Rml::Property>>> Styles;
			std::vector<Gui::FBackendNode> Snapshot;
			std::unordered_map<std::uint64_t, std::size_t> SnapshotIndices;
			Gui::FViewport Viewport;
			std::string Name;
			double Time = 0;
			bool bAcquired = false, bSynchronized = false, bReady = false, bRequiresGroupOpacity = false;
		};

		bool CloseBackend(FBackend &Backend, std::string &Error)
		{
			try
			{
				if (Backend.Close(Error))
				{
					return true;
				}
				if (Error.empty())
				{
					Error = "rmlui.close_failed";
				}
			}
			catch (const std::exception &Exception)
			{
				Error = std::string("rmlui.close_exception: ") + Exception.what();
			}
			catch (...)
			{
				Error = "rmlui.close_exception";
			}
			return false;
		}

		// Keep the complete backend alive until its creator thread finishes closing its resources.
		struct FRetirementQueue
		{
			struct FEntry
			{
				std::thread::id Thread;
				std::shared_ptr<const void> Owner;
				std::unique_ptr<FBackend> Backend;
			};
			std::mutex Mutex;
			std::vector<FEntry> Items;
		};
		FRetirementQueue &Retired()
		{
			static auto *Value = new FRetirementQueue;
			return *Value;
		}

		class FOwnedBackend final : public Gui::IBackendContext
		{
		public:
			//~ Begin Gui::IBackendContext Interface
			~FOwnedBackend() override;
			Gui::FCapabilities GetCapabilities() const noexcept override
			{
				return Backend->GetCapabilities();
			}
			bool Synchronize(const Gui::FViewport &Viewport, const std::vector<Gui::FBackendNode> &Nodes,
			    std::string &Error) override
			{
				return Backend->Synchronize(Viewport, Nodes, Error);
			}
			bool Update(double Time, std::vector<Gui::FLayoutBox> &Layout, std::string &Error) override
			{
				return Backend->Update(Time, Layout, Error);
			}
			bool Render(bool &bPresented, std::string &Error) override
			{
				return Backend->Render(bPresented, Error);
			}
			bool Close(std::string &Error) override
			{
				return Backend->Close(Error);
			}
			bool SetResourceReader(Gui::FResourceReader Reader, std::string &Error) override
			{
				return Backend->SetResourceReader(std::move(Reader), Error);
			}

			std::optional<bool> Contains(std::uint64_t Id, double X, double Y) const override
			{
				return Backend->Contains(Id, X, Y);
			}

			bool LoadFont(const Gui::FFontFace &Font, std::string &Error) override
			{
				return Backend->LoadFont(Font, Error);
			}
			bool FocusTextControl(std::uint64_t Id, const Gui::FTextInputServices &Services,
			    std::string &Error) override
			{
				return Backend->FocusTextControl(Id, Services, Error);
			}
			bool EditText(std::uint64_t Id, const Platform::FEvent &Input, Gui::FTextEditResult &Result,
			    std::string &Error) override
			{
				return Backend->EditText(Id, Input, Result, Error);
			}
			bool IsTextComposing(std::uint64_t Id) const override
			{
				return Backend->IsTextComposing(Id);
			}
			bool SetTextSelection(std::uint64_t Id, std::int64_t Start, std::int64_t End, std::string &Error) override
			{
				return Backend->SetTextSelection(Id, Start, End, Error);
			}
			//~ End Gui::IBackendContext Interface
			explicit FOwnedBackend(std::unique_ptr<FBackend> Value, std::shared_ptr<const void> InOwner)
			    : Owner(std::move(InOwner)), Backend(std::move(Value)), Thread(std::this_thread::get_id())
			{
			}
			bool Initialize(std::string &Error)
			{
				return Backend->Initialize(Error);
			}

		private:
			std::shared_ptr<const void> Owner;
			std::unique_ptr<FBackend> Backend;
			std::thread::id Thread;
		};

		FOwnedBackend::~FOwnedBackend()
		{
			std::string Ignored;
			if (Thread != std::this_thread::get_id() || !CloseBackend(*Backend, Ignored))
			{
				auto &Queue = Retired();
				std::lock_guard<std::mutex> Guard(Queue.Mutex);
				Queue.Items.push_back({Thread, std::move(Owner), std::move(Backend)});
			}
		}

		FBackend::~FBackend()
		{
			std::string Ignored;
			Close(Ignored);
		}
	} // namespace

	std::unique_ptr<Gui::IBackendContext> CreateBackend(const FOptions &Options, std::string &Error)
	{
		if (!CollectRetired(Error))
		{
			return {};
		}
		auto Backend = std::make_unique<FOwnedBackend>(std::make_unique<FBackend>(Options), Options.Owner);
		if (!Backend->Initialize(Error))
		{
			return {};
		}
		return Backend;
	}

	bool CollectRetired(std::string &Error)
	{
		Error.clear();
		std::vector<FRetirementQueue::FEntry> Values;
		auto &Queue = Retired();
		{
			std::lock_guard<std::mutex> Guard(Queue.Mutex);
			for (auto It = Queue.Items.begin(); It != Queue.Items.end();)
			{
				if (It->Thread == std::this_thread::get_id())
				{
					Values.push_back(std::move(*It));
					It = Queue.Items.erase(It);
				}
				else
				{
					++It;
				}
			}
		}
		for (auto It = Values.begin(); It != Values.end(); ++It)
		{
			if (!CloseBackend(*It->Backend, Error))
			{
				std::lock_guard<std::mutex> Guard(Queue.Mutex);
				for (; It != Values.end(); ++It)
				{
					Queue.Items.push_back(std::move(*It));
				}
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::RmlUi

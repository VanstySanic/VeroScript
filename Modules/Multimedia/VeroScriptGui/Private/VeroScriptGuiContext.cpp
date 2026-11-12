// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "Compilation/VeroScriptGuiMarkupInternal.h"
#include "VeroScriptGuiControlPaint.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>
#include <stdexcept>
#include <tuple>
#include <typeinfo>
#include <unordered_map>

namespace VeroScript::Gui
{
	bool FPointerIdentity::operator<(const FPointerIdentity& Other) const noexcept
	{
		return std::tie(Kind, DeviceId, PointerId) < std::tie(Other.Kind, Other.DeviceId, Other.PointerId);
	}
	bool FPointerIdentity::operator==(const FPointerIdentity& Other) const noexcept
	{
		return Kind == Other.Kind && DeviceId == Other.DeviceId && PointerId == Other.PointerId;
	}
	FPointerIdentity FPointerIdentity::FromEvent(const Platform::FEvent& Event) noexcept
	{
		return {Platform::GetInputDeviceKind(Event), Event.DeviceId, Event.PointerId};
	}

	namespace
	{
		struct FRetiredContexts
		{
			std::mutex Mutex;
			FContext* Head = nullptr;
		};

		FRetiredContexts& Retired()
		{
			static auto* Queue = new FRetiredContexts;
			return *Queue;
		}

		struct FFlagGuard
		{
			explicit FFlagGuard(bool& InValue) : Value(InValue)
			{
				Value = true;
			}
			~FFlagGuard()
			{
				Value = false;
			}
			bool& Value;
		};

		bool ValidRect(const FRect& Rect)
		{
			return std::isfinite(Rect.X) && std::isfinite(Rect.Y) && std::isfinite(Rect.Width) &&
			    std::isfinite(Rect.Height) && Rect.Width >= 0 && Rect.Height >= 0 &&
			    std::isfinite(Rect.X + Rect.Width) && std::isfinite(Rect.Y + Rect.Height);
		}

		std::uint64_t NextSynchronizationToken()
		{
			static std::atomic<std::uint64_t> Next{1};
			const auto Value = Next.fetch_add(1, std::memory_order_relaxed);
			if (Value > static_cast<std::uint64_t>(INT64_MAX))
			{
				throw std::overflow_error("GUI synchronization token exhausted");
			}
			return Value;
		}
	} // namespace

	FContext::~FContext()
	{
		// Destruction must occur on the owner thread, just like platform/graphics resources.
		if (Thread == std::this_thread::get_id())
		{
			std::string Ignored;
			bFinalizing = true;
			Close(Ignored);
		}
	}

	std::shared_ptr<FContext> FContext::Create(std::unique_ptr<IBackendContext> InBackend, const FViewport& InViewport,
	    std::string& Error)
	{
		if (!ValidateViewport(InViewport, Error))
		{
			return {};
		}
		if (!InBackend || !InBackend->GetCapabilities().bLayout)
		{
			Error = "gui.backend_layout_required";
			return {};
		}
		Retired();
		return std::shared_ptr<FContext>(new FContext(std::move(InBackend), InViewport),
		    [](FContext* Value)
		{
			Value->bFinalizing = true;
			Value->bAcceptingWork.store(false, std::memory_order_release);
			std::string Ignored;
			if (Value->Thread == std::this_thread::get_id() && Value->Close(Ignored))
			{
				delete Value;
				return;
			}
			auto& Queue = Retired();
			std::lock_guard<std::mutex> Guard(Queue.Mutex);
			Value->RetiredNext = Queue.Head;
			Queue.Head = Value;
		});
	}

	bool FContext::CollectRetired(std::string& Error)
	{
		Error.clear();
		auto& Queue = Retired();
		FContext* Ready = nullptr;
		{
			std::lock_guard<std::mutex> Guard(Queue.Mutex);
			for (auto** Link = &Queue.Head; *Link;)
			{
				auto* Value = *Link;
				if (Value->Thread != std::this_thread::get_id())
				{
					Link = &Value->RetiredNext;
					continue;
				}
				*Link = Value->RetiredNext;
				Value->RetiredNext = Ready;
				Ready = Value;
			}
		}
		while (Ready)
		{
			auto* Value = Ready;
			Ready = Value->RetiredNext;
			std::string Failure;
			if (Value->Close(Failure))
			{
				delete Value;
			}
			else
			{
				if (Error.empty())
				{
					Error = std::move(Failure);
				}
				std::lock_guard<std::mutex> Guard(Queue.Mutex);
				Value->RetiredNext = Queue.Head;
				Queue.Head = Value;
			}
		}
		return Error.empty();
	}

	bool FContext::SetRoot(std::shared_ptr<FWidget> Value, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (bUpdating || FWidget::IsInLifecycle())
		{
			Error = "gui.tree_during_update";
			return false;
		}
		if (Value == Root)
		{
			return true;
		}
		if (Value && (!Value->Check(Error, true, true) || !Value->Parent.expired() || Value->IsMounted()))
		{
			if (Error.empty())
			{
				Error = "gui.root_already_attached";
			}
			return false;
		}
		auto Previous = Root;
		Root = std::move(Value);
		std::vector<std::shared_ptr<FWidget>> Nodes;
		if (!Gather(Nodes, Error))
		{
			Root = std::move(Previous);
			return false;
		}
		FFlagGuard Guard(bUpdating);
		std::string LifecycleError;
		if (Previous)
		{
			Previous->Unmount(LifecycleError);
		}
		if (Root)
		{
			Root->Mount(shared_from_this());
		}
		LayoutNodes.clear();
		SceneStyles.reset();
		FloatingStates.clear();
		bHasModalControls = false;
		bDirty = true;
		bReady = false;
		CleanInputState();
		const bool bServicesReady = RefreshInputServices(Error);
		if (!LifecycleError.empty())
		{
			Error = std::move(LifecycleError);
		}
		return bServicesReady && Error.empty();
	}

	std::shared_ptr<FWidget> FContext::GetRoot() const
	{
		return Root;
	}

	bool FContext::SetViewport(const FViewport& Value, std::string& Error)
	{
		if (!Check(Error) || !ValidateViewport(Value, Error))
		{
			return false;
		}
		if (bUpdating || bDispatching)
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		Viewport = Value;
		bDirty = true;
		bReady = false;
		return true;
	}

	bool FContext::SetTheme(std::shared_ptr<const FStyleSheet> Value, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (bUpdating || FWidget::IsInLifecycle())
		{
			Error = "gui.theme_during_update";
			return false;
		}
		if (Theme != Value)
		{
			FFlagGuard Guard(bUpdating);
			Theme = std::move(Value);
			bDirty = true;
			bReady = false;
		}
		return true;
	}

	bool FContext::SetResourceReader(FResourceReader Reader, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (bUpdating || bDispatching || FWidget::IsInLifecycle())
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		FFlagGuard Guard(bUpdating);
		return Backend->SetResourceReader(std::move(Reader), Error);
	}

	bool FContext::LoadFont(const FFontFace& Font, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error) || !ValidateFontFace(Font, Error))
		{
			return false;
		}
		if (bUpdating || bDispatching || FWidget::IsInLifecycle())
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		FFlagGuard Guard(bUpdating);
		if (!Backend->LoadFont(Font, Error))
		{
			return false;
		}
		bReady = false;
		bDirty = true;
		return true;
	}

	bool FContext::SetFrameService(std::string Id, std::shared_ptr<IFrameService> Service, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (Id.empty() || bUpdating || FWidget::IsInLifecycle())
		{
			Error = "gui.invalid_frame_service_operation";
			return false;
		}
		FFlagGuard Guard(bUpdating);
		if (Service)
		{
			FrameServices[std::move(Id)] = std::move(Service);
		}
		else
		{
			FrameServices.erase(Id);
		}
		bDirty = true;
		bReady = false;
		return true;
	}

	std::shared_ptr<IFrameService> FContext::GetFrameService(const std::string& Id, std::string& Error) const
	{
		Error.clear();
		if (Thread != std::this_thread::get_id() || !IsOpen())
		{
			Error = Thread != std::this_thread::get_id() ? "gui.wrong_thread" : "gui.context_closed";
			return {};
		}
		const auto Found = FrameServices.find(Id);
		return Found == FrameServices.end() ? nullptr : Found->second;
	}

	bool FContext::SetWindowMetrics(std::uint64_t InWindowId, const Platform::FWindowMetrics& Metrics,
	    std::string& Error)
	{
		if (!std::isfinite(Metrics.ScaleX) || !std::isfinite(Metrics.ScaleY) || Metrics.ScaleX <= 0 ||
		    Metrics.ScaleY <= 0)
		{
			Error = "gui.invalid_window_scale";
			return false;
		}
		if (!SetViewport({Metrics.PixelWidth, Metrics.PixelHeight, Metrics.DisplayScale}, Error))
		{
			return false;
		}
		WindowId = InWindowId;
		WindowScaleX = Metrics.ScaleX;
		WindowScaleY = Metrics.ScaleY;
		return true;
	}

	bool FContext::Update(double ElapsedSeconds, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (bUpdating || bDraining || bDispatching || FWidget::IsInLifecycle())
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		bReady = false;
		if (!std::isfinite(ElapsedSeconds) || ElapsedSeconds < LastTime)
		{
			Error = "gui.invalid_time";
			return false;
		}
		// Deferred work runs before entering the immutable analysis/layout phase.
		std::vector<std::function<bool(std::string&)>> Work;
		{
			std::lock_guard<std::mutex> Lock(PendingMutex);
			Work = std::move(Pending);
			Pending.clear();
		}
		std::size_t WorkIndex = 0;
		try
		{
			{
				FFlagGuard DrainGuard(bDraining);
				for (; WorkIndex < Work.size(); ++WorkIndex)
				{
					if (!Work[WorkIndex](Error))
					{
						if (IsOpen())
						{
							std::lock_guard<std::mutex> Lock(PendingMutex);
							Pending.insert(Pending.begin(), std::make_move_iterator(Work.begin() + WorkIndex + 1),
							    std::make_move_iterator(Work.end()));
						}
						if (Error.empty())
						{
							Error = "gui.deferred_work_failed";
						}
						return false;
					}
					if (!IsOpen())
					{
						Error = "gui.context_closed";
						return false;
					}
				}
			}
			FFlagGuard Guard(bUpdating);
			for (const auto& Service : FrameServices)
			{
				if (!Service.second->BeginFrame(ElapsedSeconds, Error))
				{
					if (Error.empty())
					{
						Error = "gui.frame_service_failed: " + Service.first;
					}
					return false;
				}
			}
			for (const auto& Service : FrameServices)
			{
				const auto Bindings = dynamic_cast<IFrameBindingService*>(Service.second.get());
				if (Bindings && !Bindings->FlushBindings(Error))
				{
					if (Error.empty())
					{
						Error = "gui.frame_service_failed: " + Service.first;
					}
					return false;
				}
			}
			++Frame;
			bNextFrameDirty = false;
			std::vector<std::shared_ptr<FWidget>> Nodes;
			if (!Gather(Nodes, Error))
			{
				return false;
			}
			for (const auto& Node : Nodes)
			{
				if (Node->Markup && !Node->Markup->SynchronizeScripts(*this, Error))
				{
					return false;
				}
			}
			std::vector<std::vector<FWidget*>> Levels;
			if (std::any_of(Nodes.begin(), Nodes.end(),
			        [](const auto& Node)
			{
				return !Node->bInitialized || !Node->bMountNotified;
			}))
			{
				std::unordered_map<FWidget*, std::size_t> Depth;
				for (const auto& Node : Nodes)
				{
					const auto Parent = Node->Parent.lock();
					const auto Level = Parent ? Depth.at(Parent.get()) + 1 : 0;
					if (Level == Levels.size())
					{
						Levels.emplace_back();
					}
					Levels[Level].push_back(Node.get());
					Depth.emplace(Node.get(), Level);
				}
			}
			bool bStable = false;
			SceneStyles = FMarkupRuntime::BuildScene(Nodes, Theme, SceneStyles);
			unsigned UnstablePasses = 0;
			while (UnstablePasses++ < 32)
			{
				bDirty = false;
				++Round;
				if (!SceneStyles->Apply(*this, Round, Error))
				{
					return false;
				}
				const auto Synchronization = NextSynchronizationToken();
				{
					FFlagGuard SynchronizeGuard(bSynchronizingScripts);
					for (const auto& Node : Nodes)
					{
						Node->SynchronizationToken = Synchronization;
						// Legacy markup contexts already synchronize through their owning runtime.
						if (!Node->Markup && !Node->NotifyLifecycle(EWidgetLifecycle::Synchronize, Error)) return false;
					}
				}
				for (const auto& Node : Nodes)
				{
					for (std::size_t Index = 0; Index < Node->Properties.size(); ++Index)
					{
						if (!Node->Evaluate(static_cast<EProperty>(Index), Round, false, Error))
						{
							return false;
						}
					}
				}
				// Commit values before notifying, so hooks never execute inside a binding getter.
				for (const auto& Node : Nodes)
				{
					if (Node->Markup && !Node->Markup->Apply(*this, Round, Error))
					{
						return false;
					}
				}
				if (!PrepareAdditionalControls(Nodes, ElapsedSeconds, false, Error))
				{
					return false;
				}
				if (bDirty)
				{
					continue;
				}
				for (const auto& Node : Nodes)
				{
					for (std::size_t Index = 0; Index < Node->Properties.size(); ++Index)
					{
						auto& Cell = Node->Properties[Index];
						if (Cell.bNotify)
						{
							Cell.bNotify = false;
							try
							{
								Node->OnPropertyChanged(static_cast<EProperty>(Index));
							}
							catch (...)
							{
								Cell.bNotify = true;
								throw;
							}
						}
					}
				}
				if (bDirty)
				{
					continue;
				}
				for (auto Level = Levels.rbegin(); Level != Levels.rend(); ++Level)
				{
					for (auto* Node : *Level)
					{
						if (!Node->bInitialized)
						{
							if (!Node->NotifyLifecycle(EWidgetLifecycle::Initialized, Error))
							{
								return false;
							}
							Node->OnInitialized();
							Node->bInitialized = true;
							// One-time lifecycle progress is not a property stabilization cycle.
							UnstablePasses = 0;
						}
					}
					// Batch siblings; stabilize before initializing their parents.
					if (bDirty)
					{
						break;
					}
				}
				if (bDirty)
				{
					continue;
				}
				for (const auto& Level : Levels)
				{
					for (auto* Node : Level)
					{
						if (!Node->bMountNotified)
						{
							if (!Node->NotifyLifecycle(EWidgetLifecycle::Mounted, Error))
							{
								return false;
							}
							Node->OnMounted();
							Node->bMountNotified = true;
							UnstablePasses = 0;
						}
					}
					if (bDirty)
					{
						break;
					}
				}
				if (!bDirty)
				{
					bStable = true;
					break;
				}
			}
			if (!bStable)
			{
				Error = "gui.update_cycle: properties did not stabilize after 32 passes";
				return false;
			}
			if (!PrepareAdditionalControls(Nodes, ElapsedSeconds, true, Error))
			{
				return false;
			}
			std::vector<FBackendNode> Snapshot;
			Snapshot.reserve(Nodes.size());
			for (const auto& Node : Nodes)
			{
				FBackendNode Item;
				Item.Id = Node->Identity;
				Item.Control = Node->Control;
				if (Item.Control.bModal)
				{
					const auto State = FloatingStates.find(Node->Identity);
					Item.Control.ModalOrder = State != FloatingStates.end() ? State->second.ModalOrder : 0;
				}
				if (Item.Control.Kind >= EControlKind::Grid && !Backend->GetCapabilities().bExtendedControls)
				{
					Error = "gui.extended_controls_unavailable";
					return false;
				}
				if (!ValidateControlOptions(Item.Control, Error))
				{
					return false;
				}
				if (Item.Control.Kind == EControlKind::ScrollView && !Backend->GetCapabilities().bScrolling)
				{
					Error = "gui.scrolling_unavailable";
					return false;
				}
				if (IsTextControl(Item.Control.Kind) && !Backend->GetCapabilities().bTextEditing)
				{
					Error = "gui.text_editing_unavailable";
					return false;
				}
				// Plain elements with no skin or callback have no custom paint work.
				// Keep native subclasses: their virtual OnPaint may be overridden.
				if (Node->Control.Kind != EControlKind::None || Node->Control.bSelected || Node->Control.bExpandable ||
				    Node->PaintCallback || typeid(*Node) != typeid(FWidget))
				{
					Item.Paint = [Weak = std::weak_ptr<FWidget>(Node)](FPaintContext& Canvas, std::string& Failure)
					{
						const auto Widget = Weak.lock();
						if (!Widget || Widget->bDisposed)
						{
							Failure = "gui.widget_expired";
							return false;
						}
						if ((Widget->Control.Kind >= EControlKind::CheckBox &&
						        Widget->Control.Kind <= EControlKind::ScrollBar) ||
						    Widget->Control.Kind == EControlKind::RadioButton ||
						    Widget->Control.Kind == EControlKind::Splitter || Widget->Control.bSelected ||
						    Widget->Control.bExpandable)
						{
							FPropertyValue Color;
							if (!Widget->Get(EProperty::TextColor, Color, Failure) ||
							    !PaintControl(Widget->Control, std::get<Graphics::FColor>(Color),
							        Widget->ResolvedProperties.LineHeight > 0 ? Widget->ResolvedProperties.LineHeight : Widget->ResolvedProperties.FontSize * 1.2, Canvas, Failure))
							{
								return false;
							}
						}
						Widget->OnPaint(Canvas);
						return !Widget->PaintCallback || Widget->PaintCallback(Canvas, Failure);
					};
				}
				if (const auto Parent = Node->Parent.lock())
				{
					Item.Parent = Parent->Identity;
				}
				for (auto& Cell : Node->Properties)
				{
					Node->bResolvedPropertiesDirty = Node->bResolvedPropertiesDirty || Cell.Animation.has_value();
					if (Cell.Animation && !Cell.Animation->Sample(ElapsedSeconds, Cell.Presentation, Error))
					{
						return false;
					}
					// A held endpoint belongs to the presentation layer. The completed
					// track has no further samples, even while that endpoint remains visible.
					if (Cell.Animation && Cell.Animation->IsFinished())
					{
						Cell.Animation.reset();
					}
				}
				if (Node->bResolvedPropertiesDirty)
				{
					std::array<FPropertyValue, static_cast<std::size_t>(EProperty::Count)> Values;
					for (std::size_t Index = 0; Index < Values.size(); ++Index)
					{
						const auto& Cell = Node->Properties[Index];
						Values[Index] = Cell.Presentation.value_or(Cell.Value);
					}
					if (!ResolveProperties(Values, Node->ResolvedProperties, Error))
					{
						return false;
					}
					Node->bResolvedPropertiesDirty = false;
				}
				Item.Properties = Node->ResolvedProperties;
				if (!Node->bContentVisible)
				{
					Item.Properties.Visibility = EVisibility::Collapsed;
				}
				// The indicator is part of the control skin, outside user content padding.
				if (Item.Control.Kind == EControlKind::CheckBox || Item.Control.Kind == EControlKind::RadioButton)
				{
					Item.Properties.Padding.Left += 20;
				}
				if (Item.Control.Kind == EControlKind::ToggleSwitch)
				{
					Item.Properties.Padding.Left += 40;
				}
				if (Item.Control.bItemIndicator)
				{
					Item.Properties.Padding.Left += 18;
				}
				Snapshot.push_back(std::move(Item));
			}
			std::vector<FLayoutBox> Boxes;
			if (!Backend->Synchronize(Viewport, Snapshot, Error) || !Backend->Update(ElapsedSeconds, Boxes, Error))
			{
				return false;
			}
			std::unordered_map<std::uint64_t, FLayoutBox> Geometry;
			for (const auto& Box : Boxes)
			{
				if (!ValidRect(Box.Border) || !ValidRect(Box.Content) || !std::isfinite(Box.ScrollWidth) ||
				    !std::isfinite(Box.ScrollHeight) || !std::isfinite(Box.ScrollX) || !std::isfinite(Box.ScrollY) ||
				    Box.ScrollWidth < 0 || Box.ScrollHeight < 0 || Box.ScrollX < 0 || Box.ScrollY < 0 ||
				    !Geometry.emplace(Box.Id, Box).second)
				{
					Error = "gui.invalid_backend_layout";
					return false;
				}
			}
			if (Geometry.size() != Nodes.size())
			{
				Error = "gui.incomplete_backend_layout";
				return false;
			}
			std::vector<std::weak_ptr<FWidget>> PublishedNodes;
			PublishedNodes.reserve(Nodes.size());
			for (const auto& Node : Nodes)
			{
				if (!Geometry.count(Node->Identity))
				{
					Error = "gui.unknown_backend_node";
					return false;
				}
				PublishedNodes.push_back(Node);
			}
			std::vector<std::shared_ptr<FWidget>> ChangedGeometry;
			for (const auto& Node : Nodes)
			{
				const auto& Next = Geometry.at(Node->Identity);
				const auto Equal = [](const FRect& A, const FRect& B)
				{
					return A.X == B.X && A.Y == B.Y && A.Width == B.Width && A.Height == B.Height;
				};
				if (!Node->bHasLayout || !Equal(Node->Layout.Border, Next.Border) ||
				    !Equal(Node->Layout.Content, Next.Content) ||
				    Node->Layout.ScrollWidth != Next.ScrollWidth || Node->Layout.ScrollHeight != Next.ScrollHeight)
				{
					Node->bLayoutNotificationPending = true;
				}
				if (Node->bLayoutNotificationPending)
				{
					// Geometry stays readable; failed and unvisited observers remain pending for the next update.
					ChangedGeometry.push_back(Node);
				}
				Node->Layout = Next;
				Node->bHasLayout = true;
			}
			{
				FFlagGuard LayoutGuard(bNotifyingLayout);
				for (const auto& Node : ChangedGeometry)
				{
					if (!Node->NotifyLifecycle(EWidgetLifecycle::LayoutChanged, Error))
					{
						return false;
					}
					Node->bLayoutNotificationPending = false;
				}
			}

			LayoutNodes = std::move(PublishedNodes);
			// Portals retain declaration order within a modal group. A newly opened
			// modal and its descendant portals are above all earlier groups.
			if (!FloatingStates.empty())
			{
				std::unordered_map<std::uint64_t, std::size_t> Ranks;
				std::vector<std::vector<std::weak_ptr<FWidget>>> Layers(1);
				std::vector<std::vector<std::uint64_t>> Orders(1);
				for (const auto& Node : Nodes)
				{
					const auto Parent = Node->Parent.lock();
					if (IsFloatingControl(Node->Control.Kind))
					{
						Layers.emplace_back();
						std::vector<std::uint64_t> Order;
						for (auto Ancestor = Node; Ancestor; Ancestor = Ancestor->Parent.lock())
						{
							if (Ancestor->Control.bModal && Ancestor->Control.bOpen)
							{
								Order.push_back(FloatingStates.at(Ancestor->Identity).ModalOrder);
							}
						}
						std::sort(Order.rbegin(), Order.rend());
						Orders.push_back(std::move(Order));
					}
					const auto Rank = IsFloatingControl(Node->Control.Kind) ? Layers.size() - 1
					                                                        : (Parent ? Ranks.at(Parent->Identity) : 0);
					Ranks.emplace(Node->Identity, Rank);
					Layers[Rank].push_back(Node);
				}
				LayoutNodes.clear();
				std::vector<std::size_t> Indices;
				for (std::size_t Index = 0; Index < Layers.size(); ++Index)
				{
					Indices.push_back(Index);
				}
				std::stable_sort(Indices.begin(), Indices.end(), [&](auto A, auto B)
				{
					return Orders[A] < Orders[B];
				});
				for (const auto Index : Indices)
				{
					auto& Layer = Layers[Index];
					LayoutNodes.insert(LayoutNodes.end(), std::make_move_iterator(Layer.begin()),
					    std::make_move_iterator(Layer.end()));
				}
			}
			LastTime = ElapsedSeconds;
			bReady = true;
			CleanInputState();
			if (!RefreshModalFocus(Error)) return false;
			RefreshHover();
			return RefreshInputServices(Error);
		}
		catch (const std::exception& Exception)
		{
			Error = std::string("gui.callback_exception: ") + Exception.what();
		}
		catch (...)
		{
			Error = "gui.callback_exception";
		}
		if (IsOpen() && WorkIndex < Work.size())
		{
			std::lock_guard<std::mutex> Lock(PendingMutex);
			Pending.insert(Pending.begin(), std::make_move_iterator(Work.begin() + WorkIndex + 1),
			    std::make_move_iterator(Work.end()));
		}
		return false;
	}

	bool FContext::Render(bool& bPresented, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		bPresented = false;
		if (!Check(Error))
		{
			return false;
		}
		if (bUpdating || bDraining || bDispatching || FWidget::IsInLifecycle() || !bReady || bDirty)
		{
			Error = "gui.update_required";
			return false;
		}
		FFlagGuard Guard(bRendering);
		try
		{
			return Backend->Render(bPresented, Error);
		}
		catch (const std::exception& Exception)
		{
			Error = std::string("gui.backend_exception: ") + Exception.what();
		}
		catch (...)
		{
			Error = "gui.backend_exception";
		}
		bPresented = false;
		return false;
	}

	bool FContext::ProcessEvent(const Platform::FEvent& Input, bool& bHandled, std::string& Error)
	{
		FEventResult Result;
		const bool bSuccess = ProcessEvent(Input, Result, Error);
		bHandled = Result.bHandled;
		return bSuccess;
	}

	bool FContext::ProcessEvent(const Platform::FEvent& Input, FEventResult& Result, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		Result = {};
		if (!Check(Error) || !Platform::ValidateEvent(Input, Error))
		{
			return false;
		}
		if (bUpdating || bDraining || bDispatching || FWidget::IsInLifecycle())
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		if (WindowId && Input.WindowId && WindowId != Input.WindowId)
		{
			return true;
		}
		FFlagGuard Guard(bDispatching);
		using Platform::EEventType;
		if (Input.Type == EEventType::InputDeviceRemoved)
		{
			const auto Kind = Platform::GetInputDeviceKind(Input);
			bool bCancellationSucceeded = true;
			// End selection dragging before dropping the removed device's capture.
			for (const auto& Press : ControlPresses)
			{
				if (Press.first.Kind == Kind && Press.first.DeviceId == Input.DeviceId)
				{
					if (const auto Widget = Press.second.Widget.lock(); Widget && Widget->Identity == TextFocus)
					{
						auto Cancel = Input;
						Cancel.Type = EEventType::PointerCancelled;
						Cancel.PointerKind = Kind == Platform::EInputDeviceKind::Touch ? Platform::EPointerKind::Touch
						                                                               : Platform::EPointerKind::Mouse;
						Cancel.PointerId = Press.first.PointerId;
						FTextEditResult Ignored;
						try
						{
							bCancellationSucceeded = Backend->EditText(Widget->Identity, Cancel, Ignored, Error);
						}
						catch (const std::exception& Exception)
						{
							Error = std::string("gui.callback_exception: ") + Exception.what();
							bCancellationSucceeded = false;
						}
						catch (...)
						{
							Error = "gui.callback_exception";
							bCancellationSucceeded = false;
						}
						break;
					}
				}
			}
			if (Kind == Platform::EInputDeviceKind::Keyboard && ControlKeyboardDevice == Input.DeviceId)
			{
				ActiveControl.reset();
				ControlKey = Platform::EKey::Unknown;
			}
			for (auto It = ScrollGestures.begin(); It != ScrollGestures.end();)
			{
				if (It->first.Kind == Kind && It->first.DeviceId == Input.DeviceId)
				{
					It = ScrollGestures.erase(It);
				}
				else
				{
					++It;
				}
			}

			const auto RemoveDevice = [&](auto& Values)
			{
				for (auto It = Values.begin(); It != Values.end();)
				{
					if (It->first.Kind == Kind && It->first.DeviceId == Input.DeviceId)
					{
						It = Values.erase(It);
					}
					else
					{
						++It;
					}
				}
			};
			RemoveDevice(Hovered);
			RemoveDevice(Captures);
			RemoveDevice(MousePositions);
			if (LastMouse && LastMouse->Kind == Kind && LastMouse->DeviceId == Input.DeviceId)
			{
				LastMouse.reset();
				MouseHit.reset();
				bMouseInside = false;
			}
			for (auto It = Pressed.begin(); It != Pressed.end();)
			{
				if (It->first.first.Kind == Kind && It->first.first.DeviceId == Input.DeviceId)
				{
					It = Pressed.erase(It);
				}
				else
				{
					++It;
				}
			}
			if (!bCancellationSucceeded)
			{
				// Physical removal still invalidates capture even when the host service fails.
				CleanInputState();
				std::string Ignored;
				RefreshPointerServices(Ignored);
				return false;
			}
		}
		if (Input.Type == EEventType::MouseLeft)
		{
			bMouseInside = false;
			Hovered.clear();
		}
		else if (Input.Type == EEventType::MouseEntered)
		{
			bMouseInside =
			    FRect{0, 0, Viewport.PixelWidth / Viewport.DisplayScale, Viewport.PixelHeight / Viewport.DisplayScale}
			        .Contains(Input.X * WindowScaleX / Viewport.DisplayScale,
			            Input.Y * WindowScaleY / Viewport.DisplayScale);
			if (!LastMouse)
			{
				LastMouse = FPointerIdentity{Platform::EInputDeviceKind::Mouse, 0, 0};
			}
			MousePositions[*LastMouse] = {Input.X, Input.Y};
			RefreshHover();
		}
		const bool bPointer = Input.Type == EEventType::PointerMove || Input.Type == EEventType::PointerDown ||
		    Input.Type == EEventType::PointerUp || Input.Type == EEventType::PointerCancelled ||
		    Input.Type == EEventType::Wheel;
		const bool bKeyboard = Input.Type == EEventType::KeyDown || Input.Type == EEventType::KeyUp ||
		    Input.Type == EEventType::TextInput || Input.Type == EEventType::TextEditing;
		if (Input.Type == EEventType::FocusGained || Input.Type == EEventType::Foreground ||
		    Input.Type == EEventType::PointerDown || Input.Type == EEventType::KeyDown ||
		    Input.Type == EEventType::Shown || Input.Type == EEventType::Restored)
		{
			bInputActive = true;
		}
		std::shared_ptr<FWidget> LostFocus;
		if (Input.Type == EEventType::FocusLost || Input.Type == EEventType::Hidden ||
		    Input.Type == EEventType::Minimized || Input.Type == EEventType::Background ||
		    Input.Type == EEventType::InputStateReset)
		{
			bInputActive = false;
			LostFocus = Focus.lock();
			Focus.reset();
			CancelControlPress();
			Captures.clear();
			Hovered.clear();
			MousePositions.clear();
			LastMouse.reset();
			MouseHit.reset();
			bMouseInside = false;
			Pressed.clear();
		}
		// Input uses the last completed layout. Event handlers may dirty properties while a host drains its event
		// queue.
		if ((bPointer || bKeyboard) && Root && !Root->bHasLayout)
		{
			Error = "gui.update_required";
			return false;
		}
		CleanInputState();
		FEvent Event;
		Event.Input = Input;
		const auto Pointer = FPointerIdentity::FromEvent(Input);
		if (bPointer)
		{
			Event.Input.X *= WindowScaleX / Viewport.DisplayScale;
			Event.Input.Y *= WindowScaleY / Viewport.DisplayScale;
			if (Input.PointerKind == Platform::EPointerKind::Mouse && Input.Type != EEventType::PointerCancelled)
			{
				LastMouse = Pointer;
				bMouseInside = FRect{0, 0, Viewport.PixelWidth / Viewport.DisplayScale,
				    Viewport.PixelHeight / Viewport.DisplayScale}
				                   .Contains(Event.Input.X, Event.Input.Y);
				MousePositions[Pointer] = {Input.X, Input.Y};
				MouseHit = HitTest(Event.Input.X, Event.Input.Y);
				Hovered[Pointer] = MouseHit;
			}
			if (Input.Type == EEventType::PointerMove)
			{
				Event.Input.DeltaX *= WindowScaleX / Viewport.DisplayScale;
				Event.Input.DeltaY *= WindowScaleY / Viewport.DisplayScale;
			}
			if (const auto It = Captures.find(Pointer); It != Captures.end())
			{
				Event.Target = It->second.lock();
			}
			if (!Event.Target)
			{
				Event.Target =
				    Input.PointerKind == Platform::EPointerKind::Mouse && Input.Type != EEventType::PointerCancelled
				    ? MouseHit.lock()
				    : HitTest(Event.Input.X, Event.Input.Y);
			}
			if (Event.Target && !Eligible(Event.Target, true, false))
			{
				Event.Target.reset();
			}
			if (Input.Type == EEventType::PointerDown && Event.Target)
			{
				Pressed[{Pointer, Input.Button}] = Event.Target;
			}
			else if (Input.Type == EEventType::PointerUp)
			{
				Pressed.erase({Pointer, Input.Button});
			}
			else if (Input.Type == EEventType::PointerCancelled)
			{
				if (Input.PointerKind == Platform::EPointerKind::Mouse)
				{
					Hovered.erase(Pointer);
					MousePositions.erase(Pointer);
					if (LastMouse && *LastMouse == Pointer)
					{
						LastMouse.reset();
						MouseHit.reset();
						bMouseInside = false;
					}
				}
				for (auto It = Pressed.begin(); It != Pressed.end();)
				{
					if (It->first.first == Pointer)
					{
						It = Pressed.erase(It);
					}
					else
					{
						++It;
					}
				}
			}
		}
		else if (bKeyboard)
		{
			Event.Target = Focus.lock();
		}
		else
		{
			Event.Target = Root;
		}
		if (const auto Modal = GetModalRoot(); Modal && bPointer)
		{
			auto Within = Event.Target;
			while (Within && Within != Modal)
			{
				Within = Within->Parent.lock();
			}
			if (!Within)
			{
				Result.bHandled = Result.bDefaultPrevented = true;
				if (Input.Type == EEventType::PointerDown && Modal->Control.bDismissOnOutside)
				{
					FControlChange Change;
					Change.Action = EControlAction::OpenChanged;
					Change.bChecked = false;
					Change.Text = "outside";
					if (!NotifyControl(Modal, std::move(Change), Error)) return false;
				}
				return RefreshInputServices(Error);
			}
		}
		if (!Event.Target && bKeyboard)
		{
			Event.Target = GetModalRoot();
		}
		if (!Event.Target && Input.Type != EEventType::PointerDown &&
		    !(Input.Type == EEventType::KeyDown &&
		        (Input.Key == Platform::EKey::Tab || Input.Key == Platform::EKey::Escape)))
		{
			return RefreshInputServices(Error);
		}
		bool bResult = !Event.Target || Dispatch(Event, Error);
		bool bControlHandled = false;
		if (bResult && Event.bDefaultPrevented && Event.Target && IsTextControl(Event.Target->Control.Kind) &&
		    (Input.Type == EEventType::TextInput || Input.Type == EEventType::TextEditing) &&
		    Backend->IsTextComposing(Event.Target->Identity))
		{
			auto Cancel = Event.Input;
			Cancel.Type = EEventType::TextEditing;
			Cancel.Text.clear();
			FTextEditResult Ignored;
			bResult = Backend->EditText(Event.Target->Identity, Cancel, Ignored, Error);
		}

		if (bResult)
		{
			try
			{
				bResult = CommitNumber(LostFocus, false, Error);
				if (bResult && !Event.bDefaultPrevented)
				{
					bResult = PerformControlAction(Event, bControlHandled, Error);
				}
			}
			catch (const std::exception& Exception)
			{
				Error = std::string("gui.callback_exception: ") + Exception.what();
				bResult = false;
			}
			catch (...)
			{
				Error = "gui.callback_exception";
				bResult = false;
			}
		}
		if (Input.Type == EEventType::PointerCancelled ||
		    (Input.Type == EEventType::PointerUp &&
		        (Input.Button == 1 || Input.PointerKind == Platform::EPointerKind::Touch)))
		{
			if (ControlPresses.erase(Pointer))
			{
				Captures.erase(Pointer);
			}
			if (ScrollGestures.erase(Pointer))
			{
				Captures.erase(Pointer);
			}
		}
		if (Input.Type == EEventType::KeyUp && ControlKey == Input.Key && ControlKeyboardDevice == Input.DeviceId)
		{
			ActiveControl.reset();
			ControlKey = Platform::EKey::Unknown;
		}
		Result.bDefaultPrevented = Event.bDefaultPrevented;
		Result.bPropagationStopped = Event.bPropagationStopped;
		Result.bHandled = bControlHandled || Event.bDefaultPrevented || Event.bPropagationStopped;
		if (!Event.bDefaultPrevented && (Input.Type == EEventType::CloseRequested || Input.Type == EEventType::Quit))
		{
			Focus.reset();
			CancelControlPress();
			Captures.clear();
			Hovered.clear();
			MousePositions.clear();
			LastMouse.reset();
			MouseHit.reset();
			bMouseInside = false;
			Pressed.clear();
		}
		if (Input.Type == EEventType::PointerCancelled)
		{
			Captures.erase(Pointer);
		}
		CleanInputState();
		if (!bResult)
		{
			std::string Ignored;
			RefreshInputServices(Ignored);
			return false;
		}
		return RefreshInputServices(Error);
	}

	bool FContext::Post(std::function<bool(std::string&)> Work, std::string& Error)
	{
		Error.clear();
		if (!Work)
		{
			Error = "gui.empty_work";
			return false;
		}
		std::lock_guard<std::mutex> Lock(PendingMutex);
		if (!bAcceptingWork.load(std::memory_order_acquire))
		{
			Error = "gui.context_closed";
			return false;
		}
		Pending.push_back(std::move(Work));
		return true;
	}

	bool FContext::SetFocus(std::shared_ptr<FWidget> Value, std::string& Error)
	{
		return SetFocusTarget(std::move(Value), true, Error);
	}

	std::shared_ptr<FWidget> FContext::GetFocus() const
	{
		const auto KeepAlive = weak_from_this().lock();
		if (Thread != std::this_thread::get_id())
		{
			return {};
		}
		const auto Value = Focus.lock();
		return Eligible(Value, false, true) ? Value : nullptr;
	}

	bool FContext::CapturePointer(std::uint64_t PointerId, std::shared_ptr<FWidget> Value, std::string& Error)
	{
		return CapturePointer({Platform::EInputDeviceKind::Mouse, 0, PointerId}, std::move(Value), Error);
	}

	bool FContext::ReleasePointer(std::uint64_t PointerId, std::string& Error)
	{
		return ReleasePointer({Platform::EInputDeviceKind::Mouse, 0, PointerId}, Error);
	}

	bool FContext::CapturePointer(const FPointerIdentity& Pointer, std::shared_ptr<FWidget> Value, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (!Eligible(Value, true, false))
		{
			Error = "gui.invalid_capture_target";
			return false;
		}
		if (Pointer.Kind != Platform::EInputDeviceKind::Mouse && Pointer.Kind != Platform::EInputDeviceKind::Touch)
		{
			Error = "gui.invalid_pointer_identity";
			return false;
		}
		const auto Previous = Captures.find(Pointer);
		const auto Saved = Previous == Captures.end() ? std::weak_ptr<FWidget>{} : Previous->second;
		Captures[Pointer] = Value;
		if (!RefreshPointerServices(Error))
		{
			if (Saved.expired())
			{
				Captures.erase(Pointer);
			}
			else
			{
				Captures[Pointer] = Saved;
			}
			std::string Ignored;
			RefreshPointerServices(Ignored);
			return false;
		}
		return true;
	}

	bool FContext::ReleasePointer(const FPointerIdentity& Pointer, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (Pointer.Kind != Platform::EInputDeviceKind::Mouse && Pointer.Kind != Platform::EInputDeviceKind::Touch)
		{
			Error = "gui.invalid_pointer_identity";
			return false;
		}
		Captures.erase(Pointer);
		return RefreshPointerServices(Error);
	}

	bool FContext::IsEffectivelyEnabled(const std::shared_ptr<FWidget>& Value) const
	{
		const auto KeepAlive = weak_from_this().lock();
		return Thread == std::this_thread::get_id() && Eligible(Value, false, false);
	}

	bool FContext::Close(std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		Error.clear();
		if (Thread != std::this_thread::get_id())
		{
			Error = "gui.wrong_thread";
			return false;
		}
		if (!Backend)
		{
			return true;
		}
		if (bUpdating || bDispatching || bRendering || bRefreshingPointerServices || bRefreshingTextServices ||
		    FWidget::IsEvaluating() || FWidget::IsInLifecycle())
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		FFlagGuard Guard(bUpdating);
		bAcceptingWork.store(false, std::memory_order_release);
		std::string LifecycleError;
		if (Root)
		{
			Root->Unmount(LifecycleError);
		}
		Root.reset();
		Theme.reset();
		SceneStyles.reset();
		FrameServices.clear();
		Focus.reset();
		Captures.clear();
		Hovered.clear();
		MousePositions.clear();
		LastMouse.reset();
		MouseHit.reset();
		bMouseInside = false;
		Pressed.clear();
		std::vector<std::function<bool(std::string&)>> Discarded;
		{
			std::lock_guard<std::mutex> Lock(PendingMutex);
			Discarded = std::move(Pending);
		}
		Discarded.clear();
		LayoutNodes.clear();
		FloatingStates.clear();
		bHasModalControls = false;
		bReady = false;
		if (!ReleasePointerServices(Error))
		{
			return false;
		}
		try
		{
			if (!Backend->Close(Error))
			{
				if (Error.empty())
				{
					Error = "gui.backend_close_failed";
				}
				return false;
			}
		}
		catch (const std::exception& Exception)
		{
			Error = std::string("gui.backend_exception: ") + Exception.what();
			return false;
		}
		catch (...)
		{
			Error = "gui.backend_exception";
			return false;
		}
		Backend.reset();
		CancelControlPress();
		PointerServices = {};
		TextInputServices = {};
		TextFocus = 0;
		Error = std::move(LifecycleError);
		return Error.empty();
	}

	bool FContext::IsOpen() const noexcept
	{
		return Backend != nullptr && bAcceptingWork.load(std::memory_order_acquire);
	}

	FCapabilities FContext::GetCapabilities() const noexcept
	{
		return Backend ? Backend->GetCapabilities() : FCapabilities{};
	}

	FContext::FContext(std::unique_ptr<IBackendContext> InBackend, const FViewport& InViewport)
	    : Thread(std::this_thread::get_id()), Backend(std::move(InBackend)), Viewport(InViewport)
	{
	}

	bool FContext::Check(std::string& Error) const
	{
		Error.clear();
		if (Thread != std::this_thread::get_id())
		{
			Error = "gui.wrong_thread";
		}
		else if (!Backend || !bAcceptingWork.load(std::memory_order_acquire))
		{
			Error = "gui.context_closed";
		}
		else if (bRendering)
		{
			Error = "gui.reentrant_operation: rendering is in progress";
		}
		else if (bRefreshingPointerServices)
		{
			Error = "gui.reentrant_operation: pointer service callback is in progress";
		}
		else if (bRefreshingTextServices)
		{
			Error = "gui.reentrant_operation: text input service callback is in progress";
		}
		else if (FWidget::IsEvaluating())
		{
			Error = "gui.getter_mutation: context operations are not allowed in getters";
		}
		return Error.empty();
	}

	bool FContext::Gather(std::vector<std::shared_ptr<FWidget>>& Nodes, std::string& Error) const
	{
		Nodes.clear();
		if (!Root)
		{
			return true;
		}
		std::vector<std::pair<std::shared_ptr<FWidget>, std::size_t>> Stack{{Root, 1}};
		std::set<std::uint64_t> Identities;
		std::set<std::pair<std::uint64_t, std::string>> Names;
		while (!Stack.empty())
		{
			const auto Item = Stack.back();
			Stack.pop_back();
			const auto& Node = Item.first;
			if (Node->Thread != Thread || Node->bDisposed || Item.second > 256 || Nodes.size() >= 100000 ||
			    !Identities.insert(Node->Identity).second ||
			    (!Node->Id.empty() && !Names.emplace(Node->NameScope, Node->Id).second))
			{
				Error = "gui.invalid_tree: duplicate id, ownership, or tree limit";
				return false;
			}
			Nodes.push_back(Node);
			for (auto Child = Node->Children.rbegin(); Child != Node->Children.rend(); ++Child)
			{
				Stack.emplace_back(*Child, Item.second + 1);
			}
		}
		return true;
	}

	bool FContext::Eligible(const std::shared_ptr<FWidget>& Value, bool bPointer, bool bFocus) const
	{
		if (!Value || Value->bDisposed || Value->bDisposing || Value->Context.lock().get() != this)
		{
			return false;
		}
		if (bPointer || bFocus)
		{
			if (const auto Modal = GetModalRoot())
			{
				auto Ancestor = Value;
				while (Ancestor && Ancestor != Modal)
				{
					Ancestor = Ancestor->Parent.lock();
				}
				if (!Ancestor) return false;
			}
		}
		for (auto Node = Value; Node; Node = Node->Parent.lock())
		{
			if (!Node->bContentVisible || (IsFloatingControl(Node->Control.Kind) && !Node->Control.bOpen) ||
			    ((bPointer || bFocus) && Node->Control.Kind == EControlKind::ToolTip))
			{
				return false;
			}
			FPropertyValue Enabled, Visibility, HitTest;
			std::string Error;
			if (!Node->Get(EProperty::Enabled, Enabled, Error) || !std::get<bool>(Enabled) ||
			    !Node->Get(EProperty::Visibility, Visibility, Error) ||
			    std::get<EVisibility>(Visibility) != EVisibility::Visible)
			{
				return false;
			}
			if (bPointer)
			{
				if (!Node->Get(EProperty::HitTest, HitTest, Error))
				{
					return false;
				}
				const auto Mode = std::get<EHitTestMode>(HitTest);
				if (Mode == EHitTestMode::None || (Node != Value && Mode == EHitTestMode::SelfOnly) ||
				    (Node == Value && Mode == EHitTestMode::ChildrenOnly))
				{
					return false;
				}
			}
		}
		if (bFocus)
		{
			FPropertyValue Focusable;
			std::string Error;
			return Value->Get(EProperty::Focusable, Focusable, Error) && std::get<bool>(Focusable);
		}
		return true;
	}

	std::shared_ptr<FWidget> FContext::GetModalRoot() const
	{
		if (!bHasModalControls) return {};
		std::shared_ptr<FWidget> Result;
		const auto Priority = [&](const std::shared_ptr<FWidget>& Widget)
		{
			std::vector<std::uint64_t> Result;
			for (auto Node = Widget; Node; Node = Node->Parent.lock())
			{
				if (Node->Control.bModal && Node->Control.bOpen)
				{
					Result.push_back(FloatingStates.at(Node->Identity).ModalOrder);
				}
			}
			std::sort(Result.rbegin(), Result.rend());
			return Result;
		};
		for (const auto& Entry : FloatingStates)
		{
			const auto Candidate = Entry.second.Widget.lock();
			if (!Candidate || !Candidate->Control.bModal || !Candidate->Control.bOpen ||
			    Candidate->Context.lock().get() != this)
			{
				continue;
			}
			bool bVisible = true;
			for (auto Node = Candidate; Node; Node = Node->Parent.lock())
			{
				// Modal routing reads the completed property snapshot, never a getter.
				const auto& Visibility = Node->Properties[static_cast<std::size_t>(EProperty::Visibility)];
				const auto& Current = !Visibility.Getter && Visibility.Local ? *Visibility.Local : Visibility.Value;
				if (Node->bDisposed || !Node->bContentVisible ||
				    std::get<EVisibility>(Current) != EVisibility::Visible ||
				    (IsFloatingControl(Node->Control.Kind) && !Node->Control.bOpen))
				{
					bVisible = false;
					break;
				}
			}
			if (bVisible && (!Result || Priority(Candidate) > Priority(Result)))
			{
				Result = Candidate;
			}
		}
		return Result;
	}

	bool FContext::RefreshModalFocus(std::string& Error)
	{
		const auto Modal = bInputActive ? GetModalRoot() : nullptr;
		if (!Modal || Eligible(Focus.lock(), false, true)) return true;
		for (const auto& Weak : LayoutNodes)
		{
			const auto Node = Weak.lock();
			if (Node != Modal && Eligible(Node, false, true)) return SetFocusTarget(Node, false, Error);
		}
		return !Eligible(Modal, false, true) || SetFocusTarget(Modal, false, Error);
	}

	std::shared_ptr<FWidget> FContext::HitTest(double X, double Y) const
	{
		if (!FRect{0, 0, Viewport.PixelWidth / Viewport.DisplayScale, Viewport.PixelHeight / Viewport.DisplayScale}
		        .Contains(X, Y))
		{
			return {};
		}
		std::string Error;
		for (auto It = LayoutNodes.rbegin(); It != LayoutNodes.rend(); ++It)
		{
			const auto Node = It->lock();
			if (!Node || Node->Context.lock().get() != this || !Node->bHasLayout)
			{
				continue;
			}
			const auto Contains = Backend->Contains(Node->Identity, X, Y);
			if (!Contains.value_or(Node->Layout.Border.Contains(X, Y)))
			{
				continue;
			}
			bool bHittable = true;
			for (auto Ancestor = Node; Ancestor; Ancestor = Ancestor->Parent.lock())
			{
				if ((IsFloatingControl(Ancestor->Control.Kind) && !Ancestor->Control.bOpen) ||
				    Ancestor->Control.Kind == EControlKind::ToolTip)
				{
					bHittable = false;
					break;
				}
				FPropertyValue Hit, Visible;
				if (!Ancestor->Get(EProperty::HitTest, Hit, Error) ||
				    !Ancestor->Get(EProperty::Visibility, Visible, Error))
				{
					return {};
				}
				const auto Mode = std::get<EHitTestMode>(Hit);
				if (!Ancestor->bContentVisible || std::get<EVisibility>(Visible) != EVisibility::Visible || Mode == EHitTestMode::None ||
				    (Ancestor != Node && Mode == EHitTestMode::SelfOnly) ||
				    (Ancestor == Node && Mode == EHitTestMode::ChildrenOnly))
				{
					bHittable = false;
					break;
				}
			}
			// Disabled elements occlude elements behind them, but receive no input.
			if (bHittable)
			{
				return Node;
			}
		}
		return {};
	}

	void FContext::CleanInputState()
	{
		for (auto It = ScrollGestures.begin(); It != ScrollGestures.end();)
		{
			if (!Eligible(It->second.Widget.lock(), true, false))
			{
				It = ScrollGestures.erase(It);
			}
			else
			{
				++It;
			}
		}
		if (!Eligible(ActiveControl.lock(), false, true) || ActiveControl.lock() != Focus.lock())
		{
			ActiveControl.reset();
			ControlKey = Platform::EKey::Unknown;
		}
		for (auto It = ControlPresses.begin(); It != ControlPresses.end();)
		{
			if (!Eligible(It->second.Widget.lock(), true, false) || !Captures.count(It->first) ||
			    Captures.at(It->first).lock() != It->second.Widget.lock())
			{
				Captures.erase(It->first);
				It = ControlPresses.erase(It);
			}
			else
			{
				++It;
			}
		}
		if (!Eligible(Focus.lock(), false, true))
		{
			Focus.reset();
		}
		for (auto It = Hovered.begin(); It != Hovered.end();)
		{
			if (!Eligible(It->second.lock(), true, false))
			{
				It = Hovered.erase(It);
			}
			else
			{
				++It;
			}
		}
		for (auto It = Pressed.begin(); It != Pressed.end();)
		{
			if (!Eligible(It->second.lock(), true, false))
			{
				It = Pressed.erase(It);
			}
			else
			{
				++It;
			}
		}
		for (auto It = Captures.begin(); It != Captures.end();)
		{
			if (!Eligible(It->second.lock(), true, false))
			{
				It = Captures.erase(It);
			}
			else
			{
				++It;
			}
		}
	}

	void FContext::RefreshHover()
	{
		if (!bMouseInside)
		{
			MouseHit.reset();
			return;
		}
		for (const auto& Position : MousePositions)
		{
			auto Hit = HitTest(Position.second.first * WindowScaleX / Viewport.DisplayScale,
			    Position.second.second * WindowScaleY / Viewport.DisplayScale);
			if (LastMouse && Position.first == *LastMouse)
			{
				MouseHit = Hit;
			}
			if (!Eligible(Hit, true, false))
			{
				Hit.reset();
			}
			const auto Previous = Hovered.find(Position.first);
			if ((Previous == Hovered.end() ? nullptr : Previous->second.lock()) == Hit)
			{
				continue;
			}
			if (Hit)
			{
				Hovered[Position.first] = Hit;
			}
			else
			{
				Hovered.erase(Position.first);
			}
			// Preserve the completed frame; styles and tooltip state refresh on the next update.
			bNextFrameDirty = true;
		}
	}

	bool FContext::Dispatch(FEvent& Event, std::string& Error)
	{
		struct FRouteEntry
		{
			std::shared_ptr<FWidget> Widget;
			std::vector<std::shared_ptr<FEventConnection>> Listeners;
		};
		std::vector<FRouteEntry> Route;
		for (auto Node = Event.Target; Node; Node = Node->Parent.lock())
		{
			Route.push_back({Node, Node->Listeners});
		}
		const auto Invoke = [&](FRouteEntry& Entry, bool bCapture, EEventPhase Phase)
		{
			if (!Eligible(Entry.Widget, false, false))
			{
				return;
			}
			Event.CurrentTarget = Entry.Widget;
			Event.Phase = Phase;
			for (const auto& Connection : Entry.Listeners)
			{
				if (Event.bImmediateStopped || !Eligible(Entry.Widget, false, false))
				{
					break;
				}
				if (Connection->bActive && Connection->Type == Event.Input.Type && Connection->bCapture == bCapture)
				{
					auto Callback = Connection->Callback;
					Callback(Event);
				}
			}
		};
		try
		{
			for (std::size_t Index = Route.size(); Index > 1 && !Event.bPropagationStopped; --Index)
			{
				Invoke(Route[Index - 1], true, EEventPhase::Capture);
			}
			if (!Route.empty() && !Event.bPropagationStopped)
			{
				Invoke(Route[0], true, EEventPhase::Target);
				if (!Event.bImmediateStopped)
				{
					Invoke(Route[0], false, EEventPhase::Target);
				}
			}
			for (std::size_t Index = 1; Index < Route.size() && !Event.bPropagationStopped; ++Index)
			{
				Invoke(Route[Index], false, EEventPhase::Bubble);
			}
			return true;
		}
		catch (const std::exception& Exception)
		{
			Error = std::string("gui.event_exception: ") + Exception.what();
		}
		catch (...)
		{
			Error = "gui.event_exception";
		}
		return false;
	}

	bool FContext::SetFocusTarget(std::shared_ptr<FWidget> Value, bool bReveal, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (Value && !Eligible(Value, false, true))
		{
			Error = "gui.invalid_focus_target";
			return false;
		}
		const auto Previous = Focus.lock();
		if (Previous != Value && !CommitNumber(Previous, false, Error))
		{
			return false;
		}
		// Committing a draft can invoke user code that closes the context or removes the target.
		if (!Check(Error))
		{
			return false;
		}
		if (Value && !Eligible(Value, false, true))
		{
			Error = "gui.invalid_focus_target";
			return false;
		}
		Focus = Value;
		ActiveControl.reset();
		ControlKey = Platform::EKey::Unknown;
		return (!bReveal || RevealFocus(Value, Error)) && RefreshInputServices(Error);
	}
} // namespace VeroScript::Gui

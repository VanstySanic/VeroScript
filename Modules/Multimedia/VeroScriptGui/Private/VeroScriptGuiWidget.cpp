// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "Compilation/VeroScriptGuiMarkupInternal.h"
#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <set>
#include <unordered_set>
#include <stdexcept>

namespace VeroScript::Gui
{
	namespace
	{
		std::atomic<std::uint64_t> NextIdentity{1};
		std::atomic<std::uint64_t> NextNameScope{1};
		thread_local unsigned GetterDepth = 0;
		thread_local unsigned LifecycleDepth = 0;
		constexpr std::size_t PropertyCount = static_cast<std::size_t>(EProperty::Count);
	} // namespace

	FWidget::~FWidget() = default;

	std::shared_ptr<FWidget> FWidget::Create()
	{
		return std::shared_ptr<FWidget>(new FWidget);
	}

	std::uint64_t FWidget::AllocateNameScope()
	{
		const auto Value = NextNameScope.fetch_add(1);
		if (!Value || Value > static_cast<std::uint64_t>(INT64_MAX))
		{
			throw std::overflow_error("gui.scope_identity_exhausted");
		}
		return Value;
	}

	bool FWidget::SetDeclarationScope(std::uint64_t Value, std::string& Error)
	{
		if (!Check(Error, true, true))
		{
			return false;
		}
		if (!Parent.expired() || IsMounted())
		{
			Error = "gui.scope_requires_unattached_widget";
			return false;
		}
		NameScope = Value;
		InvalidateNames();
		return true;
	}

	bool FWidget::AdoptDeclarationScope(std::uint64_t Value, std::string& Error)
	{
		if (!Value || !Check(Error, true, true))
		{
			if (Error.empty())
			{
				Error = "gui.invalid_scope";
			}
			return false;
		}
		if (!Parent.expired() || IsMounted())
		{
			Error = "gui.scope_requires_unattached_widget";
			return false;
		}
		std::vector<std::shared_ptr<FWidget>> Pending{shared_from_this()}, Changed;
		while (!Pending.empty())
		{
			auto Node = std::move(Pending.back());
			Pending.pop_back();
			if (Node->NameScope)
			{
				continue;
			}
			if (!Node->Check(Error, true, true))
			{
				return false;
			}
			Changed.push_back(Node);
			for (const auto& Child : Node->Children)
			{
				Pending.push_back(Child);
			}
		}
		for (const auto& Node : Changed)
		{
			Node->NameScope = Value;
			Node->NameIndices.clear();
		}
		InvalidateNames();
		return true;
	}

	std::uint64_t FWidget::GetDeclarationScope() const noexcept
	{
		return NameScope;
	}

	std::shared_ptr<FWidget> FWidget::FindInScope(const std::string& Identifier, std::uint64_t Scope,
	    std::string& Error)
	{
		if (!Check(Error) || Identifier.empty())
		{
			return {};
		}
		auto Cached = NameIndices.find(Scope);
		if (Cached == NameIndices.end())
		{
			std::map<std::string, std::weak_ptr<FWidget>> Names;
			std::vector<std::shared_ptr<FWidget>> Pending{shared_from_this()};
			while (!Pending.empty())
			{
				auto Widget = std::move(Pending.back());
				Pending.pop_back();
				if (Widget->NameScope == Scope && !Widget->Id.empty() && !Names.emplace(Widget->Id, Widget).second)
				{
					Error = "gui.duplicate_scope_id: " + Widget->Id;
					return {};
				}
				for (const auto& Child : Widget->Children)
				{
					Pending.push_back(Child);
				}
			}
			if (NameIndices.size() >= 256)
			{
				NameIndices.clear();
			}
			Cached = NameIndices.emplace(Scope, std::move(Names)).first;
		}
		const auto Found = Cached->second.find(Identifier);
		return Found == Cached->second.end() ? nullptr : Found->second.lock();
	}

	std::uint64_t FWidget::GetIdentity() const noexcept
	{
		return Identity;
	}

	bool FWidget::IsDisposed() const noexcept
	{
		return bDisposed;
	}

	bool FWidget::IsMounted() const noexcept
	{
		return !Context.expired();
	}

	bool FWidget::IsInitialized() const noexcept
	{
		return bInitialized;
	}

	bool FWidget::HasActiveInput() const
	{
		const auto Owner = Context.lock();
		if (!Owner || Thread != std::this_thread::get_id()) return false;
		const auto Within = [this](std::shared_ptr<FWidget> Node)
		{
			for (; Node; Node = Node->Parent.lock())
			{
				if (Node.get() == this) return true;
			}
			return false;
		};
		if (Within(Owner->Focus.lock()) || Within(Owner->ActiveControl.lock())) return true;
		for (const auto& Entry : Owner->Captures)
		{
			if (Within(Entry.second.lock())) return true;
		}
		for (const auto& Entry : Owner->Pressed)
		{
			if (Within(Entry.second.lock())) return true;
		}
		return false;
	}

	std::shared_ptr<FWidget> FWidget::GetParent() const
	{
		return Parent.lock();
	}

	std::int64_t FWidget::GetChildCount() const noexcept
	{
		return static_cast<std::int64_t>(Children.size());
	}

	std::shared_ptr<FWidget> FWidget::GetChild(std::int64_t Index, std::string& Error) const
	{
		if (!Check(Error))
		{
			return {};
		}
		if (Index < 0 || static_cast<std::uint64_t>(Index) >= Children.size())
		{
			Error = "gui.child_index";
			return {};
		}
		return Children[static_cast<std::size_t>(Index)];
	}

	bool FWidget::AddChild(std::shared_ptr<FWidget> Child, std::string& Error)
	{
		if (!Check(Error, true, true))
		{
			return false;
		}
		const auto Self = weak_from_this().lock();
		if (!Self)
		{
			Error = "gui.shared_ownership_required: attach children after the factory establishes shared ownership";
			return false;
		}
		if (!Child || !Child->Check(Error, true, true))
		{
			if (!Child)
			{
				Error = "gui.null_child";
			}
			return false;
		}
		if (!Child->Parent.expired() || Child->IsMounted())
		{
			Error = "gui.child_already_attached";
			return false;
		}
		std::size_t Depth = 0;
		for (auto Ancestor = Self; Ancestor; Ancestor = Ancestor->Parent.lock())
		{
			if (Ancestor == Child || ++Depth > 256)
			{
				Error = "gui.tree_cycle_or_depth";
				return false;
			}
		}
		std::vector<std::pair<std::shared_ptr<FWidget>, std::size_t>> Stack{{Child, Depth + 1}};
		std::size_t Count = 0;
		while (!Stack.empty())
		{
			const auto Item = Stack.back();
			Stack.pop_back();
			if (Item.second > 256 || ++Count > 100000)
			{
				Error = "gui.tree_limit";
				return false;
			}
			for (const auto& Node : Item.first->Children)
			{
				Stack.emplace_back(Node, Item.second + 1);
			}
		}
		Child->Parent = Self;
		Children.push_back(Child);
		InvalidateNames();
		if (const auto Owner = Context.lock())
		{
			std::vector<std::shared_ptr<FWidget>> Nodes;
			if (!Owner->Gather(Nodes, Error))
			{
				Children.pop_back();
				Child->Parent.reset();
				return false;
			}
			Child->Mount(Owner);
		}
		MarkDirty();
		return NotifyChildrenChanged(Error);
	}

	bool FWidget::SetChildren(std::vector<std::shared_ptr<FWidget>> Value, std::string& Error)
	{
		const auto Self = weak_from_this().lock();
		if (!Check(Error, true, true) || !Self)
		{
			if (Error.empty())
			{
				Error = "gui.shared_ownership_required";
			}
			return false;
		}
		if (Value == Children)
		{
			return true;
		}
		for (const auto& Child : Value)
		{
			if (!Child || !Child->Check(Error, true, true))
			{
				if (!Child)
				{
					Error = "gui.null_child";
				}
				return false;
			}
			const auto ExistingParent = Child->Parent.lock();
			if ((ExistingParent && ExistingParent != Self) || (!ExistingParent && Child->IsMounted()))
			{
				Error = "gui.child_already_attached";
				return false;
			}
		}
		auto Root = Self;
		for (std::size_t Depth = 0; const auto Ancestor = Root->Parent.lock(); ++Depth)
		{
			if (Depth >= 256)
			{
				Error = "gui.tree_cycle_or_depth";
				return false;
			}
			Root = Ancestor;
		}
		std::vector<std::pair<std::shared_ptr<FWidget>, std::size_t>> Stack{{Root, 1}};
		std::set<std::uint64_t> Identities;
		std::set<std::pair<std::uint64_t, std::string>> Names;
		while (!Stack.empty())
		{
			const auto Item = std::move(Stack.back());
			Stack.pop_back();
			const auto& Node = Item.first;
			if (!Node->Check(Error, true, true))
			{
				return false;
			}
			if (Item.second > 256 || Identities.size() >= 100000 || !Identities.insert(Node->Identity).second ||
			    (!Node->Id.empty() && !Names.emplace(Node->NameScope, Node->Id).second))
			{
				Error = "gui.invalid_tree: duplicate id, ownership, or tree limit";
				return false;
			}
			for (const auto& Child : Node == Self ? Value : Node->Children)
			{
				Stack.emplace_back(Child, Item.second + 1);
			}
		}
		// Candidate validation finishes before publishing ownership. Lifecycle errors report a committed tree.
		std::unordered_set<const FWidget*> Retained;
		Retained.reserve(Value.size());
		for (const auto& Child : Value)
		{
			Retained.insert(Child.get());
		}
		std::string LifecycleError;
		auto Previous = std::move(Children);
		Children = std::move(Value);
		InvalidateNames();
		const auto Owner = Context.lock();
		for (const auto& Child : Previous)
		{
			if (!Retained.count(Child.get()))
			{
				Child->Parent.reset();
				std::string Failure;
				if (!Child->Unmount(Failure) && LifecycleError.empty())
				{
					LifecycleError = std::move(Failure);
				}
			}
		}
		for (const auto& Child : Children)
		{
			const bool bAdded = Child->Parent.expired();
			Child->Parent = Self;
			if (bAdded && Owner)
			{
				Child->Mount(Owner);
			}
		}
		MarkDirty();
		std::string ChangedError;
		if (!NotifyChildrenChanged(ChangedError) && LifecycleError.empty())
		{
			LifecycleError = std::move(ChangedError);
		}
		Error = std::move(LifecycleError);
		return Error.empty();
	}

	bool FWidget::SetChildrenChangedCallback(FChildrenChangedCallback Callback, std::string& Error)
	{
		if (!Check(Error, true)) return false;
		ChildrenChangedCallback = std::move(Callback);
		bChildrenNotificationPending = false;
		return true;
	}

	bool FWidget::RemoveChild(const std::shared_ptr<FWidget>& Child, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		const auto OwnerContext = Context.lock();
		if (!Check(Error, true, true))
		{
			return false;
		}
		const auto Found = std::find(Children.begin(), Children.end(), Child);
		if (Found == Children.end())
		{
			Error = "gui.not_a_child";
			return false;
		}
		// Keep ownership across erasure, including calls with a reference into Children.
		const auto Removed = *Found;
		Children.erase(Found);
		InvalidateNames();
		Removed->Parent.reset();
		const bool bUnmounted = Removed->Unmount(Error);
		MarkDirty();
		std::string ChangedError;
		const bool bNotified = NotifyChildrenChanged(ChangedError);
		if (Error.empty())
		{
			Error = std::move(ChangedError);
		}
		return bUnmounted && bNotified;
	}

	bool FWidget::Dispose(std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		Error.clear();
		if (Thread != std::this_thread::get_id())
		{
			Error = "gui.wrong_thread";
			return false;
		}
		if (bDisposed || bDisposing)
		{
			return true;
		}
		if (!Check(Error, true, true))
		{
			return false;
		}
		struct FDisposing
		{
			bool& Flag;
			explicit FDisposing(bool& Value) : Flag(Value)
			{
				Flag = true;
			}
			~FDisposing()
			{
				Flag = false;
			}
		} Guard(bDisposing);
		std::string FirstError;
		if (const auto Owner = Parent.lock())
		{
			if (!Owner->RemoveChild(shared_from_this(), Error))
			{
				if (!Parent.expired())
				{
					return false;
				}
				FirstError = std::move(Error);
			}
		}
		else if (const auto MountedContext = Context.lock())
		{
			if (!MountedContext->SetRoot({}, Error))
			{
				if (!Context.expired())
				{
					return false;
				}
				FirstError = std::move(Error);
			}
		}
		auto OwnedChildren = std::move(Children);
		InvalidateNames();
		for (const auto& Child : OwnedChildren)
		{
			Child->Parent.reset();
			std::string Failure;
			if (!Child->Dispose(Failure) && FirstError.empty())
			{
				FirstError = std::move(Failure);
			}
		}
		for (auto& Cell : Properties)
		{
			Cell.Getter = {};
			Cell.Local.reset();
			Cell.Style.reset();
			Cell.CompiledStyle.reset();
			Cell.Animation.reset();
			Cell.Presentation.reset();
		}
		for (const auto& Listener : Listeners)
		{
			Listener->bActive = false;
			Listener->Callback = {};
		}
		Listeners.clear();
		LifecycleObserver = {};
		PaintCallback = {};
		ControlCallback = {};
		ChildrenChangedCallback = {};
		bDisposed = true;
		Markup.reset();
		StyleMemberships.clear();
		StyleSheets.clear();
		StyleProfile.clear();
		StyleValues.clear();
		Error = std::move(FirstError);
		return Error.empty();
	}

	bool FWidget::SetId(std::string Value, std::string& Error)
	{
		if (!Check(Error, true, true))
		{
			return false;
		}
		if (Markup && !Markup->CheckId(*this, Value, Error))
		{
			return false;
		}
		const auto Previous = Id;
		Id = std::move(Value);
		if (const auto Owner = Context.lock())
		{
			std::vector<std::shared_ptr<FWidget>> Nodes;
			if (!Owner->Gather(Nodes, Error))
			{
				Id = Previous;
				return false;
			}
		}
		if (Markup)
		{
			Markup->IdChanged(shared_from_this(), Previous);
		}
		if (Id != Previous)
		{
			InvalidateNames();
			MarkDirty();
		}
		return true;
	}

	const std::string& FWidget::GetId() const noexcept
	{
		return Id;
	}

	bool FWidget::SetClasses(std::vector<std::string> Value, std::string& Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		for (const auto& Class : Value)
		{
			if (!IsMarkupName(Class))
			{
				Error = "gui.invalid_class";
				return false;
			}
		}
		if (Classes != Value)
		{
			Classes = std::move(Value);
			MarkDirty();
		}
		return true;
	}

	const std::vector<std::string>& FWidget::GetClasses() const noexcept
	{
		return Classes;
	}

	bool FWidget::SetClass(std::string Name, bool bEnabled, std::string& Error)
	{
		if (!Check(Error, true)) return false;
		if (!IsMarkupName(Name))
		{
			Error = "gui.invalid_class";
			return false;
		}
		const auto Found = std::find(Classes.begin(), Classes.end(), Name);
		if (bEnabled && Found == Classes.end())
		{
			Classes.push_back(std::move(Name));
			MarkDirty();
		}
		else if (!bEnabled && Found != Classes.end())
		{
			Classes.erase(std::remove(Classes.begin(), Classes.end(), Name), Classes.end());
			MarkDirty();
		}
		return true;
	}

	bool FWidget::SetContentVisible(bool bVisible, std::string& Error)
	{
		if (!Check(Error, true)) return false;
		if (bContentVisible != bVisible)
		{
			bContentVisible = bVisible;
			MarkDirty();
		}
		return true;
	}

	bool FWidget::Set(EProperty Property, FPropertyValue Value, std::string& Error)
	{
		return SetLocal(Property, std::move(Value), false, Error);
	}

	bool FWidget::SetSynchronized(EProperty Property, FPropertyValue Value, std::string& Error)
	{
		return SetLocal(Property, std::move(Value), true, Error);
	}

	bool FWidget::ClearSynchronized(EProperty Property, std::string& Error)
	{
		return ClearLocal(Property, true, Error);
	}

	bool FWidget::Bind(EProperty Property, FPropertyGetter Getter, std::string& Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		if (static_cast<std::size_t>(Property) >= PropertyCount || !Getter)
		{
			Error = "gui.invalid_binding";
			return false;
		}
		auto& Cell = Properties[static_cast<std::size_t>(Property)];
		Cell.Getter = std::move(Getter);
		Cell.Local.reset();
		++Cell.Revision;
		MarkDirty();
		return true;
	}

	bool FWidget::Clear(EProperty Property, std::string& Error)
	{
		return ClearLocal(Property, false, Error);
	}

	bool FWidget::Animate(EProperty Property, FPropertyValue To, const FAnimationOptions& Options, std::string& Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		FPropertyValue From;
		if (!GetPresentation(Property, From, Error))
		{
			return false;
		}
		auto Track = FAnimationTrack::Create(Property, From, std::move(To), Options, Error);
		if (!Track)
		{
			return false;
		}
		auto& Cell = Properties[static_cast<std::size_t>(Property)];
		Cell.Animation = std::move(Track);
		Cell.Presentation = std::move(From);
		bResolvedPropertiesDirty = true;
		MarkDirty();
		return true;
	}

	bool FWidget::StopAnimation(EProperty Property, std::string& Error)
	{
		if (!Check(Error, true) || static_cast<std::size_t>(Property) >= PropertyCount)
		{
			if (Error.empty())
			{
				Error = "gui.invalid_property";
			}
			return false;
		}
		auto& Cell = Properties[static_cast<std::size_t>(Property)];
		Cell.Animation.reset();
		Cell.Presentation.reset();
		bResolvedPropertiesDirty = true;
		MarkDirty();
		return true;
	}

	bool FWidget::GetPresentation(EProperty Property, FPropertyValue& Value, std::string& Error)
	{
		if (!Get(Property, Value, Error))
		{
			return false;
		}
		const auto& Cell = Properties[static_cast<std::size_t>(Property)];
		if (Cell.Presentation)
		{
			Value = *Cell.Presentation;
		}
		return true;
	}

	bool FWidget::SetStyle(EProperty Property, std::optional<FPropertyValue> Value, std::string& Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		if (static_cast<std::size_t>(Property) >= PropertyCount)
		{
			Error = "gui.invalid_property";
			return false;
		}
		if (!GetPropertyDefinition(Property).bStyle)
		{
			Error = "gui.property_not_styleable";
			return false;
		}
		if (Value && !ValidateProperty(Property, *Value, Error))
		{
			return false;
		}
		auto& Cell = Properties[static_cast<std::size_t>(Property)];
		if (Cell.Style.has_value() == Value.has_value() && (!Value || PropertyValuesEqual(*Value, *Cell.Style)))
		{
			return true;
		}
		Cell.Style = std::move(Value);
		++Cell.Revision;
		MarkDirty();
		return true;
	}

	bool FWidget::Get(EProperty Property, FPropertyValue& Value, std::string& Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (static_cast<std::size_t>(Property) >= PropertyCount)
		{
			Error = "gui.invalid_property";
			return false;
		}
		const auto Owner = Context.lock();
		auto& Cell = Properties[static_cast<std::size_t>(Property)];
		if (Cell.Getter && (!Owner || !Owner->bUpdating))
		{
			if (!Cell.bCommitted)
			{
				Error = "gui.binding_uninitialized";
				return false;
			}
			Value = Cell.Value;
			return true;
		}
		const auto CurrentRound = Owner && Owner->bUpdating ? Owner->Round : Cell.Round;
		if (!Evaluate(Property, CurrentRound, Owner && Owner->bUpdating, Error))
		{
			return false;
		}
		Value = Cell.Value;
		return true;
	}

	bool FWidget::GetFallback(EProperty Property, FPropertyValue& Value, std::string& Error) const
	{
		if (!Check(Error))
		{
			return false;
		}
		const auto Index = static_cast<std::size_t>(Property);
		if (Index >= Properties.size())
		{
			Error = "gui.invalid_property";
			return false;
		}
		if (!Properties[Index].CompiledStyle && !Properties[Index].Style && IsInheritedProperty(Property))
		{
			if (const auto Ancestor = Parent.lock())
			{
				return Ancestor->Get(Property, Value, Error);
			}
		}
		Value = Properties[Index].CompiledStyle.value_or(
		    Properties[Index].Style.value_or(GetPropertyDefinition(Property).DefaultValue));
		return true;
	}

	FSubscription FWidget::Subscribe(Platform::EEventType Type, bool bCapture, std::function<void(FEvent&)> Callback,
	    std::string& Error)
	{
		if (!Check(Error, true))
		{
			return {};
		}
		const auto& Definitions = GetEventDefinitions();
		if (!Callback || std::none_of(Definitions.begin(), Definitions.end(), [&](const auto& Definition)
		{
			return Definition.Type == Type;
		}))
		{
			Error = "gui.invalid_listener";
			return {};
		}
		Listeners.erase(
		    std::remove_if(Listeners.begin(), Listeners.end(), [](const auto& Value)
		{
			return !Value->bActive;
		}),
		    Listeners.end());
		auto Connection = std::make_shared<FEventConnection>();
		Connection->Thread = Thread;
		Connection->Type = Type;
		Connection->bCapture = bCapture;
		Connection->Callback = std::move(Callback);
		Listeners.push_back(Connection);
		return FSubscription(Connection);
	}

	bool FWidget::GetLayout(FLayoutBox& Value, std::string& Error) const
	{
		if (!Check(Error))
		{
			return false;
		}
		if (!bHasLayout)
		{
			Error = "gui.layout_unavailable";
			return false;
		}
		Value = Layout;
		return true;
	}

	bool FWidget::SetLifecycleObserver(FLifecycleObserver Observer, std::string& Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		LifecycleObserver = std::move(Observer);
		return true;
	}

	bool FWidget::SetPaintCallback(FPaintCallback Callback, std::string& Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		PaintCallback = std::move(Callback);
		MarkDirty();
		return true;
	}

	FWidget::FWidget() : Thread(std::this_thread::get_id()), Identity(NextIdentity.fetch_add(1))
	{
		for (std::size_t Index = 0; Index < PropertyCount; ++Index)
		{
			Properties[Index].Value = GetPropertyDefinition(static_cast<EProperty>(Index)).DefaultValue;
		}
	}

	void FWidget::OnInitialized()
	{
	}

	void FWidget::OnMounted() noexcept
	{
	}

	void FWidget::OnUnmounted() noexcept
	{
	}

	void FWidget::OnPropertyChanged(EProperty)
	{
	}

	void FWidget::OnPaint(FPaintContext&)
	{
	}

	bool FWidget::Check(std::string& Error, bool bMutation, bool bStructure) const
	{
		Error.clear();
		if (Thread != std::this_thread::get_id())
		{
			Error = "gui.wrong_thread";
		}
		else if (bDisposed)
		{
			Error = "gui.widget_disposed";
		}
		else if (bMutation && GetterDepth)
		{
			Error = "gui.getter_mutation: property getters must be read-only";
		}
		else if (const auto Owner = Context.lock(); bMutation && Owner && Owner->bRendering)
		{
			Error = "gui.mutation_during_render";
		}
		else if (bMutation && Owner && Owner->bRefreshingPointerServices)
		{
			Error = "gui.reentrant_operation: pointer service callback is in progress";
		}
		else if (bMutation && Owner && Owner->bRefreshingTextServices)
		{
			Error = "gui.reentrant_operation: text input service callback is in progress";
		}
		else if (bStructure && (LifecycleDepth || (Owner && Owner->bUpdating)))
		{
			Error = "gui.tree_during_update: defer structural changes with Context.Post";
		}
		return Error.empty();
	}

	bool FWidget::IsEvaluating() noexcept
	{
		return GetterDepth != 0;
	}

	bool FWidget::IsInLifecycle() noexcept
	{
		return LifecycleDepth != 0;
	}

	bool FWidget::Evaluate(EProperty Property, std::uint64_t InRound, bool bTrackRead, std::string& Error)
	{
		if (bTrackRead)
		{
			// Evaluation may read an ancestor without going through Get. Record
			// cached evaluations too, so every inherited dependency is retained.
			if (PropertyReadRound != InRound)
			{
				PropertyReadRound = InRound;
				ReadProperties.reset();
			}
			ReadProperties.set(static_cast<std::size_t>(Property));
		}
		auto& Cell = Properties[static_cast<std::size_t>(Property)];
		if (Cell.Evaluation == 1)
		{
			Error = "gui.binding_cycle";
			return false;
		}
		if (Cell.bCommitted && Cell.EvaluatedRevision == Cell.Revision &&
		    ((!Cell.Getter && (!IsInheritedProperty(Property) || Cell.Local || Cell.CompiledStyle || Cell.Style)) ||
		        Cell.Round == InRound))
		{
			return true;
		}
		Cell.Evaluation = 1;
		std::optional<FPropertyValue> Candidate = Cell.Local;
		bool bSuccess = true;
		if (Cell.Getter)
		{
			++GetterDepth;
			try
			{
				bSuccess = Cell.Getter(Candidate, Error);
			}
			catch (const std::exception& Exception)
			{
				Error = std::string("gui.getter_exception: ") + Exception.what();
				bSuccess = false;
			}
			catch (...)
			{
				Error = "gui.getter_exception";
				bSuccess = false;
			}
			--GetterDepth;
		}
		Cell.Evaluation = 0;
		if (!bSuccess)
		{
			if (Error.empty())
			{
				Error = "gui.binding_failed";
			}
			return false;
		}
		if (!Candidate)
		{
			if (!Cell.CompiledStyle && !Cell.Style && IsInheritedProperty(Property))
			{
				if (const auto Ancestor = Parent.lock())
				{
					if (!Ancestor->Evaluate(Property, InRound, bTrackRead, Error))
					{
						return false;
					}
					const auto& Inherited = Ancestor->Properties[static_cast<std::size_t>(Property)].Value;
					// The parent is evaluated first, including its dynamic getter. An
					// unchanged, previously validated value needs no copy or validation.
					if (Cell.bCommitted && Cell.EvaluatedRevision == Cell.Revision &&
					    PropertyValuesEqual(Cell.Value, Inherited))
					{
						Cell.Round = InRound;
						return true;
					}
					Candidate = Inherited;
				}
			}
			if (!Candidate)
			{
				Candidate = Cell.CompiledStyle ? *Cell.CompiledStyle
				    : Cell.Style               ? *Cell.Style
				                               : GetPropertyDefinition(Property).DefaultValue;
			}
		}
		if (!ValidateProperty(Property, *Candidate, Error))
		{
			return false;
		}
		const bool bChanged = !Cell.bCommitted || !PropertyValuesEqual(Cell.Value, *Candidate);
		Cell.bNotify = Cell.bNotify || bChanged;
		bResolvedPropertiesDirty = bResolvedPropertiesDirty || bChanged;
		Cell.Value = std::move(*Candidate);
		Cell.bCommitted = true;
		Cell.EvaluatedRevision = Cell.Revision;
		Cell.Round = InRound;
		return true;
	}

	bool FWidget::SetLocal(EProperty Property, FPropertyValue Value, bool bSynchronized, std::string& Error)
	{
		if (!Check(Error, true) || !ValidateProperty(Property, Value, Error))
		{
			return false;
		}
		auto& Cell = Properties[static_cast<std::size_t>(Property)];
		if (!Cell.Getter && Cell.Local && PropertyValuesEqual(*Cell.Local, Value))
		{
			return true;
		}
		Cell.Local = std::move(Value);
		Cell.Getter = {};
		++Cell.Revision;
		MarkDirty(bSynchronized && CanCommitSynchronized(Property));
		return true;
	}

	bool FWidget::ClearLocal(EProperty Property, bool bSynchronized, std::string& Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		if (static_cast<std::size_t>(Property) >= PropertyCount)
		{
			Error = "gui.invalid_property";
			return false;
		}
		auto& Cell = Properties[static_cast<std::size_t>(Property)];
		if (!Cell.Getter && !Cell.Local)
		{
			return true;
		}
		Cell.Local.reset();
		Cell.Getter = {};
		++Cell.Revision;
		MarkDirty(bSynchronized && CanCommitSynchronized(Property));
		return true;
	}

	bool FWidget::CanCommitSynchronized(EProperty Property) const
	{
		// Input eligibility also participates in modal/hover routing through
		// completed snapshots; keep its existing stabilization path conservative.
		if (Property == EProperty::Enabled || Property == EProperty::Visibility ||
		    Property == EProperty::HitTest || Property == EProperty::Focusable)
		{
			return false;
		}
		const auto Owner = Context.lock();
		return Owner && Owner->bSynchronizingScripts &&
		    (PropertyReadRound != Owner->Round || !ReadProperties.test(static_cast<std::size_t>(Property)));
	}

	void FWidget::MarkDirty(bool bPendingEvaluation)
	{
		if (MutationToken)
		{
			++MutationToken;
		}
		if (const auto Owner = Context.lock())
		{
			if (Owner->bNotifyingLayout)
			{
				Owner->bNextFrameDirty = true;
			}
			else if (!bPendingEvaluation)
			{
				Owner->bDirty = true;
				Owner->bReady = false;
			}
		}
	}

	void FWidget::InvalidateNames()
	{
		NameIndices.clear();
		for (auto Ancestor = Parent.lock(); Ancestor; Ancestor = Ancestor->Parent.lock())
		{
			Ancestor->NameIndices.clear();
		}
	}

	void FWidget::Mount(const std::shared_ptr<FContext>& Value)
	{
		Context = Value;
		++MountGeneration;
		if (bChildrenNotificationPending)
		{
			QueueChildrenNotification();
		}
		if (Control.bModal && Control.bOpen)
		{
			auto& State = Value->FloatingStates[Identity];
			State.Widget = shared_from_this();
			State.bOpen = State.bModal = true;
			Value->RememberFloatingFocus(State);
			if (Value->NextModalOrder != UINT64_MAX)
			{
				State.ModalOrder = ++Value->NextModalOrder;
			}
			Value->bHasModalControls = true;
		}
		bHasLayout = false;
		PropertyReadRound = 0;
		ReadProperties.reset();
		for (auto& Cell : Properties)
		{
			Cell.EvaluatedRevision = 0;
			Cell.Round = 0;
			if (Cell.Animation)
			{
				Cell.Animation->Pause();
			}
		}
		for (const auto& Child : Children)
		{
			Child->Mount(Value);
		}
	}

	bool FWidget::Unmount(std::string& Error)
	{
		std::string FirstError;
		for (const auto& Child : Children)
		{
			std::string Failure;
			if (!Child->Unmount(Failure) && FirstError.empty())
			{
				FirstError = std::move(Failure);
			}
		}
		if (bMountNotified)
		{
			bMountNotified = false;
			struct FDepth
			{
				FDepth()
				{
					++LifecycleDepth;
				}
				~FDepth()
				{
					--LifecycleDepth;
				}
			} Guard;
			std::string Failure;
			if (!NotifyLifecycle(EWidgetLifecycle::Unmounted, Failure) && FirstError.empty())
			{
				FirstError = std::move(Failure);
			}
			OnUnmounted();
		}
		Context.reset();
		bHasLayout = false;
		Error = std::move(FirstError);
		return Error.empty();
	}

	bool FWidget::NotifyLifecycle(EWidgetLifecycle Stage, std::string& Error)
	{
		const auto Owner = Context.lock();
		if (!LifecycleObserver || !Owner || Owner->bFinalizing)
		{
			return true;
		}
		try
		{
			// An observer may clear or replace itself while handling a lifecycle stage.
			auto Observer = LifecycleObserver;
			if (Observer(Stage, Error))
			{
				return true;
			}
			if (Error.empty())
			{
				Error = "gui.lifecycle_callback_failed";
			}
		}
		catch (const std::exception& Exception)
		{
			Error = std::string("gui.lifecycle_exception: ") + Exception.what();
		}
		catch (...)
		{
			Error = "gui.lifecycle_exception";
		}
		return false;
	}

	bool FWidget::NotifyChildrenChanged(std::string& Error, bool bRetry)
	{
		if (!ChildrenChangedCallback) return true;
		try
		{
			auto Callback = ChildrenChangedCallback;
			if (Callback(bRetry, Error))
			{
				bChildrenNotificationPending = false;
				return true;
			}
			if (Error.empty())
			{
				Error = "gui.children_callback_failed";
			}
		}
		catch (const std::exception& Exception)
		{
			Error = std::string("gui.callback_exception: ") + Exception.what();
		}
		catch (...)
		{
			Error = "gui.callback_exception";
		}
		bChildrenNotificationPending = true;
		QueueChildrenNotification();
		return false;
	}

	void FWidget::QueueChildrenNotification()
	{
		const auto Owner = Context.lock();
		if (!Owner || (bChildrenNotificationQueued && ChildrenNotificationContext.lock() == Owner)) return;
		bChildrenNotificationQueued = true;
		ChildrenNotificationContext = Owner;
		std::string Failure;
		if (!Owner->Post([Weak = weak_from_this(), QueuedOwner = std::weak_ptr<FContext>(Owner)](auto& Error)
		{
			const auto Widget = Weak.lock();
			if (!Widget || Widget->bDisposed || Widget->Context.lock() != QueuedOwner.lock() ||
			    Widget->ChildrenNotificationContext.lock() != QueuedOwner.lock())
			{
				return true;
			}
			Widget->bChildrenNotificationQueued = false;
			return !Widget->bChildrenNotificationPending || Widget->NotifyChildrenChanged(Error, true);
		}, Failure))
		{
			bChildrenNotificationQueued = false;
		}
	}
} // namespace VeroScript::Gui

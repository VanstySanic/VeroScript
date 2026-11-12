// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiBackend.h"
#include "VeroScriptGuiEvent.h"
#include "VeroScriptGuiStyle.h"
#include "VeroScriptGuiAnimation.h"
#include <map>
#include <bitset>

namespace VeroScript::Gui
{
	class FContext;
	struct FMarkupRuntime;
	class FStyleSheet;
	struct FStyleSheetData;
	enum class EWidgetLifecycle : std::uint8_t
	{
		Initialized,
		Mounted,
		Unmounted,
		LayoutChanged,
		Synchronize
	};

	/** Retained base element. Parents own children; parent/context references are weak. */
	class FWidget : public std::enable_shared_from_this<FWidget>
	{
		friend class FContext;
		friend class FMarkupProgram;
		friend struct FMarkupRuntime;

	public:
		virtual ~FWidget();
		FWidget(const FWidget&) = delete;
		FWidget& operator=(const FWidget&) = delete;
		FWidget(FWidget&&) = delete;
		FWidget& operator=(FWidget&&) = delete;
		static std::shared_ptr<FWidget> Create();
		static std::uint64_t AllocateNameScope();
		bool SetDeclarationScope(std::uint64_t Value, std::string& Error);
		/** Assigns only unowned nodes, preserving nested component name scopes. */
		bool AdoptDeclarationScope(std::uint64_t Value, std::string& Error);
		std::uint64_t GetDeclarationScope() const noexcept;
		std::shared_ptr<FWidget> FindInScope(const std::string& Id, std::uint64_t Scope, std::string& Error);
		std::uint64_t GetIdentity() const noexcept;
		std::uint64_t GetMountGeneration() const noexcept
		{
			return MountGeneration;
		}
		bool IsDisposed() const noexcept;
		bool IsMounted() const noexcept;
		bool IsInitialized() const noexcept;
		bool HasLayout() const noexcept
		{
			return bHasLayout;
		}
		std::shared_ptr<FContext> GetContext() const
		{
			return Context.lock();
		}
		/** Retain a realized container while it owns focus, a press or pointer capture. */
		bool HasActiveInput() const;
		std::shared_ptr<FWidget> GetParent() const;
		std::int64_t GetChildCount() const noexcept;
		std::shared_ptr<FWidget> GetChild(std::int64_t Index, std::string& Error) const;
		bool AddChild(std::shared_ptr<FWidget> Child, std::string& Error);
		bool SetChildren(std::vector<std::shared_ptr<FWidget>> Value, std::string& Error);
		using FChildrenChangedCallback = std::function<bool(bool, std::string&)>;
		/** Runs after ownership commits. Failed callbacks retry as deferred work (first argument true). */
		bool SetChildrenChangedCallback(FChildrenChangedCallback Callback, std::string& Error);
		bool RemoveChild(const std::shared_ptr<FWidget>& Child, std::string& Error);
		bool Dispose(std::string& Error);
		bool SetId(std::string Value, std::string& Error);
		const std::string& GetId() const noexcept;
		bool SetClasses(std::vector<std::string> Value, std::string& Error);
		const std::vector<std::string>& GetClasses() const noexcept;
		bool SetClass(std::string Name, bool bEnabled, std::string& Error);
		/** Containers suppress pages without replacing their visibility attribute or style. */
		bool SetContentVisible(bool bVisible, std::string& Error);
		bool IsContentVisible() const noexcept
		{
			return bContentVisible;
		}
		bool Set(EProperty Property, FPropertyValue Value, std::string& Error);
		/** Compiler output, applied before native evaluation. Observed dependencies still request stabilization. */
		bool SetSynchronized(EProperty Property, FPropertyValue Value, std::string& Error);
		bool ClearSynchronized(EProperty Property, std::string& Error);
		bool Bind(EProperty Property, FPropertyGetter Getter, std::string& Error);
		bool Clear(EProperty Property, std::string& Error);
		bool Animate(EProperty Property, FPropertyValue To, const FAnimationOptions& Options, std::string& Error);
		bool StopAnimation(EProperty Property, std::string& Error);
		bool GetPresentation(EProperty Property, FPropertyValue& Value, std::string& Error);
		bool SetStyle(EProperty Property, std::optional<FPropertyValue> Value, std::string& Error);
		bool SetStyleType(std::uint64_t Scope, std::string Type, std::string Mark, std::string& Error);
		bool AttachStyleSheet(std::uint64_t Scope, std::shared_ptr<const FStyleSheet> Sheet, std::string& Error);
		bool RegisterStyleProperty(FStyleProperty Property, std::string& Error);
		bool GetStyleValue(const std::string& Name, std::optional<FStyleValue>& Value, std::string& Error) const;
		bool Invalidate(std::string& Error);
		std::uint64_t GetSynchronizationToken() const noexcept;
		/** Local native mutations only; bindings, inheritance and animations still require evaluation.
		 * Zero disables reuse after counter exhaustion. */
		std::uint64_t GetMutationToken() const noexcept;
		std::uint64_t GetStyleMutationToken() const noexcept;
		bool Get(EProperty Property, FPropertyValue& Value, std::string& Error);
		bool GetFallback(EProperty Property, FPropertyValue& Value, std::string& Error) const;
		FSubscription Subscribe(Platform::EEventType Type, bool bCapture, std::function<void(FEvent&)> Callback,
		    std::string& Error);
		bool GetLayout(FLayoutBox& Value, std::string& Error) const;
		using FLifecycleObserver = std::function<bool(EWidgetLifecycle, std::string&)>;
		bool SetLifecycleObserver(FLifecycleObserver Observer, std::string& Error);
		bool SetPaintCallback(FPaintCallback Callback, std::string& Error);
		bool SetControlOptions(FControlOptions Value, std::string& Error);
		bool SetFloatingOpen(bool bOpen, std::string& Error);
		const FControlOptions& GetControlOptions() const noexcept;
		bool SetControlCallback(FControlCallback Callback, std::string& Error);
		bool Activate(std::string& Error);
		bool FocusControl(std::string& Error);
		bool SetTextSelection(std::int64_t Start, std::int64_t End, std::string& Error);

	protected:
		FWidget();
		virtual void OnInitialized();
		virtual void OnMounted() noexcept;
		virtual void OnUnmounted() noexcept;
		virtual void OnPropertyChanged(EProperty Property);
		virtual void OnPaint(FPaintContext& Context);

	private:
		struct FPropertyCell
		{
			std::optional<FPropertyValue> Local, Style, CompiledStyle;
			FPropertyGetter Getter;
			std::optional<FAnimationTrack> Animation;
			std::optional<FPropertyValue> Presentation;
			FPropertyValue Value = false;
			std::uint64_t Revision = 1, EvaluatedRevision = 0, Round = 0;
			std::uint8_t Evaluation = 0;
			bool bCommitted = false, bNotify = false;
		};
		struct FStyleMembership
		{
			std::uint64_t Scope;
			std::string Type, Mark;
		};


		bool Check(std::string& Error, bool bMutation = false, bool bStructure = false) const;
		static bool IsEvaluating() noexcept;
		static bool IsInLifecycle() noexcept;
		bool Evaluate(EProperty Property, std::uint64_t Round, bool bTrackRead, std::string& Error);
		bool SetCompiledStyle(EProperty Property, std::optional<FPropertyValue> Value, std::string& Error);
		bool SetLocal(EProperty Property, FPropertyValue Value, bool bSynchronized, std::string& Error);
		bool ClearLocal(EProperty Property, bool bSynchronized, std::string& Error);
		bool CanCommitSynchronized(EProperty Property) const;
		void MarkDirty(bool bPendingEvaluation = false);
		void InvalidateNames();
		void Mount(const std::shared_ptr<FContext>& Value);
		bool Unmount(std::string& Error);
		bool NotifyLifecycle(EWidgetLifecycle Stage, std::string& Error);
		bool NotifyChildrenChanged(std::string& Error, bool bRetry = false);
		void QueueChildrenNotification();

		std::thread::id Thread;
		std::uint64_t Identity;
		std::uint64_t MountGeneration = 0;
		std::weak_ptr<FWidget> Parent;
		std::weak_ptr<FContext> Context;
		std::vector<std::shared_ptr<FWidget>> Children;
		std::vector<std::shared_ptr<FEventConnection>> Listeners;
		std::array<FPropertyCell, static_cast<std::size_t>(EProperty::Count)> Properties;
		// Cache only resolved output. Getters, inheritance and change hooks still
		// run before this is consumed; presentation sampling invalidates it too.
		FResolvedProperties ResolvedProperties;
		bool bResolvedPropertiesDirty = true;
		std::uint64_t PropertyReadRound = 0;
		std::bitset<static_cast<std::size_t>(EProperty::Count)> ReadProperties;
		std::string Id;
		std::vector<std::string> Classes;
		std::shared_ptr<FMarkupRuntime> Markup;
		std::vector<FStyleMembership> StyleMemberships;
		std::map<std::uint64_t, std::vector<std::shared_ptr<const FStyleSheet>>> StyleSheets;
		std::map<std::string, FStyleProperty> StyleProfile;
		std::map<std::string, FStyleValue> StyleValues;
		std::uint64_t SynchronizationToken = 0;
		std::uint64_t MutationToken = 1, StyleMutationToken = 1;
		std::uint64_t NameScope = 0;
		FLayoutBox Layout;
		FLifecycleObserver LifecycleObserver;
		FPaintCallback PaintCallback;
		FControlOptions Control;
		FControlCallback ControlCallback;
		FChildrenChangedCallback ChildrenChangedCallback;
		std::map<std::uint64_t, std::map<std::string, std::weak_ptr<FWidget>>> NameIndices;
		bool bDisposing = false, bDisposed = false, bInitialized = false, bHasLayout = false, bMountNotified = false;
		bool bContentVisible = true;
		bool bLayoutNotificationPending = false;
		bool bChildrenNotificationPending = false, bChildrenNotificationQueued = false;
		std::weak_ptr<FContext> ChildrenNotificationContext;
	};
} // namespace VeroScript::Gui

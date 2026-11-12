// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiScript.h"
#include "VeroScriptValueRoot.h"
#include <array>
#include <map>
#include <stdexcept>

namespace VeroScript::Gui::ScriptBindings
{
	constexpr const char* TypeNames[]{
		"VS.Gui.__NativeWidget",	"VS.Gui.Length",		"VS.Gui.Insets",		  "VS.Gui.Color",
		"VS.Gui.__NativeElements",	"VS.Gui.__NativeEvent", "VS.Gui.__NativeContext", "VS.Gui.__NativeApplication",
		"VS.Gui.__NativeResources", "VS.Gui.PaintContext", "VS.Gui.__NativeSignal", "VS.Gui.__NativeWatch"};
	using FCall = std::function<bool(const FNativeCallContext&, FValue&, std::string&)>;
	struct FScriptEvent
	{
		Platform::FEvent Input;
		std::weak_ptr<FWidget> Target, Sender;
		FEvent* Active = nullptr;
		bool bDefaultPrevented = false;
		EEventPhase Phase = EEventPhase::Target;
	};
	struct FScriptLifecycle;
	struct FBindingSignal;
	struct FBridge
	{
		std::array<FHostTypeId, std::size(TypeNames)> Types{};
		std::array<FHostTypeId, 4> Enums{};
		std::weak_ptr<FWidget> Root;
		bool bRootPending = false;
		std::function<std::shared_ptr<FWidget>(const std::string&)> Find;
		FScriptServices Services;
		std::thread::id Thread = std::this_thread::get_id();
		std::map<std::int64_t, FSubscription> Subscriptions;
		std::int64_t NextSubscription = 1;
		std::int64_t SubscriptionSweep = 0;
		// Borrowed only inside a synchronous native callback.
		IScriptCallInvoker* Invoker = nullptr;
		std::map<std::uint64_t, std::weak_ptr<FScriptLifecycle>> Lifecycles;
		std::uint64_t LifecycleSweep = 0;
		// Sources are weak; widgets own subscriptions and retained attribute readers.
		std::vector<std::weak_ptr<FBindingSignal>> BindingSources;
		std::int64_t BindingRevision = 1;
		std::int64_t NextBindingWatch = 1;
		unsigned BindingBatchDepth = 0;
	};
	struct FScriptLifecycle
	{
		struct FFieldVersion
		{
			std::size_t Index = 0;
			const FScriptObject* Identity = nullptr;
			std::uint64_t Version = 0;
		};

		bool SourcesChanged(const FWidget& Widget, bool& bScriptChanged) const;
		void CaptureFields(std::vector<FFieldVersion>& Output) const;
		void CaptureSources(const FWidget& Widget, bool bFieldsCaptured = false);

		FValueRoot Callback;
		FValueRoot ControlAction;
		FValueRoot ChildrenChanged;
		std::weak_ptr<FBridge> Owner;
		std::weak_ptr<const void> Program;
		std::uint64_t Generation = 0;
		std::uint64_t CallbackRevision = 0;
		bool bPolling = true;
		std::weak_ptr<FScriptObject> ScriptOwner;
		bool bObserveSources = false, bSourceSnapshot = false;
		bool bProvideSourceState = false, bAllowStableSources = false;
		std::uint64_t ScriptVersion = 0, NativeVersion = 0, StyleVersion = 0;
		std::size_t FieldCount = 0;
		std::vector<FFieldVersion> Fields;
	};
	struct FWidgetView
	{
		std::shared_ptr<FWidget> lock() const
		{
			return Weak.lock();
		}

		std::weak_ptr<FWidget> Weak;
		std::shared_ptr<FWidget> Owner;
		// Shared with the observer and replacement script views.
		std::shared_ptr<FScriptLifecycle> Lifecycle{};
	};
	template <class T> struct FDelete
	{
		void operator()(FHostObject* Object) const noexcept
		{
			delete static_cast<T*>(Object->Payload);
			delete Object;
		}
	};

	template <class T> FValue Make(const std::shared_ptr<FBridge>& Bridge, std::size_t Type, T Value)
	{
		auto Payload = std::make_unique<T>(std::move(Value));
		auto Object = std::make_unique<FHostObject>();
		Object->TypeId = Bridge->Types[Type].Value;
		Object->TypeName = TypeNames[Type];
		Object->ModuleOwner = Bridge;
		Object->Payload = Payload.release();
		return FHostObjectHandle(Object.release(), FDelete<T>{});
	}

	inline FValue Make(const std::shared_ptr<FBridge>& Bridge, std::size_t Type, std::weak_ptr<FWidget> Value)
	{
		return Make(Bridge, Type, FWidgetView{std::move(Value), {}});
	}

	template <class T> const T& Payload(const FValue& Value)
	{
		const auto* Handle = std::get_if<FHostObjectHandle>(&Value);
		if (!Handle || !*Handle || !std::get_deleter<FDelete<T>>(*Handle))
		{
			throw std::invalid_argument("gui.invalid_script_value");
		}
		return *static_cast<const T*>((*Handle)->Payload);
	}

	bool Invoke(const FNativeCallContext& Context, FValue& Result, std::string& Error);
	bool Member(FHostBindingRegistry& Registry, FHostTypeId Type, const std::string& Name, EHostMemberKind Kind,
				FNativeTypeDesc Return, std::vector<FNativeTypeDesc> Parameters, FCall Call, std::string& Error,
				bool bReadOnly = false);
	bool RegisterRuntimeBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
								 std::string& Error);
	bool RegisterControlBindings(FHostBindingRegistry &Registry, const std::shared_ptr<FBridge> &Bridge,
								 std::string &Error);
	bool RegisterWidgetEvents(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
							  std::string& Error);
	bool ObserveScriptWidget(const std::shared_ptr<FBridge>& Bridge, const std::shared_ptr<FWidget>& Widget,
							 std::string& Error);
	bool AttachBindingUpdates(const std::shared_ptr<FBridge>& Bridge, const std::shared_ptr<FContext>& Context,
							  std::string& Error);
	bool RegisterNotificationBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
									  std::string& Error);
	bool PublishBindingChanges(FBridge& Bridge, std::string& Error);
	bool CheckScriptProgram(const FBridge& Bridge, const std::weak_ptr<const void>& Owner, std::uint64_t Generation,
							std::string& Error);

	class FInvokerScope final
	{
	public:
		FInvokerScope(FBridge& InBridge, IScriptCallInvoker* Invoker) : Bridge(InBridge), Previous(InBridge.Invoker)
		{
			if (Bridge.Thread != std::this_thread::get_id())
			{
				throw std::runtime_error("gui.wrong_thread");
			}
			Bridge.Invoker = Invoker;
		}
		~FInvokerScope()
		{
			Bridge.Invoker = Previous;
		}
		FInvokerScope(const FInvokerScope&) = delete;
		FInvokerScope& operator=(const FInvokerScope&) = delete;

	private:
		FBridge& Bridge;
		IScriptCallInvoker* Previous;
	};
} // namespace VeroScript::Gui::ScriptBindings

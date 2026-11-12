// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptGuiScript.h"
#include "VeroScriptGuiContext.h"
#include "VeroScriptSession.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptPackage.h"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	using namespace VeroScript::Gui;

	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	class FBackend final : public IBackendContext
	{
	public:
		//~ Begin IBackendContext Interface
		FCapabilities GetCapabilities() const noexcept override
		{
			FCapabilities Result;
			Result.bLayout = true;
			return Result;
		}
		bool Synchronize(const FViewport&, const std::vector<FBackendNode>& Value, std::string&) override
		{
			Nodes = Value;
			return true;
		}
		bool Update(double, std::vector<FLayoutBox>& Boxes, std::string&) override
		{
			for (const auto& Node : Nodes)
			{
				Boxes.push_back({Node.Id, {0, 0, 100, 100}, {0, 0, 100, 100}});
			}
			return true;
		}
		bool Render(bool& bPresented, std::string&) override
		{
			bPresented = false;
			return true;
		}
		bool Close(std::string&) override
		{
			return true;
		}
		//~ End IBackendContext Interface

	private:
		std::vector<FBackendNode> Nodes;
	};

	class FSnapshotService final : public IFrameService
	{
	public:
		explicit FSnapshotService(std::shared_ptr<double> Value) : Snapshot(std::move(Value))
		{
		}
		bool BeginFrame(double Time, std::string&) override
		{
			*Snapshot = 0.125 + Time / 32;
			return true;
		}

	private:
		std::shared_ptr<double> Snapshot;
	};

	void CheckNotifications(const std::string& BackendPath)
	{
		struct FHost final : IEngineHost
		{
			void OnPrintLine(std::string_view Text) override
			{
				std::cout << Text << '\n';
			}
			void OnDiagnosticLine(std::string_view Text) override
			{
				std::cerr << Text << '\n';
			}
		} Host;
		auto FrameSnapshot = std::make_shared<double>(0);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(BackendPath.empty());
		FScriptServices Services;
		std::shared_ptr<FContext> Native;
		Services.CreateContext = [&](const auto&, const auto& Viewport, auto& Error)
		{
			Native = FContext::Create(std::make_unique<FBackend>(), Viewport, Error);
			if (!Native || !Native->SetFrameService("zz.test-snapshot", std::make_shared<FSnapshotService>(FrameSnapshot), Error))
			{
				return std::shared_ptr<FContext>{};
			}
			return Native;
		};
		Services.IsCurrentProgram = [&](const auto& Owner, auto Generation)
		{
			const auto Current = Session.GetDebugProgram();
			return Current.Generation == Generation && Current.Owner.lock() == Owner.lock();
		};
		std::string DisposeBeforeControlDispatch;
		bool bCountControlDispatches = false;
		unsigned ControlDispatches = 0, ControlDisposals = 0;
		Services.Invoke = [&](auto Function, auto& Arguments, auto& Out, auto& Error)
		{
			if (bCountControlDispatches && (Function == "VS.Gui.Internal.DispatchControlCallback" || Function == "VS.Gui.Internal.DispatchControl"))
			{
				++ControlDispatches;
				if (Function == DisposeBeforeControlDispatch)
				{
					// Host bridges and entry safe points may dispose a live target
					// after the native dispatcher checked it but before script runs.
					DisposeBeforeControlDispatch.clear();
					++ControlDisposals;
					const auto Root = Native ? Native->GetRoot() : nullptr;
					if (!Root || !Root->Dispose(Error))
					{
						if (Error.empty())
						{
							Error = "control bridge lost its disposal target";
						}
						return false;
					}
				}
			}
			const auto Result = Session.CallFunction(Function, Arguments, &Out);
			Error = Result.Error.Message;
			return Result.bSuccess;
		};
		std::string Error;
		Require(Session.GetBindingRegistry().RegisterNative("ReadFrameValue", ENativeType::Double, {},
		            [](const FNativeCallContext& Call, FValue& Out, std::string&)
		{
			Out = *static_cast<const double*>(Call.UserData);
			return true;
		}, FrameSnapshot.get(), Error),
		    Error);
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		Config.ExecutionBackend = BackendPath.empty() ? EExecutionBackend::Vm : EExecutionBackend::Jit;
		if (!BackendPath.empty())
		{
			Config.JitBackendFactory = LoadJitBackend(BackendPath, Error);
			Require(Config.JitBackendFactory != nullptr, Error);
		}
		Require(RegisterScriptBindings(Session.GetBindingRegistry(), std::move(Services), Error) &&
		        ConfigureGuiScriptEngine(Config, Error),
		    Error);
		Session.ApplyConfig(Config);
		std::string Source = R"vs(
VS::Gui::ChangeSignal interruptedSignal = nullptr;
VS::Gui::Binding<double> interruptedBinding = nullptr;
double interruptedValue = 0.2;
void PrepareInterruptedNotification()
{
    interruptedSignal = VS::Gui::ChangeSignal();
    interruptedValue = 0.2;
    array<VS::Gui::ChangeSignal> sources = {interruptedSignal};
    interruptedBinding = VS::Gui::Binding<double>([]()->double { return interruptedValue; }, sources);
    interruptedBinding.VsguiReadValue();
}
void PublishInterruptedNotification()
{
    interruptedValue = 0.8;
    interruptedSignal.Notify();
}
int64 ReadInterruptedRevision() { return interruptedSignal.GetRevision(); }
double ReadInterruptedBinding() { return interruptedBinding.VsguiReadValue(); }
VS::Gui::Observable<double> interruptedObservable = nullptr;
VS::Gui::Binding<double> interruptedObservableBinding = nullptr;
void PrepareInterruptedObservable()
{
    interruptedObservable = VS::Gui::Observable<double>(0.2);
    interruptedObservableBinding = interruptedObservable.AsBinding();
    assert(interruptedObservableBinding.VsguiReadValue() == 0.2);
}
void PublishInterruptedObservable() { interruptedObservable.Set(0.8); }
double ReadInterruptedObservable() { return interruptedObservable.Get(); }
double ReadInterruptedObservableBinding() { return interruptedObservableBinding.VsguiReadValue(); }
void CheckObservableSetSemantics()
{
    VS::Gui::Observable<double> value = VS::Gui::Observable<double>(0.2);
    VS::Gui::Binding<double> shared = value.AsBinding();
    int64 revision = value.GetRevision();
    assert(shared.VsguiReadValue() == 0.2);
    value.Set(0.2);
    assert(value.GetRevision() == revision && shared.VsguiReadValue() == 0.2,
        "unchanged observable assignment published a notification");
    value.Set(0.8);
    assert(value.GetRevision() > revision && shared.VsguiReadValue() == 0.8,
        "observable assignment did not invalidate its shared binding");
    revision = value.GetRevision();
    value.Set(0.8);
    assert(value.GetRevision() == revision && shared.VsguiReadValue() == 0.8,
        "observable retry republished an unchanged value");
}
VS::Gui::Subscription retrySubscription = nullptr;
int disconnectAttempts = 0;
void CheckSubscriptionRetry()
{
    retrySubscription = VS::Gui::Subscription();
    retrySubscription.VsguiSetDisconnect([]()->void
    {
        disconnectAttempts++;
        retrySubscription.Disconnect();
        if (disconnectAttempts == 1) { throw "disconnect failed"; }
    });
    bool failed = false;
    try { retrySubscription.Disconnect(); }
    catch (...) { failed = true; }
    assert(failed && retrySubscription.IsConnected() && disconnectAttempts == 1,
        "failed disconnection could not retry or reentered its callback");
    retrySubscription.Disconnect();
    assert(!retrySubscription.IsConnected() && disconnectAttempts == 2,
        "retry did not finish disconnection exactly once");
}
void CheckNestedBatchFailure()
{
    VS::Gui::BindingUpdates::Batch([]()->void
    {
        bool failed = false;
        try
        {
            VS::Gui::BindingUpdates::Batch([]()->void { throw "nested batch failed"; });
        }
        catch (...) { failed = true; }
        bool boundaryBlocked = false;
        try { VS::Gui::BindingUpdates::VsguiRequireBoundary(); }
        catch (...) { boundaryBlocked = true; }
        assert(failed && boundaryBlocked &&
            VS::Gui::__NativeSignal::GetBatchDepth() == 1,
            "failed nested batch released its outer scope");
    });
    VS::Gui::BindingUpdates::VsguiRequireBoundary();
    assert(VS::Gui::__NativeSignal::GetBatchDepth() == 0,
        "nested batch failure leaked a script or native scope");
}
class TemplateRetry : public VS::Gui::Widget
{
public:
    int Attempts;
    void VsguiInstallTemplate() override
    {
        Attempts++;
        if (Attempts == 1) { throw "template failed"; }
        SetText("ready");
    }
};
void CheckTemplateRetry()
{
    TemplateRetry widget = TemplateRetry();
    bool failed = false;
    try { widget.Initialize(); }
    catch (...) { failed = true; }
    assert(failed && !widget.IsInitialized(), "initialization failure was ignored");
    widget.Initialize();
    assert(widget.Attempts == 2 && widget.IsInitialized() && widget.GetText() == "ready",
        "template installation was not retried");
    widget.Dispose();
}
int nativeScopeReads = 0;
class ManualSynchronization : public VS::Gui::Widget
{
public:
    int Controls;
    void VsguiSynchronizeControl() override { Controls++; }
};
void CheckNativeReadScopes()
{
    VS::Gui::Widget inner = VS::Gui::Widget::Create();
    std::attribute<double> source = std::attribute<double>();
    source.bind([]()->double
    {
        assert(VS::Gui::PropertyEvaluation::Depth == 2, "nested native getter lost its evaluation scope");
        nativeScopeReads++;
        return 0.65;
    });
    inner.SetOpacity(source);
    VS::Gui::Widget outer = VS::Gui::Widget::Create();
    std::attribute<double> forwarding = std::attribute<double>();
    forwarding.bind([inner]()->double
    {
        assert(VS::Gui::PropertyEvaluation::Depth == 1, "native getter lost its evaluation scope");
        double value = inner.GetOpacity();
        assert(VS::Gui::PropertyEvaluation::Depth == 1, "nested getter did not restore its caller scope");
        return value;
    });
    outer.SetOpacity(forwarding);
    VS::Gui::Widget tree = VS::Gui::Widget::Create();
    tree.AddChild(outer);
    tree.AddChild(inner);
    tree.Synchronize();
    assert(nativeScopeReads == 1 && outer.GetOpacity() == 0.65 && inner.GetOpacity() == 0.65 &&
        VS::Gui::PropertyEvaluation::Depth == 0 && VS::Gui::PropertyEvaluation::SafeDepth == 0,
        "native cache queries repeated evaluation or left a scope active");
    tree.Synchronize();
    assert(nativeScopeReads == 2, "dynamic native getters did not run in the next synchronization");
    tree.Dispose();

    ManualSynchronization custom = ManualSynchronization();
    VS::Gui::Context context = VS::Gui::Context("test", 100, 100);
    context.SetRoot(custom);
    context.Update(0);
    int controls = custom.Controls;
    context.Update(1);
    assert(custom.Controls > controls,
        "a derived widget lost its synchronization hooks");
    custom.Dispose();
    context.Close();
}
void CheckNativeFallbackStates()
{
    int widthReads = 0;
    int textReads = 0;
    std::attribute<VS::Gui::Length> width = std::attribute<VS::Gui::Length>();
    width.bind_optional([&widthReads]()->optional<VS::Gui::Length>
    {
        widthReads++;
        return optional<VS::Gui::Length>();
    });
    std::attribute<string> text = std::attribute<string>();
    text.bind_optional([&textReads]()->optional<string>
    {
        textReads++;
        return optional<string>();
    });
    array<VS::Gui::ChangeSignal> fixedSources = {};
    VS::Gui::Binding<VS::Gui::Length> binding = VS::Gui::Binding<VS::Gui::Length>::FromAttribute(width, fixedSources);
    VS::Gui::Widget widget = VS::Gui::Widget::Create();
    widget.GetWidth();
    widget.GetText();
    widget.SetClasses("fallback");
    widget.SetWidth(binding);
    widget.SetText(text);
    VS::Gui::Context context = VS::Gui::Context("test", 100, 100);
    context.SetTheme(".fallback { width: 80px; }");
    context.SetRoot(widget);
    context.Update(0);
    int before = textReads;
    assert(widget.GetWidth() == VS::Gui::Length(80) && widget.GetText() == "" && widthReads == 1,
        "empty structured or scalar binding lost its initial fallback");
    assert(textReads == before, "committed fallback query repeated an optional getter");
    context.SetTheme(".fallback { width: 90px; }");
    context.Update(1);
    assert(widget.GetWidth() == VS::Gui::Length(90) && widthReads == 1 && textReads > before,
        "empty shared binding froze style fallback or ordinary binding stopped polling");
    widget.SetWidth(VS::Gui::Length(120));
    widget.SetText("literal");
    context.Update(2);
    before = textReads;
    assert(widget.GetWidth() == VS::Gui::Length(120) && widget.GetText() == "literal",
        "an unconfigured cache overwrote a literal replacement");
    widget.ClearWidth();
    context.Update(3);
    assert(widget.GetWidth() == VS::Gui::Length(90) && textReads == before,
        "cleared cache hid style fallback or retained its former reader");
    widget.Dispose();
    context.Close();
}

int sharedSnapshotConstructors = 0;
class SharedSnapshotPayload
{
public:
    SharedSnapshotPayload() { sharedSnapshotConstructors++; }
    SharedSnapshotPayload(int number) { sharedSnapshotConstructors++; value = number; }
    int value = 0;
};
struct SharedSnapshotRecord
{
    SharedSnapshotRecord(int number) { value = number; }
    int value = 0;
};
VS::Gui::ChangeSignal sharedSnapshotSignal = nullptr;
VS::Gui::Binding<SharedSnapshotPayload> sharedSnapshotBinding = nullptr;
weak<SharedSnapshotPayload> sharedSnapshotOld = weak<SharedSnapshotPayload>();
weak<SharedSnapshotPayload> sharedSnapshotCurrent = weak<SharedSnapshotPayload>();
void PrepareSharedSnapshotLifecycle()
{
    sharedSnapshotConstructors = 0;
    sharedSnapshotSignal = VS::Gui::ChangeSignal();
    array<VS::Gui::ChangeSignal> sources = {sharedSnapshotSignal};
    sharedSnapshotBinding = VS::Gui::Binding<SharedSnapshotPayload>([]()->SharedSnapshotPayload
    {
        return SharedSnapshotPayload(17);
    }, sources);
    assert(sharedSnapshotConstructors == 0, "constructing a binding constructed its payload");
    SharedSnapshotPayload first = sharedSnapshotBinding.VsguiReadValue();
    optional<SharedSnapshotPayload> optionalValue = sharedSnapshotBinding.VsguiRead();
    assert(first == optionalValue.get() && sharedSnapshotConstructors == 1,
        "shared value and optional readers did not share one evaluation");
    sharedSnapshotOld = weak<SharedSnapshotPayload>(first);
    VS::Gui::Binding<SharedSnapshotRecord> structured = VS::Gui::Binding<SharedSnapshotRecord>([]()->SharedSnapshotRecord
    {
        return SharedSnapshotRecord(23);
    }, sources);
    SharedSnapshotRecord copy = structured.VsguiReadValue();
    copy.value = 99;
    assert(structured.VsguiReadValue().value == 23 && structured.VsguiRead().get().value == 23,
        "shared snapshot lost value semantics or required default construction");
}
void RefreshSharedSnapshotLifecycle()
{
    sharedSnapshotSignal.Notify();
    SharedSnapshotPayload value = sharedSnapshotBinding.VsguiReadValue();
    assert(sharedSnapshotConstructors == 2 && value.value == 17,
        "shared snapshot refresh constructed or evaluated its payload more than once");
    value.value = 31;
    assert(sharedSnapshotBinding.VsguiReadValue().value == 31, "class snapshot lost its mutable shared identity");
    sharedSnapshotCurrent = weak<SharedSnapshotPayload>(value);
}
void CheckSharedSnapshotReplacement()
{
    assert(sharedSnapshotOld.expired() && !sharedSnapshotCurrent.expired(),
        "shared snapshot retained its replaced payload or released its current payload");
    sharedSnapshotBinding = nullptr;
    sharedSnapshotSignal = nullptr;
}
void CheckSharedSnapshotRelease()
{
    assert(sharedSnapshotCurrent.expired(), "releasing a shared binding retained its payload");
}

void CheckSharedBindingRetry()
{
    VS::Gui::ChangeSignal changed = VS::Gui::ChangeSignal();
    array<VS::Gui::ChangeSignal> sources = {changed};
    VS::Gui::Binding<double> binding = nullptr;
    int reads = 0;
    bool recurse = true;
    binding = VS::Gui::Binding<double>([&binding, &reads, &recurse]()->double
    {
        reads++;
        if (recurse) { return binding.VsguiRead().get(); }
        return 0.75;
    }, sources);
    bool failed = false;
    try { binding.VsguiReadValue(); }
    catch (...) { failed = true; }
    assert(failed && reads == 1 && VS::Gui::PropertyEvaluation::Depth == 0,
        "recursive shared binding was accepted or leaked its read scope");
    recurse = false;
    assert(binding.VsguiReadValue() == 0.75 && reads == 2 && binding.VsguiRead().get() == 0.75 && reads == 2,
        "failed shared binding did not retry or repeated a successful cached read");
    changed.Notify();
    recurse = true;
    failed = false;
    try { binding.VsguiReadValue(); }
    catch (...) { failed = true; }
    assert(failed && reads == 3, "cached shared binding hid recursion after invalidation");
    recurse = false;
    assert(binding.VsguiReadValue() == 0.75 && reads == 4,
        "invalidated shared binding could not retry after failure");
    binding = nullptr;
}

class SuspendedProbe : public VS::Gui::Widget
{
public:
    int Synchronizations;
    bool VsguiCanSuspendSynchronization() override { return true; }
    bool VsguiAllowsSourceSnapshot() override { return true; }
    void VsguiSynchronizeControl() override { Synchronizations++; }
};
void CheckLazyCaches()
{
    SuspendedProbe probe = SuspendedProbe();
    VS::Gui::Context context = VS::Gui::Context("test", 100, 100);
    context.SetRoot(probe);
    context.Update(0);
    int before = probe.Synchronizations;
    probe.GetOpacity();
    probe.GetWidth();
    probe.GetText();
    context.Update(1);
    assert(probe.Synchronizations == before, "creating read caches invalidated widget source configuration");
    probe.SetOpacity(0.4);
    context.Update(2);
    assert(probe.Synchronizations > before && probe.GetOpacity() == 0.4, "literal replacement did not wake widget");
    probe.Dispose();
    context.Close();
}
@GuiWidget export class Meter : public VS::Gui::Widget
{
public:
    int Changes;
    bool Fail;
private:
    @GuiProperty(Style) std::attribute<double> Amount = std::attribute<double>(0.1);
protected:
    void OnAmountChanged()
    {
        if (Fail) { throw "notification change hook failed"; }
        Changes++;
    }
};
VS::Gui::Widget interruptedWidget = nullptr;
Meter interruptedMeter = nullptr;
void PrepareInterruptedPropertyReads()
{
    if (interruptedWidget != nullptr) { interruptedWidget.Dispose(); }
    if (interruptedMeter != nullptr) { interruptedMeter.Dispose(); }
    interruptedWidget = VS::Gui::Widget();
    interruptedWidget.SetOpacity(std::attribute<double>(0.8));
    interruptedMeter = Meter();
    interruptedMeter.SetAmount(0.8);
}
double ReadInterruptedNativeProperty() { return interruptedWidget.GetOpacity(); }
double ReadInterruptedGeneratedProperty() { return interruptedMeter.GetAmount(); }
int ReadEvaluationDepth() { return VS::Gui::PropertyEvaluation::Depth; }
int ReadSafeDepth() { return VS::Gui::PropertyEvaluation::SafeDepth; }
double ReadInterruptedBatch()
{
    VS::Gui::BindingUpdates::Batch([]()->void
    {
        VS::Gui::BindingUpdates::Batch([]()->void {});
    });
    VS::Gui::BindingUpdates::VsguiRequireBoundary();
    assert(VS::Gui::__NativeSignal::GetBatchDepth() == 0, "native batch scope leaked");
    return 0.8;
}
void ExhaustGuiCleanup()
{
    VS::Gui::BindingUpdates::Batch([]()->void
    {
        try { while (true) {} }
        finally { while (true) {} }
    });
}

VS::Gui::Observable<double> amount = VS::Gui::Observable<double>(0.2);
VS::Gui::Observable<double> scale = VS::Gui::Observable<double>(1.0);
VS::Gui::Observable<string> title = VS::Gui::Observable<string>("ready");
VS::Gui::Observable<bool> enabled = VS::Gui::Observable<bool>(true);
VS::Gui::Observable<VS::Gui::Length> width = VS::Gui::Observable<VS::Gui::Length>(VS::Gui::Length(25));
int reads = 0;
array<VS::Gui::ChangeSignal> computedSources = {amount, scale};
VS::Gui::Binding<double> computed = VS::Gui::Binding<double>([]()->double
{
    reads++;
    return amount.Get() * scale.Get();
}, computedSources);
VS::Gui::Context gui = nullptr;
VS::Gui::Widget root = nullptr;
VS::Gui::Widget first = nullptr;
VS::Gui::Widget second = nullptr;
VS::Gui::Widget direct = nullptr;
VS::Gui::Widget frameWidget = nullptr;
VS::Gui::ChangeSignal frameSignal = VS::Gui::ChangeSignal();
array<VS::Gui::ChangeSignal> frameSources = {frameSignal};
VS::Gui::Binding<double> frameBinding = VS::Gui::Binding<double>([]()->double
{
    return ReadFrameValue();
}, frameSources);
VS::Gui::ProgressBar progress = nullptr;
Meter meter = nullptr;
int dynamicReads = 0;
double dynamicValue = 0.3;
VS::Gui::Widget dynamicWidget = nullptr;
bool fail = false;
bool mutateInGetter = true;
int failingReads = 0;
array<VS::Gui::ChangeSignal> failingSources = {amount};
VS::Gui::Binding<double> failing = VS::Gui::Binding<double>([]()->double
{
    failingReads++;
    if (fail) { throw "notification getter failed"; }
    return amount.Get();
}, failingSources);

class ReentrantValue
{
public:
    ReentrantValue(VS::Gui::Widget value) { target = weak<VS::Gui::Widget>(value); }
    ~ReentrantValue()
    {
        VS::Gui::Widget value = target.lock();
        if (value != nullptr && !value.IsDisposed()) { value.SetOpacity(0.91); }
    }
    double Read() const { return 0.11; }
private:
    weak<VS::Gui::Widget> target;
};

void InstallReentrantValue()
{
    ReentrantValue source = ReentrantValue(second);
    VS::Gui::Binding<double> binding = VS::Gui::Binding<double>([source]()->double
    {
        return source.Read();
    }, failingSources);
    second.SetOpacity(binding);
    gui.Update(14.5);
}

weak<VS::Gui::Observable<double>> ReleaseTransientObservable()
{
    VS::Gui::Observable<double> source = VS::Gui::Observable<double>(0.5);
    VS::Gui::Widget temporary = VS::Gui::Widget::Create().Opacity(source.AsBinding());
    weak<VS::Gui::Observable<double>> result = weak<VS::Gui::Observable<double>>(source);
    temporary.Dispose();
    return result;
}

void CheckIndependentContexts()
{
    VS::Gui::Observable<double> source = VS::Gui::Observable<double>(0.15);
    VS::Gui::ChangeSignal unrelated = VS::Gui::ChangeSignal();
    array<VS::Gui::ChangeSignal> sources = {source};
    int evaluations = 0;
    VS::Gui::Binding<double> shared = VS::Gui::Binding<double>([source, &evaluations]()->double
    {
        evaluations++;
        return source.Get();
    }, sources);
    VS::Gui::Widget left = VS::Gui::Widget::Create().Opacity(shared);
    VS::Gui::Widget right = VS::Gui::Widget::Create().Opacity(shared);
    VS::Gui::Context a = VS::Gui::Context("test", 100, 100);
    VS::Gui::Context b = VS::Gui::Context("test", 100, 100);
    a.SetRoot(left);
    b.SetRoot(right);
    a.Update(0);
    b.Update(0);
    assert(evaluations == 1, "shared binding was evaluated again in another context");
    unrelated.Notify();
    left.SetText("wake without changing the binding inputs");
    a.Update(1);
    b.Update(1);
    assert(evaluations == 1, "unrelated signal invalidated a cached binding");
    source.Set(0.45);
    a.Update(2);
    assert(evaluations == 2 && left.GetOpacity() == 0.45 && right.GetOpacity() == 0.15,
           "one context prematurely committed another context's property");
    b.Update(2);
    assert(evaluations == 2 && right.GetOpacity() == 0.45,
           "one context consumed another context's pending notification");
    a.SetRoot(nullptr);
    source.Set(0.75);
    b.Update(3);
    a.Update(3);
    a.SetRoot(left);
    a.Update(4);
    assert(evaluations == 3 && left.GetOpacity() == 0.75,
           "remounting lost the retained notification configuration");
    a.SetRoot(nullptr);
    b.SetRoot(nullptr);
    left.Dispose();
    right.Dispose();
    a.Close();
    b.Close();

    int optionalReads = 0;
    std::attribute<double> empty = std::attribute<double>();
    empty.bind_optional([&optionalReads]()->optional<double>
    {
        optionalReads++;
        return optional<double>();
    });
    array<VS::Gui::ChangeSignal> fixedSources = {};
    VS::Gui::Binding<double> optionalBinding = VS::Gui::Binding<double>::FromAttribute(empty, fixedSources);
    VS::Gui::Widget fallback = VS::Gui::Widget::Create().Opacity(optionalBinding);
    fallback.SetClasses("fallback");
    VS::Gui::Context styles = VS::Gui::Context("test", 100, 100);
    styles.SetTheme(".fallback { opacity: 0.25; }");
    styles.SetRoot(fallback);
    styles.Update(0);
    assert(optionalReads == 1 && fallback.GetOpacity() == 0.25,
           "empty notified value did not expose the initial style");
    styles.SetTheme(".fallback { opacity: 0.75; }");
    styles.Update(1);
    assert(optionalReads == 1 && fallback.GetOpacity() == 0.75,
           "cached empty value froze the style fallback or repeated its getter");
    styles.SetRoot(nullptr);
    fallback.Dispose();
    styles.Close();
}

void CreateTree()
{
    first = VS::Gui::Widget::Create().Opacity(computed).Text(title.AsBinding())
        .Enabled(enabled.AsBinding()).Width(width.AsBinding());
    second = VS::Gui::Widget::Create().Opacity(computed);
    direct = VS::Gui::Widget::Create().Opacity(amount.AsBinding());
    frameWidget = VS::Gui::Widget::Create().Opacity(frameBinding);
    progress = VS::Gui::ProgressBar::Create().Value(computed);
    meter = Meter::Create().Amount(computed);
    std::attribute<double> polling = std::attribute<double>();
    polling.bind([]()->double { dynamicReads++; return dynamicValue; });
    dynamicWidget = VS::Gui::Widget::Create().Opacity(polling);
    root = VS::Gui::Widget::Create()[{first, second, direct, frameWidget, progress, meter, dynamicWidget}];
    gui = VS::Gui::Context("test", 100, 100);
    gui.SetRoot(root);
    frameSignal.Notify();
    gui.Update(0);
    assert(reads == 1 && first.GetOpacity() == 0.2 && progress.GetValue() == 0.2 && meter.GetAmount() == 0.2,
           "notification binding construction or shared result");
    assert(progress.IsValueBound() && meter.IsAmountBound(), "notified binding lost its bound state");
    assert(direct.GetOpacity() == 0.2, "observable convenience binding did not initialize");
    assert(amount.AsBinding() == amount.AsBinding(), "live observable bindings were not shared");
    assert(frameWidget.GetOpacity() == 0.125, "notification ran before frame snapshots were captured");
}

void CheckIdleAndBatch()
{
    int previous = dynamicReads;
    gui.Update(1);
    gui.Update(2);
    assert(reads == 1 && dynamicReads > previous, "idle notifications polled or legacy getter stopped polling");
    int changes = meter.Changes;
    amount.Set(0.4);
    amount.Set(0.4);
    amount.Set(0.6);
    title.Set("updated");
    enabled.Set(false);
    width.Set(VS::Gui::Length(48));
    assert(reads == 1 && first.GetOpacity() == 0.2, "notifications executed before an update boundary");
    gui.Update(3);
    assert(reads == 2 && first.GetOpacity() == 0.6 && second.GetOpacity() == 0.6 &&
           progress.GetValue() == 0.6 && meter.GetAmount() == 0.6 && meter.Changes == changes + 1,
           "coalesced notification did not update all consumers once");
    assert(first.GetText() == "updated" && !first.GetEnabled() && first.GetWidth() == VS::Gui::Length(48),
           "string, bool or host-struct notification value was lost");
    VS::Gui::BindingUpdates::Batch([]()->void
    {
        amount.Set(0.7);
        VS::Gui::BindingUpdates::Batch([]()->void { scale.Set(0.5); });
        amount.Set(0.8);
        assert(reads == 2, "nested batch committed an intermediate result");
    });
    assert(reads == 2 && first.GetOpacity() == 0.6, "batch committed before the next safe frame boundary");
    gui.Update(4);
    assert(reads == 3 && first.GetOpacity() == 0.4 && progress.GetValue() == 0.4 && meter.GetAmount() == 0.4,
           "batch did not commit its final result once");
    amount.Set(0.8);
    gui.Update(5);
    assert(reads == 3, "equal observable assignment generated work");
    bool blocked = false;
    VS::Gui::BindingUpdates::Batch([&blocked]()->void
    {
        try { gui.Update(5.25); }
        catch (...) { blocked = true; }
    });
    assert(blocked, "batch allowed an intermediate GUI update");
    uint64 nativeMutation = first.__GetNativeHandle().GetMutationToken();
    uint64 progressMutation = progress.__GetNativeHandle().GetMutationToken();
    changes = meter.Changes;
    amount.Notify();
    gui.Update(5.5);
    assert(reads == 4 && meter.Changes == changes &&
           first.__GetNativeHandle().GetMutationToken() == nativeMutation &&
           progress.__GetNativeHandle().GetMutationToken() == progressMutation,
           "equal computed result changed native properties or repeated a change hook");
}

void CheckReplacementAndFailure()
{
    first.SetOpacity(0.9);
    progress.SetValue(0.9);
    meter.ClearAmount();
    assert(!progress.IsValueBound() && !meter.IsAmountBound(), "replacement did not remove notified binding");
    amount.Set(0.4);
    gui.Update(6);
    assert(first.GetOpacity() == 0.9 && progress.GetValue() == 0.9 && meter.GetAmount() == 0.1 &&
           second.GetOpacity() == 0.2, "replaced notification overwrote a local value");
    second.SetOpacity(failing);
    gui.Update(6.5);
    assert(second.GetOpacity() == 0.4, "replacement notification was not initialized");
    fail = true;
    amount.Set(0.5);
    bool rejected = false;
    try { gui.Update(7); }
    catch (...) { rejected = true; }
    assert(rejected && second.GetOpacity() == 0.4, "failed getter changed the committed value");
    rejected = false;
    try { gui.Update(7.5); }
    catch (...) { rejected = true; }
    assert(rejected && second.GetOpacity() == 0.4, "repeated failed getter lost its pending work");
    fail = false;
    gui.Update(8);
    assert(second.GetOpacity() == 0.5, "failed queue entry was not retried");
    std::attribute<double> optionalValue = std::attribute<double>();
    optionalValue.bind_optional([]()->optional<double>
    {
        if (amount.Get() < 0.5) { return optional<double>(); }
        return optional<double>(amount.Get());
    });
    array<VS::Gui::ChangeSignal> optionalSources = {amount};
    VS::Gui::Binding<double> optionalBinding = VS::Gui::Binding<double>::FromAttribute(optionalValue, optionalSources);
    second.SetOpacity(optionalBinding);
    gui.Update(8.5);
    amount.Set(0.1);
    gui.Update(9);
    assert(second.GetOpacity() == 1.0, "empty notification value did not reveal fallback");
    dynamicValue = 0.7;
    gui.Update(10);
    assert(dynamicWidget.GetOpacity() == 0.7, "legacy dynamic binding no longer follows plain variables");
    meter.SetAmount(computed);
    meter.Fail = true;
    int previousChanges = meter.Changes;
    rejected = false;
    try { gui.Update(10.25); }
    catch (...) { rejected = true; }
    assert(rejected && meter.Changes == previousChanges, "failed hook committed its change notification");
    meter.Fail = false;
    gui.Update(10.5);
    assert(meter.Changes == previousChanges + 1 && meter.GetAmount() == amount.Get() * scale.Get(),
           "cached notification value prevented retrying a failed change hook");
}

void PrepareHostUpdate()
{
    second.SetOpacity(computed);
    amount.Set(0.6);
    frameSignal.Notify();
}

void CheckHostUpdate()
{
    assert(second.GetOpacity() == 0.3, "host-driven update did not drain notifications");
    assert(frameWidget.GetOpacity() == 0.46875, "host-driven notification used an obsolete frame snapshot");
    bool rejected = false;
    try
    {
        VS::Gui::BindingUpdates::Batch([]()->void
        {
            amount.Set(0.7);
            throw "batch failed";
        });
    }
    catch (...) { rejected = true; }
    assert(rejected && amount.Get() == 0.7 && second.GetOpacity() == 0.3,
           "failed batch rolled back data or prematurely updated the GUI");
    gui.Update(12);
    assert(second.GetOpacity() == 0.35, "failed batch left the queue or nesting depth unusable");
    VS::Gui::Binding<double> guarded = VS::Gui::Binding<double>([]()->double
    {
        if (mutateInGetter) { amount.Set(0.9); }
        return amount.Get();
    }, failingSources);
    second.SetOpacity(guarded);
    rejected = false;
    try { gui.Update(13); }
    catch (...) { rejected = true; }
    assert(rejected && amount.Get() == 0.7 && VS::Gui::PropertyEvaluation::Depth == 0 &&
           VS::Gui::PropertyEvaluation::SafeDepth == 0, "getter mutation or failed validation scope leaked");
    mutateInGetter = false;
    gui.Update(14);
    assert(second.GetOpacity() == 0.7, "getter validation failure was not recoverable");
    InstallReentrantValue();
    second.SetOpacity(amount.AsBinding());
    gui.Update(15);
    assert(second.GetOpacity() == 0.91, "capture release overwrote a reentrant replacement");
    amount.Set(0.55);
    gui.Update(16);
    assert(second.GetOpacity() == 0.91, "reentrant replacement retained its notification subscription");
    weak<VS::Gui::Observable<double>> released = ReleaseTransientObservable();
    assert(released.lock() == nullptr, "observable convenience cache retained a source-binding ownership cycle");
    int before = reads;
    gui.SetRoot(nullptr);
    root.Dispose();
    amount.Set(0.2);
    gui.Update(17);
    assert(reads == before, "disposed widgets retained queued notification work");
    gui.Close();
}

VS::Gui::Observable<double> inputAmount = VS::Gui::Observable<double>(0.25);
VS::Gui::Slider inputSlider = nullptr;
int inputEvents = 0;
weak<VS::Gui::Button> transientControl = weak<VS::Gui::Button>();
VS::Gui::__NativeWidget KeepTransientControl()
{
    VS::Gui::Button value = VS::Gui::Button::Create();
    transientControl = weak<VS::Gui::Button>(value);
    return value.__GetNativeHandle();
}
void CheckControlCallbackLifetime()
{
    VS::Gui::__NativeWidget native = KeepTransientControl();
    assert(transientControl.expired(), "control callback retained its script widget");
    assert(native.GetIdentity() > 0, "native control did not survive its collected script view");
}
void CreateInputTree()
{
    inputEvents = 0;
    inputAmount.Set(0.25);
    inputSlider = VS::Gui::Slider::Create().Value(inputAmount.AsBinding())
        .OnValueChanged([](VS::Gui::Slider sender, double candidate)->void
        {
            inputEvents++;
            inputAmount.Set(candidate);
        });
    gui = VS::Gui::Context("test", 100, 100);
    gui.SetRoot(inputSlider);
    gui.Update(20);
    assert(inputEvents == 0, "programmatic notification emitted a user input event");
}
void ForceControlDispatchFallback()
{
    inputSlider.__GetNativeHandle().SetControlActionCallback(
        [](int action, bool checkedValue, double number, string text, double x, double y)->bool { return false; });
}
void CheckInputTree()
{
    assert(inputEvents >= 2 && inputAmount.Get() < 0.5 && inputSlider.IsValueBound() &&
           inputSlider.GetValue() == inputAmount.Get() && inputSlider.GetActualValue() == inputAmount.Get(),
           "dragging a notified slider lost candidate events, its binding or the final value");
    gui.SetRoot(nullptr);
    inputSlider.Dispose();
    gui.Close();
}
void CheckDisposedInputTree()
{
    assert(inputSlider.IsDisposed() && inputEvents == 0,
           "a disposed control still received its pending script action");
    gui.SetRoot(nullptr);
    inputSlider.Dispose();
    gui.Close();
}

)vs";
		Source += "int main(){CheckObservableSetSemantics();CheckSubscriptionRetry();CheckNestedBatchFailure();CheckTemplateRetry();CheckNativeReadScopes();CheckNativeFallbackStates();CheckSharedBindingRetry();CheckControlCallbackLifetime();CheckLazyCaches();CheckIndependentContexts();CreateTree();CheckIdleAndBatch();";
		if (!BackendPath.empty())
		{
			// This case deliberately uses script-mode JIT; the VM case below
			// exercises ownership across persistent host entries.
			Source += "PrepareSharedSnapshotLifecycle();RefreshSharedSnapshotLifecycle();root.Dispose();gui.Close();";
		}
		Source += "return 0;}";
		const auto Loaded = Session.LoadSource(Source, "gui-notifications.vs");
		Require(Loaded.bSuccess, Loaded.Error.Message);
		auto Result = Session.Run();
		Require(Result.bSuccess, Result.Error.Message);
		std::cout << "GUI notification script cases passed\n";
		const auto MainJitStats = Session.GetLastJitStats();
		if (BackendPath.empty())
		{
			// Ownership checks span persistent entry boundaries. The script-mode
			// case runs its value checks in main before global cleanup.
			for (const char* Function : {"PrepareSharedSnapshotLifecycle", "RefreshSharedSnapshotLifecycle",
			         "CheckSharedSnapshotReplacement", "CheckSharedSnapshotRelease"})
			{
				Result = Session.CallFunction(Function, {});
				Require(Result.bSuccess, std::string(Function) + ": " + Result.Error.Message);
			}
		}
		if (!BackendPath.empty())
		{
			const auto Stats = MainJitStats;
			std::cout << "GUI notification JIT attempts=" << Stats.DispatchAttempts << " dispatches=" << Stats.Dispatches
			          << " native leaf=" << Stats.NativeLeafExecutions
			          << " native CFG=" << Stats.NativeCfgExecutions
			          << " linear=" << Stats.LinearExecutions
			          << " bridge=" << Stats.BridgeExecutions
			          << " VM trampoline=" << Stats.VmTrampolineExecutions
			          << " direct native=" << Stats.DirectNativeCalls
			          << " controlled native=" << Stats.ControlledNativeExecutions
			          << " native loops=" << Stats.NativeTypedLoopExecutions
			          << " host scalar=" << Stats.ScalarNativeFastPathCalls
			          << " fallbacks=" << Stats.Fallbacks
			          << " policy fallbacks=" << Stats.CostModelVmFallbacks << " failures=" << Stats.Failures << '\n';
			Require(Stats.DispatchAttempts > 0 && Stats.Failures == 0 &&
			        (Stats.Dispatches > 0 || Stats.Fallbacks > 0),
			    "Notification tests did not use the matching JIT policy successfully");
			return;
		}
		bool bInterruptedAfterPublication = false;
		const auto SavedLimits = Session.GetExecutionLimits();
		for (std::uint64_t Budget = 1; Budget <= 96; ++Budget)
		{
			Result = Session.CallFunction("PrepareInterruptedNotification", {});
			Require(Result.bSuccess, Result.Error.Message);
			FValue Revision;
			Result = Session.CallFunction("ReadInterruptedRevision", {}, &Revision);
			Require(Result.bSuccess, Result.Error.Message);
			Session.SetExecutionLimits({Budget, 0});
			const auto Interrupted = Session.CallFunction("PublishInterruptedNotification", {});
			Session.SetExecutionLimits(SavedLimits);
			FValue Current;
			Result = Session.CallFunction("ReadInterruptedRevision", {}, &Current);
			Require(Result.bSuccess, Result.Error.Message);
			if (!Interrupted.bSuccess && std::get<std::int64_t>(Current) != std::get<std::int64_t>(Revision))
			{
				bInterruptedAfterPublication = true;
				FValue Value;
				Result = Session.CallFunction("ReadInterruptedBinding", {}, &Value);
				Require(Result.bSuccess && std::get<double>(Value) == 0.8,
				    "Interrupted publication left the shared binding cache valid");
			}
		}
		Require(bInterruptedAfterPublication, "Notification interruption did not cover the publication boundary");
		FPackageBuildOptions PackageOptions;
		PackageOptions.PackageName = "gui-interruption-regressions";
		std::vector<std::uint8_t> PackageBytes;
		Require(BuildPackage(*Session.GetLoadedModule(), PackageOptions, PackageBytes, Error), Error);
		const auto MakeProbe = [&]()
		{
			auto Probe = std::make_unique<VeroScriptSession>(Host);
			Probe->SetHotReloadEnabled(true);
			Require(Probe->GetBindingRegistry().RegisterNative("ReadFrameValue", ENativeType::Double, {},
			            [](const FNativeCallContext&, FValue& Out, std::string&)
			{
				Out = 0.125;
				return true;
			}, nullptr, Error),
			    Error);
			Require(RegisterScriptBindings(Probe->GetBindingRegistry(), Error), Error);
			Probe->ApplyConfig(Config);
			FPackageData Package;
			Require(ReadPackage({PackageBytes.data(), PackageBytes.size()}, Package, Error), Error);
			const auto LoadedProbe = Probe->LoadPackage(Package, "gui-interruption-regressions.vspkg");
			Require(LoadedProbe.bSuccess, LoadedProbe.Error.Message);
			const auto Initialized = Probe->Run(ERunMode::EvalOnce);
			Require(Initialized.bSuccess, Initialized.Error.Message);
			return Probe;
		};
		auto Probe = MakeProbe();
		std::size_t Recovered = 0, Resets = 0;
		for (const char* Getter : {"ReadInterruptedBinding", "ReadInterruptedNativeProperty",
		         "ReadInterruptedGeneratedProperty", "ReadInterruptedBatch"})
		{
			const auto PrepareProbe = [&]()
			{
				if (std::string_view(Getter) == "ReadInterruptedBinding")
				{
					Result = Probe->CallFunction("PrepareInterruptedNotification", {});
					Require(Result.bSuccess, Result.Error.Message);
					Result = Probe->CallFunction("PublishInterruptedNotification", {});
				}
				else
				{
					Result = Probe->CallFunction("PrepareInterruptedPropertyReads", {});
				}
				Require(Result.bSuccess, Result.Error.Message);
			};
			bool bCompleted = false;
			for (std::uint64_t Budget = 1; Budget <= 1024; ++Budget)
			{
				PrepareProbe();
				Probe->SetExecutionLimits({Budget, 0});
				FValue LimitedValue;
				const auto Limited = Probe->CallFunction(Getter, {}, &LimitedValue);
				Probe->SetExecutionLimits(SavedLimits);
				const auto Label = std::string(Getter) + " budget=" + std::to_string(Budget);
				if (Probe->RequiresReset())
				{
					Require(!Limited.bSuccess, Label + ": incomplete cleanup was reported as success");
					const auto Rejected = Probe->CallFunction("ReadEvaluationDepth", {});
					Require(!Rejected.bSuccess && Rejected.Error.MessageId == "diag.execution_state_reset_required",
					    Label + ": unsafe state was reused");
					++Resets;
					// Core does not replay an interrupted finally. Recreate both the
					// program and its GUI bridge, without recompiling the framework.
					Probe.reset();
					Probe = MakeProbe();
					PrepareProbe();
				}
				else if (!Limited.bSuccess)
				{
					++Recovered;
				}
				for (const char* Depth : {"ReadEvaluationDepth", "ReadSafeDepth"})
				{
					FValue Value;
					Result = Probe->CallFunction(Depth, {}, &Value);
					Require(Result.bSuccess && std::get<std::int32_t>(Value) == 0,
					    Label + ": leaked evaluation scope: " + Result.Error.Message);
				}
				FValue Value;
				Result = Probe->CallFunction(Getter, {}, &Value);
				Require(Result.bSuccess && std::get<double>(Value) == 0.8,
				    Label + ": getter retry failed: " + Result.Error.Message);
				if (Limited.bSuccess)
				{
					const auto Returned = std::get_if<double>(&LimitedValue);
					Require(Returned && *Returned == 0.8, Label + ": limited call returned an incomplete result");
					bCompleted = true;
					break;
				}
			}
			Require(bCompleted, std::string(Getter) + ": interruption sweep did not cover the whole call");
		}
		bool bObservableCompleted = false, bInterruptedObservableCommitted = false;
		for (std::uint64_t Budget = 1; Budget <= 1024; ++Budget)
		{
			Result = Probe->CallFunction("PrepareInterruptedObservable", {});
			Require(Result.bSuccess, Result.Error.Message);
			Probe->SetExecutionLimits({Budget, 0});
			const auto Limited = Probe->CallFunction("PublishInterruptedObservable", {});
			Probe->SetExecutionLimits(SavedLimits);
			const auto Label = "Observable.Set budget=" + std::to_string(Budget);
			if (Probe->RequiresReset())
			{
				Require(!Limited.bSuccess, Label + ": incomplete cleanup was reported as success");
				const auto Rejected = Probe->CallFunction("ReadInterruptedObservableBinding", {});
				Require(!Rejected.bSuccess && Rejected.Error.MessageId == "diag.execution_state_reset_required",
				    Label + ": unsafe observable state was reused");
				++Resets;
				Probe.reset();
				Probe = MakeProbe();
				continue;
			}
			FValue Stored, Cached;
			Result = Probe->CallFunction("ReadInterruptedObservable", {}, &Stored);
			Require(Result.bSuccess, Label + ": " + Result.Error.Message);
			Result = Probe->CallFunction("ReadInterruptedObservableBinding", {}, &Cached);
			Require(Result.bSuccess && std::get<double>(Stored) == std::get<double>(Cached),
			    Label + ": interrupted observable assignment left a stale shared binding");
			if (!Limited.bSuccess)
			{
				++Recovered;
				bInterruptedObservableCommitted = bInterruptedObservableCommitted || std::get<double>(Stored) == 0.8;
			}
			Result = Probe->CallFunction("PublishInterruptedObservable", {});
			Require(Result.bSuccess, Label + ": retry failed: " + Result.Error.Message);
			Result = Probe->CallFunction("ReadInterruptedObservableBinding", {}, &Cached);
			Require(Result.bSuccess && std::get<double>(Cached) == 0.8,
			    Label + ": observable retry left a stale shared binding");
			if (Limited.bSuccess)
			{
				bObservableCompleted = true;
				break;
			}
		}
		Require(bObservableCompleted && bInterruptedObservableCommitted,
		    "Observable.Set interruption sweep missed its publication boundary or successful completion");
		std::cout << "GUI observable interruption publication and retry passed\n";
		Probe->SetExecutionLimits({100000, 0});
		const auto Exhausted = Probe->CallFunction("ExhaustGuiCleanup", {});
		Probe->SetExecutionLimits(SavedLimits);
		Require(!Exhausted.bSuccess && Probe->RequiresReset(), "Exhausted GUI cleanup did not require reset");
		const auto Rejected = Probe->CallFunction("ReadInterruptedBatch", {});
		Require(!Rejected.bSuccess && Rejected.Error.MessageId == "diag.execution_state_reset_required",
		    "Exhausted GUI cleanup allowed another batch");
		++Resets;
		Probe.reset();
		Probe = MakeProbe();
		FValue FreshBatch;
		Result = Probe->CallFunction("ReadInterruptedBatch", {}, &FreshBatch);
		Require(Result.bSuccess && std::get<double>(FreshBatch) == 0.8,
		    "Fresh GUI binding context retained an exhausted batch");
		Require(Recovered != 0, "Interruption sweep did not cover successful cleanup and retry");
		std::cout << "GUI interrupted calls recovered=" << Recovered << " required resets=" << Resets << '\n';
		Result = Session.CallFunction("CheckReplacementAndFailure", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("PrepareHostUpdate", {});
		Require(Result.bSuccess, Result.Error.Message);
		Require(Native && Native->Update(11, Error), Error);
		Result = Session.CallFunction("CheckHostUpdate", {});
		Require(Result.bSuccess, Result.Error.Message);
		for (const bool bFallback : {false, true})
		{
			Result = Session.CallFunction("CreateInputTree", {});
			Require(Result.bSuccess, Result.Error.Message);
			if (bFallback)
			{
				Result = Session.CallFunction("ForceControlDispatchFallback", {});
				Require(Result.bSuccess, Result.Error.Message);
			}
			Platform::FEvent Input;
			Input.Type = Platform::EEventType::PointerDown;
			Input.WindowId = 1;
			Input.PointerId = 7;
			Input.Button = 1;
			Input.X = 80;
			Input.Y = 50;
			bool bHandled = false;
			Require(Native->ProcessEvent(Input, bHandled, Error), Error);
			Input.Type = Platform::EEventType::PointerMove;
			Input.X = 20;
			Require(Native->ProcessEvent(Input, bHandled, Error), Error);
			Input.Type = Platform::EEventType::PointerUp;
			Require(Native->ProcessEvent(Input, bHandled, Error) && Native->Update(21, Error), Error);
			Result = Session.CallFunction("CheckInputTree", {});
			Require(Result.bSuccess, Result.Error.Message);
		}
		for (const bool bFallback : {false, true})
		{
			Result = Session.CallFunction("CreateInputTree", {});
			Require(Result.bSuccess, Result.Error.Message);
			if (bFallback)
			{
				Result = Session.CallFunction("ForceControlDispatchFallback", {});
				Require(Result.bSuccess, Result.Error.Message);
			}
			ControlDispatches = ControlDisposals = 0;
			DisposeBeforeControlDispatch = bFallback ? "VS.Gui.Internal.DispatchControl" : "VS.Gui.Internal.DispatchControlCallback";
			bCountControlDispatches = true;
			Platform::FEvent Input;
			Input.Type = Platform::EEventType::PointerDown;
			Input.WindowId = 1;
			Input.PointerId = 7;
			Input.Button = 1;
			Input.X = 80;
			Input.Y = 50;
			bool bHandled = false;
			const bool Success = Native->ProcessEvent(Input, bHandled, Error);
			bCountControlDispatches = false;
			Require(Success, Error);
			Require(ControlDisposals == 1 && DisposeBeforeControlDispatch.empty() &&
			        ControlDispatches == (bFallback ? 2u : 1u),
			    "disposal re-entered or duplicated the control dispatch bridge");
			Result = Session.CallFunction("CheckDisposedInputTree", {});
			Require(Result.bSuccess, Result.Error.Message);
		}
	}
}

int main(int Argc, char** Argv)
{
	try
	{
		Require(Argc <= 2, "usage: VeroScriptGuiNotificationTests [jit-backend]");
		CheckNotifications(Argc == 2 ? Argv[1] : std::string{});
		std::cout << "GUI notification queue, batching and lifecycle passed\n";
		return 0;
	}
	catch (const std::exception& Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptGuiScript.h"
#include "VeroScriptGuiContext.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptSession.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptVariant.h"
#include "Bindings/VeroScriptGuiScriptInternal.h"
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	using namespace Gui;

	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	void CheckSourceSnapshots()
	{
		for (const auto CellType : {ClosureReferenceCellTypeName, ClosureReadonlyCellTypeName})
		{
			auto Owner = std::make_shared<FScriptObject>();
			auto Attribute = std::make_shared<FScriptObject>();
			Attribute->Kind = EScriptObjectKind::Struct;
			Owner->Fields.push_back(Attribute);
			ScriptBindings::FScriptLifecycle State;
			State.bObserveSources = true;
			State.ScriptOwner = Owner;
			const auto Widget = FWidget::Create();
			State.CaptureSources(*Widget);
			Require(State.Fields.size() == 1, "source snapshot missed an ordinary attribute field");
			auto Cell = std::make_shared<FDynamicArray>();
			Cell->TypeName = CellType;
			Cell->Elements.push_back(Attribute);
			Owner->Fields[0] = Cell;
			bool bChanged = false;
			State.SourcesChanged(*Widget, bChanged);
			Require(!bChanged, "boxing an unchanged source invalidated its snapshot");
			State.CaptureSources(*Widget);
			Require(State.Fields.size() == 1, "source snapshot missed a boxed attribute field");
			++Attribute->MutationVersion;
			State.SourcesChanged(*Widget, bChanged);
			Require(bChanged, "source snapshot missed a boxed attribute mutation");
			State.CaptureSources(*Widget);
			State.SourcesChanged(*Widget, bChanged);
			Require(!bChanged, "source snapshot failed to settle after a boxed attribute mutation");
			Cell->Elements[0] = std::make_shared<FScriptObject>();
			State.SourcesChanged(*Widget, bChanged);
			Require(bChanged, "source snapshot missed a replaced boxed attribute");
		}
	}

	void CheckConstQueries()
	{
		FHostBindingRegistry Bindings;
		std::string Error;
		Require(RegisterScriptBindings(Bindings, Error), Error);
		FCompileOptions Options;
		VeroScript::Test::Configure(Options);
		Options.Bindings = &Bindings;
		const auto Compiled = VeroScriptCompile::CompileSource(R"vs(
void CheckScope(const VS::Gui::__NativeWidget widget)
{
    widget.FindInScope("missing", 0);
}
int main()
{
    VS::Gui::__NativeWidget source = VS::Gui::__NativeWidget();
    array<string> names = {};
    array<int64> values = {};
    source.RegisterStyleProperty("amount", "amount", 0, names, values);
    const VS::Gui::__NativeWidget widget = source;
    const VS::Gui::__NativeElements elements = VS::Gui::__NativeElements();
    fn()->bool hasStyle = [widget]()->bool { return widget.HasStyleValue("amount"); };
    if(hasStyle() || elements.HasLookup() || elements.Find("missing") != nullptr) return 1;
    return 0;
}
)vs",
		    "const-gui-queries.vs", Options);
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		Require(Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Bindings.GetNativeFunctions()),
		    Vm.GetError().Message);
		Require(Vm.GetProcessExitCode() == 0, "readonly GUI lookup changed its query or required-element behavior");
		const auto Required = VeroScriptCompile::CompileSource(
		    "void main(){const VS::Gui::__NativeElements elements=VS::Gui::__NativeElements();elements.Require(\"missing\");}",
		    "const-gui-required-element.vs", Options);
		Require(Required.bSuccess, Required.Error.Message);
		Require(!Vm.Execute(*Required.Module, ERunMode::Script, {}, &Bindings.GetNativeFunctions()) &&
		        Vm.GetError().Message.find("gui.element_required") != std::string::npos,
		    "readonly required-element lookup lost its missing-element error");
		Require(!VeroScriptCompile::CompileSource(
		            "void main(){const VS::Gui::__NativeWidget widget=VS::Gui::__NativeWidget();widget.SetId(\"changed\");}",
		            "const-gui-mutation.vs", Options)
		            .bSuccess,
		    "readonly GUI widget accepted a mutating method");
	}

	class FBackend final : public IBackendContext
	{
	public:
		//~ Begin IBackendContext Interface
		FCapabilities GetCapabilities() const noexcept override
		{
			FCapabilities Result;
			Result.bLayout = true;
			Result.bScrolling = bRevealFocus;
			Result.bExtendedControls = bRevealFocus;
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
				FLayoutBox Box{Node.Id, {0, 0, 100, 100}, {0, 0, 100, 100}};
				if (bRevealFocus && Node.Control.Kind == EControlKind::ScrollView)
				{
					Box.ScrollWidth = 100;
					Box.ScrollHeight = 400;
					Box.ScrollY = Node.Control.ScrollY;
				}
				if (bRevealFocus && Node.Control.Kind == EControlKind::Button)
				{
					Box.Border.Y = 200;
					Box.Content.Y = 200;
				}
				Boxes.push_back(Box);
			}
			return true;
		}
		bool Render(bool& bPresented, std::string& Error) override
		{
			for (const auto& Node : Nodes)
			{
				if (Node.Paint)
				{
					FPaintContext Canvas({0, 0, 100, 100});
					if (!Node.Paint(Canvas, Error) || !Canvas.Finish(Error))
					{
						return false;
					}
				}
			}
			bPresented = false;
			return true;
		}
		bool Close(std::string&) override
		{
			return true;
		}
		//~ End IBackendContext Interface

		std::vector<FBackendNode> Nodes;
		bool bRevealFocus = false;
	};

	void CheckContextBackendSelection()
	{
		struct FCase
		{
			const char* Invocation;
			const char* Environment;
			const char* Expected;
		};
		// nullptr omits a callback; an empty string leaves this role unspecified.
		const FCase Cases[] = {
		    {"invocation-gui", "environment-gui", "invocation-gui"},
		    {"", "environment-gui", "environment-gui"},
		    {nullptr, "environment-gui", "environment-gui"},
		    {nullptr, "", "rmlui"},
		    {"", "", "rmlui"},
		    {"", nullptr, "rmlui"},
		    {nullptr, nullptr, "rmlui"}};
		for (const auto& Case : Cases)
		{
			std::vector<std::string> Choices;
			FScriptServices Services;
			if (Case.Invocation)
			{
				Services.DefaultBackend = [Selected = std::string(Case.Invocation)](std::string_view Role)
				{
					return Role == "gui" ? Selected : std::string{};
				};
			}
			if (Case.Environment)
			{
				Services.Application.DefaultBackend = [Selected = std::string(Case.Environment)](std::string_view Role)
				{
					return Role == "gui" ? Selected : std::string("none");
				};
				Services.Application.Font = [](const auto&, auto&) -> std::shared_ptr<Font::IService>
				{
					throw std::runtime_error("disabled inherited font factory was called");
				};
			}
			Services.Application.Gui = [&](const auto& Name, const auto&, auto&) -> std::unique_ptr<IBackendContext>
			{
				Choices.push_back(Name);
				return std::make_unique<FBackend>();
			};
			std::string Error;
			FHostBindingRegistry Bindings;
			Require(RegisterScriptBindings(Bindings, std::move(Services), Error), Error);
			FCompileOptions Options;
			VeroScript::Test::Configure(Options);
			Options.Bindings = &Bindings;
			const auto Compiled = VeroScriptCompile::CompileSource(R"vs(
void main()
{
    VS::Gui::__NativeContext inherited = VS::Gui::__NativeContext("", 100, 100, 1.0);
    inherited.Close();
    VS::Gui::__NativeContext selected = VS::Gui::__NativeContext("explicit-gui", 100, 100, 1.0);
    selected.Close();
}
)vs",
			    "context-backend-selection.vs", Options);
			Require(Compiled.bSuccess, Compiled.Error.Message);
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Require(Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Bindings.GetNativeFunctions()), Vm.GetError().Message);
			Require(Choices == std::vector<std::string>{Case.Expected, "explicit-gui"},
			    "context backend selection changed invocation/environment/explicit precedence or native fallback");
		}
		FMarkupScript Script;
		Script.Source = {"rejected-engine.vswml", "public: int Value = 1;"};
		Script.Location = {Script.Source.Identity, 0, 4, 2};
		Script.bInline = true;
		FMarkupScriptOptions Options;
		VeroScript::Test::Configure(Options.Engine);
		Options.Engine.bClearVmBackend = true;
		FMarkupDiagnostic Diagnostic;
		const auto DirectContext = MakeMarkupScriptFactory(Options)(Script, {}, Diagnostic);
		Require(!DirectContext &&
		        Diagnostic.Location.Source == Script.Location.Source && Diagnostic.Location.Line == 4 &&
		        Diagnostic.Message.find("VM factory, path and clear settings are exclusive") != std::string::npos,
		    "GUI engine rejection lost its reason or location: " + Diagnostic.Format());

		int SelectionCalls = 0;
		FModuleEnvironmentDesc Description;
		Description.Scopes.push_back({"app", {}, false});
		Description.EntryOwnerId = "app";
		Description.bStrictSources = false;
		Description.bAllowAmbientSearch = true;
		Description.ExecutionBackends.Identity = "gui-configuration-rejection";
		Description.ExecutionBackends.Vm = [&](auto, auto&, std::string& Error)
		{
			++SelectionCalls;
			Error = "gui-environment-configuration-rejected";
			return false;
		};
		Options.Engine = {};
		Options.Engine.FallbackVmBackendPath = "__vsgui_missing_fallback_vm__";
		std::string Error;
		Options.Engine.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
		Require(Options.Engine.ModuleEnvironment != nullptr, Error);
		const auto EnvironmentContext = MakeMarkupScriptFactory(Options)(Script, {}, Diagnostic);
		Require(!EnvironmentContext && SelectionCalls == 1 &&
		        Diagnostic.Location.Source == Script.Location.Source && Diagnostic.Location.Line == 4 &&
		        Diagnostic.Message == "gui-environment-configuration-rejected",
		    "GUI environment configuration rejection was ignored: " + Diagnostic.Format());
	}

	double Gap(const std::shared_ptr<FWidget>& Widget)
	{
		FPropertyValue Value;
		std::string Error;
		Require(Widget && Widget->Get(EProperty::Gap, Value, Error), Error);
		return std::get<double>(Value);
	}

	void CheckPublicRuntime()
	{
		struct FHost final : IEngineHost
		{
			void OnPrintLine(std::string_view) override
			{
			}
			void OnDiagnosticLine(std::string_view Text) override
			{
				std::cerr << Text << '\n';
			}
		} Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		std::shared_ptr<FContext> Native;
		FScriptServices Services;
		Services.CreateContext = [&](const auto& Name, const auto& Viewport, auto& Error)
		{
			Require(Name == "test" || Name == "focus", "Context ignored the backend selection");
			auto Backend = std::make_unique<FBackend>();
			Backend->bRevealFocus = Name == "focus";
			Native = FContext::Create(std::move(Backend), Viewport, Error);
			return Native;
		};
		Services.IsCurrentProgram = [&](const auto& Program, auto Generation)
		{
			const auto Current = Session.GetDebugProgram();
			return Current.Generation == Generation && Current.Owner.lock() == Program.lock();
		};
		Services.Invoke = [&](auto Function, auto& Arguments, auto& Out, auto& Error)
		{
			const auto Result = Session.CallFunction(Function, Arguments, &Out);
			Error = Result.Error.Message;
			return Result.bSuccess;
		};
		std::string Error;
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		Require(RegisterScriptBindings(Session.GetBindingRegistry(), std::move(Services), Error) &&
		        ConfigureGuiScriptEngine(Config, Error),
		    Error);
		Session.ApplyConfig(Config);
		auto Loaded = Session.LoadSource(R"vs(
@GuiWidget export class RuntimeProbe : public VS::Gui::Widget
{
public:
    int InitializedCalls = 0;
    int MountedCalls = 0;
    int UnmountedCalls = 0;
    int LayoutCalls = 0;
    int PaintCalls = 0;
    optional<VS::Gui::PaintContext> SavedCanvas;
protected:
    void OnInitialized() override { InitializedCalls++; }
    void OnMounted() override { assert(IsMounted(), "mount state"); MountedCalls++; }
    void OnUnmounted() override { assert(IsMounted(), "old context was cleared before cleanup"); UnmountedCalls++; }
    void OnLayoutChanged() override { LayoutCalls++; }
    void OnPaint(VS::Gui::PaintContext canvas) override
    {
        PaintCalls++;
        if (GetChildCount()>3)
        {
            assert(GetChild(GetChildCount()-1).GetGap()==23,"borrowed view could not be read during rendering");
        }
        SavedCanvas=optional<VS::Gui::PaintContext>(canvas);
        canvas.PushClip(0,0,50,50);
        canvas.FillRect(0,0,canvas.GetWidth(),canvas.GetHeight(),VS::Gui::Color(1,0,0,1));
        canvas.PopClip();
    }
};
@GuiWidget export class RuntimeGroup : public VS::Gui::Widget
{
public:
    RuntimeGroup() { part = RuntimeProbe::Create().Id("internal"); AddInternalChild(part); }
    RuntimeProbe part;
private:
    @GuiProperty double Amount = 1;
    void OnAmountChanged() { part.SetGap(GetAmount()); }
};
@GuiWidget export class QuietWidget : public VS::Gui::Widget {};
class UnregisteredWidget : public QuietWidget
{
public:
    int SyncCalls=0;
    void VsguiSynchronizeControl() override { SyncCalls++; }
};
class LateBindingState : public VS::Gui::BindingState
{
public:
    int Calls=0;
    void VsguiSynchronizeBindings() override { Calls++; }
};
QuietWidget quiet=nullptr;
UnregisteredWidget custom=nullptr;
double quietModel=2;
int quietReads=0;
std::attribute<double> quietSource=std::attribute<double>();
VS::Gui::Context gui = nullptr;
RuntimeProbe root = nullptr;
RuntimeProbe child = nullptr;
VS::Gui::Event saved = nullptr;
VS::Gui::Subscription connection = nullptr;
int calls = 0;
int captures = 0;
int posted = 0;
int postPayloadDestroyed = 0;
int postPayloadExecuted = 0;
class PostPayload
{
public:
    int Value = 23;
    ~PostPayload()
    {
        static int destroyed = 0;
        postPayloadDestroyed = ++destroyed;
    }
};
void QueueOwnedPost()
{
    PostPayload payload = PostPayload();
    gui.Post([payload = payload]() mutable ->void
    {
        assert(payload.Value == 23,"posted capture was destroyed before execution");
        postPayloadExecuted++;
    });
}
void CheckPostExecuted()
{
    assert(postPayloadExecuted == 1 && postPayloadDestroyed == 1,
        "completed posted work did not release its captured script object");
}
void CheckPostDiscarded()
{
    assert(postPayloadExecuted == 1 && postPayloadDestroyed == 2,
        "closing a context did not release its queued captured script object");
}
int postedCaptureValue = 0;
void QueueCapturedPosts()
{
    int seed = 3;
    fn()->void work = [value = seed, &target = postedCaptureValue]() mutable ->void
    {
        static int calls = 0;
        value++;
        calls++;
        target = value * 10 + calls;
    };
    gui.Post(work);
    gui.Post(work);
}
void CheckCapturedPosts()
{
    assert(postedCaptureValue == 52,
        "posted work lost its initialized/reference captures, shared mutable state or local static");
}
void CheckFocusBridge()
{
    VS::Gui::Context focus = VS::Gui::Context("focus",100,100);
    VS::Gui::Button target = VS::Gui::Button::Create();
    VS::Gui::ScrollView scroller = VS::Gui::ScrollView::Create()[{target}];
    int notifications = 0;
    auto subscription = scroller.AddScrolled([&notifications](VS::Gui::ScrollView sender,double x,double y)->void
    {
        assert(y == 200,"focus reveal supplied the wrong scroll offset");
        notifications++;
    });
    focus.SetRoot(scroller);
    focus.Update(0);
    focus.SetFocus(target);
    assert(focus.GetFocus() == target && notifications == 1 && scroller.GetScrollY() == 200,
        "context focus did not dispatch scrolling through the active script invoker");
    focus.Close();
}
void CheckControlFractions()
{
    VS::Gui::ProgressBar progress = VS::Gui::ProgressBar::Create().Maximum(1e308).Value(1e308);
    VS::Gui::Slider slider = VS::Gui::Slider::Create().Maximum(1e308).Step(0).Value(1e308);
    VS::Gui::ScrollBar scroll = VS::Gui::ScrollBar::Create().Maximum(1e308).Step(0).Value(1e308);
    assert(progress.GetActualValue() == 1e308 && slider.GetActualValue() == 1e308 &&
        scroll.GetActualValue() == 1e308,"initial control values were not synchronized");
    progress.SetMinimum(-1e308);
    progress.SetMaximum(-5e307);
    slider.SetMinimum(-1e308);
    slider.SetMaximum(-5e307);
    scroll.SetMinimum(-1e308);
    scroll.SetMaximum(-5e307);
    assert(progress.GetNormalizedValue() == 1 && slider.GetNormalizedValue() == 1 &&
        scroll.GetNormalizedValue() == 1,"pending control ranges overflowed before clamping the actual value");
}
void Setup()
{
    child = RuntimeProbe::Create().Id("child").Focusable(true);
    quiet=QuietWidget::Create();
    custom=UnregisteredWidget();
    custom.Initialize();
    quiet.SetHitTest(VS::Gui::HitTestMode::None);
    custom.SetHitTest(VS::Gui::HitTestMode::None);
    assert(!quiet.VsguiHasCustomSynchronization() && custom.VsguiHasCustomSynchronization(),
        "static synchronization hint was inherited by an unregistered subclass");
    quietModel=2;
    quietReads=0;
    quietSource=std::attribute<double>();
    array<VS::Gui::Widget> children = {child,quiet,custom};
    root = RuntimeProbe::Create()[children];
    assert(root.InitializedCalls == 1 && child.InitializedCalls == 1, "construction initialization");
    RuntimeProbe external = RuntimeProbe::Create();
    external.SetImage("icon.png");
    string imageIdentity=external.GetImage();
    external.SetImage(std::attribute<string>("icon.png"));
    assert(external.GetImage()==imageIdentity,"bound image getter changed its resolved source base");
    external.ClearImage();
    external.SetBackgroundImage("url(icon.png)");
    string backgroundIdentity=external.GetBackgroundImage();
    external.SetBackgroundImage(std::attribute<string>("url(icon.png)"));
    assert(external.GetBackgroundImage()==backgroundIdentity,"bound background getter returned unresolved URLs");
    external.ClearBackgroundImage();
    external.SetMaskImage("url(icon.png)");
    string maskIdentity=external.GetMaskImage();
    external.SetMaskImage(std::attribute<string>("url(icon.png)"));
    assert(external.GetMaskImage()==maskIdentity,"bound mask getter returned unresolved URLs");
    external.ClearMaskImage();
    array<VS::Gui::Widget> content = {external};
    RuntimeGroup group = RuntimeGroup::Create().Amount(7)[content];
    assert(group.GetChildCount() == 2 && group.GetChild(0) == group.part && group.part.GetGap() == 7,
        "internal structure or configured first synchronization");
    array<VS::Gui::Widget> empty = array<VS::Gui::Widget>();
    group.SetChildren(empty);
    assert(group.GetChildCount() == 1 && group.GetChild(0) == group.part, "external children replaced internal structure");
    gui = VS::Gui::Context("test", 100, 100);
    std::i18n::context localization=std::i18n::context();
    localization.set_language("en-US");
    gui.SetLocalization(localization);
    gui.SetRoot(root);
    posted = 0;
    gui.Post([]()->void { posted++; gui.Post([]()->void { posted++; }); });
    assert(posted == 0, "Post executed immediately");
    root.Subscribe("pointer-down", [](VS::Gui::Widget sender, VS::Gui::Event value)->void
    {
        assert(sender == root && value.Target == child, "capture identity");
        captures++;
    }, true);
    connection = child.Subscribe("pointer-down", [](VS::Gui::Widget sender, VS::Gui::Event value)->void
    {
        assert(sender == child && value.CurrentTarget == child, "sender identity");
        RuntimeProbe target = cast<RuntimeProbe>(value.Target);
        assert(target == child && target.GetId() == "child", "derived event target");
        saved = value;
        calls++;
        gui.CapturePointer(value, sender);
        gui.ReleasePointer(value);
        value.PreventDefault();
    });
    gui.Update(0);
    gui.Render();
    quietSource.bind([]()->double { quietReads++; return quietModel; });
    quiet.SetGap(quietSource);
    gui.Update(0);
    int readsBefore=quietReads;
    int syncBefore=custom.SyncCalls;
    quietModel=8;
    gui.Update(0);
    assert(quiet.GetGap()==8 && quietReads>readsBefore && custom.SyncCalls>syncBefore,
        "static fast path suppressed a late binding or custom synchronization");
    readsBefore=quietReads;
    gui.Update(0);
    assert(quietReads==readsBefore+1,"stable round evaluated the binding more than once");
    quiet.SetGap(3);
    readsBefore=quietReads;
    gui.Update(0);
    assert(quiet.GetGap()==3 && quietReads==readsBefore,"plain replacement retained the binding");
    quiet.ClearGap();
    quiet.SetGap(quietSource);
    quietModel=11;
    gui.Update(0);
    assert(quiet.GetGap()==11,"reattached dynamic binding was skipped");
    quiet.SetTransform(std::attribute<string>("translateX(1px)"));
    gui.Update(0);
    assert(quiet.GetTransform()=="translateX(1px)","high attribute bit was skipped");
    quiet.ClearTransform();
    quiet.ClearGap();
    gui.Update(0);
    LateBindingState state=LateBindingState();
    quiet.VsguiRetainBindings(state);
    gui.Update(0);
    assert(state.Calls>0,"late binding state did not resume native polling");
    quiet.SetGap(quietSource);
    gui.Update(0);
    assert(root.PaintCalls==1 && child.PaintCalls==1,"script OnPaint dispatch");
    localization.set_language("zh-CN");
    assert(gui.GetLocalizationSnapshot().configuration().language=="en-US","localization changed within a frame");
    auto animation=VS::Gui::AnimationOptions();
    animation.Duration=0.25;
    child.AnimateGap(20,animation);
    gui.Update(0);
    child.SetGap(4);
    gui.Update(0.125);
    assert(child.GetGap()==4 && child.GetPresentedGap()==10,"animation replaced base values or did not advance");
    assert(gui.GetLocalizationSnapshot().configuration().language=="zh-CN","next frame did not acquire the localization snapshot");
    gui.Update(0.25);
    assert(child.GetPresentedGap()==4,"animation completion did not restore current base values");
    gui.SetFocus(child);
    assert(gui.GetFocus() == child && posted == 2, "focus facade or stable Post batch");
    assert(root.InitializedCalls == 1 && child.InitializedCalls == 1 && root.MountedCalls == 1 && child.MountedCalls == 1,
        "lifecycle callbacks were duplicated or omitted");
}
void Deliver(VS::Gui::__NativeEvent value)
{
    assert(gui.ProcessEvent(value), "event was not handled");
    assert(calls == 1 && captures == 1 && saved.Button == 1, "event snapshot");
    connection.Disconnect();
    assert(!connection.IsConnected(), "disconnect token");
    gui.Update(1);
    assert(posted == 2, "nested Post was lost");
}
void CheckLater()
{
    assert(calls == 1 && captures == 2 && saved.Button == 1, "host event or stored input snapshot");
    gui.Close();
    assert(!gui.IsOpen(), "context close");
    assert(root.UnmountedCalls == 1 && child.UnmountedCalls == 1 && root.LayoutCalls == 1 && child.LayoutCalls == 1,
        "unmount or unchanged geometry notifications");
}
void CheckHostSynchronization()
{
    assert(quiet.GetGap()==17,"host update did not use the retained lifecycle callback");
}
void ChangeForHostSynchronization() { quietModel=17; }
weak<QuietWidget> transientView=weak<QuietWidget>();
VS::Gui::Widget replacementView=nullptr;
double replacementModel=19;
VS::Gui::__NativeWidget MakeTransientNative()
{
    QuietWidget value=QuietWidget::Create();
    value.SetHitTest(VS::Gui::HitTestMode::None);
    transientView=weak<QuietWidget>(value);
    root.AddChild(value);
    gui.Update(0.25);
    root.RemoveChild(value);
    return value.__GetNativeHandle();
}
void RewrapTransient(VS::Gui::__NativeWidget value)
{
    assert(transientView.expired(),"native lifecycle callback retained its script widget");
    array<VS::Gui::__NativeWidget> nativeChildren=array<VS::Gui::__NativeWidget>();
    for(int64 i=0;i<root.GetChildCount();i++)
    {
        nativeChildren.push(root.GetChild(i).__GetNativeHandle());
    }
    nativeChildren.push(value);
    root.__GetNativeHandle().SetChildren(nativeChildren);
    replacementView=root.GetChild(root.GetChildCount()-1);
    std::attribute<double> source=std::attribute<double>();
    source.bind([]()->double{return replacementModel;});
    replacementView.SetGap(source);
    gui.Update(0.25);
    replacementModel=27;
    gui.Update(0.25);
    assert(replacementView.GetGap()==27,"replacement script view did not resume dynamic synchronization");
}

void InvalidEventControl() { saved.PreventDefault(); }
void InvalidPaintCanvas() { root.SavedCanvas.get().FillRect(0,0,1,1,VS::Gui::Color(1,1,1,1)); }
)vs",
		    "public-runtime.vs");
		Require(Loaded.bSuccess, Loaded.Error.Message);
		Require(Session.Run(ERunMode::EvalOnce).bSuccess, "runtime initialization");
		auto Result = Session.CallFunction("Setup", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("QueueOwnedPost", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("QueueCapturedPosts", {});
		Require(Result.bSuccess, Result.Error.Message);
		Require(Native->Update(0.25, Error), Error);
		Result = Session.CallFunction("CheckPostExecuted", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("CheckCapturedPosts", {});
		Require(Result.bSuccess, Result.Error.Message);
		FValue RetainedNative;
		Result = Session.CallFunction("MakeTransientNative", {}, &RetainedNative);
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("RewrapTransient", {RetainedNative});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("ChangeForHostSynchronization", {});
		Require(Result.bSuccess, Result.Error.Message);
		auto HostChild = FWidget::Create();
		Require(HostChild->Set(EProperty::Gap, 23.0, Error) &&
		        HostChild->Set(EProperty::HitTest, EHitTestMode::None, Error) &&
		        Native->GetRoot()->AddChild(HostChild, Error),
		    Error);
		Require(Native->Update(0.25, Error), Error);
		bool bPresented = false;
		Require(Native->Render(bPresented, Error), Error);
		Result = Session.CallFunction("CheckHostSynchronization", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("InvalidPaintCanvas", {});
		Require(!Result.bSuccess, "retained paint canvas survived its callback");
		Platform::FEvent Input;
		Input.Type = Platform::EEventType::PointerDown;
		Input.WindowId = 1;
		Input.Button = 1;
		FValue Snapshot = MakeScriptInput(Session.GetBindingRegistry(), Input, Error);
		Require(Error.empty(), Error);
		Result = Session.CallFunction("Deliver", {Snapshot});
		Require(Result.bSuccess, Result.Error.Message);
		FEventResult Delivered;
		Require(Native->ProcessEvent(Input, Delivered, Error), Error);
		Result = Session.CallFunction("QueueOwnedPost", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("CheckLater", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("CheckPostDiscarded", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("InvalidEventControl", {});
		Require(!Result.bSuccess, "stored event retained routing control after delivery");
		Result = Session.CallFunction("CheckFocusBridge", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("CheckControlFractions", {});
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.CallFunction("Setup", {});
		Require(Result.bSuccess, Result.Error.Message);
		const auto Replaced = Session.LoadSource("int main(){return 0;}", "replacement.vs");
		Require(Replaced.bSuccess, Replaced.Error.Message);
		Require(!Native->ProcessEvent(Input, Delivered, Error) &&
		        Error.find("gui.stale_script_callback") != std::string::npos,
		    "an old GUI listener entered the replacement program");
		Require(!Native->Close(Error) && !Native->IsOpen() && !Native->GetRoot(),
		    "stale lifecycle callbacks prevented native cleanup");
	}

	void CheckSynchronization(const std::string& BackendPath = {})
	{
		struct FHost final : IEngineHost
		{
			//~ Begin IEngineHost Interface
			void OnPrintLine(std::string_view) override
			{
			}
			void OnDiagnosticLine(std::string_view Text) override
			{
				std::cerr << Text << '\n';
			}
			//~ End IEngineHost Interface
		} Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(false);
		FScriptServices Services;
		Services.CreateContext = [](const auto&, const auto& Viewport, auto& Error)
		{
			return FContext::Create(std::make_unique<FBackend>(), Viewport, Error);
		};
		Services.IsCurrentProgram = [&](const auto& Program, auto Generation)
		{
			const auto Current = Session.GetDebugProgram();
			return Current.Generation == Generation && Current.Owner.lock() == Program.lock();
		};
		std::string Error;
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.ExecutionBackend = BackendPath.empty() ? EExecutionBackend::Vm : EExecutionBackend::Jit;
		if (!BackendPath.empty())
		{
			Config.JitBackendFactory = LoadJitBackend(BackendPath, Error);
			Require(Config.JitBackendFactory != nullptr, Error);
		}
		Config.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		Require(Session.GetBindingRegistry().RegisterNative(
		            "NativeStringSnapshotCapacity", ENativeType::Int64, {ENativeType::Variant},
		            [](const FNativeCallContext& Call, FValue& Out, std::string& Failure)
		{
			FValue Visible;
			if (!Variant::Snapshot(Call.Args[0], Visible, Failure)) return false;
			const auto* Object = std::get_if<FObjectHandle>(&Visible);
			const auto* Module = Call.ScriptInvoker ? Call.ScriptInvoker->GetActiveModule() : nullptr;
			if (!Object || !*Object || !Module)
			{
				Failure = "missing native string snapshot test cache";
				return false;
			}
			const auto Type = std::find_if(Module->Types.begin(), Module->Types.end(),
			    [&](const auto& Value) { return Value.Name == (*Object)->GetTypeName(); });
			if (Type != Module->Types.end())
			{
				const auto Field = std::find_if(Type->Fields.begin(), Type->Fields.end(),
				    [](const auto& Value) { return Value.Name == "snapshotValue"; });
				if (Field != Type->Fields.end())
				{
					const auto Index = static_cast<std::size_t>(Field - Type->Fields.begin());
					const auto* Stored = Index < (*Object)->Fields.size()
					    ? UnwrapClosureReferenceCell(&(*Object)->Fields[Index]) : nullptr;
					if (const auto* Text = Stored ? std::get_if<std::string>(Stored) : nullptr)
					{
						Out = static_cast<std::int64_t>(Text->capacity());
						return true;
					}
				}
			}
			Failure = "missing native string snapshot test storage";
			return false;
		}, nullptr, Error), Error);
		Require(Session.GetBindingRegistry().RegisterNative(
		            "PatchRawControl", ENativeType::Void, {ENativeType::Function, ENativeType::Bool},
		            [](const FNativeCallContext& Call, FValue& Out, std::string& Failure)
		{
			const auto Anchor = std::get<FFunctionHandle>(Call.Args[0]);
			auto Object = Anchor ? Anchor->CapturedThis : FObjectHandle{};
			if (!Object && Anchor)
			{
				for (const auto& Capture : Anchor->Captures)
				{
					const auto* Value = UnwrapClosureReferenceCell(&Capture);
					if (const auto* Captured = Value ? std::get_if<FObjectHandle>(Value) : nullptr;
					    Captured && *Captured && (*Captured)->Kind == EScriptObjectKind::Class)
					{
						Object = *Captured;
						break;
					}
				}
			}
			const auto* Module = Call.ScriptInvoker ? Call.ScriptInvoker->GetActiveModule() : nullptr;
			if (!Object || !Module)
			{
				Failure = "missing raw-control test owner";
				return false;
			}
			const auto Type = std::find_if(Module->Types.begin(), Module->Types.end(),
			    [&](const auto& T)
			{
				return T.Name == Object->GetTypeName();
			});
			if (Type == Module->Types.end())
			{
				Failure = "missing raw-control type";
				return false;
			}
			const auto Field = std::find_if(Type->Fields.begin(), Type->Fields.end(),
			    [](const auto& F)
			{
				return F.Name == "Value";
			});
			if (Field == Type->Fields.end())
			{
				Failure = "missing raw Value field";
				return false;
			}
			const auto Attribute = std::get<FObjectHandle>(Object->Fields[Field - Type->Fields.begin()]);
			const auto OwnerVersion = Object->MutationVersion;
			std::vector<FValue> Arguments{Attribute, Call.Args[1]};
			FValue Result;
			if (!Call.ScriptInvoker->InvokeFunction("PatchRawAttribute", Arguments, Result, Failure)) return false;
			const auto Patched = std::get<FObjectHandle>(Result);
			Attribute->Fields = Patched->Fields;
			++Attribute->MutationVersion;
			Out = std::int32_t{0};
			Require(Object->MutationVersion == OwnerVersion, "raw patch changed its parent revision");
			return true;
		}, &Session, Error, {}, {"fn()->void", {}}),
		    Error);
		Require(Session.GetBindingRegistry().RegisterNative(
		            "PatchRawNavigationTitle", ENativeType::Void, {ENativeType::Function, ENativeType::String},
		            [](const FNativeCallContext& Call, FValue&, std::string& Failure)
		{
			const auto Anchor = std::get<FFunctionHandle>(Call.Args[0]);
			auto Object = Anchor ? Anchor->CapturedThis : FObjectHandle{};
			if (!Object && Anchor)
			{
				for (const auto& Capture : Anchor->Captures)
				{
					const auto* Value = UnwrapClosureReferenceCell(&Capture);
					if (const auto* Captured = Value ? std::get_if<FObjectHandle>(Value) : nullptr;
					    Captured && *Captured && (*Captured)->Kind == EScriptObjectKind::Class)
					{
						Object = *Captured;
						break;
					}
				}
			}
			const auto* Module = Call.ScriptInvoker ? Call.ScriptInvoker->GetActiveModule() : nullptr;
			if (!Object || !Module)
			{
				Failure = "missing raw navigation owner";
				return false;
			}
			const auto Type = std::find_if(Module->Types.begin(), Module->Types.end(),
			    [&](const auto& Value)
			{
				return Value.Name == Object->GetTypeName();
			});
			if (Type == Module->Types.end())
			{
				Failure = "missing raw navigation type";
				return false;
			}
			const auto Field = std::find_if(Type->Fields.begin(), Type->Fields.end(),
			    [](const auto& Value)
			{
				return Value.Name == "Title";
			});
			if (Field == Type->Fields.end())
			{
				Failure = "missing raw Title field";
				return false;
			}
			const auto* Value = UnwrapClosureReferenceCell(&Object->Fields[Field - Type->Fields.begin()]);
			const auto* Attribute = Value ? std::get_if<FObjectHandle>(Value) : nullptr;
			if (!Attribute || !*Attribute)
			{
				Failure = "missing raw Title attribute";
				return false;
			}
			const auto OwnerVersion = Object->MutationVersion;
			std::vector<FValue> Arguments{*Attribute, Call.Args[1]};
			FValue Result;
			if (!Call.ScriptInvoker->InvokeFunction("PatchRawNavigationAttribute", Arguments, Result, Failure)) return false;
			(*Attribute)->Fields = std::get<FObjectHandle>(Result)->Fields;
			++(*Attribute)->MutationVersion;
			Require(Object->MutationVersion == OwnerVersion, "raw Title patch changed its owner revision");
			return true;
		}, nullptr, Error, {}, {"fn()->void", {}}),
		    Error);
		Require(RegisterScriptBindings(Session.GetBindingRegistry(), std::move(Services), Error) &&
		        ConfigureGuiScriptEngine(Config, Error),
		    Error);
		Require(Session.GetBindingRegistry().RegisterNative(
		            "PatchNavigationFallbacks", FNativeTypeDesc::Scalar(ENativeType::Void),
		            {{FNativeTypeDesc::Host("VS.Gui.__NativeWidget")}, {FNativeTypeDesc::Scalar(ENativeType::Bool)}},
		            [](const FNativeCallContext& Call, FValue&, std::string& Failure)
		{
			const auto Widget = GetScriptWidget(Call.Args[0], Failure);
			if (!Widget) return false;
			if (std::get<bool>(Call.Args[1]))
			{
				return Widget->Set(EProperty::Enabled, false, Failure) &&
				    Widget->SetStyle(EProperty::Visibility, EVisibility::Hidden, Failure);
			}
			return Widget->Clear(EProperty::Enabled, Failure) &&
			    Widget->SetStyle(EProperty::Visibility, std::nullopt, Failure);
		}, nullptr, Error),
		    Error);
		Session.ApplyConfig(Config);
		const auto Loaded = Session.LoadSource(R"vs(
@GuiWidget export class Quiet : public VS::Gui::Widget {};
int GuiJitEntryProbe() { return 17; }
class CustomBase : public Quiet
{
public:
    int SyncCalls=0;
    void VsguiSynchronizeControl() override { SyncCalls++; }
};
@GuiWidget export class CustomDerived : public CustomBase {};
class UnannotatedSyncProbe : public VS::Gui::Widget
{
public:
    int Calls=0;
    void VsguiSynchronizeControl() override { Calls++; }
};
@GuiWidget export class NumericHookProbe : public VS::Gui::ProgressBar
{
public:
    int Changes=0;
protected:
    void OnValueChanged() override { Changes++; }
};
@GuiWidget export class PassiveProgressProbe : public VS::Gui::ProgressBar
{
public:
    int Changes=0;
    bool Fail=false;
protected:
    void OnValueChanged() override { Changes++; if(Fail) throw "passive control hook failed"; }
};
@GuiWidget export class PassiveProgressChild : public PassiveProgressProbe {};
class UnregisteredPassiveProgress : public PassiveProgressProbe {};
@GuiWidget export class PassiveScrollProbe : public VS::Gui::ScrollView
{
public:
    int LayoutCalls=0;
protected:
    void OnLayoutChanged() override { LayoutCalls++; }
};
@GuiWidget class ModulePrivateScrollProbe : public VS::Gui::ScrollView {};
class ModulePrivateScrollChild : public ModulePrivateScrollProbe {};
@GuiWidget class ModulePrivateWidgetProbe : public VS::Gui::Widget {};
class ModulePrivateWidgetChild : public ModulePrivateWidgetProbe
{
protected:
    void VsguiSynchronizeControl() override {}
};
@GuiWidget export class PassivePaintProbe : public VS::Gui::Widget
{
public:
    @GuiProperty std::attribute<double> Amount=std::attribute<double>(3);
protected:
    void OnPaint(VS::Gui::PaintContext canvas) override {}
};
@GuiWidget export class ExternalVirtualProbe : public VS::Gui::CollectionItem
{
public:
    bool VsguiExpandable() const override { return true; }
};
@GuiWidget export class ExternalQueryProbe : public VS::Gui::ProgressBar
{
public:
    bool IsDisposed() const { return false; }
};
@GuiWidget export class ConstructorPolicyProbe : public VS::Gui::ProgressBar
{
public:
    ConstructorPolicyProbe() {}
};
@GuiWidget export class AttributeDefaults : public VS::Gui::Widget
{
public:
    void ChangeDefault(double value) { Amount.set(value); }
    void BindDefault(fn()->double getter) { Amount.bind(getter); }
private:
    @GuiProperty std::attribute<double> Amount = std::attribute<double>(1);
};
int disposalChecks=0;
@GuiWidget export class DisposalQueryProbe : public Quiet
{
public:
    bool IsDisposed() const {disposalChecks++;return false;}
    void VsguiSynchronizeControl() override {double value=GetAmount();assert(value==1);}
private:
    @GuiProperty std::attribute<double> Amount=std::attribute<double>(1);
};
@GuiWidget export class ScalarSnapshotProbe : public Quiet
{
public:
    int Changes=0;
    bool Fail=false;
    void ChangeRaw(double value) { Amount.set(value); }
    fn(double)->void CaptureDefault()
    {
        return [&source = Amount](double value)->void { source.set(value); };
    }
private:
    @GuiProperty std::attribute<double> Amount=std::attribute<double>(1);
    @GuiProperty std::attribute<bool> AmountHas=std::attribute<bool>(false);
    void OnAmountChanged() { Changes++; if(Fail) {throw "snapshot hook failed";} }
};
@GuiWidget export class CapacityProbe0 : public Quiet {};
@GuiWidget export class CapacityProbe1 : public Quiet {};
@GuiWidget export class CapacityProbe2 : public Quiet {};
@GuiWidget export class CapacityProbe3 : public Quiet {};
@GuiWidget export class CapacityProbe4 : public Quiet {};
@GuiWidget export class CapacityProbe5 : public Quiet {};
@GuiWidget export class CapacityProbe6 : public Quiet {};
@GuiWidget export class CapacityProbe7 : public Quiet {};
@GuiWidget export class CapacityProbe8 : public Quiet {};
@GuiWidget export class CapacityProbe9 : public Quiet {};
@GuiWidget export class CapacityProbe10 : public Quiet {};
@GuiWidget export class CapacityProbe11 : public Quiet {};
@GuiWidget export class CapacityProbe12 : public Quiet {};
@GuiWidget export class CapacityProbe13 : public Quiet {};
@GuiWidget export class CapacityProbe14 : public Quiet {};
@GuiWidget export class CapacityProbe15 : public Quiet {};
@GuiWidget export class CapacityProbe16 : public Quiet {};
@GuiWidget export class CapacityProbe17 : public Quiet {};
@GuiWidget export class CapacityProbe18 : public Quiet {};
@GuiWidget export class CapacityProbe19 : public Quiet {};
@GuiWidget export class FailingSynchronization : public Quiet
{
public:
    bool Fail=false;
    void VsguiSynchronizeControl() override { if(Fail) { throw "synchronization failed"; } }
};
class CallbackReplacementProbe : public Quiet
{
public:
    int Calls=0;
    bool Replace=true;
    fn(int,int64,bool)->bool Callback;
    void Install()
    {
        weak<CallbackReplacementProbe> observer=weak<CallbackReplacementProbe>(this);
        Callback=[observer](int stage,int64 token,bool stable)->bool
        {
            CallbackReplacementProbe value=observer.lock();
            if(value!=nullptr && stage==4)
            {
                value.Calls++;
                if(value.Replace)
                {
                    value.Replace=false;
                    value.Reinstall();
                }
            }
            return false;
        };
        Reinstall();
    }
    void Reinstall()
    {
        this.__GetNativeHandle().SetBorrowedLifecycleCallback(Callback,
            fn()->void{VsguiSynchronizeControl();},false);
    }
};
array<int> synchronizationOrder = array<int>();
int synchronizationReleased = 0;
class SynchronizationCapture
{
public:
    int Keep() const { return 0; }
    ~SynchronizationCapture() { synchronizationReleased++; }
};
@GuiWidget export class SynchronizationSequenceProbe : public VS::Gui::Widget
{
public:
    int Index = 0;
    bool Rewrite = false;
    SynchronizationSequenceProbe Later = nullptr;
    void VsguiSynchronizeControl() override
    {
        synchronizationOrder.push(Index);
        if (Rewrite) { Rewrite = false; Later.InstallLegacy(); }
    }
    void InstallCaptured()
    {
        weak<SynchronizationSequenceProbe> owner = weak<SynchronizationSequenceProbe>(this);
        SynchronizationCapture capture = SynchronizationCapture();
        this.__GetNativeHandle().SetLifecycleCallback(
            [owner, capture](int stage, int64 token, bool stable)->bool
            {
                assert(capture.Keep() == 0);
                SynchronizationSequenceProbe self = owner.lock();
                if (self != nullptr && stage == 4) self.VsguiSynchronizeControl();
                return true;
            }, fn()->void { VsguiSynchronizeControl(); }, false);
    }
    void InstallLegacy()
    {
        weak<SynchronizationSequenceProbe> owner = weak<SynchronizationSequenceProbe>(this);
        fn(int,int64)->bool callback = [owner](int stage, int64 token)->bool
            {
                SynchronizationSequenceProbe self = owner.lock();
                if (self != nullptr && stage == 4) synchronizationOrder.push(100 + self.Index);
                return true;
            };
        this.__GetNativeHandle().SetLifecycleCallback(callback, fn()->void { VsguiSynchronizeControl(); });
    }
};
void CheckSynchronizationSequence()
{
    SynchronizationSequenceProbe first = SynchronizationSequenceProbe::Create(); first.Index = 0;
    SynchronizationSequenceProbe second = SynchronizationSequenceProbe::Create(); second.Index = 1;
    SynchronizationSequenceProbe third = SynchronizationSequenceProbe::Create(); third.Index = 2;
    first.Later = second; second.InstallCaptured();
    VS::Gui::Widget root = VS::Gui::Widget::Create()[{first, second, third}];
    VS::Gui::Context context = VS::Gui::Context("test", 100, 100);
    try
    {
        context.SetRoot(root); context.Update(0);
        synchronizationOrder = array<int>(); first.Rewrite = true;
        context.Update(1);
        assert(len(synchronizationOrder) == 3 && synchronizationOrder[0] == 0 && synchronizationOrder[1] == 101 && synchronizationOrder[2] == 2,
            "native synchronization ignored a later callback replacement");
        assert(synchronizationReleased == 1, "synchronization retained a replaced callback capture");
    }
    finally { context.Close(); root.Dispose(); }
}
double synchronizedModel = .2;
int synchronizedFailureIndex = -1;
array<int> synchronizedReads = array<int>();
void CheckNativeOutputFailure()
{
    VS::Gui::Widget root = VS::Gui::Widget::Create();
    array<VS::Gui::Widget> widgets = array<VS::Gui::Widget>();
    for (int index = 0; index < 16; index++)
    {
        synchronizedReads.push(0);
        std::attribute<double> value = std::attribute<double>();
        value.bind([index]()->double
        {
            synchronizedReads[index]++;
            if (synchronizedFailureIndex == index) throw "synchronization getter failure";
            return synchronizedModel;
        });
        VS::Gui::Widget widget = VS::Gui::Widget::Create().Opacity(value);
        widgets.push(widget); root.AddChild(widget);
    }
    VS::Gui::Context context = VS::Gui::Context("test", 100, 100);
    try
    {
        context.SetRoot(root); context.Update(0);
        for (int index = 0; index < 16; index++) synchronizedReads[index] = 0;
        synchronizedModel = .4; context.Update(1);
        for (int index = 0; index < 16; index++)
            assert(synchronizedReads[index] == 1 && widgets[index].GetOpacity() == .4,
                "unobserved native outputs repeated or skipped dynamic evaluation");
        synchronizedFailureIndex = 7; synchronizedModel = .6;
        bool failed = false;
        try { context.Update(2); } catch (...) { failed = true; }
        assert(failed && VS::Gui::PropertyEvaluation::Depth == 0 && VS::Gui::PropertyEvaluation::SafeDepth == 0 &&
            VS::Gui::Internal::NativeEvaluation::Widget == nullptr, "a failed synchronization retained its evaluation scope");
        for (int index = 0; index < 16; index++)
            assert(synchronizedReads[index] == (index <= 7 ? 2 : 1), "callbacks continued past a failed participant");
        synchronizedFailureIndex = -1; context.Update(2);
        for (int index = 0; index < 16; index++)
            assert(synchronizedReads[index] == (index <= 7 ? 3 : 2) && widgets[index].GetOpacity() == .6,
                "failed synchronization did not retry");
    }
    finally { context.Close(); root.Dispose(); }
}

int objectCacheReleases=0;
int scalarCacheReleases=0;
bool scalarCacheRead=false;
double scalarCacheObserved=-1;
string scalarCacheFailure="";
VS::Gui::Internal::NativeAttributeCache<CachePayload> objectCache=VS::Gui::Internal::NativeAttributeCache<CachePayload>();
VS::Gui::Internal::NativeValueAttributeCache<double> scalarCache=VS::Gui::Internal::NativeValueAttributeCache<double>();
class CachePayload
{
public:
    ~CachePayload()
    {
        objectCacheReleases++;
        assert(objectCache.IsSet(),"replacement classification was not published before releasing its payload");
        objectCache.Reset();
    }
};
class ScalarCachePayload
{
public:
    double Value=0.5;
    ~ScalarCachePayload()
    {
        scalarCacheReleases++;
        try { scalarCacheRead=scalarCache.Read(99,false,ref scalarCacheObserved); }
        catch(string failure) { scalarCacheFailure=failure; }
        scalarCache.Reset();
    }
};
CachePayload ReadCachePayload() { return nullptr; }
int retiredStringPayloadReleases = 0;
class RetiredStringPayload
{
public:
    ~RetiredStringPayload() { retiredStringPayloadReleases++; }
};
weak<RetiredStringPayload> retiredStringObserver = weak<RetiredStringPayload>();
void CheckNativeStringSnapshotRetirement()
{
    string large = "abcdefghijklmnop";
    for (int step = 0; step < 15; step++) large += large;
    std::attribute<string> source = std::attribute<string>();
    source.bind([large]()->string { return large; });
    auto cache = VS::Gui::Internal::NativeValueAttributeCache<string>();
    cache.Set(source);
    assert(cache.Read(101,true).get() == large && NativeStringSnapshotCapacity(variant(cache)) >= len(large),
        "large string snapshot fixture did not retain its text");
    cache.Reset();cache.Remember("short");
    assert(NativeStringSnapshotCapacity(variant(cache)) <= 64 && !cache.Read(102,false).has_value(),
        "an unset string cache retained its large inactive snapshot storage");
    std::attribute<string> replacement = std::attribute<string>();
    replacement.bind([]()->string { return "replacement"; });
    cache.Set(replacement);
    assert(cache.Read(103,false).get() == "short", "retiring a snapshot changed its pending committed value");
    assert(cache.Read(103,true).get() == "replacement", "retiring a snapshot suppressed the new source");

    VS::Gui::Widget raw = VS::Gui::Widget();
    raw.SetText(large);raw.ClearText();raw.SetText(replacement);
    assert(raw.GetText() == large, "retiring a snapshot changed raw pre-initialization committed text");
    raw.Initialize();
    assert(raw.GetText() == "replacement", "raw string source did not initialize after snapshot retirement");
    raw.Dispose();

    VS::Gui::Text text = VS::Gui::Text::Create();
    text.SetText(source);text.Synchronize();
    assert(text.GetText() == large, "large Text source did not synchronize");
    text.SetText("short");text.Synchronize();
    assert(text.GetText() == "short" && text.__GetNativeHandle().GetText() == "short",
        "string snapshot retirement changed literal Text replacement");
    text.Dispose();

    auto objectCache = VS::Gui::Internal::NativeValueAttributeCache<RetiredStringPayload>();
    RetiredStringPayload payload = RetiredStringPayload();
    retiredStringObserver = weak<RetiredStringPayload>(payload);
    objectCache.Set(std::attribute<RetiredStringPayload>(payload));objectCache.Read(104,true);
    payload = nullptr;
    int before = retiredStringPayloadReleases;
    objectCache.Reset();
    assert(retiredStringObserver.lock() != nullptr && retiredStringPayloadReleases == before,
        "string snapshot retirement changed generic payload destruction timing");
    objectCache = nullptr;
}
void CheckCompoundNativeCache()
{
    VS::Gui::Internal::NativeValueAttributeCache<VS::Gui::Length> cache=VS::Gui::Internal::NativeValueAttributeCache<VS::Gui::Length>();
    bool available=false;
    bool fail=false;
    int reads=0;
    int64 epoch=101;
    VS::Gui::Length model=VS::Gui::Length(31);
    std::attribute<VS::Gui::Length> source=std::attribute<VS::Gui::Length>();
    source.bind_optional([&available,&fail,&reads,&model]()->optional<VS::Gui::Length>
    {
        reads++;
        if(fail) throw "compound getter failed";
        return available?optional<VS::Gui::Length>(model):optional<VS::Gui::Length>();
    });
    cache.Set(source);
    VS::Gui::Length output=VS::Gui::Length(99);
    bool uninitialized=false;
    try { cache.Read(epoch,false,ref output); } catch(string failure) { uninitialized=true; }
    assert(uninitialized && reads==0,"unsafe compound query evaluated an uninitialized getter");
    assert(!cache.Read(epoch,true,ref output) && output==VS::Gui::Length(99) && reads==1,
        "an empty compound getter changed its reference output");
    cache.Remember(VS::Gui::Length(17));
    assert(!cache.Read(epoch,false).has_value() && !cache.Read(epoch,true,ref output) && reads==1,
        "fallback publication replaced an empty snapshot or evaluated it twice");
    epoch++;
    assert(cache.Read(epoch,false,ref output) && output==VS::Gui::Length(17) && reads==1,
        "unsafe compound query lost its committed fallback");
    available=true;
    assert(cache.Read(epoch,true,ref output) && output==model && reads==2,
        "compound polling failed to resume");
    cache.Remember(VS::Gui::Length(23));
    assert(cache.Read(epoch,true).get()==model && cache.Read(epoch,false,ref output) && output==model && reads==2,
        "compound reference and optional queries disagreed on a snapshot");
    epoch++;
    assert(cache.Read(epoch,false).get()==VS::Gui::Length(23) && reads==2,
        "the committed compound value was confused with its previous snapshot");
    fail=true;
    bool failed=false;
    try { cache.Read(epoch,true,ref output); } catch(string failure) { failed=true; }
    fail=false;
    assert(failed && cache.Read(epoch,true,ref output) && reads==4 && output==model,
        "failed compound evaluation was committed or could not be retried");
    cache.Reset();source.reset();
    output=VS::Gui::Length(77);
    assert(!cache.Read(epoch,true,ref output) && output==VS::Gui::Length(77),
        "reset compound cache changed its reference output");
}
VS::Gui::Internal::NativeValueAttributeCache<VS::Gui::Length> recursiveCompoundCache=VS::Gui::Internal::NativeValueAttributeCache<VS::Gui::Length>();
void PrepareCompoundCacheRecursion()
{
    std::attribute<VS::Gui::Length> source=std::attribute<VS::Gui::Length>();
    source.bind([]()->VS::Gui::Length
    {
        VS::Gui::Length output=VS::Gui::Length();
        recursiveCompoundCache.Read(101,true,ref output);
        return output;
    });
    recursiveCompoundCache.Set(source);
}
void RunCompoundCacheRecursion() { recursiveCompoundCache.Read(101,true); }
void RecoverCompoundCacheRecursion()
{
    recursiveCompoundCache.Set(std::attribute<VS::Gui::Length>(VS::Gui::Length(47)));
    VS::Gui::Length output=VS::Gui::Length();
    assert(recursiveCompoundCache.Read(102,true,ref output) && output==VS::Gui::Length(47),
        "recursive compound evaluation leaked its guard");
    recursiveCompoundCache.Reset();
}
void InstallScalarCachePayload()
{
    ScalarCachePayload owner=ScalarCachePayload();
    std::attribute<double> value=std::attribute<double>();
    value.bind([owner]()->double{return owner.Value;});
    scalarCache.Set(value);
}
void CheckNativeCachePublication()
{
    objectCache.Set(std::attribute<CachePayload>(CachePayload()));
    std::attribute<CachePayload> reader=std::attribute<CachePayload>();
    reader.bind(ReadCachePayload);
    objectCache.Set(reader);
    assert(objectCacheReleases==1 && !objectCache.IsSet() && !objectCache.Read(1,true).has_value(),
        "replacement overwrote a reentrant object-cache reset");
    InstallScalarCachePayload();
    assert(scalarCacheReleases==0,"a live cache did not retain its getter capture");
    scalarCache.Set(std::attribute<double>(0.25));
    assert(scalarCacheFailure=="","reentrant cache read failed: "+scalarCacheFailure);
    assert(scalarCacheRead && scalarCacheObserved==0.25,
        "reentrant cache value: "+to_string(scalarCacheObserved));
    double value=0;
    assert(scalarCacheReleases==1 && !scalarCache.IsSet() && !scalarCache.Read(1,true,ref value),
        "replacement overwrote a reentrant scalar-cache reset");
}
VS::Gui::Widget root=nullptr;
VS::Gui::Context gui=nullptr;
weak<Quiet> transient=weak<Quiet>();
double model=2;
int reads=0;
bool available=false;
bool failRaw=false;
bool patchDuringRead=false;
VS::Gui::Context recursiveGui=nullptr;
VS::Gui::ProgressBar recursiveWidget=nullptr;
bool recursiveControl=false;
void PrepareRecursiveControl()
{
    recursiveWidget=VS::Gui::ProgressBar::Create().Maximum(100);
    std::attribute<double> guarded=std::attribute<double>();
    guarded.bind([]()->double
    {
        if(recursiveControl) { recursiveWidget.VsguiSynchronizeControl(); }
        return 12;
    });
    recursiveWidget.SetValue(guarded);
    recursiveGui=VS::Gui::Context("test",100,100);
    recursiveGui.SetRoot(recursiveWidget);
    recursiveGui.Update(0);recursiveGui.Update(0);
    recursiveWidget.__GetNativeHandle().ConfigureControl(false,99,0,100,0,false,false,"","",0,0,0.1);
    recursiveControl=true;
}
void RunRecursiveControl() { recursiveGui.Update(0); }
void RecoverRecursiveControl()
{
    assert(VS::Gui::PropertyEvaluation::Depth==0 && VS::Gui::PropertyEvaluation::SafeDepth==0 &&
        VS::Gui::Internal::NativeEvaluation::Widget==nullptr,
        "failed recursive control synchronization leaked its scope");
    recursiveControl=false;
    recursiveGui.Update(0);
    assert(recursiveWidget.GetActualValue()==12,"recursive control synchronization did not recover");
    recursiveWidget.ClearValue();
    recursiveGui.Close();
    recursiveWidget=nullptr;recursiveGui=nullptr;
}
VS::Gui::Context failingNativeGui=nullptr;
VS::Gui::Widget failingNativeWidget=nullptr;
bool failNativeGetter=false;
bool nativeGetterThrew=false;
void PrepareNativeGetterFailure()
{
    failingNativeWidget=VS::Gui::Widget::Create();
    std::attribute<double> guarded=std::attribute<double>();
    guarded.bind([]()->double
    {
        if(failNativeGetter) { nativeGetterThrew=true;throw "native snapshot getter failed"; }
        return 0.5;
    });
    failingNativeWidget.SetOpacity(guarded);
    failingNativeGui=VS::Gui::Context("test",100,100);
    failingNativeGui.SetRoot(failingNativeWidget);
    failingNativeGui.Update(0);failingNativeGui.Update(0);
    failNativeGetter=true;
}
void RunNativeGetterFailure() { failingNativeGui.Update(0); }
void RecoverNativeGetterFailure()
{
    assert(nativeGetterThrew,"the intended native getter exception was not raised");
    assert(VS::Gui::PropertyEvaluation::Depth==0 && VS::Gui::PropertyEvaluation::SafeDepth==0 &&
        VS::Gui::Internal::NativeEvaluation::Widget==nullptr,"failed native getter leaked its scope");
    failNativeGetter=false;
    failingNativeGui.Update(0);
    assert(failingNativeWidget.GetOpacity()==0.5,"native getter did not recover after an exception");
    failingNativeWidget.ClearOpacity();
    failingNativeGui.Close();
    failingNativeWidget=nullptr;failingNativeGui=nullptr;
}
export std::attribute<double> PatchRawAttribute(std::attribute<double> value,bool bind)
{
    if(bind){value.bind([]()->double{reads++;if(failRaw){throw "raw getter failed";}return model;});}
    else{value.set(55);}
    return value;
}
VS::Gui::__NativeWidget MakeTransient()
{
    Quiet value=Quiet::Create();
    transient=weak<Quiet>(value);
    root.AddChild(value);
    gui.Update(0);
    root.RemoveChild(value);
    return value.__GetNativeHandle();
}
void CheckPassiveControls()
{
    PassiveScrollProbe scroll=PassiveScrollProbe::Create();
    assert(scroll.VsguiAllowsSourceSnapshot() && scroll.VsguiCanSuspendSynchronization(),
        "a passive ScrollView subclass did not inherit suspension readiness");
    int layouts=scroll.LayoutCalls;
    scroll.VsguiLifecycle(3);
    assert(scroll.LayoutCalls==layouts+1,"suspension suppressed a derived layout hook");
    ModulePrivateWidgetProbe privateWidget=ModulePrivateWidgetProbe::Create();
    ModulePrivateWidgetChild privateWidgetChild=ModulePrivateWidgetChild();
    assert(!privateWidget.VsguiHasCustomSynchronization(),
        "module-qualified exact checks lost the registered widget identity");
    assert(privateWidgetChild.VsguiHasCustomSynchronization(),
        "module-qualified exact checks skipped a derived synchronization override");
    privateWidget.Dispose(); privateWidgetChild.Dispose();
    ModulePrivateScrollProbe modulePrivate=ModulePrivateScrollProbe::Create();
    assert(modulePrivate.VsguiAllowsSourceSnapshot() && modulePrivate.VsguiCanSuspendSynchronization(),
        "module-qualified private controls did not inherit suspension readiness");
    ModulePrivateScrollChild privateChild=ModulePrivateScrollChild();
    privateChild.Initialize(); privateChild.Synchronize();
    assert(!privateChild.VsguiAllowsSourceSnapshot() && !privateChild.VsguiCanSuspendSynchronization(),
        "an unregistered private subclass inherited an exact registered suspension hint");
    int privateReads=0;
    std::attribute<double> privateSource=std::attribute<double>();
    privateSource.bind([&privateReads]()->double { privateReads++; return 4; });
    modulePrivate.SetScrollY(privateSource); modulePrivate.Synchronize();
    assert(privateReads>0 && !modulePrivate.VsguiCanSuspendSynchronization(),
        "a module-private control froze an ordinary dynamic attribute");
    modulePrivate.SetScrollY(0); modulePrivate.Synchronize();
    assert(modulePrivate.VsguiCanSuspendSynchronization(), "a module-private control did not settle after unbinding");
    modulePrivate.Dispose(); privateChild.Dispose();
    PassivePaintProbe paint=PassivePaintProbe::Create().Amount(3);
    assert(paint.VsguiAllowsSourceSnapshot() && paint.VsguiCanSuspendSynchronization() && paint.VsguiHasPaintHook(),
        "a passive annotated paint control did not preserve its independent paint hook");
    ExternalVirtualProbe external=ExternalVirtualProbe::Create();
    ExternalQueryProbe query=ExternalQueryProbe::Create();
    ConstructorPolicyProbe constructor=ConstructorPolicyProbe::Create();
    assert(!external.VsguiAllowsSourceSnapshot() && !external.VsguiCanSuspendSynchronization() &&
        !query.VsguiAllowsSourceSnapshot() && !query.VsguiCanSuspendSynchronization() &&
        !constructor.VsguiAllowsSourceSnapshot() && !constructor.VsguiCanSuspendSynchronization(),
        "an arbitrary virtual/query override or explicit constructor was marked passive");
    UnregisteredPassiveProgress unregistered=UnregisteredPassiveProgress();
    unregistered.Initialize();
    assert(!unregistered.VsguiAllowsSourceSnapshot() && !unregistered.VsguiCanSuspendSynchronization(),
        "an unregistered subclass inherited an exact registered suspension hint");

    PassiveProgressChild value=PassiveProgressChild::Create().Maximum(100).Value(10);
    root.AddChild(value);gui.Update(0);gui.Update(0);
    assert(value.VsguiAllowsSourceSnapshot() && value.VsguiCanSuspendSynchronization(),
        "multi-level passive control did not settle");
    value.ClearValue();gui.Update(0);gui.Update(0);
    PatchRawControl([value]()->void{value.GetValue();},false);gui.Update(0);
    assert(value.GetActualValue()==55 && value.VsguiCanSuspendSynchronization(),
        "raw default mutation did not wake a passive derived control");
    PatchRawControl([value]()->void{value.GetValue();},true);
    model=62;gui.Update(0);
    int before=reads;model=63;gui.Update(0);
    assert(value.GetActualValue()==63 && reads>before && !value.VsguiCanSuspendSynchronization(),
        "a raw dynamic binding was frozen by derived suspension");
    failRaw=true;bool failed=false;
    try { gui.Update(0); } catch(...) { failed=true; }
    assert(failed,"passive-derived dynamic getter did not raise its expected failure");
    failRaw=false;model=64;gui.Update(0);
    assert(value.GetActualValue()==64,"passive-derived dynamic getter did not recover");
    PatchRawControl([value]()->void{value.GetValue();},false);gui.Update(0);
    bool available=false;
    int optionalReads=0;
    std::attribute<double> optionalSource=std::attribute<double>();
    optionalSource.bind_optional([&available,&optionalReads]()->optional<double>
    {optionalReads++;return available?optional<double>(73):optional<double>();});
    value.SetValue(optionalSource);gui.Update(0);
    before=optionalReads;gui.Update(0);
    assert(value.GetActualValue()==55 && optionalReads>before && !value.VsguiCanSuspendSynchronization(),
        "an empty ordinary optional binding stopped polling its default");
    available=true;gui.Update(0);
    assert(value.GetActualValue()==73,"ordinary optional binding did not resume on a passive-derived control");
    value.SetValue(74);gui.Update(0);gui.Update(0);
    before=optionalReads;gui.Update(0);
    assert(value.GetActualValue()==74 && value.VsguiCanSuspendSynchronization() && optionalReads==before,
        "a constant replacement retained an ordinary optional binding");
    value.Fail=true;value.SetValue(75);failed=false;
    try { gui.Update(0); } catch(...) { failed=true; }
    assert(failed,"passive-derived property hook did not raise its expected failure");
    value.Fail=false;gui.Update(0);gui.Update(0);
    assert(value.GetActualValue()==75 && value.VsguiCanSuspendSynchronization(),
        "a passive-derived change hook did not retry and settle after failure");
    root.RemoveChild(value);gui.Update(0);
}
void main()
{
    CheckNativeCachePublication();
    CheckCompoundNativeCache();
    CheckNativeStringSnapshotRetirement();
    assert(retiredStringObserver.lock() == nullptr, "released generic snapshot retained its payload");
    CapacityProbe19 capacity=CapacityProbe19::Create().Gap(3);
    assert(capacity.GetGap()==3,"batched Builder methods lost their concrete type");
    Quiet quiet=Quiet::Create();
    CustomDerived custom=CustomDerived::Create();
    assert(!quiet.VsguiHasCustomSynchronization() && custom.VsguiHasCustomSynchronization(),
        "registered subclass lost inherited custom synchronization");
    root=VS::Gui::Widget::Create()[{quiet,custom}];
    gui=VS::Gui::Context("test",100,100);
    gui.SetRoot(root);
    gui.Update(0);
    AttributeDefaults defaults=AttributeDefaults::Create();
    root.AddChild(defaults);
    gui.Update(0);
    assert(defaults.GetAmount()==1,"constant default was not initialized");
    defaults.ChangeDefault(4);
    gui.Update(0);
    assert(defaults.GetAmount()==4,"default configuration change was cached across frames");
    defaults.BindDefault([]()->double{return model;});
    gui.Update(0);
    model=5;
    gui.Update(0);
    assert(defaults.GetAmount()==5,"default dynamic binding was cached across frames");
    defaults.ChangeDefault(6);
    gui.Update(0);
    assert(defaults.GetAmount()==6,"bound default did not return to a constant");
    defaults.SetAmount(8);
    defaults.ChangeDefault(9);
    gui.Update(0);
    assert(defaults.GetAmount()==8,"default cache replaced a local override");
    defaults.ClearAmount();
    gui.Update(0);
    assert(defaults.GetAmount()==9,"cleared local override retained its cached value");
    root.RemoveChild(defaults);
    DisposalQueryProbe disposal=DisposalQueryProbe::Create();
    root.AddChild(disposal);
    int previousDisposalChecks=disposalChecks;
    gui.Update(0);
    assert(disposalChecks>previousDisposalChecks,"custom disposal queries were bypassed in native synchronization");
    root.RemoveChild(disposal);
    ScalarSnapshotProbe snapshot=ScalarSnapshotProbe::Create();
    root.AddChild(snapshot);
    gui.Update(0);
    assert(snapshot.GetAmount()==1 && !snapshot.GetAmountHas(),"primitive property slots collided");
    int changes=snapshot.Changes;
    snapshot.SetAmount(2);
    snapshot.Fail=true;
    bool hookFailed=false;
    try {gui.Update(0);} catch (...) {hookFailed=true;}
    assert(hookFailed && VS::Gui::PropertyEvaluation::Depth==0 &&
        VS::Gui::PropertyEvaluation::SafeDepth==0,"failed property evaluation leaked its guard");
    snapshot.Fail=false;
    gui.Update(0);
    assert(snapshot.Changes==changes+2 && snapshot.GetAmount()==2,
        "a failed change notification was hidden by the snapshot cache");
    snapshot.ChangeRaw(3);
    gui.Update(0);
    assert(snapshot.GetAmount()==2,"raw default replaced a constant local value");
    snapshot.ClearAmount();
    gui.Update(0);
    assert(snapshot.GetAmount()==3,"clearing a local value did not re-read its default");
    fn(double)->void changeDefault = snapshot.CaptureDefault();
    gui.Update(0);gui.Update(0);
    changeDefault(29);gui.Update(0);
    assert(snapshot.GetAmount()==29,"a captured default field did not wake synchronization");
    changeDefault(3);gui.Update(0);
    std::attribute<double> maybe=std::attribute<double>();
    maybe.bind_optional([]()->optional<double>
    {return available?optional<double>(model):optional<double>();});
    snapshot.SetAmount(maybe);
    available=false;
    gui.Update(0);
    assert(snapshot.GetAmount()==3,"optional custom-property fallback was lost");
    available=true;model=31;
    gui.Update(0);
    assert(snapshot.GetAmount()==31,"optional custom-property binding did not resume");
    available=false;
    gui.Update(0);
    assert(snapshot.GetAmount()==3,"optional custom-property binding did not return to fallback");
    root.RemoveChild(snapshot);
    VS::Gui::ProgressBar progress=VS::Gui::ProgressBar::Create().Maximum(100).Value(10);
    root.AddChild(progress);
    gui.Update(0);
    progress.__GetNativeHandle().ConfigureControl(false,60,0,100,0,false,false,"","",0,0,0.1);
    gui.Update(0);
    assert(progress.GetActualValue()==10,"native changes were hidden by the control configuration cache");
    progress.SetValue(21);
    gui.Update(0);
    assert(progress.GetActualValue()==21,"a suspended control did not resume after a setter");
    progress.ClearValue();gui.Update(0);gui.Update(0);
    PatchRawControl([progress]()->void{progress.GetValue();},false);
    gui.Update(0);
    assert(progress.GetActualValue()==55,"a raw default change did not wake suspended synchronization");
    PatchRawControl([&anchor = progress]()->void{anchor.GetValue();},false);
    PatchRawControl([progress]() mutable ->void{progress.GetValue();},true);
    model=63;gui.Update(0);
    int resumedReads=reads;model=64;gui.Update(0);
    assert(progress.GetActualValue()==64 && reads>resumedReads,"a rebound raw default stopped polling");
    failRaw=true;bool rawFailed=false;
    try{gui.Update(0);}catch(...){rawFailed=true;}
    assert(rawFailed && !VS::Gui::PropertyEvaluation::NativeSourcesStable &&
        VS::Gui::Internal::NativeEvaluation::Widget==nullptr,"failed source read leaked its native scope");
    failRaw=false;model=67;gui.Update(0);
    assert(progress.GetActualValue()==67,"dynamic source did not recover after failure");
    root.RemoveChild(progress);

    VS::Gui::ProgressBar duringRead=VS::Gui::ProgressBar::Create().Maximum(100);
    std::attribute<double> maximum=std::attribute<double>();
    maximum.bind([duringRead]()->double
    {
        if(patchDuringRead)
        {
            patchDuringRead=false;
            PatchRawControl([duringRead]()->void{duringRead.GetValue();},false);
        }
        return 100;
    });
    duringRead.SetMaximum(maximum);
    root.AddChild(duringRead);
    gui.Update(0);gui.Update(0);
    patchDuringRead=true;
    gui.Update(0);gui.Update(0);
    assert(duringRead.GetActualValue()==55,"a source changed during synchronization was lost");
    root.RemoveChild(duringRead);

    VS::Gui::ProgressBar numeric=VS::Gui::ProgressBar::Create().Maximum(1000000);
    double numericModel=10;
    int numericReads=0;
    std::attribute<double> numericSource=std::attribute<double>();
    numericSource.bind([&numericModel,&numericReads]()->double{numericReads++;return numericModel;});
    numeric.SetValue(numericSource);
    root.AddChild(numeric);
    gui.Update(0);gui.Update(0);
    for(int frame=0;frame<3;frame++)
    {
        numericModel++;
        int previousReads=numericReads;
        gui.Update(0);
        assert(numericReads>previousReads && numeric.GetActualValue()==numericModel,
            "a changing numeric binding was not evaluated");
    }
    int settledReads=numericReads;
    gui.Update(0);
    assert(numericReads==settledReads+1,"an unchanged numeric getter was not sampled once per round");
    std::attribute<double> optionalNumeric=std::attribute<double>();
    bool numericAvailable=false;
    optionalNumeric.bind_optional([&numericAvailable,&numericModel,&numericReads]()->optional<double>
    {
        numericReads++;
        return numericAvailable?optional<double>(numericModel):optional<double>();
    });
    numeric.SetValue(optionalNumeric);
    gui.Update(0);gui.Update(0);
    settledReads=numericReads;
    gui.Update(0);
    assert(numeric.GetActualValue()==0 && numericReads==settledReads+1,
        "an empty numeric binding stopped polling its fallback");
    numericAvailable=true;
    gui.Update(0);
    assert(numeric.GetActualValue()==numericModel,"an optional numeric binding did not resume");
    numericAvailable=false;
    gui.Update(0);
    assert(numeric.GetActualValue()==0,"an optional numeric binding did not return to its fallback");
    numeric.SetValue(numericSource);
    gui.Update(0);
    VS::Gui::ProgressBar numericDependent=VS::Gui::ProgressBar::Create().Maximum(1000000);
    std::attribute<double> actualDependency=std::attribute<double>();
    actualDependency.bind([numeric]()->double{return numeric.GetActualValue()+1;});
    numericDependent.SetValue(actualDependency);
    root.RemoveChild(numeric);
    root.AddChild(numericDependent);root.AddChild(numeric);
    gui.Update(0);
    numericModel++;
    gui.Update(0);
    assert(numericDependent.GetActualValue()==numericModel+1,
        "a forward dependency on an actual control value did not stabilize");
    root.RemoveChild(numericDependent);
    root.RemoveChild(numeric);
    gui.Update(0);

    NumericHookProbe hooked=NumericHookProbe::Create().Maximum(100);
    std::attribute<double> hookSource=std::attribute<double>();
    hookSource.bind([&numericModel]()->double{return numericModel;});
    hooked.SetValue(hookSource);
    root.AddChild(hooked);
    gui.Update(0);gui.Update(0);
    int previousHookChanges=hooked.Changes;
    numericModel++;
    gui.Update(0);
    assert(hooked.Changes==previousHookChanges+1 && hooked.GetActualValue()==numericModel,
        "a derived numeric control lost its property change hook");
    root.RemoveChild(hooked);
    gui.Update(0);

    VS::Gui::Widget grouped=VS::Gui::Widget::Create();
    double groupedModel=0.25;
    int groupedReads=0;
    std::attribute<double> groupedNumber=std::attribute<double>();
    groupedNumber.bind([&groupedModel,&groupedReads]()->double{groupedReads++;return groupedModel;});
    std::attribute<VS::Gui::Color> groupedColor=std::attribute<VS::Gui::Color>();
    groupedColor.bind([&groupedModel,&groupedReads]()->VS::Gui::Color
    {groupedReads++;return VS::Gui::Color(groupedModel,0,0,1);});
    grouped.SetGap(groupedNumber);grouped.SetOpacity(groupedNumber);grouped.SetFontSize(groupedNumber);
    grouped.SetLetterSpacing(groupedNumber);grouped.SetCornerRadius(groupedNumber);
    grouped.SetBorderWidth(groupedNumber);grouped.SetLineHeight(groupedNumber);grouped.SetTextColor(groupedColor);
    grouped.SetFilter(std::attribute<string>("none"));
    grouped.SetTransform(std::attribute<string>("translateX(1px)"));
    root.AddChild(grouped);
    gui.Update(0);gui.Update(0);
    groupedModel=0.5;
    gui.Update(0);
    assert(grouped.GetGap()==groupedModel && grouped.GetOpacity()==groupedModel &&
        grouped.GetFontSize()==groupedModel && grouped.GetLetterSpacing()==groupedModel &&
        grouped.GetCornerRadius()==groupedModel && grouped.GetBorderWidth()==groupedModel &&
        grouped.GetLineHeight()==groupedModel && grouped.GetTextColor()==VS::Gui::Color(groupedModel,0,0,1) &&
        grouped.GetFilter()=="none" && grouped.GetTransform()=="translateX(1px)",
        "native attribute dispatch lost a property group");
    int previousGroupedReads=groupedReads;
    gui.Update(0);
    assert(groupedReads==previousGroupedReads+8,"grouped native bindings were skipped or repeated");
    root.RemoveChild(grouped);
    gui.Update(0);

    bool scalarAvailable=true;
    bool scalarEnabled=false;
    string scalarText="";
    int scalarReads=0;
    std::attribute<bool> enabledSource=std::attribute<bool>();
    enabledSource.bind_optional([&scalarAvailable,&scalarEnabled,&scalarReads]()->optional<bool>
    {
        scalarReads++;
        return scalarAvailable?optional<bool>(scalarEnabled):optional<bool>();
    });
    std::attribute<string> textSource=std::attribute<string>();
    textSource.bind_optional([&scalarAvailable,&scalarText,&scalarReads]()->optional<string>
    {
        scalarReads++;
        return scalarAvailable?optional<string>(scalarText):optional<string>();
    });
    grouped.SetEnabled(enabledSource);grouped.SetText(textSource);
    root.AddChild(grouped);
    gui.Update(0);gui.Update(0);
    assert(!grouped.GetEnabled() && grouped.GetText()=="",
        "false and empty native attribute values were treated as missing");
    scalarEnabled=true;scalarText="updated";
    gui.Update(0);
    assert(grouped.GetEnabled() && grouped.GetText()=="updated",
        "native scalar snapshots did not observe changed values");
    scalarAvailable=false;
    gui.Update(0);gui.Update(0);
    int previousScalarReads=scalarReads;
    gui.Update(0);
    assert(grouped.GetEnabled() && grouped.GetText()=="" && scalarReads==previousScalarReads+2,
        "empty native scalar bindings did not keep polling their fallbacks");
    scalarAvailable=true;scalarEnabled=false;scalarText="resumed";
    gui.Update(0);
    assert(!grouped.GetEnabled() && grouped.GetText()=="resumed",
        "native scalar bindings did not resume after an empty result");
    grouped.SetEnabled(true);grouped.SetText("fixed");
    previousScalarReads=scalarReads;
    gui.Update(0);
    assert(grouped.GetEnabled() && grouped.GetText()=="fixed" && scalarReads==previousScalarReads,
        "replacing native scalar bindings did not stop their getters");
    root.RemoveChild(grouped);
    gui.Update(0);

    VS::Gui::Widget fixedNative=VS::Gui::Widget::Create();
    UnannotatedSyncProbe unannotated=UnannotatedSyncProbe();
    assert(!fixedNative.VsguiHasCustomSynchronization() && unannotated.VsguiHasCustomSynchronization(),
        "the base widget synchronization hint was inherited by a custom subclass");
    root.AddChild(unannotated);
    gui.Update(0);gui.Update(0);
    int previousUnannotatedCalls=unannotated.Calls;
    gui.Update(0);
    assert(unannotated.Calls>previousUnannotatedCalls,"an unannotated custom widget stopped synchronizing");
    root.RemoveChild(unannotated);
    fixedNative.SetGap(std::attribute<double>(3));
    fixedNative.SetText(std::attribute<string>("fixed native"));
    root.AddChild(fixedNative);
    gui.Update(0);gui.Update(0);
    fixedNative.__GetNativeHandle().SetGap(9);
    fixedNative.__GetNativeHandle().SetText("host changed");
    gui.Update(0);
    assert(fixedNative.__GetNativeHandle().GetGap()==3 && fixedNative.__GetNativeHandle().GetText()=="fixed native",
        "suspended fixed native attributes did not wake after a native mutation");
    fixedNative.SetGap(std::attribute<double>(7));
    gui.Update(0);
    assert(fixedNative.__GetNativeHandle().GetGap()==7,"replacing a fixed native attribute did not wake synchronization");
    root.RemoveChild(fixedNative);
    gui.Update(0);
    root.AddChild(fixedNative);
    gui.Update(0);
    assert(fixedNative.GetGap()==7 && fixedNative.GetText()=="fixed native",
        "remounting fixed native attributes lost their configuration");
    fixedNative.SetOpacity(groupedNumber);
    gui.Update(0);
    assert(fixedNative.GetOpacity()==groupedModel,"a fixed widget did not resume dynamic polling");
    fixedNative.SetOpacity(std::attribute<double>(0.75));
    previousGroupedReads=groupedReads;
    gui.Update(0);gui.Update(0);
    assert(fixedNative.GetOpacity()==0.75 && groupedReads==previousGroupedReads,
        "a fixed configuration did not stop the replaced dynamic getter");
    fixedNative.__GetNativeHandle().SetOpacity(0.25);
    gui.Update(0);
    assert(fixedNative.__GetNativeHandle().GetOpacity()==0.75,
        "a fixed configuration lost authority after replacing a dynamic getter");
    fixedNative.ClearOpacity();
    fixedNative.ClearGap();fixedNative.ClearText();
    gui.Update(0);
    assert(fixedNative.GetGap()==0 && fixedNative.GetText()=="","clearing fixed native attributes left cached values");
    root.RemoveChild(fixedNative);
    gui.Update(0);

    CallbackReplacementProbe callbackReplacement=CallbackReplacementProbe();
    callbackReplacement.Install();
    root.AddChild(callbackReplacement);
    gui.Update(0);gui.Update(0);gui.Update(0);
    assert(callbackReplacement.Calls==2,"a callback result overwrote its newer registration");
    root.RemoveChild(callbackReplacement);

    model=2;
    std::attribute<double> source=std::attribute<double>();
    source.bind([]()->double{reads++;return model;});
    quiet.SetGap(source);
    gui.Update(0);
    int before=reads;
    gui.Update(0);
    assert(reads==before+1,"stable dynamic binding was not sampled once");
    model=9;
    gui.Update(0);
    assert(quiet.GetGap()==9,"model changes without GUI setters were lost");
    quiet.SetGap(11);
    before=reads;
    gui.Update(0);
    assert(reads==before && quiet.GetGap()==11,"plain value did not replace binding");
    int syncBefore=custom.SyncCalls;
    gui.Update(0);
    assert(custom.SyncCalls>syncBefore,"inherited custom synchronization stopped polling");
    std::attribute<double> optionalSource=std::attribute<double>();
    optionalSource.bind_optional([]()->optional<double>
    {
        return available ? optional<double>(model) : optional<double>();
    });
    quiet.SetGap(optionalSource);
    gui.Update(0);
    assert(quiet.GetGap()==0,"optional empty binding did not use the fallback");
    available=true;
    model=13;
    gui.Update(0);
    assert(quiet.GetGap()==13,"optional binding did not resume without a GUI setter");
    root.RemoveChild(quiet);
    gui.Update(0);
    model=15;
    root.AddChild(quiet);
    gui.Update(0);
    assert(quiet.GetGap()==15,"remounted dynamic binding stayed stale");
    available=false;
    gui.Update(0);
    assert(quiet.GetGap()==0,"optional binding did not return to its fallback");
    quiet.ClearGap();
    Quiet dependent=Quiet::Create();
    Quiet provider=Quiet::Create();
    std::attribute<double> dependency=std::attribute<double>();
    dependency.bind([provider]()->double{return provider.GetGap()+1;});
    dependent.SetGap(dependency);
    provider.SetGap(source);
    array<VS::Gui::Widget> dependencyChildren={quiet,custom,dependent,provider};
    root.SetChildren(dependencyChildren);
    gui.Update(0);
    model=16;
    gui.Update(0);
    assert(dependent.GetGap()==17 && provider.GetGap()==16,"forward dependency used an old snapshot");
    before=reads;
    gui.Update(0);
    assert(reads==before+1,"forward dependency evaluated its provider more than once");
    array<VS::Gui::Widget> initialChildren={quiet,custom};
    root.SetChildren(initialChildren);
    auto native=MakeTransient();
    assert(transient.expired(),"lifecycle callback retained the script owner");
    root.__GetNativeHandle().SetChildren({native});
    VS::Gui::Widget replacement=root.GetChild(0);
    replacement.SetGap(source);
    gui.Update(0);
    model=17;
    gui.Update(0);
    assert(replacement.GetGap()==17,"replacement view did not resume polling");
    replacement.ClearGap();
    before=reads;
    gui.Update(0);
    assert(reads==before,"cleared attribute continued polling");
    assert(VS::Gui::Internal::NativeEvaluation::Widget==nullptr,"native validation scope leaked after synchronization");
    FailingSynchronization failure=FailingSynchronization::Create();
    root.AddChild(failure);
    failure.Fail=true;
    bool caught=false;
    try { gui.Update(0); } catch (...) { caught=true; }
    assert(caught && VS::Gui::Internal::NativeEvaluation::Widget==nullptr,
        "failed synchronization retained its native validation scope");
    root.RemoveChild(failure);
    gui.Update(0);
    CheckPassiveControls();
    gui.Close();
    assert(!gui.IsOpen(),"JIT lifecycle cleanup failed");
}
class NavigationProjectionModel
{
public:
    string Title = "Original";
    string Item = "First";
    bool Enabled = true;
    bool Visible = true;
    int TitleReads = 0;
    int ItemReads = 0;
}
std::attribute<string> PatchRawNavigationAttribute(std::attribute<string> source, string value)
{
    source.set(value); return source;
}
void CheckNavigationProjection()
{
    NavigationProjectionModel model = NavigationProjectionModel();
    std::attribute<string> title = std::attribute<string>();
    title.bind([model]() mutable -> string { model.TitleReads++; return model.Title; });
    std::attribute<bool> enabled = std::attribute<bool>();
    enabled.bind([model]()->bool { return model.Enabled; });
    std::attribute<VS::Gui::Visibility> visibility = std::attribute<VS::Gui::Visibility>();
    visibility.bind_optional([model]()->optional<VS::Gui::Visibility>
    { return model.Visible ? optional<VS::Gui::Visibility>() : optional<VS::Gui::Visibility>(VS::Gui::Visibility::Hidden); });
    VS::Gui::TabItem page = VS::Gui::TabItem::Create().Key("one").Title(title).Enabled(enabled).Visibility(visibility);
    VS::Gui::TabItem other = VS::Gui::TabItem::Create().Key("two").Title("Second");
    VS::Gui::TabView tabs = VS::Gui::TabView::Create()[{page,other}];
    tabs.Initialize(); tabs.Synchronize();
    VS::Gui::Widget headers = tabs.GetChild(0);
    VS::Gui::Widget old = headers.GetChild(0);
    int reads = model.TitleReads;
    tabs.Synchronize();
    assert(model.TitleReads > reads, "header caching froze a dynamic page title");
    model.Title = "Changed"; model.Enabled = false; model.Visible = false;
    tabs.Synchronize();
    assert(old.GetText() == "Changed" && !old.GetEnabled() && old.GetVisibility() == VS::Gui::Visibility::Hidden,
        "header projection missed dynamic text, enabled or optional visibility");
    model.Visible = true; model.Enabled = true; tabs.Synchronize();
    assert(old.GetVisibility() == VS::Gui::Visibility::Visible && old.GetEnabled(), "header optional fallback remained stale");
    PatchRawNavigationTitle([page]()->void { page.GetTitle(); }, "Raw title");
    tabs.Synchronize();
    assert(headers.GetChild(0).GetText() == "Changed", "a default Title mutation replaced an explicit binding");
    VS::Gui::TabItem rawPage = VS::Gui::TabItem::Create().Key("raw");
    VS::Gui::TabView rawTabs = VS::Gui::TabView::Create()[{rawPage}];
    rawTabs.Initialize(); rawTabs.Synchronize();
    PatchRawNavigationTitle([rawPage]()->void { rawPage.GetTitle(); }, "Raw title");
    rawTabs.Synchronize();
    assert(rawTabs.GetChild(0).GetChild(0).GetText() == "Raw title", "header caching missed a raw default attribute mutation");
    rawTabs.Dispose();
    PatchNavigationFallbacks(other.__GetNativeHandle(), true); tabs.Synchronize();
    assert(!headers.GetChild(1).GetEnabled() && headers.GetChild(1).GetVisibility() == VS::Gui::Visibility::Hidden,
        "header projection missed native local and style changes");
    PatchNavigationFallbacks(other.__GetNativeHandle(), false); tabs.Synchronize();
    assert(headers.GetChild(1).GetEnabled() && headers.GetChild(1).GetVisibility() == VS::Gui::Visibility::Visible,
        "header projection missed cleared style fallback");
    VS::Gui::Button replacement = VS::Gui::Button::Create().Text("External");
    VS::Gui::Button replacementOther = VS::Gui::Button::Create().Text("External second");
    array<VS::Gui::Widget> replacementHeaders = {replacement,replacementOther};
    headers.SetChildren(replacementHeaders);
    tabs.VsguiUpdateHeaders();
    assert(replacement.GetText() == "Changed" && replacementOther.GetText() == "Second" && old.GetParent() == nullptr,
        "weak header views ignored external same-count replacement");
    replacement.SetText("Override"); replacement.SetEnabled(false); tabs.VsguiUpdateHeaders();
    assert(replacement.GetText() == "Changed" && replacement.GetEnabled(), "external header edits were not reconciled");
    tabs.RemoveChild(other); tabs.VsguiUpdateHeaders();
    assert(headers.GetChildCount() == 1 && headers.GetChild(0).GetText() == "Changed",
        "header topology retained a removed page");
    tabs.AddChild(other); tabs.VsguiUpdateHeaders();
    assert(headers.GetChildCount() == 2 && headers.GetChild(1).GetText() == "Second",
        "header topology did not follow the restored page");
    tabs.Dispose();

    std::attribute<string> itemText = std::attribute<string>();
    itemText.bind([model]() mutable -> string { model.ItemReads++; return model.Item; });
    std::attribute<string> selected = std::attribute<string>(); selected.bind([]()->string { return "one"; });
    VS::Gui::ComboBox combo = VS::Gui::ComboBox::Create().SelectedKey(selected).Placeholder("Choose")[{
        VS::Gui::ListItem::Create().Key("one").Text(itemText), VS::Gui::ListItem::Create().Key("two").Text("Second")
    }];
    combo.Initialize(); combo.Synchronize();
    reads = model.ItemReads; model.Item = "Updated"; combo.Synchronize();
    assert(model.ItemReads > reads && combo.GetText() == "Updated", "combo label caching froze a dynamic item");
    combo.Select("two");
    assert(combo.GetText() == "Second" && combo.GetActualSelectedKey() == "two", "combo candidate label changed");
    combo.Synchronize();
    assert(combo.GetText() == "Updated" && combo.GetActualSelectedKey() == "one" && combo.IsSelectedKeyBound(),
        "combo did not restore the bound selection after a candidate");
    string missing = "unchanged";
    assert(!combo.GetList().VsguiTryGetItemText("missing", ref missing) && missing == "unchanged", "declined label read changed its output");
    array<string> replacementLabels = {"Other"};
    combo.GetList().SetItems(replacementLabels); combo.Synchronize();
    assert(combo.GetText() == "Choose", "removed combo item did not restore its placeholder");
    array<string> labels = array<string>();
    for (int64 index = 0; index < 1000; index++) labels.push("Choice " + to_string(index));
    combo.GetList().SetVirtualItems(labels,24); combo.SetSelectedKey("900"); combo.Synchronize();
    assert(combo.GetList().FindItem("900") == nullptr && combo.GetText() == "Choice 900",
        "combo label read realized an offscreen row or lost its text");
    combo.GetList().SetVirtualItemText(900,"Changed virtual choice"); combo.Synchronize();
    assert(combo.GetText() == "Changed virtual choice", "unrealized combo label did not follow data changes");
    optional<string> compatibility = combo.GetList().VsguiGetItemText("900");
    assert(compatibility.has_value() && compatibility.get() == "Changed virtual choice", "optional label query compatibility changed");
    combo.Dispose();
}
)vs",
		    "synchronization.vs");
		Require(Loaded.bSuccess, Loaded.Error.Message);
		const auto Result = Session.Run();
		Require(Result.bSuccess, Result.Error.Message);
		if (!BackendPath.empty())
		{
			const auto Stats = Session.GetLastJitStats();
			// Mandatory finally/ref fallbacks are distinct from cost-model choices.
			// Check real native entry accounting independently below.
			Require(Stats.DispatchAttempts > 0 && Stats.Failures == 0 &&
			        (Stats.Dispatches > 0 || Stats.Fallbacks > 0),
			    "GUI synchronization did not use the configured JIT policy");
			std::cout << "GUI JIT policy: dispatches=" << Stats.Dispatches
			          << ", native leaf=" << Stats.NativeLeafExecutions
			          << ", native CFG=" << Stats.NativeCfgExecutions
			          << ", controlled native=" << Stats.ControlledNativeExecutions
			          << ", linear=" << Stats.LinearExecutions
			          << ", bridge=" << Stats.BridgeExecutions
			          << ", VM trampoline=" << Stats.VmTrampolineExecutions
			          << ", fallbacks=" << Stats.Fallbacks
			          << ", cost-model VM fallbacks=" << Stats.CostModelVmFallbacks << '\n';
		}
		// Keep this context across host calls, including the expected runtime fault.
		Session.SetHotReloadEnabled(true);
		const auto Navigation = Session.CallFunction("CheckNavigationProjection", {});
		Require(Navigation.bSuccess, Navigation.Error.Message);
		if (!BackendPath.empty())
		{
			const bool NativeCode = Config.JitBackendFactory->GetDescription().bNativeCode;
			// Reuse the GUI Session and its cache. Counters describe each entry,
			// rather than accumulating earlier synchronization or host calls.
			for (int Invocation = 0; Invocation < 2; ++Invocation)
			{
				FValue Output;
				const auto Probe = Session.CallFunction("GuiJitEntryProbe", {}, &Output);
				Require(Probe.bSuccess && std::get<std::int32_t>(Output) == 17, Probe.Error.Message);
				const auto Stats = Session.GetLastJitStats();
				Require(Stats.NativeLeafExecutions + Stats.NativeCfgExecutions == (NativeCode ? 1u : 0u) &&
				        Stats.ScalarNativeFastPathCalls == 0 && Stats.Failures == 0,
				    "GUI JIT counters did not describe only the current script entry");
				Require(Session.GetLastJitDiagnostics().Functions.empty(),
				    "GUI execution unexpectedly enabled detailed JIT diagnostics");
			}
			std::cout << "GUI JIT repeated native entries per invocation=" << (NativeCode ? 1 : 0) << '\n';
		}
		auto SynchronizationCheck = Session.CallFunction("CheckSynchronizationSequence", {});
		Require(SynchronizationCheck.bSuccess, SynchronizationCheck.Error.Message);
		SynchronizationCheck = Session.CallFunction("CheckNativeOutputFailure", {});
		Require(SynchronizationCheck.bSuccess, SynchronizationCheck.Error.Message);
		auto Recursive = Session.CallFunction("PrepareRecursiveControl", {});
		Require(Recursive.bSuccess, Recursive.Error.Message);
		const auto IsRecursiveGetterFailure = [](const FScriptError& Failure)
		{
			// Mobile hosts may intentionally run without localization catalogs.
			return Failure.MessageId == "diag.assertion_failed_with_message" &&
			    std::any_of(Failure.MessageArgs.begin(), Failure.MessageArgs.end(), [](const std::string& Argument)
			{
				return Argument.find("Recursive GUI property getter") != std::string::npos;
			});
		};
		Recursive = Session.CallFunction("RunRecursiveControl", {});
		Require(!Recursive.bSuccess && IsRecursiveGetterFailure(Recursive.Error),
		    "control snapshots bypassed recursive getter validation");
		Recursive = Session.CallFunction("RecoverRecursiveControl", {});
		Require(Recursive.bSuccess, Recursive.Error.Message);
		auto NativeFailure = Session.CallFunction("PrepareNativeGetterFailure", {});
		Require(NativeFailure.bSuccess, NativeFailure.Error.Message);
		NativeFailure = Session.CallFunction("RunNativeGetterFailure", {});
		Require(!NativeFailure.bSuccess && NativeFailure.Error.bUncaughtScriptException,
		    "a native attribute getter failure was swallowed");
		NativeFailure = Session.CallFunction("RecoverNativeGetterFailure", {});
		Require(NativeFailure.bSuccess, NativeFailure.Error.Message);
		auto CompoundRecursion = Session.CallFunction("PrepareCompoundCacheRecursion", {});
		Require(CompoundRecursion.bSuccess, CompoundRecursion.Error.Message);
		CompoundRecursion = Session.CallFunction("RunCompoundCacheRecursion", {});
		Require(!CompoundRecursion.bSuccess && IsRecursiveGetterFailure(CompoundRecursion.Error),
		    "compound query overloads bypassed recursion validation");
		CompoundRecursion = Session.CallFunction("RecoverCompoundCacheRecursion", {});
		Require(CompoundRecursion.bSuccess, CompoundRecursion.Error.Message);
	}

	void CheckNativeScriptTree()
	{
		struct FHost final : IEngineHost
		{
			void OnPrintLine(std::string_view) override
			{
			}
			void OnDiagnosticLine(std::string_view Text) override
			{
				std::cerr << Text << '\n';
			}
		} Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		std::string Error;
		Require(RegisterScriptBindings(Session.GetBindingRegistry(), Error) && ConfigureGuiScriptEngine(Config, Error),
		    Error);
		Require(ConfigureGuiScriptEngine(Config, Error) && Config.ImplicitImportPaths.size() == 1,
		    "GUI library configuration is not idempotent: " + Error);
		Session.ApplyConfig(Config);
		auto Loaded = Session.LoadSource(R"vs(
import "attribute.vs";
@gUiWiDgEt(iD="frame-default")
export class Frame : public VS::Gui::Widget
{
private:
    @GuiProperty(Style) std::attribute<double> Amount = std::attribute<double>(1);
    void OnAmountChanged() {SetGap(GetAmount());}
};
Frame root = nullptr;
class Lifetime {public:double Value=8;};
int FactoryCreates = 0;
VS::Gui::Widget MakeResource(VS::Gui::FactoryArgument argument)
{
    FactoryCreates++;
    return VS::Gui::Widget::Create().Gap(6);
}
VS::Gui::Widget MakeContextResource(VS::Gui::FactoryArgument argument)
{
    auto box = cast<VS::Gui::FactoryArgumentOf<Lifetime>>(argument);
    assert(box != nullptr, "factory argument did not retain its concrete type");
    return VS::Gui::Widget::Create().Gap(box.Value.Value);
}
@GuiWidget export class EarlyProbe : public VS::Gui::Widget
{
public:
    int Reads = 0;
    EarlyProbe()
    {
        weak<EarlyProbe> observer = weak<EarlyProbe>(this);
        std::attribute<double> source = std::attribute<double>();
        source.bind([observer]()->double { EarlyProbe self = observer.lock(); self.Reads++; return 12; });
        SetAmount(source);
        bool caught = false;
        try { GetAmount(); } catch (string failure) { caught = true; }
        assert(caught && Reads == 0, "constructor executed a configured Getter early");
    }
private:
    @GuiProperty std::attribute<double> Amount = std::attribute<double>(1);
};
using namespace VS::Gui;
@GuiWidget export class RetryProperty : public VS::Gui::Widget
{
public:
    int Attempts=0;
private:
    @GuiProperty int Value=5;
    void OnValueChanged(){Attempts++;if(Attempts==1){throw "retry property hook";}SetGap(GetValue());}
};
@GuiWidget export class ScopedBase : public VS::Gui::Widget
{
public:
    ScopedBase(){AddInternalChild(VS::Gui::Widget::Create().Id("same").Gap(3));}
    double ReadBase(){return elements.Require("same").GetGap();}
};
@GuiWidget export class ScopedDerived : public ScopedBase
{
public:
    ScopedDerived(){this.AddInternalChild(VS::Gui::Widget::Create().Id("same").Gap(7));}
    double ReadDerived(){return elements.Require("same").GetGap();}
};
@GuiWidget export class AppendProbe : public VS::Gui::Widget
{
public:
    AppendProbe() { VsguiObserveChildChanges(); }
    int Changes = 0;
    bool Reenter = false;
    bool Fail = false;
    VS::Gui::Widget Extra = nullptr;
protected:
    void OnChildrenChanged() override
    {
        Changes++;
        if (Reenter) { Reenter=false; AddChild(Extra); }
        if (Fail) { Fail=false; throw "append notification failed"; }
    }
};
VS::Gui::__NativeWidget CreateRoot()
{
    AppendProbe append=AppendProbe::Create();
    VS::Gui::Widget first=VS::Gui::Widget::Create().Id("append-first");
    append.Extra=VS::Gui::Widget::Create().Id("append-extra");
    int changes=append.Changes;
    append.Reenter=true;
    append.AddChild(first);
    assert(append.Changes==changes+2 && len(append.GetChildren())==2 && append.GetChild(1)==append.Extra,
        "append lost reentrant child changes or duplicated notifications");
    append.Fail=true;
    bool failedAppend=false;
    try { append.AddChild(VS::Gui::Widget::Create().Id("append-committed")); }
    catch(string failure) { failedAppend=true; }
    assert(failedAppend && len(append.GetChildren())==3 && append.GetChild(2).GetParent()==append,
        "append notification failure lost the committed child views");
    append.Dispose();

    VS::Gui::Widget queried=VS::Gui::Widget();
    double committed=queried.GetGap();
    int queriedReads=0;
    std::attribute<double> queriedSource=std::attribute<double>();
    queriedSource.bind([&queriedReads]()->double
    {
        assert(VS::Gui::PropertyEvaluation::Depth==1, "native read guard changed getter depth");
        queriedReads++;return 41;
    });
    queried.SetGap(queriedSource);
    assert(queried.GetGap()==committed && queriedReads==0, "plain query lost its committed pre-binding value");
    queried.Initialize();
    assert(queried.GetGap()==41 && queriedReads==1, "plain query shortcut suppressed a later dynamic getter");
    queried.SetGap(7);queried.SetGap(8);
    assert(queried.GetGap()==8, "empty attribute reset retained an old value");
    queried.Dispose();
    VS::Gui::Widget readOnly=VS::Gui::Widget::Create();
    assert(readOnly.IsInitialized(),"read-only query fixture was not initialized");
    auto nativeReadOnly=readOnly.__GetNativeHandle();
    nativeReadOnly.SetWidth(VS::Gui::Length(37));nativeReadOnly.SetText("native text");
    nativeReadOnly.SetFontSize(18);nativeReadOnly.SetGap(9);
    assert(readOnly.GetWidth()==VS::Gui::Length(37) && readOnly.GetText()=="native text" &&
        readOnly.GetFontSize()==18 && readOnly.GetGap()==9,"unused scalar/compound/string queries lost native values");
    bool widthAvailable=false;
    VS::Gui::Length widthModel=VS::Gui::Length(41);
    std::attribute<VS::Gui::Length> queriedWidth=std::attribute<VS::Gui::Length>();
    queriedWidth.bind_optional([&widthAvailable,&widthModel]()->optional<VS::Gui::Length>
    { return widthAvailable?optional<VS::Gui::Length>(widthModel):optional<VS::Gui::Length>(); });
    readOnly.SetWidth(queriedWidth);
    assert(readOnly.GetWidth()==VS::Gui::Length(37),"binding after an unused query lost its pending committed value");
    readOnly.Synchronize();
    assert(readOnly.GetWidth()==nativeReadOnly.GetFallbackWidth(),"optional binding after an unused query lost fallback");
    widthAvailable=true;readOnly.Synchronize();
    assert(readOnly.GetWidth()==widthModel,"optional binding after an unused query did not resume");
    widthModel=VS::Gui::Length(47);readOnly.Synchronize();
    assert(readOnly.GetWidth()==widthModel,"optional binding after an unused query became stale");
    readOnly.ClearWidth();nativeReadOnly.SetWidth(VS::Gui::Length(52));
    assert(readOnly.GetWidth()==VS::Gui::Length(52),"cleared query cache hid native replacement");
    int nestedQueries=0;
    weak<VS::Gui::Widget> queryOwner=weak<VS::Gui::Widget>(readOnly);
    std::attribute<string> queriedText=std::attribute<string>();
    queriedText.bind([queryOwner,&nestedQueries]()->string
    {
        nestedQueries++;
        assert(VS::Gui::PropertyEvaluation::Depth==1,"unused query changed its getter's evaluation guard");
        return to_string(queryOwner.lock().GetFontSize());
    });
    readOnly.SetText(queriedText);
    assert(readOnly.GetText()=="native text" && nestedQueries==0,"unused string query executed its binding early");
    readOnly.Synchronize();nativeReadOnly.SetFontSize(19);readOnly.Synchronize();
    assert(readOnly.GetText()==to_string(19.0) && nestedQueries==2,
        "unused queries inside a getter froze a dependency or repeated an evaluation");
    readOnly.Dispose();
    RetryProperty retry=RetryProperty();
    bool failedHook=false;
    try{retry.Initialize();}catch(string failure){failedHook=true;}
    assert(failedHook && retry.Attempts==1, "property hook failure was suppressed");
    retry.Initialize();
    assert(retry.Attempts==2 && retry.GetGap()==5, "property hook was committed before successful synchronization");
    ScopedDerived scoped=ScopedDerived::Create();
    assert(scoped.ReadBase()==3 && scoped.ReadDerived()==7, "procedural inheritance shared private name scopes");
    VS::Gui::Widget assigned=nullptr;
    Frame literal=Frame::Create()[{assigned=VS::Gui::Widget::Create().Id("assigned"), Frame::Create().Id("nested")}];
    assert(literal.GetChildCount()==2 && literal.GetChild(0)==assigned, "array literal child construction or assignment order failed");
    type<VS::Gui::Widget> rootType = type<VS::Gui::Widget>();
    array<string> roots = {rootType.qualified_name};
    VS::Gui::FactoryRegistry::Register(VS::Gui::__NativeResources::Resolve("sample.vswml"), roots, "", MakeResource);
    auto factory = VS::Gui::LoadFactory<VS::Gui::Widget>("sample.vswml");
    VS::Gui::Widget resource = factory.Create();
    assert(resource.GetGap() == 6 && FactoryCreates == 1, "typed factory did not create a widget");
    bool typeMismatch = false;
    try { auto invalid = VS::Gui::LoadFactory<Frame>("sample.vswml"); } catch (string failure) { typeMismatch = true; }
    assert(typeMismatch && FactoryCreates == 1, "factory type checking constructed an unexpected UI");
    type<Lifetime> contextType = type<Lifetime>();
    VS::Gui::FactoryRegistry::Register(VS::Gui::__NativeResources::Resolve("context.vswml"), roots, contextType.qualified_name, MakeContextResource);
    auto contextual = VS::Gui::LoadFactory<VS::Gui::Widget>("context.vswml");
    Lifetime logic = Lifetime();
    logic.Value = 13;
    VS::Gui::Widget contextualRoot = contextual.Create(logic);
    assert(contextualRoot.GetGap() == 13, "typed factory context was not forwarded");
    EarlyProbe probe = EarlyProbe::Create();
    assert(probe.GetAmount() == 12 && probe.Reads == 1, "first safe property synchronization");
    probe.SetAmount(30);
    weak<EarlyProbe> observer = weak<EarlyProbe>(probe);
    std::attribute<double> later = std::attribute<double>();
    later.bind([observer]()->double { EarlyProbe self = observer.lock(); self.Reads++; return 44; });
    probe.SetAmount(later);
    assert(probe.GetAmount() == 30 && probe.Reads == 1, "pending Getter ignored its committed snapshot");
    probe.Synchronize();
    assert(probe.GetAmount() == 44 && probe.Reads == 2, "safe update did not commit a replacement Getter");
    VS::Gui::Widget early = VS::Gui::Widget();
    int nativeReads = 0;
    std::attribute<double> initial = std::attribute<double>();
    initial.bind([&nativeReads]()->double { nativeReads++; return 7; });
    early.SetGap(initial);
    bool nativeCaught = false;
    try { early.GetGap(); } catch (string failure) { nativeCaught = true; }
    assert(nativeCaught && nativeReads == 0, "native property Getter ran before initialization");
    early.Initialize();
    assert(early.GetGap() == 7 && nativeReads == 1, "native first safe synchronization");
    assert(VS::Gui::Length(12)==VS::Gui::Length(12),"Length value equality");
    assert(VS::Gui::Insets(1,2,3,4)==VS::Gui::Insets(1,2,3,4),"Insets value equality");
    assert(VS::Gui::Color(0.2,0.3,0.4,1)==VS::Gui::Color(0.2,0.3,0.4,1),"Color value equality");
    Frame child=Frame::Create().Id("child").Amount(9);
    array<VS::Gui::Widget> children={child};
    auto config=Frame::Create().Amount(6).Gap(2)[children];
    children.clear();
    root=config;
    assert(root.GetId()=="frame-default","annotation default ID");
    assert(root.GetChildCount()==1&&root.GetChild(0).GetParent()==root,"script child ownership");
    assert(root.Find("child")==root.GetChild(0),"script element lookup");
    Frame found=cast<Frame>(root.elements.Require("child"));
    assert(found==child&&found.GetAmount()==9,"typed element lookup lost script identity");
    root.Synchronize();
    double current=4;
    int reads=0;
    std::attribute<double> gap=std::attribute<double>();
    gap.bind([&current,&reads]()->double{reads++;return current;});
    VS::Gui::Widget bound=VS::Gui::Widget::Create().Gap(gap);
    assert(bound.GetGap()==4&&bound.GetGap()==4&&reads==1,"native property getter cache");
    bound.Synchronize();
    assert(bound.GetGap()==4&&reads==2,"native property frame synchronization");
    current=7;
    bound.Synchronize();
    assert(bound.GetGap()==7&&reads==3,"native property binding remained stale");
    bound.SetGap(11);
    current=20;
    bound.Synchronize();
    assert(bound.GetGap()==11&&reads==3,"native literal setter retained the old binding");
    bound.SetGap(gap);
    bound.ClearGap();
    assert(bound.GetGap()==0,"native Clear did not restore the default");
    bound.SetGap(std::attribute<double>(9));
    bound.Synchronize();
    bound.SetGap(std::attribute<double>());
    assert(bound.GetGap()==0,"an empty Attribute retained the previous native local value");
    bound.Synchronize();
    assert(bound.GetGap()==0,"an empty Attribute was restored during synchronization");
    Lifetime payload = Lifetime();
    weak<Lifetime> lifetime = weak<Lifetime>(payload);
    std::attribute<double> retained = std::attribute<double>();
    retained.bind([payload]()->double { return payload.Value; });
    Frame disposable = Frame::Create().Amount(retained);
    payload = nullptr;
    retained.reset();
    assert(!lifetime.expired(), "configured Getter was lost before disposal");
    disposable.Dispose();
    assert(lifetime.expired(), "Dispose retained a generated property Getter");
    bool disposedRead = false;
    try { disposable.GetAmount(); } catch (string failure) { disposedRead = true; }
    assert(disposedRead, "disposed custom property remained usable");
    return root.__GetNativeHandle();
}
)vs",
		    "script-tree.vs");
		Require(Loaded.bSuccess, Loaded.Error.Message);
		auto Initialized = Session.Run(ERunMode::EvalOnce);
		Require(Initialized.bSuccess, Initialized.Error.Message);
		FValue Handle;
		auto Called = Session.CallFunction("CreateRoot", {}, &Handle);
		Require(Called.bSuccess, Called.Error.Message);
		auto Root = GetScriptWidget(Handle, Error);
		Require(Root && Root->GetChildCount() == 1 && Gap(Root) == 6 && Gap(Root->GetChild(0, Error)) == 9,
		    "generated tree did not reach native GUI: " + Error);
	}

	void CheckExternal(const FMarkupScriptFactory& Factory)
	{
		FMarkupCompileOptions CompileOptions;
		CompileOptions.Read = [](const auto&, const auto& Reference, auto& Source, auto&)
		{
			Source = {Reference, R"(export class Logic {
                public:
                    int64 Count = 4;
                    @GuiMethod void Bump() { Count = Count + 1; }
                private:
                    int64 Secret = 9;
            };)"};
			return true;
		};
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile({"external.vswml", R"ML(<Widget>
            <script src="logic.vs" type="Logic" />
            <Widget id="external" bind:gap="{ Count + 1 }" on:pointer-down="{ int64 Count = 90; context.Count = context.Count + 2; Bump(); if (Count != 90) { throw &quot;shadowed local changed&quot;; } }" />
        </Widget>)ML"},
		    CompileOptions, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		auto Root = Program->Create(Factory, Diagnostic);
		Require(Root != nullptr, Diagnostic.Format());
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		Require(Gap(FMarkupProgram::Find(Root, "external")) == 5, "external unqualified member");
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::PointerDown;
		Event.WindowId = 1;
		Event.Button = 1;
		bool bHandled = false;
		Require(Context->ProcessEvent(Event, bHandled, Error) && Context->Update(1, Error) &&
		        Gap(FMarkupProgram::Find(Root, "external")) == 8,
		    "external local/member qualification: " + Error);
		Require(Context->Close(Error), Error);
		Program = FMarkupProgram::Compile(
		    {"private.vswml", "<Widget><script src='logic.vs' type='Logic'/><Widget bind:gap='{ Secret }'/></Widget>"},
		    CompileOptions, Diagnostic);
		Require(Program && !Program->Create(Factory, Diagnostic), "external private access was exposed");
		Program =
		    FMarkupProgram::Compile({"missing-type.vswml", "<Widget><script src='logic.vs' type='Missing'/></Widget>"},
		        CompileOptions, Diagnostic);
		Require(Program && !Program->Create(Factory, Diagnostic) && Diagnostic.Location.Source == "missing-type.vswml",
		    "external script directive lost its ML source location");
	}

	void CheckComponent(const FMarkupScriptFactory& Factory)
	{
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile({"component.vswml", R"ML(<component type="Demo::Card" mark="card">
            <script><![CDATA[
            public:
                int64 Count = 2;
                @GuiMethod void Bump() { Count = Count + 1; }
            ]]></script>
            <template><Widget id="part" bind:gap="{ GetGap() + Count }" on:pointer-down="Bump" /></template>
            <style>:root { gap: 4px; }</style>
        </component>)ML"},
		    {}, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		auto Root = Program->Create(Factory, Diagnostic);
		Require(Root != nullptr, Diagnostic.Format());
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		Require(Gap(FMarkupProgram::Find(Root, "part")) == 6, "component script or host Widget base");
		Require(Context->Close(Error), Error);
	}

	void CheckComponentPropertyRound(const FMarkupScriptFactory& Factory)
	{
		FMarkupCompileOptions Options;
		Options.Read = [](const auto&, const auto& Reference, auto& Source, auto& Error)
		{
			return ReadMarkupFile({}, std::string(VEROSCRIPT_GUI_STDLIB) + "/" + Reference, Source, Error);
		};
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile({"property-round.vswml", R"ML(<component type="Demo::RoundProbe">
            <import src="attribute.vs"/>
            <script><![CDATA[
            public:
                double Count = 2;
                int64 Reads = 0;
                RoundProbe()
                {
                    weak<RoundProbe> observer=weak<RoundProbe>(this);
                    Value.bind_optional([observer]()->optional<double>
                    {
                        RoundProbe owner=observer.lock();
                        if(owner==nullptr){return optional<double>();}
                        owner.Reads++;
                        return optional<double>(owner.Count);
                    });
                }
            private:
                @GuiProperty std::attribute<double> Value = std::attribute<double>();
            ]]></script>
            <template><Widget>
                <Widget id="value" bind:gap="{ GetValue() }" on:pointer-down="{ Count++; }"/>
                <Widget id="reads" bind:gap="{ Reads }"/>
            </Widget></template>
        </component>)ML"},
		    Options, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		auto Root = Program->Create(Factory, Diagnostic);
		Require(Root != nullptr, Diagnostic.Format());
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		auto Value = FMarkupProgram::Find(Root, "value");
		auto Reads = FMarkupProgram::Find(Root, "reads");
		Require(Gap(Value) == 2 && Gap(Reads) == 1, "initial GUI property read was not shared by the frame");
		Require(Context->Update(1, Error) && Gap(Value) == 2 && Gap(Reads) == 2,
		    "getter was not reevaluated for the next frame: " + Error);
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::PointerDown;
		Event.WindowId = 1;
		Event.PointerId = 1;
		Event.Button = 1;
		bool bHandled = false;
		Require(Context->CapturePointer(1, Value, Error) && Context->ProcessEvent(Event, bHandled, Error) &&
		        Context->Update(2, Error) && Gap(Value) == 3 && Gap(Reads) == 3,
		    "component property remained stale or getter executed more than once: " + Error);
		Require(Context->Close(Error), Error);
	}

	void CheckEnvironment()
	{
		std::string Error;
		auto Registry = std::make_shared<FHostBindingRegistry>();
		FHostTypeId Id;
		Require(Registry->RegisterType("Host.Existing", EHostTypeKind::Class, Id, Error), Error);
		Require(Registry->RegisterNative(
		            "ReadFromHost", ENativeType::Int64, {},
		            [](const FNativeCallContext&, FValue& Out, std::string&)
		{
			Out = std::int64_t{42};
			return true;
		},
		            nullptr, Error),
		    Error);
		FModuleEnvironmentDesc Description;
		Description.Scopes.push_back({"app", {}, false});
		Description.Scopes.push_back({"stdlib", {}, true});
		Description.SourceRoots.push_back({VEROSCRIPT_GUI_STDLIB, "stdlib"});
		Description.EntryOwnerId = "app";
		Description.Bindings = Registry;
		FAnnotationDefinition MethodAnnotation;
		MethodAnnotation.Name = "@GuiMethod";
		MethodAnnotation.Id = "host:gui-method";
		MethodAnnotation.CaseSensitive = false;
		Description.CompileProcessing.AnnotationDefinitions.push_back(std::move(MethodAnnotation));
		const auto EncodedRoot = std::filesystem::current_path().generic_u8string();
		Description.SourceRoots.push_back({std::string(EncodedRoot.begin(), EncodedRoot.end()), "app"});
		FMarkupScriptOptions Options;
		VeroScript::Test::Configure(Options.Engine);
		Options.Engine.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		Options.Engine.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
		Require(Options.Engine.ModuleEnvironment != nullptr, Error);
		Require(ConfigureGuiScriptEngine(Options.Engine, Error) && ConfigureGuiScriptEngine(Options.Engine, Error),
		    Error);
		Require(Options.Engine.ImplicitImportPaths.size() == 1 && Options.Engine.SourceOverlays.size() == 13 &&
		        Options.Engine.ModuleEnvironment->GetDescription().Modules.size() == 13,
		    "repeated GUI configuration duplicated framework sources");
		for (const auto& Overlay : Options.Engine.SourceOverlays)
		{
			const auto Canonical =
			    PathUtil::ToUtf8(std::filesystem::weakly_canonical(PathUtil::FromUtf8(Overlay.Path)));
			Require(Overlay.Path == Canonical, "GUI framework source retained an unresolved parent alias");
		}
		const auto& FrameworkPath = Options.Engine.ImplicitImportPaths.front();
		Require(FrameworkPath ==
		        PathUtil::ToUtf8(std::filesystem::weakly_canonical(PathUtil::FromUtf8(FrameworkPath))),
		    "GUI framework import retained an unresolved parent alias");
		std::cout << "GUI environment: creating component instance" << std::endl;
		CheckComponent(MakeMarkupScriptFactory(Options));
		std::cout << "GUI environment: component instance checked" << std::endl;
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile(
		    {"host-environment.vswml", "<Widget><script>public: int64 Value=ReadFromHost();</script><Widget id='host' "
		                               "bind:gap='{ Value }'/></Widget>"},
		    {}, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		std::cout << "GUI environment: creating host-bound instance" << std::endl;
		auto Root = Program->Create(MakeMarkupScriptFactory(Options), Diagnostic);
		Require(Root != nullptr, Diagnostic.Format());
		std::cout << "GUI environment: host-bound instance created" << std::endl;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error) &&
		        Gap(FMarkupProgram::Find(Root, "host")) == 42,
		    "host environment bindings were not used: " + Error);
		Require(Context->Close(Error), Error);
		Require(Registry->GetTypes().size() == 1 && Registry->FindType(Id)->Name == "Host.Existing",
		    "GUI mutated the published host catalog");
	}

	void CheckAttribute(const FMarkupScriptFactory& Factory)
	{
		FMarkupCompileOptions Options;
		Options.Read = [](const auto&, const auto& Reference, auto& Source, auto& Error)
		{
			return ReadMarkupFile({}, std::string(VEROSCRIPT_GUI_STDLIB) + "/" + Reference, Source, Error);
		};
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile({"attribute-page.vswml", R"ML(<Widget>
			<import src="attribute.vs"/>
			<script><![CDATA[public: std::attribute<double> Value = std::attribute<double>(3.0);]]></script>
			<Widget id="value" bind:gap="{ Value.get() }" on:pointer-down="{ Value.set(Value.get() + 1); }"/>
		</Widget>)ML"},
		    Options, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		auto Root = Program->Create(Factory, Diagnostic);
		Require(Root != nullptr, Diagnostic.Format());
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error) &&
		        Gap(FMarkupProgram::Find(Root, "value")) == 3,
		    "std::attribute getter: " + Error);
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::PointerDown;
		Event.WindowId = 1;
		Event.Button = 1;
		bool bHandled = false;
		Require(Context->ProcessEvent(Event, bHandled, Error) && Context->Update(1, Error) &&
		        Gap(FMarkupProgram::Find(Root, "value")) == 4,
		    "std::attribute event update: " + Error);
		Require(Context->Close(Error), Error);
	}

	void CheckReservedNames(const FMarkupScriptFactory& Factory)
	{
		for (const std::string Body : {"array&lt;int&gt; values = {1, 2}; for (int context : values) {}",
		         "using context = int;", "try { throw 1; } catch (int event) {}"})
		{
			FMarkupDiagnostic Diagnostic;
			auto Program = FMarkupProgram::Compile({"reserved.vswml", "<Widget on:pointer-down='{ " + Body + " }'/>"},
			    {}, Diagnostic);
			auto Created = Program ? Program->Create(Factory, Diagnostic) : nullptr;
			Require(Program && !Created && Diagnostic.Message.find("gui.reserved_binding_name") != std::string::npos,
			    "binding helper name was shadowed in " + Body + ": " + Diagnostic.Format());
		}
	}

	void CheckExample(const FMarkupScriptFactory& Factory)
	{
		std::string Error;
		FMarkupSource Source;
		Require(ReadMarkupFile({}, std::string(VEROSCRIPT_GUI_MARKUP_EXAMPLES) + "/counter.vswml", Source, Error),
		    Error);
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile(Source, {}, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		auto Root = Program->Create(Factory, Diagnostic);
		Require(Root != nullptr, Diagnostic.Format());
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		auto Counter = FMarkupProgram::Find(Root, "counter");
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::PointerDown;
		Event.WindowId = 1;
		Event.Button = 1;
		bool bHandled = false;
		FPropertyValue Value;
		Require(Context->ProcessEvent(Event, bHandled, Error) && Context->Update(1, Error) && Counter &&
		        Counter->Get(EProperty::Width, Value, Error) &&
		        std::get<FLength>(Value) == FLength{ELengthUnit::Percent, 30},
		    "counter file example: " + Error);
		Require(Context->Close(Error), Error);
	}

	void CheckInline(const FMarkupScriptFactory& Factory)
	{
		std::cout << "GUI inline: creating first instance" << std::endl;
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile({"script-page.vswml", R"(<Widget>
            <script><![CDATA[
            public:
                int64 Count = 2;
                @guiMETHOD(mARK = "increment")
                void Increment() { Count = Count + 1; }
                @GuiMethod(Mark = "read-count")
                double ReadCount() const { return cast<double>(Count); }
                @GuiMethod(Mark = "release")
                void Release(VS::Gui::Event input) {
                    if (input.Button != 1) { throw "wrong button"; }
                    Count = Count + 3;
                    input.PreventDefault();
                }
            ]]></script>
            <Widget id="value" bind:gap="{ Count }" bind:opacity="{ 0.5 }"
                    on:pointer-down="increment" on:pointer-up="release" />
            <Widget id="dependent" bind:gap="{ elements.Require(&quot;value&quot;).GetGap() + 1 }" />
            <Widget id="method" bind:gap="read-count" />
        </Widget>)"},
		    {}, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		auto Root = Program->Create(Factory, Diagnostic);
		Require(Root != nullptr, Diagnostic.Format());
		std::cout << "GUI inline: first instance created" << std::endl;
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		auto Value = FMarkupProgram::Find(Root, "value");
		auto Dependent = FMarkupProgram::Find(Root, "dependent");
		Require(Gap(Value) == 2 && Gap(Dependent) == 3 && Gap(FMarkupProgram::Find(Root, "method")) == 2,
		    "inline script, named or dependent getter");
		Platform::FEvent Event;
		Event.WindowId = 1;
		Event.PointerId = 1;
		Event.Button = 1;
		Event.Type = Platform::EEventType::PointerDown;
		bool bHandled = false;
		Require(Context->CapturePointer(1, Value, Error) && Context->ProcessEvent(Event, bHandled, Error) &&
		        Context->Update(1, Error) && Gap(Value) == 3 && Gap(Dependent) == 4,
		    "named event or script state did not persist: " + Error);
		Event.Type = Platform::EEventType::PointerUp;
		Require(Context->ProcessEvent(Event, bHandled, Error) && bHandled && Context->Update(2, Error) &&
		        Gap(Value) == 6,
		    "event block or routing control: " + Error);
		Require(Value->Set(EProperty::Gap, 40.0, Error) && Context->Update(3, Error) && Gap(Value) == 40 &&
		        Gap(Dependent) == 41,
		    "script getter was not replaceable");
		std::cout << "GUI inline: creating second instance" << std::endl;
		auto OtherRoot = Program->Create(Factory, Diagnostic);
		Require(OtherRoot != nullptr, Diagnostic.Format());
		std::cout << "GUI inline: second instance created" << std::endl;
		auto Other = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Require(Other->SetRoot(OtherRoot, Error) && Other->Update(0, Error) &&
		        Gap(FMarkupProgram::Find(OtherRoot, "value")) == 2,
		    "instances shared script fields");
		Require(Context->Close(Error) && Other->Close(Error), Error);
		std::cout << "GUI inline: checking invalid script" << std::endl;
		Program = FMarkupProgram::Compile(
		    {"bad-script.vswml",
		        "<Widget><script>public: int X = 1;</script><Widget bind:gap='{ Missing }'/></Widget>"},
		    {}, Diagnostic);
		Require(Program && !Program->Create(Factory, Diagnostic) && Diagnostic.Location.Source == "bad-script.vswml" &&
		        Diagnostic.Location.Line == 1,
		    "script type error lacked original ML source");
		Root.reset();
		Value.reset();
		Dependent.reset();
		OtherRoot.reset();
		std::cout << "GUI inline: collecting script instances" << std::endl;
		CollectMarkupScripts();
		std::cout << "GUI inline: instances collected" << std::endl;
	}
} // namespace

int main(int Argc, char** Argv)
{
	try
	{
		Require(Argc <= 3, "usage: VeroScriptGuiScriptTests [scenario] [jit-backend]");
		const std::string Scenario = Argc >= 2 ? Argv[1] : "All";
		if (Scenario == "JitSynchronization")
		{
			Require(Argc == 3, "JitSynchronization requires a matching JIT backend");
			CheckSynchronization(Argv[2]);
			std::cout << "GUI synchronization under JIT policy passed\n";
			return 0;
		}
		Require(Argc <= 2, "a JIT backend is accepted only for JitSynchronization");
		FMarkupScriptOptions Options;
		VeroScript::Test::Configure(Options.Engine);
		Options.Engine.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		auto Factory = MakeMarkupScriptFactory(Options);
		bool bMatched = false;
		const auto Run = [&](const char* Name, const auto& Work)
		{
			if (Scenario == "All" || Scenario == Name)
			{
				bMatched = true;
				std::cout << "GUI script scenario: " << Name << std::endl;
				Work();
			}
		};
		Run("NativeTree", []
		{
			CheckNativeScriptTree();
		});
		Run("SourceSnapshots", []
		{
			CheckSourceSnapshots();
		});
		Run("ConstQueries", []
		{
			CheckConstQueries();
		});
		Run("BackendSelection", []
		{
			CheckContextBackendSelection();
		});
		Run("PublicRuntime", []
		{
			CheckPublicRuntime();
		});
		Run("Synchronization", []
		{
			CheckSynchronization();
		});
		Run("ComponentProperties", [&]
		{
			CheckComponentPropertyRound(Factory);
		});
		Run("ExternalState", [&]
		{
			CheckExternal(Factory);
		});
		Run("Components", [&]
		{
			CheckComponent(Factory);
		});
		Run("Environment", []
		{
			CheckEnvironment();
		});
		Run("Attributes", [&]
		{
			CheckAttribute(Factory);
		});
		Run("Files", [&]
		{
			CheckExample(Factory);
		});
		Run("ReservedNames", [&]
		{
			CheckReservedNames(Factory);
		});
		Run("Inline", [&]
		{
			CheckInline(Factory);
		});
		Require(bMatched, "unknown GUI script scenario: " + Scenario);
		std::cout << "GUI VS script generation, bindings and events passed\n";
		return 0;
	}
	catch (const std::exception& Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}

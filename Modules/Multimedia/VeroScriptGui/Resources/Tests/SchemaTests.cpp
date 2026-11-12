// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptGuiSchema.h"
#include "VeroScriptCompile.h"
#include "VeroScriptGuiCompilation.h"
#include "VeroScriptSession.h"
#include "VeroScriptResources.h"
#include "VeroScriptPath.h"
#include "Compilation/VeroScriptGuiSource.h"
#include "Bindings/VeroScriptGuiScriptLibrary.h"
#include "VeroScriptTemplate.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <filesystem>

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

	FCompileOptions Options()
	{
		FCompileOptions Result;
		VeroScript::Test::Configure(Result);
		Result.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		for (const auto* Name : {"@GuiWidget", "@GuiProperty", "@GuiMethod", "@GuiEvent"})
		{
			FAnnotationDefinition Definition;
			Definition.Id = std::string("gui:") + Name;
			Definition.Name = Name;
			Definition.CaseSensitive = false;
			Result.CompileProcessing.AnnotationDefinitions.push_back(std::move(Definition));
		}
		return Result;
	}

	void CheckGeneratedScopeIdentity()
	{
		const std::string Name = "Demo.Card";
		const auto First = std::string("first#module.vs") + '\x1f' + Name;
		const auto Second = std::string("second#module.vs") + '\x1f' + Name;
		Require(GuiScopeSuffix(Name) == GuiScopeSuffix("Demo::Card"),
		    "public scope identity must match markup source spelling");
		Require(GuiScopeSuffix(First) != GuiScopeSuffix(Name) &&
		        GuiScopeSuffix(First) != GuiScopeSuffix(Second),
		    "private scope identity lost its declaring module");
		const auto Body = GuiScopedBody("AddInternalChild(child);", First, true);
		Require(Body == "VsguiScopedAddInternalChild" + GuiScopeSuffix(First) + "(child);",
		    "private declaration scope rewrite disagrees with generated members");
	}

	void CheckTemplateValueSourceTypes()
	{
		const auto Encoded = [](const FTemplateConstant& Value)
		{
			FTemplateArgument Argument;
			Argument.Kind = ETemplateParameterKind::Value;
			Argument.Value = Value;
			return "@value:" + EncodeTemplateArgument(Argument);
		};
		FTemplateValueType Type;
		Require(TryGetTemplateValueType("int", Type), "int template type");
		const auto Count = Encoded({Type, 3});
		const auto Box = "Box#3:int" + std::to_string(Count.size()) + ':' + Count;
		const auto SourceBox = GuiSourceType(Box);
		Require(SourceBox == "Box<int,(cast<int>(3i64))>", "closed GUI value template argument");
		Require(GuiSourceType("attribute#" + std::to_string(Box.size()) + ':' + Box) ==
		        "std::attribute<" + SourceBox + '>',
		    "nested GUI value template argument");
		const auto ScopedBox = std::string("review#types.vs") + '\x1f' + "Demo." + Box;
		const auto ScopedSourceBox = "Demo::" + SourceBox;
		Require(GuiSourceType(ScopedBox) == ScopedSourceBox, "namespaced GUI template type");
		Require(GuiSourceType("array<" + ScopedBox + '>') == "array<" + ScopedSourceBox + '>' &&
		        GuiSourceType("map<int,array<" + ScopedBox + ">>") ==
		            "map<int,array<" + ScopedSourceBox + ">>",
		    "GUI containers retained encoded element types");
		Require(GuiSourceType("fn(const ref " + ScopedBox + ")->array<" + ScopedBox + '>') ==
		        "fn(const ref " + ScopedSourceBox + ")->array<" + ScopedSourceBox + '>',
		    "GUI callable retained encoded parameter or return types");
		Require(GuiSourceType(ScopedBox + "[2][3]") == ScopedSourceBox + "[2][3]",
		    "GUI native array changed its element type or dimension order");
		Require(GuiSourceType("array<" + ScopedBox + ">[2][3]") ==
		        "array<" + ScopedSourceBox + ">[2][3]",
		    "GUI native array retained its encoded container element type");
		Require(GuiSourceType("string[]") == "string[]", "GUI type conversion rejected the script argument array");
		const auto ArrayArgument = "array<" + ScopedBox + '>';
		Require(GuiSourceType("Holder#" + std::to_string(ArrayArgument.size()) + ':' + ArrayArgument) ==
		        "Holder<array<" + ScopedSourceBox + ">>",
		    "mixed canonical GUI template arguments");
		const auto PlainNested = GuiSourceType("Plain$arity0.Inner");
		const auto Nested = GuiSourceType("Outer$arity1.Inner#3:int6:string");
		const auto DeepNested = GuiSourceType("A$arity1.B$arity1.C#3:int6:string4:bool");
		const auto NestedEnum = GuiSourceType("Outer$arity1.Mode#3:int");
		const auto ValueOwner = GuiSourceType("ValueOwner$arity1.Inner#" + std::to_string(Count.size()) + ':' +
		    Count + "6:string");
		Require(PlainNested == "Plain::Inner" && Nested == "Outer<int>::Inner<string>" &&
		        DeepNested == "A<int>::B<string>::C<bool>" && NestedEnum == "Outer<int>::Mode",
		    "GUI nested type owner arguments or arity markers leaked into source");
		Require(ValueOwner == "ValueOwner<(cast<int>(3i64))>::Inner<string>",
		    "GUI template value arguments were not assigned to their owner");
		Require(GuiSourceType("Outer$arity1.Inner") == "Outer::Inner",
		    "open GUI declaration names must remain comparable without concrete arguments");
		Require(GuiSourceType("array<Outer$arity1.Inner#3:int6:string>[2][3]") ==
		        "array<" + Nested + ">[2][3]",
		    "nested GUI type inside a container array lost its owner arguments");
		Require(TryGetTemplateValueType("bool", Type) && GuiSourceType(Encoded({Type, 1})) == "true" &&
		        GuiSourceType(Encoded({Type, 0})) == "false",
		    "boolean GUI value template arguments");
		Require(TryGetTemplateValueType("uint64", Type) &&
		        GuiSourceType(Encoded({Type, std::numeric_limits<std::uint64_t>::max()})) ==
		            "(cast<uint64>(18446744073709551615u64))",
		    "unsigned GUI template argument lost its range or type");
		Require(TryGetTemplateValueType("int64", Type) &&
		        GuiSourceType(Encoded({Type, std::uint64_t{1} << 63})) ==
		            "(cast<int64>((-9223372036854775807i64-1i64)))",
		    "minimum signed GUI template argument");
		Type = {std::string("private.vs") + '\x1f' + "Demo.Mode", 32, false, false, true, true};
		Require(GuiSourceType(Encoded({Type, std::numeric_limits<std::uint32_t>::max()})) ==
		        "(cast<Demo::Mode>(-1i64))",
		    "private enum GUI template argument");
		Type.Name = std::string("private#types.vs") + '\x1f' + "Outer$arity1.Mode#3:int";
		const auto NestedEnumValue = GuiSourceType(Encoded({Type, 1}));
		Require(NestedEnumValue == "(cast<Outer<int>::Mode>(1i64))",
		    "private nested enum template argument lost its owner arguments");
		for (const auto& Invalid : {std::string("@value:invalid"), Box + "[0]", Box + "[2", Box + "[2]invalid[3]",
		         std::string("Outer$arity2.Inner#3:int"), std::string("Outer$arityx.Inner#3:int"),
		         std::string("Outer$arity1.Inner$arity1.Leaf#3:int")})
		{
			bool bRejected = false;
			try
			{
				GuiSourceType(Invalid);
			}
			catch (const std::runtime_error&)
			{
				bRejected = true;
			}
			Require(bRejected, "invalid canonical GUI template or array type was accepted");
		}
		const auto Compiled = VeroScriptCompile::CompileSource(
		    "template<typename T,int N> struct Box{T data[N];};"
		    "struct Plain{struct Inner{int value;};};"
		    "class Outer<T>{public:struct Inner<U>{T first;U second;};enum class Mode{Ready=1};};"
		    "class A<T>{public:class B<U>{public:struct C<V>{T first;U second;V third;};};};"
		    "template<int N> class ValueOwner{public:struct Inner<U>{U data[N];};};"
		    "template<typename T,T V> struct Scalar{T value=V;};"
		    "class SourceMembers{public:array<" +
		        SourceBox + "> Items;array<" + SourceBox +
		        "> Read(){return Items;}};void main(){"
		        "SourceMembers members=SourceMembers();auto items=members.Read();" +
		        SourceBox +
		        " value=" + SourceBox + "();" + PlainNested + " plain=" + PlainNested + "();" + Nested +
		        " nested=" + Nested + "();" + DeepNested + " deep=" + DeepNested + "();" + ValueOwner +
		        " owned=" + ValueOwner + "();"
		                                 "Scalar<" +
		        NestedEnum + ',' + NestedEnumValue + "> scalar=Scalar<" + NestedEnum + ',' +
		        NestedEnumValue + ">();}",
		    "gui-template-value-source.vs");
		Require(Compiled.bSuccess, "generated GUI template type did not parse: " + Compiled.Error.Message);
	}

	std::shared_ptr<const FGuiSchema> Schema(const std::string& Source, std::vector<FScriptError>& Errors)
	{
		auto Compiled = VeroScriptCompile::CompileSource(Source, "schema-test.vs", Options());
		Require(Compiled.bSuccess && Compiled.PreparedProgram, Compiled.Error.Message);
		return FGuiSchema::Build(*Compiled.PreparedProgram, Errors);
	}

	void CheckDescriptions()
	{
		std::vector<FScriptError> Errors;
		auto Result = Schema(R"vs(
import "attribute.vs";
import "delegate.vs";
namespace VS::Gui { export class Widget {}; }
@gUiWiDgEt(mArK="card", ID="default-card")
export class Card : public VS::Gui::Widget
{
public:
    @GuiMethod(Mark="read-value") int64 ReadValue() const { return 4; }
private:
    @GuiProperty(Mark="value", sTyLe) std::attribute<int64> Value = std::attribute<int64>(4);
    @GuiProperty(Style) float Amount = 0.5f;
    @GuiEvent(Mark="can-close") std::delegate<bool, int64> CanClose;
    @GuiEvent(Mark="changed") std::multicast_delegate<int64> Changed;
};
@GuiWidget(Mark="derived") export class Derived : public Card {};
)vs",
		    Errors);
		Require(Result && Errors.empty(), Errors.empty() ? "missing schema" : Errors.front().Message);
		std::string Error;
		const auto* Card = Result->FindWidget("card", Error);
		Require(Card && Card == Result->FindWidget("Card", Error) && Card->DefaultId == "default-card",
		    "widget annotation, original name or default ID");
		const auto* Value = Result->FindProperty(*Card, "value");
		Require(Value && Value == Result->FindProperty(*Card, "Value") && Value->bBindable && Value->bStyle &&
		        Value->ValueType == "int64" && !Value->DefaultSource.empty(),
		    "attribute metadata or default source");
		const auto* Amount = Result->FindProperty(*Card, "Amount");
		Require(Amount && !Amount->bBindable && Amount->bStyle && Amount->ValueType == "float",
		    "plain properties must stay distinct from Attribute properties");
		const auto* Single = Result->FindEvent(*Card, "can-close");
		const auto* Multi = Result->FindEvent(*Card, "changed");
		Require(Single && Single->Kind == EGuiEventKind::Single && Single->ReturnType == "bool" &&
		        Single->ParameterTypes.size() == 1 && Multi && Multi->Kind == EGuiEventKind::Multicast &&
		        Multi->ReturnType == "void",
		    "delegate kind or signature");
		Require(Single->Dispatch == EEventDispatch::Direct && !Amount->bAnimatable,
		    "style or delegate inferred an unsupported presentation/route mapping");
		const auto* Route = FindEventDefinition("close-requested");
		Require(Route && Route->Dispatch == EEventDispatch::Routed && Route->PhaseMask == 7 &&
		        Route->bCanPreventDefault && Route->DefaultAction == EEventDefaultAction::CloseApplication &&
		        Result->GetRoutedEvents().size() == GetEventDefinitions().size(),
		    "routed event metadata is inconsistent");

		const auto* Derived = Result->FindWidget("derived", Error);
		Require(Derived && Derived->DefaultId.empty() && Result->FindProperty(*Derived, "value") &&
		        Result->FindEvent(*Derived, "changed"),
		    "inherited schema or noninherited default ID");
		Require(Card->Methods.size() == 1 && Card->Methods.front().Mark == "read-value" && Card->Methods.front().bConst,
		    "method identity and signature");
	}

	void CheckResourceReader()
	{
		FCompileProcessingOptions Processing;
		std::string Error;
		Require(ConfigureGuiCompilation(Processing, Error), Error);
		const auto& Descriptors = Processing.Registry->GetProcessors();
		const auto Found = std::find_if(Descriptors.begin(), Descriptors.end(),
		    [](const auto& Item)
		{
			return Item.ProcessorId == "veroscript.gui.markup";
		});
		Require(Found != Descriptors.end(), "markup processor missing");
		FResourceRequest Request{"files", "app", "styles", "主题/layout.vswcss"};
		const auto Identity = MakeResourceDependencyId(Request, "app");
		FSourceDocument Source;
		Source.Id = Source.ModuleId = "resource-page.vswml";
		Source.Text = "<Widget><style src='" + Identity + "' /></Widget>";
		const std::vector<FAnnotationDeclarationView> Declarations;
		const FSourceMetadataSnapshot Metadata;
		FCompileProcessingContext Context{ECompileProcessingPhase::RawSource,
		    ECompileProcessingEvent::Module,
		    0,
		    Source.ModuleId,
		    {},
		    nullptr,
		    nullptr,
		    nullptr,
		    nullptr,
		    &Source,
		    Declarations,
		    Metadata,
		    {},
		    {},
		    nullptr,
		    {},
		    {}};
		Context.Phase = ECompileProcessingPhase::RawSource;
		Context.Event = ECompileProcessingEvent::Module;
		Context.ModuleId = Source.ModuleId;
		Context.Source = &Source;
		Context.ResourceRequester = "app";
		int ResourceReads = 0, FileReads = 0;
		Context.ReadDependency = [&](auto, auto&)
		{
			FileReads++;
			return false;
		};
		Context.ReadResource = [&](const auto& Actual, auto& Result)
		{
			Require(Actual.ProviderId == Request.ProviderId && Actual.ModuleId == Request.ModuleId &&
			        Actual.Group == Request.Group && Actual.Path == Request.Path,
			    "opaque resource identity changed");
			ResourceReads++;
			const std::string Styles = "* { gap: 3px; }";
			Result.Bytes.assign(Styles.begin(), Styles.end());
			Result.bExists = true;
			return true;
		};
		auto Processor = Found->Factory();
		FCompileProcessingOutput Output;
		Processor->Process(Context, Output);
		Require(ResourceReads == 1 && FileReads == 0 && Output.GetDiagnostics().empty() &&
		        Output.GetOperations().size() == 1,
		    "resource-backed CSS was treated as a filesystem path");
	}

	void CheckRejectedDeclarations()
	{
		for (const auto& Member : {"@GuiProperty(Mark=\"value\", MARK=\"again\") int Value;", "@GuiEvent int Changed;",
		         "@GuiProperty static int Value;",
		         "@GuiMethod template<int N> void Act(){}",
		         "@GuiProperty(Mark=\"same\") int First; @GuiProperty(Mark=\"same\") int Second;",
		         "@GuiProperty int Value; int GetValue(){return 0;}",
		         "@GuiProperty int Value; void OnValueChanged(int value){}",
		         "@GuiProperty int Value; static void OnValueChanged(){}",
		         "@GuiProperty int Value; template<int N> void OnValueChanged(){}"})
		{
			std::vector<FScriptError> Errors;
			auto Result = Schema("namespace VS::Gui {export class Widget {}; } @GuiWidget export class Card : "
			                     "public VS::Gui::Widget { public: " +
			        std::string(Member) + "};",
			    Errors);
			Require(!Result && !Errors.empty() && Errors.front().Location.FileLabel == "schema-test.vs",
			    "invalid schema declaration was accepted or lost its origin");
		}
		std::vector<FScriptError> Errors;
		auto Open = Schema("namespace VS::Gui {export class Widget {}; } "
		                   "@GuiWidget template<int N> class Card : public VS::Gui::Widget {};",
		    Errors);
		Require(!Open && !Errors.empty(), "value-only widget template was accepted");
		auto Plain = Schema("namespace VS::Gui {export class Widget {}; } template<int N> class Plain {};", Errors);
		Require(Plain && Errors.empty() && Plain->GetTypes().size() == 1,
		    "unannotated value-only template entered the closed GUI schema");
		const auto Closed = VeroScriptCompile::CompileSource(
		    "namespace VS::Gui {export class Widget {}; } @GuiWidget class Card : public VS::Gui::Widget {};",
		    "schema-test.vs", Options());
		Require(Closed.bSuccess && Closed.PreparedProgram, Closed.Error.Message);
		for (const bool Local : {false, true})
		{
			auto Declarations = Closed.PreparedProgram->GetDeclarations();
			auto Card = std::find_if(Declarations.begin(), Declarations.end(),
			    [](const auto& Item)
			{
				return Item.Target == EAnnotationTarget::Class && Item.Name == "Card";
			});
			Require(Card != Declarations.end(), "missing closed widget declaration");
			Card->bNested = !Local;
			Card->bLocal = Local;
			Require(!FGuiSchema::Build(Declarations, Closed.PreparedProgram->GetSourceMetadata(), Errors) &&
			        !Errors.empty(),
			    "non-namespace GUI widget declaration was accepted");
		}
	}

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
	};

	struct FRecordingProcessor final : ICompileProcessor
	{
		std::unique_ptr<ICompileProcessor> Inner;
		std::vector<std::string>* Sources;
		void Process(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output) override
		{
			Inner->Process(Context, Output);
			for (const auto& Operation : Output.GetOperations())
			{
				Sources->push_back(Operation.Source);
			}
		}
	};

	void CheckGenerated()
	{
		FHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		std::string Error;
		Require(ConfigureGuiCompilation(Config.CompileProcessing, Error), Error);
		Require(ConfigureGuiCompilation(Config.CompileProcessing, Error) &&
		        Config.CompileProcessing.Registry->GetProcessors().size() == 2,
		    "GUI processor registration is not idempotent");
		std::vector<std::string> Generated;
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		for (auto Item : Config.CompileProcessing.Registry->GetProcessors())
		{
			auto Factory = Item.Factory;
			Item.Factory = [&Generated, Factory]
			{
				auto Processor = std::make_unique<FRecordingProcessor>();
				Processor->Inner = Factory();
				Processor->Sources = &Generated;
				return Processor;
			};
			Require(Registry->Register(std::move(Item), Error), Error);
		}
		Registry->Freeze();
		Config.CompileProcessing.Registry = Registry;
		std::string Source = R"vs(
import "attribute.vs";
import "delegate.vs";
namespace VS::Gui
{
	 export class PropertyEvaluation {public:static int64 Epoch=1;static int Depth=0;static int SafeDepth=1;
        static bool NativeSourcesStable=false;};
    namespace Internal { export class NativeEvaluation {public:static VS::Gui::Widget Widget=nullptr;}; }
    export struct NativeStyle
    {
        void RegisterStyleProperty(string name,string mark,int kind,array<string> names,array<int64> values){}
        bool HasStyleValue(string name){return false;}
        int64 GetStyleInt64(string name){return 0;}
        uint64 GetStyleMutationToken(){return 1u64;}
    };
    export class Binding<T> {};
    export class Widget
    {
    public:
        string Identifier;
        array<Widget> Children;
        bool VsguiControlConfigurationDirty=true;
        virtual void VsguiSynchronizeProperties() {}
        virtual void VsguiSynchronizeControl() {}
        virtual bool VsguiHasCustomSynchronization() {return true;}
        virtual bool VsguiCanSuspendSynchronization() {return false;}
        virtual bool VsguiAllowsSourceSnapshot() {return false;}
        virtual void VsguiDisposeProperties() {}
        virtual void VsguiRegisterStyleProperties() {}
        virtual void VsguiPullStyles() {}
        virtual bool VsguiHasInitializationHook() const {return false;}
        bool IsDisposed() const {return false;}
        bool VsguiHasStableBinding(string name) const {return false;}
        bool VsguiClearNotifiedBinding(string name) {return false;}
        void VsguiBind<T>(string name, Binding<T> value, fn(std::attribute<T>)->void install) {}
        void VsguiInvalidate() {}
        NativeStyle __GetNativeHandle(){return NativeStyle();}
        void VsguiPrepareTemplate() {}
        virtual bool VsguiHasMountedHook() const{return false;}
        virtual bool VsguiHasLayoutChangedHook() const{return false;}
        virtual bool VsguiHasPaintHook() const{return false;}
        virtual void VsguiFinalizeConfiguration() {}
        void SetId(string value){Identifier=value;}
        void SetChildren(array<Widget> values){Children=values;}
    };
    export class __NativeWidget
    {
    public:
        static bool IsScriptType(fn()->void callback,string typeName){return true;}
    };
    export class Construction
    {
    public:
        static int Depth;
        static void Begin(){Depth++;}
        static void Complete(Widget value){}
        static void End(bool value){Depth--;}
    };
    export class Subscription
    {
    public:
        bool Active = true;
        optional<fn()->void> Callback;
        bool IsConnected() const {return Active;}
        void VsguiSetDisconnect(fn()->void callback){Callback=optional<fn()->void>(callback);}
        void Disconnect() {if(!Active){return;}Active=false;if(Callback.has_value()){fn()->void cb=Callback.get();Callback=optional<fn()->void>();cb();}}
    };
}
namespace Demo
{
    template<typename T,int N> struct Box { T Data[N]; };
    class Owner<T> { public: struct Item<U> { T First; U Second; }; };
}
@GuiWidget export class QuietBase : public VS::Gui::Widget {};
@GuiWidget export class QuietDerived : public QuietBase {};
int Created = 0;
@GuiWidget(Mark="card")
export class Card : public VS::Gui::Widget
{
public:
    Card() {Created++;}
    int ChangedCount = 0;
    void Fire() {Changed.broadcast(4);}
    void FirePacked(Demo::Box<int,3> value) {Packed.broadcast(value);}
    void FireNested(Demo::Owner<int>::Item<string> value) {NestedPacked.broadcast(value);}
    bool CanClose() {return Closing.execute(9);}
private:
    @GuiProperty(Mark="value", Style) std::attribute<int64> Value = std::attribute<int64>(3);
    @GuiProperty array<Demo::Box<int,3>> Items = array<Demo::Box<int,3>>();
    @GuiProperty array<Demo::Owner<int>::Item<string>> NestedItems = array<Demo::Owner<int>::Item<string>>();
    @GuiEvent std::multicast_delegate<Demo::Box<int,3>> Packed = std::multicast_delegate<Demo::Box<int,3>>();
    @GuiEvent std::multicast_delegate<Demo::Owner<int>::Item<string>> NestedPacked = std::multicast_delegate<Demo::Owner<int>::Item<string>>();
    @GuiEvent std::delegate<bool,int64> Closing = std::delegate<bool,int64>();
    @GuiEvent std::multicast_delegate<int64> Changed = std::multicast_delegate<int64>();
    void OnValueChanged() {ChangedCount++;}
};
int Total = 0;
int GetterReads = 0;
void Handle(int64 value) {Total+=value;}
Card MakeCard() {return Card::Create().Value(8);}
int ReadCard(Card value) {return value.GetValue();}
int main()
{
    ReviewIdentityWidget identity=ReviewIdentityWidget::Create();
    identity.VsguiSynchronizeProperties();
    assert(identity.GetIdentityAmount()==2,"module-private inherited properties were lost");
    identity.SetIdentityAmount(7);
    identity.VsguiSynchronizeProperties();
    assert(identity.GetIdentityAmount()==7,"module-private generated cache identities collided");
    VS::Gui::ProgressBar privateNamedControl=VS::Gui::ProgressBar::Create();
    assert(!privateNamedControl.VsguiCanSuspendSynchronization(),
        "module-private names must not acquire trusted framework synchronization policy");
    QuietBase quietBase=QuietBase::Create();
    QuietDerived quietDerived=QuietDerived::Create();
    assert(!quietBase.VsguiHasCustomSynchronization()&&!quietDerived.VsguiHasCustomSynchronization(),
        "consecutive generated widget caches changed quiet synchronization");
    auto config=Card::Create().Value(5).OnClosing([](int64 value)->bool{return value==9;});
    auto other=config.Value(7);
    Card first=config;
    Card second=other;
    assert(first.GetValue()==5&&second.GetValue()==7,"Builder copies share mutable actions");
    assert(Created==2&&first.CanClose(),"Builder materialization or single event");
    first.VsguiSynchronizeProperties();
    first.VsguiSynchronizeProperties();
    assert(first.ChangedCount==1,"first property synchronization must occur once");
    first.VsguiSetStyleValue(12);
    first.ClearValue();
    assert(first.GetValue()==12,"ClearValue must fall back to CSS");
    first.VsguiClearStyleValue();
    assert(first.GetValue()==3,"clearing CSS must retain type default");
    std::attribute<int64> binding=std::attribute<int64>();
    binding.bind([]()->int64{return Total;});
    first.SetValue(binding);
    Total=20;
    first.VsguiSynchronizeProperties();
    assert(first.GetValue()==20&&first.ChangedCount==2,"Attribute binding or synchronization");
    first.SetValue(30);
    Total=40;
    assert(first.GetValue()==30,"literal setter must replace getter");
    std::attribute<int64> cached=std::attribute<int64>();
    cached.bind([]()->int64{GetterReads++;return 5;});
    first.SetValue(cached);
    assert(first.GetValue()==5&&first.GetValue()==5&&GetterReads==1,"getter was evaluated repeatedly in one round");
    VS::Gui::PropertyEvaluation::Epoch++;
    assert(first.GetValue()==5&&GetterReads==2,"getter cache did not advance to the next round");
    std::attribute<int64> broken=std::attribute<int64>();
    broken.bind([]()->int64{throw "getter failure";return 0;});
    first.SetValue(broken);
    bool caught=false;
    try{first.GetValue();}catch(string failure){caught=true;}
    first.SetValue(30);
    assert(caught&&first.GetValue()==30,"getter failure did not restore evaluation guards");
    auto connection=first.AddChanged(Handle);
    auto another=first.AddChanged(Handle);
    first.Fire();
    assert(Total==48,"multicast additions");
    connection.Disconnect();
    first.Fire();
    assert(Total==52,"exact subscription disconnect");
    first.RemoveChanged(Handle);
    first.Fire();
    assert(Total==52&&!another.IsConnected(),"remove earliest active handler");
    auto pending=first.AddChanged(Handle);
    first.ClearChanged();
    assert(!pending.IsConnected(),"clear must invalidate connection handles");
    auto token=VS::Gui::Subscription();
    first.AddChanged([&token](int64 value)->void{token.Disconnect();});
    token=first.AddChanged(Handle);
    first.Fire();
    assert(Total==56,"disconnect changed the current multicast snapshot");
    first.Fire();
    assert(Total==56,"disconnect did not update the next multicast snapshot");
    VS::Gui::Widget asBase=Card::Create().Value(9);
    array<VS::Gui::Widget> children={Card::Create().Value(10)};
    assert(MakeCard().GetValue()==8&&ReadCard(Card::Create().Value(11))==11,"return and argument conversion");
    array<Demo::Box<int,3>> items={Demo::Box<int,3>()};
    items[0].Data[0]=42;
    first.SetItems(items);
    int packedValue=0;
    auto packedSubscription=first.AddPacked([&packedValue](Demo::Box<int,3> value)->void{packedValue=value.Data[0];});
    first.FirePacked(first.GetItems()[0]);
    assert(packedValue==42,"closed GUI template property or event signature");
    array<Demo::Owner<int>::Item<string>> nestedItems={Demo::Owner<int>::Item<string>()};
    nestedItems[0].First=7;
    nestedItems[0].Second="nested";
    first.SetNestedItems(nestedItems);
    int nestedValue=0;
    auto nestedSubscription=first.AddNestedPacked([&nestedValue](Demo::Owner<int>::Item<string> value)->void
    {
        assert(value.Second=="nested","nested GUI event value lost its own type argument");
        nestedValue=value.First;
    });
    first.FireNested(first.GetNestedItems()[0]);
    assert(nestedValue==7,"nested GUI template property or event signature");
    return 0;
}
)vs";
		// Distinct module-private types can share a spelling and occur in the same
		// inheritance chain. Generated state and helpers need their complete identity.
		const auto FrameworkEnd = Source.find("namespace Demo");
		Require(FrameworkEnd != std::string::npos, "missing synthetic framework boundary");
		const auto FrameworkPath = PathUtil::ToUtf8(std::filesystem::absolute("__gui_schema__/framework.vs"));
		const auto BasePath = PathUtil::ToUtf8(std::filesystem::absolute("__gui_schema__/private-base.vs"));
		Config.SourceOverlays.push_back({FrameworkPath, Source.substr(0, FrameworkEnd)});
		Config.SourceOverlays.push_back({BasePath, "import \"attribute.vs\";import " + GuiQuote(FrameworkPath) + R"vs(;
@GuiWidget(Mark="review-private-base") class ReviewIdentityWidget : public VS::Gui::Widget
{
public:
    @GuiProperty(Style) std::attribute<int64> IdentityAmount=std::attribute<int64>(2);
};
@GuiWidget export class ReviewIdentityBridge : public ReviewIdentityWidget {};
)vs"});
		Source = "import \"attribute.vs\";import \"delegate.vs\";import " + GuiQuote(FrameworkPath) + ";import " + GuiQuote(BasePath) + R"vs(;
@GuiWidget(Mark="review-private-derived") class ReviewIdentityWidget : public ReviewIdentityBridge {};
namespace VS::Gui
{
    @GuiWidget(Mark="review-private-progress") class ProgressBar : public Widget
    {
    protected:
        void VsguiSynchronizeControl() override {}
    };
}
)vs" + Source.substr(FrameworkEnd);
		Session.ApplyConfig(Config);
		auto Result = Session.LoadSource(Source, "gui-generated-test.vs");
		if (!Result.bSuccess)
		{
			for (const auto& Text : Generated)
			{
				std::cerr << Text << '\n';
			}
		}
		Require(Result.bSuccess, Result.Error.Message);
		Result = Session.Run();
		Require(Result.bSuccess && Result.ExitCode == 0, Result.Error.Message);
	}
} // namespace

int main()
{
	try
	{
		Require(IsGuiScriptLibraryPath(GetGuiScriptLibraryPath()) &&
		        IsGuiScriptLibraryPath("/saved-build/__veroscript_gui__/Gui/Resources.vs") &&
		        !IsGuiScriptLibraryPath("/saved-build/__veroscript_gui__/Gui/Custom.vs") &&
		        !IsGuiScriptLibraryPath("/saved-build/my__veroscript_gui__/Gui/Resources.vs"),
		    "saved framework module classification depends on the current working directory");
		CheckTemplateValueSourceTypes();
		CheckDescriptions();
		CheckResourceReader();
		CheckRejectedDeclarations();
		CheckGeneratedScopeIdentity();
		CheckGenerated();
		std::cout << "GUI schema names, types, inheritance and event signatures passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}

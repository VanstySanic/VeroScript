// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptGuiScript.h"
#include "VeroScriptGuiTools.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptGuiContext.h"
#include "VeroScriptSession.h"
#include "VeroScriptPath.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <map>
#include <algorithm>

namespace
{
	using namespace VeroScript;
	using namespace VeroScript::Gui;
	void Require(bool Value, const std::string& Error)
	{
		if (!Value)
		{
			throw std::runtime_error(Error);
		}
	}
	struct FHost final : IEngineHost
	{
		void OnPrintLine(std::string_view) override
		{
		}
		void OnDiagnosticLine(std::string_view Text) override
		{
			std::cerr << Text << '\n';
		}
	};
	struct FBackend final : IBackendContext
	{
		FCapabilities GetCapabilities() const noexcept override
		{
			return {true};
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
		bool Render(bool& Presented, std::string&) override
		{
			Presented = false;
			return true;
		}
		bool Close(std::string&) override
		{
			return true;
		}
		std::vector<FBackendNode> Nodes;
	};
} // namespace

int main(int Argc, char** Argv)
{
	try
	{
		const std::string Scenario = Argc > 1 ? Argv[1] : "All";
		Require(Argc <= 2 && (Scenario == "All" || Scenario == "Runtime" || Scenario == "ScriptDiagnostics" || Scenario == "ExpressionDiagnostics" || Scenario == "Artifact"),
		    "unknown GUI generation scenario: " + Scenario);
		const auto Selected = [&](const char* Name)
		{
			return Scenario == "All" || Scenario == Name;
		};
		std::cout << "GUI generation scenario: " << Scenario << std::endl;
		FHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		std::string Error;
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		const auto Directory = std::filesystem::absolute("__gui_markup_generation__");
		const auto Base = PathUtil::ToUtf8(Directory / "base.vs");
		const auto Card = PathUtil::ToUtf8(Directory / "card.vswml");
		const auto Main = PathUtil::ToUtf8(Directory / "main.vs");
		std::map<std::string, std::string> Sources;
		Sources[Base] = R"vs(
import "delegate.vs";
export enum class Choice { First, Second };
@GuiWidget export class MountProbe : public VS::Gui::Widget
{
public:
    MountProbe(){auto owner=weak<MountProbe>(this);auto source=std::attribute<double>();source.bind([owner]()->double{return owner.lock().Counter;});SetGap(source);}
    double Counter=1;
protected:
    void OnMounted(){Counter=9;}
};
export struct Measure
{
public:
    double Value=0;
    Measure(int value){Value=value;}
    bool operator ==(Measure other) const{return Value==other.Value;}
};
export Measure ParseMeasure(string source)
{
    if(source=="8cm"){return Measure(80);}
    if(source=="12cm"){return Measure(120);}
    throw "Invalid measurement";
}
@GuiWidget export class ScriptBase : public VS::Gui::Widget
{
public:
    bool Ask(){return Confirm.execute(7i64);}
private:
    @GuiEvent std::delegate<bool,int64> Confirm=std::delegate<bool,int64>();
    @GuiProperty(Style) std::attribute<double> Amount = std::attribute<double>(1);
    @GuiProperty(Style, Mark="counter") int64 Counter = 0;
    @GuiProperty(Style) uint64 Unsigned = 0;
    @GuiProperty(Style) Choice Selection = Choice::First;
    @GuiProperty(Style) string Caption = "default";
    @GuiProperty(Style) VS::Gui::Length Extent = VS::Gui::Length(2);
    @GuiProperty(Style) Measure Measurement = Measure(1);
};
)vs";
		Sources[Card] = R"ml(<component type="Demo::Card" base="ScriptBase" mark="card">
    <import src="base.vs" />
    <script><![CDATA[
    private:
        @GuiProperty(Style) std::attribute<double> Extra = std::attribute<double>(2);
    public:
        @GuiMethod void Bump() { SetAmount(GetAmount() + 1); }
        VS::Gui::Widget GetPart() { return elements.Require("part"); }
    ]]></script>
    <style src="card.vswcss" />
    <template><Widget id="part" class="internal" bind:gap="{ GetAmount() + GetExtra() }" on:pointer-down="Bump" /></template>
</component>)ml";
		Sources[PathUtil::ToUtf8(Directory / "card.vswcss")] =
		    "card { Amount: 11; Extra: 4; counter: 9007199254740993; Unsigned: 18446744073709551615; Selection: "
		    "Second; Caption: 'style text'; Extent: 17px; Measurement: 8cm; } .internal { gap: 19; }";
		Sources[PathUtil::ToUtf8(Directory / "logic.vs")] = R"vs(
export int ExternalCreated = 0;
namespace Helpers
{
    export template<typename T> T Identity(T value){return value;}
}
export class Logic
{
public:
    Logic(){ExternalCreated++;}
    double Count = 4;
    @GuiMethod double Read() const {return Count;}
    @GuiMethod void Increase(){Count++;}
};
)vs";
		const auto External = PathUtil::ToUtf8(Directory / "external.vswml");
		Sources[External] = R"ml(
<Widget>
  <script src="logic.vs" type="Logic" />
  <Widget id="external" bind:gap="{ Count }" on:pointer-down="{ double Count=1; context.Count+=Count; }" />
  <Widget id="capture" bind:gap="{ ([Count = Count + 1]()->double { return Count; })() }" />
  <Widget id="callable" bind:gap="{ ([context, read = Helpers::Identity&lt;double&gt;]()->double { return read(Count); })() }" />
</Widget>)ml";
		Sources[PathUtil::ToUtf8(Directory / "injected.vswml")] = R"ml(<Widget>
    <Widget bind:gap="{ Count }" />
    <Widget id="capture" bind:gap="{ ([Count = Count + 1]()->double { return Count; })() }" />
    <Widget id="callable" bind:gap="{ ([context, read = Helpers::Identity&lt;double&gt;]()->double { return read(Count); })() }" />
</Widget>)ml";
		Sources[PathUtil::ToUtf8(Directory / "values.vswml")] =
		    "<ScriptBase Counter=\"9007199254740993\" Unsigned=\"18446744073709551615\" Selection=\"Second\" "
		    "Measurement=\"12cm\"><import src=\"base.vs\"/></ScriptBase>";
		Sources[PathUtil::ToUtf8(Directory / "inline.vswml")] = R"ml(
<Widget>
  <import src="base.vs" />
  <script><![CDATA[
    private: double Count=8;
    public:
      @GuiMethod optional<double> Read() const{return optional<double>(Count);}
      @GuiMethod void Increase(){Count++;}
      @GuiMethod bool Approve(int64 value){return value==7;}
      @GuiMethod bool Approve(string value){return false;}
  ]]></script>
  <ScriptBase id="value" bind:gap="Read" on:pointer-down="Increase" on:Confirm="Approve" />
</Widget>)ml";
		for (const auto& Source : Sources)
		{
			Config.SourceOverlays.push_back({Source.first, Source.second});
		}
		Config.CompileProcessing.ReadDependency = [Sources](auto Name, auto& Source)
		{
			const auto Found = Sources.find(std::string(Name));
			if (Found == Sources.end())
			{
				return false;
			}
			Source = Found->second;
			return true;
		};
		FScriptServices Services;
		Services.CreateContext = [](const auto&, const auto& Viewport, auto& Failure)
		{
			return FContext::Create(std::make_unique<FBackend>(), Viewport, Failure);
		};
		FGuiCompilationConfiguration GuiConfiguration;
		GuiConfiguration.ValueCodecs.push_back({"Measure", "ParseMeasure", "ParseMeasure"});
		GuiConfiguration.ResourceBindings.push_back(
		    {PathUtil::ToUtf8(Directory / "injected.vswml"), PathUtil::ToUtf8(Directory / "logic.vs"), "Logic"});
		Require(RegisterScriptBindings(Session.GetBindingRegistry(), std::move(Services), Error) &&
		        ConfigureGuiScriptEngine(Config, GuiConfiguration, Error),
		    Error);
		Session.ApplyConfig(Config);
		if (Selected("Runtime"))
		{
			const auto Loaded = Session.LoadSource(R"vs(
import "card.vswml";
import "logic.vs";
import "external.vswml";
import "inline.vswml";
import "injected.vswml";
import "values.vswml";
using namespace VS::Gui;
@GuiWidget export class Derived : public Demo::Card {};
void main()
{
    auto externalFactory=VS::Gui::LoadFactory<VS::Gui::Widget>("external.vswml");
    Logic logic=Logic();
    logic.Count=20;
    bool conflictingContext=false;
    try{externalFactory.Create(logic);}catch(string failure){conflictingContext=true;}
    assert(conflictingContext && ExternalCreated==1,"declared and provided binding contexts were combined");
    auto providedFactory=VS::Gui::LoadFactory<VS::Gui::Widget>("injected.vswml");
    VS::Gui::Widget external=providedFactory.Create(logic);
    assert(ExternalCreated==1 && external.GetChild(0).GetGap()==20, "provided context was not used or was constructed twice");
    assert(external.GetChild(1).GetGap()==21, "lambda capture initializer or its local alias was not qualified correctly");
    assert(external.GetChild(2).GetGap()==20, "ML template function capture lost its enclosing context");
    VS::Gui::Widget defaultExternal=externalFactory.Create();
    assert(ExternalCreated==2 && defaultExternal.GetChild(0).GetGap()==4, "default external context was not constructed");
    logic.Count=25;
    external.Synchronize();
    assert(external.GetChild(0).GetGap()==25 && defaultExternal.GetChild(0).GetGap()==4, "external page contexts were shared");
    assert(external.GetChild(1).GetGap()==26 && defaultExternal.GetChild(1).GetGap()==5,
        "lambda capture initializer stopped following its own page context");
    assert(external.GetChild(2).GetGap()==25 && defaultExternal.GetChild(2).GetGap()==4,
        "ML template function capture stopped following its own dynamic context");
    auto inlineFactory=VS::Gui::LoadFactory<VS::Gui::Widget>("inline.vswml");
    VS::Gui::Widget inlinePage=inlineFactory.Create();
    VS::Gui::Widget secondInline=inlineFactory.Create();
    assert(inlinePage.GetChild(0).GetGap()==8 && secondInline.GetChild(0).GetGap()==8, "private inline page state was not retained");
    assert(cast<ScriptBase>(inlinePage.GetChild(0)).Ask(), "custom returning event or exact overload selection failed");
    auto componentFactory=VS::Gui::LoadFactory<Demo::Card>("card.vswml");
    Demo::Card component=componentFactory.Create();
    assert(component.GetPart().GetGap()==3, "component resource factory did not preserve its concrete type");
    auto injectedFactory=VS::Gui::LoadFactory<VS::Gui::Widget>("injected.vswml");
    VS::Gui::Widget injected=injectedFactory.Create(logic);
    assert(injected.GetChild(0).GetGap()==25, "resource manifest context was not passed to a script-less page");
    bool missingContext=false;
    try{injectedFactory.Create();}catch(string failure){missingContext=true;}
    assert(missingContext, "required context was silently constructed");
    auto valuesFactory=VS::Gui::LoadFactory<ScriptBase>("values.vswml");
    ScriptBase values=valuesFactory.Create();
    assert(values.GetCounter()==9007199254740993i64 && values.GetUnsigned()==18446744073709551615u64 &&
        values.GetSelection()==Choice::Second && values.GetMeasurement().Value==120, "ML value codecs lost precision or type information");
    Derived first = Derived::Create().Amount(5).Extra(3);
    Demo::Card second = Demo::Card::Create().Amount(10);
    assert(first.GetPart().GetGap() == 8 && second.GetPart().GetGap() == 12, "ML typed properties or initial binding");
    MountProbe mounted=MountProbe::Create();
    array<VS::Gui::Widget> children = {first, second,mounted};
    VS::Gui::Widget root = VS::Gui::Widget::Create()[children];
    assert(root.GetChildCount() == 3, "component name scopes collided");
    first.SetAmount(7);
    root.Synchronize();
    assert(first.GetPart().GetGap() == 10 && second.GetPart().GetGap() == 12, "ML instances shared state or became stale");
    VS::Gui::Context gui = VS::Gui::Context("test", 100, 100);
    VS::Gui::Widget loadedPage=gui.LoadWidget("inline.vswml");
    VS::Gui::Widget loadedWithContext=gui.LoadWidget("injected.vswml",logic);
    assert(loadedPage.GetChild(0).GetGap()==8 && loadedWithContext.GetChild(0).GetGap()==25, "context convenience loaders lost caller origin or context");
    gui.SetRoot(root);
    gui.Update(0);
    assert(mounted.GetGap()==9,"plain state changed during mount was not synchronized before first layout");
    assert(first.GetCounter() == 9007199254740993i64 && first.GetUnsigned() == 18446744073709551615u64 &&
        first.GetSelection() == Choice::Second && first.GetCaption() == "style text" && first.GetExtent() == VS::Gui::Length(17),
        "typed CSS codec or integer precision");
    assert(first.GetMeasurement().Value==80, "custom CSS value codec was not called");
    assert(first.GetAmount() == 7 && first.GetExtra() == 3 && second.GetAmount() == 10 && second.GetExtra() == 4,
        "CSS overwrote local values or failed to supply a type default");
    first.ClearAmount();
    first.ClearExtra();
    gui.Update(1);
    assert(first.GetAmount() == 11 && first.GetExtra() == 4 && first.GetPart().GetGap() == 15,
        "custom CSS did not feed generated bindings before layout");
    first.GetPart().ClearGap();
    gui.Update(2);
    assert(first.GetPart().GetGap() == 19, "native CSS fallback after clearing a binding");
    first.GetPart().SetClasses("");
    gui.Update(3);
    assert(first.GetPart().GetGap() == 0 && second.GetPart().GetGap() == 14, "class change or instance isolation");
    gui.SetTheme("theme.vswcss", "Widget { gap: 23px; } :root { --theme-gap: 31px; } #theme-only { gap: var(--theme-gap); }");
    VS::Gui::Widget themeOnly=VS::Gui::Widget::Create().Id("theme-only");
    root.AddChild(themeOnly);
    gui.Update(4);
    assert(themeOnly.GetGap()==31 && first.GetPart().GetGap()==23 && second.GetPart().GetGap()==14,
        "context theme did not reach declarations without styles or overwrote a local stylesheet");
    themeOnly.SetGap(6);
    gui.Update(5);
    assert(themeOnly.GetGap()==6,"theme overwrote a local value");
    themeOnly.ClearGap();
    gui.ClearTheme();
    gui.Update(6);
    assert(themeOnly.GetGap()==0 && first.GetPart().GetGap()==0,"clearing a theme retained stale style values");
    gui.Close();
}
)vs",
			    Main);
			Require(Loaded.bSuccess, Loaded.Error.Message);
			const auto Run = Session.Run();
			Require(Run.bSuccess, Run.Error.Message);
			std::cout << "GUI generation: runtime assertions checked" << std::endl;
		}
		const auto BadPath = PathUtil::ToUtf8(Directory / "bad.vswml");
		const std::string BadMarkup =
		    "<Widget>\n<script><![CDATA[\npublic:\n int Broken(){return UndefinedName;}\n]]></script>\n</Widget>";
		auto BadConfig = Config;
		BadConfig.SourceOverlays.push_back({BadPath, BadMarkup});
		VeroScriptSession Broken(Host);
		VeroScript::Test::Configure(Broken);
		Require(RegisterScriptBindings(Broken.GetBindingRegistry(), Error), Error);
		Broken.ApplyConfig(BadConfig);
		if (Selected("ScriptDiagnostics"))
		{
			const auto Bad = Broken.LoadSource("import \"bad.vswml\";void main(){}", Main);
			Require(!Bad.bSuccess && Bad.Error.Location.FileLabel == BadPath && Bad.Error.Location.Line == 4 &&
			        Bad.Error.Location.Column == 22,
			    "inline script diagnostic did not retain its original ML line and column: " + Bad.Error.Message);
			const auto SyntaxPath = PathUtil::ToUtf8(Directory / "bad-syntax.vswml");
			BadConfig.SourceOverlays.push_back(
			    {SyntaxPath, "<Widget>\n<script><![CDATA[\npublic:\n int Broken(){return (1+);}\n]]></script>\n</Widget>"});
			Broken.ApplyConfig(BadConfig);
			const auto Syntax = Broken.LoadSource("import \"bad-syntax.vswml\";void main(){}", Main);
			Require(!Syntax.bSuccess && Syntax.Error.Location.FileLabel == SyntaxPath && Syntax.Error.Location.Line == 4 &&
			        Syntax.Error.Location.Column > 20,
			    "inline script syntax diagnostic lost its ML source position: " + Syntax.Error.Message);
		}
		const auto CheckMapped = [&](const std::string& Name, const std::string& Source)
		{
			std::cout << "GUI generation: checking " << Name << std::endl;
			const auto Path = PathUtil::ToUtf8(Directory / Name);
			auto Next = Config;
			Next.SourceOverlays.push_back({Path, Source});
			Broken.ApplyConfig(Next);
			const auto Result = Broken.LoadSource("import \"" + Name + "\";void main(){}", Main);
			const auto Offset = Source.find("Missing");
			const auto LineStart = Source.rfind('\n', Offset);
			const auto Column = Offset - (LineStart == std::string::npos ? 0 : LineStart + 1) + 1;
			const auto Line = 1 + std::count(Source.begin(), Source.begin() + Offset, '\n');
			Require(!Result.bSuccess && Result.Error.Location.FileLabel == Path && Result.Error.Location.Line == Line &&
			        Result.Error.Location.Column == Column,
			    "decoded ML expression lost its original position: " + Result.Error.Message + " at " +
			        std::to_string(Result.Error.Location.Line) + ':' +
			        std::to_string(Result.Error.Location.Column));
		};
		if (Selected("ExpressionDiagnostics"))
		{
			CheckMapped("bad-expression.vswml", "<Widget\n bind:gap=\"{ 1 &lt; 2 ? Missing : 0 }\" />");
			CheckMapped("bad-event.vswml", "<Widget\n on:pointer-down=\"{ if(1 &lt; 2){ Missing(); } }\" />");
			CheckMapped("bad-external-expression.vswml", "<Widget>\n<script src=\"logic.vs\" type=\"Logic\"/>\n"
			                                             "<Widget bind:gap=\"{ Count + Missing }\"/></Widget>");
		}
		if (Selected("ScriptDiagnostics"))
		{
			CheckMapped(
			    "bad-entity-script.vswml",
			    "<Widget>\n<script>public: int Broken(){if(1 &lt; 2){return Missing;}return 0;}</script>\n</Widget>");
		}
		if (Selected("Artifact"))
		{
			FCompileOptions Offline;
			VeroScript::Test::Configure(Offline);
			Offline.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
			Offline.SourceOverlays = Config.SourceOverlays;
			Offline.CompileProcessing.ReadDependency = Config.CompileProcessing.ReadDependency;
			auto Checked = CompileGuiFile(PathUtil::ToUtf8(Directory / "inline.vswml"), Offline, GuiConfiguration);
			Require(Checked.bSuccess && Checked.PreparedProgram, "offline ML check failed: " + Checked.Error.Message);
			std::cout << "GUI generation: offline ML checked, compiling artifact" << std::endl;
			auto Artifact = CompileGuiSource(R"vs(
import "inline.vswml";
import "card.vswml";
import "external.vswml";
void main()
{
    VS::Gui::Widget root=VS::Gui::LoadFactory<VS::Gui::Widget>("inline.vswml").Create();
    assert(root.GetChild(0).GetGap()==8,"baked GUI factory lost its script or binding");
    VS::Gui::Widget external=VS::Gui::LoadFactory<VS::Gui::Widget>("external.vswml").Create();
    assert(external.GetChild(2).GetGap()==4,"baked ML template function capture was lost");
    Demo::Card card=VS::Gui::LoadFactory<Demo::Card>("card.vswml").Create();
    VS::Gui::Context gui=VS::Gui::Context("test",100,100);
    gui.SetRoot(card);gui.Update(0);
    assert(card.GetCaption()=="style text" && card.GetMeasurement().Value==80 && card.GetPart().GetGap()==15,
        "source-free stylesheet, relative module base or custom codec was lost");
    gui.Close();
}
)vs",
			    PathUtil::ToUtf8(Directory / "deployed.vs"), Offline, GuiConfiguration);
			Require(Artifact.bSuccess, Artifact.Error.Message);
			FPackageBuildOptions Package;
			Package.PackageName = "gui-framework-test";
			Package.bStripDebugInfo = true;
			std::vector<std::uint8_t> Bytes;
			FGuiAssetOptions Assets;
			Assets.Resources = {PathUtil::ToUtf8(Directory / "asset.png"), PathUtil::ToUtf8(Directory / "font.ttf")};
			Assets.ReadResource = [](const auto&, auto& Data, auto&)
			{
				Data = {1, 2, 3, 4, 5};
				return true;
			};
			Require(BakeGuiApplication(Artifact, Package, Assets, Bytes, Error), Error);
			const auto LocalPath = PathUtil::ToUtf8(Directory);
			Require(std::search(Bytes.begin(), Bytes.end(), LocalPath.begin(), LocalPath.end()) == Bytes.end(),
			    "release GUI artifact retained its development resource directory");
			FPackageData Deployed;
			Require(ReadPackage({Bytes.data(), Bytes.size()}, Deployed, Error), Error);
			VeroScriptSession Runtime(Host);
			VeroScript::Test::Configure(Runtime);
			FScriptServices ArtifactServices;
			ArtifactServices.CreateContext = [](const auto&, const auto& Viewport, auto& Failure)
			{
				return FContext::Create(std::make_unique<FBackend>(), Viewport, Failure);
			};
			Require(RegisterGuiArtifactBindings(Runtime.GetBindingRegistry(), Deployed, std::move(ArtifactServices), Error),
			    Error);
			FScriptServices AssetProbe;
			Require(ConfigureGuiArtifactServices(Deployed, AssetProbe, Error), Error);
			std::string AssetIdentity;
			std::vector<std::uint8_t> Embedded;
			Require(
			    AssetProbe.ResolveResourceReference(Deployed.Module->EntryModulePath, "asset.png", AssetIdentity, Error) &&
			        AssetProbe.ReadResource(AssetIdentity, Embedded, Error) &&
			        Embedded == std::vector<std::uint8_t>({1, 2, 3, 4, 5}),
			    "relocated asset bytes: " + Error);
			auto PackageLoaded = Runtime.LoadPackage(Deployed, "relocated/gui.vsapp");
			Require(PackageLoaded.bSuccess, PackageLoaded.Error.Message);
			auto Executed = Runtime.Run();
			Require(Executed.bSuccess, "source-free GUI artifact failed: " + Executed.Error.Message);
		}
		std::cout << "GUI ML imports, script inheritance and generated bindings passed\n";
		return 0;
	}
	catch (const std::exception& Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}

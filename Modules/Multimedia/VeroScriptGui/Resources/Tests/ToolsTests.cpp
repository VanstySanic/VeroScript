// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptGuiTools.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptSession.h"
#include "VeroScriptPath.h"
#include "VeroScriptLocale.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace
{
	using namespace VeroScript;
	using namespace VeroScript::Gui;
	void Require(bool Condition, const std::string& Error)
	{
		if (!Condition)
		{
			throw std::runtime_error(Error);
		}
	}
	class FCommands final : public ICommandLineExtensionService
	{
	public:
		bool Register(FCommandLineModule Input, std::string&) const override
		{
			Module = std::move(Input);
			return true;
		}
		mutable FCommandLineModule Module;
	};
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
	class FCallerProcessor final : public ICompileProcessor
	{
	public:
		void Process(const FCompileProcessingContext&, FCompileProcessingOutput&) override
		{
		}
	};
	void CheckBackendSelection(const std::filesystem::path& Directory)
	{
		const auto Entry = Directory / "backend-entry.vs";
		const auto Extension = PathUtil::ToUtf8(Directory / "backend-selection.vs");
		std::ofstream(Entry) << "int main(){return 0;}";
		const auto Factory = VeroScript::Test::VmFactory();
		const char* Library = std::getenv("VEROSCRIPT_TEST_VM_BACKEND");
		#ifdef VEROSCRIPT_TEST_VM_LIBRARY
		if (!Library)
		{
			Library = VEROSCRIPT_TEST_VM_LIBRARY;
		}
		#endif
		Require(Library && *Library, "backend selection requires a real VM library");
		const auto Check = [&](FEngineConfig Engine, const std::string& Expected)
		{
			Engine.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
			Engine.ScriptCompileExtensions.Roots = {{Extension, {}}};
			// Stop at the first source callback, before compiling the full GUI graph.
			Engine.SourceOverlays = {{Extension, R"VS(
import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
 out.report_diagnostic("gui-vm-selection-reached",0);
};}
export void register_extension(std::compiler::registration reg){
 std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
 std::compiler::processor_definition d=std::compiler::processor_definition();
 d.id="gui.vm.selection";d.version="1";d.phases=array<std::compiler::phase>{std::compiler::phase::raw_source};
 d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);
}
)VS"}};
			FToolOptions Input;
			Input.Engine = std::move(Engine);
			auto Service = CreateCommandLineService(std::move(Input));
			auto Registration = Service->CreateRegistration();
			std::ostringstream Output, Errors;
			FCommandLineContext Context;
			Context.WorkingDirectory = PathUtil::ToUtf8(Directory);
			Context.Output = &Output;
			Context.Errors = &Errors;
			std::string Error;
			Require(Registration.Module.Configure(Context, {}, Registration.Module.UserData, Error), Error);
			const auto& Command = Registration.Module.Commands.at(0);
			const auto Code = Command.Run(Context, {PathUtil::ToUtf8(Entry)}, Command.UserData);
			const auto Message = Expected == "diag.vm_backend_required" ? Locale::FormatEnglish(Expected) : Expected;
			Require(Code == 1 && Errors.str().find(Message) != std::string::npos,
			    "GUI compiler VM selection expected " + Message + ": " + Errors.str());
		};
		FEngineConfig Fallback;
		Fallback.FallbackVmBackendFactory = Factory;
		Check(Fallback, "gui-vm-selection-reached");
		Fallback.FallbackVmBackendFactory.reset();
		Fallback.FallbackVmBackendPath = Library;
		Check(Fallback, "gui-vm-selection-reached");
		FEngineConfig Explicit;
		Explicit.VmBackendFactory = Factory;
		Explicit.FallbackVmBackendPath = "fallback-missing-vm";
		Check(Explicit, "gui-vm-selection-reached");
		Explicit = {};
		Explicit.FallbackVmBackendFactory = Factory;
		Explicit.VmBackendPath = Library;
		Check(Explicit, "gui-vm-selection-reached");
		Explicit.VmBackendPath = "explicit-missing-vm";
		Check(Explicit, "explicit-missing-vm");
		Explicit.ScriptCompileExtensions.VmBackendFactory = Factory;
		Check(Explicit, "gui-vm-selection-reached");
		Explicit.ScriptCompileExtensions.VmBackendFactory.reset();
		Explicit.ScriptCompileExtensions.VmBackendPath = "extension-missing-vm";
		Check(Explicit, "extension-missing-vm");
		Fallback.bClearVmBackend = true;
		Check(Fallback, "diag.vm_backend_required");
		FEngineConfig Controlled;
		Controlled.VmBackendFactory = Factory;
		Controlled.ExecutionControl = std::make_shared<FExecutionControl>();
		Controlled.ExecutionControl->RequestCancel();
		Check(Controlled, "compilation extension cancelled");
		Controlled.ScriptCompileExtensions.ExecutionControl = std::make_shared<FExecutionControl>();
		Check(Controlled, "gui-vm-selection-reached");
		Controlled.ExecutionControl->Reset();
		Controlled.ScriptCompileExtensions.ExecutionControl->RequestCancel();
		Check(Controlled, "compilation extension cancelled");
	}
} // namespace
int main(int Argc, char** Argv)
{
	try
	{
		std::string Scenario = "All", ModulePath;
		for (int Index = 1; Index < Argc; ++Index)
		{
			const std::string Argument = Argv[Index];
			if (Argument == "--scenario")
			{
				Require(++Index < Argc, "--scenario requires All, Content, Check, Artifact, Host, SourceImports, SourceDetection or BackendSelection");
				Scenario = Argv[Index];
			}
			else
			{
				Require(ModulePath.empty(), "expected at most one native GUI tools module path");
				ModulePath = Argument;
			}
		}
		Require(Scenario == "All" || Scenario == "Content" || Scenario == "Check" || Scenario == "Artifact" ||
		        Scenario == "Host" || Scenario == "SourceImports" || Scenario == "SourceDetection" || Scenario == "BackendSelection",
		    "unknown GUI tools scenario: " + Scenario);
		const auto Selected = [&](const char* Name)
		{
			return Scenario == "All" || Scenario == Name;
		};
		if (Selected("Content"))
		{
			std::cout << "GUI tools content compilation and resources\n"
			          << std::flush;
			FCompileOptions ContentOptions;
			VeroScript::Test::Configure(ContentOptions);
			ContentOptions.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
			const auto Content = PathUtil::FromUtf8(__FILE__).parent_path().parent_path() / "Examples/Content/main.vs";
			const auto ContentCompilation = CompileGuiFile(PathUtil::ToUtf8(Content), ContentOptions);
			Require(ContentCompilation.bSuccess, "content example compilation: " + ContentCompilation.Error.Message);
			FGuiAssetOptions Assets;
			Assets.Resources.push_back(PathUtil::ToUtf8(Content.parent_path() / "sample.tga"));
			FPackageBuildOptions PackageOptions;
			PackageOptions.PackageName = "gui-content";
			PackageOptions.bStripDebugInfo = true;
			std::vector<std::uint8_t> ContentBytes;
			std::string ContentError;
			Require(BakeGuiApplication(ContentCompilation, PackageOptions, Assets, ContentBytes, ContentError),
			    ContentError);
			FPackageData Package;
			Require(ReadPackage({ContentBytes.data(), ContentBytes.size()}, Package, ContentError), ContentError);
			FScriptServices AssetServices;
			AssetServices.ReadResource = [](const auto& Identity, auto& Data, auto&)
			{
				Data = {9, 8, 7};
				return Identity == "memory:host";
			};
			Require(ConfigureGuiArtifactServices(Package, AssetServices, ContentError), ContentError);
			std::string AssetIdentity;
			std::vector<std::uint8_t> Embedded, Original;
			Require(AssetServices.ResolveResourceReference(Package.Module->EntryModulePath, "sample.tga", AssetIdentity,
			            ContentError) &&
			        AssetServices.ReadResource(AssetIdentity, Embedded, ContentError) &&
			        ReadGuiResourceFile(Assets.Resources.front(), Original, ContentError) && Embedded == Original,
			    "content image did not survive source-free baking: " + ContentError);
			Require(ResolveGuiResourceReference(PathUtil::ToUtf8(Content), "memory:host", AssetIdentity, ContentError) &&
			        AssetIdentity == "memory:host" &&
			        AssetServices.ResolveResourceReference(Package.Module->EntryModulePath, "memory:host",
			            AssetIdentity, ContentError) &&
			        AssetIdentity == "memory:host" &&
			        AssetServices.ReadResource(AssetIdentity, Embedded, ContentError) &&
			        Embedded == std::vector<std::uint8_t>({9, 8, 7}),
			    "opaque host resource identity changed during source-free baking: " + ContentError);
		}
		FToolOptions Options;
		VeroScript::Test::Configure(Options.Engine);
		Options.Engine.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		auto Service = CreateCommandLineService(Options);
		auto Registration = Service->CreateRegistration();
		auto Independent = Service->CreateRegistration();
		const auto Directory =
		    PathUtil::MakeTemporarySiblingPath(std::filesystem::temp_directory_path() / "VeroScriptGuiToolsTests");
		std::filesystem::create_directories(Directory);
		struct FCleanup
		{
			std::filesystem::path Path;
			~FCleanup()
			{
				std::error_code Error;
				std::filesystem::remove_all(Path, Error);
			}
		} Cleanup{Directory};
		if (Selected("BackendSelection"))
		{
			CheckBackendSelection(Directory);
		}
		// Only this test's uniquely named leaf is removed by the local cleanup object.
		const auto Main = Directory / "main.vs";
		const auto Page = Directory / "page.vswml";
		{
			std::ofstream File(Main);
			File << "void main(){VS::Gui::Widget "
			        "w=VS::Gui::Widget::Create().Gap(6).Text(\"Hello\").FontSize(20).ClipContent(true);assert(w.GetGap("
			        ")==6 && w.GetText()==\"Hello\" && w.GetFontSize()==20 && w.GetClipContent());}";
		}
		{
			std::ofstream File(Page);
			File << "<Widget><script>public: double Count=3;</script><Widget bind:gap=\"{ Count }\"/></Widget>";
		}
		std::ostringstream Output, Errors;
		FCommandLineContext Context;
		Context.WorkingDirectory = PathUtil::ToUtf8(Directory);
		Context.Output = &Output;
		Context.Errors = &Errors;
		std::string Error;
		Require(
		    Registration.Module.Configure(Context, {{"gui-backend", {"chosen"}}}, Registration.Module.UserData, Error),
		    Error);
		Require(Independent.Module.Configure(Context, {}, Independent.Module.UserData, Error), Error);
		if (Selected("BackendSelection"))
		{
			auto Rejected = Options.Engine;
			Rejected.bClearVmBackend = true;
			Registration.SetEngineOptions(Rejected);
			const auto& Preview = Registration.Module.Commands.at(2);
			const auto RejectedCode = Preview.Run(Context, {PathUtil::ToUtf8(Main)}, Preview.UserData);
			Require(RejectedCode == 2 &&
			        Errors.str().find("VM factory, path and clear settings are exclusive") != std::string::npos,
			    "GUI preview continued after its engine configuration was rejected: " + Errors.str());
			Registration.SetEngineOptions(Options.Engine);
			Errors.str("");
			Errors.clear();
		}
		if (Selected("SourceImports") || Selected("SourceDetection"))
		{
			std::cout << "GUI tools demand imports\n"
			          << std::flush;
			FHostBindingRegistry Bindings;
			Require(Registration.RegisterBindings(Bindings, Error), Error);
			FEngineConfig Engine = Options.Engine;
			Require(Registration.ConfigureEngine(Engine, &Bindings, Error), Error);
			Require(Registration.ConfigureEngine(Engine, &Bindings, Error), Error);
			Require(Engine.ImplicitImportPaths.empty(), "ordinary CLI still implicitly loads GUI");
			const auto& Processing = Engine.ModuleEnvironment ? Engine.ModuleEnvironment->GetDescription().CompileProcessing
			                                                  : Engine.CompileProcessing;
			Require(Processing.Registry != nullptr, "GUI source processor is missing");
			const auto& Processors = Processing.Registry->GetProcessors();
			const auto Importer = std::find_if(Processors.begin(), Processors.end(),
			    [](const auto& Value)
			{
				return Value.ProcessorId == "veroscript.gui.source-imports";
			});
			Require(Importer != Processors.end(), "GUI source processor is missing");
			const auto CheckDemand = [&](const char* Text, bool Needed)
			{
				FSourceDocument Document;
				Document.Text = Text;
				const std::vector<FAnnotationDeclarationView> Declarations;
				const FSourceMetadataSnapshot Metadata;
				FCompileProcessingContext Input{ECompileProcessingPhase::RawSource, ECompileProcessingEvent::Module,
				    0, PathUtil::ToUtf8(Directory / "detection.vs"), {}, nullptr, nullptr, nullptr, nullptr,
				    &Document, Declarations, Metadata, {}, {}, nullptr, {}, {}, nullptr};
				Input.ReadDependency = [&](std::string_view Path, std::string& Bytes)
				{
					std::ifstream File(Directory / PathUtil::FromUtf8(Path), std::ios::binary);
					if (!File) return false;
					Bytes.assign(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>());
					return !File.bad();
				};
				FCompileProcessingOutput Changes;
				Importer->Factory()->Process(Input, Changes);
				Require(!Changes.GetOperations().empty() == Needed,
				    std::string("GUI demand detection misclassified: ") + Text);
			};
			CheckDemand("int main(){int Gui=42;return Gui;}", false);
			CheckDemand("struct Model { int Gui=1; }; int main(){Model value;return value.Gui;}", false);
			CheckDemand("int answer(int Gui){return Gui;}", false);
			CheckDemand("// VS::Gui\nstring value=\"Gui @GuiWidget\";", false);
			CheckDemand("import \"math.vs\";int main(){return 42;}", false);
			CheckDemand("#include \"math.vs\"\nint main(){return 42;}", false);
			CheckDemand("#define_export VALUE 21\nint main(){return VALUE;}", false);
			CheckDemand("#define VALUE() (3*7)\nint main(){return VALUE();}", false);
			CheckDemand("#define_export DOUBLE(x) ((x)*2)\n", true);
			CheckDemand("#if ENABLED\nint main(){return 42;}\n#endif\n", false);
			CheckDemand("void main(){VS::Gui::Widget value;}", true);
			CheckDemand("void main(){auto value=VS.Gui.Widget.Create();}", true);
			CheckDemand("using namespace VS;void main(){Gui::Widget value;}", true);
			CheckDemand("using namespace VS;void main(){auto value=Gui.Widget.Create();}", true);
			CheckDemand("using namespace VS;using namespace Gui;void main(){Widget value;}", true);
			CheckDemand("using namespace VS.Gui;void main(){Widget value;}", true);
			CheckDemand("namespace VS { namespace Gui { class Extra : public Widget {}; } }", true);
			CheckDemand("@GUIWIDGET export class Card {};", true);
			CheckDemand("#define GUI_NS VS::Gui\n", true);
			CheckDemand("#define GUI_NS Gui\n", true);
			CheckDemand("#define JOIN(a,b) a ## b\n", true);
			CheckDemand("#define TEXT(x) #x\n", true);
			CheckDemand("#define IDENTITY(x) x\n", true);
			CheckDemand("#define PREFIX(x) {} x\nusing namespace VS;void main(){PREFIX(Gui)::Widget::Create();}", true);
			CheckDemand("#define IDENTITY(int) int\n", true);
			CheckDemand("#define IDENTITY(x) ((x))\n", true);
			CheckDemand("#define NAMESPACE(x) using namespace x;\n", true);
			CheckDemand("#define ACCESS(x) x::Widget\n", true);
			CheckDemand("#define IMPORT import \"ordinary.vs\";\n", true);
			CheckDemand("int main(){int Gui=42;return identity(Gui);}", false);
			CheckDemand("import \"unknown.vslib\";int main(){return 42;}", true);
			CheckDemand("#include \"unknown.vslib\"\nint main(){return 42;}", true);
			auto ExplicitEngine = Options.Engine;
			Require(ConfigureGuiScriptEngine(ExplicitEngine, Error), Error);
			const std::string FrameworkPath(ExplicitEngine.ImplicitImportPaths.back());
			const auto CheckHostMacros = [&](FEngineConfig Input, bool Needed)
			{
				Require(Registration.ConfigureEngine(Input, &Bindings, Error), Error);
				const bool Imported = std::any_of(Input.ImplicitImportPaths.begin(), Input.ImplicitImportPaths.end(),
				    [&](const auto Path)
				{
					return PathUtil::Equal(std::string(Path), FrameworkPath);
				});
				Require(Imported == Needed, "GUI host macro demand misclassified");
			};
			auto HostEngine = Options.Engine;
			HostEngine.Defines.push_back("HOST_VALUE=1");
			HostEngine.PredefinedMacros.push_back({"F", {}, false, "7"});
			CheckHostMacros(HostEngine, false);
			HostEngine.Defines.push_back("UI=Gui");
			CheckHostMacros(HostEngine, true);
			HostEngine.Defines.pop_back();
			HostEngine.ImplicitImportPaths.push_back("opaque.vslib");
			CheckHostMacros(HostEngine, true);
			HostEngine.ImplicitImportPaths.clear();
			HostEngine.PredefinedMacros.push_back({"PREFIX", {"value"}, false, "{} value"});
			CheckHostMacros(HostEngine, true);
			HostEngine.PredefinedMacros.pop_back();
			HostEngine.PredefinedMacros.push_back({"IDENTITY", {"x"}, false, "x"});
			CheckHostMacros(HostEngine, true);
			HostEngine.PredefinedMacros.push_back({"JOIN", {"a", "b"}, false, "a ## b"});
			CheckHostMacros(HostEngine, true);
			const auto CheckBinaryMacro = [&](const char* Name, const char* Source, const char* Consumer, bool Needed)
			{
				FCompileOptions Producer;
				VeroScript::Test::Configure(Producer);
				auto Compiled = VeroScriptCompile::CompileSource(Source,
				    PathUtil::ToUtf8(Directory / (std::string(Name) + ".vs")), Producer);
				Require(Compiled.bSuccess, "binary macro fixture: " + Compiled.Error.Message);
				Require(Compiled.Module->PublicMacros.size() == 1, "binary macro fixture lost its export");
				FPackageBuildOptions Package;
				Package.Kind = EPackageKind::Library;
				Package.PackageName = Name;
				Package.bStripDebugInfo = true;
				std::vector<std::uint8_t> Bytes;
				Require(BuildPackage(*Compiled.Module, Package, Bytes, Error), Error);
				FPackageData Decoded;
				Require(ReadPackage(FByteView(Bytes), Decoded, Error) && Decoded.Module &&
				    Decoded.Module->PublicMacros.size() == 1, "binary macro fixture roundtrip failed: " + Error);
				const auto& Original = Compiled.Module->PublicMacros.front();
				const auto& Restored = Decoded.Module->PublicMacros.front();
				Require(Restored.Parameters == Original.Parameters && Restored.bVariadic == Original.bVariadic &&
				    Restored.Body == Original.Body, "binary macro fixture changed its expansion");
				std::ofstream File(Directory / (std::string(Name) + ".vslib"), std::ios::binary);
				File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
				File.close();
				Require(bool(File), "binary macro fixture write failed");
				CheckDemand(Consumer, Needed);
			};
			CheckBinaryMacro("literal", "#define_export VALUE 42\n",
			    "import \"literal.vslib\";int main(){return VALUE;}", false);
			CheckDemand("#include \"literal.vslib\"\nint main(){return VALUE;}", false);
			CheckBinaryMacro("forwarding", "#define_export PREFIX(x) {} x\n",
			    "import \"forwarding.vslib\";using namespace VS;void main(){PREFIX(Gui)::Widget::Create();}", true);
			CheckBinaryMacro("variadic", "#define_export FORWARD(...) __VA_ARGS__\n",
			    "import \"variadic.vslib\";using namespace VS;void main(){FORWARD(Gui)::Widget::Create();}", true);
			CheckBinaryMacro("scope", "#define_export SCOPE ::\n",
			    "import \"scope.vslib\";void main(){VS SCOPE Gui SCOPE Widget SCOPE Create();}", true);
			CheckDemand("#include \"scope.vslib\"\nint main(){return 42;}", true);
			if (Scenario == "SourceDetection")
			{
				std::cout << "GUI source demand detection passed\n";
				return 0;
			}
			const auto CompileOptions = [&]
			{
				FCompileOptions Input;
				VeroScript::Test::Configure(Input);
				Input.StdlibSearchPaths = Engine.StdlibSearchPaths;
				Input.SourceOverlays = Engine.SourceOverlays;
				Input.ImplicitImportPaths = Engine.ImplicitImportPaths;
				Input.ModuleEnvironment = Engine.ModuleEnvironment;
				Input.CompileProcessing = Engine.CompileProcessing;
				// Include preparation of the complete GUI graph in this integration fixture's budget.
				Input.ScriptCompileExtensions.TimeoutMilliseconds = 20000;
				return Input;
			};
			const auto Compile = [&](const std::string& Source, bool NeedsGui)
			{
				std::cout << "GUI demand: framework " << (NeedsGui ? "required" : "absent") << '\n'
				          << std::flush;
				auto Result = VeroScriptCompile::CompileSource(Source, PathUtil::ToUtf8(Directory / "demand.vs"), CompileOptions());
				Require(Result.bSuccess, "demand source compilation: " + Result.Error.Message);
				const auto GuiLoaded = std::any_of(Result.LoadedModulePaths.begin(), Result.LoadedModulePaths.end(),
				    [](const auto& Path)
				{
					return Path.find("__veroscript_gui__") != std::string::npos;
				});
				Require(GuiLoaded == NeedsGui, "GUI framework loaded for an unrelated source, or missing for a GUI source");
			};
			Compile("// VS::Gui @GuiWidget\nint main(){string value=\"Gui @GuiWidget\";return 42;}", false);
			Compile("import \"math.vs\";int main(){return 42;}", false);
			Compile("#include \"math.vs\"\nint main(){return std::clamp_int(42,0,42);}", false);
			Compile("int main(){int Gui=42;return Gui;}", false);
			Compile("using namespace VS.Gui;void main(){Widget w=Widget.Create().Gap(6);assert(w.GetGap()==6);}", true);
			const auto Library = [&](const char* Name, const std::string& Source)
			{
				FCompileOptions Producer;
				VeroScript::Test::Configure(Producer);
				Producer.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
				auto Compiled = VeroScriptCompile::CompileSource(Source,
				    PathUtil::ToUtf8(Directory / (std::string(Name) + ".vs")), Producer);
				Require(Compiled.bSuccess, "binary import fixture: " + Compiled.Error.Message);
				FPackageBuildOptions Package;
				Package.Kind = EPackageKind::Library;
				Package.PackageName = Name;
				Package.bStripDebugInfo = true;
				std::vector<std::uint8_t> Bytes;
				Require(BuildPackage(*Compiled.Module, Package, Bytes, Error), Error);
				std::ofstream File(Directory / (std::string(Name) + ".vslib"), std::ios::binary);
				File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
				Require(bool(File), "binary import fixture write failed");
			};
			Library("ordinary", "export int answer(){return 42;}");
			Compile("import \"ordinary.vslib\";int main(){return answer();}", false);
			Library("attributes", "export import \"attribute.vs\";"
			                      "export attribute<T> copied<T>(T value){return attribute<T>(value);}");
			Compile("import \"attributes.vslib\";int main(){auto value=copied(42);return value.get();}", false);
			Library("plain-macro", "#define_export VALUE 42\n");
			Compile("import \"plain-macro.vslib\";int main(){return VALUE;}", false);
			Compile("#include \"plain-macro.vslib\"\nint main(){return VALUE;}", false);
			Library("ui-macro", "#define_export UI VS::Gui\n");
			Compile("import \"ui-macro.vslib\";void main(){UI::Widget w=UI::Widget::Create();}", true);
			const auto Helper = Directory / "helper.vs";
			{
				std::ofstream File(Helper);
				File << "export class DemandBase : public VS::Gui::Widget {}"
				        "export VS::Gui::Widget MakeWidget(){return VS::Gui::Widget::Create().Gap(7);}";
			}
			Compile("import \"helper.vs\";int main(){auto widget=MakeWidget();return 42;}", true);
			Compile("import \"helper.vs\";@GUIWIDGET(Mark=\"demand\")export class DemandCard : public DemandBase {"
			        "public:@GUIPROPERTY std::attribute<int> Count=std::attribute<int>(7);}"
			        "void main(){DemandCard w=DemandCard::Create().Count(9);assert(w.GetCount()==9);}",
			    true);
			{
				std::ofstream File(Directory / "macros.vs");
				File << "#define_export UI VS::Gui\n";
			}
			Compile("import \"macros.vs\";void main(){UI::Widget w=UI::Widget::Create();}", true);
			Compile("#if 0\nVS::Gui::Widget unused;\n#endif\nint main(){return 42;}", true);
			{
				std::ofstream File(Directory / "generator.vs");
				File << R"vs(
import "compiler.vs";
import "string.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
    if(std::strings::ends_with(ctx.module_id(),"demand.vs"))
        out.add_declaration(ctx.top_scope(),"class GeneratedCard : public VS::Gui::Widget {public:int Value=7;};");
};}
export void register_extension(std::compiler::registration reg){
    std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
    std::compiler::processor_definition d=std::compiler::processor_definition();d.id="test.gui.generator";d.version="0.1.1";
    d.phases=array<std::compiler::phase>{std::compiler::phase::types};
    d.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};
    d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);
}
)vs";
			}
			Compile("import compiler \"generator.vs\";void main(){GeneratedCard w=GeneratedCard();"
			        "assert(w.Value==7);w.SetGap(9);assert(w.GetGap()==9);}",
			    true);
			const auto Invalid = VeroScriptCompile::CompileSource("using namespace VS::Gui;\nint main(){return missing_name;}",
			    PathUtil::ToUtf8(Directory / "invalid.vs"), CompileOptions());
			Require(!Invalid.bSuccess && Invalid.Error.Location.Line == 2,
			    "ordinary source diagnostic changed after GUI configuration");
			// Explicit host opt-in restores the original full import and removes the CLI source processor.
			Require(ConfigureGuiScriptEngine(Engine, Error), Error);
			Require(Engine.ImplicitImportPaths.size() == 1, "explicit GUI configuration lost its framework import");
			if (Engine.CompileProcessing.Registry)
			{
				for (const auto& Processor : Engine.CompileProcessing.Registry->GetProcessors())
				{
					Require(Processor.ProcessorId != "veroscript.gui.source-imports", "explicit GUI kept demand processing");
				}
			}
			Require(Registration.ConfigureEngine(Engine, &Bindings, Error) && Engine.ImplicitImportPaths.size() == 1,
			    "CLI configuration discarded the host's explicit GUI import");
			auto MacroEngine = Options.Engine;
			MacroEngine.Defines.push_back("HOST_VALUE=1");
			Require(Registration.ConfigureEngine(MacroEngine, &Bindings, Error) && MacroEngine.ImplicitImportPaths.empty(),
			    "ordinary host macros implicitly load the GUI framework");
			auto CallerEngine = Options.Engine;
			auto CallerRegistry = std::make_shared<FCompileProcessorRegistry>();
			FCompileProcessorDesc Caller;
			Caller.ProcessorId = "test.caller";
			Caller.Version = "1";
			Caller.Phases = CompilePhaseMask(ECompileProcessingPhase::Types);
			Caller.Operations = CompileOperationMask(ECompileOperation::SetMetadata);
			Caller.Subscriptions.push_back({ECompileProcessingEvent::Module});
			Caller.Factory = []
			{
				return std::make_unique<FCallerProcessor>();
			};
			Require(CallerRegistry->Register(std::move(Caller), Error), Error);
			CallerRegistry->Freeze();
			CallerEngine.CompileProcessing.Registry = CallerRegistry;
			Require(Registration.ConfigureEngine(CallerEngine, &Bindings, Error) && CallerEngine.ImplicitImportPaths.size() == 1,
			    "caller generators lost conservative GUI import behavior");
		}
		const auto Run = [&](std::size_t Index, std::vector<std::string> Arguments)
		{
			Errors.str("");
			Errors.clear();
			const auto& Command = Registration.Module.Commands.at(Index);
			std::cout << "GUI tools command: " << Command.Name;
			if (!Arguments.empty())
			{
				std::cout << ' ' << Arguments.front();
			}
			std::cout << '\n'
			          << std::flush;
			const int Code = Command.Run(Context, Arguments, Command.UserData);
			if (Code != 0)
			{
				std::cerr << Command.Name << " returned " << Code << ": " << Errors.str();
			}
			return Code;
		};
		if (Selected("Check"))
		{
			Require(Run(0, {"page.vswml"}) == 0, Errors.str());
			Require(Run(0, {"main.vs"}) == 0, Errors.str());
			const auto Notifications = PathUtil::FromUtf8(__FILE__).parent_path().parent_path() /
			    "Examples/Notifications/main.vs";
			Require(Run(0, {PathUtil::ToUtf8(Notifications)}) == 0, Errors.str());
			Require(Run(0, {"page.vswml", "--frames", "1"}) == 2, "check accepted a preview-only option");
		}
		if (Selected("Artifact"))
		{
			#if defined(VEROSCRIPT_WITH_APPLICATION)
			Require(Run(1, {"page.vswml", "--output", "output.vsapp"}) == 0, Errors.str());
			Require(Run(1, {"main.vs", "--output", "main.vs"}) == 2, "compiler accepted overwriting a source file");
			Require(Run(1, {"main.vs", "--output", "plain.vsapp"}) == 0, Errors.str());
			Require(Run(0, {"plain.vsapp"}) == 0, Errors.str());
			Require(Run(2, {"plain.vsapp"}) == 0, Errors.str());
			Require(std::filesystem::file_size(Directory / "output.vsapp") > 100,
			    "GUI compiler did not write its application");
			FModuleEnvironmentDesc HostEnvironment;
			HostEnvironment.EntryOwnerId = "gui-artifact-host";
			HostEnvironment.Scopes.push_back({HostEnvironment.EntryOwnerId, {}});
			HostEnvironment.Bindings = std::make_shared<FHostBindingRegistry>();
			Context.Environment = FModuleEnvironment::Create(std::move(HostEnvironment), Error);
			Require(Context.Environment != nullptr, Error);
			Require(Run(2, {"plain.vsapp"}) == 0,
			    "GUI artifact preview lost the prepared host binding catalog: " + Errors.str());
			Context.Environment.reset();
			#else
			Require(Run(1, {"page.vswml", "--output", "output.vsapp"}) == 2,
			    "GUI compiler wrote an application without container support");
			#endif
		}
		if (Selected("Host"))
		{
			Require(Run(2, {"main.vs"}) == 0, Errors.str());
			Require(Run(2, {"main.vs", "--frames", "1"}) == 2, "VS preview ignored its unsupported frame bound");
			std::cout << "GUI tools host hooks\n"
			          << std::flush;
			FHost Host;
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Require(Registration.RegisterBindings(Session.GetBindingRegistry(), Error), Error);
			FEngineConfig Engine = Options.Engine;
			Require(Registration.ConfigureEngine(Engine, nullptr, Error), Error);
			Session.ApplyConfig(Engine);
			auto Loaded = Session.LoadFile(PathUtil::ToUtf8(Main));
			Require(Loaded.bSuccess, Loaded.Error.Message);
			Require(Session.Run().bSuccess, "host hook execution failed");
		}
		if (Selected("Check") && !ModulePath.empty())
		{
			std::cout << "GUI tools native CLI forwarding\n"
			          << std::flush;
			auto Commands = std::make_shared<FCommands>();
			FNativeExtensionLoadOptions Load;
			Load.ExpectedId = "veroscript-gui-tools";
			auto Module = LoadNativeExtension(ModulePath, Error, Load);
			Require(Module != nullptr, Error);
			FNativeExtensionRegistrationOptions Input;
			Input.Origin.ProviderId = "gui.tools.test";
			FNativeExtensionServiceBinding CliBinding;
			CliBinding.Id = CommandLineServiceId;
			CliBinding.Version = CommandLineServiceVersion;
			CliBinding.Service = Commands;
			Input.Services.push_back(std::move(CliBinding));
			FNativeExtensionServiceBinding ToolBinding;
			ToolBinding.Id = ToolServiceId;
			ToolBinding.Version = ToolServiceVersion;
			ToolBinding.Service = Service;
			Input.Services.push_back(std::move(ToolBinding));
			FNativeExtensionRegistry Registry;
			Require(Registry.Add(Module, Input, Error), Error);
			Require(Commands->Module.Commands.size() == 3, "native GUI adapter lost its commands");
			const auto& Check = Commands->Module.Commands[0];
			Require(Check.Run(Context, {"page.vswml"}, Check.UserData) == 0, Errors.str());
			Commands->Module = {}; // Borrowed module state is released after its descriptor.
		}
		if (Scenario == "All")
		{
			std::cout << "GUI offline tasks, host hooks and native CLI forwarding passed\n";
		}
		else
		{
			std::cout << "GUI tools " << Scenario << " scenario passed\n";
		}
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}

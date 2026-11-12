// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptCppAot.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptCompile.h"
#include "VeroScriptAst.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptSourceMetadata.h"
#include "VeroScriptPath.h"
#include "VeroScriptCppScriptCompileFixture.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	bool TestAnnotationNamePolicy()
	{
		using namespace VeroScript;
		struct FGenerator final : IAnnotationProcessor
		{
			void Process(const FAnnotationContext& C, FAnnotationOutput& O) override
			{
				if (C.Annotation.Name != "@gEnErAtE" || C.Annotation.DefinitionId.empty())
				{
					throw std::runtime_error("AOT lost annotation spelling or definition identity");
				}

				O.SubmitMethod(C.Declaration.Id, EMemberAccess::Public, "int Read(){return 42;}");
			}
		};
		FHostBindingRegistry Bindings;
		std::string Error;
		if (!Bindings.RegisterAnnotation("@Generate", "", Error, "", {}, AnnotationAllTargets, false)) return false;
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		unsigned Instances = 0;
		if (!Registry->Register({"@GENERATE", "cpp.aot.name-policy", "1", "",
		                            AnnotationTargetMask(EAnnotationTarget::Struct), 1, 0,
		                            [&]
		{
			++Instances;
			return std::make_unique<FGenerator>();
		}},
		        Error))
		{
			return false;
		}

		Registry->Freeze();
		FCodegenOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Bindings;
		Options.AnnotationProcessing.Registry = Registry;
		const std::string Source = "@gEnErAtE struct Item{};int main(){Item x=Item();return x.Read();}";
		const auto Directory = std::filesystem::temp_directory_path() / ("vero_cpp_annotation_names_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Directory);
		struct FCleanup
		{
			~FCleanup()
			{
				std::error_code E;
				std::filesystem::remove_all(Path, E);
			}

			std::filesystem::path Path;
		} Cleanup{Directory};
		const auto Entry = (Directory / "main.vs").generic_string();
		{
			std::ofstream File(Entry);
			File << Source;
		}
		FCppCodegenBackend Backend;
		FCodegenResult Result;
		if (!Backend.EmitFromSource(Source, Entry, Options, Result) ||
		    Result.Output.find("Read") == std::string::npos ||
		    !Backend.EmitFromFile(Entry, Options, Result) || Instances != 2)
		{
			std::cerr << "FAIL: AOT annotation name policies: " << Result.Error << '\n';
			return false;
		}
		// The free program emitter has no live-catalog parameter. Copy annotation
		// policies into the producer instead of sealing it against that catalog.
		FCompileOptions Compile;
		VeroScript::Test::Configure(Compile);
		Compile.AnnotationProcessing = Options.AnnotationProcessing;
		Compile.CompileProcessing.AnnotationDefinitions = GetAnnotationDefinitions(Bindings);
		auto Prepared = VeroScriptCompile::CompileSource(Source, Entry, Compile);
		if (!Prepared.bSuccess || Instances != 3 ||
		    !EmitCppFromProgram(Prepared.PreparedProgram->GetProgram(), Result.Output, Error) ||
		    Instances != 3)
		{
			std::cerr << "FAIL: prepared AOT annotation policy reuse: " << Error << '\n';
			return false;
		}
		FHostBindingRegistry Exact;
		if (!Exact.RegisterAnnotation("@Generate", "", Error)) return false;
		Options.Bindings = &Exact;
		if (Backend.EmitFromSource(Source, Entry, Options, Result) || !Result.Output.empty())
		{
			std::cerr << "FAIL: AOT accepted an unmatched case-sensitive annotation\n";
			return false;
		}
		return true;
	}

	bool TestScriptCompileExtensions()
	{
		using namespace VeroScript;
		const auto Directory = std::filesystem::temp_directory_path() / ("vero_cpp_script_aot_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Directory);
		struct FCleanup
		{
			~FCleanup()
			{
				std::error_code E;
				std::filesystem::remove_all(Path, E);
			}

			std::filesystem::path Path;
		} Cleanup{Directory};
		const auto Entry = (Directory / "main.vs").generic_string();
		FCodegenOptions O;
		O.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		O.StdlibSearchPaths = {VEROSCRIPT_STDLIB_DIR};
		O.ScriptCompileExtensions.Roots = {{(Directory / "builder.vs").generic_string(), {}}};
		O.ScriptCompileExtensions.Configuration = {{"amount", "5"}};
		O.SourceOverlays = {{O.ScriptCompileExtensions.Roots[0].Path, CppScriptFixture::Extension}};
		O.CompileProcessing.ReadDependency = [](std::string_view, std::string& Text)
		{
			Text = "7";
			return true;
		};
		FCppCodegenBackend B;
		FCodegenResult R;
		if (B.EmitFromFile({}, O, R) || !R.Output.empty()) return false;
		if (!B.EmitFromSource(CppScriptFixture::Source, Entry, O, R) ||
		    R.Output.find("Generated") == std::string::npos)
		{
			std::cerr << "FAIL: script extension AOT source: " << R.Error << '\n';
			return false;
		}
		// The core loader accepts an entry supplied only by an overlay and matches
		// relative overlay keys against an absolute requested path.
		O.SourceOverlays.push_back({"main.vs", CppScriptFixture::Source});
		if (!B.EmitFromFile(Entry, O, R))
		{
			std::cerr << "FAIL: AOT missing entry overlay: " << R.Error << '\n';
			return false;
		}
		{
			std::ofstream F(Entry);
			F << "invalid disk contents";
		}
		if (!B.EmitFromFile(Entry, O, R))
		{
			std::cerr << "FAIL: AOT entry overlay overrides disk: " << R.Error << '\n';
			return false;
		}
		O.ScriptCompileExtensions.MaxInstructions = 1;
		if (B.EmitFromFile(Entry, O, R) || !R.Output.empty())
		{
			std::cerr << "FAIL: failed script extension AOT must clear generated output\n";
			return false;
		}
		return true;
	}
	bool TestCompileProcessing()
	{
		using namespace VeroScript;
		struct FSourceProcessor final : ICompileProcessor
		{
			void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
			{
				if (C.Phase == ECompileProcessingPhase::RawSource)
				{
					++RawCalls;
					const auto P = C.Source->Text.find("CUSTOM");
					if (P != std::string::npos)
					{
						FSourceRange R;
						R.SourceId = C.Source->Id;
						R.Begin = P;
						R.End = P + 6;
						O.EditSource(R, "class");
					}
				}
				else
				{
					if (RawCalls != 1)
					{
						throw std::runtime_error("source processor state lost");
					}

					for (const auto& D : C.Declarations)
					{
						if (D.Name == "Pending" && !D.bHasBody)
						{
							O.CompleteBody(D.Id, C.Snapshot, "{return 19;}");
						}
					}
				}
			}

			unsigned RawCalls = 0;
		};
		auto R = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc D;
		D.ProcessorId = "cpp.aot.source";
		D.Version = "1";
		D.Phases = CompilePhaseMask(ECompileProcessingPhase::RawSource) | CompilePhaseMask(ECompileProcessingPhase::Bodies);
		D.Operations = CompileOperationMask(ECompileOperation::EditSource) | CompileOperationMask(ECompileOperation::CompleteBody);
		D.Subscriptions = {{ECompileProcessingEvent::Module}};
		unsigned Instances = 0;
		D.Factory = [&]
		{
			++Instances;
			return std::make_unique<FSourceProcessor>();
		};
		std::string Error;
		if (!R->Register(std::move(D), Error)) return false;
		R->Freeze();
		const std::string Source = "CUSTOM Item{};int Pending();void main(){print(Pending());}";
		FCodegenOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.CompileProcessing.Registry = R;
		FCppCodegenBackend Backend;
		FCodegenResult Result;
		if (!Backend.EmitFromSource(Source, "aot-processing.vs", Options, Result) || Instances != 1)
		{
			std::cerr << "FAIL: AOT source processing: " << Result.Error << '\n';
			return false;
		}
		FCompileOptions Compile;
		VeroScript::Test::Configure(Compile);
		Compile.CompileProcessing = Options.CompileProcessing;
		auto Prepared = VeroScriptCompile::CompileSource(Source, "aot-processing.vs", Compile);
		if (!Prepared.bSuccess || Instances != 2 ||
		    !EmitCppFromProgram(Prepared.PreparedProgram->GetProgram(), Result.Output, Error) ||
		    Instances != 2)
		{
			std::cerr << "FAIL: AOT prepared reuse: " << Error << '\n';
			return false;
		}
		Options.CompileProcessing.MaxOperations = 0;
		if (Backend.EmitFromSource(Source, "aot-processing.vs", Options, Result) || !Result.Output.empty())
		{
			std::cerr << "FAIL: AOT must enforce processing budgets\n";
			return false;
		}
		return true;
	}
	bool TestAnnotationBudget()
	{
		using namespace VeroScript;
		struct FManyMethods final : IAnnotationProcessor
		{
			void Process(const FAnnotationContext& Context, FAnnotationOutput& Output) override
			{
				for (std::size_t Index = 0; Index <= FAnnotationProcessingOptions{}.MaxOperations; ++Index)
				{
					Output.SubmitMethod(Context.Declaration.Id, EMemberAccess::Public,
					    "int generated" + std::to_string(Index) + "(){return 42;}");
				}
			}
		};
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		std::string Error;
		if (!Registry->Register(
		        {"@Many", "cpp.aot.budget", "1", "", AnnotationTargetMask(EAnnotationTarget::Struct), 1, 0,
		            []
		{
			return std::make_unique<FManyMethods>();
		}},
		        Error))
		{
			return false;
		}

		Registry->Freeze();
		FCodegenOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.AnnotationProcessing.Registry = Registry;
		++Options.AnnotationProcessing.MaxOperations;
		FCppCodegenBackend Backend;
		FCodegenResult Result;
		if (!Backend.EmitFromSource("@Many struct Generated {}; void main(){}", "aot-budget.vs", Options, Result))
		{
			std::cerr << "FAIL: C++ AOT must preserve the explicitly raised annotation budget: " << Result.Error << '\n';
			return false;
		}
		if (Result.Output.find("generated" + std::to_string(FAnnotationProcessingOptions{}.MaxOperations)) == std::string::npos)
		{
			std::cerr << "FAIL: C++ AOT lost methods allowed by the raised annotation budget\n";
			return false;
		}
		return true;
	}
	bool ExpectEmit(
	    const std::string& Source,
	    const char* Label,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& RequiredFragments = {},
	    const std::vector<std::string>& ForbiddenFragments = {})
	{
		std::string Cpp;
		std::string Error;
		if (VeroScript::EmitCppFromSource(
		        Source,
		        "aot_emit_test.vs",
		        Cpp,
		        Error,
		        ImportPaths))
		{
			for (const std::string& Fragment : RequiredFragments)
			{
				if (Cpp.find(Fragment) == std::string::npos)
				{
					std::cerr << "FAIL: " << Label
					          << "\nmissing generated fragment: " << Fragment
					          << "\ngenerated source:\n"
					          << Cpp << '\n';
					return false;
				}
			}
			for (const std::string& Fragment : ForbiddenFragments)
			{
				if (Cpp.find(Fragment) != std::string::npos)
				{
					std::cerr << "FAIL: " << Label
					          << "\nunexpected generated fragment: " << Fragment << '\n';
					return false;
				}
			}
			return true;
		}
		std::cerr << "FAIL: " << Label << '\n'
		          << Error << '\n';
		return false;
	}

	bool ExpectEmitFailure(
	    const std::string& Source,
	    const char* Label,
	    const std::string& RequiredError)
	{
		std::string Cpp;
		std::string Error;
		if (VeroScript::EmitCppFromSource(Source, "aot_emit_test.vs", Cpp, Error, {}))
		{
			std::cerr << "FAIL: " << Label << "\nexpected AOT emission to fail\n";
			return false;
		}
		if (Error.find(RequiredError) == std::string::npos)
		{
			std::cerr << "FAIL: " << Label << "\nunexpected error: " << Error << '\n';
			return false;
		}
		return true;
	}

	bool TestConstructionDiamond()
	{
		std::string Declarations = "interface Root{virtual int read()=0;}\n";
		std::string Parent = "Root";
		for (int Level = 0; Level < 30; ++Level)
		{
			const auto Suffix = std::to_string(Level);
			const auto Left = "Left" + Suffix, Right = "Right" + Suffix, Join = "Join" + Suffix;
			Declarations += "interface " + Left + ":" + Parent + "{};interface " + Right + ":" + Parent + "{};interface " + Join + ":" + Left + "," + Right + "{};\n";
			Parent = Join;
		}
		return ExpectEmit(Declarations + "class Concrete:public " + Parent + "{public:int read(){return 7;}};int main(){Concrete value=Concrete();return value.read();}",
		           "shared inheritance resolves concrete constructor methods", {}, {"struct vs_Concrete"}) &&
		    ExpectEmitFailure(Declarations + "class Missing:public " + Parent + "{};void main(){Missing value=Missing();}",
		        "shared inheritance preserves unimplemented virtual slots", "abstract");
	}
}

int main(int Argc, char** Argv)
{
	if (Argc > 1 && std::string(Argv[1]) == "--construction-diamond-only")
	{
		return TestConstructionDiamond() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (!TestConstructionDiamond()) return EXIT_FAILURE;
	if (!ExpectEmit("int main(){if constexpr(true)return 7;else return missing();}",
	        "if constexpr discards before source emission", {}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit("constexpr int plus(int n){return n+1;}constexpr int count=plus(3);"
	                "struct Limits{static constexpr int value=plus(4);};"
	                "int main(){constexpr int n=plus(5);int data[plus(2)];return n+count+Limits::value+len(data);}",
	        "constexpr values lower before standalone emission", {}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmitFailure("int f(){return 3;}constexpr int n=f();int main(){return n;}",
	        "invalid constexpr cannot reach standalone output", "constexpr"))
	{
		return EXIT_FAILURE;
	}

	for (const auto* Source : {
	         "class Box{public:template<typename T> T echo(T value){return value;}};int main(){return 0;}",
	         "template<int N> int value(){return N;}int main(){return value<3>();}",
	         "template<typename T> T identity(T value){return value;}int main(){return identity<>(7);}",
	         "template<typename T> T identity(T value){return value;}int main(){auto call=identity<int>;return call(7);}"})
	{
		std::string Output = "unchanged", Error;
		if (VeroScript::EmitCppFromSource(Source, "template-aot.vs", Output, Error) || !Output.empty() || Error.find("Core AOT") == std::string::npos)
		{
			std::cerr << "unsupported templates must fail before source emission: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!ExpectEmit("T identity<T>(T value){using Value=T;Value result=value;return result;}"
	                "int main(){using Value=int;Value result=identity<int>(7);return result;}",
	        "ordinary block aliases remain eligible", {}, {"using Value ="}))
	{
		return EXIT_FAILURE;
	}
	for (const auto* Source : {"struct Outer{struct Item{};};int main(){return 0;}",
	         "int main(){struct Item{};return 0;}"})
	{
		if (!ExpectEmitFailure(Source, "scoped types have an explicit standalone boundary", "nested and local types"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!ExpectEmitFailure(
	        "struct Item{int value;Item(int input){value=input;}"
	        "Item(const Item& input){value=input.value+1;}};void main(){Item copy=Item(6);}",
	        "script copy construction keeps its explicit standalone boundary", "script copy constructors"))
	{
		return EXIT_FAILURE;
	}
	for (const char* Source : {
	         "void main(){variant value=1;}",
	         "variant identity<T>(T value){return value;} void main(){}",
	         "T unused<T>(T value){variant local=value;return value;} void main(){}",
	         "using Dynamic=variant; void main(){}"})
	{
		std::string Output = "unchanged", Error;
		if (VeroScript::EmitCppFromSource(Source, "variant-aot.vs", Output, Error) || !Output.empty() || Error.find("diag.aot_variant_unsupported") == std::string::npos)
		{
			std::cerr << "variant must fail before standalone emission: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	for (const char* Source : {
	         "import \"string.vs\"; void main(){print(std::strings::parse<int>(\"42\"));}",
	         "import \"string.vs\"; void main(){fn(string,int64)->string f=std::strings::repeat; print(f(\"a\",1.0));}",
	         "import \"concurrent.vs\"; void main(){auto lock=std::concurrent::make_mutex();}",
	         "import \"concurrent.vs\"; int answer(){return 42;} void main(){auto t=std::concurrent::spawn_task<int>(answer); print(t.await());}"})
	{
		std::string Output = "unchanged", Error;
		if (VeroScript::EmitCppFromSource(Source, "foundation-runtime.vs", Output, Error, {VEROSCRIPT_STDLIB_DIR}) || !Output.empty() || Error.find("standard-library foundation operations require") == std::string::npos)
		{
			std::cerr << "FAIL: runtime foundation must be rejected before standalone output: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Source = "void main(){print(42);}";
		std::string Output = Source, Error;
		if (!VeroScript::EmitCppFromSource(Output, "in-place.vs", Output, Error) || Output.empty())
		{
			std::cerr << "in-place AOT source was cleared before being read: " << Error << '\n';
			return EXIT_FAILURE;
		}
		Output = "in-place-label.vs";
		if (!VeroScript::EmitCppFromSource(Source, Output, Output, Error) || Output.empty())
		{
			std::cerr << "in-place AOT source label was cleared before being read: " << Error << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FCppCodegenBackend Backend;
		VeroScript::FCodegenResult Result;
		Result.Output = Source;
		if (!Backend.EmitFromSource(Result.Output, "in-place-backend.vs", {}, Result) ||
		    Result.Output.empty())
		{
			std::cerr << "AOT result reset invalidated its source input: " << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		Result.Output = "in-place-backend-label.vs";
		if (!Backend.EmitFromSource(Source, Result.Output, {}, Result) || Result.Output.empty())
		{
			std::cerr << "AOT result reset invalidated its source label: " << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "app";
		Description.Scopes = {{"app", {}}};
		const std::string Entry = "veroscript-module:/app/in-place.vs";
		Description.Modules = {{"in-place.vs", "in-place", Entry, "app", "in-place.1", Source}};
		VeroScript::FCodegenOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ModuleEnvironment = VeroScript::FModuleEnvironment::Create(std::move(Description), Error);
		if (!Options.ModuleEnvironment)
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		Result.Output = Source;
		if (!Backend.EmitFromSource(Result.Output, Entry, Options, Result) || Result.Output.empty())
		{
			std::cerr << "environment normalization invalidated its source input: " << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		Result.Output = Entry;
		if (!Backend.EmitFromFile(Result.Output, Options, Result) || Result.Output.empty())
		{
			std::cerr << "environment normalization invalidated its file path: " << Result.Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		FModuleEnvironmentDesc D;
		D.EntryOwnerId = "app";
		D.Scopes = {{"app", {"library"}}, {"library", {}}};
		D.Modules = {{"library.vs", "library.api", "veroscript-module:/library/api.vs", "library", "aot-example", std::string("export int answer(){return 42;}")}};
		std::string Error;
		FCodegenOptions O;
		O.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		O.ModuleEnvironment = FModuleEnvironment::Create(D, Error);
		FCppCodegenBackend Backend;
		FCodegenResult Result;
		if (!O.ModuleEnvironment ||
		    !Backend.EmitFromSource(
		        "import \"library.vs\"; int main(){return answer();}", "<environment-aot>", O, Result))
		{
			std::cerr << "module environment AOT failed: " << Error << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		FHostBindingRegistry HostBindings;
		if (!HostBindings.RegisterNative(
		        "host_answer", ENativeType::Int, {},
		        [](const FNativeCallContext&, FValue& Out, std::string&)
		{
			Out = std::int32_t{42};
			return true;
		},
		        nullptr, Error))
		{
			std::cerr << "host AOT binding registration failed: " << Error << '\n';
			return EXIT_FAILURE;
		}
		O.Bindings = &HostBindings;
		if (!Backend.EmitFromSource(
		        "int main(){return host_answer();}", "<environment-host-aot>", O, Result) ||
		    Result.Output.find("extern \"C\"") == std::string::npos ||
		    Result.Output.find("host_answer") == std::string::npos)
		{
			std::cerr << "environment AOT lost explicit host bindings: " << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		VeroScriptModuleLoader HostLoader("<environment-host-program>", O.ModuleEnvironment);
		const auto HostProgram = HostLoader.LoadSourceModule("int main(){return host_answer();}", "<environment-host-program>");
		if (!HostProgram || !PrepareCodegenProgram(*HostProgram, {}, &HostBindings, Error))
		{
			std::cerr << "environment AOT program preparation lost explicit host bindings: " << Error << '\n';
			return EXIT_FAILURE;
		}
		auto PublishedDescription = D;
		PublishedDescription.Bindings = std::make_shared<FHostBindingRegistry>();
		const auto PublishedEnvironment = FModuleEnvironment::Create(std::move(PublishedDescription), Error);
		if (!PublishedEnvironment)
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		VeroScriptModuleLoader PublishedLoader("<environment-published-host>", PublishedEnvironment);
		const auto PublishedProgram = PublishedLoader.LoadSourceModule("int main(){return host_answer();}", "<environment-published-host>");
		if (!PublishedProgram || PrepareCodegenProgram(*PublishedProgram, {}, &HostBindings, Error))
		{
			std::cerr << "environment AOT allowed replacing its published binding catalog\n";
			return EXIT_FAILURE;
		}
		O.Bindings = nullptr;
		D.AvailableDependencies = {{"native", "0.1.1", "abi", "build", "content"}};
		D.RequiredDependencies = D.AvailableDependencies;
		O.ModuleEnvironment = FModuleEnvironment::Create(D, Error);
		if (!O.ModuleEnvironment ||
		    Backend.EmitFromSource("int main(){return 1;}", "<environment-aot>", O, Result) ||
		    Result.Error.find("native package contracts") == std::string::npos)
		{
			std::cerr << "native package contract was not rejected by standalone AOT: " << Error
			          << Result.Error << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!TestScriptCompileExtensions()) return EXIT_FAILURE;
	if (!TestAnnotationNamePolicy()) return EXIT_FAILURE;
	if (!TestCompileProcessing()) return EXIT_FAILURE;
	if (!ExpectEmit(
	        "struct pair<T,U>{T custom;U extra;T read(){return custom;}}void main(){pair<int,string> value=pair<int,string>();"
	        "value.custom=7;value.extra=\"user\";print(value.custom);ordered_map<string,pair<int,string>> values=ordered_map<string,pair<int,string>>();"
	        "values[\"a\"]=value;for(pair<int,string> item:values){print(item.read());}"
	        "for((string key,pair<int,string> item):values){print(item.read());}"
	        "for(auto& [key,item]:values){print(item.read());}print(values[\"a\"].read());}",
	        "user pair template uses ordinary generic lowering", {},
	        {"vs_pair<int, std::string> value", "vs_pair_g_int_string_read("},
	        {"vs_InternalPair<int, std::string>", "vs_pair_int_string_read"}))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmit(
	        "struct pair<T>{T value=9;pair(){value+=7;}}void main(){pair<int> local=pair<int>();print(local.value);"
	        "pair<int,int> entry=pair<int,int>();print(entry.first);}",
	        "user pair<T> constructor is not treated as a builtin pair constructor", {},
	        {"vs_pair_g_int_new()", "vs_InternalPair<int, int>"}))
	{
		return EXIT_FAILURE;
	}

	for (const char* Source :
	    {"struct pair<T,U>{T first;U second;}void main(){ordered_map<string,int> m={\"a\":3};auto entries=m.entries();print(entries[0].second);}",
	        "struct pair<T,U>{T first;U second;}void main(){ordered_map<string,int> m={\"a\":3};for(pair<string,int> entry:m){print(entry.second);}}"})
	{
		if (!ExpectEmitFailure(Source, "overridden pair map entries have an explicit AOT boundary",
		        "map entries with a user pair<T,U> template require the VM backend"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const char* Source :
	    {"@Tag struct S{};void main(){print(type<S>().annotation_count());}",
	        "void main(){source_node_info n=source_node_info();print(n.is_valid());}",
	        "void main(){annotation_info a=annotation_info();print(a.matches_name(\"@Tag\"));}",
	        "void main(){annotation_definition_info d=annotation_definition_info();print(d.case_sensitive());}",
	        "void main(){print(module_info().documentation());}"})
	{
		if (!ExpectEmitFailure(
		        Source, "runtime source metadata has an explicit AOT boundary", "runtime source metadata"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectEmitFailure(
	        "class C{public:string text=\"before\";string& borrow(){return text;}~C(){print(text);}}"
	        "void main(){string& alias=C().borrow();print(alias);}",
	        "field references must not delay script cleanup", "requires managed reference cleanup"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmitFailure(
	        "class C{public:string text;~C(){print(1);}}void main(){C c=C();string& alias=c.text;}",
	        "direct field aliases preserve cleanup", "requires managed reference cleanup"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmitFailure(
	        "array<int> values={7};void main(){int& alias=values[0];array<int> other={9};values=other;print(alias);}",
	        "persistent indexed aliases require storage paths", "requires managed storage paths"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmitFailure("array<int> values={7};void main(){for(int& alias=values[0];false;){} }",
	        "for initializer indexed aliases require storage paths", "requires managed storage paths"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmitFailure(
	        "array<int> values={7};void bump(ref int value){values.clear();value=9;}"
	        "void main(){bump(ref values[0]);}",
	        "indexed reference arguments require managed identity checks", "requires managed storage paths"))
	{
		return EXIT_FAILURE;
	}

	for (const auto& Case :
	    std::vector<std::pair<std::string, const char*>>{
	        {"class C{public:string text;}C item=C();string& get(){return item.text;}void main(){print(get());}",
	            "returned class fields require owner retention"},
	        {"class C{public:string text;}string& get(){string& alias=C().text;return alias;}void main(){print(get());}",
	            "returned direct field aliases require owner retention"},
	        {"array<int> values={7};int& get(){return values[0];}void main(){print(get());}",
	            "returned indexed storage requires owner retention"},
	        {"struct S{public:int n=7;int& borrow(){return n;}}void main(){int& alias=S().borrow();print(alias);}",
	            "temporary struct method aliases require owner retention"}})
	{
		if (!ExpectEmitFailure(Case.first, Case.second, "requires managed owner retention"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const auto& Case : std::vector<std::pair<std::string, const char*>>{
	         {"class B{public:~B(){print(1);}}class C:B{public:string text;string& borrow(){return text;}}"
	          "void main(){C c=C();string& alias=c.borrow();}",
	             "field aliases preserve base cleanup"},
	         {"class B{}class D:B{public:~D(){print(1);}}"
	          "class C{public:B child=D();string text;string& borrow(){return text;}}"
	          "void main(){C c=C();string& alias=c.borrow();}",
	             "field aliases preserve polymorphic class field cleanup"},
	         {"interface I{virtual int marker()=0;}class D:I{public:int marker(){return 1;}~D(){print(1);}}"
	          "class C{public:I child=D();string text;string& borrow(){return text;}}"
	          "void main(){C c=C();string& alias=c.borrow();}",
	             "field aliases preserve polymorphic interface field cleanup"}})
	{
		if (!ExpectEmitFailure(Case.first, Case.second, "requires managed reference cleanup"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectEmitFailure(
	        "class C{public:string text=\"before\";string& borrow(){return text;}}"
	        "string& get(){string& alias=C().borrow();return alias;}void main(){print(get());}",
	        "returned field aliases require managed owner retention", "requires managed owner retention"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmitFailure(
	        "class C{public:string text=\"before\";string& borrow(int ignored){if(ignored>0)return text;return text;}}"
	        "C item=C();int replace(){item=C();return 1;}"
	        "void main(){string& alias=item.borrow(replace());print(alias);}",
	        "escaping sequenced reference must not outlive its receiver", "requires managed owner retention"))
	{
		return EXIT_FAILURE;
	}

	if (!TestAnnotationBudget()) return EXIT_FAILURE;
	for (const char* Source : {
	         "fn()->const int& make(){return [value=7]()->const int&{return value;};}"
	         "void main(){const int& value=make()();print(value);}",
	         "class Owner{public:int value=7;fn()->int& reader(){return [=]()->int&{return value;};}};"
	         "void main(){Owner object=Owner();auto reader=object.reader();print(reader());}"})
	{
		if (!ExpectEmitFailure(Source, "owned closure references must retain their storage", "owned lambda state")) return EXIT_FAILURE;
	}
	for (const char* Source : {
	         "struct State { public: int n=1; }; void main(){ State s=State(); "
	         "fn()->int f=[s]()->int { s=State(); return s.n; }; print(f()); }",
	         "struct State { public: int n=1; }; void main(){ State s=State(); "
	         "fn()->int f=fn()->int { s=State(); return s.n; }; print(f()); }",
	         "struct State<T> { public: T n; }; void main(){ State<int> s=State<int>(); "
	         "fn()->int f=[s]()->int { s=State<int>(); return s.n; }; print(f()); }",
	         "struct State { public: int n=1; }; void replace(State& s){s=State();} "
	         "void main(){State s=State(); fn()->int f=[s]()->int {replace(&s); return s.n;}; print(f());}"})
	{
		if (!ExpectEmitFailure(Source, "captured struct bindings must not be silently rebound",
		        "const"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!ExpectEmit("struct State { public: int n=1; }; void main(){ State s=State(); "
	                "fn()->int f=[s]()->int { State s=State(); s=State(); return s.n; }; print(f()); }",
	        "a shadowed local struct is not a captured binding", {}))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmitFailure(
	        "struct Counter { int value = 0; Counter operator++() { Counter next = Counter(); next.value = value + 1; return next; } }; "
	        "void main() { Counter value = Counter(); ++value; }",
	        "overloaded increment must not produce unusable C++", "overloaded increment/decrement"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmitFailure(
	        "struct Counter { int value = 0; Counter operator++() { Counter next = Counter(); next.value = value + 1; return next; } }; "
	        "T increase<T>(T value) { return ++value; } "
	        "void main() { Counter value = Counter(); value = increase(value); }",
	        "generic increment must reject a user-type specialization", "overloaded increment/decrement"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmitFailure(
	        "struct Counter { int value = 0; Counter operator++() { Counter next = Counter(); next.value = value + 1; return next; } }; "
	        "struct Holder<T> { T advance(T value) { return ++value; } }; "
	        "void main() { Holder<Counter> holder = Holder<Counter>(); Counter value = Counter(); holder.advance(value); }",
	        "generic member increment must reject a user-type parameter", "overloaded increment/decrement"))
	{
		return EXIT_FAILURE;
	}
	const std::vector<std::string> StdlibImportPaths = {
	    VEROSCRIPT_STDLIB_DIR};
	if (!ExpectEmit(R"vs(
int sum_shared<T>(shared<T>... owners) {
    int total = 0;
    for (shared<T> owner : owners) { total += to_int(owner.value); }
    return total;
}
int read_shared<T>(shared<T> owner, int extra = 0) { return to_int(owner.value) + extra; }
shared<T> keep_shared<T>(handle<T> owner) { return owner; }
shared<U> forward_shared<U>(handle<U> owner) { return keep_shared<U>(owner); }
shared<T> forward_same_shared<T>(handle<T> owner) { return keep_shared<T>(owner); }
bool same_shared<T>(shared<T> first, handle<T> second) { return first == second; }
int count_shared<T>(shared<T>... owners) { return len(owners); }
int sum_from<T>(shared<T> first, shared<T>... rest) {
    return to_int(first.value) + sum_shared(rest[0], rest[1]);
}
void clear_shared<T>(ref handle<T> owner) { owner = nullptr; }
void main() {
    auto owner = shared(3);
    assert(sum_shared(owner, owner, owner) == 9);
    assert(owner.sum_shared(owner, owner) == 9);
    assert(sum_shared<int>() == 0 && read_shared(owner) == 3);
    assert(owner.read_shared() == 3);
    assert(sum_from(owner, owner, owner) == 9);
    assert(keep_shared<int>(nullptr) == nullptr);
    assert(forward_shared(owner) == owner && forward_same_shared(owner) == owner);
    assert(!same_shared(nullptr, owner) && !same_shared(owner, nullptr));
    assert(!owner.same_shared(nullptr));
    assert(count_shared<int>(nullptr, owner) == 2);
    owner.clear_shared(); assert(owner == nullptr);
}
)vs",
	        "shared generic variadic, default and reference arguments", {},
	        {"std::vector<std::shared_ptr<vs_SoftHandle<int>>>"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "int main() { return 0; }\n",
	        "explicit int main return is not duplicated",
	        {},
	        {},
	        {"return 0;\n    return 0;"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "struct Payload { int value; Payload(int input) { value = input; } }\n"
	        "class Holder { public: const int id; const Payload payload; "
	        "Holder(int input) : payload(input + 1), id(input) {} }\n"
	        "int main() { Holder value = Holder(4); return value.id + value.payload.value; }\n",
	        "constructor field initializers are emitted",
	        {},
	        {"self.id =", "vs_Payload_init(self.payload,"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmitFailure(
	        "struct BuiltValue { int value; BuiltValue(int left, int right) { value = left + right; } }\n"
	        "struct GenericBuiltField<T> { const T value; "
	        "GenericBuiltField(int left, int right) : value(left, right) {} }\n"
	        "int main() { GenericBuiltField<BuiltValue> value = "
	        "GenericBuiltField<BuiltValue>(2, 3); return value.value.value; }\n",
	        "open type parameter field construction fails during AOT emission",
	        "requires exactly one argument of the same type"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "import \"concurrent.vs\";\n"
	        "void worker(channel<int> ch) {\n"
	        "    int value = ch.recv();\n"
	        "    ch.send(value + 1);\n"
	        "}\n"
	        "void main() {\n"
	        "    channel<int> ch = channel<int>();\n"
	        "    int task_id = std::concurrent::spawn(\"worker\", ch);\n"
	        "    ch.send(41);\n"
	        "    std::concurrent::join_task(task_id);\n"
	        "    print(ch.recv());\n"
	        "}\n",
	        "generic channel extensions",
	        StdlibImportPaths,
	        {"struct VsChannelCloserEntry",
	            "Entry.Lifetime.expired()",
	            "static int Next = 0",
	            "VsCompletedTaskResults()",
	            "VsScriptFail(\"invalid task id\")",
	            "VsRetainTimedOutTask(Id, std::move(Th), std::move(State))",
	            "auto TimedOut = VsTimedOutTasks().find(Id)"}))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmit(
	        "void main() { try { throw 1; } catch (int value) { print(value); } }\n",
	        "scalar exception runtime dependencies",
	        {},
	        {"#include <memory>", "struct vs_ExceptionCarrier"}))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmit(
	        "void main() { print(std::chrono::now()); print(std::chrono::steady_now()); }\n",
	        "chrono runtime does not include sleep dependency",
	        {},
	        {"#include <chrono>",
	            "static inline double VsNowSeconds()",
	            "static inline double VsSteadyNowSeconds()",
	            "std::chrono::duration<double>"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmitFailure(
	        "class Counter { public: int value; ~Counter() { "
	        "fn()->int get = fn() -> int => value; print(get()); } };\n"
	        "int main() { Counter value = Counter(); return 0; }\n",
	        "destructor lambda cannot reacquire an expiring receiver",
	        "a lambda cannot capture class 'this' while the object is being destroyed"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmitFailure(
	        "class Counter { public: ~Counter() { Counter retained = this; } };\n"
	        "int main() { Counter value = Counter(); return 0; }\n",
	        "destructor value cannot reacquire an expiring receiver",
	        "a class 'this' value cannot be retained while the object is being destroyed"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmitFailure(
	        "struct S{int id;S(int n){id=n;}~S(){print(id);}}\n"
	        "void main(){array<S> values=array<S>();values.push(S(1));values.push(S(2));values.remove_at(0);print(\"after\");}\n",
	        "destructible array removal requires VM lifetime handling",
	        "removing array values with script cleanup requires the VM backend") ||
	    !ExpectEmitFailure(
	        "struct S{int id;S(int n){id=n;}~S(){print(id);}}\n"
	        "void main(){map<int,S> values=map<int,S>();values[0]=S(1);print(\"after\");}\n",
	        "destructible map assignment requires VM lifetime handling",
	        "assigning map values with script cleanup requires the VM backend"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "class Pair { public: int a; int b; int sum() const { return a + b; } };\n"
	        "int main() { Pair value = Pair(); value.a = 20; value.b = 22; "
	        "return value.sum(); }\n",
	        "non-escaping class local uses automatic storage",
	        {},
	        {"void vs_Pair_init(vs_Pair& self, const void* ___vs_complete_receiver)",
	            "vs_Pair value{};",
	            "vs_Pair_init(value, VsConstructionAddress(value));",
	            "(value).a = 20",
	            "(value).sum()"},
	        {"std::shared_ptr<vs_Pair> value = vs_Pair_new()"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "class Pair { public: int value; };\n"
	        "Pair make_pair() { Pair local = Pair(); local.value = 7; return local; }\n"
	        "int main() { return make_pair().value; }\n",
	        "escaping class local preserves shared ownership",
	        {},
	        {"std::shared_ptr<vs_Pair> local = vs_Pair_new()",
	            "return local;"},
	        {"vs_Pair local{};"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "class Vec { public: int x; Vec(int value) { x = value; } };\n"
	        "int main() { Vec value = Vec(42); return value.x; }\n",
	        "stack class constructor uses direct self reference",
	        {},
	        {"void vs_Vec_init(vs_Vec& self, int value, const void* ___vs_complete_receiver)",
	            "(self.x = value)",
	            "vs_Vec value{};",
	            "vs_Vec_init(value, 42, VsConstructionAddress(value));"},
	        {"(*self).x"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "class GroupValue { public: int value; };\n"
	        "GroupValue retain(GroupValue value) { return value; }\n"
	        "int main() { GroupValue first = GroupValue(), "
	        "second = retain(first); return second.value; }\n",
	        "later group initializer keeps earlier class local owned",
	        {},
	        {"std::shared_ptr<vs_GroupValue> first = vs_GroupValue_new()",
	            "std::shared_ptr<vs_GroupValue> second = vs_retain_"},
	        {"vs_GroupValue first{};"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "class Counter { public: int value; Counter(int v) { value = v; } "
	        "fn()->int getter() { return fn() -> int => value; } };\n"
	        "fn()->int make_getter() { Counter value = Counter(42); "
	        "return value.getter(); }\n"
	        "int main() { fn()->int get = make_getter(); return get(); }\n",
	        "class local whose method captures this retains shared ownership",
	        {},
	        {"std::shared_ptr<vs_Counter> value = vs_Counter_new(42);",
	            "(*vs_this).value",
	            "[=, vs_this = VsRetainObject(this)]("},
	        {"vs_Counter value{};"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "interface ICounter { virtual int current() = 0; };\n"
	        "struct Counter : ICounter { int value; int current() { return value; } "
	        "fn()->int getter() { "
	        "return fn() -> int => value; } };\n"
	        "int main() { Counter value = Counter(); value.value = 42; "
	        "fn()->int get = value.getter(); value.value = 7; return get(); }\n",
	        "struct lambda captures a value snapshot instead of raw C++ this",
	        {},
	        {"vs_this = *this", "vs_this.value"},
	        {"VsRetainObject(this)", "(*vs_this).value"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "class Observed { public: int value; ~Observed() { print(value); } };\n"
	        "int main() { Observed value = Observed(); value.value = 7; "
	        "return value.value; }\n",
	        "observable destructor keeps class local owned",
	        {},
	        {"std::shared_ptr<vs_Observed> value = vs_Observed_new()"},
	        {"vs_Observed value{};"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "class Cell { public: int value; int& borrow() { return value; } };\n"
	        "int main() { Cell cell = Cell(); int& alias = cell.borrow(); "
	        "alias = 7; return cell.value; }\n",
	        "reference-returning method prevents stack object lifetime escape",
	        {},
	        {"std::shared_ptr<vs_Cell> cell = vs_Cell_new()"},
	        {"vs_Cell cell{};"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "void touch(int& value) { value++; }\n"
	        "class Cell { public: int value; };\n"
	        "int main() { Cell cell = Cell(); touch(ref cell.value); return cell.value; }\n",
	        "field passed across a call keeps owning object storage",
	        {},
	        {"std::shared_ptr<vs_Cell> cell = vs_Cell_new()"},
	        {"vs_Cell cell{};"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "class Holder<T> { public: T value; Holder(T v) { value = v; } "
	        "fn()->T getter() { return fn() -> T => value; } };\n"
	        "int main() { Holder<int> value = Holder<int>(9); "
	        "fn()->int get = value.getter(); return get(); }\n",
	        "open generic class lambda retains instantiated receiver",
	        {},
	        {"vs_this = VsRetainObject(this)", "(*vs_this).value"}))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmit(
	        "void main() { std::console::write(\"x\"); std::console::flush(); "
	        "print(std::console::is_interactive()); print(std::console::clear()); }\n",
	        "console runtime uses raw output and terminal guards",
	        {},
	        {"VsConsoleWrite(", "VsConsoleFlush()", "VsConsoleIsInteractive()",
	            "VsConsoleClear()", "#include <unistd.h>"}))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmit(
	        "struct ErrorBase { public: int code = 1; };\n"
	        "struct ErrorChild : public ErrorBase {};\n"
	        "void main() { try { ErrorChild value(); throw value; } "
	        "catch (ErrorBase& value) { print(value.code); } }\n",
	        "value exception inheritance views",
	        {},
	        {"vs_MakeValueExceptionAs(", "vs_TypeList<vs_ErrorBase>"}))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmit(
	        "int main() {\n"
	        "    int total = 0, next = 1;\n"
	        "    char marker = '\\xFF';\n"
	        "    do { total += next; next++; } while (total < 2);\n"
	        "    for (int i = 0, j = 1; i < 2; i++, j++) { total += j; }\n"
	        "    for (total = 0, next = 1; total < 2; total++, next++) {}\n"
	        "    return marker == '\\xFF' ? total : 0;\n"
	        "}\n",
	        "new core syntax is emitted by C++ AOT",
	        {},
	        {"int total = 0;",
	            "int next = 1;",
	            "std::int8_t marker = static_cast<std::int8_t>(-1);",
	            "do\n",
	            "while ((total < 2));",
	            "for (int i = 0, j = 1;",
	            "(total = 0), (next = 1)",
	            "::VsCheckedIncDec<true, true>(j)"}))
	{
		return EXIT_FAILURE;
	}

	{
		namespace fs = std::filesystem;
		const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
		const fs::path Directory = fs::temp_directory_path() / ("veroscript_cpp_aot_emit_" + std::to_string(Suffix));
		std::error_code FileError;
		fs::create_directories(Directory, FileError);
		const fs::path ScriptPath = Directory / "source.vs";
		const std::string ScriptName = VeroScript::PathUtil::ToUtf8(ScriptPath);
		const std::string Original = "void main() { print(42); }\n";
		{
			std::ofstream Script(ScriptPath, std::ios::binary);
			Script << Original;
		}
		std::string Error;
		if (VeroScript::EmitCppFromFile(
		        ScriptName, ScriptName, Error))
		{
			std::cerr << "FAIL: AOT must reject an output path equal to its input\n";
			fs::remove_all(Directory, FileError);
			return EXIT_FAILURE;
		}
		std::ifstream ScriptAfter(ScriptPath, std::ios::binary);
		std::ostringstream Contents;
		Contents << ScriptAfter.rdbuf();
		ScriptAfter.close();
		if (Contents.str() != Original)
		{
			std::cerr << "FAIL: rejected same-path AOT emission changed its input\n";
			fs::remove_all(Directory, FileError);
			return EXIT_FAILURE;
		}
		const fs::path ProtectedOutput = Directory / "existing.cpp";
		const std::string ProtectedName = VeroScript::PathUtil::ToUtf8(ProtectedOutput);
		const std::string Sentinel = "preserve the existing output\n";
		{
			std::ofstream Existing(ProtectedOutput, std::ios::binary);
			Existing << Sentinel;
		}
		std::string InvalidScript = ScriptName;
		InvalidScript.push_back('\0');
		InvalidScript += "ignored.vs";
		std::string InvalidOutput = ProtectedName;
		InvalidOutput.push_back('\0');
		InvalidOutput += "ignored.cpp";
		if (VeroScript::EmitCppFromFile(InvalidScript, ProtectedName, Error) || Error.find("NUL") == std::string::npos || VeroScript::EmitCppFromFile(ScriptName, InvalidOutput, Error) || Error.find("NUL") == std::string::npos)
		{
			std::cerr << "FAIL: AOT accepted a NUL file path\n";
			fs::remove_all(Directory, FileError);
			return EXIT_FAILURE;
		}
		if (VeroScript::EmitCppFromFile(VeroScript::PathUtil::ToUtf8(Directory), ProtectedName, Error) ||
		    (Error.find("cannot open script") == std::string::npos && Error.find("failed while reading script") == std::string::npos))
		{
			std::cerr << "FAIL: AOT did not report a source file read failure\n";
			fs::remove_all(Directory, FileError);
			return EXIT_FAILURE;
		}
		const fs::path EmptyPath = Directory / "empty.vs";
		{
			std::ofstream Empty(EmptyPath, std::ios::binary);
		}
		if (VeroScript::EmitCppFromFile(VeroScript::PathUtil::ToUtf8(EmptyPath), ProtectedName, Error) ||
		    Error.find("no functions to emit") == std::string::npos)
		{
			std::cerr << "FAIL: an empty AOT source lost its code-generation diagnostic\n";
			fs::remove_all(Directory, FileError);
			return EXIT_FAILURE;
		}
		std::ifstream ProtectedAfter(ProtectedOutput, std::ios::binary);
		std::ostringstream ProtectedContents;
		ProtectedContents << ProtectedAfter.rdbuf();
		ProtectedAfter.close();
		if (ProtectedContents.str() != Sentinel)
		{
			std::cerr << "FAIL: rejected AOT file input changed the existing output\n";
			fs::remove_all(Directory, FileError);
			return EXIT_FAILURE;
		}
		VeroScript::FCppCodegenBackend Backend;
		VeroScript::FCodegenResult Result;
		Result.Output = "previous output";
		if (Backend.EmitFromFile(InvalidScript, {}, Result) || Result.Error.find("NUL") == std::string::npos || !Result.Output.empty())
		{
			std::cerr << "FAIL: AOT backend accepted a NUL script path\n";
			fs::remove_all(Directory, FileError);
			return EXIT_FAILURE;
		}
		{
			std::ofstream Script(ScriptPath, std::ios::binary);
			Script << std::string(16384, ' ') << Original;
		}
		const fs::path OutputPath = Directory / "nested" / fs::u8path("generated-\xE7\xBC\x96\xE8\xAF\x91.cpp");
		if (!VeroScript::EmitCppFromFile(
		        ScriptName, VeroScript::PathUtil::ToUtf8(OutputPath), Error) ||
		    !fs::exists(OutputPath))
		{
			std::cerr << "FAIL: transactional AOT file emission\n"
			          << Error << '\n';
			fs::remove_all(Directory, FileError);
			return EXIT_FAILURE;
		}
		fs::remove_all(Directory, FileError);
	}

	if (!ExpectEmitFailure("class F { public: void operator()(int& x) { x++; } }; void main() { F f = F(); int x = 1; f(x); }",
	        "reference operator source emission is rejected explicitly", "reference operators"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit(
	        "class F { public: int operator+(char n) { return n; } "
	        "int operator[](char n) { return n; } void operator+=(char n) {} }; "
	        "void main() { F f = F(); int n = 300; print(f + n); print(f[n]); f += n; }",
	        "member operator narrowing uses checked numeric conversion", {},
	        {"VsCheckedNumericCast<std::int8_t>(n)"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmitFailure(
	        "class F {}; int operator+(F f, char n) { return n; } "
	        "void main() { int n = 300; print(F() + n); }",
	        "printing cannot hide an unsupported operator expression", "no matching operator overload"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit("class F { public: void operator()(int& x) = delete; }; void main() { print(1); }",
	        "unused deleted reference operator does not block source emission", {}, {" = delete;"}))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectEmit("void operator1(int& n, int delta = 4) { n += delta; } "
	                "class F { public: static void operator_helper(int& n) { n++; } }; "
	                "void main() { int n = 2; F::operator_helper(ref n); operator1(ref n); print(n); }",
	        "ordinary names with operator prefix retain reference parameter support", {}))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmit("void main() { int n = 120; shared<int8> h = shared(n); print(h.value); }",
	        "shared payload construction preserves checked numeric conversion", {},
	        {"VsCheckedNumericCast<std::int8_t>(n)"}))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectEmitFailure("void main(){std::memory::native_addresses_available();}",
	        "runtime memory rejected", "std::memory requires") ||
	    !ExpectEmitFailure("void main(){auto b=new std::memory::buffer(8);}", "buffer use rejected",
	        "std::memory requires") ||
	    !ExpectEmitFailure("void ignore(std::memory::readonly_view v){} void main(){}",
	        "runtime-only signature rejected", "std::memory requires") ||
	    !ExpectEmit(
	        "import \"memory.vs\"; void main(){print(1);}", "unused memory import", StdlibImportPaths) ||
	    !ExpectEmit("import \"core.vs\"; void main(){print(1);}", "unused core import", StdlibImportPaths))
	{
		return EXIT_FAILURE;
	}
	{
		VeroScript::FHostBindingRegistry Bindings;
		std::string Error;
		for (const auto P : {VeroScript::ENativeType::Int, VeroScript::ENativeType::String})
		{
			if (!Bindings.RegisterNative("host_overload", VeroScript::ENativeType::Int, {P},
			        [](const VeroScript::FNativeCallContext&, VeroScript::FValue& R, std::string&)
			{
				R = std::int32_t{1};
				return true;
			}, nullptr, Error))
			{
				return EXIT_FAILURE;
			}
		}
		VeroScript::FCodegenOptions Options;
		Options.Bindings = &Bindings;
		VeroScript::FCppCodegenBackend Backend;
		VeroScript::FCodegenResult Result;
		if (Backend.EmitFromSource("int main(){return host_overload(1);}", "<host-overloads>", Options, Result) || Result.Error.find("Core AOT") == std::string::npos || !Result.Output.empty())
		{
			std::cerr << "standalone host overload boundary lost: " << Result.Error;
			return EXIT_FAILURE;
		}
	}
	{
		VeroScript::FHostBindingRegistry Bindings;
		std::string Error;
		VeroScript::FHostTypeId Type;
		if (!Bindings.RegisterType("OverloadHost", VeroScript::EHostTypeKind::Class, Type, Error)) return EXIT_FAILURE;
		for (const auto Kind : {VeroScript::EHostMemberKind::Method, VeroScript::EHostMemberKind::StaticMethod})
		{
			VeroScript::FHostMemberDesc Method;
			Method.Kind = Kind;
			Method.Name = "read";
			Method.ReturnType = VeroScript::ENativeType::Int;
			Method.ParamTypes = {Kind == VeroScript::EHostMemberKind::Method
			        ? VeroScript::ENativeType::Int
			        : VeroScript::ENativeType::String};
			Method.Callback = [](const VeroScript::FNativeCallContext&, VeroScript::FValue& R, std::string&)
			{
				R = std::int32_t{1};
				return true;
			};
			if (!Bindings.RegisterMember(Type, Method, Error)) return EXIT_FAILURE;
		}
		VeroScript::FCodegenOptions Options;
		Options.Bindings = &Bindings;
		VeroScript::FCppCodegenBackend Backend;
		VeroScript::FCodegenResult Result;
		if (Backend.EmitFromSource("int main(){return OverloadHost::read(\"x\");}", "<host-method-overloads>", Options, Result) || Result.Error.find("Core AOT") == std::string::npos || !Result.Output.empty())
		{
			std::cerr << "standalone host method overload boundary lost: " << Result.Error;
			return EXIT_FAILURE;
		}
	}
	for (int Index = 0; Index < 8; ++Index)
	{
		const std::string Receiver = "LateGenericInterfaceReceiver" + std::to_string(Index);
		const std::string Following = "FollowingConcreteDeclaration" + std::to_string(Index);
		const std::string Source =
		    "interface IParent<T>{virtual T get()=0;}\n"
		    "interface IChild<T>:IParent<T>{virtual void set(T value)=0;}\n"
		    "class " +
		    Receiver + ":public IChild<int>{public:int value;"
		               "int get(){return value;}void set(int input){value=input;}}\n"
		               "class " +
		    Following + "{public:int read(){return 9;}}\n"
		                "int readChild<T>(T value)where T:IChild<int>{return value.get();}\n"
		                "void main(){" +
		    Receiver + " value=" + Receiver + "();value.set(8);"
		                                      "print(readChild(value));" +
		    Following + " next=" + Following + "();print(next.read());}\n";
		if (!ExpectEmit(Source, "late generic interface layouts preserve following declarations", {},
		        {"struct vs_" + Receiver, "struct vs_" + Following, "std::is_convertible_v"}))
		{
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

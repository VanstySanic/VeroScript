// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCppTranspiler.h"
#include "VeroScriptCppToolOutput.h"
#include "VeroScriptPath.h"
#include "VeroScriptAst.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptVm.h"
#if defined(VEROSCRIPT_CPP_TRANSPILE_AOT_TEST)
#include "VeroScriptCppAot.h"
#endif

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
	bool Expect(const bool Condition, const char* Message)
	{
		if (Condition)
		{
			return true;
		}
		std::cerr << Message << '\n';
		return false;
	}

	bool TestNumericSemantics(const char* NativeOutput)
	{
		// Use the host C++ data model as the reference, including LLP64/LP64.
		const auto Maximum = std::to_string(std::numeric_limits<unsigned long>::max());
		const std::string Source =
		    "enum class Flags : unsigned int {All=~0u, Wrap=0xffffffffu+1u, More=All>1};"
		    "enum Loose {LooseValue=1u, LooseCheck=(LooseValue-2)>0};"
		    "enum class SignedBounds : long long {Min=-9223372036854775807ll-1ll, Max=9223372036854775807ll};"
		    "enum class IntBounds : int {Min=1<<31};"
		    "bool Wrapped(){unsigned long value=" +
		    Maximum + "UL;value+=1UL;return value==0UL;}"
		              "bool AutoWrapped(){auto value=" +
		    Maximum + "LU;value+=1UL;return value==0UL;}"
		              "bool CastWrapped(){auto value=static_cast<unsigned long>(" +
		    Maximum + "UL);value+=1UL;return value==0UL;}"
		              "void main(){print(Wrapped());print(AutoWrapped());print(CastWrapped());"
		              "auto mask=0xffffffff;mask+=1U;print(mask==0U);"
		              "long long wide=1LL;print(wide);auto unsignedWide=1ULL;print(unsignedWide);"
		              "std::ptrdiff_t distance=2;std::intptr_t pointer=3;std::uintptr_t address=4U;"
		              "print(distance);print(pointer);print(address);print(077);print(0b11'11);"
		              "print(static_cast<unsigned int>(Flags::All));print(static_cast<unsigned int>(Flags::Wrap));"
		              "print(static_cast<unsigned int>(Flags::More));print(static_cast<int>(LooseCheck));"
		              "print(static_cast<long long>(SignedBounds::Min));print(static_cast<long long>(SignedBounds::Max));"
		              "print(static_cast<int>(IntBounds::Min));}";
		const auto Result = VeroScript::CppTranspiler::TranspileSource(Source, "numeric-types.cpp");
		if (!Result.Succeeded())
		{
			for (const auto& D : Result.Diagnostics)
			{
				std::cerr << VeroScript::CppFrontend::FormatDiagnostic(D) << '\n';
			}
		}

		if (!Expect(Result.Succeeded(), "local numeric types, casts and literals transpile consistently"))
		{
			return false;
		}
		VeroScript::VeroScriptModuleLoader Loader("");
		auto Program = Loader.LoadSourceModule(Result.Source, "numeric-types.vs");
		if (!Expect(Program && !Loader.HasError(), "numeric fixture loads")) return false;
		VeroScript::VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Expect(Module && !Compiler.HasError(), "numeric fixture compiles")) return false;
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		const std::string Expected = "true\ntrue\ntrue\ntrue\n1\n1\n2\n3\n4\n63\n15\n4294967295\n0\n1\n1\n-9223372036854775808\n9223372036854775807\n-2147483648\n";
		bool Ok = Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str() == Expected,
		    "transpiled numeric execution matches C++ widths, unsigned wrapping and literal bases");
		if (!Ok)
		{
			std::cerr << Output.str() << Vm.GetError().Message << '\n';
		}

		#if defined(VEROSCRIPT_CPP_TRANSPILE_AOT_TEST)
		if (NativeOutput)
		{
			std::string Cpp, Error;
			if (!VeroScript::EmitCppFromProgram(*Program, Cpp, Error))
			{
				std::cerr << Error << '\n';
				return false;
			}
			const auto Path = std::filesystem::u8path(NativeOutput).parent_path() / "numeric.cpp";
			std::filesystem::create_directories(Path.parent_path());
			std::ofstream File(Path);
			File << Cpp;
			File.close();
			Ok &= Expect(bool(File), "write native numeric pipeline fixture");
		}
		#else
		(void)NativeOutput;
		#endif
		for (const char* Unsupported : {"auto x=18446744073709551616ULL;", "auto x=18446744073709551615;",
		         "auto x=1.0L;", "auto x=0x1p2;", "auto x=1''2;", "auto x=1lL;", "auto x=1.0_custom;"})
		{
			const auto Rejected =
			    VeroScript::CppTranspiler::TranspileSource(std::string("void main(){") + Unsupported + "}");
			Ok &= Expect(!Rejected.Succeeded() &&
			        std::any_of(Rejected.Diagnostics.begin(), Rejected.Diagnostics.end(),
			            [](const auto& D)
			{
				return D.Code == "cpp.transpile.numeric_literal_unsupported";
			}),
			    "unrepresentable numeric literals fail explicitly");
		}
		return Ok;
	}

	bool TestProjectDefinitionScope(const char* NativeOutput)
	{
		const auto Root = std::filesystem::temp_directory_path() / ("veroscript_cpp_definition_scope_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Root);
		struct FCleanup
		{
			~FCleanup()
			{
				std::error_code Error;
				std::filesystem::remove_all(Path, Error);
			}

			std::filesystem::path Path;
		} Cleanup{Root};
		const auto Header = Root / "decl.h";
		const auto Implementation = Root / "impl.cpp";
		{
			std::ofstream File(Header);
			File << "namespace API { using Number=int; "
			        "class Reader { public: virtual Number Read(Number declared=0); "
			        "Number AddedDefault(Number declared); static Number Static(Number declared=1); "
			        "Number Const(Number declared=2) const; Number Mutate(const Number declared=3); }; "
			        "class Child:public Reader { public: int Read(int declared) override; }; "
			        "template<typename T> struct Box { T value; Box(T initial){value=initial;} T Get() const {return value;} }; }";
		}
		{
			std::ofstream File(Implementation);
			File << "namespace Helpers { int Value(){return 42;} } "
			        "using namespace Helpers; int API::Reader::Read(int input){return Value()+input;} "
			        "int API::Reader::AddedDefault(const int input=3){return Value()+input;} "
			        "int API::Reader::Static(int input){return Value()+input;} "
			        "int API::Reader::Const(int input) const {return Value()+input;} "
			        "int API::Reader::Mutate(int input){input+=1;return Value()+input;} "
			        "int API::Child::Read(int input){return Value()+input+1;} "
			        "void main(){API::Reader reader=API::Child();print(reader.Read()); "
			        "API::Box<int> box=API::Box<int>(7);print(box.Get());"
			        "print(reader.AddedDefault());print(API::Reader::Static());print(reader.Const());print(reader.Mutate());}";
		}
		const auto Project =
		    VeroScript::CppTranspiler::TranspileFiles({Header.string(), Implementation.string()});
		if (!Project.Succeeded())
		{
			for (const auto& Diagnostic : Project.Diagnostics)
			{
				std::cerr << VeroScript::CppFrontend::FormatDiagnostic(Diagnostic) << '\n';
			}
		}

		if (!Expect(Project.Succeeded() && Project.Files.size() == 2, "definition scope project transpiles"))
		{
			return false;
		}
		std::vector<std::string> Paths;
		for (const auto& File : Project.Files)
		{
			Paths.push_back((Root / File.OutputName).string());
			std::ofstream Output(Paths.back());
			Output << File.Source;
		}
		VeroScript::VeroScriptModuleLoader Loader(Paths.back());
		Loader.SetAllowProjectWideAccess(true);
		for (const auto& Path : Paths)
		{
			Loader.AddImplicitImportPath(Path);
		}

		auto Program = Loader.LoadMainModule();
		if (!Expect(Program && !Loader.HasError(), "definition scope generated modules load")) return false;
		VeroScript::VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			std::cerr << "generated project loses implementation lookup scope: " << Compiler.GetError().Message << '\n';
			return false;
		}
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script) &&
		            Output.str() == "43\n7\n45\n43\n44\n46\n",
		        "project definitions retain lookup scope, dispatch, defaults, parameter constness, static/const methods, and generic values"))
		{
			return false;
		}

		#if defined(VEROSCRIPT_CPP_TRANSPILE_AOT_TEST)
		if (NativeOutput)
		{
			std::string Cpp, Error;
			if (!VeroScript::EmitCppFromProgram(*Program, Cpp, Error))
			{
				std::cerr << Error << '\n';
				return false;
			}
			const auto Path = std::filesystem::u8path(NativeOutput);
			std::filesystem::create_directories(Path.parent_path());
			std::ofstream File(Path);
			File << Cpp;
			File.close();
			if (!Expect(bool(File), "write native project pipeline fixture")) return false;
		}
		#else
		(void)NativeOutput;
		#endif
		{
			std::ofstream File(Header);
			File << "namespace N { namespace std { using uint32_t=unsigned short; } }";
		}
		{
			std::ofstream File(Implementation);
			File << "namespace N { unsigned int Read(){std::uint32_t value=65535U;value+=1U;return value;} }";
		}
		VeroScript::CppTranspiler::FTranspileProjectOptions NamespaceOptions;
		NamespaceOptions.bValidateGeneratedSources = false;
		const auto NamespaceShadow = VeroScript::CppTranspiler::TranspileFiles(
		    {Header.string(), Implementation.string()}, NamespaceOptions);
		if (!Expect(!NamespaceShadow.Succeeded() && NamespaceShadow.Files.empty() && std::any_of(NamespaceShadow.Diagnostics.begin(), NamespaceShadow.Diagnostics.end(), [](const auto& Diagnostic)
		{
			return Diagnostic.Code == "cpp.transpile.standard_namespace_shadowed";
		}),
		        "local std namespaces in sibling inputs must be rejected before any project output"))
		{
			return false;
		}
		const auto RunNamespaceProject = [&](const VeroScript::CppTranspiler::FTranspileProjectResult& Project)
		{
			if (!Expect(Project.Succeeded() && Project.Files.size() == 2,
			        "an unrelated source file's namespace imports must not reject a valid project"))
			{
				return false;
			}
			std::vector<std::string> ModulePaths;
			for (const auto& File : Project.Files)
			{
				ModulePaths.push_back((Root / File.OutputName).string());
				std::ofstream Generated(ModulePaths.back());
				Generated << File.Source;
			}
			VeroScript::VeroScriptModuleLoader ProjectLoader(ModulePaths.back());
			ProjectLoader.SetAllowProjectWideAccess(true);
			for (const auto& Path : ModulePaths)
			{
				ProjectLoader.AddImplicitImportPath(Path);
			}
			auto ProjectProgram = ProjectLoader.LoadMainModule();
			VeroScript::VeroScriptCompiler ProjectCompiler;
			auto ProjectModule = ProjectProgram ? ProjectCompiler.Compile(*ProjectProgram) : nullptr;
			std::ostringstream ProjectOutput;
			VeroScript::VeroScriptVm ProjectVm(ProjectOutput);
			VeroScript::Test::Configure(ProjectVm);
			return Expect(ProjectModule && ProjectVm.Execute(*ProjectModule, VeroScript::ERunMode::Script) && ProjectOutput.str() == "65536\n", "namespace imports retain their original source file's numeric meaning");
		};
		for (const bool ReopenNamespace : {false, true})
		{
			const std::string Prefix = ReopenNamespace ? "namespace Shared {" : "";
			const std::string Suffix = ReopenNamespace ? "}" : "";
			{
				std::ofstream File(Header);
				File << "namespace Local {namespace std {using uint32_t=unsigned short;}} " << Prefix
				     << "using namespace Local; int Unrelated(){return 0;} " << Suffix;
			}
			{
				std::ofstream File(Implementation);
				File << "#include <cstdint>\n"
				     << Prefix
				     << "unsigned int Read(){std::uint32_t value=65535U;value+=1U;return value;} " << Suffix
				     << "void main(){print(" << (ReopenNamespace ? "Shared::Read" : "Read") << "());}";
			}
			if (!RunNamespaceProject(VeroScript::CppTranspiler::TranspileFiles(
			        {Header.string(), Implementation.string()})))
			{
				return false;
			}
		}
		// A provider may supply a field declaration and initializer tokens from
		// different inputs. The token origin, rather than the field location, owns
		// the using context; no additional C++ declaration syntax is needed here.
		auto InitializerUnit = VeroScript::CppFrontend::ParseSource(
		    "namespace Local {namespace std {using uint32_t=unsigned short;}} "
		    "using namespace Local; struct Storage {inline static unsigned int Value=1U;}; "
		    "void main(){print(Storage::Value);}",
		    Header.string());
		const auto InitializerBody = VeroScript::CppFrontend::ParseSource(
		    "unsigned int Init(){return static_cast<std::uint32_t>(65535U)+1U;}", Implementation.string());
		if (!Expect(InitializerUnit.Succeeded() && InitializerUnit.Unit.Records.size() == 1 && InitializerUnit.Unit.Records[0].Fields.size() == 1 && InitializerBody.Succeeded() && InitializerBody.Unit.Functions.size() == 1 && InitializerBody.Unit.Functions[0].BodyTokens.size() > 2,
		        "field initializer origin fixture must parse"))
		{
			return false;
		}
		const auto& InitializerTokens = InitializerBody.Unit.Functions[0].BodyTokens;
		InitializerUnit.Unit.Records[0].Fields[0].InitializerTokens.assign(InitializerTokens.begin() + 1, InitializerTokens.end() - 1);
		const auto InitializerSource = VeroScript::CppTranspiler::Transpile(InitializerUnit.Unit);
		if (!Expect(InitializerSource.Succeeded(), "field initializer tokens retain their own input context")) return false;
		VeroScript::VeroScriptModuleLoader InitializerLoader("");
		auto InitializerProgram = InitializerLoader.LoadSourceModule(InitializerSource.Source, "initializer-origin.vs");
		VeroScript::VeroScriptCompiler InitializerCompiler;
		auto InitializerModule = InitializerProgram ? InitializerCompiler.Compile(*InitializerProgram) : nullptr;
		std::ostringstream InitializerOutput;
		VeroScript::VeroScriptVm InitializerVm(InitializerOutput);
		VeroScript::Test::Configure(InitializerVm);
		if (!Expect(InitializerModule && InitializerVm.Execute(*InitializerModule, VeroScript::ERunMode::Script) && InitializerOutput.str() == "65536\n", "field initializer conversion uses the initializer token's namespace context"))
		{
			return false;
		}
		{
			std::ofstream File(Header);
			File << "namespace Local {namespace std {using uint32_t=unsigned short;}} "
			        "using namespace Local; int Unrelated(){return 0;}";
		}
		{
			std::ofstream File(Implementation);
			File << "using namespace Local; unsigned int Read(){std::uint32_t value=65535U;value+=1U;return value;}";
		}
		const auto RepeatedNamespaceImport = VeroScript::CppTranspiler::TranspileFiles(
		    {Header.string(), Implementation.string()}, NamespaceOptions);
		if (!Expect(!RepeatedNamespaceImport.Succeeded() && RepeatedNamespaceImport.Files.empty() && std::any_of(RepeatedNamespaceImport.Diagnostics.begin(), RepeatedNamespaceImport.Diagnostics.end(), [&](const auto& Diagnostic)
		{
			return Diagnostic.Code == "cpp.transpile.standard_namespace_shadowed" && Diagnostic.Location.File == Implementation.string();
		}),
		        "matching namespace imports in different inputs must each retain their diagnostics"))
		{
			return false;
		}
		{
			std::ofstream File(Header);
			File << "template<typename T,typename U> struct pair {T first=7;U second=9;};";
		}
		{
			std::ofstream File(Implementation);
			File << "void main(){std::pair<int,int> value=std::pair<int,int>();print(value.first);}";
		}
		VeroScript::CppTranspiler::FTranspileProjectOptions PairOptions;
		PairOptions.bValidateGeneratedSources = false;
		const auto PairCollision = VeroScript::CppTranspiler::TranspileFiles({Header.string(), Implementation.string()}, PairOptions);
		if (!Expect(!PairCollision.Succeeded() && PairCollision.Files.empty() &&
		            std::any_of(PairCollision.Diagnostics.begin(), PairCollision.Diagnostics.end(),
		                [](const auto& Diagnostic)
		{
			return Diagnostic.Code == "cpp.transpile.standard_pair_shadowed";
		}),
		        "standard pair collisions in sibling declarations are rejected before project output"))
		{
			return false;
		}

		{
			std::ofstream File(Header);
			File << "template<typename T> struct pair {T value=7;};";
		}
		if (!Expect(VeroScript::CppTranspiler::TranspileFiles({Header.string(), Implementation.string()})
		                .Succeeded(),
		        "project records of another pair arity preserve the standard pair"))
		{
			return false;
		}

		{
			std::ofstream File(Header);
			File << "struct Reader { int Read(int declared); };";
		}
		{
			std::ofstream File(Implementation);
			File << "int Reader::Read(const int input){input+=1;return input;}";
		}
		const auto ConstMutation =
		    VeroScript::CppTranspiler::TranspileFiles({Header.string(), Implementation.string()});
		if (!Expect(!ConstMutation.Succeeded() && ConstMutation.Files.empty() &&
		            std::any_of(ConstMutation.Diagnostics.begin(), ConstMutation.Diagnostics.end(),
		                [&](const auto& Diagnostic)
		{
			return Diagnostic.Code == "cpp.transpile.generated_source_invalid" &&
			    Diagnostic.Location.File == Implementation.string() &&
			    Diagnostic.Message.find("const") != std::string::npos;
		}),
		        "definition parameter constness must reject mutation and preserve the implementation error location"))
		{
			return false;
		}

		{
			std::ofstream File(Header);
			File << "struct Reader { int Read(int declared); };";
		}
		{
			std::ofstream File(Implementation);
			File << "int Value(){return 42;} "
			        "int Reader::Read(int input=Value()){return input;}";
		}
		VeroScript::CppTranspiler::FTranspileProjectOptions NoValidation;
		NoValidation.bValidateGeneratedSources = false;
		const auto InvalidDefault = VeroScript::CppTranspiler::TranspileFiles(
		    {Header.string(), Implementation.string()}, NoValidation);
		if (!Expect(!InvalidDefault.Succeeded() && InvalidDefault.Files.empty() &&
		            std::any_of(InvalidDefault.Diagnostics.begin(), InvalidDefault.Diagnostics.end(),
		                [&](const auto& Diagnostic)
		{
			return Diagnostic.Code ==
			    "cpp.transpile.default_argument_expression_unsupported" &&
			    Diagnostic.Location.File == Implementation.string();
		}),
		        "definition defaults must be validated without compile checks and report their original location"))
		{
			return false;
		}

		const auto UnicodeHeader = Root / std::filesystem::u8path("\xC3\x89.h");
		const auto UnicodeImplementation = Root / std::filesystem::u8path("\xC3\xA9.cpp");
		{
			std::ofstream File(UnicodeHeader);
			File << "int First(){return 1;}";
		}
		{
			std::ofstream File(UnicodeImplementation);
			File << "int Second(){return 2;} void main(){print(First()+Second());}";
		}
		const auto UnicodeNames =
		    VeroScript::CppTranspiler::TranspileFiles({VeroScript::PathUtil::ToUtf8(UnicodeHeader),
		        VeroScript::PathUtil::ToUtf8(UnicodeImplementation)});
		const VeroScript::CppTools::FOutputFileNameLess NameLess;
		if (!Expect(UnicodeNames.Succeeded() && UnicodeNames.Files.size() == 2 &&
		            (NameLess(UnicodeNames.Files[0].OutputName, UnicodeNames.Files[1].OutputName) ||
		                NameLess(UnicodeNames.Files[1].OutputName, UnicodeNames.Files[0].OutputName)),
		        "Unicode case-colliding basenames must generate distinct valid project modules"))
		{
			return false;
		}

		{
			std::ofstream File(Header);
			File << "using Value=int; namespace N { "
			        "struct Value{int field;}; int Read(Value value); int Read(int value); }";
		}
		{
			std::ofstream File(Implementation);
			File << "namespace N { "
			        "int Read(Value value){return value.field;} int Read(int value){return value;} } "
			        "void main(){N::Value value=N::Value(); value.field=42; print(N::Read(value)+N::Read(1));}";
		}
		const auto ShadowedAliases =
		    VeroScript::CppTranspiler::TranspileFiles({Header.string(), Implementation.string()});
		if (!ShadowedAliases.Succeeded())
		{
			for (const auto& D : ShadowedAliases.Diagnostics)
			{
				std::cerr << VeroScript::CppFrontend::FormatDiagnostic(D) << '\n';
			}
		}

		if (!Expect(ShadowedAliases.Succeeded() && ShadowedAliases.Files.size() == 2,
		        "project alias lookup must preserve record overloads and match their definitions"))
		{
			return false;
		}

		{
			std::ofstream File(Header);
			File << "struct RootItem{int value;}; using RootAlias=RootItem; "
			        "namespace N{struct RootItem{double other;}; template<typename T> using Identity=T;} "
			        "using NestedIdentity=N::Identity<RootItem>;";
		}
		{
			std::ofstream File(Implementation);
			File << "namespace N{RootAlias Echo(RootAlias input){return input;} "
			        "NestedIdentity EchoNested(NestedIdentity input){return input;}} "
			        "void main(){RootItem value=RootItem(); value.value=42; "
			        "print(N::Echo(value).value+N::EchoNested(value).value);}";
		}
		const auto GlobalAliases =
		    VeroScript::CppTranspiler::TranspileFiles({Header.string(), Implementation.string()});
		if (!GlobalAliases.Succeeded())
		{
			for (const auto& D : GlobalAliases.Diagnostics)
			{
				std::cerr << VeroScript::CppFrontend::FormatDiagnostic(D) << '\n';
			}
		}

		if (!Expect(GlobalAliases.Succeeded() && GlobalAliases.Files.size() == 2,
		        "project aliases and generic arguments keep global lookup across namespace boundaries"))
		{
			return false;
		}

		std::vector<std::string> GlobalPaths;
		for (const auto& File : GlobalAliases.Files)
		{
			GlobalPaths.push_back((Root / File.OutputName).string());
			std::ofstream GlobalFile(GlobalPaths.back());
			GlobalFile << File.Source;
		}
		VeroScript::VeroScriptModuleLoader GlobalLoader(GlobalPaths.back());
		GlobalLoader.SetAllowProjectWideAccess(true);
		for (const auto& Path : GlobalPaths)
		{
			GlobalLoader.AddImplicitImportPath(Path);
		}

		auto GlobalProgram = GlobalLoader.LoadMainModule();
		VeroScript::VeroScriptCompiler GlobalCompiler;
		auto GlobalModule = GlobalProgram ? GlobalCompiler.Compile(*GlobalProgram) : nullptr;
		std::ostringstream GlobalOutput;
		VeroScript::VeroScriptVm GlobalVm(GlobalOutput);
		VeroScript::Test::Configure(GlobalVm);
		if (!Expect(GlobalModule && GlobalVm.Execute(*GlobalModule, VeroScript::ERunMode::Script) &&
		            GlobalOutput.str() == "84\n",
		        "generated project calls preserve the actual global object layout and return values"))
		{
			return false;
		}

		{
			std::ofstream File(Header);
			File << "using T=int;";
		}
		{
			std::ofstream File(Implementation);
			File << "template<typename T> struct Box{T value;};";
		}
		const auto ShadowedParameter = VeroScript::CppTranspiler::TranspileFiles(
		    {Header.string(), Implementation.string()}, NoValidation);
		if (!Expect(!ShadowedParameter.Succeeded() && ShadowedParameter.Files.empty() &&
		            std::any_of(ShadowedParameter.Diagnostics.begin(), ShadowedParameter.Diagnostics.end(),
		                [](const auto& D)
		{
			return D.Code == "cpp.transpile.template_parameter_shadow_unsupported";
		}),
		        "project preflight rejects target template-name collisions across sibling files without validation"))
		{
			return false;
		}

		// Constructors still require a body in their owner's module in the core.
		// Actual-module validation must reject a scope-losing merge rather than
		// accepting the merged unit and returning an unusable project.
		{
			std::ofstream File(Header);
			File << "struct Broken { int value; Broken(); };";
		}
		{
			std::ofstream File(Implementation);
			File
			    << "namespace Helpers { int Value(){return 42;} } "
			       "using namespace Helpers; Broken::Broken(){value=Value();} void main(){Broken x=Broken();}";
		}
		const auto Invalid = VeroScript::CppTranspiler::TranspileFiles({Header.string(), Implementation.string()});
		return Expect(!Invalid.Succeeded() && Invalid.Files.empty() &&
		        std::any_of(Invalid.Diagnostics.begin(), Invalid.Diagnostics.end(),
		            [&](const auto& Diagnostic)
		{
			return Diagnostic.Code == "cpp.transpile.generated_source_invalid" &&
			    Diagnostic.Location.File == Implementation.string();
		}),
		    "invalid generated module graphs fail with diagnostics mapped to the original C++ file");
	}
}

int main(int Argc, char** Argv)
{
	using namespace VeroScript::CppTranspiler;
	bool Ok = true;
	{
		for (const bool Validate : {true, false})
		{
			FTranspileOptions Options;
			Options.bValidateGeneratedSource = Validate;
			for (const char* Source : {
			         "namespace N { namespace std { using uint32_t=unsigned short; } "
			         "unsigned int Read(){std::uint32_t value=65535U;value+=1U;return value;} }",
			         "namespace N { namespace std { using uint32_t=unsigned short; } } "
			         "unsigned int Read(){::N::std::uint32_t value=65535U;value+=1U;return value;}",
			         "namespace Outer { namespace N { namespace std { using uint32_t=unsigned short; } } "
			         "namespace M {using namespace N;} using namespace M; "
			         "unsigned int Read(){std::uint32_t value=65535U;value+=1U;return value;} }",
			         "namespace Local {namespace std {using uint32_t=unsigned short;}} "
			         "namespace Shared {using namespace Local;} namespace Shared {"
			         "unsigned int Read(){std::uint32_t value=65535U;value+=1U;return value;} }"})
			{
				const auto Shadowed = TranspileSource(Source, "local-std-namespace.cpp", Options);
				Ok &= Expect(!Shadowed.Succeeded() && Shadowed.Source.empty() && std::any_of(Shadowed.Diagnostics.begin(), Shadowed.Diagnostics.end(), [](const auto& Diagnostic)
				{
					return Diagnostic.Code == "cpp.transpile.standard_namespace_shadowed";
				}),
				    "local std namespaces must not silently become standard-library builtins");
			}
		}
		Ok &= Expect(TranspileSource(
		                 "namespace N { namespace std { using uint32_t=unsigned short; } } "
		                 "void main(){std::uint32_t value=65535U;value+=1U;print(value);}",
		                 "unrelated-std-namespace.cpp")
		                 .Succeeded(),
		    "an unrelated local std namespace must not hide the global standard library");
		Ok &= Expect(TranspileSource(
		                 "namespace N { namespace std { using uint32_t=unsigned short; } "
		                 "::std::uint32_t Identity(::std::uint32_t value){return value;} } "
		                 "void main(){print(N::Identity(65536U));}",
		                 "global-standard-types.cpp")
		                 .Succeeded(),
		    "explicit global standard types retain their existing structured type mapping");
		Ok &= Expect(!TranspileSource(
		                 "namespace Local {namespace std {using uint32_t=unsigned short;}} namespace Alias=Local; "
		                 "unsigned int Read(){Alias::std::uint32_t value=65535U;value+=1U;return value;}",
		                 "unsupported-namespace-alias.cpp")
		                 .Succeeded(),
		    "namespace aliases remain explicitly outside the source frontend subset");
	}
	{
		const auto Bases =
		    TranspileSource("class Base{public:int root;}; namespace N{ "
		                    "class Base{public:int local;}; class Child:public Base{};} "
		                    "void main(){N::Child value=N::Child(); value.local=42; print(value.local);}",
		        "scoped-bases.cpp");
		if (!Bases.Succeeded())
		{
			for (const auto& D : Bases.Diagnostics)
			{
				std::cerr << VeroScript::CppFrontend::FormatDiagnostic(D) << '\n';
			}
		}

		Ok &= Expect(Bases.Succeeded(),
		    "base lookup must retain the enclosing namespace instead of selecting a global namesake");
		const auto GenericBase = TranspileSource(
		    "template<typename T> class Storage{public:T value;}; "
		    "class Child:public Storage<double>{}; void main(){Child value=Child(); value.value=2.5; print(value.value);}",
		    "generic-bases.cpp");
		if (!GenericBase.Succeeded())
		{
			for (const auto& D : GenericBase.Diagnostics)
			{
				std::cerr << VeroScript::CppFrontend::FormatDiagnostic(D) << '\n';
			}
		}

		Ok &= Expect(GenericBase.Succeeded(), "base types retain their structured generic arguments");
		FTranspileOptions NoValidation;
		NoValidation.bValidateGeneratedSource = false;
		for (const char* Base : {"::Base", "RootAlias"})
		{
			const auto Hidden = TranspileSource(std::string("class Base{public:int root;};using RootAlias=Base;namespace N{class Base{public:double local;};class Child:public ") + Base + "{};}", "global-base-shadow.cpp", NoValidation);
			Ok &= Expect(!Hidden.Succeeded() && Hidden.Source.empty() &&
			        std::any_of(Hidden.Diagnostics.begin(), Hidden.Diagnostics.end(),
			            [](const auto& D)
			{
				return D.Code == "cpp.transpile.global_base_shadowed";
			}),
			    "hidden global bases must fail before output when target syntax cannot preserve root lookup");
		}
		for (const char* Source : {"class Child:public std::vector<int>{};",
		         "template<typename T> class Child:public T{};"})
		{
			const auto Unsupported = TranspileSource(Source, "unsupported-base.cpp", NoValidation);
			Ok &= Expect(!Unsupported.Succeeded() && Unsupported.Source.empty(),
			    "builtin containers and dependent parameter bases cannot become script inheritance even without validation");
		}
	}
	{
		const auto Generic = TranspileSource(
		    "using T=int; template<typename T> using Items=std::vector<T>; "
		    "template<typename U> struct Box{U Value;}; template<typename U> U First(Items<U> values){return values[0];} "
		    "int main(){Box<double> box=Box<double>(); box.Value=2.5; std::vector<double> values={box.Value}; "
		    "print(First<double>(values)); return 0;}",
		    "parameter-alias-shadow.cpp");
		if (!Generic.Succeeded())
		{
			for (const auto& D : Generic.Diagnostics)
			{
				std::cerr << VeroScript::CppFrontend::FormatDiagnostic(D) << '\n';
			}
		}

		Ok &= Expect(
		    Generic.Succeeded(), "template parameters shadow aliases throughout the transpilation pipeline");
		if (Generic.Succeeded())
		{
			VeroScript::VeroScriptModuleLoader Loader("");
			auto Program = Loader.LoadSourceModule(Generic.Source, "parameter-alias-shadow.vs");
			VeroScript::VeroScriptCompiler Compiler;
			auto Module = Program ? Compiler.Compile(*Program) : nullptr;
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Ok &= Expect(Module && Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str() == "2.5\n", "generic alias substitution retains the instantiated value type");
		}
		FTranspileOptions NoValidation;
		NoValidation.bValidateGeneratedSource = false;
		const auto Unsupported = TranspileSource("using T=int; template<typename T> T Identity(T value){return value;}",
		    "parameter-name-collision.cpp", NoValidation);
		Ok &= Expect(!Unsupported.Succeeded() && Unsupported.Source.empty() &&
		        std::any_of(Unsupported.Diagnostics.begin(), Unsupported.Diagnostics.end(),
		            [](const auto& D)
		{
			return D.Code == "cpp.transpile.template_parameter_shadow_unsupported";
		}),
		    "template parameter shadows unsupported by the target must fail before output even without validation");
		Ok &= Expect(TranspileSource("namespace N{using T=int; template<typename T> struct Box{T Value;};}",
		                 "qualified-parameter-shadow.cpp")
		                 .Succeeded(),
		    "qualified aliases must not unnecessarily reserve their leaf names as template parameters");
	}
	{
		const auto PairResult = TranspileSource(
		    "void main(){std::pair<int, int> value=std::pair<int, int>(); value.first=8; value.second=13; print(value.first+value.second);}",
		    "lowercase-pair.cpp");
		if (!PairResult.Succeeded())
		{
			for (const auto& D : PairResult.Diagnostics)
			{
				std::cerr << VeroScript::CppFrontend::FormatDiagnostic(D) << '\n';
			}
		}

		Ok &= Expect(PairResult.Succeeded() && PairResult.Source.find("pair") != std::string::npos &&
		        PairResult.Source.find("Pair<") == std::string::npos,
		    "C++ std::pair transpiles to lowercase pair");
		if (PairResult.Succeeded())
		{
			VeroScript::VeroScriptModuleLoader Loader("");
			auto Program = Loader.LoadSourceModule(PairResult.Source, "lowercase-pair.vs");
			VeroScript::VeroScriptCompiler Compiler;
			auto Module = Program ? Compiler.Compile(*Program) : nullptr;
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Ok &= Expect(Module && Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str() == "21\n", "transpiled lowercase pair executes through the core");
		}
	}
	Ok &= TestNumericSemantics(Argc > 1 ? Argv[1] : nullptr);
	{
		const auto Collision = TranspileSource("template<typename T,typename U> struct pair {T first=7;U second=9;};"
		                                       "void main(){std::pair<int,int> value=std::pair<int,int>();print(value.first);}",
		    "pair-collision.cpp");
		Ok &= Expect(!Collision.Succeeded() && Collision.Source.empty() &&
		        std::any_of(Collision.Diagnostics.begin(), Collision.Diagnostics.end(),
		            [](const auto& D)
		{
			return D.Code == "cpp.transpile.standard_pair_shadowed";
		}),
		    "std::pair must not select a user pair template after transpilation");
		FTranspileOptions NoValidation;
		NoValidation.bValidateGeneratedSource = false;
		Ok &= Expect(!TranspileSource("template<typename T,typename U> struct pair {T first;U second;};"
		                              "using Entry=std::pair<int,int>;",
		                 "pair-alias.cpp", NoValidation)
		                 .Succeeded(),
		    "standard pair alias collisions are rejected without final source validation");
		const auto DefaultPair = TranspileSource("template<typename T,typename U> struct pair {T first=7;U second=9;};"
		                                         "template<typename T=std::pair<int,int>> struct Holder {T value;};",
		    "pair-template-default.cpp", NoValidation);
		Ok &= Expect(!DefaultPair.Succeeded() && DefaultPair.Source.empty() &&
		        std::any_of(DefaultPair.Diagnostics.begin(), DefaultPair.Diagnostics.end(),
		            [](const auto& D)
		{
			return D.Code == "cpp.transpile.standard_pair_shadowed";
		}),
		    "standard pair collisions in template defaults are rejected before output");
		const auto FunctionDefaultPair = TranspileSource("template<typename T,typename U> struct pair {T first=7;U second=9;};"
		                                                 "template<typename T=std::pair<int,int>> int read(){T value;return value.first;}",
		    "pair-function-default.cpp", NoValidation);
		Ok &= Expect(!FunctionDefaultPair.Succeeded() && FunctionDefaultPair.Source.empty() &&
		        std::any_of(FunctionDefaultPair.Diagnostics.begin(), FunctionDefaultPair.Diagnostics.end(),
		            [](const auto& D)
		{
			return D.Code == "cpp.transpile.standard_pair_shadowed";
		}),
		    "standard pair collisions in function template defaults are rejected before output");
		Ok &= Expect(!TranspileSource("namespace N {template<typename T,typename U> struct pair {T first=7;U second=9;};"
		                              "int read(){std::pair<int,int> value=std::pair<int,int>();return value.first;}}",
		                 "pair-scope.cpp", NoValidation)
		                 .Succeeded(),
		    "standard pair collisions follow the emitted lexical scope");
		Ok &= Expect(TranspileSource("namespace Other {template<typename T,typename U> struct pair {T first=7;U second=9;};}"
		                             "void main(){std::pair<int,int> value=std::pair<int,int>();print(value.first);}",
		                 "pair-unrelated.cpp")
		                 .Succeeded(),
		    "a user pair in an unrelated namespace does not block the standard pair");
		const auto ImportedPair = TranspileSource("namespace Other {template<typename T,typename U> struct pair {T first=7;U second=9;};}"
		                                          "using namespace Other; void main(){std::pair<int,int> value=std::pair<int,int>();print(value.first);}",
		    "pair-using.cpp");
		if (ImportedPair.Succeeded())
		{
			VeroScript::VeroScriptModuleLoader Loader("");
			auto Program = Loader.LoadSourceModule(ImportedPair.Source, "pair-using.vs");
			VeroScript::VeroScriptCompiler Compiler;
			auto Module = Program ? Compiler.Compile(*Program) : nullptr;
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Ok &= Expect(Module && Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str() == "0\n",
			    "using namespaces must not redirect the standard pair to a user template");
			if (Output.str() != "0\n")
			{
				std::cerr << "imported pair output: " << Output.str() << '\n';
			}
		}
		Ok &= Expect(TranspileSource("template<typename T> struct pair {T value=7;};"
		                             "void main(){std::pair<int,int> value=std::pair<int,int>();print(value.first);}",
		                 "pair-other-arity.cpp")
		                 .Succeeded(),
		    "a user pair of another arity can coexist with the standard pair");
	}
	Ok &= TestProjectDefinitionScope(Argc > 1 ? Argv[1] : nullptr);
	const std::string Source = R"cpp(
namespace Demo
{
	enum class EMode : int { Off = 0, On = 1 };
	struct Counter
	{
		int Value = 0;
		Counter(int Initial) { this->Value = Initial; }
		int Increment(int Amount) { Value += Amount; return Value; }
	};
	int Add(int Left, int Right)
	{
		return static_cast<int>(Left + Right);
	}
	int Defaulted(int Value = 3) { return Value; }
}
int main()
{
	Demo::Counter Counter(2);
	return Demo::Add(Counter.Increment(3), 4) == 9 ? 0 : 1;
}
)cpp";
	FTranspileOptions MainOptions;
	MainOptions.GeneratedSourcePath = "generated/custom_output.vs";
	const FTranspileResult Result = TranspileSource(Source, "transpile_test.cpp", MainOptions);
	if (!Result.Succeeded())
	{
		for (const auto& Diagnostic : Result.Diagnostics)
		{
			std::cerr << VeroScript::CppFrontend::FormatDiagnostic(Diagnostic) << '\n';
		}
		return 1;
	}
	Ok &= Expect(Result.Source.find("cast<int>") != std::string::npos,
	    "static_cast should lower to cast");
	const FTranspileResult BracketOperators = TranspileSource(R"cpp(
struct Callable {
    int Value = 2;
    int operator()(const int& Delta) { return Value + Delta; }
    int operator()() const { return Value; }
    void operator()(int& Target, const int& Delta) { Target += Delta; }
    int& operator[](const int& Index) { return Value; }
    const int& operator[](const int& Index) const { return Value; }
};
int& Identity(int& Value) { return Value; }
int main() {
    Callable Value = Callable();
    Value[0] = 7;
    int Target = 1;
    Value(Target, 3);
    return Value(3) == 10 && Value() == 7 && Target == 4 ? 0 : 1;
}
)cpp",
	    "bracket_operators.cpp");
	Ok &= Expect(BracketOperators.Succeeded() && BracketOperators.Source.find("operator()(const int& Delta)") != std::string::npos && BracketOperators.Source.find("operator()() const") != std::string::npos && BracketOperators.Source.find("void operator()(int& Target, const int& Delta)") != std::string::npos && BracketOperators.Source.find("int& operator[](const int& Index)") != std::string::npos && BracketOperators.Source.find("const int& operator[](const int& Index) const") != std::string::npos && BracketOperators.Source.find("int& Identity(int& Value)") != std::string::npos,
	    "call/subscript operators with reference signatures must generate valid VeroScript");
	Ok &= Expect(Result.Source.find("this.Value") != std::string::npos,
	    "this->member should lower to this.member");
	Ok &= Expect(Result.Source.find("Defaulted(int Value = 3)") != std::string::npos,
	    "literal default arguments must be retained");
	Ok &= Expect(Result.SourceMapJson.find("transpile_test.cpp") != std::string::npos,
	    "source map must retain the original source path");
	Ok &= Expect(Result.SourceMapJson.find("generated/custom_output.vs") != std::string::npos,
	    "source map must record the actual generated source path");
	const FTranspileResult NegativeEnum = TranspileSource(
	    "enum class EValue : int { Negative = -1 };",
	    "negative_enum.cpp");
	Ok &= Expect(NegativeEnum.Succeeded() && NegativeEnum.Source.find("Negative = -1") != std::string::npos,
	    "negative integer enum literals must remain supported");
	const FTranspileResult EnumExpression = TranspileSource(R"cpp(
enum class EFlags : int
{
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Both = ::EFlags::Read | Write,
	Next,
	LetterA = 'A'
};
)cpp",
	    "enum_expression.cpp");
	Ok &= Expect(EnumExpression.Succeeded() && EnumExpression.Source.find("Read = 1") != std::string::npos && EnumExpression.Source.find("Write = 2") != std::string::npos && EnumExpression.Source.find("Both = 3") != std::string::npos && EnumExpression.Source.find("Next = 4") != std::string::npos && EnumExpression.Source.find("LetterA = 65") != std::string::npos,
	    "supported C++ enum constant expressions must lower to deterministic integer literals");
	FTranspileOptions NoEnumValidation;
	NoEnumValidation.bValidateGeneratedSource = false;
	Ok &= Expect(!TranspileSource(
	                 "enum class EBad : int { Value = 1 / 0 };",
	                 "bad_enum_expression.cpp", NoEnumValidation)
	                 .Succeeded(),
	    "invalid enum arithmetic must fail before generated-source validation");
	Ok &= Expect(!TranspileSource(
	                 "enum class ESmall : uint8 { Value = 256 };",
	                 "enum_range.cpp", NoEnumValidation)
	                 .Succeeded(),
	    "enum underlying-range validation must run even when final source validation is disabled");
	const FTranspileResult DestructorResult = TranspileSource(R"cpp(
struct Tracked
{
    ~Tracked() { print("destroyed"); }
};
)cpp",
	    "destructor.cpp");
	Ok &= Expect(DestructorResult.Succeeded() && DestructorResult.Source.find("~Tracked()") != std::string::npos,
	    "C++ destructors must lower to VeroScript destructors");
	const FTranspileResult TemplateResult = TranspileSource(R"cpp(
template<typename T, class U = int>
struct Box
{
    T Value;
    U Extra;
};
template<typename T> T Identity(T Value) { return Value; }
int main() { Box<int> Value = Box<int>(); return Identity(Value.Value); }
)cpp",
	    "template.cpp");
	const bool bTemplateResultValid = TemplateResult.Succeeded() && TemplateResult.Source.find("template<T, U = int> struct Box") != std::string::npos && TemplateResult.Source.find("template<T> T Identity(T Value)") != std::string::npos;
	if (!bTemplateResultValid)
	{
		for (const auto& Diagnostic : TemplateResult.Diagnostics)
		{
			std::cerr << VeroScript::CppFrontend::FormatDiagnostic(Diagnostic) << '\n';
		}
		std::cerr << TemplateResult.Source << '\n';
	}
	Ok &= Expect(bTemplateResultValid,
	    "safe C++ type templates must lower to VeroScript open generics");
	Ok &= Expect(!TranspileSource(R"cpp(
struct Utility
{
    template<typename T> T Echo(T Value) { return Value; }
};
)cpp",
	                 "member_template.cpp")
	                 .Succeeded(),
	    "member function templates must fail until VeroScript supports method-level generic parameters");
	const FTranspileResult OutOfClassResult = TranspileSource(R"cpp(
struct Outside
{
    int Value;
    Outside(int Initial);
    ~Outside();
    int Read(int Offset = 2) const;
};
Outside::Outside(int Initial) { Value = Initial; }
Outside::~Outside() { Value = 0; }
int Outside::Read(int Offset) const { return Value + Offset; }
int main() { Outside Value(3); return Value.Read() == 5 ? 0 : 1; }
)cpp",
	    "out_of_class.cpp");
	Ok &= Expect(OutOfClassResult.Succeeded() && OutOfClassResult.Source.find("Outside(int Initial)") != std::string::npos && OutOfClassResult.Source.find("~Outside()") != std::string::npos && OutOfClassResult.Source.find("int Read(int Offset = 2) const") != std::string::npos && OutOfClassResult.Source.find("Outside::Read") == std::string::npos,
	    "same-file out-of-class definitions must lower as the owning VeroScript methods");
	const FTranspileResult PureVirtualBodyResult = TranspileSource(R"cpp(
struct AbstractValue
{
    virtual int Read() const = 0;
    virtual ~AbstractValue() = 0;
};
int AbstractValue::Read() const { return 3; }
AbstractValue::~AbstractValue() = default;
struct ConcreteValue : AbstractValue
{
    int Read() const override { return 4; }
};
int main() { ConcreteValue Value = ConcreteValue(); return Value.Read() == 4 ? 0 : 1; }
)cpp",
	    "pure_virtual_body.cpp");
	if (!PureVirtualBodyResult.Succeeded())
	{
		for (const auto& Diagnostic : PureVirtualBodyResult.Diagnostics)
		{
			std::cerr << VeroScript::CppFrontend::FormatDiagnostic(Diagnostic) << '\n';
		}
		std::cerr << PureVirtualBodyResult.Source << '\n';
	}
	Ok &= Expect(PureVirtualBodyResult.Succeeded() && PureVirtualBodyResult.Source.find("virtual int Read() const = 0;") != std::string::npos && PureVirtualBodyResult.Source.find("int AbstractValue::Read() const") != std::string::npos && PureVirtualBodyResult.Source.find("AbstractValue::~AbstractValue()") != std::string::npos,
	    "pure virtual base bodies and defaulted pure destructors must remain out-of-class definitions");
	const FTranspileResult ExceptionResult = TranspileSource(R"cpp(
int Recover(int Value)
{
    try
    {
        if (Value < 0) throw Value;
        return Value;
    }
    catch (const int& Error)
    {
        if (Error < -10) throw;
        return -Error;
    }
}
int main() { return Recover(-3) == 3 ? 0 : 1; }
)cpp",
	    "exceptions.cpp");
	Ok &= Expect(ExceptionResult.Succeeded() && ExceptionResult.Source.find("try") != std::string::npos && ExceptionResult.Source.find("catch") != std::string::npos && ExceptionResult.Source.find("const int&") != std::string::npos && ExceptionResult.Source.find("throw;") != std::string::npos,
	    "ordinary typed C++ exceptions must preserve VeroScript throw/catch semantics");
	const FTranspileResult FriendResult = TranspileSource(R"cpp(
struct Secret
{
private:
    int Value;
    friend int Inspect(const Secret& Value);
};
int Inspect(const Secret& Value) { return Value.Value; }
int main() { return 0; }
)cpp",
	    "friend.cpp");
	Ok &= Expect(FriendResult.Succeeded() && FriendResult.Source.find("friend int Inspect") != std::string::npos && FriendResult.Source.find("const Secret&") != std::string::npos,
	    "friend declarations must retain their exact access grant");
	const FTranspileResult GotoResult = TranspileSource(R"cpp(
int main()
{
    int Value = 0;
again:
    Value++;
    if (Value < 2) goto again;
    return Value == 2 ? 0 : 1;
}
)cpp",
	    "goto.cpp");
	Ok &= Expect(!GotoResult.Succeeded(),
	    "C++ goto must fail closed because VeroScript's protected-scope rules are stricter");
	const FTranspileResult UsingNamespaceResult = TranspileSource(R"cpp(
namespace Values { int Get() { return 3; } }
namespace Consumer
{
    using namespace Values;
    int Read() { return Get(); }
}
int main() { return Consumer::Read() == 3 ? 0 : 1; }
)cpp",
	    "using_namespace.cpp");
	Ok &= Expect(UsingNamespaceResult.Succeeded() && UsingNamespaceResult.Source.find("using namespace Values;") != std::string::npos,
	    "using namespace declarations must retain their lexical scope");
	const FTranspileResult AnnotationResult = TranspileSource(R"cpp(
[[veroscript::Reflected(Category = "Data")]]
struct Value
{
    [[vero::Property(Visible)]] int Field;
};
)cpp",
	    "attribute.cpp");
	Ok &= Expect(AnnotationResult.Succeeded() && AnnotationResult.Source.find("@Reflected(Category = \"Data\")") != std::string::npos && AnnotationResult.Source.find("@Property(Visible)") != std::string::npos,
	    "explicit VeroScript C++ attributes must lower to declaration annotations");
	Ok &= Expect(!TranspileSource(
	                 "[[nodiscard]] int Read() { return 1; }",
	                 "unscoped_attribute.cpp")
	                 .Succeeded(),
	    "unscoped C++ attributes must remain rejected without an explicit mapping policy");
	FTranspileOptions AttributeMappingOptions;
	AttributeMappingOptions.AttributeMappings.push_back({"nodiscard", "must_use"});
	const FTranspileResult MappedAttribute = TranspileSource(
	    "[[nodiscard]] int Read() { return 1; }",
	    "mapped_attribute.cpp", AttributeMappingOptions);
	Ok &= Expect(MappedAttribute.Succeeded() && MappedAttribute.Source.find("@must_use()") != std::string::npos,
	    "an explicit attribute mapping must opt an otherwise unrelated C++ attribute into VeroScript metadata");
	AttributeMappingOptions.AttributeMappings.push_back({"nodiscard", "again"});
	Ok &= Expect(!TranspileSource(
	                 "int Read() { return 1; }", "duplicate_attribute_mapping.cpp",
	                 AttributeMappingOptions)
	                 .Succeeded(),
	    "duplicate attribute mappings must be rejected before source emission");
	FTranspileOptions InvalidAttributeValueOptions;
	InvalidAttributeValueOptions.bValidateGeneratedSource = false;
	Ok &= Expect(!TranspileSource(
	                 "[[veroscript::Meta(Value = 1 + 2)]] int Read() { return 1; }",
	                 "invalid_attribute_value.cpp", InvalidAttributeValueOptions)
	                 .Succeeded(),
	    "attribute values outside VeroScript annotation grammar must fail even when final source validation is disabled");
	FTranspileOptions KeywordAttributeOptions;
	KeywordAttributeOptions.AttributeMappings.push_back({"nodiscard", "class"});
	Ok &= Expect(!TranspileSource(
	                 "[[nodiscard]] int Read() { return 1; }",
	                 "keyword_attribute_mapping.cpp", KeywordAttributeOptions)
	                 .Succeeded(),
	    "attribute mappings must reject VeroScript keywords as annotation names");
	Ok &= Expect(!TranspileSource(
	                 "int Read([[veroscript::Input]] int Value) { return Value; }",
	                 "parameter_attribute.cpp")
	                 .Succeeded(),
	    "parameter attributes must fail while VeroScript source has no parameter annotation slot");
	const FTranspileResult AliasResult = TranspileSource(
	    "using Score = int; int main() { Score Value = 0; return Value; }",
	    "alias.cpp");
	Ok &= Expect(AliasResult.Succeeded() && AliasResult.Source.find("using Score = int;") != std::string::npos,
	    "ordinary C++ using aliases must remain declarations in generated source");
	const FTranspileResult TypedefResult = TranspileSource(
	    "typedef unsigned int Count; Count Read(Count Value) { return Value; }",
	    "typedef.cpp");
	const bool bTypedefLowered = TypedefResult.Succeeded() && TypedefResult.Source.find("using Count = unsigned int;") != std::string::npos && TypedefResult.Source.find("unsigned int Read(unsigned int Value)") != std::string::npos;
	if (!bTypedefLowered)
	{
		std::cerr << "typedef output:\n"
		          << TypedefResult.Source << '\n';
	}
	Ok &= Expect(bTypedefLowered,
	    "ordinary C++ typedef declarations must lower through the shared alias model");
	const FTranspileResult RawStringResult = TranspileSource(R"source(
string Read() { return R"tag(first line
"quoted"\path)tag"; }
)source",
	    "raw_string.cpp");
	Ok &= Expect(RawStringResult.Succeeded() && RawStringResult.Source.find("return \"first line\\n\\\"quoted\\\"\\\\path\";") != std::string::npos,
	    "C++ raw strings must lower to equivalent VeroScript string literals");
	const FTranspileResult DeclaratorListResult = TranspileSource(
	    "struct Point { int X = 1, Y = 2; }; int First = 3, Second = 4;",
	    "declarator_list.cpp");
	Ok &= Expect(DeclaratorListResult.Succeeded() && DeclaratorListResult.Source.find("int X = 1;") != std::string::npos && DeclaratorListResult.Source.find("int Y = 2;") != std::string::npos && DeclaratorListResult.Source.find("int First = 3;") != std::string::npos && DeclaratorListResult.Source.find("int Second = 4;") != std::string::npos,
	    "simple C++ declarator lists must lower to independent VeroScript declarations");
	const FTranspileResult AliasTemplateResult = TranspileSource(R"cpp(
template<typename T = int> using List = std::vector<T>;
struct Values { List<> Items; };
)cpp",
	    "alias_template.cpp");
	Ok &= Expect(AliasTemplateResult.Succeeded() && AliasTemplateResult.Source.find("array<int> Items") != std::string::npos && AliasTemplateResult.Source.find("using List") == std::string::npos,
	    "generic C++ aliases in structured declaration types must expand to their concrete VeroScript target");
	Ok &= Expect(!TranspileSource(R"cpp(
template<typename T> using List = std::vector<T>;
int main() { List<int> Values; return 0; }
)cpp",
	                 "alias_template_body.cpp")
	                 .Succeeded(),
	    "generic aliases inside opaque C++ bodies must remain fail-closed");
	const VeroScript::CppFrontend::FParseResult ExternalAlias =
	    VeroScript::CppFrontend::ParseSource(
	        "template<typename T> using ExternalList = std::vector<T>;",
	        "external_alias.hpp");
	FTranspileOptions ExternalAliasOptions;
	ExternalAliasOptions.bValidateGeneratedSource = false;
	if (ExternalAlias.Succeeded())
	{
		ExternalAliasOptions.AdditionalKnownTypeAliases = ExternalAlias.Unit.TypeAliases;
	}
	Ok &= Expect(ExternalAlias.Succeeded() && !TranspileSource("int main() { ExternalList<int> Values; return 0; }", "external_alias_body.cpp", ExternalAliasOptions).Succeeded(),
	    "project-supplied generic aliases in opaque bodies must fail even when final validation is disabled");

	const FTranspileResult Rejected = TranspileSource(
	    "int main() { int* P = nullptr; delete P; return 0; }", "delete.cpp");
	Ok &= Expect(!Rejected.Succeeded(), "delete expressions must fail closed");
	Ok &= Expect(!TranspileSource(
	                 "int Stable() noexcept { return 1; }", "noexcept.cpp")
	                 .Succeeded(),
	    "noexcept must not be silently discarded by source transpilation");
	Ok &= Expect(!TranspileSource(
	                 "constinit int Global = 1;", "constinit.cpp")
	                 .Succeeded(),
	    "constinit must not be silently discarded by source transpilation");
	Ok &= Expect(!TranspileSource(
	                 "struct ExternalConstinit { static int Value; }; "
	                 "constinit int ExternalConstinit::Value = 1;",
	                 "external_constinit.cpp")
	                 .Succeeded(),
	    "class-external constinit must not bypass field initialization-guarantee checks");
	Ok &= Expect(!TranspileSource(
	                 "consteval int Immediate() { return 1; }", "consteval.cpp")
	                 .Succeeded(),
	    "consteval must not be silently lowered to a runtime function");
	Ok &= Expect(!TranspileSource(
	                 "constexpr int Constant() { return 1; }", "constexpr.cpp")
	                 .Succeeded(),
	    "constexpr must not be silently lowered without a constexpr evaluation model");
	Ok &= Expect(!TranspileSource(
	                 "constexpr int Constant = 1;", "constexpr_global.cpp")
	                 .Succeeded(),
	    "constexpr globals must not be silently lowered to runtime const variables");
	Ok &= Expect(!TranspileSource(
	                 "struct Constants { static constexpr int Value = 1; };",
	                 "constexpr_field.cpp")
	                 .Succeeded(),
	    "constexpr fields must not be silently lowered to runtime const variables");
	Ok &= Expect(!TranspileSource(
	                 "class Incomplete;", "forward.cpp")
	                 .Succeeded(),
	    "standalone forward declarations must not become empty VeroScript types");
	Ok &= Expect(!TranspileSource(
	                 "int BadDefault(int Value = static_cast<int>(3)) { return Value; }",
	                 "default_expression.cpp")
	                 .Succeeded(),
	    "non-literal C++ defaults must be rejected before emitting invalid VeroScript");
	const FTranspileResult StaticHelper = TranspileSource(
	    "static int Helper() { return 7; } int main() { return Helper(); }",
	    "static_helper.cpp");
	Ok &= Expect(StaticHelper.Succeeded() && StaticHelper.Source.find("static int Helper") == std::string::npos && StaticHelper.Source.find("int Helper") != std::string::npos,
	    "C++ internal-linkage helpers should lower to naturally file-private VeroScript functions");
	Ok &= Expect(!TranspileSource(
	                 "extern int Shared; int main() { return Shared; }",
	                 "extern_global.cpp")
	                 .Succeeded(),
	    "extern declarations must not become VeroScript global definitions");
	Ok &= Expect(!TranspileSource(
	                 "struct Item {}; std::shared_ptr<Item> Shared;",
	                 "shared_ptr.cpp")
	                 .Succeeded(),
	    "shared ownership must not be silently erased by transpilation");
	FTranspileOptions NoValidation;
	NoValidation.bValidateGeneratedSource = false;
	const FTranspileResult InlineStatic = TranspileSource(
	    "struct Storage { inline static int Value = 1; };",
	    "inline_static.cpp", NoValidation);
	Ok &= Expect(InlineStatic.Succeeded() && InlineStatic.Source.find("static int Value = 1;") != std::string::npos,
	    "inline static C++ data-member definitions must remain transpilable");
	Ok &= Expect(!TranspileSource(
	                 "struct Storage { static int Value; };",
	                 "noninline_static.cpp", NoValidation)
	                 .Succeeded(),
	    "non-inline static data-member declarations must fail even when final compile validation is disabled");
	Ok &= Expect(!TranspileSource(
	                 "enum class EValue : int { One = static_cast<std::int32_t>(1) };",
	                 "enum_cast.cpp", NoValidation)
	                 .Succeeded(),
	    "non-literal C++ enum initializers must fail even when final compile validation is disabled");
	Ok &= Expect(!TranspileSource(
	                 "int Original = 1; int& Alias = Original;",
	                 "reference_global.cpp", NoValidation)
	                 .Succeeded(),
	    "reference globals must fail even when final compile validation is disabled");
	Ok &= Expect(!TranspileSource(
	                 "struct Bad { int& Alias; };",
	                 "reference_field.cpp", NoValidation)
	                 .Succeeded(),
	    "reference fields must fail even when final compile validation is disabled");
	Ok &= Expect(!TranspileSource(
	                 "int Make(); struct Bad { int Value = Make(); };",
	                 "field_default.cpp", NoValidation)
	                 .Succeeded(),
	    "known-invalid instance-field defaults must fail even when final compile validation is disabled");
	Ok &= Expect(!TranspileSource(
	                 "struct Item {}; int main() { Item* Value = new Item(); return 0; }",
	                 "new_expression.cpp", NoValidation)
	                 .Succeeded(),
	    "C++ allocation expressions must fail even when final compile validation is disabled");

	{
		namespace fs = std::filesystem;
		const fs::path Directory = fs::temp_directory_path() / ("veroscript_cpp_transpile_project_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::error_code FileError;
		fs::create_directories(Directory, FileError);
		const fs::path TypesPath = Directory / "types.cpp";
		const fs::path UsePath = Directory / "use.cpp";
		{
			std::ofstream Types(TypesPath, std::ios::binary);
			Types << "using Count = int;\n"
			         "struct SharedValue { static int Instances; int Value = 1; int Add(int Amount = 3) const; };\n"
			         "extern int SharedGlobal;\n"
			         "int Read(SharedValue Input, Count Offset = 2);\n";
			std::ofstream Use(UsePath, std::ios::binary);
			Use << "int SharedGlobal = 7;\n"
			       "int SharedValue::Instances = 2;\n"
			       "int SharedValue::Add(int Amount) const { return Value + Amount; }\n"
			       "int Read(SharedValue Input, Count Offset) { return Input.Value + SharedGlobal + Offset; }\n";
		}
		const FTranspileProjectResult Project = TranspileFiles(
		    {TypesPath.string(), UsePath.string()});
		Ok &= Expect(Project.Succeeded() && Project.Files.size() == 2,
		    "project transpilation must emit one source per input");
		Ok &= Expect(Project.ManifestJson.find("allowProjectWideAccess") != std::string::npos,
		    "project transpilation manifest must describe sibling visibility");
		if (Project.Files.size() == 2)
		{
			Ok &= Expect(Project.Files[0].Source.find("using Count = int;") != std::string::npos,
			    "project aliases must remain in their declaring output");
			const bool bAttachedSplitMember =
			    Project.Files[0].Source.find("int Add(int Amount = 3) const;") != std::string::npos && Project.Files[0].Source.find("return Value+Amount;") == std::string::npos && Project.Files[1].Source.find("SharedValue::Add") != std::string::npos && Project.Files[1].Source.find("return Value+Amount;") != std::string::npos;
			if (!bAttachedSplitMember)
			{
				std::cerr << "types output:\n"
				          << Project.Files[0].Source
				          << "\nuse output:\n"
				          << Project.Files[1].Source << '\n';
			}
			Ok &= Expect(bAttachedSplitMember,
			    "cross-file members retain a declaration and one body in the defining module");
			Ok &= Expect(Project.Files[0].Source.find("static int Instances = 2;") != std::string::npos && Project.Files[1].Source.find("SharedValue::Instances") == std::string::npos,
			    "cross-file static data-member definitions must attach to the owning record exactly once");
			Ok &= Expect(Project.Files[1].Source.find("SharedValue") != std::string::npos,
			    "project-wide record types must map in sibling outputs");
			Ok &= Expect(Project.Files[0].Source.find("int Read(") == std::string::npos && Project.Files[0].Source.find("SharedGlobal") == std::string::npos,
			    "declarations satisfied by sibling definitions must not become invalid standalone output");
			Ok &= Expect(Project.Files[1].Source.find("int Read(::SharedValue Input, int Offset = 2)") != std::string::npos && Project.Files[1].Source.find("int SharedGlobal = 7") != std::string::npos,
			    "sibling definitions and declaration-provided defaults must remain in the defining output");
			std::vector<fs::path> GeneratedPaths;
			for (const FTranspiledFile& File : Project.Files)
			{
				const fs::path GeneratedPath = Directory / File.OutputName;
				std::ofstream Generated(GeneratedPath, std::ios::binary);
				Generated << File.Source;
				GeneratedPaths.push_back(GeneratedPath);
			}
			VeroScript::VeroScriptModuleLoader Loader(GeneratedPaths[1].string());
			Loader.SetAllowProjectWideAccess(true);
			for (const fs::path& GeneratedPath : GeneratedPaths)
			{
				Loader.AddImplicitImportPath(GeneratedPath.string());
			}
			const std::unique_ptr<VeroScript::FProgram> Loaded = Loader.LoadMainModule();
			Ok &= Expect(Loaded != nullptr && !Loader.HasError(),
			    "generated project files must load together with manifest visibility settings");
			if (Loader.HasError())
			{
				std::cerr << Loader.GetError().Message << '\n';
			}
		}
		Ok &= Expect(!TranspileFiles({TypesPath.string(), TypesPath.string()}).Succeeded(),
		    "project transpilation must reject duplicate input identities");

		const fs::path FirstDirectory = Directory / "first";
		const fs::path SecondDirectory = Directory / "second";
		fs::create_directories(FirstDirectory, FileError);
		fs::create_directories(SecondDirectory, FileError);
		const fs::path FirstSameName = FirstDirectory / "shared.cpp";
		const fs::path SecondSameName = SecondDirectory / "shared.cpp";
		{
			std::ofstream First(FirstSameName, std::ios::binary);
			First << "int FirstValue() { return 1; }\n";
			std::ofstream Second(SecondSameName, std::ios::binary);
			Second << "int SecondValue() { return 2; }\n";
		}
		const FTranspileProjectResult SameNames = TranspileFiles(
		    {FirstSameName.string(), SecondSameName.string()});
		Ok &= Expect(SameNames.Succeeded() && SameNames.Files.size() == 2 && SameNames.Files[0].OutputName != SameNames.Files[1].OutputName && SameNames.Files[0].OutputName.find("shared.") == 0 && SameNames.Files[1].OutputName.find("shared.") == 0,
		    "same-basename project inputs must receive distinct deterministic output names");

		const fs::path FirstStatic = Directory / "first_static.cpp";
		const fs::path SecondStatic = Directory / "second_static.cpp";
		{
			std::ofstream First(FirstStatic, std::ios::binary);
			First << "static int Helper() { return 1; }\n";
			std::ofstream Second(SecondStatic, std::ios::binary);
			Second << "static int Helper() { return 2; }\n";
		}
		const FTranspileProjectResult StaticCollision = TranspileFiles(
		    {FirstStatic.string(), SecondStatic.string()});
		const bool bHasInternalLinkageDiagnostic = std::any_of(
		    StaticCollision.Diagnostics.begin(), StaticCollision.Diagnostics.end(),
		    [](const VeroScript::CppFrontend::FDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == "cpp.merge.internal_linkage_collision";
		});
		Ok &= Expect(!StaticCollision.Succeeded() && bHasInternalLinkageDiagnostic,
		    "same-signature internal-linkage functions must fail explicitly across project inputs");
		fs::remove_all(Directory, FileError);
	}
	return Ok ? 0 : 1;
}

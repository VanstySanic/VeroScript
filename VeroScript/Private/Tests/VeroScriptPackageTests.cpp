// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptCompile.h"
#include "VeroScriptBytecodeLinker.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHost.h"
#include "VeroScriptLexer.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPackageInterface.h"
#include "VeroScriptParser.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"
#include "VeroScriptTestTemporaryDirectory.h"
#include "VeroScriptVm.h"
#include "VeroScriptVersion.h"

#include "VeroScriptAst.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <locale>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	class FCommaDecimalPoint final : public std::numpunct<char>
	{
	protected:
		char do_decimal_point() const override
		{
			return ',';
		}
	};

	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptPackageTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(
	    const std::string& Source)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "<package-test>");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "<package-test>");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (Parser.HasError() || !Program)
		{
			std::cerr << Parser.GetError().Message << '\n';
			return nullptr;
		}
		Program->EntryModulePath = "<package-test>";
		VeroScript::VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (Compiler.HasError())
		{
			std::cerr << Compiler.GetError().Message << '\n';
			return nullptr;
		}
		return Module;
	}

	struct FCaptureHost final : VeroScript::IEngineHost
	{
		//~ Begin VeroScript::IEngineHost Interface
		void OnPrintLine(const std::string_view Line) override
		{
			Output.append(Line);
			Output.push_back('\n');
		}

		void OnDiagnosticLine(const std::string_view Line) override
		{
			Errors.append(Line);
			Errors.push_back('\n');
		}
		//~ End VeroScript::IEngineHost Interface

		std::string Output;
		std::string Errors;
	};

	bool ReadBuiltPackage(
	    const VeroScript::FBytecodeModule& Module,
	    const std::uint64_t Version,
	    VeroScript::FPackageData& Out)
	{
		VeroScript::FPackageBuildOptions Options;
		Options.PackageName = "PackageTest";
		Options.PackageVersion = Version;
		Options.HostMetadata["a.one"] = {0x00, 0x7F, 0xFF};
		Options.HostMetadata["b.two"] = {'o', 'p', 'a', 'q', 'u', 'e'};
		Options.HostMetadata["future.host.extension.v9"] = {9, 8, 7, 6};
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		return VeroScript::BuildPackage(Module, Options, Bytes, Error) && VeroScript::ReadPackage(VeroScript::FByteView(Bytes), Out, Error);
	}

	bool WrapRawPackageForReadTest(
	    const VeroScript::FBytecodeModule& Module,
	    const VeroScript::EPackageKind Kind,
	    std::vector<std::uint8_t>& OutBytes,
	    std::string& OutError)
	{
		std::vector<std::uint8_t> BytecodeBytes;
		if (!VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
		        Module, 0, BytecodeBytes, OutError))
		{
			return false;
		}
		const std::string PackageName = "Raw.Read.Test";
		OutBytes.clear();
		VeroScript::Bytecode::WriteU32LE(
		    OutBytes, VeroScript::VEROSCRIPT_PACKAGE_MAGIC);
		VeroScript::Bytecode::WriteU16LE(
		    OutBytes, VeroScript::VEROSCRIPT_PACKAGE_FORMAT_MAJOR);
		VeroScript::Bytecode::WriteU16LE(
		    OutBytes, VeroScript::VEROSCRIPT_PACKAGE_FORMAT_MINOR);
		VeroScript::Bytecode::WriteU64LE(OutBytes, 1);
		VeroScript::Bytecode::WriteU8(
		    OutBytes, static_cast<std::uint8_t>(Kind));
		VeroScript::Bytecode::WriteU8(OutBytes, 0);
		VeroScript::Bytecode::WriteU8(OutBytes, 0);
		VeroScript::Bytecode::WriteU8(OutBytes, 0);
		VeroScript::Bytecode::WriteU32LE(
		    OutBytes, static_cast<std::uint32_t>(PackageName.size()));
		VeroScript::Bytecode::WriteU32LE(OutBytes, 0);
		OutBytes.insert(OutBytes.end(), PackageName.begin(), PackageName.end());
		VeroScript::Bytecode::WriteU64LE(
		    OutBytes, static_cast<std::uint64_t>(BytecodeBytes.size()));
		OutBytes.insert(
		    OutBytes.end(), BytecodeBytes.begin(), BytecodeBytes.end());
		return true;
	}

	bool PrivateCallbackPackageIdentities()
	{
		using namespace VeroScript;
		Tests::FScopedTemporaryDirectory Directory("veroscript-private-callback-package-");
		if (!Directory) return false;
		const auto BasePath = PathUtil::ToUtf8(Directory.Get() / "base.vs");
		const auto SourcePath = PathUtil::ToUtf8(Directory.Get() / "main.vs");
		for (const std::string Kind : {"struct", "class"})
		{
			for (const std::string Parameter : {"Probe", "::Probe"})
			{
				for (const auto PackageKind : {EPackageKind::Application, EPackageKind::Library})
				{
					auto CompileOptions = Test::MakeCompileOptions();
					CompileOptions.SourceOverlays.push_back({BasePath,
					    "class Probe { public: int baseValue = 7; }; export class Bridge : public Probe {};"});
					const std::string Source = "import \"base.vs\"; class Probe : public Bridge { public: int ownValue = 42; };"
					                           "int observed = 0; " +
					    Kind + " Builder { public: array<fn(" + Parameter + ")->void> actions;"
					                                                        "void Append(fn(" +
					    Parameter + ")->void callback) { actions.push(callback); }"
					                "void Append(int value) { assert(value == 42); }"
					                "void Configure() { Append(42); Append([](" +
					    Parameter + " value)->void {"
					                "assert(value.baseValue == 7); observed = value.ownValue; });"
					                "auto callback = actions[0]; callback(Probe()); } };" +
					    (PackageKind == EPackageKind::Library ? "export " : "") + "int run() { observed = 0; Builder builder = Builder(); builder.Configure(); return observed; }" + (PackageKind == EPackageKind::Application ? "int main() { return run(); }" : "");
					auto Compiled = VeroScriptCompile::CompileSource(Source, SourcePath, CompileOptions);
					if (!Compiled.bSuccess || !Compiled.Module)
					{
						std::cerr << "private callback compile: " << Compiled.Error.Message << '\n';
						return false;
					}
					if (std::any_of(Compiled.Module->Functions.begin(), Compiled.Module->Functions.end(),
					        [](const auto& Function)
					{
						return Function.Code.empty() && !Function.bIsAbstract && !Function.bDeleted;
					}))
					{
						std::cerr << "private callback declarations left an uncompiled function\n";
						return false;
					}
					for (const bool Strip : {false, true})
					{
						FPackageBuildOptions Options;
						Options.Kind = PackageKind;
						Options.PackageName = "PrivateCallbackIdentity";
						Options.bStripDebugInfo = Strip;
						std::vector<std::uint8_t> Bytes;
						std::string Error;
						FPackageData Package;
						if (!BuildPackage(*Compiled.Module, Options, Bytes, Error) || !ReadPackage(FByteView(Bytes), Package, Error))
						{
							std::cerr << "private callback package: " << Error << '\n';
							return false;
						}
						FCompileResult Consumer;
						if (PackageKind == EPackageKind::Library)
						{
							{
								std::ofstream File(Directory.Get() / "private.vslib", std::ios::binary | std::ios::trunc);
								File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
								if (!File) return false;
							}
							Consumer = VeroScriptCompile::CompileSource("import \"private.vslib\"; int main() { return run(); }",
							    PathUtil::ToUtf8(Directory.Get() / "consumer.vs"), Test::MakeCompileOptions());
							if (!Consumer.bSuccess || !Consumer.Module)
							{
								std::cerr << "private callback import: " << Consumer.Error.Message << '\n';
								return false;
							}
						}
						std::ostringstream Output;
						VeroScriptVm Vm(Output);
						Test::Configure(Vm);
						const auto& Executable = PackageKind == EPackageKind::Application ? Package.Module : Consumer.Module;
						if (!Vm.Execute(*Executable, ERunMode::Script) || Vm.GetProcessExitCode() != 42)
						{
							std::cerr << "private callback run: " << Vm.GetError().Message << '\n';
							return false;
						}
					}
				}
			}
		}
		return true;
	}

	bool DefaultConstructionDiamond()
	{
		using namespace VeroScript;
		Tests::FScopedTemporaryDirectory Directory("veroscript-default-construction-diamond-");
		if (!Directory) return false;
		for (const bool UserConstructor : {false, true})
		{
			for (const bool BothBranches : {false, true})
			{
				const auto WriteLibrary = [&](const std::string& Stem, const std::string& Source)
				{
					const auto SourcePath = PathUtil::ToUtf8(Directory.Get() / (Stem + ".vs"));
					auto Compiled = VeroScriptCompile::CompileSource(Source, SourcePath);
					std::string Error;
					std::vector<std::uint8_t> Bytes;
					FPackageBuildOptions Options;
					Options.Kind = EPackageKind::Library;
					Options.PackageName = "DefaultConstruction." + Stem;
					if (!Compiled.bSuccess || !Compiled.Module || !BuildPackage(*Compiled.Module, Options, Bytes, Error))
					{
						std::cerr << "default construction diamond " << Stem << ": " << Compiled.Error.Message << Error << '\n';
						return false;
					}
					std::ofstream File(Directory.Get() / (Stem + ".vslib"), std::ios::binary);
					File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
					return static_cast<bool>(File);
				};
				const std::string Type = UserConstructor
				    ? "export struct S{int value;S(){value=7;}};"
				    : "int initial(){return 7;}export struct S{int value=initial();};";
				if (!WriteLibrary("base", Type) || !WriteLibrary("left", "import \"base.vslib\";export int left(){S values[1];return values[0].value;}") || !WriteLibrary("right", BothBranches ? "import \"base.vslib\";export int right(){S values[1];return values[0].value;}" : "import \"base.vslib\";export int right(){return 0;}"))
				{
					return false;
				}
				for (const bool ReverseImports : {false, true})
				{
					const std::string Imports = ReverseImports
					    ? "import \"right.vslib\";import \"left.vslib\";"
					    : "import \"left.vslib\";import \"right.vslib\";";
					auto Root = VeroScriptCompile::CompileSource(
					    Imports + "void main(){print(left());print(right());}",
					    PathUtil::ToUtf8(Directory.Get() / "main.vs"));
					if (!Root.bSuccess || !Root.Module)
					{
						std::cerr << "default construction diamond link: " << Root.Error.Message << '\n';
						return false;
					}
					std::ostringstream Output;
					VeroScriptVm Vm(Output);
					VeroScript::Test::Configure(Vm);
					if (!Vm.Execute(*Root.Module, ERunMode::Script) || Output.str() != (BothBranches ? "7\n7\n" : "7\n0\n"))
					{
						std::cerr << "default construction diamond run: " << Vm.GetError().Message << Output.str() << '\n';
						return false;
					}
				}
			}
		}
		return true;
	}
}

namespace
{
	bool PackageStringConstantRoles()
	{
		using namespace VeroScript;
		const std::string Literal = std::string("<package-test>") + '\x1F' + "Hidden";
		const std::string Source =
			"class Hidden{public:string value=\"" + Literal + "\";static string shared=\"" + Literal + "\";}"
			"export int probe(){Hidden value=Hidden();string text=\"" + Literal + "\";"
			"assert(value.value==text);assert(Hidden::shared==text);"
			"type<Hidden> info=type<Hidden>();return info.qualified_name==text?1:2;}";
		for (const auto Kind : {EPackageKind::Application, EPackageKind::Library})
		{
			auto Original = CompileSource(Source +
				(Kind == EPackageKind::Application ? "int main(){return probe();}" : ""));
			if (!Expect(static_cast<bool>(Original), "string role package fixture compiles")) return false;
			const auto OriginalPool = Original->StringPool;
			bool SharedRoles = false;
			for (const auto& Type : Original->Types)
			{
				for (const auto& Field : Type.Fields)
				{
					if (!Field.bHasDefault || !Field.DefaultValue.bIsLinkageString)
					{
						continue;
					}
					for (const auto& Constant : Original->Constants)
					{
						SharedRoles |= Constant.Kind == EConstantKind::Str && !Constant.bIsLinkageString &&
							Constant.StringIndex == Field.DefaultValue.StringIndex &&
							OriginalPool[Constant.StringIndex] == Literal;
					}
				}
			}
			if (!Expect(SharedRoles, "ordinary data and reflection identity share interned storage before packaging")) return false;
			std::vector<std::uint8_t> Cached;
			std::string CacheError;
			if (!Expect(VeroScriptBytecodeIO::SerializeBytecode(*Original, 7, Cached, CacheError),
					"string roles survive an independent bytecode cache write"))
			{
				return false;
			}
			Original = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Cached), 7, CacheError);
			if (!Expect(static_cast<bool>(Original), "string roles survive an independent bytecode cache read")) return false;
			for (const bool Strip : {false, true})
			{
				FPackageBuildOptions Options;
				Options.Kind = Kind;
				Options.PackageName = "StringRoles";
				Options.bStripDebugInfo = Strip;
				std::vector<std::uint8_t> Bytes, Repeated;
				std::string Error;
				FPackageData Parsed;
				if (!Expect(BuildPackage(*Original, Options, Bytes, Error) &&
					BuildPackage(*Original, Options, Repeated, Error) && Bytes == Repeated &&
					ReadPackage(FByteView(Bytes), Parsed, Error),
					"package string roles round trip deterministically"))
				{
					std::cerr << Error << '\n';
					return false;
				}
				if (!Expect(Original->StringPool == OriginalPool, "packaging leaves the source string pool unchanged")) return false;
				const std::int32_t Expected = Strip || Kind == EPackageKind::Library ? 2 : 1;
				for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
				{
					if (Backend == EExecutionBackend::Jit && !std::getenv("VEROSCRIPT_TEST_JIT_BACKEND"))
					{
						continue;
					}
					std::ostringstream Output;
					VeroScriptVm Vm(Output);
					Test::Configure(Vm);
					Vm.SetExecutionBackend(Backend);
					if (const auto* Path = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND"))
					{
						if (!Vm.SetJitBackendPath(Path, Error)) return false;
					}
					FValue Result;
					if (!Expect(Vm.ExecuteFreeFunction(*Parsed.Module, "probe", {}, nullptr, nullptr, &Result) &&
						std::get<std::int32_t>(Result) == Expected,
						"packaged literals and defaults retain exact data while reflection identities are relocated"))
					{
						std::cerr << Vm.GetError().Message << '\n';
						return false;
					}
				}
			}
			FPackageBuildOptions FullOptions;
			FullOptions.Kind = Kind;
			FullOptions.PackageName = "FullStringRoles";
			FullOptions.bStripDebugInfo = true;
			auto Full = *Original;
			Full.StringPool.resize(std::numeric_limits<std::uint16_t>::max(), "padding");
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			if (!Expect(!BuildPackage(Full, FullOptions, Bytes, Error) && Bytes.empty() &&
							Error.find("string pool") != std::string::npos,
					"preserving data at the string slot limit fails without wraparound or partial output"))
			{
				return false;
			}
		}
		FBytecodeModule Data, Linkage;
		Data.InitCode = {static_cast<std::uint8_t>(EOpcode::Return)};
		Data.StringPool = {"shared"};
		FBytecodeConstant Constant;
		Constant.Kind = EConstantKind::Str;
		Data.Constants.push_back(Constant);
		Linkage = Data;
		Linkage.Constants[0].bIsLinkageString = true;
		std::unique_ptr<FBytecodeModule> Linked;
		std::string Error;
		if (!Expect(LinkBytecodeModules(Data, {&Linkage}, Linked, Error) && Linked->Constants.size() == 2 &&
						Linked->Constants[0].bIsLinkageString != Linked->Constants[1].bIsLinkageString,
				"linker keeps string constants with distinct roles separate"))
		{
			return false;
		}
		std::vector<std::uint8_t> Encoding;
		if (!Expect(VeroScriptBytecodeIO::SerializeBytecode(Data, 0, Encoding, Error),
				"data string role fixture serializes"))
		{
			return false;
		}
		// This fixture has no native references, one narrow string, no wide
		// strings, and one constant. Locate its role byte without touching payloads.
		std::size_t Offset = 18u;
		if (!Expect(Bytecode::ReadU32LE(Encoding.data() + Offset) == 0u,
				"string role encoding fixture has no native references"))
		{
			return false;
		}
		Offset += 8u;
		Offset += 4u + Bytecode::ReadU32LE(Encoding.data() + Offset);
		Offset += 8u;
		if (!Expect(Encoding[Offset] == static_cast<std::uint8_t>(EConstantKind::Str),
				"string role encoding fixture locates its string constant"))
		{
			return false;
		}
		Encoding[Offset + 3u] = 2u;
		if (!Expect(!VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Encoding), 0, Error) &&
						Error.find("linkage role") != std::string::npos,
				"string role encoding rejects noncanonical flag values"))
		{
			return false;
		}
		Data.Constants[0].Kind = EConstantKind::I32;
		Data.Constants[0].bIsLinkageString = true;
		return Expect(!VeroScriptBytecodeIO::ValidateModule(Data, Error),
			"only narrow string constants may carry a linkage role");
	}

	bool NativeArtifactBindings()
	{
		using namespace VeroScript;
		const auto MakeNative = [](const char* Name, const int Value)
		{
			FNativeFunction Native;
			Native.Name = Name;
			Native.ReturnType = ENativeType::Int;
			Native.UserData = reinterpret_cast<void*>(static_cast<std::intptr_t>(Value));
			Native.Callback = [](const FNativeCallContext& Context, FValue& Out, std::string&)
			{
				Out = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(Context.UserData));
				return true;
			};
			return Native;
		};
		const auto Alpha = MakeNative("host_alpha", 11);
		const auto Beta = MakeNative("host_beta", 22);
		int Initializations = 0;
		auto Mark = MakeNative("host_mark", 0);
		Mark.UserData = &Initializations;
		Mark.Callback = [](const FNativeCallContext& Context, FValue& Out, std::string&)
		{
			Out = ++*static_cast<int*>(Context.UserData);
			return true;
		};
		std::vector<FNativeFunction> Producer{Alpha, Mark};
		auto Options = Test::MakeCompileOptions();
		Options.Natives = &Producer;
		auto Compiled = VeroScriptCompile::CompileSource(
		    "int initialized=host_mark();void main(){print(host_alpha());}", "native-app.vs", Options);
		if (!Expect(Compiled.bSuccess && Compiled.Module, "native binding application compiles")) return false;
		FPackageData Package;
		if (!Expect(ReadBuiltPackage(*Compiled.Module, 1, Package), "native references survive package round trip")) return false;
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !std::getenv("VEROSCRIPT_TEST_JIT_BACKEND"))
			{
				continue;
			}
			std::vector<FNativeFunction> Consumer{Beta, Mark, Alpha};
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			std::string Error;
			if (const auto* Path = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND"))
			{
				if (!Vm.SetJitBackendPath(Path, Error)) return false;
			}
			Initializations = 0;
			if (!Expect(Vm.Execute(*Package.Module, ERunMode::Script, {}, &Consumer) && Output.str() == "11\n" && Initializations == 1, "native registration order does not change artifact calls"))
			{
				return false;
			}
			for (int Failure = 0; Failure < 4; ++Failure)
			{
				Consumer = {Beta, Mark};
				if (Failure != 0)
				{
					auto Changed = Alpha;
					if (Failure == 1)
					{
						Changed.ReturnType = ENativeType::Double;
					}
					if (Failure == 2)
					{
						Changed.Origin.ProviderId = "another-provider";
					}
					Consumer.push_back(Changed);
					if (Failure == 3)
					{
						Consumer.push_back(Alpha);
					}
				}
				Initializations = 0;
				if (!Expect(!Vm.Execute(*Package.Module, ERunMode::Script, {}, &Consumer) && Initializations == 0, "missing, incompatible and ambiguous natives fail before initialization"))
				{
					return false;
				}
			}
		}
		Producer = {Alpha};
		auto LibraryA = VeroScriptCompile::CompileSource("export int alpha(){return host_alpha();}", "alpha.vs", Options);
		Producer = {Beta};
		auto LibraryB = VeroScriptCompile::CompileSource("export int beta(){return host_beta();}", "beta.vs", Options);
		std::unique_ptr<FBytecodeModule> Linked;
		std::string Error;
		if (!Expect(LibraryA.bSuccess && LibraryB.bSuccess && LinkBytecodeModules(*LibraryA.Module, {LibraryB.Module.get()}, Linked, Error), "independently compiled native references link"))
		{
			return false;
		}
		std::vector<FNativeFunction> Consumer{Beta, Alpha};
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		Test::Configure(Vm);
		std::size_t Calls = 0;
		for (std::uint16_t Index = 0; Index < Linked->Functions.size(); ++Index)
		{
			const auto& Name = Linked->Functions[Index].Name;
			const bool AlphaExport = Name.rfind("alpha#fn", 0) == 0;
			if (!AlphaExport && Name.rfind("beta#fn", 0) != 0)
			{
				continue;
			}
			FValue Result;
			++Calls;
			if (!Expect(Vm.ExecuteFunction(*Linked, Index, {}, &Consumer, &Result) && std::get<std::int32_t>(Result) == (AlphaExport ? 11 : 22), "linker remaps native slots independently of registration order"))
			{
				return false;
			}
		}
		return Expect(Calls == 2, "both linked native library exports execute");
	}
}

int main()
{
	using namespace VeroScript;
	if (!PackageStringConstantRoles()) return EXIT_FAILURE;
	if (!NativeArtifactBindings()) return EXIT_FAILURE;
	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Session.LoadSource("int retained=7;void main(){print(retained);}", "<package-test>").bSuccess || !Session.Run().bSuccess)
		{
			return EXIT_FAILURE;
		}
		auto Failing = CompileSource(
		    "int retained=99;int fail(){throw 7;return 0;}"
		    "class Value<T>{public:static int value=fail();};"
		    "int get(bool select){fn()->int f=select?[](){return Value<int>::value;}:[](){return 0;};return f();}"
		    "class State{public:Value<int> early=nullptr;static int result=get(true);};"
		    "void main(){print(retained);}");
		FPackageData Package;
		if (!Expect(Failing && ReadBuiltPackage(*Failing, 2, Package)
		        && Session.ReloadPackage(Package, "deferred-init.vslib").bSuccess
		        && !Session.Run().bSuccess && Session.Run().bSuccess && Host.Output == "7\n7\n",
		        "failed dynamic global initialization rolls back a persistent package generation"))
		{
			std::cerr << Host.Errors << Host.Output;
			return EXIT_FAILURE;
		}
	}
	if (!Expect(PrivateCallbackPackageIdentities(), "private callback identities agree across declarations, bodies and packages"))
	{
		return EXIT_FAILURE;
	}

	{
		Tests::FScopedTemporaryDirectory Directory("portable-template-declarations");
		if (!Expect(static_cast<bool>(Directory), "portable template fixture directory")) return EXIT_FAILURE;
		const auto SourcePath = PathUtil::ToUtf8(Directory.Get() / "templates.vs");
		auto CompileOptions = Test::MakeCompileOptions();
		CompileOptions.SourceOverlays.push_back({SourcePath,
		    "namespace Helpers{export template<typename T> T Identity(T value){return value;}}"
		    "export class Box<T>{public:template<typename U> U Convert(U value){return value;}}"});
		for (const auto Kind : {EPackageKind::Application, EPackageKind::Library})
		{
			const std::string Entry = Kind == EPackageKind::Application ? "int main()" : "export int Read()";
			const std::string Import = Kind == EPackageKind::Application
			    ? "import \"templates.vs\";"
			    : "export import \"templates.vs\";";
			const auto Compilation = VeroScriptCompile::CompileSource(
			    Import + Entry + "{"
			                     "auto read=[callback=Helpers::Identity<int>]()->int{return callback(42);};"
			                     "Box<int> box=Box<int>();return read()+box.Convert<int>(0);}",
			    PathUtil::ToUtf8(Directory.Get() / "consumer.vs"), CompileOptions);
			if (!Expect(Compilation.bSuccess, "exported template declaration fixture compiles"))
			{
				std::cerr << Compilation.Error.Message << '\n';
				return EXIT_FAILURE;
			}
			FPackageBuildOptions Options;
			Options.Kind = Kind;
			Options.PackageName = "PortableTemplateDeclarations";
			Options.bStripDebugInfo = true;
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			FPackageData Package;
			if (!Expect(BuildPackage(*Compilation.Module, Options, Bytes, Error) &&
			            ReadPackage(FByteView(Bytes), Package, Error),
			        "template declaration identities round-trip"))
			{
				std::cerr << Error << '\n';
				return EXIT_FAILURE;
			}
			if (!Expect(std::search(Bytes.begin(), Bytes.end(), SourcePath.begin(), SourcePath.end()) == Bytes.end(),
			        "portable template declarations retain no source module or owner paths"))
			{
				return EXIT_FAILURE;
			}
			FCompileResult Consumer;
			if (Kind == EPackageKind::Library)
			{
				{
					std::ofstream File(Directory.Get() / "portable.vslib", std::ios::binary);
					File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
					if (!File) return EXIT_FAILURE;
				}
				Consumer = VeroScriptCompile::CompileSource(
				    "import \"portable.vslib\";int main(){Box<double> box=Box<double>();"
				    "return Read()+cast<int>(box.Convert<int64>(0i64));}",
				    PathUtil::ToUtf8(Directory.Get() / "deployed.vs"), Test::MakeCompileOptions());
				if (!Expect(Consumer.bSuccess, "portable template library reimports without its source overlay"))
				{
					std::cerr << Consumer.Error.Location.FileLabel << ':' << Consumer.Error.Location.Line << ':' << Consumer.Error.Location.Column << ": " << Consumer.Error.Message << '\n';
					return EXIT_FAILURE;
				}
			}
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			Test::Configure(Vm);
			const auto& Executable = Kind == EPackageKind::Application ? Package.Module : Consumer.Module;
			if (!Expect(Vm.Execute(*Executable, ERunMode::Script) && Vm.GetProcessExitCode() == 42,
			        "portable template function capture and owned method execute"))
			{
				std::cerr << Vm.GetError().Message << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		VeroScript::Tests::FScopedTemporaryDirectory Directory("lambda-static-packages");
		const auto LibraryPath = Directory.Get() / "lambda_static.vslib";
		const auto Compilation = VeroScriptCompile::CompileSource(
		    "export fn()->int makeCounter(){return [count=0]()mutable->int{return ++count;};}"
		    "export int next(){static int value=0;return ++value;}"
		    "export T firstValue<T>(T seed){static T first=seed;return first;}",
		    PathUtil::ToUtf8(Directory.Get() / "library.vs"));
		if (!Expect(Compilation.bSuccess && Compilation.Module, "library captures and statics compile")) return EXIT_FAILURE;
		for (const bool Strip : {false, true})
		{
			FPackageBuildOptions Options;
			Options.Kind = EPackageKind::Library;
			Options.PackageName = "LambdaStatic";
			Options.bStripDebugInfo = Strip;
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			if (!Expect(BuildPackage(*Compilation.Module, Options, Bytes, Error), "library static metadata packages"))
			{
				std::cerr << Error << '\n';
				return EXIT_FAILURE;
			}
			{
				std::ofstream File(LibraryPath, std::ios::binary | std::ios::trunc);
				File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
				if (!File) return EXIT_FAILURE;
			}
			auto Root = VeroScriptCompile::CompileSource(
			    "int padding=7;import \"lambda_static.vslib\";using Count=int;void main(){"
			    "auto counter=makeCounter();auto alias=counter;assert(counter()==1&&alias()==2);"
			    "assert(next()==1&&next()==2);assert(firstValue<int>(3)==3&&firstValue<Count>(9)==3);assert(padding==7);}",
			    PathUtil::ToUtf8(Directory.Get() / "main.vs"));
			if (!Expect(Root.bSuccess && Root.Module, "library static relocation and open generic instantiation"))
			{
				std::cerr << Root.Error.Message << '\n';
				return EXIT_FAILURE;
			}
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			if (!Expect(Vm.Execute(*Root.Module, ERunMode::Script), "imported captures and statics execute"))
			{
				std::cerr << Vm.GetError().Message << '\n';
				return EXIT_FAILURE;
			}
			FPackageData Application;
			if (!Expect(ReadBuiltPackage(*Root.Module, VEROSCRIPT_VERSION_CODE, Application) && Vm.Execute(*Application.Module, ERunMode::Script), "application package retains capture and static semantics"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	if (!Expect(DefaultConstructionDiamond(), "lazy default construction entries compose through library diamonds"))
	{
		return EXIT_FAILURE;
	}

	{
		FHostBindingRegistry Registry;
		FHostTypeId Color;
		std::string Error;
		if (!Expect(Registry.RegisterType("BoundColor", EHostTypeKind::Enum, Color, Error) && Registry.SetEnumScoped(Color, true, Error), "register repeated host enum fixture"))
		{
			return EXIT_FAILURE;
		}
		FHostMemberDesc Red;
		Red.Kind = EHostMemberKind::EnumMember;
		Red.Name = "Red";
		Red.EnumValue = 1;
		if (!Expect(Registry.RegisterMember(Color, std::move(Red), Error), "register host enum value"))
		{
			return EXIT_FAILURE;
		}
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.Bindings = &Registry;
		auto Root = VeroScriptCompile::CompileSource(
		    "template<BoundColor C=BoundColor::Red> class Box{};void main(){Box<> b=Box<>();}", "host-root.vs", Options);
		auto First = VeroScriptCompile::CompileSource("export int first(){return 1;}", "host-first.vs", Options);
		auto Second = VeroScriptCompile::CompileSource("export int second(){return 2;}", "host-second.vs", Options);
		std::unique_ptr<FBytecodeModule> Linked;
		if (!Expect(Root.bSuccess && First.bSuccess && Second.bSuccess && LinkBytecodeModules(*Root.Module, {First.Module.get(), Second.Module.get()}, Linked, Error),
		        "independent units share identical host enum metadata"))
		{
			std::cerr << Root.Error.Message << ' ' << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(std::count_if(Linked->Enums.begin(), Linked->Enums.end(),
		                [](const auto& Enum)
		{
			return Enum.Name == "BoundColor";
		}) == 1,
		        "linked image retains one strong host enum identity"))
		{
			return EXIT_FAILURE;
		}
		const auto Changed = std::find_if(Second.Module->Enums.begin(), Second.Module->Enums.end(),
		    [](const auto& Enum)
		{
			return Enum.Name == "BoundColor";
		});
		if (!Expect(Changed != Second.Module->Enums.end() && !Changed->Members.empty(),
		        "independent unit contains its host enum descriptor"))
		{
			return EXIT_FAILURE;
		}
		Changed->Members[0].Value = 2;
		if (!Expect(!LinkBytecodeModules(*Root.Module, {Second.Module.get()}, Linked, Error),
		        "different host enum definitions remain rejected"))
		{
			return EXIT_FAILURE;
		}
		auto Left = CompileSource("export enum class PublicColor{Red=1};");
		auto Right = CompileSource("export enum class PublicColor{Red=1};");
		if (!Expect(Left && Right && !LinkBytecodeModules(*Left, {Right.get()}, Linked, Error),
		        "identical source enums retain ordinary duplicate ownership rejection"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FProgram InterfaceProgram;
		FFunctionDecl Function;
		Function.Name = "default_value";
		Function.ModulePath = "locale-test.vs";
		Function.ReturnType = "double";
		Function.bExported = true;
		FFunctionParam Parameter;
		Parameter.Name = "value";
		Parameter.TypeName = "double";
		Parameter.DefaultValue = std::make_unique<FFloatLiteralExpr>(1.5);
		Function.Parameters.push_back(std::move(Parameter));
		Function.Body = std::make_unique<FBlockStmt>();
		InterfaceProgram.Functions.push_back(std::move(Function));

		FStructDecl Token;
		Token.Name = "Token";
		Token.ModulePath = "locale-test.vs";
		Token.bExported = true;
		FConstructorDecl DefaultCtor;
		DefaultCtor.bDefaulted = true;
		DefaultCtor.Body = std::make_unique<FBlockStmt>();
		Token.Constructors.push_back(std::move(DefaultCtor));
		FConstructorDecl DeletedCtor;
		DeletedCtor.bExplicit = true;
		DeletedCtor.bDeleted = true;
		FFunctionParam DeletedCtorParam;
		DeletedCtorParam.TypeName = "string";
		DeletedCtorParam.Name = "text";
		DeletedCtor.Parameters.push_back(std::move(DeletedCtorParam));
		Token.Constructors.push_back(std::move(DeletedCtor));
		FFieldDecl MutableCache;
		MutableCache.TypeName = "int";
		MutableCache.Name = "cache";
		MutableCache.bIsMutable = true;
		Token.Fields.push_back(std::move(MutableCache));
		FMethodDecl DeletedMethod;
		DeletedMethod.ReturnType = "int";
		DeletedMethod.Name = "removed";
		DeletedMethod.bDeleted = true;
		FFunctionParam DeletedMethodParam;
		DeletedMethodParam.TypeName = "int";
		DeletedMethodParam.Name = "value";
		DeletedMethod.Parameters.push_back(std::move(DeletedMethodParam));
		Token.Methods.push_back(std::move(DeletedMethod));
		FFriendDecl ClosedFriend;
		ClosedFriend.Kind = FFriendDecl::EKind::Type;
		ClosedFriend.TypeName = "Inspector";
		Token.Friends.push_back(std::move(ClosedFriend));
		Token.Destructor = std::make_unique<FDestructorDecl>();
		Token.Destructor->bDefaulted = true;
		Token.Destructor->Body = std::make_unique<FBlockStmt>();
		InterfaceProgram.Structs.push_back(std::move(Token));

		const std::locale PreviousLocale = std::locale();
		std::locale::global(std::locale(
		    PreviousLocale, new FCommaDecimalPoint));
		std::string InterfaceSource;
		std::string InterfaceError;
		const bool bBuilt = BuildPackageInterfaceSource(
		    InterfaceProgram,
		    {"locale-test.vs"},
		    InterfaceSource,
		    InterfaceError);
		std::locale::global(PreviousLocale);
		if (!Expect(
		        bBuilt && InterfaceSource.find("1.5") != std::string::npos && InterfaceSource.find("1,5") == std::string::npos && InterfaceSource.find("Token() = default;") != std::string::npos && InterfaceSource.find("explicit Token(string text) = delete;") != std::string::npos && InterfaceSource.find("mutable int cache;") != std::string::npos && InterfaceSource.find("int removed(int value) = delete;") != std::string::npos && InterfaceSource.find("~Token() = default;") != std::string::npos && InterfaceSource.find("friend") == std::string::npos,
		        "package interface must preserve locale-independent defaults and default/delete declarations"))
		{
			std::cerr << InterfaceError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		FProgram StaticTypeInterfaceProgram;
		FClassDecl StaticUtility;
		StaticUtility.Name = "StaticUtility";
		StaticUtility.ModulePath = "static-interface.vs";
		StaticUtility.bExported = true;
		StaticUtility.bIsStatic = true;
		FMethodDecl GetAnswer;
		GetAnswer.Name = "GetAnswer";
		GetAnswer.ReturnType = "int";
		GetAnswer.Access = EMemberAccess::Public;
		GetAnswer.bIsStatic = true;
		GetAnswer.Body = std::make_unique<FBlockStmt>();
		StaticUtility.Methods.push_back(std::move(GetAnswer));
		StaticTypeInterfaceProgram.Classes.push_back(std::move(StaticUtility));
		FStructDecl StaticRecord;
		StaticRecord.Name = "StaticRecord";
		StaticRecord.ModulePath = "static-interface.vs";
		StaticRecord.bExported = true;
		StaticRecord.bIsStatic = true;
		FFieldDecl Seed;
		Seed.Name = "Seed";
		Seed.TypeName = "int";
		Seed.Access = EMemberAccess::Public;
		Seed.bIsStatic = true;
		StaticRecord.Fields.push_back(std::move(Seed));
		StaticTypeInterfaceProgram.Structs.push_back(std::move(StaticRecord));

		std::string InterfaceSource;
		std::string InterfaceError;
		if (!Expect(
		        BuildPackageInterfaceSource(
		            StaticTypeInterfaceProgram,
		            {"static-interface.vs"},
		            InterfaceSource,
		            InterfaceError) &&
		            InterfaceSource.find("export static class StaticUtility") != std::string::npos && InterfaceSource.find("export static struct StaticRecord") != std::string::npos,
		        "package interface must preserve static class and struct declarations"))
		{
			std::cerr << InterfaceError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		FProgram AvailabilityProgram;
		FStructDecl HiddenLocal;
		HiddenLocal.Name = "Demo::Shared";
		HiddenLocal.ModulePath = "api.vs";
		AvailabilityProgram.Structs.push_back(std::move(HiddenLocal));
		FClassDecl Exported;
		Exported.Name = "Demo.Shared";
		Exported.ModulePath = "library.vs";
		Exported.bExported = true;
		AvailabilityProgram.Classes.push_back(std::move(Exported));
		FFunctionDecl Function;
		Function.Name = "value";
		Function.ModulePath = "api.vs";
		Function.ReturnType = "Demo::Shared";
		Function.bExported = true;
		Function.Body = std::make_unique<FBlockStmt>();
		AvailabilityProgram.Functions.push_back(std::move(Function));
		std::string Source, Error;
		const auto Build = [&]()
		{
			return BuildPackageInterfaceSource(AvailabilityProgram, {"api.vs", "library.vs"}, Source, Error);
		};
		if (!Expect(!Build() && Error.find("Demo.Shared") != std::string::npos,
		        "package availability gives a hidden same-module declaration priority over another export"))
		{
			return EXIT_FAILURE;
		}
		AvailabilityProgram.Structs.front().ModulePath = "hidden.vs";
		if (!Expect(Build(), "package availability finds another module's export after hidden candidates"))
		{
			return EXIT_FAILURE;
		}
		AvailabilityProgram.Classes.front().bExported = false;
		if (!Expect(!Build(), "package availability rejects a type with only unavailable declarations"))
		{
			return EXIT_FAILURE;
		}
	}

	auto Original = CompileSource("void main() { print(\"package\"); }\n");
	if (!Expect(static_cast<bool>(Original), "failed to compile package module"))
	{
		return EXIT_FAILURE;
	}

	FPackageBuildOptions Options;
	Options.PackageName = "PackageTest";
	Options.PackageVersion = 42;
	Options.HostMetadata["a.one"] = {0x00, 0x7F, 0xFF};
	Options.HostMetadata["b.two"] = {'o', 'p', 'a', 'q', 'u', 'e'};
	Options.HostMetadata["future.host.extension.v9"] = {9, 8, 7, 6};
	std::vector<std::uint8_t> First;
	std::vector<std::uint8_t> Second;
	std::string Error;
	if (!Expect(
	        BuildPackage(*Original, Options, First, Error) && BuildPackage(*Original, Options, Second, Error) && First == Second,
	        "package builds must be deterministic"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	FPackageData Parsed;
	if (!Expect(
	        ReadPackage(FByteView(First), Parsed, Error),
	        "canonical package should parse") ||
	    !Expect(Parsed.PackageVersion == 42, "package version should round-trip") || !Expect(Parsed.Kind == EPackageKind::Application && Parsed.PackageName == "PackageTest", "package kind and name should round-trip") || !Expect(Parsed.HostMetadata == Options.HostMetadata, "unknown host metadata must round-trip unchanged") || !Expect(Parsed.CanonicalSignedBytes == First, "signed range must be the exact accepted canonical bytes") || !Expect(static_cast<bool>(Parsed.Module), "parsed package needs a module"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	{
		auto ReadonlyModule = CompileSource(
		    "struct Counter { int value = 1; void set_value(int next) { value = next; } };\n"
		    "const array<int> SharedLocked = {5};\n"
		    "const array<int>& shared_locked() { return SharedLocked; }\n"
		    "void main() {\n"
		    "    readonly<Counter> counter = Counter();\n"
		    "    const_cast<Counter>(counter.get()).set_value(12);\n"
		    "    const_cast<array<int>>(shared_locked()).push(6);\n"
		    "    print(counter.get().value);\n"
		    "    print(len(SharedLocked));\n"
		    "}\n");
		if (!Expect(
		        static_cast<bool>(ReadonlyModule),
		        "readonly package fixture should compile"))
		{
			return EXIT_FAILURE;
		}
		for (const EExecutionBackend Backend : {
		         EExecutionBackend::Vm,
		         EExecutionBackend::Mixed})
		{
			FPackageData ReadonlyPackage;
			FCaptureHost Host;
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Session.SetExecutionBackend(Backend);
			if (!Expect(
			        ReadBuiltPackage(*ReadonlyModule, 1, ReadonlyPackage) && Session.LoadPackage(ReadonlyPackage, "Readonly.vsapp").bSuccess && Session.Run().bSuccess,
			        "readonly package should deserialize and run") ||
			    !Expect(
			        Host.Output == "12\n2\n",
			        "readonly package produced wrong output"))
			{
				std::cerr << Host.Errors;
				return EXIT_FAILURE;
			}
		}
	}

	{
		auto ExceptionModule = CompileSource(
		    "int main() {\n"
		    "    int result = 0;\n"
		    "    try { throw 7; }\n"
		    "    catch (const int& value) { result = value; }\n"
		    "    finally { result += 2; }\n"
		    "    return result;\n"
		    "}\n");
		if (!Expect(
		        static_cast<bool>(ExceptionModule),
		        "exception package fixture should compile"))
		{
			return EXIT_FAILURE;
		}
		for (const EExecutionBackend Backend : {
		         EExecutionBackend::Vm,
		         EExecutionBackend::Mixed})
		{
			FPackageData ExceptionPackage;
			FCaptureHost Host;
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Session.SetExecutionBackend(Backend);
			if (!Expect(
			        ReadBuiltPackage(*ExceptionModule, 1, ExceptionPackage) && Session.LoadPackage(ExceptionPackage, "Exception.vsapp").bSuccess,
			        "exception package should deserialize and load"))
			{
				std::cerr << Host.Errors;
				return EXIT_FAILURE;
			}
			const FRunResult Run = Session.Run();
			if (!Expect(
			        Run.bSuccess && Run.ExitCode == 9,
			        "exception package should preserve catch/finally metadata"))
			{
				std::cerr << Host.Errors;
				return EXIT_FAILURE;
			}
		}
	}

	{
		FPackageBuildOptions StrippedOptions = Options;
		StrippedOptions.bStripDebugInfo = true;
		std::vector<std::uint8_t> StrippedBytes;
		FPackageData Stripped;
		if (!Expect(
		        BuildPackage(
		            *Original, StrippedOptions, StrippedBytes, Error) &&
		            ReadPackage(FByteView(StrippedBytes), Stripped, Error),
		        "stripped package should build and parse") ||
		    !Expect(
		        Stripped.Module->DebugFiles.empty() && Stripped.Module->InitDebugLines.empty() && Stripped.Module->EntryModulePath.find("<package-test>") == std::string::npos,
		        "stripped package must remove module debug paths and tables"))
		{
			return EXIT_FAILURE;
		}
		for (const FBytecodeFunction& Function : Stripped.Module->Functions)
		{
			if (!Expect(
			        Function.DebugLines.empty() && Function.ModulePath.find("<package-test>") == std::string::npos && Function.Name.find("<package-test>") == std::string::npos && Function.ClosureTypeName.find("<package-test>") == std::string::npos,
			        "stripped package must remove function debug paths"))
			{
				return EXIT_FAILURE;
			}
		}
		const FBytecodeFunction& StrippedMain = Stripped.Module->Functions[static_cast<std::size_t>(Stripped.Module->MainFunctionIndex)];
		const std::size_t MainIdentity = StrippedMain.Name.find('\x1F');
		if (!Expect(
		        MainIdentity != std::string::npos && StrippedMain.Name.substr(0, MainIdentity) == Stripped.Module->EntryModulePath,
		        "stripped entry identity must use the same virtual module path"))
		{
			return EXIT_FAILURE;
		}
		for (const std::string& Value : Stripped.Module->StringPool)
		{
			if (!Expect(
			        Value.find("<package-test>") == std::string::npos,
			        "stripped package must remove linkage paths from structural strings"))
			{
				return EXIT_FAILURE;
			}
		}
		for (const FBytecodeTypeDesc& Type : Stripped.Module->Types)
		{
			if (!Expect(
			        Type.Name.find("<package-test>") == std::string::npos,
			        "stripped package must remove private type source paths"))
			{
				return EXIT_FAILURE;
			}
		}
		for (const std::string& Global : Stripped.Module->GlobalNames)
		{
			if (!Expect(
			        Global.find("<package-test>") == std::string::npos,
			        "stripped package must remove private global source paths"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const auto ReflectionModule = CompileSource(
		    "struct ReflectBox { int value; int add(int n) { value += n; return value; } }\n"
		    "int main() {\n"
		    "    type<ReflectBox> info = type<ReflectBox>();\n"
		    "    field_info<ReflectBox> field = field_info<ReflectBox>(info.qualified_name, 0);\n"
		    "    method_info<ReflectBox> method = method_info<ReflectBox>(info.qualified_name, 0);\n"
		    "    ReflectBox box = ReflectBox();\n"
		    "    field.set<int>(box, 40);\n"
		    "    assert(field.get<int>(box) == 40);\n"
		    "    print(\"literal#03:int\");\n"
		    "    return method.invoke<int>(box, 2);\n"
		    "}\n");
		if (!Expect(static_cast<bool>(ReflectionModule),
		        "executable reflection package fixture should compile"))
		{
			return EXIT_FAILURE;
		}
		FPackageBuildOptions ReflectionOptions = Options;
		ReflectionOptions.bStripDebugInfo = true;
		std::vector<std::uint8_t> ReflectionBytes;
		FPackageData ReflectionPackage;
		if (!Expect(BuildPackage(*ReflectionModule, ReflectionOptions,
		                ReflectionBytes, Error) &&
		            ReadPackage(FByteView(ReflectionBytes), ReflectionPackage, Error),
		        "stripped executable reflection package should build and load"))
		{
			std::cerr << Error;
			return EXIT_FAILURE;
		}
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Expect(Session.LoadPackage(ReflectionPackage,
		                       "Reflection.vsapp")
		                .bSuccess,
		        "stripped executable reflection package should load into a session"))
		{
			std::cerr << Host.Errors;
			return EXIT_FAILURE;
		}
		const FRunResult ReflectionRun = Session.Run();
		if (!Expect(ReflectionRun.bSuccess && ReflectionRun.ExitCode == 42 && Host.Output == "literal#03:int\n",
		        "executable reflection must retain its binding in a stripped package"))
		{
			std::cerr << Host.Errors;
			return EXIT_FAILURE;
		}
	}

	{
		std::vector<std::uint8_t> Bytecode;
		constexpr std::uint64_t Fingerprint = 0x0123456789ABCDEFull;
		if (!Expect(
		        VeroScriptBytecodeIO::SerializeBytecode(
		            *Original, Fingerprint, Bytecode, Error),
		        "memory bytecode serialization should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        static_cast<bool>(VeroScriptBytecodeIO::DeserializeBytecode(
		            FByteView(Bytecode), Fingerprint, Error)),
		        "matching bytecode fingerprint should load") ||
		    !Expect(
		        !VeroScriptBytecodeIO::DeserializeBytecode(
		            FByteView(Bytecode), Fingerprint + 1, Error),
		        "mismatching bytecode fingerprint must fail") ||
		    !Expect(
		        static_cast<bool>(VeroScriptBytecodeIO::DeserializeBytecode(
		            FByteView(Bytecode), std::nullopt, Error)),
		        "null fingerprint should skip only fingerprint comparison"))
		{
			return EXIT_FAILURE;
		}
		std::vector<std::uint8_t> WrongVersion = Bytecode;
		WrongVersion[4] ^= 0x01u;
		if (!Expect(
		        !VeroScriptBytecodeIO::DeserializeBytecode(
		            FByteView(WrongVersion), std::nullopt, Error),
		        "null fingerprint must not skip bytecode version validation"))
		{
			return EXIT_FAILURE;
		}
		std::vector<std::uint8_t> OldLayout = Bytecode;
		OldLayout[0] = 0x56u; // Previous development magic: "VRO\0".
		OldLayout[1] = 0x52u;
		OldLayout[2] = 0x4Fu;
		OldLayout[3] = 0x00u;
		if (!Expect(
		        !VeroScriptBytecodeIO::DeserializeBytecode(
		            FByteView(OldLayout), std::nullopt, Error),
		        "old 1.0.0 binary layout must be rejected despite matching version"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::vector<std::uint8_t> Corrupt = First;
		Corrupt[0] ^= 0x01u;
		FPackageData Rejected;
		if (!Expect(
		        !ReadPackage(FByteView(Corrupt), Rejected, Error),
		        "bad package magic must fail"))
		{
			return EXIT_FAILURE;
		}
		Corrupt = First;
		Corrupt.pop_back();
		if (!Expect(
		        !ReadPackage(FByteView(Corrupt), Rejected, Error),
		        "truncated package must fail"))
		{
			return EXIT_FAILURE;
		}
		Corrupt = First;
		Corrupt.push_back(0);
		if (!Expect(
		        !ReadPackage(FByteView(Corrupt), Rejected, Error),
		        "trailing package bytes must fail"))
		{
			return EXIT_FAILURE;
		}

		// Fixed header, package name, first key/value, then second key.
		Corrupt = First;
		std::size_t Offset = 28 + Options.PackageName.size();
		const std::uint32_t FirstKeyLength =
		    Bytecode::ReadU32LE(Corrupt.data() + Offset);
		const std::size_t FirstKeyOffset = Offset + 4;
		Offset = FirstKeyOffset + FirstKeyLength;
		const std::uint64_t FirstValueLength =
		    Bytecode::ReadU64LE(Corrupt.data() + Offset);
		Offset += 8 + static_cast<std::size_t>(FirstValueLength);
		const std::uint32_t SecondKeyLength =
		    Bytecode::ReadU32LE(Corrupt.data() + Offset);
		if (!Expect(
		        FirstKeyLength == SecondKeyLength,
		        "test metadata keys should have equal lengths"))
		{
			return EXIT_FAILURE;
		}
		std::copy_n(
		    Corrupt.data() + FirstKeyOffset,
		    FirstKeyLength,
		    Corrupt.data() + Offset + 4);
		if (!Expect(
		        !ReadPackage(FByteView(Corrupt), Rejected, Error),
		        "duplicate/noncanonical metadata keys must fail"))
		{
			return EXIT_FAILURE;
		}

		Corrupt = First;
		Offset = 28 + Options.PackageName.size();
		for (std::uint32_t Index = 0;
		    Index < static_cast<std::uint32_t>(Options.HostMetadata.size());
		    ++Index)
		{
			const std::uint32_t KeyLength =
			    Bytecode::ReadU32LE(Corrupt.data() + Offset);
			Offset += 4 + KeyLength;
			const std::uint64_t ValueLength =
			    Bytecode::ReadU64LE(Corrupt.data() + Offset);
			Offset += 8 + static_cast<std::size_t>(ValueLength);
		}
		const std::uint64_t BytecodeLength =
		    Bytecode::ReadU64LE(Corrupt.data() + Offset);
		Offset += 8;
		if (!Expect(
		        BytecodeLength > 4 && Offset + BytecodeLength == Corrupt.size(),
		        "test package bytecode offset should be valid"))
		{
			return EXIT_FAILURE;
		}
		const std::size_t BytecodeOffset = Offset;
		Corrupt[Offset] ^= 0x01u;
		Rejected.PackageVersion = 999;
		Rejected.HostMetadata = {{"sentinel", {1}}};
		if (!Expect(
		        !ReadPackage(FByteView(Corrupt), Rejected, Error),
		        "corrupt contained bytecode must fail") ||
		    !Expect(
		        Rejected.PackageVersion == 999 && Rejected.HostMetadata.count("sentinel") == 1,
		        "failed ReadPackage must not change its output object"))
		{
			return EXIT_FAILURE;
		}

		Corrupt = First;
		// Bytecode header: magic(4), semantic version(6), then the canonical
		// package source fingerprint (must be zero).
		Corrupt[BytecodeOffset + 10] ^= 0x01u;
		if (!Expect(
		        !ReadPackage(FByteView(Corrupt), Rejected, Error),
		        "package bytecode fingerprint must use canonical zero"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FPackageBuildOptions OtherVersion = Options;
		OtherVersion.PackageVersion = 43;
		std::vector<std::uint8_t> OtherBytes;
		if (!Expect(
		        BuildPackage(*Original, OtherVersion, OtherBytes, Error) && OtherBytes != First,
		        "package version must participate in signed bytes"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		auto LibraryModule = CompileSource(
		    "export int library_answer() { return 42; }\n");
		FPackageBuildOptions LibraryOptions;
		LibraryOptions.Kind = EPackageKind::Library;
		LibraryOptions.PackageName = "Test.Library";
		LibraryOptions.PackageVersion = 1;
		std::vector<std::uint8_t> LibraryBytes;
		FPackageData Library;
		if (!Expect(
		        LibraryModule && BuildPackage(*LibraryModule, LibraryOptions, LibraryBytes, Error) && ReadPackage(FByteView(LibraryBytes), Library, Error),
		        "library package should build and parse") ||
		    !Expect(
		        Library.Kind == EPackageKind::Library && Library.PackageName == "Test.Library" && Library.Module && Library.Module->MainFunctionIndex < 0,
		        "library package identity or entry-point invariant mismatch"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		std::vector<FFreeFunctionInfo> LibraryFunctions;
		ModuleMeta::EnumerateFreeFunctions(
		    *Library.Module, LibraryFunctions, false);
		if (!Expect(
		        LibraryFunctions.size() == 1 && LibraryFunctions.front().Name == "library_answer" && LibraryFunctions.front().ModulePath == "<package-test>" && LibraryFunctions.front().bExported,
		        "library package must preserve exported function reflection provenance"))
		{
			return EXIT_FAILURE;
		}
		FCaptureHost LibraryHost;
		VeroScriptSession LibrarySession(LibraryHost);
		VeroScript::Test::Configure(LibrarySession);
		if (!Expect(
		        LibrarySession.LoadPackage(
		                          Library, "Test.Library.vslib")
		            .bSuccess,
		        "session should load a library package for host function calls") ||
		    !Expect(
		        !LibrarySession.Run().bSuccess,
		        "session must reject running a library package as an application"))
		{
			return EXIT_FAILURE;
		}
		FPackageData InvalidKindPackage;
		InvalidKindPackage.Kind = static_cast<EPackageKind>(0xff);
		InvalidKindPackage.Module = CompileSource(
		    "export int invalid_kind_value() { return 1; }\n");
		if (!Expect(
		        InvalidKindPackage.Module && !LibrarySession.LoadPackage(InvalidKindPackage, "InvalidKind.vslib").bSuccess && InvalidKindPackage.Module,
		        "session must reject an invalid package kind without consuming it"))
		{
			return EXIT_FAILURE;
		}

		FPackageBuildOptions WrongKind = Options;
		WrongKind.Kind = EPackageKind::Library;
		std::vector<std::uint8_t> RejectedBytes;
		if (!Expect(
		        !BuildPackage(*Original, WrongKind, RejectedBytes, Error),
		        "library package with main must be rejected"))
		{
			return EXIT_FAILURE;
		}

		FBytecodeModule NestedLibrary = *LibraryModule;
		NestedLibrary.bContainsPackageDependencies = true;
		FPackageData RejectedPackage;
		if (!Expect(
		        WrapRawPackageForReadTest(
		            NestedLibrary,
		            EPackageKind::Library,
		            RejectedBytes,
		            Error) &&
		            ReadPackage(
		                FByteView(RejectedBytes), RejectedPackage, Error) &&
		            RejectedPackage.Module && RejectedPackage.Module->bContainsPackageDependencies,
		        "package reader must preserve nested-library dependency state"))
		{
			return EXIT_FAILURE;
		}
		FBytecodeModule InvalidInterfaceLibrary = *LibraryModule;
		InvalidInterfaceLibrary.PackageInterfaceError = "invalid public ABI";
		if (!Expect(
		        WrapRawPackageForReadTest(
		            InvalidInterfaceLibrary,
		            EPackageKind::Library,
		            RejectedBytes,
		            Error) &&
		            !ReadPackage(
		                FByteView(RejectedBytes), RejectedPackage, Error),
		        "package reader must reject a library with an invalid interface"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FPackageBuildOptions LibraryOptions;
		LibraryOptions.Kind = EPackageKind::Library;
		LibraryOptions.PackageName = "Invalid.OpenGenericFixture";
		std::vector<std::uint8_t> RejectedBytes;
		auto GenericModule = CompileSource(
		    "export struct Box<T> { T value; }\n");
		if (!Expect(
		        GenericModule && !BuildPackage(*GenericModule, LibraryOptions, RejectedBytes, Error) && Error.find("no retained template modules") != std::string::npos,
		        "a manually assembled open generic needs retained template modules"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}

		auto InvalidSurfaceModule = CompileSource(
		    "struct InternalOnly { int value; }\n"
		    "export class PublicType {\n"
		    "protected: InternalOnly state;\n"
		    "}\n");
		LibraryOptions.PackageName = "Invalid.PrivateSurfaceType";
		if (!Expect(
		        InvalidSurfaceModule && !BuildPackage(*InvalidSurfaceModule, LibraryOptions, RejectedBytes, Error) && Error.find("not exported by this package") != std::string::npos,
		        "public/protected package ABI must not reference hidden script types"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}

		auto InvalidNamespacedSurfaceModule = CompileSource(
		    "namespace api {\n"
		    "struct InternalOnly { int value; }\n"
		    "export struct PublicType { public: InternalOnly state; }\n"
		    "}\n");
		LibraryOptions.PackageName = "Invalid.NamespacedPrivateSurfaceType";
		if (!Expect(
		        InvalidNamespacedSurfaceModule && !BuildPackage(*InvalidNamespacedSurfaceModule, LibraryOptions, RejectedBytes, Error) && Error.find("api.InternalOnly") != std::string::npos,
		        "unqualified hidden types in an exported namespace must be rejected"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}

		auto NamespacedShadowModule = CompileSource(
		    "namespace internal { struct Shared { int hidden; } }\n"
		    "namespace api {\n"
		    "export struct Shared { int value; }\n"
		    "export struct PublicType { public: Shared state; }\n"
		    "}\n");
		LibraryOptions.PackageName = "Valid.NamespacedExportShadow";
		if (!Expect(
		        NamespacedShadowModule && BuildPackage(*NamespacedShadowModule, LibraryOptions, RejectedBytes, Error),
		        "an exported type in the current namespace must shadow unrelated hidden types"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		auto RangeModule = CompileSource(
		    "int first = 1;\n"
		    "int second = 2;\n"
		    "void main() {}\n");
		if (!Expect(
		        RangeModule && RangeModule->GlobalInitRanges.size() >= 2,
		        "range validation test module should have two initializers"))
		{
			return EXIT_FAILURE;
		}
		FBytecodeModule Misaligned = *RangeModule;
		++Misaligned.GlobalInitRanges.front().BeginOffset;
		std::vector<std::uint8_t> RejectedBytes;
		if (!Expect(
		        !BuildPackage(Misaligned, Options, RejectedBytes, Error),
		        "misaligned initializer range must be rejected"))
		{
			return EXIT_FAILURE;
		}

		FBytecodeModule Duplicate = *RangeModule;
		Duplicate.GlobalInitRanges.push_back(
		    Duplicate.GlobalInitRanges.front());
		if (!Expect(
		        !BuildPackage(Duplicate, Options, RejectedBytes, Error),
		        "duplicate initializer range must be rejected"))
		{
			return EXIT_FAILURE;
		}

		FBytecodeModule Crossing = *RangeModule;
		const FBytecodeGlobalInitRange FirstRange =
		    Crossing.GlobalInitRanges[0];
		const FBytecodeGlobalInitRange SecondRange =
		    Crossing.GlobalInitRanges[1];
		const std::size_t InnerBoundary =
		    FirstRange.BeginOffset + Bytecode::InstructionSize(static_cast<EOpcode>(Crossing.InitCode[FirstRange.BeginOffset]));
		Crossing.GlobalInitRanges = {
		    {FirstRange.GlobalSlot,
		        FirstRange.BeginOffset,
		        SecondRange.BeginOffset},
		    {SecondRange.GlobalSlot,
		        InnerBoundary,
		        SecondRange.EndOffset}};
		if (!Expect(
		        !BuildPackage(Crossing, Options, RejectedBytes, Error),
		        "crossing initializer ranges must be rejected"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const EExecutionBackend Backend : {
	         EExecutionBackend::Vm,
	         EExecutionBackend::Mixed})
	{
		FPackageData Runnable;
		if (!Expect(
		        ReadPackage(FByteView(First), Runnable, Error),
		        "runnable package should parse"))
		{
			return EXIT_FAILURE;
		}
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetExecutionBackend(Backend);
		if (!Expect(
		        Session.LoadPackage(Runnable, "App.vslib").bSuccess,
		        "LoadPackage should accept a validated module") ||
		    !Expect(!Runnable.Module, "LoadPackage must consume on success") || !Expect(Session.Run().bSuccess, "loaded package should run") || !Expect(Host.Output == "package\n", "package produced wrong output"))
		{
			std::cerr << Host.Errors;
			return EXIT_FAILURE;
		}
	}

	{
		auto LibraryModule = CompileSource(
		    "export int answer() { return 42; }\n");
		auto ApplicationModule = CompileSource(
		    "export int answer() { return 0; }\n"
		    "void main() { print(answer()); }\n");
		if (!Expect(
		        LibraryModule && ApplicationModule,
		        "linker fixtures should compile"))
		{
			return EXIT_FAILURE;
		}
		const auto StubIt = ApplicationModule->FunctionIndices.find(
		    "answer#fn#0");
		if (!Expect(
		        StubIt != ApplicationModule->FunctionIndices.end(),
		        "application fixture should contain answer stub"))
		{
			return EXIT_FAILURE;
		}
		FBytecodeFunction& Stub =
		    ApplicationModule->Functions[StubIt->second];
		Stub.Code.clear();
		Stub.DebugLines.clear();
		Stub.LocalNames.clear();
		Stub.LocalCount = 0;

		auto LibraryFileLabel =
		    std::make_shared<std::string>("linked-library-anchor.vs");
		auto ApplicationFileLabel =
		    std::make_shared<std::string>("linked-application-anchor.vs");
		int SharedAnchorAddress = 0;
		auto FirstSharedAddressOwner = std::shared_ptr<const void>(
		    &SharedAnchorAddress, [](const void*)
		{
		});
		auto SecondSharedAddressOwner = std::shared_ptr<const void>(
		    &SharedAnchorAddress, [](const void*)
		{
		});
		const std::weak_ptr<const void> WeakSecondSharedAddressOwner =
		    SecondSharedAddressOwner;
		LibraryModule->LifetimeAnchors.push_back(LibraryFileLabel);
		LibraryModule->LifetimeAnchors.push_back(FirstSharedAddressOwner);
		LibraryModule->LifetimeAnchors.push_back(SecondSharedAddressOwner);
		ApplicationModule->LifetimeAnchors.push_back(ApplicationFileLabel);
		LibraryModule->Functions[LibraryModule->FunctionIndices.at("answer#fn#0")]
		    .Location.FileLabel = *LibraryFileLabel;
		ApplicationModule->Functions[static_cast<std::size_t>(
		                                 ApplicationModule->MainFunctionIndex)]
		    .Location.FileLabel =
		    *ApplicationFileLabel;

		std::unique_ptr<FBytecodeModule> Linked;
		Error.clear();
		const bool bLinked = LinkBytecodeModules(
		    *ApplicationModule,
		    {LibraryModule.get()},
		    Linked,
		    Error);
		if (!Expect(
		        bLinked && Linked,
		        "application should link against library function stub"))
		{
			std::cerr << "LinkBytecodeModules=" << bLinked
			          << ", module=" << static_cast<bool>(Linked)
			          << ", error=[" << Error << "]\n";
			return EXIT_FAILURE;
		}
		LibraryModule.reset();
		ApplicationModule.reset();
		LibraryFileLabel.reset();
		ApplicationFileLabel.reset();
		FirstSharedAddressOwner.reset();
		SecondSharedAddressOwner.reset();
		std::vector<std::string> LifetimeChurn(
		    4096, std::string(256, 'x'));
		const auto LinkedAnswer = Linked->FunctionIndices.find("answer#fn#0");
		if (!Expect(
		        Linked->LifetimeAnchors.size() >= 2 && !WeakSecondSharedAddressOwner.expired() && LinkedAnswer != Linked->FunctionIndices.end() && Linked->Functions[LinkedAnswer->second].Location.FileLabel == "linked-library-anchor.vs" && Linked->MainFunctionIndex >= 0 && Linked->Functions[static_cast<std::size_t>(Linked->MainFunctionIndex)].Location.FileLabel == "linked-application-anchor.vs",
		        "linked module must retain root and dependency location owners"))
		{
			return EXIT_FAILURE;
		}
		FPackageBuildOptions LinkedOptions;
		LinkedOptions.PackageName = "LinkedApp";
		std::vector<std::uint8_t> LinkedBytes;
		FPackageData LinkedPackage;
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Expect(
		        BuildPackage(
		            *Linked, LinkedOptions, LinkedBytes, Error) &&
		            ReadPackage(
		                FByteView(LinkedBytes), LinkedPackage, Error) &&
		            Session.LoadPackage(
		                       LinkedPackage, "LinkedApp.vsapp")
		                .bSuccess &&
		            Session.Run().bSuccess,
		        "linked application package should run") ||
		    !Expect(
		        Host.Output == "42\n",
		        "linked call should execute the library implementation"))
		{
			std::cerr << Error << Host.Errors << Host.Output;
			return EXIT_FAILURE;
		}
	}

	{
		// Seeding the compiler from package bytecode replaces its initially empty
		// output module. The replacement must still own both source and package
		// template backing storage used by later generic instantiations/diagnostics.
		auto DependencyModule = CompileSource(
		    "export int retained_dependency() { return 7; }\n");
		if (!Expect(
		        DependencyModule != nullptr,
		        "compiler lifetime dependency fixture should compile"))
		{
			return EXIT_FAILURE;
		}
		auto Dependency = std::shared_ptr<const FBytecodeModule>(
		    std::move(DependencyModule));
		const std::weak_ptr<const FBytecodeModule> WeakDependency = Dependency;

		const std::string Source = "void main() { print(1); }\n";
		VeroScriptLexer Lexer(Source, "<package-anchor-test>");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		VeroScriptParser Parser(Tokens, "<package-anchor-test>");
		std::unique_ptr<FProgram> Program = Parser.ParseProgram();
		if (!Expect(
		        !Lexer.HasError() && !Parser.HasError() && Program,
		        "compiler lifetime root fixture should parse"))
		{
			return EXIT_FAILURE;
		}
		Program->EntryModulePath = "<package-anchor-test>";
		Program->PrecompiledPackageModules.push_back(Dependency);
		VeroScriptCompiler Compiler;
		std::unique_ptr<FBytecodeModule> Seeded = Compiler.Compile(*Program);
		Program.reset();
		Dependency.reset();
		const bool bRetainedDependency = Seeded && !WeakDependency.expired();
		if (!Expect(
		        !Compiler.HasError() && bRetainedDependency,
		        "package-seeded compilation must retain dependency storage"))
		{
			if (Compiler.HasError())
			{
				std::cerr << Compiler.GetError().Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		FBytecodeModule RootWithUnresolvedVoidStub;
		FBytecodeFunction Stub;
		Stub.Name = "missing_void#fn#0";
		Stub.bReturnsVoid = true;
		RootWithUnresolvedVoidStub.Functions.push_back(std::move(Stub));
		RootWithUnresolvedVoidStub.FunctionIndices.emplace(
		    "missing_void#fn#0", std::uint16_t{0});
		std::unique_ptr<FBytecodeModule> Linked;
		if (!Expect(
		        !LinkBytecodeModules(
		            RootWithUnresolvedVoidStub, {}, Linked, Error),
		        "linker must reject an unresolved void function stub"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FBytecodeModule PositiveZeroModule;
		FBytecodeConstant PositiveZero;
		PositiveZero.Kind = EConstantKind::F64;
		PositiveZero.F64 = 0.0;
		PositiveZeroModule.Constants.push_back(PositiveZero);
		FBytecodeModule NegativeZeroModule;
		FBytecodeConstant NegativeZero = PositiveZero;
		NegativeZero.F64 = -0.0;
		NegativeZeroModule.Constants.push_back(NegativeZero);
		FBytecodeModule EmptyRoot;
		std::unique_ptr<FBytecodeModule> Linked;
		if (!Expect(
		        LinkBytecodeModules(
		            EmptyRoot,
		            {&PositiveZeroModule, &NegativeZeroModule},
		            Linked,
		            Error) &&
		            Linked && Linked->Constants.size() == 2 && !std::signbit(Linked->Constants[0].F64) && std::signbit(Linked->Constants[1].F64),
		        "linker constant deduplication must preserve floating bit patterns"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		auto SharedModule = CompileSource(
		    "export int shared_counter = 1;\n"
		    "export class SharedBox { public: const int value = 1; int read() { return value; } }\n"
		    "export class SharedChild : public SharedBox { public: using SharedBox::value; }\n"
		    "export int shared_answer() { return shared_counter; }\n");
		FPackageBuildOptions SharedOptions;
		SharedOptions.Kind = EPackageKind::Library;
		SharedOptions.PackageName = "Shared.Component";
		SharedOptions.PackageVersion = 7;
		std::vector<std::uint8_t> SharedBytes;
		FPackageData FirstShared;
		FPackageData SecondShared;
		if (!Expect(
		        SharedModule && BuildPackage(*SharedModule, SharedOptions, SharedBytes, Error) && ReadPackage(FByteView(SharedBytes), FirstShared, Error) && ReadPackage(FByteView(SharedBytes), SecondShared, Error),
		        "shared-component fixtures should build and parse"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		FBytecodeModule EmptyRoot;
		std::unique_ptr<FBytecodeModule> Linked;
		if (!Expect(
		        LinkBytecodeModules(
		            EmptyRoot,
		            {FirstShared.Module.get(), SecondShared.Module.get()},
		            Linked,
		            Error) &&
		            Linked && Linked->PackageComponents.size() == 1 && Linked->Functions.size() == FirstShared.Module->Functions.size() && Linked->GlobalNames.size() == FirstShared.Module->GlobalNames.size() && Linked->InitCode.size() == FirstShared.Module->InitCode.size(),
		        "identical package components and initializers should link once"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}

		FBytecodeModule ConflictingField = *SecondShared.Module;
		bool bChangedField = false;
		for (FBytecodeTypeDesc& Type : ConflictingField.Types)
		{
			if (Type.Name.find("SharedBox") == std::string::npos)
			{
				continue;
			}
			for (FBytecodeFieldDesc& Field : Type.Fields)
			{
				if (Field.Name == "value")
				{
					Field.bIsConst = false;
					bChangedField = true;
				}
			}
		}
		if (!Expect(bChangedField, "shared package fixture has a const field") || !Expect(!LinkBytecodeModules(EmptyRoot, {FirstShared.Module.get(), &ConflictingField}, Linked, Error), "duplicate package field cannot change its const qualifier"))
		{
			return EXIT_FAILURE;
		}
		FBytecodeModule ConflictingAccess = *SecondShared.Module;
		for (FBytecodeTypeDesc& Type : ConflictingAccess.Types)
		{
			if (Type.Name.find("SharedBox") == std::string::npos)
			{
				continue;
			}
			for (FBytecodeFieldDesc& Field : Type.Fields)
			{
				if (Field.Name == "value")
				{
					Field.Access = EMemberAccess::Private;
				}
			}
		}
		if (!Expect(
		        !LinkBytecodeModules(
		            EmptyRoot,
		            {FirstShared.Module.get(), &ConflictingAccess},
		            Linked,
		            Error),
		        "duplicate package field cannot change its access level"))
		{
			return EXIT_FAILURE;
		}
		FBytecodeModule ConflictingBaseAccess = *SecondShared.Module;
		bool bChangedBaseAccess = false;
		for (FBytecodeTypeDesc& Type : ConflictingBaseAccess.Types)
		{
			if (Type.Name.find("SharedChild") != std::string::npos && !Type.BaseInheritanceAccess.empty())
			{
				Type.BaseInheritanceAccess.front() = EMemberAccess::Private;
				bChangedBaseAccess = true;
			}
		}
		if (!Expect(bChangedBaseAccess, "shared package fixture has inherited access") || !Expect(!LinkBytecodeModules(EmptyRoot, {FirstShared.Module.get(), &ConflictingBaseAccess}, Linked, Error), "duplicate package type cannot change its inheritance access"))
		{
			return EXIT_FAILURE;
		}
		FBytecodeModule ConflictingUsingAccess = *SecondShared.Module;
		bool bChangedUsingAccess = false;
		for (FBytecodeTypeDesc& Type : ConflictingUsingAccess.Types)
		{
			if (Type.Name.find("SharedChild") != std::string::npos && !Type.UsingMembers.empty())
			{
				Type.UsingMembers.front().Access = EMemberAccess::Private;
				bChangedUsingAccess = true;
			}
		}
		if (!Expect(bChangedUsingAccess, "shared package fixture has a using declaration") || !Expect(!LinkBytecodeModules(EmptyRoot, {FirstShared.Module.get(), &ConflictingUsingAccess}, Linked, Error), "duplicate package type cannot change using access"))
		{
			return EXIT_FAILURE;
		}
		FBytecodeModule ConflictingMethodAccess = *SecondShared.Module;
		bool bChangedMethodAccess = false;
		for (FBytecodeFunction& Function : ConflictingMethodAccess.Functions)
		{
			if (Function.Name.find("SharedBox::read") != std::string::npos)
			{
				Function.Access = EMemberAccess::Private;
				bChangedMethodAccess = true;
			}
		}
		if (!Expect(bChangedMethodAccess, "shared package fixture has a method") || !Expect(!LinkBytecodeModules(EmptyRoot, {FirstShared.Module.get(), &ConflictingMethodAccess}, Linked, Error), "duplicate package method cannot change its access level"))
		{
			return EXIT_FAILURE;
		}

		FBytecodeModule ConflictingFingerprint = *SecondShared.Module;
		ConflictingFingerprint.PackageComponents.front().Fingerprint ^= 1u;
		if (!Expect(
		        !LinkBytecodeModules(
		            EmptyRoot,
		            {FirstShared.Module.get(), &ConflictingFingerprint},
		            Linked,
		            Error),
		        "same package identity with a different fingerprint must fail"))
		{
			return EXIT_FAILURE;
		}

		FBytecodeModule UnexpectedDefinition = *SecondShared.Module;
		FBytecodeFunction Added;
		Added.Name = "unexpected_owned#fn#0";
		Added.bReturnsVoid = true;
		Added.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
		UnexpectedDefinition.FunctionIndices.emplace(
		    Added.Name,
		    static_cast<std::uint16_t>(UnexpectedDefinition.Functions.size()));
		UnexpectedDefinition.Functions.push_back(std::move(Added));
		UnexpectedDefinition.PackageFunctionOwners.push_back(
		    UnexpectedDefinition.PackageComponents.front().Id);
		if (!Expect(
		        !LinkBytecodeModules(
		            EmptyRoot,
		            {FirstShared.Module.get(), &UnexpectedDefinition},
		            Linked,
		            Error),
		        "an existing component must not add an unverified definition"))
		{
			return EXIT_FAILURE;
		}

		FBytecodeModule ConflictingInitializer = *SecondShared.Module;
		if (!Expect(
		        !ConflictingInitializer.Constants.empty(),
		        "shared component fixture should contain an initializer constant"))
		{
			return EXIT_FAILURE;
		}
		FBytecodeConstant& Changed = ConflictingInitializer.Constants.front();
		Changed.I32 += 1;
		if (!Expect(
		        !LinkBytecodeModules(
		            EmptyRoot,
		            {FirstShared.Module.get(), &ConflictingInitializer},
		            Linked,
		            Error),
		        "same component fingerprint must not hide changed initializer code"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const bool bStripDebugInfo : {false, true})
	{
		FBytecodeModule PrivateLibrary;
		FBytecodeFunction Helper;
		Helper.Name = std::string("C:/shared#1/private.vs") + '\x1F' + "helper#fn#0";
		Helper.bReturnsVoid = true;
		Helper.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
		PrivateLibrary.FunctionIndices.emplace(Helper.Name, std::uint16_t{0});
		PrivateLibrary.Functions.push_back(std::move(Helper));

		FPackageBuildOptions LeftOptions;
		LeftOptions.Kind = EPackageKind::Library;
		LeftOptions.PackageName = "PrivateLibA";
		LeftOptions.bStripDebugInfo = bStripDebugInfo;
		FPackageBuildOptions RightOptions = LeftOptions;
		RightOptions.PackageName = "PrivateLibB";
		std::vector<std::uint8_t> LeftBytes;
		std::vector<std::uint8_t> RightBytes;
		FPackageData LeftPackage;
		FPackageData RightPackage;
		if (!Expect(
		        BuildPackage(
		            PrivateLibrary, LeftOptions, LeftBytes, Error) &&
		            BuildPackage(
		                PrivateLibrary, RightOptions, RightBytes, Error) &&
		            ReadPackage(FByteView(LeftBytes), LeftPackage, Error) && ReadPackage(FByteView(RightBytes), RightPackage, Error),
		        "private-symbol package fixtures should round-trip"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        LeftPackage.Module->Functions[0].Name != RightPackage.Module->Functions[0].Name,
		        "private linkage identity must include the package name"))
		{
			return EXIT_FAILURE;
		}
		FBytecodeModule EmptyRoot;
		std::unique_ptr<FBytecodeModule> Linked;
		if (!Expect(
		        LinkBytecodeModules(
		            EmptyRoot,
		            {LeftPackage.Module.get(), RightPackage.Module.get()},
		            Linked,
		            Error) &&
		            Linked && Linked->Functions.size() == 2,
		        "independent packages must not collide on private symbols"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		const std::string StablePrefix = "C:/project#1/stable.vs";
		FBytecodeModule Before;
		FBytecodeFunction StableFunction;
		StableFunction.Name = StablePrefix + '\x1F' + "stable#fn#0";
		StableFunction.bReturnsVoid = true;
		StableFunction.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
		Before.FunctionIndices.emplace(StableFunction.Name, std::uint16_t{0});
		Before.Functions.push_back(StableFunction);

		FBytecodeModule After = Before;
		FBytecodeFunction AddedFunction;
		AddedFunction.Name = std::string("A:/earlier#1.vs") + '\x1F' + "added#fn#0";
		AddedFunction.bReturnsVoid = true;
		AddedFunction.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
		After.Functions.push_front(AddedFunction);
		After.FunctionIndices.clear();
		After.FunctionIndices.emplace(AddedFunction.Name, std::uint16_t{0});
		After.FunctionIndices.emplace(StableFunction.Name, std::uint16_t{1});

		FPackageBuildOptions StableOptions;
		StableOptions.Kind = EPackageKind::Library;
		StableOptions.PackageName = "StablePrivateIdentity";
		StableOptions.bStripDebugInfo = true;
		std::vector<std::uint8_t> BeforeBytes;
		std::vector<std::uint8_t> AfterBytes;
		FPackageData BeforePackage;
		FPackageData AfterPackage;
		if (!Expect(
		        BuildPackage(Before, StableOptions, BeforeBytes, Error) && BuildPackage(After, StableOptions, AfterBytes, Error) && ReadPackage(FByteView(BeforeBytes), BeforePackage, Error) && ReadPackage(FByteView(AfterBytes), AfterPackage, Error),
		        "stable private-identity package fixtures should round-trip"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		const std::string BeforeStableName =
		    BeforePackage.Module->Functions.front().Name;
		const auto AfterStable = std::find_if(
		    AfterPackage.Module->Functions.begin(),
		    AfterPackage.Module->Functions.end(),
		    [](const FBytecodeFunction& Function)
		{
			return Function.Name.find("stable#fn#0") != std::string::npos;
		});
		if (!Expect(
		        AfterStable != AfterPackage.Module->Functions.end() && AfterStable->Name == BeforeStableName,
		        "adding another module must not rename existing private package symbols"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Expect(
		        Session.LoadSource(
		                   "void main() { print(\"old\"); }\n", "<compiled-old>")
		            .bSuccess,
		        "compiled reload seed module should load"))
		{
			return EXIT_FAILURE;
		}
		auto WrongShapeModule = CompileSource(
		    "export int library_value() { return 1; }\n");
		const FBytecodeModule* const ShapeOldModule = Session.GetLoadedModule();
		if (!Expect(
		        WrongShapeModule && !Session.ReloadCompiledModule(WrongShapeModule, "WrongShape").bSuccess,
		        "compiled reload must preserve application/library shape") ||
		    !Expect(
		        WrongShapeModule != nullptr,
		        "wrong-shape reload must preserve caller ownership") ||
		    !Expect(
		        Session.GetLoadedModule() == ShapeOldModule,
		        "wrong-shape reload must preserve current module"))
		{
			return EXIT_FAILURE;
		}
		auto InvalidModule = CompileSource(
		    "int answer() { return 42; }\n"
		    "void main() {}\n");
		if (!Expect(InvalidModule != nullptr, "invalid reload fixture should compile"))
		{
			return EXIT_FAILURE;
		}
		const auto Answer = std::find_if(
		    InvalidModule->Functions.begin(),
		    InvalidModule->Functions.end(),
		    [](const FBytecodeFunction& Function)
		{
			return Function.Name.find("answer#fn#0") != std::string::npos;
		});
		if (!Expect(
		        Answer != InvalidModule->Functions.end(),
		        "invalid reload fixture should contain answer"))
		{
			return EXIT_FAILURE;
		}
		Answer->Code.clear();
		const FBytecodeModule* const OldModule = Session.GetLoadedModule();
		if (!Expect(
		        !Session.ReloadCompiledModule(
		                    InvalidModule, "InvalidCompiledModule")
		            .bSuccess,
		        "invalid compiled reload must fail") ||
		    !Expect(
		        InvalidModule != nullptr,
		        "failed compiled reload must preserve caller ownership") ||
		    !Expect(
		        Session.GetLoadedModule() == OldModule,
		        "failed compiled reload must preserve current module"))
		{
			return EXIT_FAILURE;
		}

		auto NewModule = CompileSource(
		    "void main() { print(\"compiled-new\"); }\n");
		if (!Expect(
		        NewModule && Session.ReloadCompiledModule(NewModule, "CompiledNew").bSuccess,
		        "compiled module reload should stage") ||
		    !Expect(
		        !NewModule,
		        "successful compiled reload should consume caller ownership") ||
		    !Expect(
		        Session.Run().bSuccess,
		        "compiled module replacement should initialize and run") ||
		    !Expect(
		        Host.Output == "compiled-new\n",
		        "compiled module replacement output mismatch"))
		{
			std::cerr << Host.Errors << Host.Output;
			return EXIT_FAILURE;
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Expect(
		        Session.LoadSource(
		                   "void main() { print(\"old\"); }\n", "<old>")
		            .bSuccess,
		        "reload seed module should load"))
		{
			return EXIT_FAILURE;
		}
		auto NewModule = CompileSource(
		    "void main() {\n"
		    "    try { throw \"new\"; }\n"
		    "    catch (const string& value) { print(value); }\n"
		    "    finally { print(\"finally\"); }\n"
		    "}\n");
		FPackageData NewPackage;
		if (!Expect(
		        NewModule && ReadBuiltPackage(*NewModule, 1, NewPackage),
		        "replacement package should build") ||
		    !Expect(
		        Session.ReloadPackage(NewPackage, "New.vslib").bSuccess,
		        "package reload should stage") ||
		    !Expect(!NewPackage.Module, "successful reload should consume module") || !Expect(Session.Run().bSuccess, "replacement package should initialize and run") || !Expect(Host.Output == "new\nfinally\n", "replacement package must preserve exception metadata"))
		{
			std::cerr << Host.Errors << Host.Output;
			return EXIT_FAILURE;
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Expect(
		        Session.LoadSource(
		                   "void main() { print(1); }\n", "<application>")
		            .bSuccess,
		        "application should load before package-kind mismatch test"))
		{
			return EXIT_FAILURE;
		}
		auto LibraryModule = CompileSource(
		    "export int callable() { return 1; }\n");
		FPackageBuildOptions LibraryOptions;
		LibraryOptions.Kind = EPackageKind::Library;
		LibraryOptions.PackageName = "Replacement.Library";
		std::vector<std::uint8_t> LibraryBytes;
		FPackageData LibraryPackage;
		if (!Expect(
		        LibraryModule && BuildPackage(*LibraryModule, LibraryOptions, LibraryBytes, Error) && ReadPackage(FByteView(LibraryBytes), LibraryPackage, Error),
		        "mismatched reload package should build") ||
		    !Expect(
		        !Session.ReloadPackage(
		                    LibraryPackage, "Replacement.Library.vslib")
		                .bSuccess &&
		            LibraryPackage.Module,
		        "application reload must reject a library without taking ownership"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Expect(
		        Session.LoadSource(
		                   "void main() { print(\"old\"); }\n", "<old>")
		            .bSuccess,
		        "old module should load"))
		{
			return EXIT_FAILURE;
		}
		auto FailingModule = CompileSource(
		    "int broken = 1 / 0;\n"
		    "void main() { print(\"new\"); }\n");
		FPackageData FailingPackage;
		if (!Expect(
		        FailingModule && ReadBuiltPackage(*FailingModule, 2, FailingPackage),
		        "failing replacement package should build") ||
		    !Expect(
		        Session.ReloadPackage(FailingPackage, "Broken.vslib").bSuccess,
		        "valid replacement should stage") ||
		    !Expect(
		        !FailingPackage.Module,
		        "staged package module should transfer ownership") ||
		    !Expect(
		        !Session.Run().bSuccess,
		        "replacement initialization should fail") ||
		    !Expect(
		        Session.Run().bSuccess,
		        "old module should run after rollback") ||
		    !Expect(
		        Host.Output == "old\n",
		        "failed package generation must not run its main"))
		{
			std::cerr << Host.Errors << Host.Output;
			return EXIT_FAILURE;
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Expect(
		        Session.LoadSource(
		                   "int retained = 7;\n"
		                   "void main() { print(retained); }\n",
		                   "<package-test>")
		                .bSuccess &&
		            Session.Run().bSuccess,
		        "compatible persistent module should initialize"))
		{
			return EXIT_FAILURE;
		}
		auto Compatible = CompileSource(
		    "int retained = 99;\n"
		    "void main() { print(retained); }\n");
		FPackageData CompatiblePackage;
		if (!Expect(
		        Compatible && ReadBuiltPackage(*Compatible, 3, CompatiblePackage),
		        "compatible package should build") ||
		    !Expect(
		        Session.ReloadPackage(
		                   CompatiblePackage, "Compatible.vslib")
		            .bSuccess,
		        "compatible package migration should stage") ||
		    !Expect(
		        Session.Run().bSuccess,
		        "compatible package replacement should run") ||
		    !Expect(
		        Host.Output == "7\n7\n",
		        "package reload must retain global value and skip its replacement initializer"))
		{
			std::cerr << Host.Errors << Host.Output;
			return EXIT_FAILURE;
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Expect(
		        Session.LoadSource(
		                   "int retained = 7;\n"
		                   "void main() { print(retained); }\n",
		                   "<retained>")
		                .bSuccess &&
		            Session.Run().bSuccess,
		        "persistent old module should initialize"))
		{
			return EXIT_FAILURE;
		}
		auto Incompatible = CompileSource(
		    "void main() { print(9); }\n");
		FPackageData IncompatiblePackage;
		if (!Expect(
		        Incompatible && ReadBuiltPackage(*Incompatible, 3, IncompatiblePackage),
		        "incompatible package should build"))
		{
			return EXIT_FAILURE;
		}
		const FBytecodeModule* const OldModule = Session.GetLoadedModule();
		if (!Expect(
		        !Session.ReloadPackage(
		                    IncompatiblePackage, "Incompatible.vslib")
		            .bSuccess,
		        "incompatible migration must fail") ||
		    !Expect(
		        static_cast<bool>(IncompatiblePackage.Module),
		        "migration failure must preserve package ownership") ||
		    !Expect(
		        Session.GetLoadedModule() == OldModule,
		        "migration failure must preserve current module"))
		{
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

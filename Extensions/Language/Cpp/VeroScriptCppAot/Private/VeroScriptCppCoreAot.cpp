// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppCoreAot.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptPackage.h"
#include "VeroScriptModuleEnvironment.h"
#include <optional>
#include <sstream>

namespace VeroScript
{
	namespace
	{
		std::string Quote(std::string_view Text)
		{
			std::string Out = "\"";
			for (const unsigned char C : Text)
			{
				if (C == '\\' || C == '"')
				{
					Out += '\\';
					Out += static_cast<char>(C);
				}
				else if (C == '\n')
				{
					Out += "\\n";
				}
				else if (C == '\r')
				{
					Out += "\\r";
				}
				else if (C == '\t')
				{
					Out += "\\t";
				}
				else if (C < 32)
				{
					constexpr char Hex[] = "0123456789abcdef";
					Out += "\\u00";
					Out += Hex[C >> 4];
					Out += Hex[C & 15];
				}
				else
				{
					Out += static_cast<char>(C);
				}
			}
			return Out + '"';
		}
		bool ValidTarget(std::string_view Target)
		{
			return Target == "android-arm64" || Target == "android-x64" || Target == "ios-arm64" ||
			    Target == "ios-simulator-arm64" || Target == "ios-simulator-x64" || Target == "ios-catalyst-arm64" ||
			    Target == "ios-catalyst-x64" || Target == "win-x64" || Target == "win-arm64" ||
			    Target == "linux-x64" || Target == "linux-arm64" || Target == "macos-x64" || Target == "macos-arm64";
		}
	}

	bool GenerateCoreAotSourceBundle(const FBytecodeModule& InputModule, const FCoreAotOptions& Options,
	    FCoreAotSourceBundle& Out, std::string& Error)
	{
		Out = {};
		Error.clear();
		const auto IsStart = [](char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_';
		};
		if (!ValidTarget(Options.Target) || Options.FactoryName.empty() || !IsStart(Options.FactoryName.front()))
		{
			Error = "Core AOT requires an explicit supported 64-bit target and a factory identifier";
			return false;
		}
		for (const auto C : Options.FactoryName)
		{
			if (!IsStart(C) && (C < '0' || C > '9'))
			{
				Error = "invalid Core AOT factory identifier";
				return false;
			}
		}
		constexpr std::string_view Keywords = " alignas alignof and and_eq asm auto bitand bitor bool break case catch char "
		                                      "char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await "
		                                      "co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float "
		                                      "for friend goto if import inline int long module mutable namespace new noexcept not not_eq nullptr operator "
		                                      "or or_eq private protected public register reinterpret_cast requires return short signed sizeof static "
		                                      "static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename "
		                                      "union unsigned using virtual void volatile wchar_t while xor xor_eq std VeroScript ";
		if (Options.FactoryName.front() == '_' || Options.FactoryName.find("__") != std::string::npos ||
		    Keywords.find(" " + Options.FactoryName + " ") != std::string_view::npos)
		{
			Error = "reserved Core AOT factory identifier";
			return false;
		}
		if (!Options.ExecutionCheck.Poll() || !VeroScriptBytecodeIO::ValidateModule(InputModule, Error))
		{
			if (Error.empty())
			{
				Error = "Core AOT generation stopped by host";
			}
			return false;
		}
		FCoreAotSourceBundle Result;
		FPackageBuildOptions PackageOptions;
		PackageOptions.PackageName = "core-aot";
		FPackageData Package;
		if (!BuildPackage(InputModule, PackageOptions, Result.Application, Error) ||
		    !ReadPackage(FByteView(Result.Application), Package, Error))
		{
			return false;
		}
		const auto& Module = *Package.Module;
		if (!VeroScriptBytecodeIO::SerializeBytecode(Module, 0, Result.Bytecode, Error)) return false;
		std::string Digest;
		if (!ComputeAotProgramDigest(Module, Digest, Error, Options.ExecutionCheck)) return false;
		std::vector<FNativeFunction> NativeStorage;
		const auto* Natives = StandardLibrary::MergeNativeFunctions(Options.Natives, NativeStorage);
		std::vector<FNativeFunction> BoundNatives;
		if (!BindBytecodeNativeReferences(Module, *Natives, BoundNatives, Error)) return false;
		Natives = &BoundNatives;
		std::string BindingDigest;
		if (!ComputeAotNativeBindingsDigest(*Natives, BindingDigest, Error, Options.ExecutionCheck)) return false;
		std::ostringstream Source, Entries, InteropEntries, Manifest;
		Source << "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n"
		          "// Generated Core-linked AOT. Compile with the target Core SDK.\n"
		          "#include <VeroScriptAotProgram.h>\n#include <VeroScriptJitModule.h>\n#include <new>\n"
		          "static_assert(sizeof(void*) == 8, \"Core AOT target requires 64-bit pointers\");\n"
		          "namespace {\n";
		Manifest << "{\"version\":\"0.1.1\",\"profile\":\"core-linked-aot\",\"target\":" << Quote(Options.Target)
		         << ",\"programDigest\":" << Quote(Digest) << ",\"nativeBindingsDigest\":" << Quote(BindingDigest)
		         << ",\"moduleInitNative\":false,\"fieldInitializersNative\":false,\"functions\":[";
		bool First = true;
		for (std::size_t F = 0; F < Module.Functions.size(); ++F)
		{
			if (!Options.ExecutionCheck.Poll())
			{
				Error = "Core AOT generation stopped by host";
				return false;
			}
			const auto& Function = Module.Functions[F];
			std::vector<FCompiledInstruction> Plan;
			std::string Reason;
			const bool Native = BuildCompiledFunctionPlan(Function, Plan, Reason, Options.ExecutionCheck);
			if (!Options.ExecutionCheck.Poll())
			{
				Error = "Core AOT generation stopped by host";
				return false;
			}
			Result.Coverage.push_back({Function.Name, Reason, Native});
			if (!First)
			{
				Manifest << ',';
			}
			First = false;
			Manifest << "{\"index\":" << F << ",\"name\":" << Quote(Function.Name)
			         << ",\"native\":" << (Native ? "true" : "false") << ",\"reason\":" << Quote(Reason) << '}';
			if (!Native)
			{
				if (Options.bRequireAllNative && !Function.bIsAbstract && !Function.bDeleted)
				{
					Error = "Core AOT does not cover " + Function.Name + ": " + Reason;
					return false;
				}
				continue;
			}
			Source << "int Function" << F << "(void* Context) noexcept {\nauto* S = static_cast<VeroScript::ICompiledFunctionServices*>(Context);\nint R = -1;\n";
			// Do not turn dead bytecode into unreachable C++ warnings in the
			// embedding application's warnings-as-errors build.
			std::vector<bool> Reachable(Plan.size(), false);
			std::vector<std::size_t> Work{0};
			Reachable[0] = true;
			std::size_t Visited = 0;
			while (!Work.empty())
			{
				if ((Visited++ & 255u) == 0 && !Options.ExecutionCheck.Poll())
				{
					Error = "Core AOT generation stopped by host";
					return false;
				}
				const auto Index = Work.back();
				Work.pop_back();
				const auto& I = Plan[Index];
				const auto Add = [&](std::size_t Target)
				{
					if (Target < Plan.size() && !Reachable[Target])
					{
						Reachable[Target] = true;
						Work.push_back(Target);
					}
				};
				if (I.Opcode == EOpcode::Return)
				{
					continue;
				}
				if (I.Opcode == EOpcode::Jmp || I.Opcode == EOpcode::JmpIfFalse || I.Opcode == EOpcode::JmpIfLocalGeImm || I.Opcode == EOpcode::BeginLocalStatic)
				{
					Add(static_cast<std::size_t>(std::lower_bound(Plan.begin(), Plan.end(), I.Target,
					                                 [](const auto& Item, auto Offset)
					{
						return Item.Offset < Offset;
					}) - Plan.begin()));
				}
				if (I.Opcode != EOpcode::Jmp)
				{
					Add(Index + 1);
				}
			}
			std::vector<std::uint32_t> Targets;
			for (std::size_t Index = 0; Index < Plan.size(); ++Index)
			{
				if (!Reachable[Index])
				{
					continue;
				}
				const auto& I = Plan[Index];
				if (I.Opcode == EOpcode::Jmp || I.Opcode == EOpcode::JmpIfFalse || I.Opcode == EOpcode::JmpIfLocalGeImm || I.Opcode == EOpcode::BeginLocalStatic)
				{
					Targets.push_back(I.Target);
				}
			}
			std::sort(Targets.begin(), Targets.end());
			std::size_t Emitted = 0;
			EOpcode LastEmitted = EOpcode::Nop;
			for (std::size_t Index = 0; Index < Plan.size(); ++Index)
			{
				if (!Reachable[Index])
				{
					continue;
				}
				const auto& I = Plan[Index];
				LastEmitted = I.Opcode;
				if ((Emitted++ & 255u) == 0 && !Options.ExecutionCheck.Poll())
				{
					Error = "Core AOT generation stopped by host";
					return false;
				}
				if (std::binary_search(Targets.begin(), Targets.end(), I.Offset))
				{
					Source << "L" << I.Offset << ": ";
				}
				Source << "R = S->Execute(" << I.Offset << "); if (R < 0) return R;\n";
				if (I.Opcode == EOpcode::Return)
				{
					Source << "return R;\n";
				}
				else if (I.Opcode == EOpcode::Jmp || I.Opcode == EOpcode::JmpIfFalse || I.Opcode == EOpcode::JmpIfLocalGeImm || I.Opcode == EOpcode::BeginLocalStatic)
				{
					Source << "if (R != 0) goto L" << I.Target << ";\n";
				}
			}
			if (LastEmitted != EOpcode::Return)
			{
				Source << "return R;\n";
			}
			Source << "}\n";
			Entries << "{" << F << ", &Function" << F << ", VeroScript::ControlledExecutionCapabilities},\n";
			InteropEntries << "{" << F << ", 0, 0, &Function" << F << ", VeroScript::ControlledExecutionCapabilities},\n";
		}
		if (Options.bRequireAllNative && (!Module.InitCode.empty() || std::any_of(Module.Types.begin(), Module.Types.end(), [](const auto& T)
		{
			return std::any_of(T.Fields.begin(), T.Fields.end(), [](const auto& F)
			{
				return F.bHasInitCode;
			});
		})))
		{
			Error = "Core AOT full-native mode does not cover module/field initialization";
			return false;
		}
		Source << "}\n#ifndef VEROSCRIPT_AOT_WITH_INTEROP\nstd::shared_ptr<const VeroScript::IAotProgram> " << Options.FactoryName << "(std::string& Error) {\n"
		                                                                                                                              "VeroScript::FAotProgramDescription D;\nD.ProgramDigest = "
		       << Quote(Digest) << ";\nD.NativeBindingsDigest = "
		       << Quote(BindingDigest) << ";\nD.Target = " << Quote(Options.Target) << ";\n"
		                                                                               "D.ServiceAbi = VeroScript::BuildJitModuleAbi();\nD.CoreImplementation = VEROSCRIPT_CORE_IMPLEMENTATION_ID;\n"
		                                                                               "return VeroScript::CreateAotProgram(std::move(D), {\n"
		       << Entries.str() << "}, Error);\n}\n";
		Source << "#else\n#include <VeroScriptInterop.h>\nextern \"C\" vs_interop_status VS_INTEROP_CALL " << Options.FactoryName
		       << "Interop(vs_interop_aot_program** Out, vs_interop_buffer** Error) {\n"
		          "if (Out) *Out = nullptr;\nif (Error) *Error = nullptr;\ntry\n{\n"
		          "const auto Abi = VeroScript::BuildJitModuleAbi();\n"
		          "const auto View = [](std::string_view V) { return vs_interop_string_view{V.data(), V.size()}; };\n"
		          "const vs_interop_aot_description D{sizeof(D), VeroScript::VEROSCRIPT_VERSION_CODE, View("
		       << Quote(Digest) << "), View(" << Quote(BindingDigest) << "), View(Abi), View(VEROSCRIPT_CORE_IMPLEMENTATION_ID), View("
		       << Quote(Options.Target) << ")};\nconst std::vector<vs_interop_aot_function> F{\n"
		       << InteropEntries.str()
		       << "};\nreturn vs_interop_aot_program_create(&D, F.data(), F.size(), Out, Error);\n"
		          "}\ncatch (const std::bad_alloc&)\n{\n\treturn VS_INTEROP_STATUS_OUT_OF_MEMORY;\n}\n"
		          "catch (...)\n{\n\treturn VS_INTEROP_STATUS_INTERNAL_ERROR;\n}\n}\n#endif\n";

		Manifest << "]}";
		Result.Source = Source.str();
		Result.ManifestJson = Manifest.str();
		Out = std::move(Result);
		return true;
	}

	bool CompileSourceToCoreAot(std::string_view Source, std::string_view FileLabel,
	    const FCompileOptions& CompileOptions, const FCoreAotOptions& Options,
	    FCoreAotSourceBundle& Out, std::string& Error)
	{
		// The input views may refer to the bundle that this call replaces.
		const std::string OwnedSource(Source);
		const std::string OwnedFileLabel(FileLabel);
		Out = {};
		if (!ValidTarget(Options.Target))
		{
			Error = "unsupported explicit Core AOT target";
			return false;
		}
		if (!Options.ExecutionCheck.Poll())
		{
			Error = "Core AOT generation stopped by host";
			return false;
		}
		auto Effective = CompileOptions;
		std::vector<std::string> Defines;
		const bool Android = Options.Target.rfind("android-", 0) == 0;
		const bool Ios = Options.Target.rfind("ios-", 0) == 0;
		Defines.push_back(std::string("VEROSCRIPT_TARGET_ANDROID=") + (Android ? "1" : "0"));
		Defines.push_back(std::string("VEROSCRIPT_TARGET_IOS=") + (Ios ? "1" : "0"));
		Defines.push_back(std::string("VEROSCRIPT_TARGET_SIMULATOR=") + (Options.Target.rfind("ios-simulator-", 0) == 0 ? "1" : "0"));
		Defines.push_back(std::string("VEROSCRIPT_TARGET_ARM64=") + (Options.Target.find("arm64") != std::string::npos ? "1" : "0"));
		Defines.push_back("VEROSCRIPT_TARGET_POINTER_BITS=64");
		for (const auto& D : Defines)
		{
			Effective.Defines.push_back(D);
		}
		if (Options.Natives)
		{
			Effective.Natives = Options.Natives;
		}
		// Normalize the same environment/catalog used by ordinary compilation.
		// Keeping Bindings preserves host types and annotation definitions.
		if (Effective.ModuleEnvironment && !Effective.ModuleEnvironment->Apply(Effective, Error)) return false;
		const auto* ActualNatives = Effective.Bindings ? &Effective.Bindings->GetNativeFunctions() : Effective.Natives;
		if (Options.Natives && ActualNatives != Options.Natives)
		{
			std::string ActualDigest, RequestedDigest;
			if (!ComputeAotNativeBindingsDigest(*ActualNatives, ActualDigest, Error, Options.ExecutionCheck) ||
			    !ComputeAotNativeBindingsDigest(*Options.Natives, RequestedDigest, Error, Options.ExecutionCheck))
			{
				return false;
			}
			if (ActualDigest != RequestedDigest)
			{
				Error = "Core AOT native bindings conflict with the compile binding catalog";
				return false;
			}
		}
		FCompileResult Compiled;
		{
			// Parsing, imports, processors and semantic compilation use the existing
			// analysis checkpoints. Keep a caller's cancellation/profiling scope too.
			const auto* Parent = ActiveAnalysisControl;
			std::optional<FAnalysisControlScope> Scope;
			if (Options.ExecutionCheck.Check)
			{
				Scope.emplace([Check = Options.ExecutionCheck, Parent]
				{
					return !Check.Poll() || (Parent && Parent->ShouldCancel && Parent->ShouldCancel());
				}, Parent ? Parent->RecordStage : std::function<void(std::string_view, double)>{});
			}
			try
			{
				Compiled = VeroScriptCompile::CompileSource(OwnedSource, OwnedFileLabel, Effective);
			}
			catch (const FAnalysisCancelled&)
			{
				Error = "Core AOT compilation stopped by host";
				return false;
			}
		}
		if (!Compiled.bSuccess || !Compiled.Module)
		{
			Error = Compiled.Error.Message;
			return false;
		}
		auto FinalOptions = Options;
		FinalOptions.Natives = ActualNatives;
		return GenerateCoreAotSourceBundle(*Compiled.Module, FinalOptions, Out, Error);
	}
}

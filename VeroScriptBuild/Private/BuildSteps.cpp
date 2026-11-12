// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "BuildInternal.h"
#include "VeroScriptProcess.h"
#include "VeroScriptVm.h"
#include "VeroScriptAnalysisControl.h"
#include <sstream>

namespace VeroScript::Build::Detail
{
	void RunBuiltinStep(const std::string& Provider, const FStepContext& C, const FServices& Services)
	{
		const auto O = Parse(C.OptionsJson, C.Limits);
		if (Provider == "process")
		{
			if (!Services.AllowProcess)
			{
				Fail("process-permission", "process steps require explicit host permission");
			}
			Application::FProcessRequest P;
			P.Executable = NativePathText(Absolute(O.at("program"), PathUtil::FromUtf8(C.ProjectRoot)));
			P.WorkingDirectory = O.contains("cwd") ? NativePathText(Absolute(O.at("cwd"), PathUtil::FromUtf8(C.ProjectRoot))) : C.StagingRoot;
			P.CheckCancelled = C.CheckCancelled;
			P.TimeoutMilliseconds = C.Limits.StepTimeoutMilliseconds;
			P.MaxOutputBytes = C.Limits.MaxLogBytes;
			P.Output = [&](std::string_view Text, bool)
			{
				if (C.Log)
				{
					C.Log(std::string(Text));
				}
			};
			const auto A = O.value("arguments", J::array());
			if (!A.is_array())
			{
				Fail("process-arguments", "process arguments must be an array");
			}
			for (const auto& V : A)
			{
				if (V.is_string())
				{
					P.Arguments.push_back(V.get<std::string>());
				}
				else
				{
					Keys(V, {"input", "output"}, "argument");
					if (V.size() != 1)
					{
						Fail("process-argument", "argument must reference exactly one input or output");
					}
					const auto& Map = V.contains("input") ? C.Inputs : C.Outputs;
					const auto N = V.begin().value().get<std::string>();
					if (!Map.count(N))
					{
						Fail("process-argument", "unknown named step input or output", N);
					}
					P.Arguments.push_back(Map.at(N).Path);
				}
			}
			const auto E = O.value("environment", J::object());
			if (!E.is_object())
			{
				Fail("process-environment", "process environment must be an object");
			}
			for (auto I = E.begin(); I != E.end(); ++I)
			{
				P.Environment.emplace(I.key(), I.value().get<std::string>());
			}
			Application::FDiagnostic Error;
			int Code;
			Require(Application::RunProcess(P, Code, Error), Error);
			if (Code != 0)
			{
				Fail("process-exit", "build process returned exit code " + std::to_string(Code), P.Executable);
			}
			return;
		}
		if (!Services.AllowScript)
		{
			Fail("script-permission", "script steps require explicit host permission");
		}
		struct FState
		{
			const FStepContext* Context;
			std::string Operation;
		};
		std::vector<FState> States;
		States.reserve(3);
		std::vector<FNativeFunction> Natives;
		const auto Add = [&](std::string Name, ENativeType Type, unsigned Arity)
		{
			States.push_back({&C, Name});
			FNativeFunction F;
			F.Name = "build_" + Name;
			F.ReturnType = Type;
			F.ParamTypes.assign(Arity, ENativeType::String);
			F.UserData = &States.back();
			F.Callback = [](const FNativeCallContext& N, FValue& Out, std::string& Error)
			{
				try
				{
					const auto& S = *static_cast<const FState*>(N.UserData);
					const auto& Context = *S.Context;
					Context.CheckCancelled();
					const auto Name = std::get<std::string>(N.Args[0]);
					if (S.Operation == "message")
					{
						if (Name.size() > Context.Limits.MaxLogBytes)
						{
							Fail("log-budget", "build message exceeds log budget");
						}
						if (Context.Log)
						{
							Context.Log(Name);
						}
						return true;
					}
					const auto& Map = S.Operation == "read_text" ? Context.Inputs : Context.Outputs;
					if (!Map.count(Name))
					{
						Fail("script-input", "unknown named build input or output", Name);
					}
					const auto& Item = Map.at(Name);
					const auto Relative = std::get<std::string>(N.Args[1]);
					fs::path P = PathUtil::FromUtf8(Item.Path);
					if (Item.Type == "directory")
					{
						Safe(Relative);
						P /= PathUtil::FromUtf8(Relative);
					}
					else if (!Relative.empty())
					{
						Fail("script-path", "file input/output requires an empty member path");
					}
					if (S.Operation == "read_text")
					{
						Context.TrackInput(NativePathText(P));
						Out = Read(P, Context.Limits);
					}
					else
					{
						const auto& Text = std::get<std::string>(N.Args[2]);
						if (Text.size() > Context.Limits.Content.MaxFileBytes)
						{
							Fail("write-budget", "generated source exceeds byte budget");
						}
						Write(P, Text);
					}
					return true;
				}
				catch (const std::exception& E)
				{
					Error = E.what();
					return false;
				}
			};
			Natives.push_back(std::move(F));
		};
		Add("read_text", ENativeType::String, 2);
		Add("write_text", ENativeType::Void, 3);
		Add("message", ENativeType::Void, 1);
		FCompileOptions Compile;
		Compile.Natives = &Natives;
		Compile.CompileProcessing.CheckCancelled = C.CheckCancelled;
		Compile.AnnotationProcessing.CheckCancelled = C.CheckCancelled;
		const auto Entry = Absolute(O.at("entry"), PathUtil::FromUtf8(C.ProjectRoot));
		C.TrackInput(NativePathText(Entry));
		FModuleEnvironmentDesc Environment;
		Environment.EntryOwnerId = "build.script";
		Environment.Scopes.push_back({Environment.EntryOwnerId, {}, false});
		Environment.SourceRoots.push_back({NativePathText(Entry.parent_path()), Environment.EntryOwnerId});
		for (const auto& I : C.Inputs)
		{
			const auto Path = PathUtil::FromUtf8(I.second.Path);
			if (I.second.Type == "directory")
			{
				Environment.SourceRoots.push_back({NativePathText(Path), Environment.EntryOwnerId});
				Environment.ImportPaths.push_back(NativePathText(Path));
			}
		}
		for (const auto& Path : C.StandardLibraryPaths)
		{
			const auto Id = "build.stdlib." + Application::ContentSha256(Path);
			Environment.Scopes.push_back({Id, {}, true});
			Environment.SourceRoots.push_back({Path, Id});
			Environment.StdlibSearchPaths.push_back(Path);
		}
		std::string EnvironmentError;
		Compile.ModuleEnvironment = FModuleEnvironment::Create(std::move(Environment), EnvironmentError);
		if (!Compile.ModuleEnvironment)
		{
			Fail("script-environment", EnvironmentError);
		}
		FAnalysisControlScope Control([&]()
		{
			C.CheckCancelled();
			return false;
		});
		auto Compiled = VeroScriptCompile::CompileFile(NativePathText(Entry), Compile);
		if (!Compiled.bSuccess)
		{
			Fail("script-compile", Compiled.Error.Message, Compiled.Error.Location.FileLabel);
		}
		for (const auto& P : Compiled.LoadedModulePaths)
		{
			if (PathUtil::FromUtf8(P).is_absolute() && fs::exists(PathUtil::FromUtf8(P)))
			{
				C.TrackInput(P);
			}
		}
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		const auto& Execution = Services.Compile.ScriptCompileExtensions;
		if (!Execution.VmBackendFactory && Execution.VmBackendPath.empty())
		{
			Fail("script-vm", "script steps require an explicitly configured VM backend");
		}
		if (Execution.VmBackendFactory && !Execution.VmBackendPath.empty())
		{
			Fail("script-vm", "VM factory and path are exclusive");
		}
		if (!Execution.VmBackendPath.empty())
		{
			C.TrackInput(Execution.VmBackendPath);
		}
		if (!(Execution.VmBackendFactory
		            ? Vm.SetVmBackendFactory(Execution.VmBackendFactory, EnvironmentError)
		            : Vm.SetVmBackendPath(Execution.VmBackendPath, EnvironmentError)))
		{
			Fail("script-vm", EnvironmentError);
		}
		if (!Vm.SetModuleEnvironment(Compile.ModuleEnvironment, EnvironmentError))
		{
			Fail("script-environment", EnvironmentError);
		}
		FScriptCapabilities Capabilities;
		Capabilities.bAllowFileRead = Capabilities.bAllowFileWrite = Capabilities.bAllowFileDelete = false;
		Capabilities.bAllowEnvironmentRead = Capabilities.bAllowEnvironmentWrite = false;
		Capabilities.bAllowConsoleInput = Capabilities.bAllowConsoleOutput = false;
		Capabilities.bAllowTaskSpawn = Capabilities.bAllowProcessDirectoryChange = false;
		Capabilities.bAllowMemoryAddress = Capabilities.bAllowNondeterminism = false;
		Vm.SetCapabilities(Capabilities);
		Vm.SetExecutionBackend(EExecutionBackend::Vm);
		Vm.SetExecutionLimits({C.Limits.ScriptInstructions, C.Limits.StepTimeoutMilliseconds});
		Vm.SetExecutionCheck([&]()
		{
			C.CheckCancelled();
			return true;
		});
		Vm.SetMaxCallDepth(256);
		Vm.SetMemoryContext(std::make_shared<FMemoryContext>(FMemoryLimits{64ull << 20, 64ull << 20}));
		FNumericLimits Numbers;
		Numbers.MaxValueBits = 4096;
		Vm.SetNumericLimits(Numbers);
		if (!Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Natives))
		{
			Fail("script-execution", Vm.GetError().Message);
		}
		if (Vm.GetProcessExitCode() != 0)
		{
			Fail("script-exit", "build script returned a nonzero exit code");
		}
	}
}

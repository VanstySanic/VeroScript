// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAst.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptJitBuild.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptModuleEnvironment.h"
#include "ModuleSupport/VeroScriptModuleSupport.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPackagePath.h"
#include "VeroScriptPath.h"
#include "VeroScriptPlatform.h"
#include "VeroScriptProcessingSession.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptScriptExtensionsInternal.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace VeroScript::ScriptExtensions
{
	std::string Text(const FValue& V)
	{
		auto P = std::get_if<std::string>(&V);
		if (!P)
		{
			throw std::runtime_error("expected string compilation value");
		}

		return *P;
	}
	std::int64_t Integer(const FValue& V)
	{
		if (auto P = std::get_if<FEnumValue>(&V))
		{
			return P->Value;
		}

		if (auto P = std::get_if<std::int32_t>(&V))
		{
			return *P;
		}

		if (auto P = std::get_if<std::int64_t>(&V))
		{
			return *P;
		}

		if (auto P = std::get_if<std::uint64_t>(&V))
		{
			if (*P > INT64_MAX)
			{
				throw std::runtime_error("compilation integer out of range");
			}

			return static_cast<std::int64_t>(*P);
		}
		throw std::runtime_error("expected integer compilation value");
	}
	const std::vector<FValue>& Elements(const FValue& V)
	{
		auto P = std::get_if<FDynamicArrayHandle>(&V);
		if (!P || !*P)
		{
			throw std::runtime_error("expected compilation array");
		}

		return (*P)->Elements;
	}
	FDynamicArrayHandle Array(std::string Element, std::vector<FValue> Values)
	{
		auto A = std::make_shared<FDynamicArray>();
		A->ElementTypeName = std::move(Element);
		A->Elements = std::move(Values);
		return A;
	}
	FValue Get(const FBridgeValue& V, std::string_view Name)
	{
		auto I = V.Fields.find(std::string(Name));
		if (I == V.Fields.end())
		{
			throw std::runtime_error("unknown compilation field: " + std::string(Name));
		}

		return I->second;
	}
	FTask::~FTask()
	{
		try
		{
			std::vector<FScriptError> D;
			Close(D);
		}
		catch (...)
		{
		}
	}
	bool FTask::Close(std::vector<FScriptError>& D)
	{
		if (Closed)
		{
			return true;
		}

		Closed = true;
		++Epoch;
		Context = nullptr;
		Output = nullptr;
		Registering = false;
		bool Ok = true;
		for (auto I = Modules.rbegin(); I != Modules.rend(); ++I)
		{
			auto& M = **I;
			if (!M.Vm)
			{
				continue;
			}

			M.Vm->SetExecutionLimits({10000, 100});
			M.Vm->SetExecutionDeadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
			M.Vm->SetExecutionCheck({});
			for (auto& P : Processors)
			{
				if (P.Module.get() == &M)
				{
					P.Factory.reset();
					P.Callback.reset();
				}
			}

			M.Registration.reset();
			const auto RecordCleanup = [&](const bool Released)
			{
				if (!Released)
				{
					FScriptError E;
					E.Location = M.Vm->GetError().Location;
					E.Message = "compiler extension cleanup failed: " + M.Vm->GetError().Message;
					D.push_back(std::move(E));
					Ok = false;
				}
			};
			RecordCleanup(M.Vm->DiscardRetainedCallables(*M.Code, M.Callables, &Bindings.GetNativeFunctions()));
			RecordCleanup(M.Vm->DiscardPersistentState(*M.Code, M.State, &Bindings.GetNativeFunctions()));
		}
		Executing.reset();
		Processing.Registry.reset();
		Processing.PrepareSource = {};
		return Ok;
	}
	bool FTask::MatchesPreparedProgram(const FPreparedProgram& Prepared, const IHostBindingCatalog* BusinessBindings)
	{
		Check();
		if (!Frozen || Executing || Context || !Cacheable || !Prepared.IsCacheable())
		{
			return false;
		}

		const auto BindingsToMatch = BusinessBindings ? BusinessBindings : Inputs.Bindings;
		const auto Natives = BindingsToMatch ? &BindingsToMatch->GetNativeFunctions() : Inputs.Natives;
		if (!Prepared.MatchesHost(Natives, BindingsToMatch) ||
			!Prepared.MatchesProcessing(Processing, BindingsToMatch))
		{
			return false;
		}

		auto Validation = Processing;
		Validation.ReadDependency = [this](std::string_view Path, std::string& Content)
		{
			auto Code = Contents.find(PathUtil::IdentityKey(std::string(Path)));
			if (Code != Contents.end())
			{
				Content = Code->second;
				return true;
			}
			return ReadDependency(Path, Content);
		};
		return Prepared.MatchesDependencies(Validation);
	}
	void FTask::Check() const
	{
		CheckAnalysisCancellation();
		if (Closed || std::this_thread::get_id() != Thread)
		{
			throw std::runtime_error("compilation service is expired or used from another thread");
		}

		if (Inputs.CompileProcessing.CheckCancelled)
		{
			Inputs.CompileProcessing.CheckCancelled();
		}

		if (Inputs.ScriptCompileExtensions.ExecutionControl &&
			Inputs.ScriptCompileExtensions.ExecutionControl->IsCancellationRequested())
		{
			throw std::runtime_error("compilation extension cancelled");
		}

		if (std::chrono::steady_clock::now() >= Deadline)
		{
			throw std::runtime_error("compilation extension deadline exceeded");
		}
	}
	FValue FTask::Invoke(FModule& M, const FFunctionHandle& Fn, const std::vector<FValue>& Args, std::string_view Entry)
	{
		Check();
		const auto Limit = Inputs.ScriptCompileExtensions.MaxInstructions;
		if (Limit && Instructions >= Limit)
		{
			throw std::runtime_error("compiler extension instruction budget exhausted");
		}

		M.Vm->SetExecutionLimits({Limit ? Limit - Instructions : 0, 0});
		FValue Result;
		const bool Ok =
			Fn ? M.Vm->ExecuteCallable(*M.Code, Fn, Args, &M.State, &Bindings.GetNativeFunctions(), &Result)
			   : M.Vm->ExecuteFreeFunction(*M.Code, Entry, Args, &M.State, &Bindings.GetNativeFunctions(), &Result);
		const auto Count = M.Vm->GetExecutedInstructionCount();
		if (Count > UINT64_MAX - Instructions)
		{
			throw std::runtime_error("compiler instruction accounting overflow");
		}

		Instructions += Count;
		if (ExecutionFailure)
		{
			std::rethrow_exception(ExecutionFailure);
		}

		Check();
		if (!Ok)
		{
			throw std::runtime_error("compiler extension " + M.Path + ": " + M.Vm->GetError().Message);
		}

		return Result;
	}
	FHostObjectHandle FTask::Make(std::string Type, std::map<std::string, FValue> Fields, bool Service)
	{
		Check();
		const auto& Limits = Inputs.ScriptCompileExtensions;
		std::size_t Bytes = Type.size();
		for (const auto& F : Fields)
		{
			Bytes += F.first.size() + sizeof(FValue);
			if (auto S = std::get_if<std::string>(&F.second))
			{
				Bytes += S->size();
			}
		}
		if (++BridgeValues > Limits.MaxBridgeValues ||
			Bytes > Limits.MaxBridgeBytes - std::min(BridgeBytes, Limits.MaxBridgeBytes))
		{
			throw std::runtime_error("compilation bridge value budget exceeded");
		}

		BridgeBytes += Bytes;
		auto Desc = Bindings.FindType("std.compiler." + Type);
		if (!Desc)
		{
			throw std::runtime_error("unknown compilation service type: " + Type);
		}

		auto* P = new FBridgeValue{std::move(Type), std::move(Fields),
			Service ? weak_from_this() : std::weak_ptr<FTask>{}, Service ? Epoch : 0};
		return MakeHostObject(Desc->Id.Value, Desc->Name, P,
			[](void* V)
			{
				delete static_cast<FBridgeValue*>(V);
			});
	}
	FBridgeValue& FTask::Value(const FValue& V, std::string_view Type) const
	{
		auto P = std::get_if<FHostObjectHandle>(&V);
		if (!P || !*P || !(*P)->Payload || (*P)->TypeName.rfind("std.compiler.", 0) != 0)
		{
			throw std::runtime_error("invalid compilation service value");
		}

		auto& B = *static_cast<FBridgeValue*>((*P)->Payload);
		if (!Type.empty() && B.Type != Type)
		{
			throw std::runtime_error("wrong compilation service value kind");
		}

		return B;
	}
	void FTask::Validate(const FBridgeValue& V, std::string_view Type) const
	{
		Check();
		if (V.Type != Type || V.Task.lock().get() != this || V.Epoch != Epoch || !Executing)
		{
			throw std::runtime_error("expired or foreign compilation service handle");
		}
	}
	namespace
	{
		bool Ident(char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_';
		}
		void Skip(std::string_view S, std::size_t& I)
		{
			for (;;)
			{
				while (I < S.size() && (S[I] == ' ' || S[I] == '\t' || S[I] == '\n' || S[I] == '\r'))
				{
					++I;
				}

				if (S.substr(I, 2) == "//")
				{
					while (I < S.size() && S[I] != '\n')
					{
						++I;
					}

					continue;
				}
				if (S.substr(I, 2) == "/*")
				{
					auto E = S.find("*/", I + 2);
					if (E == S.npos)
					{
						throw std::runtime_error("unterminated compiler import comment");
					}

					I = E + 2;
					continue;
				}
				break;
			}
		}
		bool Word(std::string_view S, std::size_t& I, std::string_view W)
		{
			if (S.substr(I, W.size()) != W || (I + W.size() < S.size() && Ident(S[I + W.size()])))
			{
				return false;
			}

			I += W.size();
			return true;
		}
	} // namespace
	FValue FTask::Read(std::string_view Path)
	{
		Check();
		if (!Executing)
		{
			throw std::runtime_error("dependency read outside extension execution");
		}

		if (IsResourceDependencyId(std::string(Path)))
		{
			FResourceRequest R;
			std::string From;
			if (!ParseResourceDependencyId(std::string(Path), R, From) ||
				From != (Context ? Context->ResourceRequester : Executing->Path))
			{
				throw std::runtime_error("resource dependency has a different requesting module");
			}

			return ReadResource(R);
		}
		auto P = PathUtil::FromUtf8(Path);
		if (P.is_relative())
		{
			P = PathUtil::FromUtf8(Executing->Path).parent_path() / P;
		}

		auto Name = PathUtil::Normalize(PathUtil::ToUtf8(P));
		// Check before the context ledger can return an already recorded dependency.
		if (!IsPathAllowed(Name))
		{
			throw std::runtime_error("compilation dependency path is outside allowed roots");
		}

		std::string Content;
		const bool Exists = Context ? Context->ReadDependency(Name, Content) : ReadDependency(Name, Content);
		return Make("dependency", {{"path", Name}, {"exists", Exists}, {"content", Content}});
	}
	bool HasCompilerImport(std::string_view Source)
	try
	{
		std::size_t I = Source.substr(0, 3) == "\xef\xbb\xbf" ? 3 : 0;
		Skip(Source, I);
		if (!Word(Source, I, "import"))
		{
			return false;
		}

		Skip(Source, I);
		if (!Word(Source, I, "compiler"))
		{
			return false;
		}

		Skip(Source, I);
		return I < Source.size() && Source[I] == '"';
	}
	catch (const std::exception&)
	{
		// A prefix probe must not replace ordinary lexer diagnostics for malformed
		// source (for example an unterminated leading block comment).
		return false;
	}
	FValue FTask::ReadResource(const FResourceRequest& R)
	{
		Check();
		if (!Executing || !Inputs.ModuleEnvironment)
		{
			throw std::runtime_error("resource read outside an authorized extension environment");
		}

		const auto From = Context ? Context->ResourceRequester : Executing->Path;
		FResourceData Data;
		std::string Failure;
		if (Context)
		{
			if (!Context->ReadResource)
			{
				throw std::runtime_error("resource service is unavailable");
			}

			Context->ReadResource(R, Data);
		}
		else
		{
			if (!Inputs.ModuleEnvironment->ReadResource(R, From,
					{[this]
						{
							Check();
						},
						Inputs.CompileProcessing.MaxInputBytes},
					Data, Failure))
			{
				throw std::runtime_error(Failure);
			}

			std::string Recorded;
			const bool Exists = ReadDependency(MakeResourceDependencyId(R, From), Recorded);
			if (Exists != Data.bExists || Recorded != ResourceDependencyContent(Data))
			{
				throw std::runtime_error("resource changed during extension registration");
			}
		}
		std::string Content;
		if (!Data.Bytes.empty())
		{
			Content.assign(reinterpret_cast<const char*>(Data.Bytes.data()), Data.Bytes.size());
		}

		return Make("dependency",
			{{"path", MakeResourceDependencyId(R, From)}, {"exists", Data.bExists},
				{"content", std::move(Content)}});
	}
	bool FTask::ReadDependency(std::string_view Path, std::string& Content)
	{
		Check();
		const bool Resource = IsResourceDependencyId(std::string(Path));
		auto Name = Resource ? std::string(Path) : PathUtil::Normalize(std::string(Path));
		auto Identity = Resource ? Name : PathUtil::IdentityKey(Name);
		if (!Resource && Executing && Inputs.ModuleEnvironment)
		{
			std::string Denial;
			if (!Inputs.ModuleEnvironment->AuthorizeSource(Name, Executing->Path, Denial))
			{
				throw std::runtime_error(Denial);
			}
		}
		if (!Resource && !IsPathAllowed(Name))
		{
			throw std::runtime_error("compilation dependency path is outside allowed roots");
		}

		auto It = Dependencies.find(Identity);
		if (It == Dependencies.end())
		{
			if (Dependencies.size() >= Inputs.CompileProcessing.MaxDependencies)
			{
				throw std::runtime_error("compilation dependency count exceeded");
			}

			FAnnotationDependency D;
			D.Path = Name;
			if (Inputs.CompileProcessing.ReadDependency)
			{
				D.bExists = Inputs.CompileProcessing.ReadDependency(Name, D.Content);
			}

			else
			{
				D.bExists = ReadSource(Name, D.Content);
			}

			if (D.Content.size() > Inputs.CompileProcessing.MaxInputBytes -
					std::min(DependencyBytes, Inputs.CompileProcessing.MaxInputBytes))
			{
				throw std::runtime_error("compilation dependency byte budget exceeded");
			}

			DependencyBytes += D.Content.size();
			It = Dependencies.emplace(Identity, std::move(D)).first;
		}
		Content = It->second.Content;
		return It->second.bExists;
	}
	void FTask::Load(const std::string& Path, const std::string& Entry, std::size_t Depth)
	{
		Check();
		std::string RequestedEntry = Entry;
		while (RequestedEntry.rfind("::", 0) == 0)
		{
			RequestedEntry.erase(0, 2);
		}

		for (std::size_t P = 0; (P = RequestedEntry.find("::", P)) != std::string::npos;)
		{
			RequestedEntry.replace(P, 2, ".");
		}

		auto Identity = PathUtil::IdentityKey(Path);
		if (Visiting.count(Identity))
		{
			throw std::runtime_error("cyclic compiler extension dependency: " + Path);
		}

		if (Loaded.count(Identity))
		{
			for (const auto& M : Modules)
			{
				if (PathUtil::IdentityKey(M->Path) == Identity && !RequestedEntry.empty() &&
					M->Entry != RequestedEntry)
				{
					throw std::runtime_error("compiler extension has conflicting registration entries");
				}
			}

			return;
		}
		if (Modules.size() + Visiting.size() >= Inputs.ScriptCompileExtensions.MaxRoots ||
			Depth >= Inputs.ScriptCompileExtensions.MaxDependencyDepth)
		{
			throw std::runtime_error("compiler extension graph budget exceeded");
		}

		Visiting.insert(Identity);
		std::string Source;
		if (!ReadSource(Path, Source))
		{
			throw std::runtime_error("failed to read compiler extension: " + Path);
		}

		RecordContent(Path, Source);
		std::shared_ptr<FModule> M = std::make_shared<FModule>();
		M->Path = Path;
		M->Identity = Identity;
		M->Entry = RequestedEntry.empty() ? "register_extension" : RequestedEntry;
		if (Detail::IsLibraryPackagePath(Path))
		{
			FPackageData Package;
			std::string Error;
			if (!ReadPackage(FByteView(reinterpret_cast<const std::uint8_t*>(Source.data()), Source.size()),
					Package, Error))
			{
				throw std::runtime_error("compiler extension package: " + Error);
			}

			if (!Package.CompileExtension)
			{
				throw std::runtime_error("library package is missing a compilation extension manifest");
			}

			std::string HostFingerprint;
			if (Inputs.ScriptCompileExtensions.Bindings)
			{
				std::string Catalog;
				if (!ExportBindingCatalogJson(*Inputs.ScriptCompileExtensions.Bindings, Catalog))
				{
					throw std::runtime_error("cannot fingerprint compilation host services");
				}

				HostFingerprint = std::to_string(Runtime::Fnv1a64(14695981039346656037ull, Catalog));
			}
			if (Package.CompileExtension->HostBindingFingerprint != HostFingerprint)
			{
				throw std::runtime_error("compilation extension package host service contract mismatch");
			}

			if (!RequestedEntry.empty() && RequestedEntry != Package.CompileExtension->RegistrationEntry)
			{
				throw std::runtime_error("registration entry disagrees with compilation extension manifest");
			}

			M->Entry = Package.CompileExtension->RegistrationEntry;
			M->Identity = Package.CompileExtension->ModuleId;
			for (const auto& Existing : Modules)
			{
				if (Existing->Identity == M->Identity)
				{
					auto Original = Contents.find(PathUtil::IdentityKey(Existing->Path));
					if (Original == Contents.end() || Original->second != Source ||
						Existing->Entry != M->Entry)
					{
						throw std::runtime_error(
							"different compiler extension packages share one module identity");
					}

					Loaded.insert(Identity);
					Visiting.erase(Identity);
					return;
				}
			}

			for (const auto& D : Package.CompileExtension->Dependencies)
			{
				Load(Resolve(D.Path, Path), D.RegistrationEntry, Depth + 1);
			}

			M->Code = std::move(Package.Module);
		}
		else
		{
			Mask(Path, Source, true);
			auto Boot = Inputs;
			Boot.AnnotationProcessing = {};
			Boot.CompileProcessing = {};
			Boot.CompileProcessing.ModuleEnvironment = Inputs.ModuleEnvironment;
			if (Inputs.ModuleEnvironment)
			{
				Boot.CompileProcessing.ImplementationFingerprint =
					Inputs.ModuleEnvironment->GetCompileFingerprint();
			}

			Boot.ScriptCompileExtensions = {};
			Boot.ScriptCompileExtensions.bAllowSourceImports = false;
			Boot.Bindings = &Bindings;
			Boot.Natives = nullptr;
			Boot.ImplicitImportPaths.clear();
			Boot.SourceOverlays.erase(std::remove_if(Boot.SourceOverlays.begin(), Boot.SourceOverlays.end(),
										  [&](const auto& O)
										  {
											  return PathUtil::IdentityKey(O.Path) == Identity;
										  }),
				Boot.SourceOverlays.end());
			Boot.SourceOverlays.push_back({Path, Source});
			Boot.CompileProcessing.PrepareSource = [W = weak_from_this()](std::string_view P, std::string& S)
			{
				auto T = W.lock();
				if (!T)
				{
					throw std::runtime_error("expired bootstrap");
				}

				if (!T->Contents.count(PathUtil::IdentityKey(std::string(P))))
				{
					T->RecordContent(std::string(P), S);
				}

				return T->Mask(P, S);
			};
			Boot.CompileProcessing.MaxInputBytes = Inputs.CompileProcessing.MaxInputBytes -
												   std::min(ContentBytes, Inputs.CompileProcessing.MaxInputBytes) +
												   Source.size();
			const auto ParentControl = ActiveAnalysisControl;
			FAnalysisControlScope Analysis(
				[this, ParentControl]
				{
					if (Inputs.CompileProcessing.CheckCancelled)
					{
						Inputs.CompileProcessing.CheckCancelled();
					}

					if (Inputs.ScriptCompileExtensions.ExecutionControl &&
						Inputs.ScriptCompileExtensions.ExecutionControl->IsCancellationRequested())
					{
						throw std::runtime_error("compilation extension cancelled");
					}

					if (std::chrono::steady_clock::now() >= Deadline)
					{
						throw std::runtime_error("compilation extension deadline exceeded");
					}

					return ParentControl && ParentControl->ShouldCancel && ParentControl->ShouldCancel();
				},
				ParentControl ? ParentControl->RecordStage : std::function<void(std::string_view, double)>{});
			auto C = CompileBootstrap(*this, Path, Source, Boot);
			for (const auto& Import : C.Prepared->GetProgram().Imports)
			{
				if (Detail::IsLibraryPackagePath(Import.ResolvedPath))
				{
					std::string Bytes;
					if (!ReadSource(Import.ResolvedPath, Bytes))
					{
						throw std::runtime_error("compiler helper package disappeared");
					}

					RecordContent(Import.ResolvedPath, Bytes);
				}
			}

			M->Code = std::move(C.Code);
			std::size_t Matches = 0;
			for (const auto& F : C.Prepared->GetDeclarations())
			{
				if (F.Name == M->Entry && F.ModulePath == Path && F.Target == EAnnotationTarget::Function)
				{
					if (!F.bExported || !F.TypeParameters.empty() || F.TypeName != "void" ||
						F.Parameters.size() != 1 || F.Parameters[0].TypeName != "std.compiler.registration" ||
						F.Parameters[0].bIsRef)
					{
						throw std::runtime_error(
							"compiler registration entry must be exported void(registration)");
					}

					++Matches;
				}
			}

			if (Matches != 1)
			{
				throw std::runtime_error("compiler registration entry missing or ambiguous: " + Entry);
			}
		}
		static std::atomic<std::uint64_t> NextGeneration{1};
		M->Generation = NextGeneration.fetch_add(1, std::memory_order_relaxed);
		if (!M->Generation)
		{
			throw std::runtime_error("compilation task generation exhausted");
		}

		M->Vm = std::make_unique<VeroScriptVm>(std::cerr);
		{
			std::string Failure;
			const auto& VmOptions = Inputs.ScriptCompileExtensions;
			if ((VmOptions.VmBackendFactory && !VmOptions.VmBackendPath.empty())
				|| !(VmOptions.VmBackendFactory
					? M->Vm->SetVmBackendFactory(VmOptions.VmBackendFactory, Failure)
					: M->Vm->SetVmBackendPath(VmOptions.VmBackendPath, Failure)))
			{
				throw std::runtime_error(Failure.empty() ? "compiler VM factory and path are exclusive" : Failure);
			}
			if (!M->Vm->SetModuleEnvironment(Inputs.ModuleEnvironment, Failure, false))
			{
				throw std::runtime_error(Failure);
			}
		}
		M->Vm->SetExecutionBackend(EExecutionBackend::Vm);
		M->Vm->SetProgramOwner(M->Code);
		M->Vm->SetProgramGeneration(M->Generation);
		for (std::size_t I = 0; I < M->Code->Functions.size(); ++I)
		{
			const auto& F = M->Code->Functions[I];
			if (F.ModulePath != M->Code->EntryModulePath)
			{
				continue;
			}

			const auto Begin = FindLinkageIdentityStart(F.Name), End = FindFunctionSignatureMarker(F.Name);
			const auto N =
				std::string_view(F.Name).substr(Begin, (End == std::string_view::npos ? F.Name.size() : End) - Begin);
			if (N != M->Entry)
			{
				continue;
			}

			if (F.bIsMethod || F.bDeleted || F.ClosureTypeName != "fn(std.compiler.registration)->void" ||
				BytecodeFunctionHasRefParams(F))
			{
				throw std::runtime_error("invalid compilation extension registration signature");
			}

			if (M->Registration)
			{
				throw std::runtime_error("ambiguous compilation extension registration entry");
			}

			M->Registration = std::make_shared<FFunctionValue>();
			M->Registration->FunctionIndex = static_cast<std::uint16_t>(I);
			M->Registration->ProgramGeneration = M->Generation;
			M->Registration->FunctionName = F.Name;
			M->Registration->TypeName = F.ClosureTypeName;
			M->Registration->ProgramOwner = M->Code;
		}
		if (!M->Registration)
		{
			throw std::runtime_error("compilation extension registration bytecode is missing");
		}

		M->Vm->SetExecutionControl(Inputs.ScriptCompileExtensions.ExecutionControl);
		M->Vm->SetExecutionDeadline(Deadline);
		M->Vm->SetExecutionCheck(
			[W = weak_from_this()]
			{
				auto T = W.lock();
				if (!T)
				{
					return false;
				}

				try
				{
					T->Check();
					return true;
				}
				catch (...)
				{
					T->ExecutionFailure = std::current_exception();
					return false;
				}
			});
		FScriptCapabilities Caps;
		Caps.bAllowFileRead = Caps.bAllowFileWrite = Caps.bAllowFileDelete = false;
		Caps.bAllowEnvironmentRead = Caps.bAllowEnvironmentWrite = false;
		Caps.bAllowConsoleInput = Caps.bAllowConsoleOutput = false;
		Caps.bAllowTaskSpawn = Caps.bAllowProcessDirectoryChange = Caps.bAllowMemoryAddress =
			Caps.bAllowNondeterminism = false;
		M->Vm->SetCapabilities(Caps);
		Modules.push_back(std::move(M));
		Loaded.insert(Identity);
		Visiting.erase(Identity);
	}
	std::vector<FSourceRange> FTask::Mask(std::string_view Module, std::string& Source, bool Discover)
	{
		Check();
		std::vector<FSourceRange> Ranges;
		std::size_t I = Source.substr(0, 3) == "\xef\xbb\xbf" ? 3 : 0;
		for (;;)
		{
			Skip(Source, I);
			const auto Begin = I;
			if (!Word(Source, I, "import"))
			{
				break;
			}

			Skip(Source, I);
			if (!Word(Source, I, "compiler"))
			{
				break;
			}

			Skip(Source, I);
			if (I == Source.size() || Source[I] != '"')
			{
				break;
			}

			++I;
			std::string Path;
			bool End = false;
			while (I < Source.size())
			{
				char C = Source[I++];
				if (C == '"')
				{
					End = true;
					break;
				}
				if (C == '\n' || C == '\r')
				{
					throw std::runtime_error("compiler import path must be a string literal");
				}

				if (C == '\\')
				{
					if (I == Source.size())
					{
						break;
					}

					C = Source[I++];
					if (C != '\\' && C != '"')
					{
						throw std::runtime_error("unsupported escape in compiler import path");
					}
				}
				Path += C;
			}
			Skip(Source, I);
			if (!End || I == Source.size() || Source[I++] != ';' || Path.empty() ||
				Path.find('\0') != std::string::npos)
			{
				throw std::runtime_error("invalid compiler import directive");
			}

			if (!Inputs.ScriptCompileExtensions.bAllowSourceImports)
			{
				throw std::runtime_error("source compiler imports are disabled");
			}

			auto Resolved = Resolve(Path, Module);
			auto Id = PathUtil::IdentityKey(Resolved);
			if (Discover)
			{
				ExtensionDependencies[std::string(Module)].push_back({Path, {}});
				Load(Resolved, "", Visiting.size());
			}
			else if (!Loaded.count(Id) && !Visiting.count(Id))
			{
				throw std::runtime_error(
					"new compiler extension must be declared in the entry or project configuration");
			}

			FSourceRange R;
			R.SourceId = std::string(Module);
			R.Begin = Begin;
			R.End = I;
			Ranges.push_back(std::move(R));
			for (auto J = Begin; J < I; ++J)
			{
				if (Source[J] != '\n' && Source[J] != '\r')
				{
					Source[J] = ' ';
				}
			}
		}
		return Ranges;
	}
	bool FTask::IsPathAllowed(std::string_view Path) const
	{
		if (Inputs.ScriptCompileExtensions.AllowedRoots.empty()) return true;
		const auto Key = [](const std::string& Value)
		{
			// Resolve existing parents as well as files for unsaved overlays and
			// missing dependencies under short paths or directory aliases.
			const auto Physical = ModuleSupport::Physical(Value);
			return PathUtil::IdentityKey(Physical.empty() ? Value : Physical);
		};
		const auto Identity = Key(std::string(Path));
		for (const auto& Root : Inputs.ScriptCompileExtensions.AllowedRoots)
		{
			const auto R = Key(Root);
			if (!R.empty() &&
				(Identity == R ||
					(Identity.size() > R.size() && Identity.compare(0, R.size(), R) == 0 &&
						(R.back() == '/' || Identity[R.size()] == '/'))))
			{
				return true;
			}
		}
		return false;
	}
	std::string FTask::Resolve(std::string_view Path, std::string_view Importer, bool EnforceAllowedRoots) const
	{
		VeroScriptModuleLoader Resolver(Importer, Inputs.ModuleEnvironment);
		for (const auto& P : Inputs.ImportPaths)
		{
			Resolver.AddImportPath(std::string(P));
		}

		for (const auto& P : Inputs.StdlibSearchPaths)
		{
			Resolver.AddStdlibSearchRoot(std::string(P));
		}

		for (const auto& O : Inputs.SourceOverlays)
		{
			Resolver.SetSourceOverlay(O.Path, O.Source);
		}

		std::string Result;
		auto Dir = PathUtil::ToUtf8(PathUtil::FromUtf8(Importer).parent_path());
		// Bare entry filenames use the working directory, as ordinary imports do.
		if (Dir.empty())
		{
			Dir = ".";
		}

		if (!Resolver.ResolveImportPath(std::string(Path), Dir, Result, Importer))
		{
			throw std::runtime_error("compilation extension not found: " + std::string(Path));
		}

		if (EnforceAllowedRoots && !IsPathAllowed(Result))
		{
			throw std::runtime_error("compilation extension path is outside allowed roots");
		}

		return Result;
	}
	bool FTask::ReadSource(std::string_view Path, std::string& Source) const
	{
		std::string Denial;
		if (Inputs.ModuleEnvironment &&
			!Inputs.ModuleEnvironment->AuthorizeSource(std::string(Path), std::string(Path), Denial))
		{
			throw std::runtime_error(Denial);
		}

		for (auto I = Inputs.SourceOverlays.rbegin(); I != Inputs.SourceOverlays.rend(); ++I)
		{
			if (PathUtil::IdentityKey(I->Path) == PathUtil::IdentityKey(std::string(Path)))
			{
				if (I->Source.size() > Inputs.CompileProcessing.MaxInputBytes)
				{
					throw std::runtime_error("compilation extension source budget exceeded");
				}

				Source = I->Source;
				return true;
			}
		}

		std::ifstream In(PathUtil::FromUtf8(Path), std::ios::binary);
		if (!In)
		{
			std::error_code Error;
			if (!std::filesystem::exists(PathUtil::FromUtf8(Path), Error) && !Error)
			{
				return false;
			}

			throw std::runtime_error("cannot read compilation dependency: " + std::string(Path));
		}
		In.seekg(0, std::ios::end);
		auto Size = In.tellg();
		if (Size < 0 || static_cast<std::uint64_t>(Size) > Inputs.CompileProcessing.MaxInputBytes)
		{
			throw std::runtime_error("compilation extension source budget exceeded");
		}

		In.seekg(0);
		Source.resize(static_cast<std::size_t>(Size));
		In.read(Source.data(), Size);
		if (!In && Size)
		{
			throw std::runtime_error("failed to read compilation extension source");
		}

		return true;
	}
	void FTask::RecordContent(const std::string& Path, const std::string& Source)
	{
		const auto Identity = PathUtil::IdentityKey(Path);
		const auto Previous = Contents.find(Identity);
		if (Previous != Contents.end())
		{
			if (Previous->second != Source)
			{
				throw std::runtime_error("compilation source changed during preparation: " + Path);
			}

			return;
		}
		if (Contents.size() >= Inputs.CompileProcessing.MaxDependencies ||
			Source.size() > Inputs.CompileProcessing.MaxInputBytes -
					std::min(ContentBytes, Inputs.CompileProcessing.MaxInputBytes))
		{
			throw std::runtime_error("compilation extension code budget exceeded");
		}

		ContentBytes += Source.size();
		Contents.emplace(Identity, Source);
	}
} // namespace VeroScript::ScriptExtensions

namespace VeroScript
{
	bool HasScriptCompileExtensionImport(std::string_view Source)
	{
		try
		{
			return ScriptExtensions::HasCompilerImport(Source);
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
	std::string GetScriptCompileServiceFingerprint()
	{
		static const auto Schema = []
		{
			auto T = std::make_shared<ScriptExtensions::FTask>();
			T->Deadline = std::chrono::steady_clock::time_point::max();
			T->InitializeBindings();
			std::string Json;
			if (!ExportBindingCatalogJson(T->Bindings, Json))
			{
				throw std::runtime_error("failed to build compiler service schema");
			}

			return std::to_string(Runtime::Fnv1a64(14695981039346656037ull, Json));
		}();
		return std::string("std::compiler:0.1.1:") + VEROSCRIPT_JIT_PUBLIC_ABI + ":" +
			   VEROSCRIPT_CORE_IMPLEMENTATION_ID + ":" + Schema;
	}
	std::shared_ptr<const IHostBindingCatalog> GetScriptCompileServiceCatalog()
	{
		static const auto Catalog = []() -> std::shared_ptr<const IHostBindingCatalog>
		{
			auto T = std::make_shared<ScriptExtensions::FTask>();
			T->Deadline = std::chrono::steady_clock::time_point::max();
			T->InitializeBindings();
			std::string Json, Error;
			if (!ExportBindingCatalogJson(T->Bindings, Json))
			{
				throw std::runtime_error("failed to export compiler service catalog");
			}

			auto R = std::make_shared<FHostBindingRegistry>();
			if (!ImportBindingCatalogJson(Json, *R, Error))
			{
				throw std::runtime_error("failed to import compiler service catalog: " + Error);
			}

			return R;
		}();
		return Catalog;
	}
	FCompileResult CompileScriptCompileExtensionSource(std::string_view Source, std::string_view Label,
													   const FCompileOptions& O, std::string_view RegistrationEntry)
	try
	{
		auto T = std::make_shared<ScriptExtensions::FTask>();
		T->Inputs = O;
		T->Processing = O.CompileProcessing;
		T->Deadline = O.ScriptCompileExtensions.TimeoutMilliseconds
						  ? std::chrono::steady_clock::now() +
								std::chrono::milliseconds(
									std::min<std::uint64_t>(O.ScriptCompileExtensions.TimeoutMilliseconds, INT32_MAX))
						  : std::chrono::steady_clock::time_point::max();
		T->InitializeBindings();
		T->Inputs.SourceOverlays.push_back({std::string(Label), std::string(Source)});
		for (const auto& Root : O.ScriptCompileExtensions.Roots)
		{
			T->Load(T->Resolve(Root.Path, Label), Root.RegistrationEntry, 0);
		}

		T->Load(std::string(Label), std::string(RegistrationEntry), 0);
		FCompileResult R;
		R.bSuccess = true;
		auto Root = std::find_if(T->Modules.begin(), T->Modules.end(),
			[&](const auto& M)
			{
				return PathUtil::IdentityKey(M->Path) == PathUtil::IdentityKey(std::string(Label));
			});
		if (Root == T->Modules.end())
		{
			throw std::runtime_error("compiled extension root is missing");
		}

		R.Module = std::make_unique<FBytecodeModule>(*(*Root)->Code);
		auto Dependencies = O.ScriptCompileExtensions.Roots;
		const auto& SourceDependencies = T->ExtensionDependencies[std::string(Label)];
		Dependencies.insert(Dependencies.end(), SourceDependencies.begin(), SourceDependencies.end());
		// Match the loading graph's resolved identities while retaining the first
		// spelling for deployment. A configured root can also occur in the source
		// prefix, or can name the producer itself through a relative path.
		std::set<std::string> DependencyIdentities;
		const auto RootIdentity = PathUtil::IdentityKey(std::string(Label));
		for (const auto& Dependency : Dependencies)
		{
			const auto Identity = PathUtil::IdentityKey(T->Resolve(Dependency.Path, Label));
			if (Identity != RootIdentity && DependencyIdentities.insert(Identity).second)
			{
				R.CompileExtensionDependencies.push_back(Dependency);
			}
		}
		auto Manifest = std::make_shared<FCompileExtensionManifest>();
		Manifest->ModuleId = R.Module->EntryModulePath;
		Manifest->RegistrationEntry = (*Root)->Entry;
		Manifest->ServiceFingerprint = GetScriptCompileServiceFingerprint();
		for (const auto& Dependency : R.CompileExtensionDependencies)
		{
			Manifest->Dependencies.push_back({Dependency.Path, Dependency.RegistrationEntry});
		}

		if (O.ScriptCompileExtensions.Bindings)
		{
			std::string Catalog;
			if (!ExportBindingCatalogJson(*O.ScriptCompileExtensions.Bindings, Catalog))
			{
				throw std::runtime_error("cannot fingerprint compilation host services");
			}

			Manifest->HostBindingFingerprint =
				std::to_string(Runtime::Fnv1a64(14695981039346656037ull, Catalog));
		}
		R.CompileExtensionManifest = std::move(Manifest);
		std::vector<FScriptError> D;
		if (!T->Close(D))
		{
			R.bSuccess = false;
			R.Diagnostics = std::move(D);
			R.Error = R.Diagnostics.front();
			R.Module.reset();
		}
		return R;
	}
	catch (const FAnalysisCancelled&)
	{
		throw;
	}
	catch (const std::exception& X)
	{
		FCompileResult R;
		R.Error.Message = X.what();
		R.Diagnostics.push_back(R.Error);
		return R;
	}
	std::shared_ptr<IScriptCompileExtensionTask> LoadScriptCompileExtensions(std::string_view Source,
																			 std::string_view Label,
																			 const FCompileOptions& O,
																			 std::vector<FScriptError>& D)
	try
	{
		if (O.CompileProcessing.PrepareSource && !O.ScriptCompileExtensions.bAllowSourceImports &&
			O.ScriptCompileExtensions.Roots.empty())
		{
			return {};
		}

		if (O.ScriptCompileExtensions.Roots.empty() && !ScriptExtensions::HasCompilerImport(Source))
		{
			return {};
		}

		if (O.CompileProcessing.Registry && !O.CompileProcessing.Registry->IsFrozen())
		{
			throw std::runtime_error("base compiler processor registry must be frozen");
		}

		auto T = std::make_shared<ScriptExtensions::FTask>();
		T->Inputs = O;
		T->Processing = O.CompileProcessing;
		auto CopyViews = [&](const auto& Original, auto& Dest)
		{
			Dest.clear();
			for (const auto V : Original)
			{
				T->InputStrings.emplace_back(V);
				Dest.push_back(T->InputStrings.back());
			}
		};
		CopyViews(O.ImportPaths, T->Inputs.ImportPaths);
		CopyViews(O.StdlibSearchPaths, T->Inputs.StdlibSearchPaths);
		CopyViews(O.Defines, T->Inputs.Defines);
		CopyViews(O.ImplicitImportPaths, T->Inputs.ImplicitImportPaths);
		if (O.Bindings)
		{
			for (auto Def : GetAnnotationDefinitions(*O.Bindings))
			{
				T->Processing.AnnotationDefinitions.push_back(std::move(Def));
			}
		}

		std::string DefinitionError;
		if (!NormalizeActiveAnnotationDefinitions(T->Processing.AnnotationDefinitions, DefinitionError))
		{
			throw std::runtime_error(DefinitionError);
		}

		if (O.CompileProcessing.InitialDependencies.size() > O.CompileProcessing.MaxDependencies)
		{
			throw std::runtime_error("initial compiler dependency count exceeded");
		}

		for (const auto& Dependency : O.CompileProcessing.InitialDependencies)
		{
			if (Dependency.Path.empty() ||
				!T->Dependencies
					.emplace(IsResourceDependencyId(Dependency.Path) ? Dependency.Path
																	 : PathUtil::IdentityKey(Dependency.Path),
						Dependency)
					.second ||
				Dependency.Content.size() > O.CompileProcessing.MaxInputBytes -
						std::min(T->DependencyBytes, O.CompileProcessing.MaxInputBytes))
			{
				throw std::runtime_error("invalid initial compiler dependency");
			}

			T->DependencyBytes += Dependency.Content.size();
		}
		T->Deadline = O.ScriptCompileExtensions.TimeoutMilliseconds
						  ? std::chrono::steady_clock::now() +
								std::chrono::milliseconds(
									std::min<std::uint64_t>(O.ScriptCompileExtensions.TimeoutMilliseconds, INT32_MAX))
						  : std::chrono::steady_clock::time_point::max();
		T->InitializeBindings();
		for (const auto& R : O.ScriptCompileExtensions.Roots)
		{
			T->Load(T->Resolve(R.Path, Label), R.RegistrationEntry, 0);
		}

		std::string Entry(Source);
		T->Mask(Label, Entry, true);
		if (T->Modules.empty())
		{
			return {};
		}

		if (O.ScriptCompileExtensions.Bindings && !O.ScriptCompileExtensions.bBindingsDeterministic)
		{
			T->Cacheable = false;
		}

		for (const auto& C : T->Contents)
		{
			if (!T->Dependencies.count(C.first))
			{
				if (T->Dependencies.size() >= O.CompileProcessing.MaxDependencies ||
					C.second.size() > O.CompileProcessing.MaxInputBytes -
							std::min(T->DependencyBytes, O.CompileProcessing.MaxInputBytes))
				{
					throw std::runtime_error("compilation code dependency budget exceeded");
				}

				T->DependencyBytes += C.second.size();
				T->Dependencies.emplace(C.first, FAnnotationDependency{C.first, C.second, true});
			}
		}

		for (const auto& M : T->Modules)
		{
			T->Executing = M;
			T->Registering = true;
			++T->Epoch;
			T->Invoke(*M, M->Registration, {T->Make("registration", {}, true)});
			T->Registering = false;
		}
		T->Frozen = true;
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		std::string Error;
		if (O.CompileProcessing.Registry)
		{
			for (const auto& P : O.CompileProcessing.Registry->GetProcessors())
			{
				if (!Registry->Register(P, Error))
				{
					throw std::runtime_error(Error);
				}
			}
		}

		std::sort(T->Processors.begin(), T->Processors.end(),
			[](const auto& A, const auto& B)
			{
				return std::tie(A.Description.Order, A.Description.ProcessorId) <
					std::tie(B.Description.Order, B.Description.ProcessorId);
			});
		for (auto& P : T->Processors)
		{
			T->Executing = P.Module;
			++T->Epoch;
			auto V = T->Invoke(*P.Module, P.Factory, {});
			auto F = std::get_if<FFunctionHandle>(&V);
			if (!F || !*F)
			{
				throw std::runtime_error("compiler factory returned an unbound callback");
			}

			if ((*F)->ProgramOwner.get() != P.Module->Code.get() ||
				(*F)->ProgramGeneration != P.Module->Generation ||
				(*F)->TypeName != "fn(std.compiler.context,std.compiler.output)->void")
			{
				throw std::runtime_error("compiler callback has a foreign module or invalid signature");
			}

			P.Callback = *F;
			P.Module->Callables.push_back(P.Callback);
			if (!Registry->Register(P.Description, Error))
			{
				throw std::runtime_error(Error);
			}
		}
		Registry->Freeze();
		T->Executing.reset();
		T->Processing.Registry = Registry;
		T->Cacheable = T->Cacheable && O.CompileProcessing.bCacheable;
		for (const auto& P : Registry->GetProcessors())
		{
			T->Cacheable = T->Cacheable && P.Cacheable;
		}

		std::string Profile = GetScriptCompileServiceFingerprint();
		auto AppendProfile = [&](std::string_view Value)
		{
			Profile += std::to_string(Value.size()) + ":";
			Profile.append(Value);
		};
		AppendProfile(O.CompileProcessing.ImplementationFingerprint);
		AppendProfile(std::to_string(T->Contents.size()));
		for (const auto& Content : T->Contents)
		{
			AppendProfile(Content.first);
			AppendProfile(std::to_string(Content.second.size()));
			AppendProfile(std::to_string(Runtime::Fnv1a64(14695981039346656037ull, Content.second)));
		}
		AppendProfile(O.ScriptCompileExtensions.bAllowSourceImports ? "1" : "0");
		AppendProfile(std::to_string(O.ScriptCompileExtensions.AllowedRoots.size()));
		for (const auto& Root : O.ScriptCompileExtensions.AllowedRoots)
		{
			AppendProfile(Root);
		}

		AppendProfile(std::to_string(T->Modules.size()));
		for (const auto& Module : T->Modules)
		{
			AppendProfile(Module->Identity);
			AppendProfile(Module->Entry);
			if (const auto& Factory = Module->Vm->GetVmBackendFactory())
			{
				const auto& Backend = Factory->GetDescription();
				AppendProfile(Backend.Id);
				AppendProfile(Backend.Version);
				AppendProfile(Backend.ImplementationId);
				AppendProfile(Backend.Target);
				AppendProfile(std::to_string(Backend.Capabilities));
			}
		}
		AppendProfile(std::to_string(O.ScriptCompileExtensions.Configuration.size()));
		for (const auto& Item : O.ScriptCompileExtensions.Configuration)
		{
			AppendProfile(Item.first);
			AppendProfile(Item.second);
		}
		AppendProfile(O.ScriptCompileExtensions.BindingConfiguration);
		AppendProfile(std::to_string(O.ScriptCompileExtensions.MaxInstructions));
		AppendProfile(std::to_string(O.ScriptCompileExtensions.TimeoutMilliseconds));
		AppendProfile(GetHostPlatformString());
		AppendProfile(std::to_string(sizeof(void*)));
		AppendProfile(std::to_string(O.ScriptCompileExtensions.MaxRoots));
		AppendProfile(std::to_string(O.ScriptCompileExtensions.MaxDependencyDepth));
		AppendProfile(std::to_string(O.ScriptCompileExtensions.MaxRegistrations));
		AppendProfile(std::to_string(O.ScriptCompileExtensions.MaxBridgeBytes));
		AppendProfile(std::to_string(O.ScriptCompileExtensions.MaxBridgeValues));
		AppendProfile(std::to_string(O.Defines.size()));
		for (const auto Define : O.Defines)
		{
			AppendProfile(Define);
		}

		AppendProfile(std::to_string(O.PredefinedMacros.size()));
		for (const auto& Macro : O.PredefinedMacros)
		{
			AppendProfile(Macro.Name);
			AppendProfile(Macro.Body);
			AppendProfile(Macro.bVariadic ? "1" : "0");
			AppendProfile(Macro.SourceName);
			AppendProfile(std::to_string(Macro.Parameters.size()));
			for (const auto& Parameter : Macro.Parameters)
			{
				AppendProfile(Parameter);
			}
		}
		std::string Catalog;
		if (!ExportBindingCatalogJson(T->Bindings, Catalog))
		{
			throw std::runtime_error("failed to fingerprint compilation bindings");
		}

		Profile += ":" + std::to_string(Runtime::Fnv1a64(14695981039346656037ull, Catalog));
		T->Processing.ImplementationFingerprint = std::move(Profile);
		T->Processing.bCacheable = T->Cacheable;
		T->Processing.InitialDependencies.clear();
		for (const auto& P : T->Dependencies)
		{
			T->Processing.InitialDependencies.push_back(P.second);
		}

		T->Processing.ReadDependency = [W = T->weak_from_this()](std::string_view P, std::string& S)
		{
			auto T = W.lock();
			if (!T)
			{
				throw std::runtime_error("expired compiler dependency service");
			}

			return T->ReadDependency(P, S);
		};
		T->Processing.PrepareSource = [W = T->weak_from_this()](std::string_view P, std::string& S)
		{
			auto T = W.lock();
			if (!T)
			{
				throw std::runtime_error("expired compiler extension preparation");
			}

			return T->Mask(P, S);
		};
		return T;
	}
	catch (const FAnalysisCancelled&)
	{
		throw;
	}
	catch (const std::exception& E)
	{
		FScriptError Error;
		Error.Message = E.what();
		Error.Location = {Label, 1, 1};
		D.push_back(std::move(Error));
		return {};
	}
} // namespace VeroScript

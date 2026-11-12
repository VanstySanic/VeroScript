// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptJitFrameServices.h"
#include "VeroScriptJitValueServices.h"
#include "VeroScriptJitCallServices.h"
#include "VeroScriptJitScalarServices.h"
#include "VeroScriptAotProgram.h"
#include "VeroScriptCompile.h"
#include "VeroScriptJitModule.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptVm.h"

#include <atomic>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
	using namespace VeroScript;

	enum class EBehavior
	{
		Return,
		PreferVm,
		PreferVmSuppress,
		Unsupported,
		Defer,
		DeferOnce,
		CompileFailure,
		CompileFailureOnce,
		CompileThrow,
		InvalidArtifact,
		Refuse,
		DirtyRefuse,
		ExecuteThrow,
		ControlTransfer,
		MissingDiagnostic,
		WrongReturn,
		ForeignOwner,
		PublishFailure,
		NestedCall,
		Interpret,
		OptimizedScope,
		LeakedOptimizedScope,
		Frame,
		FrameNestedCall,
		FrameReference,
		StaleFrame,
		LeakedFrame,
		FrameThrow,
		ValueOperations,
		FrameExit,
	};

	struct FProbe
	{
		std::atomic<std::size_t> Instances{0};
		std::atomic<std::size_t> Compiles{0};
		std::atomic<std::size_t> Effects{0};
		std::atomic<std::size_t> LiveCode{0};
		std::atomic<std::size_t> Retirements{0};
		std::atomic<std::size_t> Publications{0};
		std::atomic<std::size_t> Discards{0};
		std::atomic<std::size_t> Destructions{0};
		std::string CompileReason;
	};

	class FServices final : public IJitRuntimeServices
	{
	public:
		//~ Begin IJitRuntimeServices Interface
		bool HasError() const noexcept override
		{
			return bError;
		}

		bool HasPendingControlTransfer() const noexcept override
		{
			return bTransfer;
		}

		bool CheckExecutionGovernance() noexcept override
		{
			return !bError && !bTransfer;
		}

		void ReportError(std::string_view) noexcept override
		{
			bError = true;
		}

		bool PrepareReturn(const FBytecodeFunction&, FValue& Value) noexcept override
		{
			return std::holds_alternative<std::int32_t>(Value);
		}

		bool InvokeFunction(std::uint16_t, const FObjectHandle&, const FValue*,
		    std::uint8_t, FValue&) noexcept override
		{
			bError = true;
			return false;
		}

		bool CopyValue(const FValue& Value, FValue& OutCopy) noexcept override
		{
			if (const auto* Integer = std::get_if<std::int32_t>(&Value))
			{
				OutCopy = *Integer;
				return true;
			}
			return false;
		}

		bool ReleaseValue(FValue& Value) noexcept override
		{
			Value = std::int32_t{0};
			return true;
		}
		//~ End IJitRuntimeServices Interface

		bool bError = false;
		bool bTransfer = false;
	};

	class FExecutable final : public IJitExecutable
	{
	public:
		//~ Begin IJitExecutable Interface
		EJitInvocationResult Invoke(FJitCallContext& Context) const override
		{
			if (Behavior == EBehavior::Refuse || ((Behavior == EBehavior::NestedCall || Behavior == EBehavior::Interpret || Behavior == EBehavior::FrameNestedCall || Behavior == EBehavior::ValueOperations) && Context.FunctionIndex != 0))
			{
				return EJitInvocationResult::NotEntered;
			}
			Context.MarkEntered();
			++Probe->Effects;
			if (Behavior == EBehavior::DirtyRefuse)
			{
				return EJitInvocationResult::NotEntered;
			}
			if (Behavior == EBehavior::ExecuteThrow)
			{
				throw std::runtime_error("probe execution failure");
			}
			if (Behavior == EBehavior::ControlTransfer)
			{
				static_cast<FServices*>(Context.Services)->bTransfer = true;
				return EJitInvocationResult::ControlTransfer;
			}
			if (Behavior == EBehavior::MissingDiagnostic)
			{
				return EJitInvocationResult::Failed;
			}
			if (Behavior == EBehavior::Interpret)
			{
				if (!Context.Services->GetCallServices()->InterpretInvocation(Context.Arguments,
				        Context.ArgumentCount, Context.Receiver, Context.ReturnValue))
				{
					return Context.Services->HasPendingControlTransfer() ? EJitInvocationResult::ControlTransfer : EJitInvocationResult::Failed;
				}
				Context.bHasReturn = !Context.Program->Module->Functions[Context.FunctionIndex].bReturnsVoid;
				return EJitInvocationResult::Returned;
			}
			if (Behavior == EBehavior::OptimizedScope || Behavior == EBehavior::LeakedOptimizedScope)
			{
				auto* Calls = Context.Services->GetCallServices();
				if (!Calls || Calls->GetRemainingCallDepth() != 1 || !Calls->EnterOptimizedCall() || Calls->GetRemainingCallDepth() != 0)
				{
					return EJitInvocationResult::Failed;
				}
				if (Behavior == EBehavior::OptimizedScope)
				{
					Calls->LeaveOptimizedCall();
				}
				Context.ReturnValue = std::int32_t{99};
				Context.bHasReturn = true;
				return EJitInvocationResult::Returned;
			}
			if (Behavior >= EBehavior::Frame)
			{
				auto* Frames = Context.Services->GetFrameServices();
				FJitFrameToken Token;
				if (!Frames || !Frames->Enter({Context.FunctionIndex}, Context.Arguments, Context.ArgumentCount, Context.Receiver, Token))
				{
					return EJitInvocationResult::Failed;
				}
				if (Behavior == EBehavior::FrameExit)
				{
					FValue Code{std::int32_t{7}};
					bool bHasResult = false;
					(void)Context.Services->GetCallServices()->InvokeBuiltin(EBuiltinId::Exit, &Code, 1, bHasResult, Context.ReturnValue);
					return Context.Services->HasPendingControlTransfer() ? EJitInvocationResult::ControlTransfer : EJitInvocationResult::Failed;
				}
				if (Behavior == EBehavior::FrameThrow)
				{
					throw std::runtime_error("failure with an active JIT frame");
				}
				const auto DirectLocals = Frames->BorrowDirectLocalStorage(Token);
				if (!DirectLocals.Data || DirectLocals.Count == 0) return EJitInvocationResult::Failed;
				FValue Value;
				if (!Frames->SetInstructionOffset(Token, 0) || !Frames->EvaluateArithmetic(EOpcode::Add, FValue{std::int32_t{20}}, FValue{std::int32_t{22}}, Value) || !Frames->StoreLocal(Token, 0, Value) || std::get<std::int32_t>(DirectLocals.Data[0]) != 42)
				{
					return EJitInvocationResult::Failed;
				}
				if (Behavior == EBehavior::Frame)
				{
					auto* Scalar = Context.Services->GetScalarServices();
					bool bHandled = false;
					if (!Scalar || !Scalar->AddLocalImm(0, 1) || !Scalar->TryLoadScalarLocal(0, Value, bHandled) || !bHandled || std::get<std::int32_t>(Value) != 43 || !Scalar->BorrowDirectLocal(0) || !Frames->StoreLocal(Token, 0, FValue{std::int32_t{42}}))
					{
						return EJitInvocationResult::Failed;
					}
					const FValue Text = std::string("a\0b", 3);
					FValue Snapshot;
					if (!Frames->StoreLocal(Token, 0, Text) || !Frames->LoadLocal(Token, 0, Snapshot) || !Scalar->AddLocalLocal(0, 0) || !Scalar->AddValueToLocal(0, FValue{std::string("!")}) || !Frames->LoadLocal(Token, 0, Value) || std::get<std::string>(Value) != std::string("a\0ba\0b!", 7) || std::get<std::string>(Snapshot) != std::string("a\0b", 3) || !Frames->StoreLocal(Token, 0, FValue{std::u16string{u'x', 0, 0xD83D, 0xDE42}}) || !Scalar->AddLocalLocal(0, 0) || !Scalar->AddValueToLocal(0, FValue{std::u16string{u'!'}}) || !Frames->LoadLocal(Token, 0, Value) || std::get<std::u16string>(Value) != std::u16string{u'x', 0, 0xD83D, 0xDE42, u'x', 0, 0xD83D, 0xDE42, u'!'} || !Frames->StoreLocal(Token, 0, FValue{std::int32_t{42}}))
					{
						return EJitInvocationResult::Failed;
					}
				}
				if (Behavior == EBehavior::ValueOperations)
				{
					if (!ExerciseValueServices(Context, *Frames, Token)) return EJitInvocationResult::Failed;
				}
				else if (Behavior == EBehavior::FrameNestedCall)
				{
					if (!Context.Services->InvokeFunction(1, {}, nullptr, 0, Context.ReturnValue)) return EJitInvocationResult::Failed;
					const auto CurrentLocals = Frames->BorrowDirectLocalStorage(Token);
					if (CurrentLocals.Data != DirectLocals.Data || CurrentLocals.Count != DirectLocals.Count || std::get<std::int32_t>(DirectLocals.Data[0]) != 42)
					{
						return EJitInvocationResult::Failed;
					}
				}
				else if (Behavior == EBehavior::FrameReference)
				{
					if (!Frames->MakeLocalReference(Token, 0, Context.ReturnValue) || !Context.Services->PrepareReturn(Context.Program->Module->Functions[Context.FunctionIndex], Context.ReturnValue))
					{
						return EJitInvocationResult::Failed;
					}
				}
				else if (!Frames->LoadLocal(Token, 0, Context.ReturnValue))
				{
					return EJitInvocationResult::Failed;
				}
				Context.bHasReturn = true;
				if (Behavior == EBehavior::LeakedFrame) return EJitInvocationResult::Returned;
				if (!Frames->Leave(Token)) return EJitInvocationResult::Failed;
				if (Behavior == EBehavior::StaleFrame && !Frames->Leave(Token)) return EJitInvocationResult::Failed;
				return EJitInvocationResult::Returned;
			}
			if (!Context.Program->Module->Functions[Context.FunctionIndex].bReturnsVoid)
			{
				if (Behavior == EBehavior::NestedCall)
				{
					if (!Context.Services->InvokeFunction(1, {}, nullptr, 0, Context.ReturnValue))
					{
						return Context.Services->HasPendingControlTransfer()
						    ? EJitInvocationResult::ControlTransfer
						    : EJitInvocationResult::Failed;
					}
				}
				else
				{
					Context.ReturnValue = Behavior == EBehavior::WrongReturn ? FValue{true} : FValue{std::int32_t{42}};
				}
				Context.bHasReturn = true;
			}
			return EJitInvocationResult::Returned;
		}
		//~ End IJitExecutable Interface

		~FExecutable() override
		{
			--Probe->LiveCode;
		}

		FExecutable(const EBehavior InBehavior, std::shared_ptr<FProbe> InProbe)
		    : Behavior(InBehavior), Probe(std::move(InProbe))
		{
			++Probe->LiveCode;
		}

	private:
		static bool ExerciseValueServices(FJitCallContext& Context, IJitFrameServices& Frames, const FJitFrameToken Token)
		{
			auto* Values = Context.Services->GetValueServices();
			auto* Calls = Context.Services->GetCallServices();
			auto* Scalar = Context.Services->GetScalarServices();
			if (!Values || !Calls || !Scalar) return false;
			FValue Object;
			FValue Result;
			FValue Closure;
			const bool bOk = [&]()
			{
				if (!Values->CreateObject(0, Object) || !Frames.StoreLocal(Token, 0, Object) || !Context.Services->ReleaseValue(Object) || !Frames.LoadLocal(Token, 0, Object) || !Values->StoreField(Object, 0, FValue{std::int32_t{40}}))
				{
					return false;
				}
				const auto CheckField = [&](FValue& Owner, const std::uint16_t ExpectedIndex, const std::int32_t ExpectedValue)
				{
					std::uint16_t Index = 0xFFFF;
					FValue* FieldStorage = nullptr;
					return Values->BorrowObjectField(*std::get<FObjectHandle>(Owner), 0, Index, FieldStorage) && Index == ExpectedIndex && FieldStorage && std::get<std::int32_t>(*FieldStorage) == ExpectedValue;
				};
				if (!CheckField(Object, 0, 40) || !CheckField(Object, 0, 40) || !Values->CreateObject(1, Result) || !Values->StoreField(Result, 0, FValue{std::int32_t{17}}) || !CheckField(Result, 1, 17) || !CheckField(Result, 1, 17) || !CheckField(Object, 0, 40) || !Context.Services->ReleaseValue(Result))
				{
					return false;
				}
				FScriptObject* FieldOwner = nullptr;
				std::uint16_t FieldIndex = 0;
				std::int32_t* Storage = nullptr;
				if (!Scalar->BorrowLocalI32Field(0, 0, FieldOwner, FieldIndex, Storage) || !Storage) return false;
				++*Storage;
				Scalar->CommitBorrowedFieldMutation(*FieldOwner, FieldIndex);
				if (!Values->CloneStruct(Object) || !Values->StoreField(Object, 0, FValue{std::int32_t{9}}) || !Context.Services->ReleaseValue(Object) || !Frames.LoadLocal(Token, 0, Object) || !Values->LoadField(Object, 0, Result))
				{
					return false;
				}
				if (std::get<std::int32_t>(Result) != 41)
				{
					Context.Services->ReportError("local loads must alias before an explicit struct clone");
					return false;
				}
				if (!Values->CreateContainer(EOpcode::NewDynamicArray, 1, Object) || !Frames.StoreLocal(Token, 0, Object) || !Calls->InvokeMethod(2, Object, &Result, 1, 0, Context.ReturnValue) || !Context.Services->ReleaseValue(Context.ReturnValue) || !Values->GetIndex(Object, FValue{std::int32_t{0}}, Result))
				{
					return false;
				}
				if (!Frames.EvaluateArithmetic(EOpcode::Add, Result, FValue{std::int32_t{1}}, Context.ReturnValue) || !Calls->MakeClosure(1, nullptr, 0, Closure) || !Calls->InvokeIndirect(Closure, nullptr, 0, Result))
				{
					return false;
				}
				if (std::get<std::int32_t>(Result) != 99) return false;
				bool bHasResult = false;
				const auto Status = Context.Services->BorrowExecutionStatus();
				if (!Status.InstanceInterceptor || !Status.MutationObserver || *Status.InstanceInterceptor || *Status.MutationObserver)
				{
					return false;
				}
				if (!Calls->InvokeBuiltin(EBuiltinId::Len, &Object, 1, bHasResult, Result) || !bHasResult || std::get<std::int64_t>(Result) != 1 || !Calls->InvokeNative(0, nullptr, 0, Result))
				{
					return false;
				}
				if (!*Status.InstanceInterceptor || !*Status.MutationObserver || !Calls->HasInstanceInterceptor() || !Calls->HasMutationObserver() || !Calls->InvokeNative(2, nullptr, 0, Result) || *Status.InstanceInterceptor || *Status.MutationObserver || Calls->HasInstanceInterceptor() || Calls->HasMutationObserver())
				{
					return false;
				}
				return std::get<std::int32_t>(Result) == 7;
			}();
			const bool bReleasedObject = Context.Services->ReleaseValue(Object);
			const bool bReleasedResult = Context.Services->ReleaseValue(Result);
			const bool bReleasedClosure = Context.Services->ReleaseValue(Closure);
			return bOk && bReleasedObject && bReleasedResult && bReleasedClosure;
		}

		EBehavior Behavior;
		std::shared_ptr<FProbe> Probe;
	};

	class FInstance final : public IJitBackendInstance
	{
	public:
		//~ Begin IJitBackendInstance Interface
		const FJitBackendDescription& GetDescription() const noexcept override
		{
			return Description;
		}

		EJitCompilationResult Compile(const FJitCompileRequest& Request, FJitCompileResponse& Response) override
		{
			++Probe->Compiles;
			Response.Reason = Probe->CompileReason;
			if (Behavior == EBehavior::Unsupported)
			{
				return EJitCompilationResult::Unsupported;
			}
			if (Behavior == EBehavior::Defer || (Behavior == EBehavior::DeferOnce && Probe->Compiles == 1))
			{
				return EJitCompilationResult::Deferred;
			}
			if (Behavior == EBehavior::CompileFailure ||
			    (Behavior == EBehavior::CompileFailureOnce && Probe->Compiles == 1))
			{
				if (Response.Reason.empty())
				{
					Response.Reason = "probe compile failure";
				}

				return EJitCompilationResult::Failed;
			}
			if (Behavior == EBehavior::CompileThrow)
			{
				throw std::bad_alloc();
			}
			auto Function = std::make_shared<FJitCompiledFunction>();
			Function->Identity = Request.Program.Identity;
			Function->FunctionIndex = Request.FunctionIndex;
			Function->ProgramOwner = Request.Program.Owner;
			Function->Executable = std::make_shared<FExecutable>(Behavior, Probe);
			Function->bPreferVmForExecution = Behavior == EBehavior::PreferVm || Behavior == EBehavior::PreferVmSuppress;
			if (Behavior == EBehavior::PreferVmSuppress)
			{
				Function->bSuppressNestedBackendOnFallback = true;
				Function->GovernanceCapabilities = ControlledExecutionCapabilities | ExecutionObservationCapability;
			}
			if (Behavior == EBehavior::InvalidArtifact)
			{
				++Function->Identity.Generation;
			}
			if (Behavior == EBehavior::ForeignOwner)
			{
				// Deliberately invalid ownership: equal raw pointers are insufficient.
				Function->ProgramOwner = std::shared_ptr<const FBytecodeModule>(Request.Program.Module, [](const auto*)
				{
				});
			}
			Response.Function = std::move(Function);
			return EJitCompilationResult::Ready;
		}

		FJitResourceUsage GetResourceUsage() const noexcept override
		{
			return {Probe->LiveCode * 4096, Probe->LiveCode * 128, 0};
		}

		bool Publish(const FJitCompiledFunction&, std::string& OutError) override
		{
			if (Behavior == EBehavior::PublishFailure)
			{
				OutError = "probe publication failure";
				return false;
			}
			++Probe->Publications;
			return true;
		}

		void Discard(const std::shared_ptr<const FJitCompiledFunction>& Function) noexcept override
		{
			if (Function)
			{
				++Probe->Discards;
			}
		}

		void Retire() noexcept override
		{
			++Probe->Retirements;
		}
		//~ End IJitBackendInstance Interface

		FInstance(const EBehavior InBehavior, std::shared_ptr<FProbe> InProbe,
		    FJitBackendDescription InDescription)
		    : Behavior(InBehavior), Probe(std::move(InProbe)), Description(std::move(InDescription))
		{
			++Probe->Instances;
		}

	private:
		EBehavior Behavior;
		std::shared_ptr<FProbe> Probe;
		FJitBackendDescription Description;
	};

	class FFactory final : public IJitBackendFactory
	{
	public:
		//~ Begin IJitBackendFactory Interface
		const FJitBackendDescription& GetDescription() const noexcept override
		{
			return Description;
		}

		std::unique_ptr<IJitBackendInstance> CreateInstance(const FJitProgramView&,
		    const FJitResourceLimits&, std::string&) const override
		{
			return std::make_unique<FInstance>(Behavior, Probe, Description);
		}
		//~ End IJitBackendFactory Interface

		FFactory(const EBehavior InBehavior, std::shared_ptr<FProbe> InProbe)
		    : Behavior(InBehavior), Probe(std::move(InProbe))
		{
			Description.Id = "contract-probe";
			Description.DisplayName = "Independent public-contract test backend";
			Description.Target = "portable-cpp";
			if (Behavior == EBehavior::PreferVmSuppress)
			{
				Description.Capabilities |= JitControlledExecutionCapability;
			}
			if (Behavior == EBehavior::Interpret || Behavior == EBehavior::OptimizedScope || Behavior == EBehavior::LeakedOptimizedScope)
			{
				Description.RequiredServices = JitCallServiceCapability;
			}
			if (Behavior >= EBehavior::Frame)
			{
				Description.RequiredServices = JitFrameServiceCapability;
			}
			if (Behavior == EBehavior::ValueOperations || Behavior == EBehavior::FrameExit)
			{
				Description.RequiredServices |= JitValueServiceCapability | JitCallServiceCapability;
			}
			if (Behavior == EBehavior::Frame || Behavior == EBehavior::ValueOperations)
			{
				Description.RequiredServices |= JitScalarServiceCapability;
			}
		}

		FJitBackendDescription Description;

	private:
		EBehavior Behavior;
		std::shared_ptr<FProbe> Probe;
	};

	FJitProgramView MakeProgram(const bool bVoid = false)
	{
		auto Module = std::make_shared<FBytecodeModule>();
		Module->Functions.resize(3);
		for (auto& Function : Module->Functions)
		{
			Function.bReturnsVoid = bVoid;
			Function.ClosureTypeName = bVoid ? "fn()->void" : "fn()->int";
		}
		return {Module.get(), Module, {AllocateJitProgramIdentity(), 1, 1}};
	}

	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "JIT contract failure: " << Message << '\n';
		}
		return Condition;
	}

	std::unique_ptr<FJitBackendCache> MakeCache(const EBehavior Behavior,
	    const std::shared_ptr<FProbe>& Probe, FJitProgramView Program = MakeProgram(),
	    const FJitPolicy Policy = {})
	{
		std::string Error;
		auto Cache = FJitBackendCache::Create(std::make_shared<FFactory>(Behavior, Probe),
		    std::move(Program), Policy, {}, 0, Error);
		if (!Cache)
		{
			throw std::runtime_error(Error);
		}
		return Cache;
	}

	bool CheckPreparationInvalidation()
	{
		auto Compiled = VeroScriptCompile::CompileSource("void main(){print(7);}", "preparation-invalidation.vs");
		if (!Expect(Compiled.bSuccess, "preparation invalidation fixture compiles")) return false;
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
		for (const bool Reuse : {false, true})
		{
			for (const bool Stop : {false, true})
			{
				for (const bool ReplacePolicy : {false, true})
				{
					auto Probe = std::make_shared<FProbe>();
					std::ostringstream Output;
					VeroScriptVm Vm(Output);
					VeroScript::Test::Configure(Vm);
					Vm.SetExecutionBackend(EExecutionBackend::Jit);
					std::string Error;
					if (!Expect(Vm.SetJitBackendFactory(std::make_shared<FFactory>(EBehavior::Return, Probe), Error) && Vm.SetJitProgramOwner(Module, Error), "preparation fixture selects backend"))
					{
						return false;
					}
					bool Armed = false, Invalidated = false, RetainedDuringCheck = false;
					Vm.SetExecutionCheck([&]()
					{
						if (Armed && !Invalidated && Vm.GetJitResourceUsage().RetainedBytes != 0)
						{
							Invalidated = true;
							if (ReplacePolicy)
							{
								Vm.SetJitPolicy({});
							}
							else
							{
								Vm.InvalidateJitCache();
							}
							RetainedDuringCheck = Probe->Retirements == 0;
						}
						return !(Armed && Stop && Invalidated);
					});
					if (Reuse)
					{
						if (!Expect(Vm.Execute(*Module, ERunMode::Script) && Output.str() == "7\n",
						        "preparation fixture warms the retained cache"))
						{
							return false;
						}
						Output.str({});
						Output.clear();
					}
					Armed = true;
					const bool Ok = Vm.Execute(*Module, ERunMode::Script);
					if (!Expect(Ok == !Stop && Invalidated && RetainedDuringCheck && Probe->Retirements == 1 && Vm.GetJitResourceUsage().RetainedBytes == 0 && Output.str() == (Stop ? "" : "7\n"),
					        "preparation retains its cache across checks without restoring an invalidated cache"))
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	bool CheckAotNativeBindingIdentity()
	{
		FNativeFunction Native;
		Native.Name = "host_cast";
		Native.ReturnType = ENativeType::HostObject;
		Native.ParamTypes = {ENativeType::HostObject};
		Native.ReturnHostTypeName = "object";
		Native.ParamHostTypeNames = {"object"};
		Native.TypeParams = {"T", "U"};
		Native.ScriptReturnTypeName = "T";
		Native.bGenericHostObjectCast = true;
		const auto Digest = ComputeAotNativeBindingsDigest({Native});
		auto Compiled = VeroScriptCompile::CompileSource("void main(){}", "aot-binding-identity.vs");
		if (!Expect(Compiled.bSuccess, "AOT identity fixture compiles")) return false;
		std::string Error;
		FAotProgramDescription Description;
		Description.NativeBindingsDigest = Digest;
		Description.ServiceAbi = GetAotServiceAbi();
		Description.CoreImplementation = VEROSCRIPT_CORE_IMPLEMENTATION_ID;
		Description.Target = GetHostPlatformInfo().Platform;
		if (!Expect(ComputeAotProgramDigest(*Compiled.Module, Description.ProgramDigest, Error),
		        "AOT identity fixture has a bytecode digest"))
		{
			return false;
		}
		const auto Program = CreateAotProgram(std::move(Description), {}, Error);
		if (!Expect(Program && ValidateAotProgram(*Program, *Compiled.Module, {Native}, Error),
		        "matching AOT native bindings validate"))
		{
			return false;
		}
		auto Mismatched = Program->GetDescription();
		Mismatched.CoreVersion = VEROSCRIPT_VERSION_CODE + 1;
		if (!Expect(!CreateAotProgram(std::move(Mismatched), {}, Error),
		        "AOT rejects a different Core product version"))
		{
			return false;
		}
		const auto Changed = [&](const FNativeFunction& Other)
		{
			return Expect(ComputeAotNativeBindingsDigest({Other}) != Digest && !ValidateAotProgram(*Program, *Compiled.Module, {Other}, Error) && Error == "AOT bytecode or native binding identity mismatch",
			    "AOT rejects a changed native runtime contract");
		};
		auto Other = Native;
		Other.TypeParams.pop_back();
		if (!Changed(Other)) return false;
		Other = Native;
		std::swap(Other.TypeParams[0], Other.TypeParams[1]);
		if (!Changed(Other)) return false;
		Other = Native;
		Other.ScriptReturnTypeName = "U";
		if (!Changed(Other)) return false;
		Other = Native;
		Other.ReturnGenericTemplateId = 1;
		if (!Changed(Other)) return false;
		Other = Native;
		Other.ReturnGenericTypeArguments = {"int"};
		if (!Changed(Other)) return false;
		Other = Native;
		Other.ReturnType = ENativeType::Int64;
		Other.RuntimeReturnTypeName = "object";
		if (!Changed(Other)) return false;
		Other = Native;
		Other.ParamTypes[0] = ENativeType::Int64;
		Other.RuntimeParamTypeNames = {"object"};
		if (!Changed(Other)) return false;
		Other = Native;
		Other.ParamFlags = {0};
		Other.UserData = &Other;
		Other.ProviderIdentity = std::make_shared<int>(7);
		if (!Expect(ComputeAotNativeBindingsDigest({Other}) == Digest && ValidateAotProgram(*Program, *Compiled.Module, {Other}, Error),
		        "AOT binding identity normalizes value flags and excludes process-local state"))
		{
			return false;
		}
		std::string Interrupted = "stale";
		return Expect(!ComputeAotNativeBindingsDigest({Native}, Interrupted, Error,
		                  {+[](void*) noexcept
		{
			return false;
		},
		                      nullptr}) &&
		        Interrupted.empty() && Error == "AOT validation stopped by host",
		    "AOT binding hashing preserves cancellation without a partial digest");
	}

	std::vector<FCompiledInstruction> PartialAotChildPlan;
	std::size_t PartialAotChildEntries = 0;

	int ExecutePartialAotChild(void* Context)
	{
		++PartialAotChildEntries;
		auto& Services = *static_cast<ICompiledFunctionServices*>(Context);
		for (const auto& Instruction : PartialAotChildPlan)
		{
			const auto Result = Services.Execute(Instruction.Offset);
			if (Result != 0) return Result;
		}
		return -1;
	}

	bool CheckVmCostPreferenceWithPartialAot()
	{
		auto Compilation = VeroScriptCompile::CompileSource(
		    "int child(int n){return n+1;}int main(){int sum=0;for(int i=0;i<3;i++){sum+=child(i);}return sum;}",
		    "partial-aot-vm-cost.vs");
		if (!Expect(Compilation.bSuccess && Compilation.Module->MainFunctionIndex >= 0 &&
						Compilation.Module->NativeReferences.empty(),
				"partial AOT cost fixture compiles"))
		{
			return false;
		}
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compilation.Module);
		// Resolve the actual call operand rather than assuming an unmangled name.
		std::size_t ChildIndex = Module->Functions.size();
		const auto& MainCode = Module->Functions[Module->MainFunctionIndex].Code;
		for (std::size_t Offset = 0; Offset < MainCode.size();)
		{
			const auto Opcode = static_cast<EOpcode>(MainCode[Offset]);
			const auto Size = Bytecode::InstructionSize(Opcode);
			if (!Size || Size > MainCode.size() - Offset) return false;
			if (Opcode == EOpcode::Call)
			{
				ChildIndex = Bytecode::ReadU16LE(MainCode.data() + Offset + 1u);
			}
			Offset += Size;
		}
		std::string Error;
		if (!Expect(ChildIndex < Module->Functions.size(), "partial AOT fixture resolves its child call")) return false;
		const auto Child = static_cast<std::uint16_t>(ChildIndex);
		if (!Expect(BuildCompiledFunctionPlan(Module->Functions[Child], PartialAotChildPlan, Error), Error.c_str())) return false;
		FAotProgramDescription Description;
		Description.ServiceAbi = GetAotServiceAbi();
		Description.CoreImplementation = VEROSCRIPT_CORE_IMPLEMENTATION_ID;
		Description.Target = GetHostPlatformInfo().Platform;
		std::vector<FNativeFunction> NativeStorage;
		Description.NativeBindingsDigest = ComputeAotNativeBindingsDigest(*StandardLibrary::MergeNativeFunctions(nullptr, NativeStorage));
		if (!ComputeAotProgramDigest(*Module, Description.ProgramDigest, Error)) return false;
		const auto Program = CreateAotProgram(std::move(Description), {{Child, &ExecutePartialAotChild, ControlledExecutionCapabilities}}, Error);
		if (!Expect(Program != nullptr, Error.c_str())) return false;
		const auto ObservedProgram = CreateAotProgram(Program->GetDescription(),
		    {{Child, &ExecutePartialAotChild, ControlledExecutionCapabilities | ExecutionObservationCapability}}, Error);
		if (!Expect(ObservedProgram != nullptr, Error.c_str())) return false;
		struct FResult
		{
			bool Ok;
			std::string Error;
			std::uint64_t Count;
			std::uint32_t Line, Column;
			int ExitCode;
			std::size_t AotEntries;
			std::size_t BackendEffects;
			FJitStats Stats;
		};
		const auto Run = [&](const EExecutionBackend Mode, const bool AttachAot, const std::uint64_t Limit,
		    const bool Observe = false, const bool AotSupportsObservation = false)
		{
			PartialAotChildEntries = 0;
			auto Probe = std::make_shared<FProbe>();
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			Test::Configure(Vm);
			Vm.SetExecutionBackend(Mode);
			Vm.SetExecutionLimits({Limit, 0});
			Vm.SetExecutionControl(std::make_shared<FExecutionControl>());
			if (Observe)
			{
				Vm.SetMutationCallback([](const FMutationEvent &)
					{
					});
			}
			// This fixture exercises a JIT cost artifact beside a partial AOT map.
			// Governed Mixed otherwise deliberately disables dynamic compilation.
			FJitPolicy Policy;
			Policy.bEnableControlledMixed = true;
			Vm.SetJitPolicy(Policy);
			if (!Vm.SetJitBackendFactory(std::make_shared<FFactory>(EBehavior::PreferVmSuppress, Probe), Error) ||
			    !Vm.SetJitProgramOwner(Module, Error) ||
			    (AttachAot && !Vm.SetAotProgram(AotSupportsObservation ? ObservedProgram : Program, Error)))
			{
				throw std::runtime_error(Error);
			}
			const bool Ok = Vm.Execute(*Module, ERunMode::Script);
			return FResult{Ok, Vm.HasError() ? Vm.GetError().Message : std::string{}, Vm.GetExecutedInstructionCount(),
			    Vm.HasError() ? Vm.GetError().Location.Line : 0, Vm.HasError() ? Vm.GetError().Location.Column : 0,
			    Vm.GetProcessExitCode(), PartialAotChildEntries, Probe->Effects.load(), Vm.GetJitStats()};
		};
		const auto Base = Run(EExecutionBackend::Vm, false, 0);
		if (!Expect(Base.Ok && Base.ExitCode == 6, "partial AOT cost fixture result")) return false;
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			for (const bool AttachAot : {false, true})
			{
				const auto Result = Run(Mode, AttachAot, 0);
				const bool UsesAot = AttachAot && Mode == EExecutionBackend::Mixed;
				if (!Expect(Result.Ok && Result.ExitCode == 6 && Result.Count == Base.Count && Result.BackendEffects == 0 &&
				        Result.AotEntries == (UsesAot ? 3u : 0u) && Result.Stats.AotExecutions == Result.AotEntries &&
				        (Mode == EExecutionBackend::Vm || Result.Stats.CostModelVmFallbacks != 0),
				        "VM loop cost hints must preserve eligible partial AOT callees and ordinary VM/JIT selection"))
				{
					std::cerr << "partial AOT mode=" << static_cast<int>(Mode) << " attach=" << AttachAot
					          << " ok=" << Result.Ok << " exit=" << Result.ExitCode << " count=" << Result.Count
					          << " reference-count=" << Base.Count << " entries=" << Result.AotEntries
					          << " stats-aot=" << Result.Stats.AotExecutions << " cost-vm=" << Result.Stats.CostModelVmFallbacks
					          << " error=" << Result.Error << '\n';
					return false;
				}
			}
		}
		for (std::uint64_t Limit = 1; Limit <= Base.Count + 1; ++Limit)
		{
			const auto Reference = Run(EExecutionBackend::Vm, false, Limit);
			const auto Selected = Run(EExecutionBackend::Mixed, true, Limit);
			if (!Expect(Reference.Ok == Selected.Ok && Reference.Count == Selected.Count &&
			        Reference.Error == Selected.Error && Reference.Line == Selected.Line && Reference.Column == Selected.Column &&
			        Reference.ExitCode == Selected.ExitCode, "partial AOT under VM cost fallback changed a quota prefix"))
			{
				std::cerr << "partial AOT quota=" << Limit << " VM=" << Reference.Ok << ',' << Reference.Count << ','
				          << Reference.Error << " AOT=" << Selected.Ok << ',' << Selected.Count << ',' << Selected.Error
				          << " entries=" << Selected.AotEntries << '\n';
				return false;
			}
		}
		for (const bool AotSupportsObservation : {false, true})
		{
			const auto Reference = Run(EExecutionBackend::Vm, false, 0, true);
			const auto Selected = Run(EExecutionBackend::Mixed, true, 0, true, AotSupportsObservation);
			if (!Expect(Reference.Ok && Selected.Ok && Reference.ExitCode == Selected.ExitCode &&
			        Reference.Count == Selected.Count && Selected.BackendEffects == 0 &&
			        Selected.AotEntries == (AotSupportsObservation ? 3u : 0u) &&
			        Selected.Stats.AotExecutions == Selected.AotEntries &&
			        Selected.Stats.CostModelVmFallbacks >= (AotSupportsObservation ? 1u : 4u),
			        "AOT capability rejection must honor first and cached JIT cost hints without activating their executable"))
			{
				std::cerr << "partial AOT observation-compatible=" << AotSupportsObservation
				          << " ok=" << Selected.Ok << " exit=" << Selected.ExitCode << " count=" << Selected.Count
				          << " reference-count=" << Reference.Count << " entries=" << Selected.AotEntries
				          << " backend-effects=" << Selected.BackendEffects << " cost-vm=" << Selected.Stats.CostModelVmFallbacks
				          << " error=" << Selected.Error << '\n';
				return false;
			}
			for (std::uint64_t Limit = 1; Limit <= Reference.Count + 1; ++Limit)
			{
				const auto Vm = Run(EExecutionBackend::Vm, false, Limit, true);
				const auto Mixed = Run(EExecutionBackend::Mixed, true, Limit, true, AotSupportsObservation);
				if (!Expect(Vm.Ok == Mixed.Ok && Vm.Count == Mixed.Count && Vm.Error == Mixed.Error &&
								Vm.Line == Mixed.Line && Vm.Column == Mixed.Column && Vm.ExitCode == Mixed.ExitCode &&
								Mixed.BackendEffects == 0,
						"AOT observation capability fallback changed a quota prefix"))
				{
					return false;
				}
			}
		}
		PartialAotChildPlan.clear();
		return true;
	}
}

int main()
{
	const FSemanticVersion RuntimeVersion{
	    static_cast<std::uint16_t>(VEROSCRIPT_VERSION_CODE >> 24),
	    static_cast<std::uint16_t>((VEROSCRIPT_VERSION_CODE >> 16) & 0xff),
	    static_cast<std::uint16_t>(VEROSCRIPT_VERSION_CODE & 0xffff)};
	if (!Expect(RuntimeVersion == VEROSCRIPT_VERSION && FJitModuleDescriptor{}.CoreVersion == VEROSCRIPT_VERSION_CODE && FJitBackendDescription{}.CoreVersion == VEROSCRIPT_VERSION_CODE && FAotProgramDescription{}.CoreVersion == VEROSCRIPT_VERSION_CODE,
	        "JIT and AOT descriptors use the product version"))
	{
		return 1;
	}
	if (!Expect(std::string(GetJitCapabilityString()) == "backend-services",
	        "JIT capability identification has no separate version"))
	{
		return 1;
	}
	if (!CheckPreparationInvalidation()) return 1;
	std::string Error;
	if (!CheckAotNativeBindingIdentity()) return 1;
	if (!CheckVmCostPreferenceWithPartialAot()) return 1;
	{
		auto Probe = std::make_shared<FProbe>();
		FJitPolicy Policy;
		Policy.MaxCachedEntries = 1;
		auto Cache = MakeCache(EBehavior::Return, Probe, MakeProgram(), Policy);
		if (!Expect(Cache->EnsureCompiled(0, {}, ExecutionInstructionCapability) == EJitCompilationResult::Unsupported &&
		            Cache->GetStats().UnsupportedEntries == 1 &&
		            Cache->EnsureCompiled(1, {}, ExecutionInstructionCapability) == EJitCompilationResult::Deferred &&
		            Probe->Compiles == 1 && Probe->Publications == 0 && Probe->LiveCode == 0,
		        "missing governance is a cached unsupported decision within the entry budget"))
		{
			return 1;
		}
		auto Warm = MakeCache(EBehavior::Return, std::make_shared<FProbe>());
		if (!Expect(Warm->EnsureCompiled(0) == EJitCompilationResult::Ready &&
		            Warm->EnsureCompiled(0, {}, ExecutionInstructionCapability) == EJitCompilationResult::Unsupported,
		        "a ready uncontrolled artifact is not ready for a governed request"))
		{
			return 1;
		}
	}
	if (!Expect(!FJitBackendCache::Create(nullptr, MakeProgram(), {}, {}, 0, Error) && Error.empty(), "null factory means no backend"))
	{
		return 1;
	}
	for (const auto Behavior : {EBehavior::CompileFailure, EBehavior::CompileThrow, EBehavior::InvalidArtifact,
	         EBehavior::ForeignOwner, EBehavior::PublishFailure})
	{
		auto Probe = std::make_shared<FProbe>();
		auto Cache = MakeCache(Behavior, Probe);
		if (!Expect(Cache->EnsureCompiled(0) == EJitCompilationResult::Failed && !Cache->Find(0) && !Cache->GetLastError().empty() && Probe->Publications == 0,
		        "compile failure is diagnosed and never published"))
		{
			return 1;
		}
	}
	{
		auto Probe = std::make_shared<FProbe>();
		FJitResourceLimits Limits;
		Limits.ExecutableBytes = 1;
		auto Cache = FJitBackendCache::Create(std::make_shared<FFactory>(EBehavior::Return, Probe),
		    MakeProgram(), {}, Limits, 0, Error);
		if (!Expect(Cache && Cache->EnsureCompiled(0) == EJitCompilationResult::Deferred && !Cache->Find(0) && Probe->Publications == 0 && Probe->Discards == 1 && Probe->LiveCode == 0 && !Cache->GetFunctionReason(0).empty(), "over-budget candidate is discarded before publication"))
		{
			return 1;
		}
	}
	{
		auto Probe = std::make_shared<FProbe>();
		auto Cache = MakeCache(EBehavior::Unsupported, Probe);
		if (!Expect(Cache->EnsureCompiled(0) == EJitCompilationResult::Unsupported && Cache->EnsureCompiled(0) == EJitCompilationResult::Unsupported && Probe->Compiles == 1, "unsupported decisions are cached"))
		{
			return 1;
		}
	}
	{
		auto Probe = std::make_shared<FProbe>();
		auto Cache = MakeCache(EBehavior::Defer, Probe, MakeProgram(), FJitPolicy{1, 2, 8, 8});
		Cache->EnsureCompiled(0);
		Cache->EnsureCompiled(0);
		if (!Expect(Probe->Compiles == 1, "deferred calls do not spin")) return 1;
		Cache->BeginRun();
		Cache->EnsureCompiled(0);
		Cache->BeginRun();
		Cache->EnsureCompiled(0);
		if (!Expect(Probe->Compiles == 2, "retry respects the cumulative compile quota")) return 1;
	}
	for (const auto Behavior : {EBehavior::Unsupported, EBehavior::Defer, EBehavior::CompileFailure})
	{
		for (const auto DiagnosticBudget : {std::size_t{64}, std::size_t{8192}})
		{
			auto Probe = std::make_shared<FProbe>();
			Probe->CompileReason.assign(4096, 'x');
			FJitResourceLimits Limits;
			Limits.RetainedBytes = FJitBackendCache::MinimumRetainedBytes(3) + DiagnosticBudget;
			auto Cache = FJitBackendCache::Create(std::make_shared<FFactory>(Behavior, Probe), MakeProgram(), {},
			    Limits, 0, Error);
			if (!Expect(Cache && Cache->GetResourceUsage().RetainedBytes <= Limits.RetainedBytes,
			        "diagnostic budget probe starts within its retention limit"))
			{
				return 1;
			}

			const auto Result = Cache->EnsureCompiled(0);
			const auto Expected = Behavior == EBehavior::Unsupported ? EJitCompilationResult::Unsupported
			    : Behavior == EBehavior::Defer                       ? EJitCompilationResult::Deferred
			                                                         : EJitCompilationResult::Failed;
			const auto Usage = Cache->GetResourceUsage();
			const auto Reason =
			    Behavior == EBehavior::CompileFailure ? Cache->GetLastError() : Cache->GetFunctionReason(0);
			if (!Expect(Result == Expected && Usage.RetainedBytes <= Limits.RetainedBytes &&
			            Usage.bResourceLimitReached == (DiagnosticBudget < Probe->CompileReason.size()) &&
			            !Cache->Find(0) && Probe->Publications == 0 && !Reason.empty() &&
			            (DiagnosticBudget < Probe->CompileReason.size() || Reason == Probe->CompileReason),
			        "backend diagnostics respect retention limits and preserve fallback/error state"))
			{
				return 1;
			}
		}
	}
	{
		auto MeasurementProbe = std::make_shared<FProbe>();
		MeasurementProbe->CompileReason.assign(4096, 'x');
		auto MeasurementCache = MakeCache(EBehavior::DeferOnce, MeasurementProbe);
		const auto Baseline = MeasurementCache->GetResourceUsage().RetainedBytes;
		if (!Expect(MeasurementCache->EnsureCompiled(0) == EJitCompilationResult::Deferred,
		        "measure a retained deferred diagnostic"))
		{
			return 1;
		}

		FJitResourceLimits Limits;
		Limits.RetainedBytes = MeasurementCache->GetResourceUsage().RetainedBytes + 64;
		auto Probe = std::make_shared<FProbe>();
		Probe->CompileReason = MeasurementProbe->CompileReason;
		auto Cache = FJitBackendCache::Create(std::make_shared<FFactory>(EBehavior::DeferOnce, Probe), MakeProgram(),
		    {}, Limits, 0, Error);
		if (!Expect(Cache && Cache->EnsureCompiled(0) == EJitCompilationResult::Deferred &&
		            Cache->EnsureCompiled(0) == EJitCompilationResult::Deferred && Probe->Compiles == 1 &&
		            Cache->GetFunctionReason(0) == Probe->CompileReason,
		        "a deferred diagnostic remains available until a retry is eligible"))
		{
			return 1;
		}

		Cache->BeginRun();
		if (!Expect(Cache->EnsureCompiled(0) == EJitCompilationResult::Ready && Cache->Find(0) &&
		            Probe->Compiles == 2 && Probe->Publications == 1 && Cache->GetFunctionReason(0).empty() &&
		            Cache->GetResourceUsage().RetainedBytes < Baseline + 4096 &&
		            !Cache->GetResourceUsage().bResourceLimitReached,
		        "a retry releases its obsolete diagnostic before budgeting and publishing code"))
		{
			return 1;
		}
	}
	{
		auto Probe = std::make_shared<FProbe>();
		Probe->CompileReason.assign(4096, 'x');
		auto Cache = MakeCache(EBehavior::CompileFailureOnce, Probe);
		const auto Baseline = Cache->GetResourceUsage().RetainedBytes;
		if (!Expect(Cache->EnsureCompiled(0) == EJitCompilationResult::Failed &&
		            Cache->GetLastError() == Probe->CompileReason &&
		            Cache->GetResourceUsage().RetainedBytes > Baseline,
		        "a failed compilation retains its diagnostic until the next run"))
		{
			return 1;
		}

		Cache->BeginRun();
		if (!Expect(Cache->GetLastError().empty() && Cache->GetResourceUsage().RetainedBytes == Baseline &&
		            Cache->EnsureCompiled(0) == EJitCompilationResult::Ready,
		        "a new run releases the expired failure diagnostic before retrying"))
		{
			return 1;
		}
	}
	// Exercise the actual VM dispatch boundary with a public-contract backend.
	// VM bytecode returns 99; successful probe code returns 42, proving selection.
	for (const auto Behavior : {EBehavior::Return, EBehavior::PreferVm, EBehavior::Unsupported, EBehavior::Refuse, EBehavior::NestedCall,
	         EBehavior::DirtyRefuse, EBehavior::ExecuteThrow, EBehavior::WrongReturn, EBehavior::CompileFailure,
	         EBehavior::Interpret, EBehavior::OptimizedScope, EBehavior::LeakedOptimizedScope,
	         EBehavior::Frame, EBehavior::FrameNestedCall, EBehavior::FrameReference, EBehavior::StaleFrame,
	         EBehavior::LeakedFrame, EBehavior::FrameThrow, EBehavior::ValueOperations, EBehavior::FrameExit})
	{
		auto Program = MakeProgram();
		auto Module = std::const_pointer_cast<FBytecodeModule>(Program.Owner);
		FBytecodeConstant Constant;
		Constant.I32 = 99;
		Module->Constants.push_back(Constant);
		for (auto& Function : Module->Functions)
		{
			Function.Name = "probe_function";
			Function.LocalCount = 1;
			Function.Code.push_back(static_cast<std::uint8_t>(EOpcode::ConstI32));
			Bytecode::WriteU16LE(Function.Code, 0);
			Function.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
		}
		if (Behavior == EBehavior::Interpret)
		{
			auto& Code = Module->Functions[0].Code;
			Code = {static_cast<std::uint8_t>(EOpcode::Call)};
			Bytecode::WriteU16LE(Code, 1);
			Code.push_back(0);
			Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
		}
		if (Behavior == EBehavior::FrameReference)
		{
			Module->Functions[0].bReturnsRef = true;
			Module->Functions[0].ClosureTypeName = "fn()->ref int";
		}
		auto Probe = std::make_shared<FProbe>();
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		Vm.SetExecutionBackend(EExecutionBackend::Jit);
		Vm.SetMaxCallDepth(2);
		if (!Vm.SetJitBackendFactory(std::make_shared<FFactory>(Behavior, Probe), Error) || !Vm.SetJitProgramOwner(Module, Error))
		{
			return 1;
		}
		FValue Result;
		std::vector<FNativeFunction> Natives;
		if (Behavior == EBehavior::ValueOperations)
		{
			Module->StringPool = {"value", "array<int>", "push"};
			FBytecodeTypeDesc Type;
			Type.Kind = EScriptObjectKind::Struct;
			Type.Name = "ProbeStruct";
			Type.DestructorFunctionIndex = 2;
			auto& Destructor = Module->Functions[2];
			Destructor.bIsMethod = true;
			Destructor.bReturnsVoid = true;
			Destructor.ClosureTypeName = "fn()->void";
			Destructor.Code.clear();
			Destructor.Code.push_back(static_cast<std::uint8_t>(EOpcode::CallNative));
			Bytecode::WriteU16LE(Destructor.Code, 1);
			Destructor.Code.push_back(0);
			Destructor.Code.push_back(static_cast<std::uint8_t>(EOpcode::ConstI32));
			Bytecode::WriteU16LE(Destructor.Code, 0);
			Destructor.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
			FBytecodeFieldDesc Field;
			Field.Name = "value";
			Field.TypeName = "int";
			Field.Access = EMemberAccess::Public;
			Type.Fields.push_back(std::move(Field));
			Module->Types.push_back(std::move(Type));
			FBytecodeTypeDesc OtherType;
			OtherType.Name = "ProbeOther";
			FBytecodeFieldDesc OtherField;
			OtherField.Name = "padding";
			OtherField.TypeName = "int";
			OtherField.Access = EMemberAccess::Public;
			OtherType.Fields.push_back(OtherField);
			OtherField.Name = "value";
			OtherType.Fields.push_back(std::move(OtherField));
			Module->Types.push_back(std::move(OtherType));
			FNativeFunction Native;
			Native.Name = "probe_native";
			Native.ReturnType = ENativeType::Int;
			Native.UserData = &Vm;
			Native.Callback = [](const FNativeCallContext& Call, FValue& Out, std::string&) -> bool
			{
				auto& CallbackVm = *static_cast<VeroScriptVm*>(Call.UserData);
				CallbackVm.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext&, FValue&, FScriptError&)
				{
					return EInstanceCallInterceptResult::Unhandled;
				});
				CallbackVm.SetMutationCallback([](const FMutationEvent&)
				{
				});
				Out = std::int32_t{7};
				return true;
			};
			Natives.push_back(std::move(Native));
			FNativeFunction Destruction;
			Destruction.Name = "probe_destroyed";
			Destruction.UserData = Probe.get();
			Destruction.Callback = [](const FNativeCallContext& Call, FValue&, std::string&) -> bool
			{
				++static_cast<FProbe*>(Call.UserData)->Destructions;
				return true;
			};
			Natives.push_back(std::move(Destruction));
			FNativeFunction ClearCallbacks;
			ClearCallbacks.Name = "probe_clear_callbacks";
			ClearCallbacks.ReturnType = ENativeType::Int;
			ClearCallbacks.UserData = &Vm;
			ClearCallbacks.Callback = [](const FNativeCallContext& Call, FValue& Out, std::string&) -> bool
			{
				auto& CallbackVm = *static_cast<VeroScriptVm*>(Call.UserData);
				CallbackVm.SetInstanceCallInterceptor({});
				CallbackVm.SetMutationCallback({});
				Out = std::int32_t{7};
				return true;
			};
			Natives.push_back(std::move(ClearCallbacks));
		}
		const bool bOk = Vm.ExecuteFunction(*Module, 0, {}, &Natives, &Result);
		const bool bExpectedOk = Behavior == EBehavior::Return || Behavior == EBehavior::PreferVm || Behavior == EBehavior::Unsupported || Behavior == EBehavior::Refuse || Behavior == EBehavior::NestedCall || Behavior == EBehavior::Frame || Behavior == EBehavior::FrameNestedCall || Behavior == EBehavior::FrameReference || Behavior == EBehavior::ValueOperations || Behavior == EBehavior::FrameExit || Behavior == EBehavior::Interpret || Behavior == EBehavior::OptimizedScope;
		if (bOk != bExpectedOk || (!bOk && !Vm.HasError()))
		{
			std::cerr << "VM probe " << static_cast<int>(Behavior) << ": " << Vm.GetError().Message << '\n';
		}
		if (!Expect(bOk == bExpectedOk && (bOk || Vm.HasError()), "VM preserves backend failure outcomes")) return 1;
		if (Behavior == EBehavior::FrameExit && !Expect(Vm.WasExitRequested() && Vm.GetProcessExitCode() == 7, "exit propagates through a materialized backend frame")) return 1;
		if (bExpectedOk && Behavior != EBehavior::FrameExit && !Expect(std::get<std::int32_t>(Result) == (Behavior == EBehavior::Return || Behavior == EBehavior::Frame || Behavior == EBehavior::FrameReference || Behavior == EBehavior::ValueOperations ? 42 : 99), "VM uses the selected backend and executes fallback or nested VM exactly once"))
		{
			return 1;
		}
		if (Behavior == EBehavior::ValueOperations && !Expect(Probe->Destructions == 2, "value services release the original and explicit struct copy exactly once"))
		{
			return 1;
		}
		if (!Expect(Probe->Effects == (Behavior == EBehavior::Refuse || Behavior == EBehavior::PreferVm || Behavior == EBehavior::Unsupported || Behavior == EBehavior::CompileFailure ? 0 : 1),
		        "VM never replays an entry after observable effects"))
		{
			return 1;
		}
		if (Behavior == EBehavior::PreferVm && !Expect(Vm.GetJitStats().CostModelVmFallbacks == 1, "a cached VM preference declines before backend activation"))
		{
			return 1;
		}
	}
	{
		auto Program = MakeProgram();
		auto Module = std::const_pointer_cast<FBytecodeModule>(Program.Owner);
		Module->Constants.push_back({});
		Module->Constants[0].I32 = 99;
		Module->Functions[0].Code = {static_cast<std::uint8_t>(EOpcode::ConstI32), 0, 0, static_cast<std::uint8_t>(EOpcode::Return)};
		auto Probe = std::make_shared<FProbe>();
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		Vm.SetExecutionBackend(EExecutionBackend::Jit);
		if (!Vm.SetJitBackendFactory(std::make_shared<FFactory>(EBehavior::Return, Probe), Error) || !Vm.SetJitProgramOwner(Module, Error))
		{
			return 1;
		}
		FValue Result;
		std::vector<FNativeFunction> Natives(1);
		if (!Vm.ExecuteFunction(*Module, 0, {}, &Natives, &Result)) return 1;
		const auto Before = Probe->Instances.load();
		Natives[0].Name = "changed_binding";
		if (!Expect(Vm.ExecuteFunction(*Module, 0, {}, &Natives, &Result) && Probe->Instances == Before + 1,
		        "a mutable native table cannot reuse code from a previous binding generation"))
		{
			return 1;
		}
	}
	for (const bool bFailCallback : {false, true})
	{
		auto Module = std::make_shared<FBytecodeModule>();
		Module->InitCode = {static_cast<std::uint8_t>(EOpcode::CallNative), 0, 0, 0};
		auto Probe = std::make_shared<FProbe>();
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		Vm.SetExecutionBackend(EExecutionBackend::Jit);
		if (!Vm.SetJitBackendFactory(std::make_shared<FFactory>(EBehavior::Interpret, Probe), Error) || !Vm.SetJitProgramOwner(Module, Error))
		{
			return 1;
		}
		struct FInvalidationContext
		{
			VeroScriptVm& Vm;
			bool bFail;
			std::size_t Calls = 0;
		};
		FInvalidationContext CallbackContext{Vm, bFailCallback};
		FNativeFunction Native;
		Native.Name = "invalidate_eval_cache";
		Native.UserData = &CallbackContext;
		Native.Callback = [](const FNativeCallContext& Call, FValue&, std::string& OutError) -> bool
		{
			auto& Callback = *static_cast<FInvalidationContext*>(Call.UserData);
			++Callback.Calls;
			Callback.Vm.InvalidateJitCache();
			if (Callback.bFail)
			{
				OutError = "callback failure after cache invalidation";
				return false;
			}
			return true;
		};
		const std::vector<FNativeFunction> Natives{Native};
		const bool bOk = Vm.Execute(*Module, ERunMode::EvalOnce, {}, &Natives);
		const bool bValid = bOk == !bFailCallback && Vm.HasError() == bFailCallback && CallbackContext.Calls == 1 && Probe->Effects == 1 && Probe->Instances == 2 && Probe->Retirements == 2 && Probe->LiveCode == 0 && Vm.GetJitResourceUsage().RetainedBytes == 0;
		if (!bValid)
		{
			std::cerr << "Eval invalidation probe: success=" << bOk << ", error=" << Vm.HasError()
			          << ", callbacks=" << CallbackContext.Calls << ", effects=" << Probe->Effects
			          << ", instances=" << Probe->Instances << ", retirements=" << Probe->Retirements
			          << ", live code=" << Probe->LiveCode << ", diagnostic=" << Vm.GetError().Message << '\n';
		}
		if (!Expect(bValid,
		        "eval restores execution safely without resurrecting a cache invalidated by a host callback"))
		{
			return 1;
		}
	}
	for (const auto Behavior : {EBehavior::Return, EBehavior::Refuse, EBehavior::DirtyRefuse,
	         EBehavior::ExecuteThrow, EBehavior::ControlTransfer, EBehavior::MissingDiagnostic, EBehavior::WrongReturn})
	{
		auto Probe = std::make_shared<FProbe>();
		auto Cache = MakeCache(Behavior, Probe);
		Cache->EnsureCompiled(0);
		FServices Services;
		FJitCallContext Context;
		Context.Services = &Services;
		const auto Result = Cache->Invoke(0, Context);
		const auto Expected = Behavior == EBehavior::Return ? EJitInvocationResult::Returned
		    : Behavior == EBehavior::Refuse                 ? EJitInvocationResult::NotEntered
		    : Behavior == EBehavior::ControlTransfer        ? EJitInvocationResult::ControlTransfer
		                                                    : EJitInvocationResult::Failed;
		if (!Expect(Result == Expected && Probe->Effects == (Behavior == EBehavior::Refuse ? 0 : 1),
		        "entry outcome preserves side effects without replay"))
		{
			return 1;
		}
	}
	{
		auto Probe = std::make_shared<FProbe>();
		auto Program = MakeProgram(true);
		std::weak_ptr<const FBytecodeModule> Module = Program.Owner;
		auto Cache = MakeCache(EBehavior::Return, Probe, std::move(Program));
		Cache->EnsureCompiled(0);
		auto Lease = Cache->Find(0);
		FServices Services;
		FJitCallContext Context;
		Context.Services = &Services;
		if (!Expect(Cache->Invoke(0, Context) == EJitInvocationResult::Returned && !Context.bHasReturn,
		        "void entry does not synthesize a second stack result"))
		{
			return 1;
		}
		Cache.reset();
		if (!Expect(Probe->LiveCode == 1 && Probe->Retirements == 1 && !Module.expired(),
		        "leased code and module survive cache retirement"))
		{
			return 1;
		}
		Lease.reset();
		if (!Expect(Probe->LiveCode == 0 && Module.expired(), "last lease releases code and module")) return 1;
	}
	{
		auto Probe = std::make_shared<FProbe>();
		auto Factory = std::make_shared<FFactory>(EBehavior::Return, Probe);
		Factory->Description.CoreVersion = VEROSCRIPT_VERSION_CODE + 1;
		if (!Expect(!FJitBackendCache::Create(Factory, MakeProgram(), {}, {}, 0, Error) && !Error.empty() && Probe->Instances == 0, "a different Core product version is rejected before creation"))
		{
			return 1;
		}
	}
	{
		auto Probe = std::make_shared<FProbe>();
		auto First = MakeCache(EBehavior::Return, Probe);
		auto Second = MakeCache(EBehavior::Return, Probe);
		First->EnsureCompiled(0);
		if (!Expect(!Second->Find(0) && Probe->Instances == 2, "execution domains do not share caches")) return 1;
		FServices Services;
		FJitCallContext Context;
		Context.Services = &Services;
		Context.Program = &Second->GetProgram();
		if (!Expect(First->Invoke(0, Context) == EJitInvocationResult::Failed && Probe->Effects == 0,
		        "program identity mismatch is rejected before execution"))
		{
			return 1;
		}
	}
	{
		auto Cache = MakeCache(EBehavior::Return, std::make_shared<FProbe>());
		FJitStats Transient;
		Transient.ControlledNativeExecutions = 3;
		Transient.AotExecutions = 2;
		Cache->AccumulateTransientStatistics(Transient);
		if (!Expect(Cache->GetStats().ControlledNativeExecutions == 3 && Cache->GetStats().AotExecutions == 2,
		        "transient native entry counts are retained"))
		{
			return 1;
		}
		Cache->ResetExecutionStatistics();
		if (!Expect(Cache->GetStats().ControlledNativeExecutions == 0 && Cache->GetStats().AotExecutions == 0,
		        "reset clears controlled and AOT transient entry counts"))
		{
			return 1;
		}
	}
	std::cout << "JIT public contract checks passed\n";
	return 0;
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptVm.h"
#include "VeroScriptVmJitDiagnostics.h"
#include "VeroScriptVmScalarCalls.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptVmCleanup.h"
#include "VeroScriptVmReferenceStorage.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptJit.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptConsoleNative.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptVm::InvokeCallable(
	    const FFunctionHandle& Callable,
	    std::vector<FValue>& InOutArguments,
	    FValue& OutReturn,
	    std::string& OutError)
	{
		OutError.clear();
		if (!Callable)
		{
			OutError = "cannot invoke an unbound script callable";
			return false;
		}
		if (!ActiveModule || Callable->ProgramGeneration != ProgramGeneration || Callable->ProgramOwner != ProgramOwner || Callable->FunctionIndex >= ActiveModule->Functions.size())
		{
			OutError = "script callable is stale or belongs to another module";
			return false;
		}
		const FBytecodeFunction& Target = ActiveModule->Functions[Callable->FunctionIndex];
		if (Callable->FunctionName.empty() || Callable->FunctionName != Target.Name || Callable->TypeName != Target.ClosureTypeName)
		{
			OutError = "script callable identity no longer matches the loaded module";
			return false;
		}
		const FValue CallableKeep = Callable;
		const std::vector<FValue> ArgumentKeep = NativeValueRoots && NativeValueRoots->HasLiveRoots()
		    ? InOutArguments
		    : std::vector<FValue>{};
		FNativeValueScope CallableScope(*this, &CallableKeep, 1, OutReturn);
		FNativeValueScope ArgumentScope(*this, ArgumentKeep.data(), ArgumentKeep.size(), OutReturn);
		return InvokeActiveFunctionAt(
		    Callable->FunctionIndex,
		    Callable->CapturedThis,
		    &Callable->Captures,
		    InOutArguments,
		    OutReturn,
		    OutError);
	}

	bool VeroScriptVm::InvokeFunction(
	    const std::string_view FunctionName,
	    std::vector<FValue>& InOutArguments,
	    FValue& OutReturn,
	    std::string& OutError)
	{
		OutError.clear();
		if (!ActiveModule)
		{
			OutError = "script invocation requires an active VM call";
			return false;
		}
		if (InOutArguments.size() > std::numeric_limits<std::uint8_t>::max())
		{
			OutError = "script function supports at most 255 arguments";
			return false;
		}
		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		const EMethodResolveStatus Status = ModuleMeta::TryResolveFreeFunction(
		    *ActiveModule,
		    FunctionName,
		    static_cast<std::uint8_t>(InOutArguments.size()),
		    FunctionIndex,
		    InOutArguments.empty() ? nullptr : InOutArguments.data());
		if (Status != EMethodResolveStatus::Found)
		{
			OutError = Status == EMethodResolveStatus::Ambiguous
			    ? "script function call is ambiguous: " + std::string(FunctionName)
			    : "script function not found: " + std::string(FunctionName);
			return false;
		}
		const std::vector<FValue> ArgumentKeep = NativeValueRoots && NativeValueRoots->HasLiveRoots()
		    ? InOutArguments
		    : std::vector<FValue>{};
		FNativeValueScope ArgumentScope(*this, ArgumentKeep.data(), ArgumentKeep.size(), OutReturn);
		return InvokeActiveFunctionAt(
		    FunctionIndex,
		    {},
		    nullptr,
		    InOutArguments,
		    OutReturn,
		    OutError);
	}

	std::string VeroScriptVm::GetCallingModulePath(std::size_t Depth) const
	{
		if (Depth >= Frames.size())
		{
			return {};
		}
		const auto& Frame = Frames[Frames.size() - 1 - Depth];
		if (Frame.Function)
		{
			return Frame.Function->ModulePath;
		}
		if (!ActiveModule)
		{
			return {};
		}
		const auto Offset = Frame.InstructionPointer ? Frame.InstructionPointer - 1 : 0;
		const FBytecodeGlobalInitRange* Best = nullptr;
		for (const auto& Range : ActiveModule->GlobalInitRanges)
		{
			if (Range.BeginOffset <= Offset && Offset < Range.EndOffset &&
			    Range.GlobalSlot < ActiveModule->GlobalMetadata.size() &&
			    (!Best || Range.EndOffset - Range.BeginOffset < Best->EndOffset - Best->BeginOffset))
			{
				Best = &Range;
			}
		}
		return Best ? ActiveModule->GlobalMetadata[Best->GlobalSlot].ModulePath : ActiveModule->EntryModulePath;
	}

	VeroScriptVm::VeroScriptVm(
	    std::ostream& InOutputStream,
	    std::istream& InInputStream,
	    std::ostream& InErrorStream)
	    : FExecutionContext(InOutputStream, InInputStream, InErrorStream)
	{
		SetMaxCallDepth(0);
	}

	VeroScriptVm::VeroScriptVm(std::ostream& Output, std::istream& Input, std::ostream& Error,
	    std::shared_ptr<FVmExecutionDomain> Domain, std::shared_ptr<const void> Owner)
	    : FExecutionContext(Output, Input, Error, std::move(Domain), std::move(Owner))
	{
		// Session configures its current explicit/default limit before execution.
		// Avoid a second environment lookup and a throwaway ownership allocation.
	}

	bool VeroScriptVm::CallFunctionAt(
	    const std::uint16_t FunctionIndex,
	    const FValue* const Arguments,
	    const std::uint8_t ArgumentCount,
	    const FObjectHandle ThisObject,
	    const bool bAllowJit)
	{
		if (!ActiveModule || FunctionIndex >= ActiveModule->Functions.size())
		{
			ReportError("invalid function index");
			return false;
		}
		if (GetLogicalCallDepth() >= CallDepthLimit)
		{
			ReportErrorId("diag.call_stack_overflow");
			return false;
		}

		const FBytecodeFunction& Function = ActiveModule->Functions[FunctionIndex];
		if (ThisObject && ThisObject->bDestroyed)
		{
			ReportError("method receiver has already been destroyed", Function.Location);
			return false;
		}
		// User copy/constructor hooks in argument preparation are already execution.
		// Preflight the selected VM before them. Native hooks commit the domain.
		if (!VmExecutionDomain->bFrozen)
		{
			if (!VmExecutionDomain->Factory && !JitBackendFactory && !AotProgram)
			{
				ReportErrorId("diag.vm_backend_required");
				return false;
			}
			if (VmExecutionDomain->Factory && !PrepareVmBackend()) return false;
		}
		if (!Function.UnavailableReason.empty())
		{
			ReportError("use of unavailable defaulted operation: " + Function.UnavailableReason, Function.Location);
			return false;
		}
		if (Function.bDeleted)
		{
			ReportError("use of deleted declaration: " + Function.Name);
			return false;
		}
		if (Function.ParamCount != ArgumentCount)
		{
			ReportErrorId(
			    "diag.argument_count_mismatch_for_function",
			    Function.Location,
			    {Function.Name});
			return false;
		}
		if (Function.LocalCount < Function.ParamCount)
		{
			ReportErrorId(
			    "diag.invalid_function_frame_localcount_less_than_paramcount",
			    Function.Location,
			    {Function.Name});
			return false;
		}

		const bool bConstructor = ThisObject && Function.bReturnsVoid && Function.bIsMethod && Function.OwnerTypeIndex < ActiveModule->Types.size() && MethodLinkageBareLeaf(Function.Name) == ModuleMeta::TypeSimpleName(ActiveModule->Types[Function.OwnerTypeIndex].Name);
		if (bConstructor && ArgumentCount == 0 && Function.bDefaulted
		    && TryRunTrivialDefaultConstructor(FunctionIndex, ThisObject))
		{
			return !bHasError;
		}
		if (bConstructor)
		{
			ThisObject->bConstructionIncomplete = true;
			const auto& OwnerType = ActiveModule->Types[Function.OwnerTypeIndex];
			if (OwnerType.BaseTypeIndices.empty() && !OwnerType.HostBaseTypeName.empty() && !(Function.SpecialMemberKind == ESpecialMemberKind::CopyConstructor && Function.bDefaulted) && !InitializeObjectHostBase(ThisObject, Function.Location))
			{
				(void)DestroyObject(ThisObject, Function.Location);
				return false;
			}
		}
		bool bDisableNestedJit =
		    bDemandGlobalInitializing || (!Frames.empty() && Frames.back().bDisableNestedJit) || !ActiveContainerCopyReads.empty();
		const bool bOrdinaryConstructor = bConstructor && Function.SpecialMemberKind != ESpecialMemberKind::CopyConstructor;
		// ThisObject retains the receiver across backend callbacks and execution.
		if (bAllowJit && !bDisableNestedJit && (BackendCache || AotProgram) &&
		    (!(ThisObject && ThisObject->bConstructionIncomplete) || bOrdinaryConstructor))
		{
			bool bJitOk = false;
			bool bHandled = false;
			if (bConstructor || (ThisObject && ThisObject->bDestroying && Function.OwnerTypeIndex < ActiveModule->Types.size() && ActiveModule->Types[Function.OwnerTypeIndex].DestructorFunctionIndex == FunctionIndex))
			{
				const FObjectLifetimeScope Stage(ObjectLifetimeScope, Frames.size(), ThisObject, Function.OwnerTypeIndex);
				bHandled = TryDispatchBackendCall(FunctionIndex, Arguments, ArgumentCount, ThisObject, bJitOk);
			}
			else
			{
				bHandled = TryDispatchBackendCall(FunctionIndex, Arguments, ArgumentCount, ThisObject, bJitOk);
			}
			if (bHandled)
			{
				if (bConstructor)
				{
					if (bJitOk && !bHasError && !bHasPendingScriptException && !bExitRequested)
					{
						CompleteObjectConstruction(ThisObject, Function.OwnerTypeIndex);
					}
					else if (!bExitRequested)
					{
						const FObjectLifetimeScope Stage(ObjectLifetimeScope, Frames.size(), ThisObject, Function.OwnerTypeIndex);
						(void)DestroyObject(ThisObject, Function.Location);
					}
				}
				return bJitOk;
			}
			// TryDispatchBackendCall has now demand-compiled (or found) the entry. Carry
			// the native-only cost decision on the interpreter frame so calls made by
			// the rejected loop do not repeatedly enter a slower portable trampoline.
			if (BackendCache)
			{
				if (const auto* Entry = BackendCache->FindBorrowed(FunctionIndex))
				{
					// A partial AOT program may still provide profitable native callees.
					// The JIT's VM cost hint must not suppress those static entries.
					bDisableNestedJit = Entry->bSuppressNestedBackendOnFallback && !AotProgram;
				}
			}
		}

		// Backend selection and its error protocol precede interpreter shortcuts.
		FValue ScalarReturn;
		if (!bConstructor && !bDemandGlobalInitializing && TryCallScalarExpression(FunctionIndex, Arguments, ArgumentCount, ThisObject, nullptr, ScalarReturn))
		{
			Push(std::move(ScalarReturn));
			return !bHasError;
		}
		const bool bReuseFrame = !FreeFrames.empty();
		FCallFrame Frame = bReuseFrame ? std::move(FreeFrames.back()) : FCallFrame{};
		if (bReuseFrame)
		{
			FreeFrames.pop_back();
		}
		Frame.Function = &Function;
		Frame.JitRuntimeFrameIdentity = 0;
		Frame.DiagnosticToken = JitPolicy.Diagnostics.MaxFunctions ? BeginFunctionDiagnostic(FunctionIndex, EJitExecutionKind::Vm) : 0;
		if (JitPolicy.Diagnostics.MaxFunctions && RequestedExecutionBackend != EExecutionBackend::Vm)
		{
			if (Debugger)
			{
				RecordFunctionFallback(FunctionIndex, EJitFallbackReason::Debugger);
			}
			else if (!bAllowJit && !Function.Captures.empty())
			{
				RecordFunctionFallback(FunctionIndex, EJitFallbackReason::Closure);
			}
			else if (!BackendCache && HasExecutionGovernance())
			{
				RecordFunctionFallback(FunctionIndex, EJitFallbackReason::Governance);
			}
		}
		Frame.InstructionPointer = 0;
		// A prepared frame has not entered any protected region. Dispatch records
		// the first real offset; a backend rejection must not run this body's finally.
		Frame.LastInstructionOffset = std::numeric_limits<std::size_t>::max();
		Frame.StackBase = OperandStack.size();
		// Reuse Locals capacity from FreeFrames when possible.
		if (Frame.Locals.size() == Function.LocalCount)
		{
			// Parameter slots are overwritten below; only the remaining locals need
			// their default value before execution and closure-capture installation.
			std::fill(Frame.Locals.begin() + ArgumentCount, Frame.Locals.end(), FValue{std::int32_t{0}});
		}
		else
		{
			Frame.Locals.assign(Function.LocalCount, std::int32_t{0});
		}
		for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
		{
			Frame.Locals[Index] = Arguments[Index];
		}
		Frame.ThisObject = ThisObject;
		Frame.ReturnValue = std::int32_t{0};
		Frame.bHasReturn = false;
		Frame.bExpressionEval = false;
		Frame.bDisableNestedJit = bDisableNestedJit;
		Frame.FinallyContinuations.clear();
		Frame.PrepareInlineCaches(ActiveModule, &Function, ProgramGeneration);
		Frames.push_back(std::move(Frame));
		return true;
	}

	bool VeroScriptVm::InvokeActiveFunctionAt(
	    const std::uint16_t FunctionIndex,
	    const FObjectHandle& ThisObject,
	    const std::vector<FValue>* const Captures,
	    std::vector<FValue>& InOutArguments,
	    FValue& OutReturn,
	    std::string& OutError)
	{
		OutError.clear();
		if (!ActiveModule)
		{
			OutError = "script invocation requires an active VM call";
			return false;
		}
		if (FunctionIndex >= ActiveModule->Functions.size())
		{
			OutError = "script callable has an invalid function identity";
			return false;
		}
		if (InOutArguments.size() > std::numeric_limits<std::uint8_t>::max())
		{
			OutError = "script callable supports at most 255 arguments";
			return false;
		}

		const FBytecodeFunction& Target = ActiveModule->Functions[FunctionIndex];
		if (Target.ParamCount != InOutArguments.size())
		{
			OutError = "script callable argument count mismatch: " + Target.Name;
			return false;
		}
		if ((Captures == nullptr && !Target.Captures.empty()) || (Captures && Captures->size() != Target.Captures.size()))
		{
			OutError = "script callable capture schema is stale: " + Target.Name;
			return false;
		}
		if (Captures)
		{
			for (std::size_t CaptureIndex = 0;
			    CaptureIndex < Captures->size();
			    ++CaptureIndex)
			{
				const auto& Capture = Target.Captures[CaptureIndex];
				const std::string& ExpectedType = Capture.TypeName;
				const bool bReferenceCapture = Capture.Mode != EClosureCaptureMode::Value;
				FValue CaptureValue = (*Captures)[CaptureIndex];
				FValue* const ResolvedCapture = ResolveRefStorage(CaptureValue);
				if ((bReferenceCapture && !IsClosureReferenceCell((*Captures)[CaptureIndex])) || !ResolvedCapture || !ValueMatchesElementType(*ResolvedCapture, ExpectedType, {}))
				{
					OutError = "script callable capture type mismatch at index " + std::to_string(CaptureIndex) + ": " + Target.Name;
					return false;
				}
			}
		}

		if (CallableSignatureModule != ActiveModule || CallableSignatureGeneration != ProgramGeneration)
		{
			CallableSignatures.clear();
			CallableSignatureModule = ActiveModule;
			CallableSignatureGeneration = ProgramGeneration;
		}
		auto SignatureEntry = CallableSignatures.find(FunctionIndex);
		if (SignatureEntry == CallableSignatures.end() || SignatureEntry->second->Name != Target.ClosureTypeName)
		{
			auto Signature = std::make_shared<FVmCallableSignature>();
			Signature->Name = Target.ClosureTypeName;
			if (!Runtime::ParseFunctionType(Target.ClosureTypeName, Signature->Parameters, Signature->ReturnType))
			{
				OutError = "script callable has invalid signature metadata: " + Target.Name;
				return false;
			}
			if (CallableSignatures.size() >= 1024)
			{
				CallableSignatures.clear();
			}
			SignatureEntry = CallableSignatures.insert_or_assign(FunctionIndex, std::move(Signature)).first;
		}
		// Synchronous callbacks can clear the VM cache; retain this immutable schema.
		const auto Signature = SignatureEntry->second;
		const auto& ParamTypes = Signature->Parameters;
		std::string ReturnType = Signature->ReturnType;
		if (ParamTypes.size() != InOutArguments.size())
		{
			OutError = "script callable has invalid signature metadata: " + Target.Name;
			return false;
		}
		for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
		{
			if (ActiveModule && StandardLibrary::IsDiscreteArgument(*ActiveModule, Target, Index) && !Runtime::IsIntegerValue(InOutArguments[Index]))
			{
				OutError = "standard-library count/base/timeout requires an integer argument";
				return false;
			}
			std::string Expected = ParamTypes[Index];
			if (Expected.rfind("const ref ", 0) == 0)
			{
				Expected.erase(0, 10);
			}
			else if (Expected.rfind("ref ", 0) == 0)
			{
				Expected.erase(0, 4);
			}
			const bool bReferenceParam = Index < Target.ParamFlags.size() && (Target.ParamFlags[Index] & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0;
			bool bMatches = false;
			if (Runtime::IsPrimitiveValueTypeName(Expected))
			{
				bMatches = Runtime::ValueMatchesPrimitiveTypeName(
				    InOutArguments[Index], Expected);
				if (!bMatches && !bReferenceParam)
				{
					FValue Converted = InOutArguments[Index];
					std::string ConversionError;
					bMatches = Runtime::CoerceValueToPrimitiveTypeName(
					    Expected, Converted, ConversionError);
				}
			}
			else
			{
				bMatches = ValueMatchesElementType(
				    InOutArguments[Index], Expected, {});
			}
			if (!bMatches)
			{
				OutError = "script callable argument type mismatch at index " + std::to_string(Index) + ": " + Target.Name;
				return false;
			}
		}

		std::vector<FValue> ClonedArguments;
		ClonedArguments.reserve(InOutArguments.size());
		for (std::size_t Index = 0; Index < InOutArguments.size(); ++Index)
		{
			FValue Cloned;
			const auto Flags = Index < Target.ParamFlags.size() ? Target.ParamFlags[Index] : 0;
			const bool Reference = (Flags & BytecodeParamFlagRef) != 0;
			bool Ok = true;
			if (Reference && (Flags & BytecodeParamFlagConstRef) != 0)
			{
				Cloned = InOutArguments[Index];
			}
			else if (Reference)
			{
				Ok = Runtime::TryCloneValue(InOutArguments[Index], Cloned);
			}
			else
			{
				Ok = CopyLanguageValue(InOutArguments[Index], Cloned, {}, false, ParamTypes[Index]);
			}
			if (!Ok)
			{
				if (!bHasError && !bHasPendingScriptException)
				{
					ReportError("failed to stage callable argument");
				}
				OutError = Error.Message;
				(void)ReleaseValueList(ClonedArguments, {});
				return false;
			}
			ClonedArguments.push_back(std::move(Cloned));
		}
		for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
		{
			const bool bReferenceParam = Index < Target.ParamFlags.size() && (Target.ParamFlags[Index] & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0;
			if (bReferenceParam || !Runtime::IsPrimitiveValueTypeName(ParamTypes[Index]))
			{
				continue;
			}
			std::string ConversionError;
			if (!Runtime::CoerceValueToPrimitiveTypeName(
			        ParamTypes[Index], ClonedArguments[Index], ConversionError))
			{
				OutError = "script callable argument conversion failed at index " + std::to_string(Index) + ": " + Target.Name;
				(void)ReleaseValueList(ClonedArguments, {});
				return false;
			}
		}

		const auto IsWritableRef = [&Target](const std::size_t Index) -> bool
		{
			return Index < Target.ParamFlags.size() && (Target.ParamFlags[Index] & BytecodeParamFlagRef) != 0 && (Target.ParamFlags[Index] & BytecodeParamFlagConstRef) == 0;
		};
		bool bHasWritableRefs = false;
		for (std::size_t Index = 0; Index < InOutArguments.size(); ++Index)
		{
			bHasWritableRefs = bHasWritableRefs || IsWritableRef(Index);
		}

		std::size_t AnchorIndex = static_cast<std::size_t>(-1);
		std::vector<FValue> CallArguments;
		const FValue* CallArgumentData = ClonedArguments.data();
		if (bHasWritableRefs)
		{
			AnchorIndex = Frames.size();
			FCallFrame Anchor;
			Anchor.Function = nullptr;
			Anchor.InstructionPointer = 0;
			Anchor.StackBase = OperandStack.size();
			Anchor.Locals.assign(InOutArguments.size(), FValue{std::int32_t{0}});
			CallArguments.assign(InOutArguments.size(), FValue{std::int32_t{0}});
			for (std::size_t Index = 0; Index < InOutArguments.size(); ++Index)
			{
				if (IsWritableRef(Index))
				{
					Anchor.Locals[Index] = std::move(ClonedArguments[Index]);
					ClonedArguments[Index] = std::int32_t{0};
					FLocalRef Ref;
					Ref.FrameIndex = static_cast<std::uint32_t>(AnchorIndex);
					Ref.Slot = static_cast<std::uint16_t>(Index);
					CallArguments[Index] = Ref;
				}
				else
				{
					CallArguments[Index] = std::move(ClonedArguments[Index]);
					ClonedArguments[Index] = std::int32_t{0};
				}
			}
			Frames.push_back(std::move(Anchor));
			CallArgumentData = CallArguments.data();
		}

		const auto CleanupAnchor = [&]()
		{
			if (AnchorIndex == static_cast<std::size_t>(-1) || Frames.size() != AnchorIndex + 1)
			{
				return;
			}
			(void)DiscardExecutionAbove(AnchorIndex, OperandStack.size(), {});
		};

		const std::size_t DepthBefore = Frames.size();
		const std::size_t StackBefore = OperandStack.size();
		FValue ScalarResult;
		const bool bScalarCapture = Captures && !Captures->empty() && !bHasWritableRefs && TryCallScalarExpression(FunctionIndex, CallArgumentData, static_cast<std::uint8_t>(InOutArguments.size()), ThisObject, Captures, ScalarResult);
		if (bScalarCapture)
		{
			Push(std::move(ScalarResult));
		}
		const bool bCallOk = bScalarCapture || CallFunctionAt(FunctionIndex, CallArgumentData, static_cast<std::uint8_t>(InOutArguments.size()), ThisObject,
		                                           /*bAllowJit=*/!bHasWritableRefs && (!Captures || Captures->empty()));
		if (bCallOk && !bScalarCapture && Captures && !Captures->empty())
		{
			FCallFrame& Callee = Frames.back();
			for (std::size_t CaptureIndex = 0; CaptureIndex < Captures->size(); ++CaptureIndex)
			{
				const std::size_t Slot = Target.ParamCount + CaptureIndex;
				if (Slot >= Callee.Locals.size())
				{
					ReportError("script callable capture slot out of range");
					break;
				}
				Callee.Locals[Slot] = (*Captures)[CaptureIndex];
			}
		}

		if (bHasWritableRefs)
		{
			for (std::size_t Index = 0; Index < CallArguments.size(); ++Index)
			{
				if (!IsWritableRef(Index))
				{
					(void)ReleaseValue(CallArguments[Index], {});
					CallArguments[Index] = std::int32_t{0};
				}
			}
		}
		(void)ReleaseValueList(ClonedArguments, {});

		if (!bCallOk || !JitFinishNestedCall(DepthBefore, StackBefore))
		{
			OutError = Error.Message.empty() ? "nested script invocation failed" : Error.Message;
			CleanupAnchor();
			return false;
		}
		if (bExitRequested)
		{
			CleanupAnchor();
			OutReturn = std::int32_t{0};
			return true;
		}

		FValue ReturnValue = Pop();
		if (bHasError)
		{
			OutError = Error.Message;
			CleanupAnchor();
			return false;
		}
		if (Target.bReturnsVoid)
		{
			(void)ReleaseValue(ReturnValue, {});
			ReturnValue = std::int32_t{0};
		}
		else if (Target.bReturnsRef)
		{
			FValue* const ReferencedValue = ResolveRefStorage(ReturnValue);
			FValue Snapshot;
			const std::string ValueReturnType(Runtime::StripReferenceTypeName(ReturnType));
			if (!ReferencedValue || ReferencedValue == &ReturnValue || !ValueMatchesElementType(*ReferencedValue, ValueReturnType, {}) || !CloneValueOrReport(*ReferencedValue, Snapshot, {}))
			{
				OutError = Error.Message.empty()
				    ? "script callable returned an invalid reference: " + Target.Name
				    : Error.Message;
				CleanupAnchor();
				(void)ReleaseValue(ReturnValue, {});
				return false;
			}
			(void)ReleaseValue(ReturnValue, {});
			ReturnValue = std::move(Snapshot);
			ReturnType = ValueReturnType;
		}
		else if (!ValueMatchesElementType(ReturnValue, ReturnType, {}))
		{
			OutError = "script callable returned a value incompatible with " + ReturnType + ": " + Target.Name;
			CleanupAnchor();
			(void)ReleaseValue(ReturnValue, {});
			return false;
		}

		if (bHasWritableRefs)
		{
			FCallFrame& Anchor = Frames[AnchorIndex];
			std::vector<FValue> WriteBack(InOutArguments.size(), FValue{std::int32_t{0}});
			for (std::size_t Index = 0; Index < InOutArguments.size(); ++Index)
			{
				if (IsWritableRef(Index))
				{
					WriteBack[Index] = std::move(Anchor.Locals[Index]);
					Anchor.Locals[Index] = std::int32_t{0};
				}
			}
			for (std::size_t Index = 0; Index < InOutArguments.size(); ++Index)
			{
				if (IsWritableRef(Index) && !AssignValue(InOutArguments[Index], std::move(WriteBack[Index]), {}))
				{
					OutError = Error.Message;
					CleanupAnchor();
					(void)ReleaseValue(ReturnValue, {});
					return false;
				}
			}
		}
		CleanupAnchor();
		if (!AssignValue(OutReturn, std::move(ReturnValue), {}))
		{
			OutError = Error.Message;
			return false;
		}
		return true;
	}

	std::string VeroScriptVm::ReadNextToken()
	{
		std::optional<std::string> Token;
		if (!ReportStandardLibraryResult(StandardLibrary::ReadInput(InputStream, true, Token,
		        {StandardLibraryLimits, GetMemoryWork()})))
		{
			return {};
		}
		return Token ? std::move(*Token) : std::string{};
	}

	std::string VeroScriptVm::GetCallerModulePath() const
	{
		const std::vector<std::string> Modules = GetSpawnReferenceModules();
		return Modules.empty() ? std::string{} : Modules.back();
	}

	std::vector<std::string> VeroScriptVm::GetSpawnReferenceModules() const
	{
		std::vector<std::string> Modules;
		auto AddModule = [&](const std::string& ModulePath)
		{
			if (ModulePath.empty())
			{
				return;
			}
			if (std::find(Modules.begin(), Modules.end(), ModulePath) == Modules.end())
			{
				Modules.push_back(ModulePath);
			}
		};
		for (auto It = Frames.rbegin(); It != Frames.rend(); ++It)
		{
			if (!It->Function)
			{
				continue;
			}
			const std::string& LinkageName = It->Function->Name;
			const std::size_t Sep = LinkageName.find('\x1F');
			if (Sep != std::string::npos)
			{
				AddModule(LinkageName.substr(0, Sep));
			}
			else
			{
				AddModule(std::string(It->Function->Location.FileLabel));
			}
		}
		AddModule(EntryModulePath);
		bool bInModuleInit = false;
		for (const FCallFrame& Frame : Frames)
		{
			if (Frame.Function == nullptr)
			{
				bInModuleInit = true;
				break;
			}
		}
		if (bInModuleInit && ActiveModule)
		{
			for (const FBytecodeFunction& Function : ActiveModule->Functions)
			{
				const std::size_t Sep = Function.Name.find('\x1F');
				if (Sep != std::string::npos)
				{
					AddModule(Function.Name.substr(0, Sep));
				}
				else if (!Function.Location.FileLabel.empty())
				{
					AddModule(std::string(Function.Location.FileLabel));
				}
			}
		}
		return Modules;
	}

	FValue VeroScriptVm::Pop()
	{
		return ExecutionDetail::Pop(OperandStack, Frames,
		    [this](const char* Message)
		{
			ReportError(Message);
		});
	}

	void VeroScriptVm::Push(FValue Value)
	{
		ExecutionDetail::Push(OperandStack, MaxOperandStackDepth, std::move(Value),
		    [this](const char* Message)
		{
			ReportError(Message);
		});
	}

	bool VeroScriptVm::ReleasePoppedArguments(
	    FValue* const Arguments,
	    const std::uint8_t ArgumentCount,
	    const FSourceLocation& Location)
	{
		if (ArgumentCount != 0 && Arguments == nullptr)
		{
			ReportError("invalid temporary argument buffer", Location);
			return false;
		}
		for (std::size_t Index = ArgumentCount; Index > 0; --Index)
		{
			if (!ReleaseValue(Arguments[Index - 1], Location))
			{
				return false;
			}
			Arguments[Index - 1] = std::int32_t{0};
		}
		return true;
	}

	FValue VeroScriptVm::Peek(const std::size_t Distance) const
	{
		const std::size_t StackBase = Frames.empty() ? 0 : Frames.back().StackBase;
		if (OperandStack.size() <= StackBase || Distance >= OperandStack.size() - StackBase)
		{
			return std::int32_t{0};
		}
		return OperandStack[OperandStack.size() - 1 - Distance];
	}

	std::uint16_t VeroScriptVm::FindTypeIndex(const std::string& TypeName) const
	{
		if (!ActiveModule)
		{
			return static_cast<std::uint16_t>(-1);
		}
		if (TypeIndexModule != ActiveModule || TypeIndexGeneration != ProgramGeneration)
		{
			TypeIndexByName.clear();
			TypeIndexByName.reserve(ActiveModule->Types.size() * 2u);
			// Exact names must win even when an earlier linkage-qualified type
			// exposes the same unqualified suffix.
			for (std::size_t Index = 0; Index < ActiveModule->Types.size(); ++Index)
			{
				TypeIndexByName.emplace(
				    ActiveModule->Types[Index].Name,
				    static_cast<std::uint16_t>(Index));
			}
			// Native-array / generic element names are often unqualified ("C")
			// while types are stored under linkage names ("file\x1FC").
			for (std::size_t Index = 0; Index < ActiveModule->Types.size(); ++Index)
			{
				const std::string& RegisteredName = ActiveModule->Types[Index].Name;
				const std::size_t SeparatorIndex = RegisteredName.find('\x1F');
				if (SeparatorIndex != std::string::npos)
				{
					TypeIndexByName.emplace(
					    RegisteredName.substr(SeparatorIndex + 1),
					    static_cast<std::uint16_t>(Index));
				}
			}
			TypeIndexModule = ActiveModule;
			TypeIndexGeneration = ProgramGeneration;
			LastTypeIndexName.clear();
			LastTypeIndex = static_cast<std::uint16_t>(-1);
		}
		if (LastTypeIndexName == TypeName)
		{
			return LastTypeIndex;
		}
		const auto It = TypeIndexByName.find(TypeName);
		const std::uint16_t Result = It == TypeIndexByName.end()
		    ? static_cast<std::uint16_t>(-1)
		    : It->second;
		LastTypeIndexName = TypeName;
		LastTypeIndex = Result;
		return Result;
	}

	bool VeroScriptVm::IsActiveObjectTypeAssignableTo(const std::string& ActualTypeName,
	    const std::string& ExpectedTypeName, const std::uint16_t KnownActualIndex) const
	{
		if (!ActiveModule) return false;
		// Borrowed modules may be edited between host field/layout queries, which
		// need not begin a new execution. Outside execution only an owned immutable
		// program proves cached compatibility until its generation changes.
		if ((!bMemoryExecutionActive || Frames.empty()) && (!JitProgramOwner || JitProgramOwner.get() != ActiveModule))
		{
			return ModuleMeta::IsObjectTypeAssignableTo(*ActiveModule, ActualTypeName, ExpectedTypeName);
		}
		const auto Actual = KnownActualIndex < ActiveModule->Types.size() &&
		        ActiveModule->Types[KnownActualIndex].Name == ActualTypeName
		    ? KnownActualIndex : FindTypeIndex(ActualTypeName);
		auto Expected = FindTypeIndex(ExpectedTypeName);
		bool Interface = false;
		if (Expected >= ActiveModule->Types.size())
		{
			if (!ModuleMeta::FindInterfaceIndex(*ActiveModule, ExpectedTypeName, Expected) || Expected >= ActiveModule->Interfaces.size() || ActiveModule->Interfaces[Expected].Name != ExpectedTypeName)
			{
				return ModuleMeta::IsObjectTypeAssignableTo(*ActiveModule, ActualTypeName, ExpectedTypeName);
			}
			Interface = true;
		}
		// Exact class/interface names have context-independent index keys. Aliases
		// retain authoritative resolution. Borrowed programs cache only while their
		// execution is active; BeginExecutionGovernance clears them between calls.
		if (Actual >= ActiveModule->Types.size() || ActiveModule->Types[Actual].Name != ActualTypeName || (!Interface && ActiveModule->Types[Expected].Name != ExpectedTypeName))
		{
			return ModuleMeta::IsObjectTypeAssignableTo(*ActiveModule, ActualTypeName, ExpectedTypeName);
		}
		if (TypeCompatibilityModule != ActiveModule || TypeCompatibilityGeneration != ProgramGeneration)
		{
			TypeCompatibility.clear();
			TypeCompatibilityModule = ActiveModule;
			TypeCompatibilityGeneration = ProgramGeneration;
		}
		const auto Key = (static_cast<std::uint64_t>(Actual) << 32u) | (static_cast<std::uint64_t>(Interface) << 16u) | Expected;
		const auto Found = TypeCompatibility.find(Key);
		if (Found != TypeCompatibility.end()) return Found->second;
		const bool Result = ModuleMeta::IsObjectTypeAssignableTo(*ActiveModule, ActualTypeName, ExpectedTypeName);
		if (TypeCompatibility.size() >= 4096)
		{
			TypeCompatibility.clear();
		}
		TypeCompatibility.emplace(Key, Result);
		return Result;
	}

	std::uint16_t VeroScriptVm::FindActiveNativeIndexByName(
	    const std::string& Name) const
	{
		if (!ActiveNatives)
		{
			return static_cast<std::uint16_t>(-1);
		}
		if (ActiveNativeIndexSource != ActiveNatives)
		{
			ActiveNativeIndexByName.clear();
			ActiveNativeProviderByType.clear();
			ActiveHostTypeNames.clear();
			ActiveZeroArgHostCtorByType.clear();
			constexpr std::size_t MaxNativeCount =
			    static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u;
			const std::size_t IndexedCount =
			    std::min<std::size_t>(ActiveNatives->size(), MaxNativeCount);
			ActiveNativeIndexByName.reserve(IndexedCount * 2u);
			ActiveHostTypeNames.reserve(IndexedCount * 2u);
			ActiveZeroArgHostCtorByType.reserve(IndexedCount);
			for (std::size_t Index = 0; Index < IndexedCount; ++Index)
			{
				const FNativeFunction& Native = (*ActiveNatives)[Index];
				// Match the prior forward scan: duplicate names resolve to the first
				// registered entry. Catalog validation reports duplicates separately.
				ActiveNativeIndexByName.emplace(
				    Native.Name,
				    static_cast<std::uint16_t>(Index));
				// Preserve CreateDefaultValue's historical host-type discovery rules.
				ActiveHostTypeNames.emplace(Native.Name);
				if (!Native.HostTypeName.empty())
				{
					ActiveHostTypeNames.emplace(Native.HostTypeName);
					if (Native.ProviderIdentity)
					{
						// Registration identity uses the same first-provider rule as
						// the authoritative forward scan, with per-entry invalidation.
						ActiveNativeProviderByType.emplace(Native.HostTypeName,
						    static_cast<std::uint16_t>(Index));
					}
				}
				if (!Native.ReturnHostTypeName.empty())
				{
					ActiveHostTypeNames.emplace(Native.ReturnHostTypeName);
				}
				if (!Native.Name.empty() && Native.ReturnType == ENativeType::HostObject && Native.ParamTypes.empty() && Native.Callback)
				{
					// The former forward scan retained the last matching zero-argument
					// constructor when a manually supplied table contained duplicates.
					ActiveZeroArgHostCtorByType[Native.Name] =
					    static_cast<std::uint16_t>(Index);
				}
			}
			ActiveNativeIndexSource = ActiveNatives;
		}
		const auto It = ActiveNativeIndexByName.find(Name);
		return It == ActiveNativeIndexByName.end()
		    ? static_cast<std::uint16_t>(-1)
		    : It->second;
	}

	bool VeroScriptVm::IsActiveHostTypeName(const std::string& TypeName) const
	{
		if (!ActiveNatives)
		{
			return false;
		}
		// Ensure every native metadata index shares one rebuild and invalidation point.
		if (ActiveNativeIndexSource != ActiveNatives)
		{
			(void)FindActiveNativeIndexByName(TypeName);
		}
		return ActiveHostTypeNames.find(TypeName) != ActiveHostTypeNames.end();
	}

	std::uint16_t VeroScriptVm::FindActiveZeroArgHostCtor(
	    const std::string& TypeName) const
	{
		if (!ActiveNatives)
		{
			return static_cast<std::uint16_t>(-1);
		}
		if (ActiveNativeIndexSource != ActiveNatives)
		{
			(void)FindActiveNativeIndexByName(TypeName);
		}
		const auto It = ActiveZeroArgHostCtorByType.find(TypeName);
		return It == ActiveZeroArgHostCtorByType.end()
		    ? static_cast<std::uint16_t>(-1)
		    : It->second;
	}

	std::uint16_t VeroScriptVm::ResolveObjectTypeIndex(
	    const FScriptObject& Object) const
	{
		if (!ActiveModule || Object.bDestroyed)
		{
			return static_cast<std::uint16_t>(-1);
		}
		if (Object.RuntimeTypeModule == ActiveModule && Object.RuntimeTypeGeneration == ProgramGeneration && Object.RuntimeTypeIndex < ActiveModule->Types.size())
		{
			return Object.RuntimeTypeIndex;
		}
		// The object-local identity is initialized before publication and remains a
		// lock-free fast path for its owning module generation. Do not rewrite it on
		// a lookup from another VM or a newer hot-reload generation: object handles
		// may be shared through synchronized host/task boundaries, and mutating a
		// cache through a const object would otherwise create a C++ data race. The
		// VM-local type-name index remains generation-qualified and authoritative.
		return FindTypeIndex(Object.GetTypeName());
	}

	std::uint16_t VeroScriptVm::FindFieldIndexByName(
	    const std::uint16_t TypeIndex,
	    const std::string& FieldName) const
	{
		if (TypeIndex >= ActiveModule->Types.size())
		{
			return static_cast<std::uint16_t>(-1);
		}
		const FBytecodeTypeDesc& TypeDesc = ActiveModule->Types[TypeIndex];
		for (std::size_t Index = 0; Index < TypeDesc.Fields.size(); ++Index)
		{
			if (TypeDesc.Fields[Index].Name == FieldName)
			{
				return static_cast<std::uint16_t>(Index);
			}
		}
		return static_cast<std::uint16_t>(-1);
	}

	std::uint16_t VeroScriptVm::FindJitFieldIndex(
	    const std::uint16_t TypeIndex,
	    const std::uint16_t NameIndex) const
	{
		if (!ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			return static_cast<std::uint16_t>(-1);
		}
		const auto FindInStableShapeTable = [&]() -> std::uint16_t
		{
			if (JitTypeFieldIcModule != ActiveModule || JitTypeFieldIcGeneration != ProgramGeneration || TypeIndex >= JitTypeFieldIc.size() || TypeIndex >= ActiveModule->Types.size())
			{
				return static_cast<std::uint16_t>(-1);
			}
			FJitTypeFieldCacheSet& TypeSet = JitTypeFieldIc[TypeIndex];
			if (!TypeSet.bInitialized)
			{
				const FBytecodeTypeDesc& Type = ActiveModule->Types[TypeIndex];
				std::size_t Capacity = 4u;
				while (Capacity < Type.Fields.size() * 2u)
				{
					Capacity <<= 1u;
				}
				TypeSet.Slots.assign(
				    Capacity, std::numeric_limits<std::uint32_t>::max());
				const std::size_t Mask = Capacity - 1u;
				for (std::size_t FieldIndex = 0; FieldIndex < Type.Fields.size() && FieldIndex < std::numeric_limits<std::uint16_t>::max(); ++FieldIndex)
				{
					const auto Found = std::find(
					    ActiveModule->StringPool.begin(),
					    ActiveModule->StringPool.end(),
					    Type.Fields[FieldIndex].Name);
					if (Found == ActiveModule->StringPool.end())
					{
						continue;
					}
					const std::size_t ResolvedNameIndex = static_cast<std::size_t>(
					    std::distance(ActiveModule->StringPool.begin(), Found));
					if (ResolvedNameIndex >= std::numeric_limits<std::uint16_t>::max())
					{
						continue;
					}
					const std::uint32_t Packed =
					    (static_cast<std::uint32_t>(ResolvedNameIndex) << 16u) | static_cast<std::uint16_t>(FieldIndex);
					std::size_t Slot =
					    (static_cast<std::size_t>(ResolvedNameIndex) * 2654435761u) & Mask;
					while (TypeSet.Slots[Slot] != std::numeric_limits<std::uint32_t>::max())
					{
						Slot = (Slot + 1u) & Mask;
					}
					TypeSet.Slots[Slot] = Packed;
				}
				TypeSet.bInitialized = true;
			}
			if (TypeSet.Slots.empty())
			{
				return static_cast<std::uint16_t>(-1);
			}
			const std::size_t Mask = TypeSet.Slots.size() - 1u;
			std::size_t Slot =
			    (static_cast<std::size_t>(NameIndex) * 2654435761u) & Mask;
			for (std::size_t Probe = 0; Probe < TypeSet.Slots.size(); ++Probe)
			{
				const std::uint32_t Packed = TypeSet.Slots[Slot];
				if (Packed == std::numeric_limits<std::uint32_t>::max())
				{
					return static_cast<std::uint16_t>(-1);
				}
				if ((Packed >> 16u) == NameIndex)
				{
					return static_cast<std::uint16_t>(Packed & 0xFFFFu);
				}
				Slot = (Slot + 1u) & Mask;
			}
			return static_cast<std::uint16_t>(-1);
		};
		if (const std::uint16_t StableFieldIndex = FindInStableShapeTable();
		    StableFieldIndex != static_cast<std::uint16_t>(-1))
		{
			return StableFieldIndex;
		}
		constexpr std::size_t WaysPerSet = 4;
		constexpr std::size_t SetCount =
		    std::tuple_size_v<decltype(JitFieldIcNextWay)>;
		static_assert(
		    WaysPerSet * SetCount == std::tuple_size_v<decltype(JitFieldIc)>);
		static_assert((WaysPerSet & (WaysPerSet - 1u)) == 0u);
		static_assert((SetCount & (SetCount - 1u)) == 0u);
		const std::size_t SetIndex =
		    (static_cast<std::size_t>(TypeIndex) * 131u + NameIndex) & (SetCount - 1u);
		const std::size_t SetBase = SetIndex * WaysPerSet;
		for (std::size_t Way = 0; Way < WaysPerSet; ++Way)
		{
			const FJitFieldCacheEntry& Entry = JitFieldIc[SetBase + Way];
			if (Entry.Module == ActiveModule && Entry.ProgramGeneration == ProgramGeneration && Entry.NameIndex == NameIndex && Entry.TypeIndex == TypeIndex)
			{
				return Entry.FieldIndex;
			}
		}
		bModuleLookupArraysDirty = true;
		const std::size_t ReplacementWay =
		    JitFieldIcNextWay[SetIndex]++ & (WaysPerSet - 1u);
		FJitFieldCacheEntry& Entry = JitFieldIc[SetBase + ReplacementWay];
		if (Entry.Module == ActiveModule && Entry.ProgramGeneration == ProgramGeneration && (JitTypeFieldIcModule != ActiveModule || JitTypeFieldIcGeneration != ProgramGeneration || JitTypeFieldIc.size() != ActiveModule->Types.size()))
		{
			JitTypeFieldIc.assign(
			    ActiveModule->Types.size(), FJitTypeFieldCacheSet{});
			JitTypeFieldIcModule = ActiveModule;
			JitTypeFieldIcGeneration = ProgramGeneration;
		}
		Entry.Module = ActiveModule;
		Entry.ProgramGeneration = ProgramGeneration;
		Entry.NameIndex = NameIndex;
		Entry.TypeIndex = TypeIndex;
		Entry.FieldIndex = FindFieldIndexByName(
		    TypeIndex, ActiveModule->StringPool[NameIndex]);
		// The first lookup that activates the L2 builds the complete type table.
		// The current value remains authoritative if malformed metadata omitted the
		// field name from StringPool; validated compiler output always agrees.
		(void)FindInStableShapeTable();
		return Entry.FieldIndex;
	}

	bool VeroScriptVm::ReleaseValueList(std::deque<FValue>& Values, const FSourceLocation& Location)
	{
		const bool bHadPendingError = bHasError;
		FScriptError PendingError = Error;
		bHasError = false;
		Error = {};
		bool bReleasedAll = true;
		FScriptError FirstReleaseError;
		for (std::size_t Index = Values.size(); Index > 0; --Index)
		{
			if (!ReleaseValue(Values[Index - 1], Location))
			{
				VmDetail::RecordCleanupFailure(bReleasedAll, FirstReleaseError, bHasError, Error);
			}
			Values[Index - 1] = std::int32_t{0};
		}
		Values.clear();
		bReleasedAll = VmDetail::FinishCleanup(bReleasedAll, bHadPendingError, std::move(PendingError),
		    std::move(FirstReleaseError), bExitRequested, bHasError, Error);
		return bReleasedAll;
	}

	FValue VeroScriptVm::CreateDefaultValue(const std::string& TypeName, const FSourceLocation& Location)
	{
		if (TypeName == "variant") return FScriptVariantHandle{};
		if (Memory::IsTypeName(TypeName))
		{
			if (TypeName == "std.memory.byte_order")
			{
				return FEnumValue{TypeName, 0};
			}
			FValue Value;
			const auto Result = TypeName == "std.memory.buffer"
			    ? Memory::CreateBuffer(GetMemoryContext(), 0, Value, GetMemoryWork())
			    : Memory::CreateEmptyView(TypeName == "std.memory.readonly_view", Value);
			if (!Result)
			{
				ReportErrorId(Result.MessageId, Location);
			}
			return Value;
		}
		if (Runtime::IsFunctionTypeName(TypeName))
		{
			return FFunctionHandle{};
		}
		if (Runtime::IsDynamicArrayTypeName(TypeName))
		{
			return CreateDynamicArray(TypeName, Location);
		}
		if (Runtime::IsMapTypeName(TypeName))
		{
			return CreateMap(TypeName, Location);
		}
		if (Runtime::IsMultiSetTypeName(TypeName))
		{
			return CreateMap(TypeName, Location);
		}
		if (Runtime::IsSetTypeName(TypeName))
		{
			return CreateSet(TypeName, Location);
		}
		if (Runtime::IsQueueTypeName(TypeName))
		{
			return CreateQueue(TypeName, Location);
		}
		if (Runtime::IsChannelTypeName(TypeName))
		{
			return CreateChannel(TypeName, Location);
		}
		if (Runtime::IsStackTypeName(TypeName))
		{
			return CreateStack(TypeName, Location);
		}
		if (Runtime::IsOrderedMapTypeName(TypeName))
		{
			return CreateOrderedMap(TypeName, Location);
		}
		if (Runtime::IsOrderedMultiSetTypeName(TypeName))
		{
			return CreateOrderedMap(TypeName, Location);
		}
		if (Runtime::IsDequeTypeName(TypeName))
		{
			return CreateDeque(TypeName, Location);
		}
		if (Runtime::IsNativeArrayTypeName(TypeName))
		{
			return CreateNativeArray(TypeName, Location);
		}
		if (TypeName == "bool")
		{
			return false;
		}
		if (TypeName == "string")
		{
			return std::string{};
		}
		if (TypeName == "wstring")
		{
			return std::u16string{};
		}
		if (TypeName == "float")
		{
			return 0.0f;
		}
		if (TypeName == "double")
		{
			return 0.0;
		}
		if (Runtime::IsUnsignedTypeName(TypeName))
		{
			if (TypeName == "unsigned char")
			{
				return std::uint8_t{0};
			}
			if (TypeName == "unsigned short")
			{
				return std::uint16_t{0};
			}
			return TypeName == "uint64" || TypeName == "unsigned long"
			    ? FValue{std::uint64_t{0}}
			    : FValue{std::uint32_t{0}};
		}
		if (TypeName == "int64" || TypeName == "long")
		{
			return std::int64_t{0};
		}
		if (TypeName == "char")
		{
			return std::int8_t{0};
		}
		if (TypeName == "short")
		{
			return std::int16_t{0};
		}
		if (TypeName == "int")
		{
			return std::int32_t{0};
		}
		if (ActiveModule)
		{
			for (const FBytecodeEnumDesc& EnumDesc : ActiveModule->Enums)
			{
				if (EnumDesc.bScoped && EnumDesc.Name == TypeName)
				{
					return FEnumValue{Runtime::EnumValueTypeName(*ActiveModule, TypeName), 0};
				}
			}
		}

		const std::uint16_t TypeIndex = FindTypeIndex(TypeName);
		if (TypeIndex != static_cast<std::uint16_t>(-1))
		{
			// Class fields are handles: default to null. Struct fields are values: default-construct.
			if (ActiveModule->Types[TypeIndex].Kind == EScriptObjectKind::Class)
			{
				return FObjectHandle{};
			}
			const auto& Type = ActiveModule->Types[TypeIndex];
			const auto Factory = ActiveModule->FunctionIndices.find(
			    Type.Name + std::string(DefaultConstructionFactorySuffix));
			if (Factory != ActiveModule->FunctionIndices.end())
			{
				if (Factory->second >= ActiveModule->Functions.size() || ActiveModule->Functions[Factory->second].ParamCount != 0 || ActiveModule->Functions[Factory->second].bIsMethod || ActiveModule->Functions[Factory->second].bReturnsVoid)
				{
					ReportError("invalid default construction factory for type: " + TypeName, Location);
					return {};
				}
				const auto Depth = Frames.size();
				const auto Stack = OperandStack.size();
				if (!CallFunctionAt(Factory->second, nullptr, 0, {}, false) || !JitFinishNestedCall(Depth, Stack))
				{
					return {};
				}
				if (bExitRequested) return {};
				FValue Result = Pop();
				const auto* Object = std::get_if<FObjectHandle>(&Result);
				if (!Object || !*Object || (*Object)->bDestroyed || (*Object)->bConstructionIncomplete || (*Object)->GetTypeName() != Type.Name)
				{
					ReportError("default construction factory returned an invalid value for type: " + TypeName, Location);
					(void)ReleaseValue(Result, Location);
					return {};
				}
				return Result;
			}
			return CreateObject(TypeIndex, Location);
		}

		// Host types live in ActiveNatives, not Module.Types — never fall through to int32.
		if (ActiveNatives && IsActiveHostTypeName(TypeName))
		{
			const std::uint16_t ZeroArgCtorIndex = FindActiveZeroArgHostCtor(TypeName);
			if (ZeroArgCtorIndex != static_cast<std::uint16_t>(-1))
			{
				const std::size_t StackBefore = OperandStack.size();
				if (!CallNative(ZeroArgCtorIndex, 0, Location))
				{
					if (OperandStack.size() > StackBefore)
					{
						OperandStack.resize(StackBefore);
					}
					return {};
				}
				if (OperandStack.size() != StackBefore + 1)
				{
					ReportError(
					    "host default constructor produced unexpected stack depth: " + TypeName,
					    Location);
					return {};
				}
				return Pop();
			}
			// Host class → null. Host struct without a zero-arg ctor → empty handle
			// placeholder (T[N] brace-init fills slots; bare struct fields are rejected
			// at compile time).
			return FHostObjectHandle{};
		}

		return std::int32_t{0};
	}

	bool VeroScriptVm::GetField(
	    FScriptObject& Object,
	    const std::uint16_t FieldIndex,
	    FValue& OutValue,
	    const FSourceLocation& Location)
	{
		if (Object.bDestroyed)
		{
			ReportError("field receiver has already been destroyed", Location);
			return false;
		}
		if (FieldIndex >= Object.Fields.size())
		{
			ReportError("invalid field index", Location);
			return false;
		}
		FValue& Field = Object.Fields[FieldIndex];
		// A direct scalar cannot carry a reference or a mutation origin.
		if (const auto* Scalar = std::get_if<std::int32_t>(&Field))
		{
			const std::int32_t Result = *Scalar;
			OutValue = Result;
			return true;
		}
		FValue* const Storage = ResolveRefStorage(Field);
		if (!Storage)
		{
			ReportError("invalid referenced field storage", Location);
			return false;
		}
		OutValue = *Storage;
		StampContainerMutationOrigin(OutValue, Object, FieldIndex);
		return true;
	}

	bool VeroScriptVm::SetField(
	    FScriptObject& Object,
	    const std::uint16_t FieldIndex,
	    const FValue& Value,
	    const FSourceLocation& Location,
	    const bool bNotifyMutation)
	{
		if (Object.bDestroyed)
		{
			ReportError("field receiver has already been destroyed", Location);
			return false;
		}
		if (FieldIndex >= Object.Fields.size())
		{
			ReportError("invalid field index", Location);
			return false;
		}
		FValue& Field = Object.Fields[FieldIndex];
		auto* Target = std::get_if<std::int32_t>(&Field);
		const auto* Source = std::get_if<std::int32_t>(&Value);
		// A pending exception must retain the ordinary release protocol.
		if (Target && Source && !bHasPendingScriptException)
		{
			*Target = *Source;
		}
		else
		{
			FValue* const Storage = ResolveRefStorage(Field);
			if (!Storage)
			{
				ReportError("invalid referenced field storage", Location);
				return false;
			}
			if (!AssignValue(*Storage, Value, Location))
			{
				return false;
			}
			StampContainerMutationOrigin(Object.Fields[FieldIndex], Object, FieldIndex);
		}
		if (bNotifyMutation)
		{
			NotifyFieldMutation(Object, FieldIndex);
		}
		return true;
	}

	void VeroScriptVm::NotifyFieldMutation(
	    FScriptObject& Object,
	    const std::uint16_t FieldIndex,
	    const EMutationKind Kind,
	    const std::size_t Depth)
	{
		if (Depth >= 64)
		{
			return;
		}
		++Object.MutationVersion;
		if (!MutationCallback)
		{
			return;
		}
		FMutationEvent Event;
		Event.Object = &Object;
		Event.ObjectTypeName = Object.GetTypeName();
		Event.FieldIndex = FieldIndex;
		Event.Kind = Kind;
		Event.ObjectMutationVersion = Object.MutationVersion;
		if (FieldIndex < Object.Fields.size())
		{
			Event.Value = UnwrapClosureReferenceCell(&Object.Fields[FieldIndex]);
		}
		if (ActiveModule)
		{
			const std::uint16_t TypeIndex = ResolveObjectTypeIndex(Object);
			if (TypeIndex < ActiveModule->Types.size() && FieldIndex < ActiveModule->Types[TypeIndex].Fields.size())
			{
				Event.FieldName = ActiveModule->Types[TypeIndex].Fields[FieldIndex].Name;
			}
		}
		try
		{
			MutationCallback(Event);
		}
		catch (const std::exception& Exception)
		{
			ReportError(std::string("mutation callback failed: ") + Exception.what());
		}
		catch (...)
		{
			ReportError("mutation callback failed");
		}
		if (Object.Kind == EScriptObjectKind::Struct)
		{
			if (const FObjectHandle Parent = Object.MutationOrigin.Object.lock();
			    Parent && Parent.get() != &Object)
			{
				NotifyFieldMutation(
				    *Parent,
				    Object.MutationOrigin.FieldIndex,
				    Kind,
				    Depth + 1);
			}
		}
	}

	void VeroScriptVm::StampContainerMutationOrigin(
	    FValue& Value,
	    FScriptObject& Object,
	    const std::uint16_t FieldIndex)
	{
		const FContainerMutationOrigin Origin{Object.weak_from_this(), FieldIndex};
		if (Memory::IsRegion(Value))
		{
			WatchMemoryMutation(Value, Object, FieldIndex);
		}
		if (auto* NestedObject = std::get_if<FObjectHandle>(&Value);
		    NestedObject && *NestedObject && (*NestedObject)->Kind == EScriptObjectKind::Struct)
		{
			(*NestedObject)->MutationOrigin = Origin;
		}
		else if (auto* Array = std::get_if<FDynamicArrayHandle>(&Value); Array && *Array)
		{
			(*Array)->MutationOrigin = Origin;
		}
		else if (auto* NativeArray = std::get_if<FNativeArray>(&Value))
		{
			NativeArray->Origin() = Origin;
		}
		else if (auto* Map = std::get_if<FMapHandle>(&Value); Map && *Map)
		{
			(*Map)->MutationOrigin = Origin;
		}
		else if (auto* Set = std::get_if<FSetHandle>(&Value); Set && *Set)
		{
			(*Set)->MutationOrigin = Origin;
		}
		else if (auto* Queue = std::get_if<FQueueHandle>(&Value); Queue && *Queue)
		{
			(*Queue)->MutationOrigin = Origin;
		}
		else if (auto* Stack = std::get_if<FStackHandle>(&Value); Stack && *Stack)
		{
			(*Stack)->MutationOrigin = Origin;
		}
		else if (auto* OrderedMap = std::get_if<FOrderedMapHandle>(&Value);
		    OrderedMap && *OrderedMap)
		{
			(*OrderedMap)->MutationOrigin = Origin;
		}
		else if (auto* Deque = std::get_if<FDequeHandle>(&Value); Deque && *Deque)
		{
			(*Deque)->MutationOrigin = Origin;
		}
		else if (auto* Channel = std::get_if<FChannelHandle>(&Value); Channel && *Channel)
		{
			(*Channel)->SetMutationOrigin(Origin);
		}
	}

	void VeroScriptVm::NotifyContainerMutation(
	    const FValue& Container,
	    const EMutationKind Kind)
	{
		const FContainerMutationOrigin* Origin = nullptr;
		FContainerMutationOrigin ChannelOrigin;
		if (auto* Array = std::get_if<FDynamicArrayHandle>(&Container); Array && *Array)
		{
			Origin = &(*Array)->MutationOrigin;
		}
		else if (auto* NativeArray = std::get_if<FNativeArray>(&Container))
		{
			Origin = &NativeArray->Origin();
		}
		else if (auto* Map = std::get_if<FMapHandle>(&Container); Map && *Map)
		{
			Origin = &(*Map)->MutationOrigin;
		}
		else if (auto* Set = std::get_if<FSetHandle>(&Container); Set && *Set)
		{
			Origin = &(*Set)->MutationOrigin;
		}
		else if (auto* Queue = std::get_if<FQueueHandle>(&Container); Queue && *Queue)
		{
			Origin = &(*Queue)->MutationOrigin;
		}
		else if (auto* Stack = std::get_if<FStackHandle>(&Container); Stack && *Stack)
		{
			Origin = &(*Stack)->MutationOrigin;
		}
		else if (auto* OrderedMap = std::get_if<FOrderedMapHandle>(&Container);
		    OrderedMap && *OrderedMap)
		{
			Origin = &(*OrderedMap)->MutationOrigin;
		}
		else if (auto* Deque = std::get_if<FDequeHandle>(&Container); Deque && *Deque)
		{
			Origin = &(*Deque)->MutationOrigin;
		}
		else if (auto* Channel = std::get_if<FChannelHandle>(&Container);
		    Channel && *Channel)
		{
			if (!(*Channel)->TryGetMutationOriginForCurrentThread(ChannelOrigin))
			{
				return;
			}
			Origin = &ChannelOrigin;
		}
		if (!Origin || Origin->FieldIndex == static_cast<std::uint16_t>(-1))
		{
			return;
		}
		if (const FObjectHandle Object = Origin->Object.lock())
		{
			NotifyFieldMutation(*Object, Origin->FieldIndex, Kind);
		}
	}

	FValue* VeroScriptVm::ResolveRefStorage(
	    FValue& Value,
	    const bool bUnwrapClosureCell,
	    const std::size_t Depth)
	{
		constexpr std::size_t MaxReferenceDepth = 64;
		if (Depth >= MaxReferenceDepth)
		{
			return nullptr;
		}
		const auto ResolveNext = [&](FValue& Next) -> FValue*
		{
			return ResolveRefStorage(Next, bUnwrapClosureCell, Depth + 1);
		};
		if (const auto* LocalRef = std::get_if<FLocalRef>(&Value))
		{
			if (LocalRef->FrameIndex >= Frames.size())
			{
				return nullptr;
			}
			FCallFrame& TargetFrame = Frames[LocalRef->FrameIndex];
			if (LocalRef->Slot >= TargetFrame.Locals.size())
			{
				return nullptr;
			}
			return ResolveNext(TargetFrame.Locals[LocalRef->Slot]);
		}
		if (const auto* GlobalRef = std::get_if<FGlobalRef>(&Value))
		{
			if (GlobalRef->Slot >= Globals.size())
			{
				return nullptr;
			}
			return ResolveNext(Globals[GlobalRef->Slot]);
		}
		if (auto* ThisFieldRef = std::get_if<FThisFieldRef>(&Value))
		{
			if (!ThisFieldRef->Object || ThisFieldRef->Object->bDestroyed || ThisFieldRef->FieldIndex >= ThisFieldRef->Object->Fields.size())
			{
				return nullptr;
			}
			return ResolveNext(ThisFieldRef->Object->Fields[ThisFieldRef->FieldIndex]);
		}
		if (const auto* IndexRef = std::get_if<FIndexRef>(&Value))
		{
			FValue* Storage = nullptr;
			if (IndexRef->Target)
			{
				Storage = VmDetail::FindReferenceElement(*IndexRef->Target, IndexRef->Index);
			}
			else if (IndexRef->FrameIndex < Frames.size() && IndexRef->ArraySlot < Frames[IndexRef->FrameIndex].Locals.size())
			{
				// Only compiler assignment paths remain unbound. Explicit borrows
				// always carry an original-container target, including failed targets.
				if (auto* Container = ResolveNext(Frames[IndexRef->FrameIndex].Locals[IndexRef->ArraySlot]))
				{
					Storage = VmDetail::FindIndexPathElement(*Container, IndexRef->Index);
				}
			}
			return Storage ? ResolveNext(*Storage) : nullptr;
		}
		if (const auto* MapRef = std::get_if<FMapEntryRef>(&Value))
		{
			FValue* Storage = MapRef->Target ? VmDetail::FindReferenceElement(*MapRef->Target, 0) : nullptr;
			return Storage ? ResolveNext(*Storage) : nullptr;
		}
		if (bUnwrapClosureCell && IsClosureReferenceCell(Value))
		{
			FValue* const Visible = UnwrapClosureReferenceCell(&Value);
			if (!Visible)
			{
				return nullptr;
			}
			// A well-formed cell contains a value. Continue resolving defensively in
			// case a host supplied a reference alternative inside the reserved cell.
			return ResolveRefStorage(*Visible, true, Depth + 1);
		}
		return &Value;
	}

	bool VeroScriptVm::TryGetReferenceMutationOrigin(
	    const FValue& Binding,
	    FContainerMutationOrigin& OutOrigin,
	    bool& bOutContainerElement,
	    const std::size_t Depth)
	{
		if (Depth >= 64)
		{
			return false;
		}
		const auto AcceptOrigin = [&](const FContainerMutationOrigin& Origin,
		                              const bool bContainerElement) -> bool
		{
			if (Origin.FieldIndex == static_cast<std::uint16_t>(-1) || Origin.Object.expired())
			{
				return false;
			}
			OutOrigin = Origin;
			bOutContainerElement = bContainerElement;
			return true;
		};

		if (IsClosureReferenceCell(Binding))
		{
			const FDynamicArrayHandle& Cell = std::get<FDynamicArrayHandle>(Binding);
			return AcceptOrigin(
			    Cell->MutationOrigin,
			    Cell->MutationOrigin.bContainerElement);
		}
		if (const auto* LocalRef = std::get_if<FLocalRef>(&Binding))
		{
			if (LocalRef->FrameIndex >= Frames.size() || LocalRef->Slot >= Frames[LocalRef->FrameIndex].Locals.size())
			{
				return false;
			}
			return TryGetReferenceMutationOrigin(
			    Frames[LocalRef->FrameIndex].Locals[LocalRef->Slot],
			    OutOrigin,
			    bOutContainerElement,
			    Depth + 1);
		}
		if (const auto* GlobalRef = std::get_if<FGlobalRef>(&Binding))
		{
			if (GlobalRef->Slot >= Globals.size())
			{
				return false;
			}
			return TryGetReferenceMutationOrigin(
			    Globals[GlobalRef->Slot],
			    OutOrigin,
			    bOutContainerElement,
			    Depth + 1);
		}
		if (const auto* FieldRef = std::get_if<FThisFieldRef>(&Binding))
		{
			if (!FieldRef->Object || FieldRef->FieldIndex >= FieldRef->Object->Fields.size())
			{
				return false;
			}
			FContainerMutationOrigin Origin;
			Origin.Object = FieldRef->Object;
			Origin.FieldIndex = FieldRef->FieldIndex;
			return AcceptOrigin(Origin, false);
		}
		if (const auto* IndexRef = std::get_if<FIndexRef>(&Binding))
		{
			if (IndexRef->Target) return AcceptOrigin(IndexRef->Target->Origin, true);
			if (IndexRef->FrameIndex >= Frames.size() || IndexRef->ArraySlot >= Frames[IndexRef->FrameIndex].Locals.size()) return false;
			const auto* Container = ResolveRefStorage(Frames[IndexRef->FrameIndex].Locals[IndexRef->ArraySlot]);
			if (!Container) return false;
			if (const auto* Native = std::get_if<FNativeArray>(Container)) return AcceptOrigin(Native->Origin(), true);
			if (const auto* Array = std::get_if<FDynamicArrayHandle>(Container); Array && *Array) return AcceptOrigin((*Array)->MutationOrigin, true);
			if (const auto* Deque = std::get_if<FDequeHandle>(Container); Deque && *Deque) return AcceptOrigin((*Deque)->MutationOrigin, true);
			return false;
		}
		if (const auto* MapRef = std::get_if<FMapEntryRef>(&Binding))
		{
			return MapRef->Target && AcceptOrigin(MapRef->Target->Origin, true);
		}
		return false;
	}

	void VeroScriptVm::NotifyReferenceMutation(FValue& Binding)
	{
		FContainerMutationOrigin Origin;
		bool bContainerElement = false;
		if (TryGetReferenceMutationOrigin(Binding, Origin, bContainerElement))
		{
			if (const FObjectHandle Object = Origin.Object.lock();
			    Object && Origin.FieldIndex < Object->Fields.size())
			{
				// Assigning a container or nested struct through a reference must retain
				// the same parent-field origin for its later in-place mutations.
				if (FValue* const MutatedValue = ResolveRefStorage(Binding))
				{
					StampContainerMutationOrigin(
					    *MutatedValue,
					    *Object,
					    Origin.FieldIndex);
				}
				NotifyFieldMutation(
				    *Object,
				    Origin.FieldIndex,
				    bContainerElement
				        ? EMutationKind::ContainerSet
				        : EMutationKind::FieldWrite);
			}
		}
	}

	bool VeroScriptVm::BindElementReference(FValue& Binding, const FSourceLocation& Location)
	{
		auto* IndexRef = std::get_if<FIndexRef>(&Binding);
		auto* MapRef = std::get_if<FMapEntryRef>(&Binding);
		if (!IndexRef && !MapRef) return false;
		auto& Target = IndexRef ? IndexRef->Target : MapRef->Target;
		if (Target && Target->bBound) return ResolveRefStorage(Binding) != nullptr;
		const auto Frame = IndexRef ? IndexRef->FrameIndex : MapRef->FrameIndex;
		const auto Slot = IndexRef ? IndexRef->ArraySlot : MapRef->MapSlot;
		if (Frame >= Frames.size() || Slot >= Frames[Frame].Locals.size())
		{
			ReportError("invalid container reference binding", Location);
			return false;
		}
		FValue* Container = ResolveRefStorage(Frames[Frame].Locals[Slot]);
		if (!Container)
		{
			ReportError("invalid container reference binding", Location);
			return false;
		}
		if (!Target)
		{
			Target = std::make_shared<FContainerElementReference>();
		}
		const bool bContainerBound = std::visit([&](auto& Value)
		{
			using T = std::decay_t<decltype(Value)>;
			if constexpr (std::is_same_v<T, FNativeArray>)
			{
				if (!IndexRef) return false;
				auto& Storage = Value.Storage();
				if (!Storage.ReferenceLifetime)
				{
					Storage.ReferenceLifetime = std::make_shared<int>(0);
				}
				Target->Container = std::weak_ptr<const void>(Storage.ReferenceLifetime);
				Target->NativeStorage = &Storage;
				Target->Origin = Storage.MutationOrigin;
				return true;
			}
			else if constexpr (std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FDequeHandle>
			    || std::is_same_v<T, FMapHandle> || std::is_same_v<T, FOrderedMapHandle>)
			{
				if (!Value) return false;
				constexpr bool bSequence = std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FDequeHandle>;
				if (bSequence != (IndexRef != nullptr)) return false;
				if constexpr (std::is_same_v<T, FDynamicArrayHandle>)
				{
					Value->MaterializeBoxedElements();
				}
				Target->Container = std::weak_ptr<typename T::element_type>(Value);
				Target->Origin = Value->MutationOrigin;
				return true;
			}
			else
			{
				return false;
			}
		}, *Container);
		FValue* Element = bContainerBound ? VmDetail::FindReferenceElement(*Target, IndexRef ? IndexRef->Index : 0) : nullptr;
		if (!Element)
		{
			ReportError("container reference target does not exist", Location);
			return false;
		}
		FDynamicArrayHandle Cell;
		if (IsClosureReferenceCell(*Element))
		{
			Cell = std::get<FDynamicArrayHandle>(*Element);
		}
		else
		{
			Cell = std::make_shared<FDynamicArray>();
			Cell->TypeName.assign(ClosureReferenceCellTypeName);
			Cell->ElementTypeName = "__internal";
			Cell->MutationOrigin = Target->Origin;
			Cell->MutationOrigin.bContainerElement = true;
			Cell->Elements.push_back(std::move(*Element));
			*Element = Cell;
		}
		Target->Cell = Cell;
		Target->bBound = true;
		return true;
	}

	bool VeroScriptVm::PromoteReferenceCapture(
	    FValue Binding,
	    FValue& OutCell,
	    const FSourceLocation& Location)
	{
		FValue* const Storage = ResolveRefStorage(Binding, false);
		if (!Storage)
		{
			ReportError("invalid reference capture binding", Location);
			return false;
		}
		if (IsClosureReferenceCell(*Storage))
		{
			OutCell = *Storage;
			return true;
		}
		FContainerMutationOrigin Origin;
		bool bContainerElement = false;
		const bool bHasMutationOrigin = TryGetReferenceMutationOrigin(
		    Binding, Origin, bContainerElement);
		auto Cell = std::make_shared<FDynamicArray>();
		Cell->TypeName.assign(ClosureReferenceCellTypeName);
		Cell->ElementTypeName = "__internal";
		if (bHasMutationOrigin)
		{
			Origin.bContainerElement = bContainerElement;
			Cell->MutationOrigin = std::move(Origin);
		}
		Cell->Elements.push_back(std::move(*Storage));
		*Storage = Cell;
		OutCell = std::move(Cell);
		return true;
	}

	bool VeroScriptVm::PrepareClosureCaptures(
	    const std::uint16_t FunctionIndex,
	    std::vector<FValue>& Captures,
	    const FSourceLocation& Location)
	{
		if (!ActiveModule || FunctionIndex >= ActiveModule->Functions.size() || ActiveModule->Functions[FunctionIndex].Captures.size() != Captures.size())
		{
			ReportError("closure capture schema mismatch", Location);
			return false;
		}
		const auto& Schema = ActiveModule->Functions[FunctionIndex].Captures;
		for (std::size_t Index = 0; Index < Captures.size(); ++Index)
		{
			if (Schema[Index].Mode == EClosureCaptureMode::Reference)
			{
				FValue Cell;
				if (!PromoteReferenceCapture(Captures[Index], Cell, Location)) return false;
				Captures[Index] = std::move(Cell);
			}
			else if (Schema[Index].Mode == EClosureCaptureMode::MutableValue || Schema[Index].bStableStorage)
			{
				auto Cell = std::make_shared<FDynamicArray>();
				Cell->TypeName.assign(Schema[Index].Mode == EClosureCaptureMode::Value
				        ? ClosureReadonlyCellTypeName
				        : ClosureReferenceCellTypeName);
				Cell->ElementTypeName = "__internal";
				Cell->Elements.push_back(std::move(Captures[Index]));
				Captures[Index] = std::move(Cell);
			}
		}
		return true;
	}

	bool VeroScriptVm::CreateClosure(const std::uint16_t FunctionIndex, std::vector<FValue> Captures,
	    const FObjectHandle& Receiver, FFunctionHandle& Out, const FSourceLocation& Location)
	{
		FFunctionHandle Function;
		try
		{
			if (PrepareClosureCaptures(FunctionIndex, Captures, Location))
			{
				Function = std::make_shared<FFunctionValue>();
				const auto& Description = ActiveModule->Functions[FunctionIndex];
				Function->FunctionIndex = FunctionIndex;
				Function->ProgramGeneration = ProgramGeneration;
				Function->ProgramOwner = ProgramOwner;
				Function->FunctionName = Description.Name;
				Function->TypeName = Description.ClosureTypeName;
				Function->Captures = std::move(Captures);
				if (Description.bIsMethod && Receiver)
				{
					if (Receiver->Kind == EScriptObjectKind::Struct)
					{
						FValue Copy;
						if (CopyLanguageValue(FValue{Receiver}, Copy, Location))
						{
							Function->CapturedThis = std::get<FObjectHandle>(std::move(Copy));
						}
					}
					else
					{
						Function->CapturedThis = Receiver;
					}
				}
				if (!HasError())
				{
					Out = std::move(Function);
					return true;
				}
			}
		}
		catch (...)
		{
			ReportError("cannot create closure storage", Location);
		}
		if (Function)
		{
			FValue Incomplete = std::move(Function);
			(void)ReleaseValue(Incomplete, Location);
		}
		for (auto It = Captures.rbegin(); It != Captures.rend(); ++It)
		{
			(void)ReleaseValue(*It, Location);
		}
		return false;
	}

	bool VeroScriptVm::ExportFunctionReturn(
	    const FBytecodeFunction& Function,
	    FValue ReturnValue,
	    FValue* const OutValue)
	{
		if (OutValue == nullptr)
		{
			return ReleaseValue(ReturnValue, {});
		}
		if (Function.bReturnsVoid)
		{
			if (!ReleaseValue(ReturnValue, {}))
			{
				return false;
			}
			return AssignValue(*OutValue, FValue{std::int32_t{0}}, {});
		}
		if (Function.bReturnsRef)
		{
			FValue* const ReferencedValue = ResolveRefStorage(ReturnValue);
			FValue Snapshot;
			if (!ReferencedValue || ReferencedValue == &ReturnValue || !CloneValueOrReport(*ReferencedValue, Snapshot, {}))
			{
				if (!bHasError)
				{
					ReportError(
					    "function returned an invalid reference: " + Function.Name);
				}
				(void)ReleaseValue(ReturnValue, {});
				return false;
			}
			if (!ReleaseValue(ReturnValue, {}))
			{
				(void)ReleaseValue(Snapshot, {});
				return false;
			}
			ReturnValue = std::move(Snapshot);
		}
		return AssignValue(*OutValue, std::move(ReturnValue), {});
	}

	bool VeroScriptVm::ExpectBool(const FValue& Value, const FSourceLocation& Location)
	{
		if (!std::holds_alternative<bool>(Value))
		{
			ReportError("condition must be bool", Location);
			return false;
		}
		return true;
	}

	// Keep checked back-edges in the Wasm dispatch loop; finally transfers retain
	// their complete handling through TryTransferToOffset below.
	#if defined(__EMSCRIPTEN__)
	__attribute__((always_inline))
	#endif
	bool VeroScriptVm::TryJump(
	    FCallFrame& Frame,
	    const std::size_t CodeSize,
	    const std::int32_t Offset,
	    const FSourceLocation& Location)
	{
		const std::int64_t Next = static_cast<std::int64_t>(Frame.InstructionPointer) + Offset;
		if (Next < 0 || static_cast<std::size_t>(Next) > CodeSize)
		{
			ReportError("jump out of range", Location);
			return false;
		}
		// Ordinary control flow has no finally state to unwind. Keep loop
		// back-edges on the direct VM path; protected functions still use the
		// full transfer routine so their pending finally semantics are unchanged.
		if (Frame.FinallyContinuations.empty() && (!Frame.Function || Frame.Function->ExceptionRegions.empty()))
		{
			Frame.InstructionPointer = static_cast<std::size_t>(Next);
			return true;
		}
		return TryTransferToOffset(
		    static_cast<std::size_t>(&Frame - Frames.data()),
		    static_cast<std::size_t>(Next), Location);
	}

	bool VeroScriptVm::TryTransferToOffset(
	    const std::size_t FrameIndex,
	    const std::size_t TargetOffset,
	    const FSourceLocation& Location)
	{
		if (!Frames[FrameIndex].FinallyContinuations.empty() && !ReleaseFinallyContinuations(Frames[FrameIndex], Location, TargetOffset))
		{
			return false;
		}
		// Releasing a parked value can run a destructor and relocate Frames.
		FCallFrame& Frame = Frames[FrameIndex];

		const FBytecodeExceptionRegion* Selected = nullptr;
		if (Frame.Function)
		{
			for (const FBytecodeExceptionRegion& Region : Frame.Function->ExceptionRegions)
			{
				if (Region.FinallyOffset == std::numeric_limits<std::uint32_t>::max() || Frame.LastInstructionOffset < Region.BeginOffset || Frame.LastInstructionOffset >= Region.EndOffset || (TargetOffset >= Region.BeginOffset && TargetOffset < Region.EndOffset))
				{
					continue;
				}
				if (!Selected || (Region.EndOffset - Region.BeginOffset) < (Selected->EndOffset - Selected->BeginOffset))
				{
					Selected = &Region;
				}
			}
		}
		if (!Selected)
		{
			Frame.InstructionPointer = TargetOffset;
			return true;
		}

		FCallFrame::FFinallyContinuation Continuation;
		Continuation.Kind = FCallFrame::EFinallyContinuationKind::Jump;
		Continuation.TargetOffset = TargetOffset;
		Continuation.FinallyBeginOffset = Selected->FinallyOffset;
		Continuation.FinallyEndOffset = Selected->FinallyEndOffset;
		Frame.FinallyContinuations.push_back(std::move(Continuation));
		Frame.InstructionPointer = Selected->FinallyOffset;
		return true;
	}

	bool VeroScriptVm::BeginReturnOrComplete(
	    const std::size_t FrameIndex,
	    FValue ReturnValue,
	    const FSourceLocation& Location)
	{
		if (!Frames[FrameIndex].FinallyContinuations.empty() && !ReleaseFinallyContinuations(Frames[FrameIndex], Location))
		{
			return false;
		}
		FCallFrame& Frame = Frames[FrameIndex];

		const std::size_t CodeSize = Frame.Function ? Frame.Function->Code.size() : 0;
		const FBytecodeExceptionRegion* Selected = nullptr;
		if (Frame.Function)
		{
			for (const FBytecodeExceptionRegion& Region : Frame.Function->ExceptionRegions)
			{
				if (Region.FinallyOffset == std::numeric_limits<std::uint32_t>::max() || Frame.LastInstructionOffset < Region.BeginOffset || Frame.LastInstructionOffset >= Region.EndOffset)
				{
					continue;
				}
				if (!Selected || (Region.EndOffset - Region.BeginOffset) < (Selected->EndOffset - Selected->BeginOffset))
				{
					Selected = &Region;
				}
			}
		}
		if (Selected)
		{
			FCallFrame::FFinallyContinuation Continuation;
			Continuation.Kind = FCallFrame::EFinallyContinuationKind::Return;
			Continuation.FinallyBeginOffset = Selected->FinallyOffset;
			Continuation.FinallyEndOffset = Selected->FinallyEndOffset;
			Continuation.Value = std::move(ReturnValue);
			Frame.FinallyContinuations.push_back(std::move(Continuation));
			Frame.InstructionPointer = Selected->FinallyOffset;
			return true;
		}

		Frame.bHasReturn = true;
		if (Frame.Function && Frame.Function->bReturnsVoid)
		{
			if (!ReleaseValue(ReturnValue, Location))
			{
				return false;
			}
			Frames[FrameIndex].InstructionPointer = CodeSize;
			return true;
		}
		if (Frame.Function && Frame.Function->bReturnsRef)
		{
			FValue PersistentCell;
			if (!PromoteReferenceCapture(std::move(ReturnValue), PersistentCell, Location))
			{
				return false;
			}
			ReturnValue = std::move(PersistentCell);
		}
		Frame.ReturnValue = ReturnValue;
		Push(std::move(ReturnValue));
		Frame.InstructionPointer = CodeSize;
		return true;
	}

	bool VeroScriptVm::TryHandlePendingScriptException()
	{
		if (!bHasPendingScriptException || Frames.size() <= StopFrameDepth)
		{
			return false;
		}
		FScriptError PendingError = std::move(Error);
		bHasError = false;
		Error = {};
		// Keep the exception being resolved off the VM's pending slot. Cleanup can
		// run destructors with their own caught throws and must not replace it.
		FValue ExceptionValue = std::move(PendingScriptException);
		const FSourceLocation ExceptionLocation = PendingScriptExceptionLocation;
		PendingScriptException = std::int32_t{0};
		bHasPendingScriptException = false;
		PendingScriptExceptionLocation = {};
		const auto FinishExit = [&]()
		{
			const bool bReleasedException = ReleaseValue(ExceptionValue, ExceptionLocation);
			const bool bReleasedPending = ReleasePendingScriptException();
			return bReleasedException && bReleasedPending && !bHasError;
		};
		const auto RestoreException = [&]()
		{
			if (ExecutionCleanup)
			{
				RecordExecutionCleanupFailure(MakeDiagnostic("diag.execution_cleanup_failed", ExceptionLocation));
			}
			if (bExitRequested)
			{
				return FinishExit();
			}
			if (bHasPendingScriptException)
			{
				// An uncaught cleanup exception supersedes the transfer being resolved.
				// Suspend it while releasing the old value: that destructor can itself
				// catch or replace an exception without overwriting the saved transfer.
				FValue Replacement = std::move(PendingScriptException);
				FSourceLocation ReplacementLocation = PendingScriptExceptionLocation;
				PendingScriptException = std::int32_t{0};
				bHasPendingScriptException = false;
				PendingScriptExceptionLocation = {};
				(void)ReleaseValue(ExceptionValue, ExceptionLocation);
				while (bHasPendingScriptException)
				{
					FValue Superseded = std::move(Replacement);
					const FSourceLocation SupersededLocation = ReplacementLocation;
					Replacement = std::move(PendingScriptException);
					ReplacementLocation = PendingScriptExceptionLocation;
					PendingScriptException = std::int32_t{0};
					bHasPendingScriptException = false;
					PendingScriptExceptionLocation = {};
					(void)ReleaseValue(Superseded, SupersededLocation);
				}
				PendingScriptException = std::move(Replacement);
				bHasPendingScriptException = true;
				PendingScriptExceptionLocation = ReplacementLocation;
				return TryHandlePendingScriptException();
			}
			else
			{
				PendingScriptException = std::move(ExceptionValue);
				bHasPendingScriptException = true;
				PendingScriptExceptionLocation = ExceptionLocation;
			}
			return false;
		};
		const auto EnsurePendingError = [&]()
		{
			if (PendingError.Message.empty())
			{
				FillFromEnglishMessage(
				    PendingError,
				    "uncaught script exception",
				    ExceptionLocation);
				PendingError.bUncaughtScriptException = true;
			}
		};
		for (std::size_t FrameCount = Frames.size(); FrameCount > StopFrameDepth; --FrameCount)
		{
			const std::size_t FrameIndex = FrameCount - 1;
			FCallFrame& CandidateFrame = Frames[FrameIndex];
			if (!CandidateFrame.Function)
			{
				continue;
			}
			const FBytecodeExceptionRegion* SingleRegion = nullptr;
			std::size_t ActiveRegionCount = 0;
			for (const FBytecodeExceptionRegion& Region : CandidateFrame.Function->ExceptionRegions)
			{
				if (CandidateFrame.LastInstructionOffset >= Region.BeginOffset && CandidateFrame.LastInstructionOffset < Region.EndOffset)
				{
					SingleRegion = &Region;
					++ActiveRegionCount;
				}
			}
			std::vector<const FBytecodeExceptionRegion*> NestedRegions;
			if (ActiveRegionCount > 1)
			{
				NestedRegions.reserve(ActiveRegionCount);
				for (const FBytecodeExceptionRegion& Region : CandidateFrame.Function->ExceptionRegions)
				{
					if (CandidateFrame.LastInstructionOffset >= Region.BeginOffset && CandidateFrame.LastInstructionOffset < Region.EndOffset)
					{
						NestedRegions.push_back(&Region);
					}
				}
				std::sort(NestedRegions.begin(), NestedRegions.end(), [](const auto* Left, const auto* Right)
				{
					return (Left->EndOffset - Left->BeginOffset) < (Right->EndOffset - Right->BeginOffset);
				});
			}
			const auto RegionAt = [&](const std::size_t Index)
			{
				return ActiveRegionCount == 1 ? SingleRegion : NestedRegions[Index];
			};
			for (std::size_t RegionIndex = 0; RegionIndex < ActiveRegionCount; ++RegionIndex)
			{
				const FBytecodeExceptionRegion* const Region = RegionAt(RegionIndex);
				const FBytecodeCatchHandler* Matched = nullptr;
				for (const FBytecodeCatchHandler& Handler : Region->Catches)
				{
					if (Handler.TypeName.empty() || ValueMatchesElementType(ExceptionValue, Handler.TypeName, {}))
					{
						Matched = &Handler;
						break;
					}
				}
				if (Matched)
				{
					if (!ReleaseFinallyContinuations(
					        Frames[FrameIndex], CandidateFrame.Function->Location,
					        Matched->HandlerOffset))
					{
						return RestoreException();
					}
					if (bExitRequested)
					{
						return FinishExit();
					}

					MarkDiscardedFunctionDiagnostics(JitDiagnostics.get(), Frames, FrameIndex + 1, false, true);
					if (!DiscardExecutionAbove(FrameIndex + 1, Frames[FrameIndex].StackBase,
					        Frames[FrameIndex].Function->Location))
					{
						return RestoreException();
					}
					if (bExitRequested)
					{
						return FinishExit();
					}

					AbortLocalStaticInitializers(FrameIndex, Matched->HandlerOffset);
					if (Matched->LocalSlot >= Frames[FrameIndex].Locals.size() ||
					    Matched->RethrowSlot >= Frames[FrameIndex].Locals.size())
					{
						ReportError("invalid exception handler local slot");
						return RestoreException();
					}
					if (!AssignValue(
					        Frames[FrameIndex].Locals[Matched->RethrowSlot],
					        std::move(ExceptionValue), {}))
					{
						return false;
					}
					if (bExitRequested)
					{
						return FinishExit();
					}

					if (Matched->LocalSlot != Matched->RethrowSlot)
					{
						FValue BoundCopy;
						if (!CloneValueOrReport(
						        Frames[FrameIndex].Locals[Matched->RethrowSlot], BoundCopy, {}))
						{
							return false;
						}
						// A stamped host clone may reenter the VM and relocate Frames. Never
						// retain or reuse a frame reference across that callback boundary.
						if (bExitRequested)
						{
							(void)ReleaseValue(BoundCopy, {});
							return FinishExit();
						}
						if (FrameIndex >= Frames.size())
						{
							ReportError("exception handler frame invalidated during clone");
							return false;
						}
						if (!AssignValue(
						        Frames[FrameIndex].Locals[Matched->LocalSlot],
						        std::move(BoundCopy), {}))
						{
							return false;
						}
					}
					if (bExitRequested)
					{
						return FinishExit();
					}

					AbortLocalStaticInitializers(FrameIndex, Matched->HandlerOffset);
					Frames[FrameIndex].InstructionPointer = Matched->HandlerOffset;
					bHasError = false;
					Error = {};
					return true;
				}
				if (Region->FinallyOffset != std::numeric_limits<std::uint32_t>::max())
				{
					EnsurePendingError();
					if (!ReleaseFinallyContinuations(
					        Frames[FrameIndex], CandidateFrame.Function->Location,
					        Region->FinallyOffset))
					{
						return RestoreException();
					}
					if (bExitRequested)
					{
						return FinishExit();
					}

					MarkDiscardedFunctionDiagnostics(JitDiagnostics.get(), Frames, FrameIndex + 1, false, true);
					if (!DiscardExecutionAbove(FrameIndex + 1, Frames[FrameIndex].StackBase,
					        Frames[FrameIndex].Function->Location))
					{
						return RestoreException();
					}
					if (bExitRequested)
					{
						return FinishExit();
					}

					FCallFrame& TargetFrame = Frames[FrameIndex];
					FCallFrame::FFinallyContinuation Continuation;
					Continuation.Kind = FCallFrame::EFinallyContinuationKind::Exception;
					Continuation.FinallyBeginOffset = Region->FinallyOffset;
					Continuation.FinallyEndOffset = Region->FinallyEndOffset;
					Continuation.Value = std::move(ExceptionValue);
					// Preserve the original throw site and stack trace while the exception
					// is parked for finally execution. Re-raising at EndFinally must not make
					// an uncaught exception appear to originate from compiler-generated code.
					Continuation.Error = PendingError;
					TargetFrame.FinallyContinuations.push_back(std::move(Continuation));
					AbortLocalStaticInitializers(FrameIndex, Region->FinallyOffset);
					TargetFrame.InstructionPointer = Region->FinallyOffset;
					bHasError = false;
					Error = {};
					return true;
				}
			}
			if (!ReleaseFinallyContinuations(
			        Frames[FrameIndex], Frames[FrameIndex].Function->Location))
			{
				return RestoreException();
			}
			if (bExitRequested)
			{
				return FinishExit();
			}
		}
		EnsurePendingError();
		bHasError = true;
		Error = std::move(PendingError);
		return RestoreException();
	}

	bool VeroScriptVm::TryHandleCurrentFrameScriptException(
	    FValue& Value,
	    const FSourceLocation& Location,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (bHasPendingScriptException || Frames.size() <= StopFrameDepth)
		{
			return true;
		}
		const std::size_t FrameIndex = Frames.size() - 1u;
		FCallFrame& Frame = Frames[FrameIndex];
		if (!Frame.Function || Frame.Function->ExceptionRegions.empty())
		{
			return true;
		}
		// Throwing out of an active finally must first release/replace its parked
		// continuation. Keep that uncommon case on the authoritative unwinder.
		if (!Frame.FinallyContinuations.empty())
		{
			const FCallFrame::FFinallyContinuation& Active =
			    Frame.FinallyContinuations.back();
			if (Frame.LastInstructionOffset >= Active.FinallyBeginOffset && Frame.LastInstructionOffset < Active.FinallyEndOffset)
			{
				return true;
			}
		}

		// Most functions have one region; a fixed local list keeps a repeated
		// throw/catch loop allocation-free. Deeply generated nesting falls back to
		// the general unwinder instead of imposing a semantic limit.
		std::array<const FBytecodeExceptionRegion*, 16> ActiveRegions{};
		std::size_t ActiveRegionCount = 0;
		for (const FBytecodeExceptionRegion& Region : Frame.Function->ExceptionRegions)
		{
			if (Frame.LastInstructionOffset < Region.BeginOffset || Frame.LastInstructionOffset >= Region.EndOffset)
			{
				continue;
			}
			if (ActiveRegionCount >= ActiveRegions.size())
			{
				return true;
			}
			ActiveRegions[ActiveRegionCount++] = &Region;
		}
		if (ActiveRegionCount == 0)
		{
			return true;
		}
		if (ActiveRegionCount > 1)
		{
			std::sort(
			    ActiveRegions.begin(), ActiveRegions.begin() + ActiveRegionCount,
			    [](const FBytecodeExceptionRegion* const Left,
			        const FBytecodeExceptionRegion* const Right)
			{
				return (Left->EndOffset - Left->BeginOffset) < (Right->EndOffset - Right->BeginOffset);
			});
		}

		const FBytecodeCatchHandler* Matched = nullptr;
		for (std::size_t RegionIndex = 0;
		    RegionIndex < ActiveRegionCount; ++RegionIndex)
		{
			const FBytecodeExceptionRegion& Region = *ActiveRegions[RegionIndex];
			for (const FBytecodeCatchHandler& Handler : Region.Catches)
			{
				if (Handler.TypeName.empty() || ValueMatchesElementType(Value, Handler.TypeName, {}))
				{
					Matched = &Handler;
					break;
				}
			}
			if (Matched)
			{
				break;
			}
			if (Region.FinallyOffset != std::numeric_limits<std::uint32_t>::max())
			{
				// An inner finally must execute before an outer catch.
				return true;
			}
		}
		if (!Matched)
		{
			return true;
		}
		const std::size_t StackBase = Frame.StackBase;
		const FSourceLocation FunctionLocation = Frame.Function->Location;
		if ((Frames.size() != FrameIndex + 1u || OperandStack.size() != StackBase) && !DiscardExecutionAbove(FrameIndex + 1u, StackBase, FunctionLocation))
		{
			return false;
		}
		if (FrameIndex >= Frames.size() || Matched->LocalSlot >= Frames[FrameIndex].Locals.size() || Matched->RethrowSlot >= Frames[FrameIndex].Locals.size())
		{
			ReportError("invalid exception handler local slot", Location);
			return false;
		}
		FValue& RethrowLocal = Frames[FrameIndex].Locals[Matched->RethrowSlot];
		FValue& BoundLocal = Frames[FrameIndex].Locals[Matched->LocalSlot];
		const bool bTrivialCaughtValue =
		    (Runtime::IsNumericValue(Value) || std::holds_alternative<bool>(Value)) && RethrowLocal.index() == Value.index() && BoundLocal.index() == Value.index();
		if (bTrivialCaughtValue)
		{
			// Primitive catch values have no ownership or script-visible release hook.
			// Keep the rethrow copy and the named binding in their pretyped local slots
			// without routing through clone/release on every hot throw.
			if (Matched->LocalSlot != Matched->RethrowSlot)
			{
				BoundLocal = Value;
			}
			RethrowLocal = std::move(Value);
		}
		else
		{
			if (!AssignValue(RethrowLocal, std::move(Value), {}))
			{
				return false;
			}
			if (Matched->LocalSlot != Matched->RethrowSlot)
			{
				FValue BoundCopy;
				if (!CloneValueOrReport(
				        Frames[FrameIndex].Locals[Matched->RethrowSlot], BoundCopy, {}))
				{
					return false;
				}
				if (FrameIndex >= Frames.size())
				{
					ReportError("exception handler frame invalidated during clone");
					return false;
				}
				if (!AssignValue(
				        Frames[FrameIndex].Locals[Matched->LocalSlot],
				        std::move(BoundCopy), {}))
				{
					return false;
				}
			}
		}
		AbortLocalStaticInitializers(FrameIndex, Matched->HandlerOffset);
		Frames[FrameIndex].InstructionPointer = Matched->HandlerOffset;
		bHasError = false;
		Error = {};
		OutHandled = true;
		return true;
	}

	bool VeroScriptVm::TryHandlePendingRuntimeFinally()
	{
		if (SkipScriptCleanup()) return false;
		if (!bHasError || bHasPendingScriptException || Frames.size() <= StopFrameDepth)
		{
			return false;
		}
		const bool bHadExecutionCleanup = ExecutionCleanup != nullptr;
		FScriptError PendingError = Error;
		// A later fault inside finally must not overwrite the first fault parked
		// by an enclosing continuation. Script catches inside finally remain valid.
		bool bFoundPrimaryFault = false;
		for (const auto& PendingFrame : Frames)
		{
			for (const auto& Continuation : PendingFrame.FinallyContinuations)
			{
				if (Continuation.Kind == FCallFrame::EFinallyContinuationKind::FatalError)
				{
					PendingError = Continuation.Error;
					bFoundPrimaryFault = true;
					break;
				}
			}
			if (bFoundPrimaryFault)
			{
				break;
			}
		}
		bHasError = false;
		Error = {};
		const auto RestoreFailure = [&]()
		{
			bHasError = true;
			Error = std::move(PendingError);
			return false;
		};
		const auto DiscardCleanupException = [&]()
		{
			// The primary runtime fault remains in PendingError. Cleanup helpers
			// finish their release pass even when a destructor fails; discard its
			// secondary transfer before entering the selected finally.
			(void)ReleasePendingScriptException();
			if (!bHadExecutionCleanup)
			{
				PreserveExecutionCleanupError(PendingError);
			}
			bHasError = false;
			Error = {};
		};
		for (std::size_t FrameCount = Frames.size(); FrameCount > StopFrameDepth; --FrameCount)
		{
			const std::size_t FrameIndex = FrameCount - 1;
			FCallFrame& CandidateFrame = Frames[FrameIndex];
			if (!CandidateFrame.Function)
			{
				continue;
			}
			const FBytecodeExceptionRegion* Selected = nullptr;
			for (const FBytecodeExceptionRegion& Region : CandidateFrame.Function->ExceptionRegions)
			{
				if (Region.FinallyOffset == std::numeric_limits<std::uint32_t>::max() || CandidateFrame.LastInstructionOffset < Region.BeginOffset || CandidateFrame.LastInstructionOffset >= Region.EndOffset)
				{
					continue;
				}
				if (!Selected || (Region.EndOffset - Region.BeginOffset) < (Selected->EndOffset - Selected->BeginOffset))
				{
					Selected = &Region;
				}
			}
			if (!Selected)
			{
				(void)ReleaseFinallyContinuations(Frames[FrameIndex], CandidateFrame.Function->Location);
				DiscardCleanupException();
				if (bExitRequested)
				{
					return RestoreFailure();
				}

				continue;
			}
			(void)ReleaseFinallyContinuations(Frames[FrameIndex], CandidateFrame.Function->Location,
			    Selected->FinallyOffset);
			DiscardCleanupException();
			if (bExitRequested)
			{
				return RestoreFailure();
			}

			MarkDiscardedFunctionDiagnostics(JitDiagnostics.get(), Frames, FrameIndex + 1, true, false);
			(void)DiscardExecutionAbove(
			    FrameIndex + 1, Frames[FrameIndex].StackBase, Frames[FrameIndex].Function->Location);
			DiscardCleanupException();
			if (bExitRequested)
			{
				return RestoreFailure();
			}

			FCallFrame& TargetFrame = Frames[FrameIndex];
			FCallFrame::FFinallyContinuation Continuation;
			Continuation.Kind = FCallFrame::EFinallyContinuationKind::FatalError;
			Continuation.FinallyBeginOffset = Selected->FinallyOffset;
			Continuation.FinallyEndOffset = Selected->FinallyEndOffset;
			Continuation.Error = std::move(PendingError);
			TargetFrame.FinallyContinuations.push_back(std::move(Continuation));
			AbortLocalStaticInitializers(FrameIndex, Selected->FinallyOffset);
			TargetFrame.InstructionPointer = Selected->FinallyOffset;
			bHasError = false;
			Error = {};
			return true;
		}
		bHasError = true;
		Error = std::move(PendingError);
		return false;
	}

	bool VeroScriptVm::RaiseScriptException(
	    FValue Value,
	    const FSourceLocation& Location)
	{
		// Resolve the catch/finally target before replacing any parked transfer:
		// a throw caught inside the active finally must preserve its continuation.
		if (bHasPendingScriptException)
		{
			// Destruction may throw/catch and rewrite the pending slot. The old
			// object handle must stay in stable storage until destruction completes.
			FValue Previous = std::move(PendingScriptException);
			PendingScriptException = std::int32_t{0};
			bHasPendingScriptException = false;
			PendingScriptExceptionLocation = {};
			(void)ReleaseValue(Previous, Location);
		}
		PendingScriptException = std::move(Value);
		bHasPendingScriptException = true;
		// A caught exception is a control-flow transfer, not a diagnostic. Preserve
		// the precise throw location and materialize the error only if no catch is
		// found (or if a finally continuation must retain it).
		PendingScriptExceptionLocation = RefineErrorLocation(Location);
		return false;
	}

	bool VeroScriptVm::ReleaseFinallyContinuations(
	    FCallFrame& Frame,
	    const FSourceLocation& Location,
	    const std::size_t TargetOffset)
	{
		std::size_t KeepCount = Frame.FinallyContinuations.size();
		while (KeepCount != 0)
		{
			const FCallFrame::FFinallyContinuation& Active =
			    Frame.FinallyContinuations[KeepCount - 1u];
			if (TargetOffset >= Active.FinallyBeginOffset && TargetOffset < Active.FinallyEndOffset)
			{
				break;
			}
			--KeepCount;
		}
		if (KeepCount == Frame.FinallyContinuations.size())
		{
			return true;
		}
		// Detach discarded values before any destructor can reenter the VM and
		// relocate Frames. Keep enclosing transfers whose finally contains target.
		std::vector<FCallFrame::FFinallyContinuation> Discarded;
		if (KeepCount == 0)
		{
			Discarded.swap(Frame.FinallyContinuations);
		}
		else
		{
			Discarded.reserve(Frame.FinallyContinuations.size() - KeepCount);
			for (std::size_t Index = KeepCount;
			    Index < Frame.FinallyContinuations.size(); ++Index)
			{
				Discarded.push_back(std::move(Frame.FinallyContinuations[Index]));
			}
			Frame.FinallyContinuations.resize(KeepCount);
		}
		bool bOk = true;
		for (auto It = Discarded.rbegin(); It != Discarded.rend(); ++It)
		{
			if (It->Kind == FCallFrame::EFinallyContinuationKind::FatalError)
			{
				if (ExecutionCleanup)
				{
					RecordExecutionCleanupFailure(MakeDiagnostic("diag.execution_cleanup_failed", Location));
				}
				// This continuation is being abandoned, rather than completing through
				// EndFinally. Keep its non-catchable fault even when a script throw or
				// exit replaces the surrounding transfer. Enclosing faults take priority.
				bHasError = true;
				Error = std::move(It->Error);
				bOk = false;
			}
			if ((It->Kind == FCallFrame::EFinallyContinuationKind::Return || It->Kind == FCallFrame::EFinallyContinuationKind::Exception) && !ReleaseValue(It->Value, Location))
			{
				bOk = false;
			}
		}
		return bOk;
	}

	bool VeroScriptVm::EvalExpressionCode(
	    const std::vector<std::uint8_t>& Code,
	    FValue& OutValue,
	    const FSourceLocation& Location,
	    const std::uint16_t LocalCount)
	{
		if (Code.empty())
		{
			ReportError("empty field initializer", Location);
			return false;
		}

		FBytecodeFunction TempFunction;
		TempFunction.Code = Code;
		TempFunction.LocalCount = LocalCount;
		FCallFrame TempFrame;
		TempFrame.Function = &TempFunction;
		TempFrame.bExpressionEval = true;
		TempFrame.StackBase = OperandStack.size();
		if (LocalCount > 0)
		{
			TempFrame.Locals.assign(LocalCount, std::int32_t{0});
		}
		const std::size_t DepthBefore = Frames.size();
		const std::size_t StackBefore = OperandStack.size();
		Frames.push_back(std::move(TempFrame));
		const std::size_t SavedStopDepth = StopFrameDepth;
		StopFrameDepth = DepthBefore;
		const bool bOk = Run();
		StopFrameDepth = SavedStopDepth;
		if (!bOk || bExitRequested || OperandStack.size() != StackBefore + 1)
		{
			(void)DiscardExecutionAbove(DepthBefore, StackBefore, Location);
			if (!bHasError && !bHasPendingScriptException && !bExitRequested)
			{
				ReportError("field initializer produced no value", Location);
			}
			return false;
		}
		OutValue = Pop();
		return DiscardExecutionAbove(DepthBefore, StackBefore, Location);
	}

	void VeroScriptVm::ReportError(std::string Message, const FSourceLocation& Location)
	{
		if (!bHasError)
		{
			bHasError = true;
			bJitErrorLocationPinned = false;
			FillFromEnglishMessage(Error, std::move(Message), RefineErrorLocation(Location));
			RecordExecutionCleanupFailure(Error);
		}
	}

	void VeroScriptVm::ReportErrorId(
	    const std::string_view MessageId,
	    const FSourceLocation& Location,
	    const std::initializer_list<FDiagnosticArg> Args)
	{
		if (!bHasError)
		{
			bHasError = true;
			bJitErrorLocationPinned = false;
			FillDiagnostic(Error, MessageId, RefineErrorLocation(Location), Args);
			RecordExecutionCleanupFailure(Error);
		}
	}

	bool VeroScriptVm::HostObjectMatchesType(const FHostObjectHandle& Object, std::string_view Expected) const
	{
		if (Object && Object->ProviderIdentity)
		{
			std::shared_ptr<const void> Identity;
			const auto* Catalog = bUseEnvironmentBindings && ModuleEnvironment && ModuleEnvironment->GetDescription().Bindings
			    ? ModuleEnvironment->GetDescription().Bindings.get()
			    : MemoryBindings;
			if (Catalog)
			{
				const auto* Type = Catalog->FindType(Object->TypeName);
				if (Type)
				{
					Identity = Type->ProviderIdentity;
				}
			}
			if (!Identity && ActiveNatives)
			{
				if (ActiveNativeIndexSource != ActiveNatives)
				{
					(void)FindActiveNativeIndexByName(Object->TypeName);
				}
				const auto Found = ActiveNativeProviderByType.find(Object->TypeName);
				if (Found != ActiveNativeProviderByType.end() && Found->second < ActiveNatives->size())
				{
					const auto& Native = (*ActiveNatives)[Found->second];
					if (Native.HostTypeName == Object->TypeName && Native.ProviderIdentity)
					{
						Identity = Native.ProviderIdentity;
					}
				}
				if (!Identity)
				{
					// Retain diagnostics for oversized or manually replaced tables.
					for (const auto& Native : *ActiveNatives)
					{
						if (Native.HostTypeName == Object->TypeName && Native.ProviderIdentity)
						{
							Identity = Native.ProviderIdentity;
							break;
						}
					}
				}
			}

			if (!Identity || Identity.owner_before(Object->ProviderIdentity) ||
			    Object->ProviderIdentity.owner_before(Identity))
			{
				return false;
			}
		}
		return VeroScript::HostObjectMatchesType(Object, Expected);
	}

	void VeroScriptVm::StampHostValueCallbacks(const FHostObjectHandle& Handle)
	{
		if (!Handle || !ActiveNatives || Handle->TypeName.empty() || !HostObjectMatchesType(Handle, "object"))
		{
			return;
		}
		if (Handle->CloneCallback && Handle->EqualsCallback && Handle->HashCallback)
		{
			return;
		}
		const std::string CloneName = MakeHostCloneNativeName(Handle->TypeName);
		const std::string EqualsName = MakeHostEqualsNativeName(Handle->TypeName);
		const std::string HashName = MakeHostHashNativeName(Handle->TypeName);
		const auto BindCallback = [&](const std::string& Name,
		                              FHostValueCallback& OutCallback, void*& OutUserData, std::shared_ptr<void>& OutOwner)
		{
			if (OutCallback)
			{
				return;
			}
			const std::uint16_t NativeIndex = FindActiveNativeIndexByName(Name);
			if (NativeIndex != static_cast<std::uint16_t>(-1) && (*ActiveNatives)[NativeIndex].Callback)
			{
				const FNativeFunction& Native = (*ActiveNatives)[NativeIndex];
				OutCallback = Native.Callback;
				OutUserData = Native.UserData;
				OutOwner = Native.CallbackOwner;
				return;
			}
			// Valid catalogs cannot contain duplicate linkage names. Preserve the old
			// forward-scan behavior for a manually supplied invalid table whose first
			// duplicate has no callback; this cold compatibility path stays out of the
			// normal host-value hot path.
			for (const FNativeFunction& Native : *ActiveNatives)
			{
				if (Native.Name == Name && Native.Callback)
				{
					OutCallback = Native.Callback;
					OutUserData = Native.UserData;
					OutOwner = Native.CallbackOwner;
					return;
				}
			}
		};
		BindCallback(CloneName, Handle->CloneCallback, Handle->CloneUserData, Handle->CloneOwner);
		BindCallback(EqualsName, Handle->EqualsCallback, Handle->EqualsUserData, Handle->EqualsOwner);
		BindCallback(HashName, Handle->HashCallback, Handle->HashUserData, Handle->HashOwner);
	}

	bool VeroScriptVm::PrepareHostKeyForInsert(
	    const FHostObjectHandle& Handle,
	    FHostObjectHandle& OutKey,
	    const FSourceLocation& Location)
	{
		if (!Handle)
		{
			ReportError("host key must not be null", Location);
			return false;
		}
		StampHostValueCallbacks(Handle);
		if (!Handle->CloneCallback)
		{
			OutKey = Handle;
			return true;
		}
		FValue Cloned;
		if (!CloneValueOrReport(FValue(Handle), Cloned, Location))
		{
			return false;
		}
		const auto* ClonedHandle = std::get_if<FHostObjectHandle>(&Cloned);
		if (!ClonedHandle || !*ClonedHandle)
		{
			ReportError("host key clone did not return an object", Location);
			return false;
		}
		OutKey = *ClonedHandle;
		StampHostValueCallbacks(OutKey);
		return true;
	}

	bool VeroScriptVm::CloneValueOrReport(
	    const FValue& Value,
	    FValue& Out,
	    const FSourceLocation& Location)
	{
		return CopyLanguageValue(Value, Out, Location);
	}

	bool VeroScriptVm::AssignClonedValue(
	    FValue& Dest,
	    const FValue& Source,
	    const FSourceLocation& Location)
	{
		FValue* const ResolvedDest = ResolveRefStorage(Dest);
		if (!ResolvedDest)
		{
			ReportError("invalid assignment destination", Location);
			return false;
		}
		FValue SourceBinding = Source;
		const auto* ResolvedSource = ResolveRefStorage(SourceBinding);
		const auto* TargetObject = std::get_if<FObjectHandle>(ResolvedDest);
		const auto* SourceObject = ResolvedSource ? std::get_if<FObjectHandle>(ResolvedSource) : nullptr;
		if (TargetObject && *TargetObject && SourceObject && *SourceObject && (*TargetObject)->Kind == EScriptObjectKind::Struct && (*TargetObject)->GetTypeName() == (*SourceObject)->GetTypeName() && ActiveModule)
		{
			const auto Target = *TargetObject, From = *SourceObject;
			const auto Name = MakeMethodFunctionName(Target->GetTypeName(), "@copy_assign", {"const ref " + Target->GetTypeName()});
			const auto It = ActiveModule->FunctionIndices.find(Name);
			if (It == ActiveModule->FunctionIndices.end())
			{
				ReportError("missing checked struct assignment service", Location);
				return false;
			}
			// SetIndex's source access was checked in its lexical compiler context;
			// owner/friend assignments may legitimately select a private operator.
			const auto Depth = Frames.size(), Stack = OperandStack.size();
			FValue Argument = From;
			if (!CallFunctionAt(It->second, &Argument, 1, Target, false) || !JitFinishNestedCall(Depth, Stack)) return false;
			FValue Return = Pop();
			const auto* Current = ResolveRefStorage(Dest);
			const auto* CurrentObject = Current ? std::get_if<FObjectHandle>(Current) : nullptr;
			if (!CurrentObject || *CurrentObject != Target)
			{
				ReportError("assignment destination changed during a callback", Location);
				(void)ReleaseValue(Return, Location);
				return false;
			}
			return ReleaseValue(Return, Location);
		}
		FValue Cloned;
		if (!CloneValueOrReport(Source, Cloned, Location))
		{
			return false;
		}
		FValue* const Current = ResolveRefStorage(Dest);
		if (!Current)
		{
			(void)ReleaseValue(Cloned, Location);
			return false;
		}
		return AssignValue(*Current, std::move(Cloned), Location);
	}

	bool VeroScriptVm::AppendClonedValue(
	    std::vector<FValue>& Dest,
	    const FValue& Source,
	    const FSourceLocation& Location)
	{
		FValue Cloned;
		if (!CloneValueOrReport(Source, Cloned, Location))
		{
			return false;
		}
		Dest.push_back(std::move(Cloned));
		return true;
	}

	bool VeroScriptVm::AppendClonedValue(
	    std::deque<FValue>& Dest,
	    const FValue& Source,
	    const FSourceLocation& Location)
	{
		FValue Cloned;
		if (!CloneValueOrReport(Source, Cloned, Location))
		{
			return false;
		}
		Dest.push_back(std::move(Cloned));
		return true;
	}

	FSourceLocation VeroScriptVm::RefineErrorLocation(const FSourceLocation& Location) const
	{
		// Hot path skips per-opcode DebugLines when no debugger is attached. On the
		// cold error path, restore the precise statement line from DebugLines.
		if (!ActiveModule)
		{
			return Location;
		}
		const FBytecodeFunction* RefineFunction = nullptr;
		std::size_t RefineIp = 0;
		if (bTeardownErrorActive && Frames.size() <= TeardownFrameDepth)
		{
			// Frame already popped; ReleaseValue failed without a deeper nested Run.
			// TeardownErrorFunction may be null (module InitCode → InitDebugLines).
			RefineFunction = TeardownErrorFunction;
			RefineIp = TeardownErrorIp;
		}
		else if (!Frames.empty())
		{
			RefineFunction = Frames.back().Function;
			RefineIp = ActiveInstructionStart;
		}
		else
		{
			return Location;
		}
		const std::vector<FDebugLine>& DebugLines = RefineFunction
		    ? RefineFunction->DebugLines
		    : ActiveModule->InitDebugLines;
		if (DebugLines.empty())
		{
			return Location;
		}
		RefineIp = std::min(RefineIp, DebugLines.size() - 1u);
		while (RefineIp != 0u && DebugLines[RefineIp].Line == 0u)
		{
			--RefineIp;
		}
		if (DebugLines[RefineIp].Line == 0u)
		{
			return Location;
		}
		FSourceLocation Refined = Location;
		const FDebugLine& DebugLine = DebugLines[RefineIp];
		Refined.Line = DebugLine.Line;
		Refined.Column = DebugLine.Column;
		if (DebugLine.FileIndex < ActiveModule->DebugFiles.size() && !ActiveModule->DebugFiles[DebugLine.FileIndex].empty())
		{
			Refined.FileLabel = ActiveModule->DebugFiles[DebugLine.FileIndex];
		}
		return Refined;
	}
}

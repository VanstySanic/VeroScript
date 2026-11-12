// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitLinear.h"
#include "VeroScriptAsmJitHost.h"
#include "VeroScriptJitRawScratch.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptJitHotLoops.h"
#include "VeroScriptJitLowering.h"
#include "VeroScriptJitTextPatterns.h"
#include "VeroScriptJitNative.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptAsmJitExecutionContext.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <queue>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VeroScript::AsmJit::JitLinear
{
	namespace
	{
		template <typename F>
		class TLinearScopeExit final
		{
		public:
			explicit TLinearScopeExit(F&& InFunction)
				: Function(std::move(InFunction))
			{
			}

			TLinearScopeExit(const TLinearScopeExit&) = delete;
			TLinearScopeExit& operator=(const TLinearScopeExit&) = delete;

			~TLinearScopeExit() noexcept
			{
				Function();
			}

		private:
			F Function;
		};

		template <typename F>
		auto MakeLinearScopeExit(F&& Function)
		{
			return TLinearScopeExit<std::decay_t<F>>(std::forward<F>(Function));
		}

		#include "Internal/VeroScriptJitLinearAnalysis.inl"
		#include "Internal/VeroScriptJitCaughtScalarInlining.inl"
		#include "Internal/VeroScriptJitLinearMetadata.inl"
		#include "Internal/VeroScriptJitTextEntryCost.inl"
		#include "Internal/VeroScriptJitLinearExecution.inl"

	}

	bool HasDirectI32FieldSummary(const FBytecodeModule& Module,
		const FBytecodeFunction& Function)
	{
		FJitFunctionEntry Entry;
		CollectResolvedThisFieldMetadata(Module, Function, Entry);
		return Entry.bDirectThisFieldAddModuloI32;
	}

	bool TryCompile(
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		JitNative::FArena& Arena,
		FJitFunctionEntry& OutEntry)
	{
		bool bHasThrowOpcode = false;
		bool bHasEndFinallyOpcode = false;
		bool bHasExceptionRelevantCallOpcode = false;
		const std::uint32_t FirstProtectedOffset = Function.ExceptionRegions.empty()
			? 0u
			: std::min_element(
				Function.ExceptionRegions.begin(), Function.ExceptionRegions.end(),
				[](const FBytecodeExceptionRegion& Left,
					const FBytecodeExceptionRegion& Right)
				{
					return Left.BeginOffset < Right.BeginOffset;
				})->BeginOffset;
		for (std::size_t Ip = 0; Ip < Function.Code.size();)
		{
			const EOpcode Opcode = static_cast<EOpcode>(Function.Code[Ip]);
			if (Opcode == EOpcode::Throw)
			{
				bHasThrowOpcode = true;
			}
			else if (Opcode == EOpcode::EndFinally)
			{
				bHasEndFinallyOpcode = true;
			}
			else if (Opcode == EOpcode::Call || Opcode == EOpcode::CallMethod
				|| Opcode == EOpcode::CallBuiltin || Opcode == EOpcode::CallNative
				|| Opcode == EOpcode::CallBoundMethod || Opcode == EOpcode::CallIndirect)
			{
				// Calls before the first protected range cannot transfer into one of
				// this function's handlers. Calls in/after a try construct may raise or
				// rethrow across it and therefore retain the authoritative VM unwinder.
				bHasExceptionRelevantCallOpcode =
					bHasExceptionRelevantCallOpcode || Ip >= FirstProtectedOffset;
			}
			const std::size_t Size = Bytecode::InstructionSize(Opcode);
			if (Size == 0 || Size > Function.Code.size() - Ip)
			{
				break;
			}
			Ip += Size;
		}
		const bool bHasFinallyRegion = std::any_of(
			Function.ExceptionRegions.begin(), Function.ExceptionRegions.end(),
			[](const FBytecodeExceptionRegion& Region)
			{
				return Region.FinallyOffset != std::numeric_limits<std::uint32_t>::max();
			});
		const bool bHasFusibleI32CatchLoop =
			IsFusibleI32CatchAccumulatorLoop(Module, Function);
		const bool bCanElideCatchMetadataAfterInlining =
			CanElideCatchOnlyMetadataAfterScalarInlining(Module, Function);
		if (!Function.ExceptionRegions.empty() && bHasExceptionRelevantCallOpcode && !bHasFinallyRegion &&
			!bHasEndFinallyOpcode && !bCanElideCatchMetadataAfterInlining)
		{
			FBytecodeFunction CaughtCallsInlined;
			std::uint16_t CatchInlineSites = 0;
			JitLowering::FPlan Plan;
			if (TryBuildCaughtI32Calls(Module, Function, CaughtCallsInlined, CatchInlineSites) &&
				JitLowering::BuildScalarPlan(Module, CaughtCallsInlined, Plan) &&
				JitNative::TryBindLinearI32Cfg(Arena, Module, CaughtCallsInlined, OutEntry, &Plan))
			{
				OutEntry.InlinedCallSites = CatchInlineSites;
				OutEntry.RuntimeDebugLines = std::move(CaughtCallsInlined.DebugLines);
				return true;
			}
		}
		// Machine-code unwinding never observes FValue exceptions. A catch-only
		// function with no nested calls can, however, transfer entirely inside the
		// ownership-aware Linear frame. Calls are excluded because a nested pending
		// exception must still unwind through the authoritative VM loop.
		if (bHasEndFinallyOpcode || bHasFinallyRegion
			|| (bHasThrowOpcode && Function.ExceptionRegions.empty())
			|| (!bCanElideCatchMetadataAfterInlining
				&& !Function.ExceptionRegions.empty()
				&& (bHasThrowOpcode || bHasExceptionRelevantCallOpcode)
				&& (!bHasFusibleI32CatchLoop || bHasExceptionRelevantCallOpcode)))
		{
			OutEntry = {};
			OutEntry.Entry = &JitTrampoline_Interpret;
			OutEntry.Status = EJitCompileStatus::Ok;
			OutEntry.Kind = EJitEntryKind::VmTrampoline;
			OutEntry.bPreferVmForExecution = true;
			OutEntry.VmPreferenceCategory = EJitFallbackReason::ExceptionHandling;
			OutEntry.VmPreferenceReason = "exception/finally control flow requires the Core unwinder";
			return true;
		}
		std::vector<EStackTag> Params;
		EStackTag ReturnTag = EStackTag::I32;
		bool bVoidReturn = false;
		if (!Function.ClosureTypeName.empty())
		{
			if (!ParseClosureType(Function.ClosureTypeName, Params, ReturnTag, bVoidReturn)
				|| Function.bReturnsVoid != bVoidReturn
				|| Params.size() != Function.ParamCount)
			{
				Params.assign(Function.ParamCount, EStackTag::Any);
				bVoidReturn = Function.bReturnsVoid;
				ReturnTag = EStackTag::Any;
			}
		}
		else if (Function.bIsMethod)
		{
			Params.assign(Function.ParamCount, EStackTag::Any);
			bVoidReturn = Function.bReturnsVoid;
			ReturnTag = EStackTag::Any;
		}
		else
		{
			return false;
		}
		if (Function.LocalCount < Function.ParamCount
			|| Function.Code.size() > 65536
			|| (Function.Code.empty() && !Function.bReturnsVoid))
		{
			return false;
		}
		FBytecodeFunction CatchElidedFunction;
		const FBytecodeFunction* OptimizationSource = &Function;
		if (bCanElideCatchMetadataAfterInlining)
		{
			CatchElidedFunction = Function;
			CatchElidedFunction.ExceptionRegions.clear();
			OptimizationSource = &CatchElidedFunction;
		}
		std::uint16_t LinearStackCapacityHint = 0;
		std::uint16_t LinearBoxedStackCapacityHint = 0;
		std::vector<std::size_t> LinearTextAddOffsets;
		std::vector<std::size_t> LinearUnsupportedTextFormatOffsets;
		// Rewrites can erase operand reads (for example an identity Call becomes
		// Nop). Verify the original caller before any rewrite can hide underflow.
		const bool bSourceCfgValid = ValidateCfg(
			Module, *OptimizationSource, Params, ReturnTag, bVoidReturn,
			&LinearStackCapacityHint, &LinearBoxedStackCapacityHint, &LinearTextAddOffsets,
			nullptr, &LinearUnsupportedTextFormatOffsets);
		FBytecodeFunction ClosureInlinedFunction;
		std::uint16_t ClosureInlineSites = 0;
		std::vector<std::size_t> ClosureTextAddOffsets;
		std::vector<std::size_t> ClosureUnsupportedTextFormatOffsets;
		if (bSourceCfgValid && TryInlineLocalScalarClosure(Module, *OptimizationSource, ClosureInlinedFunction, ClosureInlineSites)
			&& ValidateCfg(Module, ClosureInlinedFunction, Params, ReturnTag, bVoidReturn,
				&LinearStackCapacityHint, &LinearBoxedStackCapacityHint, &ClosureTextAddOffsets,
				nullptr, &ClosureUnsupportedTextFormatOffsets))
		{
			OptimizationSource = &ClosureInlinedFunction;
			LinearTextAddOffsets = std::move(ClosureTextAddOffsets);
			LinearUnsupportedTextFormatOffsets = std::move(ClosureUnsupportedTextFormatOffsets);
		}
		else
		{
			ClosureInlineSites = 0;
		}
		// Native CFG leaves assume plain scalars — skip ref-parameter functions.
		// Methods without this/field opcodes may still bind as native leaves.
		const bool bHasRefParams = BytecodeFunctionHasRefParams(Function);
		const bool bExceptionMetadataIsOnlyOwnedState =
			!Function.ExceptionRegions.empty()
			&& !bHasThrowOpcode
			&& (!bHasExceptionRelevantCallOpcode
				|| bCanElideCatchMetadataAfterInlining)
			&& !bHasFinallyRegion;
		const bool bHasI32ModuloAccumulatorLoop =
			HasFusibleI32ModuloAccumulatorLoop(Module, Function);
		const bool bPreferLinearU32AffineRecurrence =
			HasFusibleU32AffineRecurrenceLoop(Module, Function)
			|| bHasI32ModuloAccumulatorLoop;
		const bool bHasExplicitOwnedValues =
			JitLowering::RequiresVmOwnership(*OptimizationSource)
			&& !bExceptionMetadataIsOnlyOwnedState;
		const bool bHasDeclaredBoxedSignature =
			HasDeclaredSignatureTypeMetadata(Function.DeclMeta);
		// Object/handle types are represented as Any in the scalar signature. A
		// method can return `this` without placing a boxed operand in the IR stack,
		// so signature ownership must participate in the trampoline decision too.
		const bool bSignatureHasBoxedValues = bHasDeclaredBoxedSignature || std::any_of(
			Params.begin(), Params.end(), [](const EStackTag Tag)
			{
				return Tag == EStackTag::Any || Tag == EStackTag::String || Tag == EStackTag::WString;
			})
				|| ReturnTag == EStackTag::Any
				|| ReturnTag == EStackTag::String
				|| ReturnTag == EStackTag::WString;
		std::vector<std::uint32_t> ScalarNativeCallGuards;
		const bool bScalarNativeLinearCandidate =
			!bHasRefParams
			&& !bSignatureHasBoxedValues
			&& !bHasExplicitOwnedValues
			&& IsScalarNativeLinearCandidate(*OptimizationSource, ScalarNativeCallGuards);
		const bool bOwnershipAwareLinearCandidate =
			!bHasRefParams
			&& bHasExplicitOwnedValues
			&& (IsOwnershipAwareLinearCandidate(Module, Function)
				|| bHasFusibleI32CatchLoop);
		FBytecodeFunction DirectInlinedFunction;
		std::uint16_t DirectInlineSites = 0;
		const FBytecodeFunction* InlineSource = OptimizationSource;
		if (bSourceCfgValid
			&& !bHasRefParams && !bSignatureHasBoxedValues && !bHasExplicitOwnedValues
			&& TryRewriteDirectScalarLeafCalls(
				Module, *OptimizationSource, DirectInlinedFunction, DirectInlineSites))
		{
			InlineSource = &DirectInlinedFunction;
		}
		FBytecodeFunction InlinedFunction;
		std::uint16_t ExpandedInlineSites = 0;
		std::uint16_t InlineSites = static_cast<std::uint16_t>(DirectInlineSites + ClosureInlineSites);
		const FBytecodeFunction* NativeFunction = OptimizationSource;
		if (bSourceCfgValid
			&& !bHasRefParams && !bSignatureHasBoxedValues && !bHasExplicitOwnedValues
			&& TryBuildTransitivelyInlinedScalarFunction(
				Module,
				*InlineSource,
				Params,
				ReturnTag,
				bVoidReturn,
				4,
				1536,
				24,
				InlinedFunction,
				ExpandedInlineSites))
		{
			NativeFunction = &InlinedFunction;
			InlineSites = static_cast<std::uint16_t>(
				InlineSites + ExpandedInlineSites);
		}
		else if (InlineSource != OptimizationSource
			&& ValidateCfg(
				Module, *InlineSource, Params, ReturnTag, bVoidReturn))
		{
			NativeFunction = InlineSource;
		}
		else
		{
			InlineSites = ClosureInlineSites;
		}
		// Pure scalar callees are also safe to expand inside an ownership-aware
		// Linear caller.  Previously this rewrite was only consumed by NativeCfg,
		// leaving container/object loops to cross a VM/JIT call boundary on every
		// iteration.  Keep the boundary conservative: exception tables are not yet
		// remapped by the bytecode rewriter, and the fully rewritten CFG must verify
		// before it can become executable.
		FBytecodeFunction LinearInlinedFunction;
		std::uint16_t LinearInlineSites = 0;
		const FBytecodeFunction* LinearFunction = OptimizationSource;
		if (bSourceCfgValid && bOwnershipAwareLinearCandidate
			&& OptimizationSource->ExceptionRegions.empty()
			&& TryBuildTransitivelyInlinedScalarFunction(
				Module,
				*OptimizationSource,
				Params,
				ReturnTag,
				bVoidReturn,
				3,
				1024,
				16,
				LinearInlinedFunction,
				LinearInlineSites))
		{
			LinearFunction = &LinearInlinedFunction;
		}
		const bool bFunctionCfgValid = LinearFunction == OptimizationSource
			? bSourceCfgValid
			: ValidateCfg(
			Module,
			*LinearFunction,
			Params,
			ReturnTag,
			bVoidReturn,
			&LinearStackCapacityHint,
			&LinearBoxedStackCapacityHint,
			&LinearTextAddOffsets,
			nullptr, &LinearUnsupportedTextFormatOffsets);
		if (NativeFunction != &Function || bFunctionCfgValid)
		{
			if (!bHasRefParams && !bSignatureHasBoxedValues && !bHasExplicitOwnedValues
				&& !bPreferLinearU32AffineRecurrence)
			{
				// All native CFG candidates consume the same typed analysis. Build it
				// once here instead of repeating dataflow for I32, F64, and mixed probes.
				JitLowering::FPlan NativePlan;
				const JitLowering::FPlan* NativePlanPtr = nullptr;
				if (JitLowering::BuildScalarPlan(
						Module, *NativeFunction, NativePlan))
				{
					NativePlanPtr = &NativePlan;
				}
				if (JitNative::TryBindLinearI32Cfg(
						Arena, Module, *NativeFunction, OutEntry, NativePlanPtr))
				{
					OutEntry.InlinedCallSites = InlineSites;
					// Core can execute this bounded receiver expression transactionally
					// without installing a second frame or crossing the native bridge.
					OutEntry.bPreferVmForNestedExecution = PreferBoundedReceiverExpression(Function, Params, ReturnTag);
					if (NativeFunction != &Function)
					{
						OutEntry.RuntimeDebugLines = NativeFunction->DebugLines;
					}
					CollectResolvedThisFieldMetadata(Module, Function, OutEntry);
					return true;
				}
				if (JitNative::TryBindLinearF64Cfg(
						Arena, Module, *NativeFunction, OutEntry, NativePlanPtr))
				{
					OutEntry.InlinedCallSites = InlineSites;
					if (NativeFunction != &Function)
					{
						OutEntry.RuntimeDebugLines = NativeFunction->DebugLines;
					}
					CollectResolvedThisFieldMetadata(Module, Function, OutEntry);
					return true;
				}
				if (JitNative::TryBindLinearMixedCfg(
						Arena, Module, *NativeFunction, OutEntry, NativePlanPtr))
				{
					OutEntry.InlinedCallSites = InlineSites;
					if (NativeFunction != &Function)
					{
						OutEntry.RuntimeDebugLines = NativeFunction->DebugLines;
					}
					CollectResolvedThisFieldMetadata(Module, Function, OutEntry);
					return true;
				}
			}
			// The FValue linear trampoline owns its operand stack, call temporaries,
			// receivers, and frame locals. Unlike NativeCfg it deliberately preserves
			// boxed FValue identity, so object/interface/host-value signatures are safe
			// here once the complete bytecode CFG validates. The scalar IR plan is an
			// optimization aid for NativeCfg, not a correctness prerequisite for this
			// executor. Native value arrays, refs, struct clones, and closures still
			// require the interpreter until their alias/capture ownership is modeled.
			if (!bFunctionCfgValid
				|| bHasRefParams
				|| (bHasExplicitOwnedValues && !bOwnershipAwareLinearCandidate))
			{
				OutEntry = {};
				OutEntry.Entry = &JitTrampoline_Interpret;
				OutEntry.Status = EJitCompileStatus::Ok;
				OutEntry.Kind = EJitEntryKind::VmTrampoline;
				OutEntry.bPreferVmForExecution = true;
				OutEntry.VmPreferenceCategory = bHasRefParams ? EJitFallbackReason::ReferenceParameter
					: bHasExplicitOwnedValues ? EJitFallbackReason::Ownership : EJitFallbackReason::UnsupportedOpcode;
				OutEntry.VmPreferenceReason = bHasRefParams ? "reference parameters require the Core ownership frame"
					: bHasExplicitOwnedValues ? "value ownership is not covered by this Linear plan" : "Linear instruction flow is not verified";
				OutEntry.StackCapacityHint = LinearStackCapacityHint;
				OutEntry.BoxedStackCapacityHint = LinearBoxedStackCapacityHint;
				return true;
			}
			OutEntry = {};
			OutEntry.Entry = &JitTrampoline_LinearScalar;
			OutEntry.Status = EJitCompileStatus::Ok;
			OutEntry.Kind = EJitEntryKind::LinearTrampoline;
			OutEntry.StackCapacityHint = LinearStackCapacityHint;
			OutEntry.BoxedStackCapacityHint = LinearBoxedStackCapacityHint;
			OutEntry.bRequiresScalarNativeGuard = bScalarNativeLinearCandidate;
			OutEntry.ScalarNativeCallGuards = std::move(ScalarNativeCallGuards);
			const bool bNonEscapingScalarMap =
				IsNonEscapingScalarMapCandidate(Module, Function);
			const bool bNonEscapingScalarSet =
				IsNonEscapingScalarSetCandidate(Module, Function);
			const bool bNonEscapingScalarArray =
				IsNonEscapingScalarArrayCandidate(Module, Function);
			auto MutableTypedLoopPlans = BuildTypedLoopPlans(
				Module,
				*LinearFunction,
				Arena,
				bNonEscapingScalarArray,
				bNonEscapingScalarMap,
				bNonEscapingScalarSet,
				&LinearTextAddOffsets,
				&LinearUnsupportedTextFormatOffsets);
			OutEntry.TypedLoopPlans = MutableTypedLoopPlans;
			// The existing affine constant-delta int->double map kernel runs the
			// complete loop as one raw-storage operation and is materially cheaper
			// than the general typed bytecode executor. Do not let a newly admitted
			// F64 plan intercept that stronger lowering at the loop header.
			if (HasCanonicalTypedAffineMapValueUpdate(Module, *LinearFunction)
				|| HasCanonicalAffineF64ArrayRecurrence(Module, *LinearFunction)
				|| HasCanonicalBorrowedStringCompareLoopShape(*LinearFunction))
			{
				OutEntry.TypedLoopPlans.reset();
			}
			OutEntry.bUseScalarMapStorage =
				bNonEscapingScalarMap
				|| (OutEntry.TypedLoopPlans
					&& OutEntry.TypedLoopPlans->bRequiresScalarMapStorage);
			OutEntry.bUseScalarSetStorage =
				bNonEscapingScalarSet
				|| (OutEntry.TypedLoopPlans
					&& OutEntry.TypedLoopPlans->bRequiresScalarSetStorage);
			OutEntry.bUseScalarArrayStorage =
				bNonEscapingScalarArray
				|| (OutEntry.TypedLoopPlans
					&& OutEntry.TypedLoopPlans->bRequiresScalarArrayStorage);
			const bool bHasCompleteProfitableComplexLoopCoverage =
				OutEntry.TypedLoopPlans
					&& OutEntry.TypedLoopPlans->bHasProfitablePlan
					&& !OutEntry.TypedLoopPlans->bHasUnplannedComplexLoop
					&& !OutEntry.TypedLoopPlans->bHasUnprofitableComplexPlan
					&& !OutEntry.TypedLoopPlans->bNativeOnlyPlanUnavailable;
			FRetainedObjectArrayProjectionShape RetainedObjectArrayShape;
			const bool bHasRetainedObjectArrayProjection =
				TryDescribeRetainedObjectArrayProjectionShape(
					Module, Function, RetainedObjectArrayShape);
			if (MutableTypedLoopPlans && OutEntry.TypedLoopPlans)
			{
				MutableTypedLoopPlans->bRetainedObjectArrayProjectionAnalyzed = true;
				MutableTypedLoopPlans->bHasRetainedObjectArrayProjection =
					bHasRetainedObjectArrayProjection;
				if (bHasRetainedObjectArrayProjection)
				{
					MutableTypedLoopPlans->RetainedObjectArrayProjection =
						RetainedObjectArrayShape;
				}
			}
			// A numeric plan can be structurally valid yet always fail its runtime
			// binding for string arithmetic. Reuse the verified CFG's operand types;
			// string assignments and text initialized outside loops stay eligible.
			const auto Loops = SummarizeLinearLoops(*LinearFunction, LinearTextAddOffsets);
			const bool bHasNativeLoop = OutEntry.TypedLoopPlans && std::any_of(
				OutEntry.TypedLoopPlans->Plans.begin(), OutEntry.TypedLoopPlans->Plans.end(),
				[&](const auto& Plan)
				{
					return !Plan.bRuntimeBoundArrayPush && (Plan.NativeI32Code || Plan.NativeWideCode) && std::none_of(
						Plan.Instructions.begin(), Plan.Instructions.end(), [&](const auto& I)
						{
							return !I.bTextAdd && std::find(LinearTextAddOffsets.begin(), LinearTextAddOffsets.end(), I.BytecodeOffset)
								!= LinearTextAddOffsets.end();
						});
				});
			const bool bPreferVmForComplexOwnedLoop =
				!bHasRetainedObjectArrayProjection
				&& ((Loops.bHasTextArithmeticLoop && !bHasNativeLoop)
					|| (!bHasCompleteProfitableComplexLoopCoverage
						&& ShouldPreferVmForComplexOwnedLoop(Module, *LinearFunction)));
			const bool bUnplannedScalarLoop = Loops.bHasLoop
				&& (!OutEntry.TypedLoopPlans || !OutEntry.TypedLoopPlans->bHasProfitablePlan)
				&& !bPreferLinearU32AffineRecurrence
				&& !bScalarNativeLinearCandidate
				&& (bSignatureHasBoxedValues
					? LinearFunction->ExceptionRegions.empty() && Loops.bAllLoopsUseLocalScalarOpcodes
					: !JitLowering::RequiresVmOwnership(*LinearFunction));
			// Preserve direct container/descriptor kernels. Boxed wrapper methods
			// without a loop retain independent callee dispatch through ordinary VM.
			OutEntry.bPreferVmForExecution =
				(bSignatureHasBoxedValues && !bHasRefParams && IsBoxedServiceForwarder(*LinearFunction))
				|| (!bScalarNativeLinearCandidate
					&& !Loops.bHasLoop
					&& HasUnloweredMethodCallWithoutContainerWork(*LinearFunction))
				|| (OutEntry.TypedLoopPlans
					&& OutEntry.TypedLoopPlans->bNativeOnlyPlanUnavailable)
				|| bPreferVmForComplexOwnedLoop
				|| bUnplannedScalarLoop;
			if (OutEntry.bPreferVmForExecution)
			{
				OutEntry.VmPreferenceCategory = EJitFallbackReason::CostModel;
				OutEntry.VmPreferenceReason = IsBoxedServiceForwarder(*LinearFunction) ? "cost model: boxed service forwarder"
					: !Loops.bHasLoop ? "cost model: short method chain with an unlowered call"
					: bPreferVmForComplexOwnedLoop ? "cost model: owned/text loop lacks profitable native coverage"
					: "cost model: scalar loop lacks a profitable native plan";
			}
			DescribeTextEntryCost(Module, *LinearFunction, Params, OutEntry);
			#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
			OutEntry.bSuppressNestedJitOnVmFallback =
				OutEntry.bPreferVmForExecution
				&& OutEntry.TypedLoopPlans
				&& OutEntry.TypedLoopPlans->bNativeOnlyPlanUnavailable;
			#else
			// Once a portable build has selected the bytecode VM for a loop,
			// entering boxed scalar Linear callees from that hot loop compounds the
			// exact bridge cost the selector rejected. Known container callees retain
			// independent dispatch because their typed plans can still be profitable.
			OutEntry.bSuppressNestedJitOnVmFallback =
				OutEntry.bPreferVmForExecution
				&& CanSuppressNestedJitForScalarCallees(Module, *LinearFunction);
			#endif
			OutEntry.bUseI32ModuloAccumulatorLoop =
				bHasI32ModuloAccumulatorLoop
				|| HasFusibleI32ModuloAccumulatorLoop(Module, *LinearFunction);
			CollectLinearDispatchMetadata(Module, *LinearFunction, OutEntry);
			if (LinearFunction != &Function)
			{
				// LinearFunction can refer either to the inlined body or to the
				// catch-metadata-elided optimization source.  Persist the body that
				// was actually validated; moving LinearInlinedFunction unconditionally
				// stored an empty declaration for the latter case and made the
				// no-asmjit/fallback trampoline fail its signature check at runtime.
				OutEntry.LinearFunction = std::make_shared<FBytecodeFunction>(
					*LinearFunction);
				OutEntry.RuntimeDebugLines = OutEntry.LinearFunction->DebugLines;
				OutEntry.InlinedCallSites = static_cast<std::uint16_t>(LinearInlineSites + ClosureInlineSites);
			}
			return true;
		}
		// Broad coverage: typed bodies that fail linear CFG validation still run via
		// a VM-interpret trampoline so Mixed/Jit backends avoid Unsupported gaps.
		OutEntry = {};
		OutEntry.Entry = &JitTrampoline_Interpret;
		OutEntry.Status = EJitCompileStatus::Ok;
		OutEntry.Kind = EJitEntryKind::VmTrampoline;
		OutEntry.bPreferVmForExecution = true;
		OutEntry.VmPreferenceCategory = bHasRefParams ? EJitFallbackReason::ReferenceParameter : EJitFallbackReason::UnsupportedOpcode;
		OutEntry.VmPreferenceReason = bHasRefParams ? "reference parameters require the Core ownership frame; Linear flow is not verified"
			: "Linear instruction flow is not verified";
		return true;
	}
}

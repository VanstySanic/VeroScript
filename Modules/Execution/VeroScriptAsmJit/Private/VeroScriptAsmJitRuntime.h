// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptJit.h"
#include "VeroScriptRun.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace VeroScript::AsmJit
{
	class FExecutionContext;

	namespace JitNative
	{
		class FArena;
	}
	namespace JitLinear
	{
		struct FTypedLoopPlanSet;
	}

	/**
	 * Stable internal result codes returned by raw native JIT kernels.  They are
	 * deliberately distinct from diagnostics: the VM owns localization and
	 * source mapping after the kernel has returned.
	 */
	enum class EJitRuntimeStatus : std::int32_t
	{
		Ok = 0,
		IntegerOverflow = 1,
		DivisionByZero = 2,
		ShiftOutOfRange = 3,
		GenericFailure = 4,
		ToUnsignedOverflow = 5,
		ToIntOverflow = 6,
		Count,
	};

	/**
	 * Result of attempting to JIT-compile one bytecode function.
	 * Unsupported: codegen cannot handle this body yet (safe VM fallback).
	 * Failed: attempted compile failed (also falls back to VM).
	 * Ok: entry recorded and may be invoked by the VM.
	 */
	enum class EJitCompileStatus : std::uint8_t
	{
		Unsupported = 0,
		Failed,
		Ok,
	};

	/**
	 * How a cached JIT entry is executed. Used for diagnostics and profiling.
	 */
	enum class EJitEntryKind : std::uint8_t
	{
		Unsupported = 0,
		NativeLeaf,
		NativeCfg,
		BridgeTrampoline,
		LinearTrampoline,
		VmTrampoline,
	};

	/**
	 * Raw scalar ABI used by a NativeCfg kernel.
	 */
	enum class EJitScalarAbi : std::uint8_t
	{
		None = 0,
		I32,
		U32,
		F64,
		Mixed,
	};

	/**
	 * Script-visible scalar kind carried by a raw NativeCfg ABI slot.
	 */
	enum class EJitScalarValueKind : std::uint8_t
	{
		None = 0,
		I8,
		U8,
		I16,
		U16,
		I32,
		U32,
		I64,
		U64,
		F32,
		F64,
		Bool,
		Void,
	};

	/**
	 * Optional raw leaf stored alongside a NativeCfg entry for dynamically resolved
	 * scalar methods whose body does not observe `this` or call back into the VM.
	 * The NativeCfg entry remains authoritative for every other call path.
	 */
	enum class EJitDirectScalarKind : std::uint8_t
	{
		None,
		NullaryI32,
		UnaryI32,
		BinaryI32,
		NullaryU32,
		UnaryU32,
		UnaryU32RightImm,
		BinaryU32,
		NullaryF64,
		UnaryF64,
		BinaryF64,
		BinaryF64Bool,
		// Portable raw leaves for scalar kinds carried by the Mixed CFG ABI.
		// The operation is stored in DirectScalarOpcode; no executable-code
		// pointer is required, so these paths work on every supported target.
		UnaryI64,
		UnaryI64RightImm,
		BinaryI64,
		UnaryU64,
		UnaryU64RightImm,
		BinaryU64,
		UnaryF32,
		UnaryF32RightImm,
		BinaryF32,
	};

	inline constexpr std::uint32_t InvalidJitInstructionOffset = 0xFFFFFFFFu;

	/**
	 * Cold-path result returned by (or reconstructed for) a native JIT kernel.
	 *
	 * The raw machine-code ABI deliberately remains a 32-bit status value on
	 * every supported architecture.  A bridge can attach the bytecode offset
	 * when the kernel/helper has it; legacy scalar leaves leave it invalid and
	 * use the entry's conservative, statically-proven mapping instead.  This
	 * keeps the public failure contract structured without widening hot returns
	 * or making an x64-only calling convention observable to AArch64/x86.
	 */
	struct FJitKernelFailure
	{
		constexpr bool HasFailure() const
		{
			return Status != EJitRuntimeStatus::Ok;
		}

		EJitRuntimeStatus Status = EJitRuntimeStatus::Ok;
		std::uint32_t InstructionOffset = InvalidJitInstructionOffset;

	};

	// Raw kernels return one 32-bit word on every supported ABI.  The low byte
	// is the stable runtime status and the remaining 24 bits store
	// InstructionOffset + 1 (zero means "not supplied").  Existing scalar leaf
	// kernels that return only values 0..6 therefore remain ABI-compatible.
	inline constexpr std::uint32_t JitRuntimeStatusMask = 0xFFu;
	inline constexpr std::uint32_t JitFailureOffsetShift = 8u;
	inline constexpr std::uint32_t MaxEncodedJitInstructionOffset = 0x00FFFFFEu;

	constexpr std::int32_t EncodeJitKernelFailure(
		const EJitRuntimeStatus Status,
		const std::uint32_t InstructionOffset = InvalidJitInstructionOffset)
	{
		std::uint32_t Encoded = static_cast<std::uint32_t>(Status) & JitRuntimeStatusMask;
		if (Status != EJitRuntimeStatus::Ok
			&& InstructionOffset != InvalidJitInstructionOffset
			&& InstructionOffset <= MaxEncodedJitInstructionOffset)
		{
			Encoded |= (InstructionOffset + 1u) << JitFailureOffsetShift;
		}
		return static_cast<std::int32_t>(Encoded);
	}

	constexpr FJitKernelFailure DecodeJitKernelFailure(const std::int32_t EncodedFailure)
	{
		const std::uint32_t Encoded = static_cast<std::uint32_t>(EncodedFailure);
		const auto Status = static_cast<EJitRuntimeStatus>(Encoded & JitRuntimeStatusMask);
		const std::uint32_t StoredOffset = Encoded >> JitFailureOffsetShift;
		return FJitKernelFailure{
			Status,
			StoredOffset == 0u ? InvalidJitInstructionOffset : StoredOffset - 1u};
	}

	/**
	 * Native entry for a JIT-compiled function.
	 * VmContext is the backend-owned FExecutionContext*. While the entry runs, GetPendingJitArguments/Count/This,
	 * GetPendingJitFunctionIndex, GetPendingJitPayloadI32/F64, and GetPendingJitNativeCode
	 * describe the call.
	 * Non-void: push the return with FExecutionContext::JitPushReturn.
	 * Void: leave the operand stack unchanged; the VM pushes the void sentinel after
	 * a successful entry so Call+Pop sites stay balanced with the interpreter.
	 * Return false if an error was reported on the VM.
	 * Path: asmjit native leaves (when enabled) -> C++ trampolines -> linear FValue
	 * interpreter -> VM-interpret trampoline for richer typed/method bodies.
	 * Everyday JIT-friendly shapes: scalar int/uint/bool(+print) loops and method
	 * bodies with this-field/this-calls -> NativeCfg. Keep hot-loop arithmetic in locals
	 * (not per-iteration field load/store). Host subclass methods that only use script
	 * locals stay NativeCfg; CallNative (host methods/properties) with scalar signatures
	 * uses LinearTrampoline. Validated object/interface/host-value signatures also use
	 * the FValue-preserving LinearTrampoline, including dynamic calls and field access.
	 * Functions that create shared-handle objects/containers use its ownership-aware
	 * path. Untyped hand-built bytecode without ClosureTypeName stays Unsupported.
	 */
	// FJitFunctionEntry stores this function-pointer alias and needs its definition.
	using FJitNativeEntry = bool (*)(void* VmContext);

	struct FJitFunctionEntry
	{

		FJitNativeEntry Entry = nullptr;
		EJitCompileStatus Status = EJitCompileStatus::Unsupported;
		EJitEntryKind Kind = EJitEntryKind::Unsupported;

		// Optional generation-owned bytecode used by the ownership-aware Linear
		// executor.  It contains only verifier-approved expansion of pure scalar
		// callees; the module function remains the declaration/identity authority.
		// Keeping the rewritten function on the cache entry makes its lifetime match
		// hot reload invalidation and avoids mutating package bytecode in place.
		std::shared_ptr<const FBytecodeFunction> LinearFunction;

		// Generation-scoped, architecture-neutral typed natural-loop plan.  The
		// public entry keeps only an opaque shared owner; its representation and
		// executor remain private to JitLinear.  This lets arbitrary scalar index/key
		// expressions use raw local/container storage without making JIT IR part of
		// the installed runtime ABI.
		std::shared_ptr<const JitLinear::FTypedLoopPlanSet> TypedLoopPlans;

		// A verified free-function text query can use the canonical builtin
		// service without another argument snapshot or script frame.
		std::uint8_t ReadonlyTextBuiltin = 0xFFu;
		std::uint32_t ReadonlyTextBuiltinOffset = InvalidJitInstructionOffset;
		// A bounded, canonical pure-text loop may be cheaper in the VM when its
		// actual input supplies too little work to amortize native frame setup.
		std::uint16_t TextEntryVmTripThreshold = 0u; // Exclusive bound; zero disables the hint.
		std::uint16_t TextEntryLimitArgument = 0xFFFFu;
		std::int32_t TextEntryLimitImmediate = 0;

		// For each native kernel result code, this stores a bytecode instruction
		// only when static analysis proves that exactly one instruction in the
		// function can produce that result.  Ambiguous entries remain invalid: a
		// diagnostic must never be attached to a plausible-but-wrong source line.
		std::array<std::uint32_t, static_cast<std::size_t>(EJitRuntimeStatus::Count)>
			RuntimeErrorInstructionOffsets = []
			{
				std::array<std::uint32_t,
						static_cast<std::size_t>(EJitRuntimeStatus::Count)>
					Offsets{};
				Offsets.fill(InvalidJitInstructionOffset);
				return Offsets;
			}();

		// Native CFG compilation may rewrite or inline bytecode before emitting the
		// kernel.  Kernel failure offsets refer to that rewritten stream, not the
		// module's original function.  Retain its source map only when the streams
		// differ; ordinary entries keep this empty and use the module-owned map.
		std::vector<FDebugLine> RuntimeDebugLines;

		// Immediate baked at compile time (ConstI32 return / Add|Sub with const).
		std::int32_t PayloadI32 = 0;

		// Immediate for ConstF64 return leaves.
		double PayloadF64 = 0.0;

		// asmjit kernel pointer owned by FJitCodeCache::NativeArena (may be null).
		void* NativeCode = nullptr;
		void* DirectScalarCode = nullptr;
		std::uint32_t DirectScalarPayloadU32 = 0;
		std::uint64_t DirectScalarPayloadU64 = 0;
		EJitDirectScalarKind DirectScalarKind = EJitDirectScalarKind::None;

		// Nop means an identity leaf.
		EOpcode DirectScalarOpcode = EOpcode::Nop;

		// True when Entry is a native-leaf bridge backed by NativeCode.
		bool bNativeLeaf = false;

		// The compiled method reads/captures its receiver. Methods that only use
		// parameters/constants can borrow the caller's existing root without
		// publishing a shared PendingJitThis handle on every dynamic call.
		bool bRequiresThisObject = false;

		// Lets nested native calls select the matching raw ABI without reparsing
		// the function type string on every invocation.
		EJitScalarAbi ScalarAbi = EJitScalarAbi::None;

		// Compact signature metadata lets boxed VM/dynamic calls enter a raw
		// NativeCfg method without reparsing ClosureTypeName on every invocation.
		// Four bits are used per parameter; signatures above 16 parameters fall
		// back to the ordinary boxed bridge.
		std::uint64_t ScalarParameterKinds = 0;
		std::uint8_t ScalarParameterCount = 0;
		EJitScalarValueKind ScalarReturnKind = EJitScalarValueKind::None;

		// Compile-time stack capacity hint for linear/object fallback execution.
		std::uint16_t StackCapacityHint = 0;
		// Native inlining may require more locals than the source declaration.
		std::uint16_t NativeLocalCount = 0;

		// Subset of StackCapacityHint that may hold boxed/object values.
		std::uint16_t BoxedStackCapacityHint = 0;

		/**
		 * This linear entry was admitted using the scalar-CallNative exception.
		 * Its runtime native table must be checked before entering the linear
		 * frame; incompatible/manual tables fall back to the interpreter.
		 */
		bool bRequiresScalarNativeGuard = false;

		// Compile-time proof for raw scalar storage in a frame-local map. Cached on
		// the generation-scoped JIT entry so Linear calls never rescan bytecode.
		bool bUseScalarMapStorage = false;

		// Same proof boundary as scalar maps, specialized for integral set values.
		bool bUseScalarSetStorage = false;

		// Non-escaping primitive array whose elements can remain in a typed vector.
		bool bUseScalarArrayStorage = false;

		// Some ownership-aware Linear bodies are semantically supported but are
		// predictably slower than the bytecode VM when every loop iteration crosses
		// several boxed/container helper boundaries. Execution backends bypass such
		// entries until a proven fused specialization is available.
		bool bPreferVmForExecution = false;
		bool bPreferVmForNestedExecution = false;
		EJitFallbackReason VmPreferenceCategory = EJitFallbackReason::None;
		const char* VmPreferenceReason = nullptr;

		// When a portable build selects the VM for a complex owned loop, Linear
		// dispatch of callees inside that loop is also known to be negatively
		// profitable. The interpreter frame propagates this decision to descendants
		// without changing their standalone dispatch policy.
		bool bSuppressNestedJitOnVmFallback = false;

		// Generation-scoped proof for the canonical checked i32 affine/modulo
		// recurrence. The Linear executor consumes this without rescanning bytecode.
		bool bUseI32ModuloAccumulatorLoop = false;

		// Method-owner shape and StringPool-name -> field-slot table. Native this
		// field helpers use it only for an exact runtime shape match; inherited or
		// reloaded shapes retain the generation-qualified field PIC fallback.
		std::uint16_t ResolvedThisFieldOwnerTypeIndex = 0xFFFF;

		// Sorted packed entries: high 16 bits = StringPool name index, low 16
		// bits = owner field index.
		std::vector<std::uint32_t> ResolvedThisFields;

		// Direct receiver-field method summary for a fully checked field addition.
		// The delta starts with intParam0 for a unary method, optionally applies
		// DirectThisArgumentModuloI32 and then adds DirectThisFieldAddendI32. A
		// nullary wrapper uses DirectThisFieldAddendI32 directly;
		// DirectThisFieldModuloI32 == 0 means no final remainder operation.
		// Dynamic/virtual dispatch still
		// resolves the actual method and interceptor first; only the selected body is
		// executed without constructing a nested native frame.
		bool bDirectThisFieldAddModuloI32 = false;
		std::uint16_t DirectThisFieldIndex = 0xFFFF;
		std::int32_t DirectThisFieldAddendI32 = 0;

		// Optional `% constant` applied to intParam0 before the field addition.
		// Zero means the parameter is consumed directly.
		std::int32_t DirectThisArgumentModuloI32 = 0;
		std::int32_t DirectThisFieldModuloI32 = 0;

		// Side-effect-free `return intParam0 + constant` summary, optionally followed
		// by `% constant`. A zero modulus means that no modulo operation is present.
		// It is consumed only after ordinary virtual resolution has selected this
		// exact generation-scoped entry.
		bool bDirectI32AddModulo = false;
		std::int32_t DirectI32Addend = 0;
		std::int32_t DirectI32Modulo = 0;

		// Immutable Linear bytecode-shape metadata. It is collected once when the
		// generation-scoped entry is compiled instead of rescanning the function on
		// every invocation.
		bool bMayHaveDirectScalarMethodStore = false;
		bool bMayHaveScalarMethodAccumulator = false;
		bool bMayHaveBoundScalarMethodCall = false;

		// True when this function directly writes a script-object field. A registered
		// mutation observer can re-enter and replace field storage, so dispatch uses
		// the authoritative VM frame unless the call site independently proves a
		// callback-safe operation.
		bool bMutatesScriptFields = false;

		// 0 = unrelated; otherwise EJitScalarSetOperation + 1 at CallMethod offsets.
		std::vector<std::uint8_t> ScalarSetCallKinds;

		// Non-zero at the first LoadLocal of a comparison whose two local operands
		// are immutable for the complete invocation. Linear execution may evaluate
		// these comparisons once per frame. The proof rejects ref-taking/rebinding
		// and any local with more than its single initialization store.
		std::vector<std::uint8_t> InvariantLocalCompareSites;

		// Encoded (native index << 8 | argument count) CallNative sites admitted by
		// the scalar Linear path. Runtime tables may differ from the compile-time
		// catalog, so dispatch validates these exact entries without rescanning the
		// whole bytecode function on every invocation.
		std::vector<std::uint32_t> ScalarNativeCallGuards;

		// Number of bounded scalar call sites expanded into NativeCfg. This includes
		// pure free calls and statically-bound calls on the current `this`.
		std::uint16_t InlinedCallSites = 0;

	};

	/**
	 * Per-VM JIT code cache. Invalidate on module replace / hot reload / debugger attach.
	 * Compiles leaf patterns to asmjit kernels and/or C++ trampolines; everything else
	 * is Unsupported and falls back to the bytecode interpreter.
	 */
	class FJitCodeCache
	{
	public:
		FJitCodeCache();
		~FJitCodeCache();
		FJitCodeCache(const FJitCodeCache&) = delete;
		FJitCodeCache& operator=(const FJitCodeCache&) = delete;

		void InvalidateAll();
		void SetPolicy(FJitPolicy Policy);
		const FJitPolicy& GetPolicy() const;

		// Record one Mixed-mode call and report whether compilation should begin.
		bool ShouldCompileOnCall(std::uint16_t FunctionIndex);

		/**
		 * Ensure FunctionIndex is compiled for Module (or record Unsupported).
		 * Idempotent while the cache is valid for this module generation.
		 */
		EJitCompileStatus EnsureCompiled(
			const FBytecodeModule& Module,
			std::uint16_t FunctionIndex);

		const FJitFunctionEntry* GetCandidate(std::uint16_t FunctionIndex) const noexcept;

		bool PublishCandidate(std::uint16_t FunctionIndex) noexcept;

		void DiscardCandidate(std::uint16_t FunctionIndex) noexcept;

		// True only when EnsureCompiled previously returned Ok for this index.
		bool HasEntry(std::uint16_t FunctionIndex) const;

		const FJitFunctionEntry* TryGet(std::uint16_t FunctionIndex) const;

		/**
		 * When Backend is Jit, walk all functions and attempt compile.
		 * Mixed leaves compilation to demand via EnsureCompiled on Call.
		 */
		void PrepareModule(
			const FBytecodeModule& Module,
			EExecutionBackend Backend,
			std::size_t PreferredFunctionIndex = static_cast<std::size_t>(-1));

		FJitStats GetStats() const;
		void AccumulatePublishedStatistics(FJitStats& Stats) const noexcept;

		// Retained capacities, including nested plans/copied bytecode/native pages.
		// Excludes allocator bookkeeping and transient compiler memory.
		std::size_t RetainedPayloadBytes() const;
		std::size_t RetainedTextScratchBytes() const noexcept;
		static std::size_t MinimumPayloadBytes(std::size_t FunctionCount);

		std::size_t UnsupportedCount() const;
		std::size_t CompiledCount() const;
		std::size_t CompiledNativeCount() const;

		JitNative::FArena& NativeArena();

	private:
		friend class FExecutionContext;

		// Keep cache containers, counters, and executable-page ownership out of
		// installed headers. This is an opaque ownership boundary for the source
		// SDK; the stable process ABI remains VeroScriptInterop C11.
		struct FImpl;

		void* const* GetDirectI32Targets() const;

		std::size_t GetDirectI32TargetCount() const;

		void* const* GetDirectF64Targets() const;

		std::size_t GetDirectF64TargetCount() const;

		std::unique_ptr<FImpl> Impl;

	};
}

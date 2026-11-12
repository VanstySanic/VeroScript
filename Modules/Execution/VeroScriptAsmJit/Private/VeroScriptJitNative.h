// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptJitAbi.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VeroScript::AsmJit::JitLowering
{
	struct FPlan;
}

namespace VeroScript::AsmJit::JitLinear
{
	struct FTypedLoopPlan;
	struct FTypedLoopNativeContext;
	struct FTypedLoopInstruction;
}

namespace VeroScript::AsmJit::JitNative
{
	struct FNativeRecursionState
	{
		std::size_t* ActiveDepth = nullptr;
		std::size_t NativeDepthLimit = 0;
		std::size_t LanguageDepthLimit = 0;
		std::size_t DirectCalls = 0;
		// Dense module-generation-owned target table. Entries are published only
		// for full I32/U32 NativeCfg kernels using FLinearI32Kernel; leaf kernels and
		// other ABIs remain null. The array is allocated before execution and never
		// resized until the cache generation is invalidated.
		void* const* DirectI32Targets = nullptr;
		std::size_t DirectI32TargetCount = 0;
		// Homogeneous-double kernels use the same raw five-argument CFG ABI on
		// x64 and AArch64, but require a separate table so an I32 caller can never
		// enter an F64 target through an incorrectly typed function pointer.
		void* const* DirectF64Targets = nullptr;
		std::size_t DirectF64TargetCount = 0;
		// An outer direct-call site sets this when a nested kernel returns a structured
		// failure. The VM uses the callee's generation-owned debug map instead of
		// incorrectly attributing the failure to the caller's bytecode offset.
		std::uint16_t FailureFunctionIndex = 0xFFFFu;
	};

	// Kernel status: 0 ok, 1 integer overflow, 2 division by zero, 3 shift out of range,
	// 5 to_unsigned overflow, 6 to_int overflow (4 reserved for linear non-void fallthrough).
	using FBinaryI32Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			std::int32_t Left,
			std::int32_t Right,
			std::int32_t* OutResult);

	// Unary kernel (Neg/BitNot or value±imm): status + out result.
	using FUnaryI32Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			std::int32_t Value,
			std::int32_t* OutResult);

	using FUnaryI32ImmKernel = FUnaryI32Kernel;

	// The generated unsigned kernels use the same platform register ABI as their
	// signed counterparts, but these exact types avoid signed conversions and
	// preserve the full uint32 domain at direct dynamic call sites.
	using FBinaryU32Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			std::uint32_t Left,
			std::uint32_t Right,
			std::uint32_t* OutResult);

	using FUnaryU32Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			std::uint32_t Value,
			std::uint32_t* OutResult);

	using FConstU32Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			std::uint32_t* OutResult);

	// Nullary ConstI32: writes Imm to Out, returns status.
	using FConstI32Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(std::int32_t* OutResult);

	// double binary: MS x64 xmm0/xmm1 + r8 Out*; AAPCS64 d0/d1 + x0 Out*.
	using FBinaryF64Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			double Left,
			double Right,
			double* OutResult);

	using FUnaryF64Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			double Value,
			double* OutResult);

	using FConstF64Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(double* OutResult);

	// CmpF64 writes 0/1 to Out (int).
	using FCmpF64Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			double Left,
			double Right,
			std::int32_t* OutResult);

	// Native linear CFG: Locals/OpStack are int32 buffers; Out may be null for void.
	using FLinearI32Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			void* VmContext,
			std::int32_t* Locals,
			std::int32_t* OpStack,
			std::int32_t* OutResult,
			FNativeRecursionState* RecursionState);

	// Native double CFG: Locals/OpStack are double buffers; Out may be null for void.
	using FLinearF64Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			void* VmContext,
			double* Locals,
			double* OpStack,
			double* OutResult,
			FNativeRecursionState* RecursionState);

	// Mixed scalar CFG: Locals/OpStack are 8-byte payloads; Out may be null for void.
	using FLinearMixedKernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
			void* VmContext,
			std::uint64_t* Locals,
			std::uint64_t* OpStack,
			std::uint64_t* OutResult,
			FNativeRecursionState* RecursionState);

	using FTypedLoopI32Kernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
		JitLinear::FTypedLoopNativeContext* Context);
	using FPureI32ExpressionKernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
		std::int32_t Argument0,
		std::int32_t Argument1,
		std::int32_t* OutResult,
		std::uint32_t* OutFailureOffset);
	using FFieldI32ExpressionKernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
		std::int32_t Argument,
		void* Fields,
		std::int32_t* OutResult,
		std::uint32_t* OutFailureOffset);
	using FFieldI32ArgumentsExpressionKernel = std::int32_t (VEROSCRIPT_JIT_CALL *)(
		const std::uint64_t* Arguments, void* Fields,
		std::int32_t* OutResult, std::uint32_t* OutFailureOffset);

	/**
	 * Owns asmjit JitRuntime executable pages for one FJitCodeCache.
	 * Reset() frees all previously emitted kernels (InvalidateAll).
	 */
	class FArena
	{
	public:
		FArena();
		~FArena();
		FArena(const FArena&) = delete;
		FArena& operator=(const FArena&) = delete;

		void Reset();
		std::size_t ReservedBytes() const;
		bool IsAvailable() const;

		FBinaryI32Kernel EmitAddI32();
		FBinaryI32Kernel EmitSubI32();
		FBinaryI32Kernel EmitMulI32();
		FBinaryI32Kernel EmitDivI32();
		FBinaryI32Kernel EmitModI32();
		FBinaryI32Kernel EmitBitAndI32();
		FBinaryI32Kernel EmitBitOrI32();
		FBinaryI32Kernel EmitBitXorI32();
		FBinaryI32Kernel EmitShlI32();
		FBinaryI32Kernel EmitShrI32();
		FBinaryI32Kernel EmitCmpEqI32();
		FBinaryI32Kernel EmitCmpNeI32();
		FBinaryI32Kernel EmitCmpLtI32();
		FBinaryI32Kernel EmitCmpLeI32();
		FBinaryI32Kernel EmitCmpGtI32();
		FBinaryI32Kernel EmitCmpGeI32();

		FBinaryI32Kernel EmitAddU32();
		FBinaryI32Kernel EmitSubU32();
		FBinaryI32Kernel EmitMulU32();
		FBinaryI32Kernel EmitDivU32();
		FBinaryI32Kernel EmitModU32();
		FBinaryI32Kernel EmitCmpEqU32();
		FBinaryI32Kernel EmitCmpNeU32();
		FBinaryI32Kernel EmitCmpLtU32();
		FBinaryI32Kernel EmitCmpLeU32();
		FBinaryI32Kernel EmitCmpGtU32();
		FBinaryI32Kernel EmitCmpGeU32();

		FUnaryI32Kernel EmitNegI32();
		FUnaryI32Kernel EmitBitNotI32();

		FUnaryI32ImmKernel EmitAddImmI32(std::int32_t Imm);
		FUnaryI32ImmKernel EmitSubImmI32(std::int32_t Imm);
		FUnaryI32ImmKernel EmitMulImmI32(std::int32_t Imm);
		FUnaryI32ImmKernel EmitDivImmI32(std::int32_t Imm);
		FUnaryI32ImmKernel EmitModImmI32(std::int32_t Imm);
		FUnaryI32ImmKernel EmitBitAndImmI32(std::int32_t Imm);
		FUnaryI32ImmKernel EmitBitOrImmI32(std::int32_t Imm);
		FUnaryI32ImmKernel EmitBitXorImmI32(std::int32_t Imm);
		// Imm must be in [0, 31]; otherwise returns nullptr.
		FUnaryI32ImmKernel EmitShlImmI32(std::int32_t Imm);
		FUnaryI32ImmKernel EmitShrImmI32(std::int32_t Imm);
		// Imm - value (Const; Local0; Sub).
		FUnaryI32ImmKernel EmitSubFromImmI32(std::int32_t Imm);
		// Imm / value and Imm % value.
		FUnaryI32ImmKernel EmitDivFromImmI32(std::int32_t Imm);
		FUnaryI32ImmKernel EmitModFromImmI32(std::int32_t Imm);

		FConstI32Kernel EmitConstI32(std::int32_t Imm);

		// uint32 wrapping arithmetic (div/mod: only zero-divisor fails).

		FUnaryI32Kernel EmitNegU32();
		FUnaryI32Kernel EmitBitNotU32();
		FUnaryI32Kernel EmitNotBool();
		FUnaryI32Kernel EmitIdentityI32();
		FUnaryF64Kernel EmitIdentityF64();

		// double leaves (MS x64 XMM ABI). Status: 0 ok, 2 div0.
		FBinaryF64Kernel EmitAddF64();
		FBinaryF64Kernel EmitSubF64();
		FBinaryF64Kernel EmitMulF64();
		FBinaryF64Kernel EmitDivF64();
		FCmpF64Kernel EmitCmpEqF64();
		FCmpF64Kernel EmitCmpNeF64();
		FCmpF64Kernel EmitCmpLtF64();
		FCmpF64Kernel EmitCmpLeF64();
		FCmpF64Kernel EmitCmpGtF64();
		FCmpF64Kernel EmitCmpGeF64();
		FUnaryF64Kernel EmitNegF64();
		FConstF64Kernel EmitConstF64(double Imm);

		/**
		 * Emit a full I32/bool or homogeneous uint scalar CFG body.
		 * Returns FLinearI32Kernel or nullptr.
		 */
		FLinearI32Kernel EmitLinearI32Cfg(
			const FBytecodeModule& Module,
			const FBytecodeFunction& Function,
			const JitLowering::FPlan* Plan = nullptr);

		// Homogeneous double scalar CFG (math builtins + calls/globals).
		FLinearF64Kernel EmitLinearF64Cfg(
			const FBytecodeModule& Module,
			const FBytecodeFunction& Function,
			const JitLowering::FPlan* Plan = nullptr);

		// Mixed int/uint/double/bool scalar CFG.
		FLinearMixedKernel EmitLinearMixedCfg(
			const FBytecodeModule& Module,
			const FBytecodeFunction& Function,
			const JitLowering::FPlan* Plan = nullptr);

		// Emit an ownership-free typed natural-loop CFG. Unsupported plans or
		// architectures return nullptr and retain the portable typed executor.
		FTypedLoopI32Kernel EmitTypedLoopI32(
			const JitLinear::FTypedLoopPlan& Plan);
		FTypedLoopI32Kernel EmitTypedLoopWide(const JitLinear::FTypedLoopPlan& Plan);
		// Lower a verified, side-effect-free two-argument i32 expression to a tiny
		// raw kernel. Unsupported architectures retain the portable RPN evaluator.
		FPureI32ExpressionKernel EmitPureI32Expression(
			const std::vector<JitLinear::FTypedLoopInstruction>& Instructions);
		FFieldI32ExpressionKernel EmitFieldI32Expression(
			const std::vector<JitLinear::FTypedLoopInstruction>& Instructions,
			std::uint8_t ArgumentCount = 1);

	private:
		struct FState;
		FState* State = nullptr;
	};

	/**
	 * If Arena can emit a native kernel for this matched leaf opcode, fill OutEntry
	 * (Entry=bridge, NativeCode=kernel, bNativeLeaf=true). Returns false to fall back.
	 */
	bool TryBindBinaryI32(
		FArena& Arena,
		EOpcode Op,
		FJitFunctionEntry& OutEntry);

	bool TryBindCmpI32(
		FArena& Arena,
		EOpcode Op,
		FJitFunctionEntry& OutEntry);

	bool TryBindNegI32(FArena& Arena, FJitFunctionEntry& OutEntry);
	bool TryBindBitNotI32(FArena& Arena, FJitFunctionEntry& OutEntry);
	bool TryBindConstI32(FArena& Arena, std::int32_t Imm, FJitFunctionEntry& OutEntry);

	bool TryBindLocal0AddImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindLocal0SubImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindLocal0MulImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindLocal0DivImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindLocal0ModImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindLocal0BitAndImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindLocal0BitOrImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindLocal0BitXorImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindLocal0ShlImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindLocal0ShrImm(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindImmSubLocal0(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindImmDivLocal0(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindImmModLocal0(
		FArena& Arena,
		std::int32_t Imm,
		FJitFunctionEntry& OutEntry);

	bool TryBindBinaryU32(
		FArena& Arena,
		EOpcode Op,
		FJitFunctionEntry& OutEntry);

	bool TryBindCmpU32(
		FArena& Arena,
		EOpcode Op,
		FJitFunctionEntry& OutEntry);

	bool TryBindNegU32(FArena& Arena, FJitFunctionEntry& OutEntry);
	bool TryBindBitNotU32(FArena& Arena, FJitFunctionEntry& OutEntry);
	bool TryBindNotBool(FArena& Arena, FJitFunctionEntry& OutEntry);
	bool TryBindConstBool(FArena& Arena, bool Value, FJitFunctionEntry& OutEntry);
	bool TryBindIdentity(
		FArena& Arena,
		const FBytecodeFunction& Function,
		FJitFunctionEntry& OutEntry);

	bool TryBindBinaryF64(
		FArena& Arena,
		EOpcode Op,
		FJitFunctionEntry& OutEntry);

	bool TryBindCmpF64(
		FArena& Arena,
		EOpcode Op,
		FJitFunctionEntry& OutEntry);

	bool TryBindNegF64(FArena& Arena, FJitFunctionEntry& OutEntry);
	bool TryBindConstF64(FArena& Arena, double Imm, FJitFunctionEntry& OutEntry);

	/**
	 * Prefer asmjit CFG kernel for I32/bool/uint scalar control-flow functions.
	 * On failure returns false so caller can use the C++ linear trampoline.
	 */
	bool TryBindLinearI32Cfg(
		FArena& Arena,
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		FJitFunctionEntry& OutEntry,
		const JitLowering::FPlan* Plan = nullptr);

	bool TryBindLinearF64Cfg(
		FArena& Arena,
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		FJitFunctionEntry& OutEntry,
		const JitLowering::FPlan* Plan = nullptr);

	bool TryBindLinearMixedCfg(
		FArena& Arena,
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		FJitFunctionEntry& OutEntry,
		const JitLowering::FPlan* Plan = nullptr);

	// C++ bridges used as FJitNativeEntry (read PendingJitNativeCode).
	bool BridgeBinaryI32(void* VmContext);
	bool BridgeCmpI32(void* VmContext);
	bool BridgeUnaryI32(void* VmContext);
	bool BridgeUnaryImmI32(void* VmContext);
	bool BridgeConstI32(void* VmContext);
	bool BridgeConstBool(void* VmContext);
	bool BridgeIdentity(void* VmContext);
	bool BridgeBinaryU32(void* VmContext);
	bool BridgeCmpU32(void* VmContext);
	bool BridgeUnaryU32(void* VmContext);
	bool BridgeNotBool(void* VmContext);
	bool BridgeBinaryF64(void* VmContext);
	bool BridgeCmpF64(void* VmContext);
	bool BridgeUnaryF64(void* VmContext);
	bool BridgeConstF64(void* VmContext);
	bool BridgeLinearI32Cfg(void* VmContext);
	bool BridgeLinearF64Cfg(void* VmContext);
	bool BridgeLinearMixedCfg(void* VmContext);
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitIr.h"

#include <cstddef>
#include <string>
#include <vector>

namespace VeroScript::AsmJit::JitLowering
{
	/**
	 * Architecture-neutral lowering categories consumed by native backends.
	 */
	enum class EKind : std::uint8_t
	{
		Nop = 0,
		Constant,
		LocalLoad,
		LocalStore,
		GlobalLoad,
		GlobalStore,
		FieldLoad,
		FieldStore,
		Arithmetic,
		Compare,
		Unary,
		Branch,
		Call,
		Return,
		RuntimeFallback,
	};

	struct FInstruction
	{
		std::size_t Offset = 0;
		EOpcode Opcode = EOpcode::Nop;
		EKind Kind = EKind::RuntimeFallback;
		JitIr::EValueType ResultType = JitIr::EValueType::Unknown;
		JitIr::EValueType InputType = JitIr::EValueType::Unknown;
		std::uint16_t Operand0 = 0;
		std::uint16_t Operand1 = 0;
		// The operand expression must still execute and be popped, but the local
		// memory write itself is unobservable according to CFG liveness.
		bool bElideLocalStore = false;
	};

	struct FBlock
	{
		std::size_t StartOffset = 0;
		std::size_t EndOffset = 0;
		std::vector<FInstruction> Instructions;
		std::vector<std::size_t> Successors;
	};

	struct FValueSlot
	{
		enum class EStorage : std::uint8_t
		{
			Scalar32 = 0,
			Scalar64,
			Boxed,
		};

		std::uint16_t Index = 0;
		JitIr::EValueType Type = JitIr::EValueType::Unknown;
		EStorage Storage = EStorage::Boxed;
	};

	struct FPlan
	{
		JitIr::FSignature Signature;
		std::vector<FBlock> Blocks;
		std::vector<EOpcode> RuntimeFallbackOpcodes;
		std::vector<std::size_t> ElidedLocalStoreOffsets;
		std::vector<FValueSlot> LocalSlots;
		std::uint16_t LocalCount = 0;
		std::uint16_t MaxStackSlots = 0;
		std::uint16_t MaxBoxedStackSlots = 0;
		bool bHasBoxedValues = false;
		// Body contains refs/closures/constructors requiring the VM's FValue teardown.
		bool bRequiresVmOwnership = false;
		bool bScalarEligible = false;
		bool bHasRuntimeFallback = false;
	};

	// Build a typed, architecture-neutral plan for scalar native lowering.
	bool BuildScalarPlan(
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		FPlan& OutPlan,
		std::string* OutError = nullptr);

	// Conservative architecture-neutral ownership gate shared by every backend.
	bool RequiresVmOwnership(const FBytecodeFunction& Function);
}

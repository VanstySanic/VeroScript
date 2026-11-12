// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::AsmJit::JitIr
{
	/**
	 * Architecture-neutral value categories used by the JIT frontend.
	 */
	enum class EValueType : std::uint8_t
	{
		Unknown = 0,
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
		String,
		WString,
		Enum,
		Any,
		Void,
	};

	/**

	 * Parsed function signature used to seed later data-flow propagation.

	 */
	struct FSignature
	{
		std::vector<EValueType> Parameters;
		EValueType ReturnType = EValueType::Unknown;
		bool bValid = false;
	};

	/**

	 * One decoded bytecode instruction in the shared JIT frontend.

	 */
	struct FInstruction
	{
		std::size_t Offset = 0;
		EOpcode Opcode = EOpcode::Nop;
		std::size_t Size = 0;
	};

	/**

	 * Basic block with instruction offsets and CFG successor/predecessor indices.

	 */
	struct FBasicBlock
	{
		std::size_t StartOffset = 0;
		std::size_t EndOffset = 0;
		std::vector<FInstruction> Instructions;
		std::vector<std::size_t> Successors;
		std::vector<std::size_t> Predecessors;
	};

	/**

	 * Abstract operand-stack/local state at a basic-block entry.

	 */
	struct FTypeState
	{
		std::vector<EValueType> Stack;
		std::vector<EValueType> Locals;
	};

	/**

	 * Shared, architecture-neutral control-flow representation for one function.

	 */
	struct FFunction
	{
		FSignature Signature;
		std::vector<FBasicBlock> Blocks;
		std::vector<FTypeState> BlockEntryStates;
		// Type state immediately before each instruction, parallel to Blocks.
		std::vector<std::vector<FTypeState>> InstructionEntryStates;
		// Type state immediately after each instruction, parallel to Blocks.
		std::vector<std::vector<FTypeState>> InstructionExitStates;
		// Stable type of each local after joining every reachable program point.
		// Unknown is also the initial aggregate sentinel; conflicting concrete
		// assignments and reachable uninitialized CFG paths join to Any. Lowering
		// uses this instead of the entry state, where non-parameter locals have not
		// received their first value yet.
		std::vector<EValueType> LocalTypes;
		// Reachable bytecode usage facts. They form the conservative basis for later
		// alias/escape optimization: an address-taken local is never assumed unique,
		// while a completely unused compiler-reserved slot does not force boxing.
		std::vector<std::uint32_t> LocalReadCounts;
		std::vector<std::uint32_t> LocalWriteCounts;
		std::vector<bool> LocalAddressEscapes;
		// Parallel to Blocks. Unreachable blocks retain empty instruction states and
		// cannot contaminate type facts used by native lowering.
		std::vector<bool> ReachableBlocks;
		// Backward local liveness, parallel to Blocks and their instructions. These
		// facts are architecture-neutral and deliberately track only local slots;
		// operand-stack values remain explicit in the typed entry/exit states above.
		std::vector<std::vector<bool>> BlockLiveInLocals;
		std::vector<std::vector<bool>> BlockLiveOutLocals;
		// Parallel to block instructions. True only for a StoreLocal whose memory
		// write cannot reach any observable read.
		std::vector<std::vector<bool>> DeadLocalStores;
		bool bHasLoop = false;
		bool bTypeAnalysisPrecise = false;
		bool bHasUnsupportedOperations = false;
	};

	// Parse a canonical `fn(T,...)->R` bytecode signature.
	// Optional parameter views borrow TypeName only for the caller's query scope.
	bool ParseSignature(std::string_view TypeName, FSignature& OutSignature,
		std::vector<std::string_view>* OutParameterNames = nullptr);

	/**
	 * Decode one bytecode function into a validated CFG.
	 *
	 * This is intentionally structural for now: type propagation and lowering are
	 * separate follow-up stages. It rejects malformed instruction streams and jump
	 * targets that do not land on instruction boundaries.
	 */
	bool BuildFunction(
		const FBytecodeFunction& Function,
		FFunction& OutFunction,
		std::string* OutError = nullptr);

	// Propagate scalar stack/local types through the previously built CFG.
	bool AnalyzeTypes(
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		FFunction& InOutFunction,
		std::string* OutError = nullptr);
}

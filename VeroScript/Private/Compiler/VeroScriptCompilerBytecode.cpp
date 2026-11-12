// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptCompiler::ResolveAndPatchGotos()
	{
		FEmitter Emitter = MakeEmitter();
		for (const FGotoPatch& Patch : GotoPatches)
		{
			const auto LabelIt = LabelTable.find(Patch.Name);
			if (LabelIt == LabelTable.end() || LabelIt->second.Offset < 0)
			{
				ReportErrorId("diag.unresolved_label", Patch.Location, {Patch.Name});
				return false;
			}
			if (LabelIt->second.ScopeDepth != Patch.ScopeDepth || LabelIt->second.ExceptionScopePath != Patch.ExceptionScopePath)
			{
				ReportErrorId("diag.goto_scope_depth_mismatch", Patch.Location, {Patch.Name});
				return false;
			}
			if (Patch.GotoIp < LabelIt->second.Offset)
			{
				for (const FLocalInitMarker& Marker : LocalInitMarkers)
				{
					if (Marker.ScopeDepth == Patch.ScopeDepth && Marker.Offset > Patch.GotoIp && Marker.Offset <= LabelIt->second.Offset)
					{
						ReportErrorId(
						    "diag.goto_jumps_over_initialization",
						    Patch.Location,
						    {Patch.Name, Marker.Name});
						return false;
					}
				}
			}
			Emitter.PatchI32(
			    Patch.OperandOffset,
			    LabelIt->second.Offset - static_cast<std::int32_t>(Patch.OperandOffset + 4));
		}
		return true;
	}

	namespace
	{
		void RemapInstructionMetadata(std::vector<std::uint32_t>& Markers,
		    std::vector<FBytecodePackageInitSegment>* Segments,
		    const std::vector<std::uint32_t>& OldToNew)
		{
			for (auto& Offset : Markers)
			{
				Offset = OldToNew.at(Offset);
			}
			if (Segments)
			{
				for (auto& Segment : *Segments)
				{
					Segment.BeginOffset = OldToNew.at(Segment.BeginOffset);
					Segment.EndOffset = OldToNew.at(Segment.EndOffset);
				}
			}
		}

		bool TryGetI32ExactNumericConstant(
		    const FBytecodeConstant& Constant,
		    std::int32_t& OutValue)
		{
			switch (Constant.Kind)
			{
			case EConstantKind::I8:
				{
					OutValue = Constant.I8;
					return true;
				}
			case EConstantKind::U8:
				{
					OutValue = Constant.U8;
					return true;
				}
			case EConstantKind::I16:
				{
					OutValue = Constant.I16;
					return true;
				}
			case EConstantKind::U16:
				{
					OutValue = Constant.U16;
					return true;
				}
			case EConstantKind::I32:
				{
					OutValue = Constant.I32;
					return true;
				}
			case EConstantKind::U32:
				{
					if (Constant.U32 <= static_cast<std::uint32_t>(
					                        std::numeric_limits<std::int32_t>::max()))
					{
						OutValue = static_cast<std::int32_t>(Constant.U32);
						return true;
					}
					return false;
				}
			case EConstantKind::I64:
				{
					if (Constant.I64 >= std::numeric_limits<std::int32_t>::min() && Constant.I64 <= std::numeric_limits<std::int32_t>::max())
					{
						OutValue = static_cast<std::int32_t>(Constant.I64);
						return true;
					}
					return false;
				}
			case EConstantKind::U64:
				{
					if (Constant.U64 <= static_cast<std::uint64_t>(
					                        std::numeric_limits<std::int32_t>::max()))
					{
						OutValue = static_cast<std::int32_t>(Constant.U64);
						return true;
					}
					return false;
				}
			case EConstantKind::F32:
				{
					if (std::isfinite(Constant.F32))
					{
						const double Value = static_cast<double>(Constant.F32);
						if (Value >= static_cast<double>(std::numeric_limits<std::int32_t>::min()) && Value <= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
						{
							const std::int32_t Candidate = static_cast<std::int32_t>(Value);
							if (static_cast<float>(Candidate) == Constant.F32)
							{
								OutValue = Candidate;
								return true;
							}
						}
					}
					return false;
				}
			case EConstantKind::F64:
				{
					if (std::isfinite(Constant.F64) && Constant.F64 >= static_cast<double>(std::numeric_limits<std::int32_t>::min()) && Constant.F64 <= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
					{
						const std::int32_t Candidate = static_cast<std::int32_t>(Constant.F64);
						if (static_cast<double>(Candidate) == Constant.F64)
						{
							OutValue = Candidate;
							return true;
						}
					}
					return false;
				}
			default:
				return false;
			}
		}

		/**
		 * Fuse local-arithmetic peepholes in a linear bytecode buffer:
		 *   Pattern A: LoadLocal s; numeric constant idx; Add; StoreLocal s [; LoadLocal s; Pop]
		 *              -> IncLocal s      (when Constants[idx] is numeric one)
		 *              -> AddLocalImm s,imm (any exact int32 numeric constant)
		 *   Pattern A2: LoadLocal s; ConstI32 idx; Sub; StoreLocal s [; LoadLocal s; Pop]
		 *              -> AddLocalImm s,-imm when -imm is representable (CheckedAdd path)
		 *   Pattern B: LoadLocal a; LoadLocal b; Add; StoreLocal a [; LoadLocal a; Pop]
		 *              -> AddLocalLocal a,b
		 *   Pattern C: LoadLocal s; numeric constant idx; Lt; JmpIfFalse off
		 *              -> JmpIfLocalGeImm s, exact-i32(Constants[idx]), off
		 *              (jump iff !(local < imm); NaN-safe for floating-point locals)
		 *   Pattern E: LoadLocal s; StoreLocal tmp; LoadLocal tmp; numeric one;
		 *              Add|Sub; StoreLocal s; LoadLocal tmp; Pop
		 *              -> IncLocal s | AddLocalImm s,-1
		 *              (postfix result is discarded by an expression statement)
		 * Relative Jmp/JmpIfFalse/JmpIfLocalGeImm offsets and (optional) init ranges are
		 * remapped so fusion never shifts a jump target or a global-initializer span.
		 * Patterns whose interior instructions are jump targets are left untouched. The
		 * debug line of the pattern's first opcode is kept for the fused instruction.
		 */
		void PeepholeFuseLocalArithmetic(
		    const std::vector<FBytecodeConstant>& Constants,
		    std::vector<std::uint8_t>& Code,
		    std::vector<FDebugLine>& DebugLines,
		    std::vector<FBytecodeGlobalInitRange>* InitRanges,
		    const std::vector<std::string>* LocalNames,
		    std::vector<std::uint32_t>& PauseMarkers,
		    std::vector<FBytecodePackageInitSegment>* Segments = nullptr)
		{
			const std::size_t OldSize = Code.size();
			if (OldSize == 0)
			{
				return;
			}

			// Pass 1: validate the instruction stream and collect jump targets.
			std::vector<bool> IsJumpTarget(OldSize + 1, false);
			for (std::size_t Ip = 0; Ip < OldSize;)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > OldSize)
				{
					return; // Malformed stream: skip optimization entirely.
				}
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse || Op == EOpcode::JmpIfLocalGeImm || Op == EOpcode::BeginLocalStatic)
				{
					const std::size_t OffsetPos = Size - 4u;
					const std::int32_t Offset = Bytecode::ReadI32LE(Code.data() + Ip + OffsetPos);
					const std::int64_t Target =
					    static_cast<std::int64_t>(Ip) + static_cast<std::int64_t>(Size) + Offset;
					if (Target >= 0 && static_cast<std::size_t>(Target) <= OldSize)
					{
						IsJumpTarget[static_cast<std::size_t>(Target)] = true;
					}
				}
				Ip += Size;
			}

			if (Segments)
			{
				for (const auto& Segment : *Segments)
				{
					if (Segment.BeginOffset <= OldSize)
					{
						IsJumpTarget[Segment.BeginOffset] = true;
					}
					if (Segment.EndOffset <= OldSize)
					{
						IsJumpTarget[Segment.EndOffset] = true;
					}
				}
			}

			std::vector<std::uint8_t> NewCode;
			NewCode.reserve(OldSize);
			std::vector<FDebugLine> NewDebugLines;
			std::vector<std::uint32_t> OldToNew(OldSize + 1, 0);
			struct FJumpFixup
			{
				std::size_t NewOperandOffset;
				std::size_t OldTarget;
			};
			std::vector<FJumpFixup> JumpFixups;

			const auto DebugAt = [&](const std::size_t Ip) -> FDebugLine
			{
				return Ip < DebugLines.size() ? DebugLines[Ip] : FDebugLine{};
			};
			const auto SetNewDebug = [&](const std::size_t Start, const FDebugLine& Line)
			{
				if (Line.Line == 0)
				{
					return;
				}
				if (NewDebugLines.size() < Start + 1)
				{
					NewDebugLines.resize(Start + 1);
				}
				NewDebugLines[Start] = Line;
			};
			const auto ConsumeAssignResult =
			    [&](const std::size_t AfterStoreIp, const std::uint16_t Slot) -> std::size_t
			{
				// Statement form `s = ...;` emits StoreLocal; LoadLocal; Pop.
				if (AfterStoreIp + 4 <= OldSize && !IsJumpTarget[AfterStoreIp] && !IsJumpTarget[AfterStoreIp + 3] && static_cast<EOpcode>(Code[AfterStoreIp]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + AfterStoreIp + 1) == Slot && static_cast<EOpcode>(Code[AfterStoreIp + 3]) == EOpcode::Pop)
				{
					return 4;
				}
				return 0;
			};
			const auto IsPostfixTemporary = [&](const std::uint16_t Slot) -> bool
			{
				if (!LocalNames || Slot >= LocalNames->size())
				{
					return false;
				}
				const std::string& Name = (*LocalNames)[Slot];
				if (Name.size() <= 2 || Name[0] != '$' || Name[1] != 'a')
				{
					return false;
				}
				for (std::size_t Index = 2; Index < Name.size(); ++Index)
				{
					if (Name[Index] < '0' || Name[Index] > '9')
					{
						return false;
					}
				}
				return true;
			};

			for (std::size_t Ip = 0; Ip < OldSize;)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);

				bool bFused = false;

				// Pattern E: statement-form i32 postfix ++/--. The compiler preserves
				// the old value in a temporary for general postfix expressions; when a
				// following Pop proves that value is unused, remove the entire snapshot.
				if (Op == EOpcode::LoadLocal && Ip + 20 <= OldSize && !IsJumpTarget[Ip + 3] && !IsJumpTarget[Ip + 6] && !IsJumpTarget[Ip + 9] && !IsJumpTarget[Ip + 12] && !IsJumpTarget[Ip + 13] && !IsJumpTarget[Ip + 16] && !IsJumpTarget[Ip + 19] && static_cast<EOpcode>(Code[Ip + 3]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[Ip + 6]) == EOpcode::LoadLocal && (static_cast<EOpcode>(Code[Ip + 9]) == EOpcode::ConstI32 || static_cast<EOpcode>(Code[Ip + 9]) == EOpcode::ConstF64) && (static_cast<EOpcode>(Code[Ip + 12]) == EOpcode::Add || static_cast<EOpcode>(Code[Ip + 12]) == EOpcode::Sub) && static_cast<EOpcode>(Code[Ip + 13]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[Ip + 16]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 19]) == EOpcode::Pop)
				{
					const std::uint16_t TargetSlot =
					    Bytecode::ReadU16LE(Code.data() + Ip + 1);
					const std::uint16_t TempSlot =
					    Bytecode::ReadU16LE(Code.data() + Ip + 4);
					const std::uint16_t TempReloadSlot =
					    Bytecode::ReadU16LE(Code.data() + Ip + 7);
					const std::uint16_t ConstantIndex =
					    Bytecode::ReadU16LE(Code.data() + Ip + 10);
					const std::uint16_t StoreSlot =
					    Bytecode::ReadU16LE(Code.data() + Ip + 14);
					const std::uint16_t ResultSlot =
					    Bytecode::ReadU16LE(Code.data() + Ip + 17);
					if (TargetSlot != TempSlot && TargetSlot == StoreSlot && TempSlot == TempReloadSlot && TempSlot == ResultSlot && IsPostfixTemporary(TempSlot) && ConstantIndex < Constants.size() && [&]()
					{
						std::int32_t Value = 0;
						return TryGetI32ExactNumericConstant(
						           Constants[ConstantIndex], Value) &&
						    Value == 1;
					}())
					{
						const std::size_t Start = NewCode.size();
						const EOpcode Arithmetic =
						    static_cast<EOpcode>(Code[Ip + 12]);
						if (Arithmetic == EOpcode::Add)
						{
							NewCode.push_back(static_cast<std::uint8_t>(EOpcode::IncLocal));
							Bytecode::WriteU16LE(NewCode, TargetSlot);
						}
						else
						{
							NewCode.push_back(static_cast<std::uint8_t>(EOpcode::AddLocalImm));
							Bytecode::WriteU16LE(NewCode, TargetSlot);
							Bytecode::WriteI32LE(NewCode, -1);
						}
						SetNewDebug(Start, DebugAt(Ip));
						for (const std::size_t OldInstruction :
						    {Ip, Ip + 3, Ip + 6, Ip + 9, Ip + 12, Ip + 13,
						        Ip + 16, Ip + 19})
						{
							OldToNew[OldInstruction] = static_cast<std::uint32_t>(Start);
						}
						Ip += 20;
						bFused = true;
					}
				}

				// Pattern C: LoadLocal; numeric constant; Lt; JmpIfFalse (3+3+1+5 = 12)
				if (!bFused && Op == EOpcode::LoadLocal && Ip + 12 <= OldSize && !IsJumpTarget[Ip + 3] && !IsJumpTarget[Ip + 6] && !IsJumpTarget[Ip + 7] && (static_cast<EOpcode>(Code[Ip + 3]) == EOpcode::ConstI32 || static_cast<EOpcode>(Code[Ip + 3]) == EOpcode::ConstF64) && static_cast<EOpcode>(Code[Ip + 6]) == EOpcode::Lt && static_cast<EOpcode>(Code[Ip + 7]) == EOpcode::JmpIfFalse)
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					const std::uint16_t ConstIndex = Bytecode::ReadU16LE(Code.data() + Ip + 4);
					std::int32_t Imm = 0;
					if (ConstIndex < Constants.size() && TryGetI32ExactNumericConstant(Constants[ConstIndex], Imm))
					{
						const std::int32_t OldOffset = Bytecode::ReadI32LE(Code.data() + Ip + 8);
						const std::int64_t OldTarget =
						    static_cast<std::int64_t>(Ip) + 12 + static_cast<std::int64_t>(OldOffset);
						const std::size_t Start = NewCode.size();
						NewCode.push_back(static_cast<std::uint8_t>(EOpcode::JmpIfLocalGeImm));
						Bytecode::WriteU16LE(NewCode, Slot);
						Bytecode::WriteI32LE(NewCode, Imm);
						const std::size_t NewOperandOffset = NewCode.size();
						Bytecode::WriteI32LE(NewCode, 0); // patched below
						SetNewDebug(Start, DebugAt(Ip));
						OldToNew[Ip] = static_cast<std::uint32_t>(Start);
						OldToNew[Ip + 3] = static_cast<std::uint32_t>(Start);
						OldToNew[Ip + 6] = static_cast<std::uint32_t>(Start);
						OldToNew[Ip + 7] = static_cast<std::uint32_t>(Start);
						JumpFixups.push_back({NewOperandOffset, static_cast<std::size_t>(OldTarget)});
						Ip += 12;
						bFused = true;
					}
				}

				// Pattern A/B: LoadLocal; X; Add|Sub; StoreLocal (10 bytes) + optional Load;Pop
				if (!bFused && Op == EOpcode::LoadLocal && Ip + 10 <= OldSize && !IsJumpTarget[Ip + 3] && !IsJumpTarget[Ip + 6] && !IsJumpTarget[Ip + 7] && (static_cast<EOpcode>(Code[Ip + 6]) == EOpcode::Add || static_cast<EOpcode>(Code[Ip + 6]) == EOpcode::Sub) && static_cast<EOpcode>(Code[Ip + 7]) == EOpcode::StoreLocal)
				{
					const std::uint16_t DestSlot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					const std::uint16_t StoreSlot = Bytecode::ReadU16LE(Code.data() + Ip + 8);
					const EOpcode ArithOp = static_cast<EOpcode>(Code[Ip + 6]);
					const EOpcode SecondOp = static_cast<EOpcode>(Code[Ip + 3]);
					if (DestSlot == StoreSlot && (SecondOp == EOpcode::ConstI32 || SecondOp == EOpcode::ConstF64))
					{
						const std::uint16_t ConstIndex = Bytecode::ReadU16LE(Code.data() + Ip + 4);
						std::int32_t Imm = 0;
						if (ConstIndex < Constants.size() && TryGetI32ExactNumericConstant(Constants[ConstIndex], Imm))
						{
							bool bEmit = false;
							std::int32_t AddImm = Imm;
							if (ArithOp == EOpcode::Add)
							{
								bEmit = true;
							}
							else if (Imm != std::numeric_limits<std::int32_t>::min())
							{
								// Sub → AddLocalImm(-imm); INT_MIN cannot be negated safely.
								AddImm = -Imm;
								bEmit = true;
							}
							if (bEmit)
							{
								const std::size_t Start = NewCode.size();
								if (ArithOp == EOpcode::Add && Imm == 1)
								{
									NewCode.push_back(static_cast<std::uint8_t>(EOpcode::IncLocal));
									Bytecode::WriteU16LE(NewCode, DestSlot);
								}
								else
								{
									NewCode.push_back(static_cast<std::uint8_t>(EOpcode::AddLocalImm));
									Bytecode::WriteU16LE(NewCode, DestSlot);
									Bytecode::WriteI32LE(NewCode, AddImm);
								}
								SetNewDebug(Start, DebugAt(Ip));
								OldToNew[Ip] = static_cast<std::uint32_t>(Start);
								OldToNew[Ip + 3] = static_cast<std::uint32_t>(Start);
								OldToNew[Ip + 6] = static_cast<std::uint32_t>(Start);
								OldToNew[Ip + 7] = static_cast<std::uint32_t>(Start);
								const std::size_t Extra = ConsumeAssignResult(Ip + 10, DestSlot);
								if (Extra != 0)
								{
									OldToNew[Ip + 10] = static_cast<std::uint32_t>(Start);
									OldToNew[Ip + 13] = static_cast<std::uint32_t>(Start);
								}
								Ip += 10 + Extra;
								bFused = true;
							}
						}
					}
					else if (ArithOp == EOpcode::Add && DestSlot == StoreSlot && SecondOp == EOpcode::LoadLocal)
					{
						const std::uint16_t SrcSlot = Bytecode::ReadU16LE(Code.data() + Ip + 4);
						const std::size_t Start = NewCode.size();
						NewCode.push_back(static_cast<std::uint8_t>(EOpcode::AddLocalLocal));
						Bytecode::WriteU16LE(NewCode, DestSlot);
						Bytecode::WriteU16LE(NewCode, SrcSlot);
						SetNewDebug(Start, DebugAt(Ip));
						OldToNew[Ip] = static_cast<std::uint32_t>(Start);
						OldToNew[Ip + 3] = static_cast<std::uint32_t>(Start);
						OldToNew[Ip + 6] = static_cast<std::uint32_t>(Start);
						OldToNew[Ip + 7] = static_cast<std::uint32_t>(Start);
						const std::size_t Extra = ConsumeAssignResult(Ip + 10, DestSlot);
						if (Extra != 0)
						{
							OldToNew[Ip + 10] = static_cast<std::uint32_t>(Start);
							OldToNew[Ip + 13] = static_cast<std::uint32_t>(Start);
						}
						Ip += 10 + Extra;
						bFused = true;
					}
				}

				if (bFused)
				{
					continue;
				}

				// Pattern D: StoreLocal s; LoadLocal s; Pop  (statement discard of assigned value)
				if (Op == EOpcode::StoreLocal && Ip + 7 <= OldSize && !IsJumpTarget[Ip + 3] && !IsJumpTarget[Ip + 6] && static_cast<EOpcode>(Code[Ip + 3]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 6]) == EOpcode::Pop)
				{
					const std::uint16_t StoreSlot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					const std::uint16_t LoadSlot = Bytecode::ReadU16LE(Code.data() + Ip + 4);
					if (StoreSlot == LoadSlot)
					{
						const std::size_t Start = NewCode.size();
						NewCode.push_back(static_cast<std::uint8_t>(EOpcode::StoreLocal));
						Bytecode::WriteU16LE(NewCode, StoreSlot);
						SetNewDebug(Start, DebugAt(Ip));
						OldToNew[Ip] = static_cast<std::uint32_t>(Start);
						OldToNew[Ip + 3] = static_cast<std::uint32_t>(Start);
						OldToNew[Ip + 6] = static_cast<std::uint32_t>(Start);
						Ip += 7;
						continue;
					}
				}

				// Copy the instruction verbatim, remembering jump operands to re-target.
				const std::size_t Start = NewCode.size();
				OldToNew[Ip] = static_cast<std::uint32_t>(Start);
				NewCode.insert(NewCode.end(), Code.begin() + Ip, Code.begin() + Ip + Size);
				SetNewDebug(Start, DebugAt(Ip));
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse)
				{
					const std::int32_t Offset = Bytecode::ReadI32LE(Code.data() + Ip + 1);
					const std::int64_t Target =
					    static_cast<std::int64_t>(Ip) + static_cast<std::int64_t>(Size) + Offset;
					JumpFixups.push_back(
					    {Start + 1, static_cast<std::size_t>(Target)});
				}
				else if (Op == EOpcode::JmpIfLocalGeImm || Op == EOpcode::BeginLocalStatic)
				{
					const std::int32_t Offset = Bytecode::ReadI32LE(Code.data() + Ip + Size - 4u);
					const std::int64_t Target =
					    static_cast<std::int64_t>(Ip) + static_cast<std::int64_t>(Size) + Offset;
					JumpFixups.push_back(
					    {Start + Size - 4u, static_cast<std::size_t>(Target)});
				}
				Ip += Size;
			}
			OldToNew[OldSize] = static_cast<std::uint32_t>(NewCode.size());

			// Re-target relative jumps against the rebuilt offsets.
			for (const FJumpFixup& Fixup : JumpFixups)
			{
				const std::uint32_t NewTarget = OldToNew[Fixup.OldTarget];
				const std::int32_t NewOffset =
				    static_cast<std::int32_t>(NewTarget) - static_cast<std::int32_t>(Fixup.NewOperandOffset + 4);
				const auto Unsigned = static_cast<std::uint32_t>(NewOffset);
				NewCode[Fixup.NewOperandOffset] = static_cast<std::uint8_t>(Unsigned & 0xFFu);
				NewCode[Fixup.NewOperandOffset + 1] = static_cast<std::uint8_t>((Unsigned >> 8) & 0xFFu);
				NewCode[Fixup.NewOperandOffset + 2] = static_cast<std::uint8_t>((Unsigned >> 16) & 0xFFu);
				NewCode[Fixup.NewOperandOffset + 3] = static_cast<std::uint8_t>((Unsigned >> 24) & 0xFFu);
			}

			// Remap init-range byte spans (offsets are always instruction boundaries).
			if (InitRanges)
			{
				for (FBytecodeGlobalInitRange& Range : *InitRanges)
				{
					if (Range.BeginOffset <= OldSize)
					{
						Range.BeginOffset = OldToNew[Range.BeginOffset];
					}
					if (Range.EndOffset <= OldSize)
					{
						Range.EndOffset = OldToNew[Range.EndOffset];
					}
				}
			}

			RemapInstructionMetadata(PauseMarkers, Segments, OldToNew);
			Code = std::move(NewCode);
			DebugLines = std::move(NewDebugLines);
		}

		/**
		 * Replace Call of a trivial binary arithmetic leaf with the arithmetic opcode.
		 * Matches callee bodies of the form:
		 *   LoadLocal 0; LoadLocal 1; Add|Sub|Mul|Div|Mod; Return
		 * Call sites already have both arguments on the operand stack, so Call becomes Op.
		 * When InitRanges is non-null (module InitCode), remaps GlobalInitRanges like
		 * PeepholeFuseLocalArithmetic so hot-reload skip spans stay consistent.
		 */
		void PeepholeInlineTrivialCalls(
		    const FBytecodeModule& Module,
		    std::vector<std::uint8_t>& Code,
		    std::vector<FDebugLine>& DebugLines,
		    std::vector<FBytecodeGlobalInitRange>* InitRanges,
		    std::vector<std::uint32_t>& PauseMarkers,
		    std::vector<FBytecodePackageInitSegment>* Segments = nullptr)
		{
			const std::size_t OldSize = Code.size();
			if (OldSize == 0)
			{
				return;
			}

			const auto IsTrivialBinaryArith = [&](const FBytecodeFunction& Fn, EOpcode& OutOp) -> bool
			{
				if (Fn.ParamCount != 2 || Fn.bIsMethod || Fn.Code.size() < 8)
				{
					return false;
				}
				const std::uint8_t* const C = Fn.Code.data();
				const std::size_t N = Fn.Code.size();
				if (static_cast<EOpcode>(C[0]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(C + 1) != 0 || static_cast<EOpcode>(C[3]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(C + 4) != 1)
				{
					return false;
				}
				const EOpcode Op = static_cast<EOpcode>(C[6]);
				if (Op != EOpcode::Add && Op != EOpcode::Sub && Op != EOpcode::Mul && Op != EOpcode::Div && Op != EOpcode::Mod)
				{
					return false;
				}
				if (static_cast<EOpcode>(C[7]) != EOpcode::Return)
				{
					return false;
				}
				for (std::size_t Ip = 8; Ip < N;)
				{
					const EOpcode Trailing = static_cast<EOpcode>(C[Ip]);
					if (Trailing != EOpcode::Nop)
					{
						return false;
					}
					++Ip;
				}
				OutOp = Op;
				return true;
			};

			std::vector<bool> IsJumpTarget(OldSize + 1, false);
			for (std::size_t Ip = 0; Ip < OldSize;)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > OldSize)
				{
					return;
				}
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse || Op == EOpcode::JmpIfLocalGeImm || Op == EOpcode::BeginLocalStatic)
				{
					const std::size_t OffsetPos = Size - 4u;
					const std::int32_t Offset = Bytecode::ReadI32LE(Code.data() + Ip + OffsetPos);
					const std::int64_t Target =
					    static_cast<std::int64_t>(Ip) + static_cast<std::int64_t>(Size) + Offset;
					if (Target >= 0 && static_cast<std::size_t>(Target) <= OldSize)
					{
						IsJumpTarget[static_cast<std::size_t>(Target)] = true;
					}
				}
				Ip += Size;
			}

			if (Segments)
			{
				for (const auto& Segment : *Segments)
				{
					if (Segment.BeginOffset <= OldSize)
					{
						IsJumpTarget[Segment.BeginOffset] = true;
					}
					if (Segment.EndOffset <= OldSize)
					{
						IsJumpTarget[Segment.EndOffset] = true;
					}
				}
			}

			std::vector<std::uint8_t> NewCode;
			NewCode.reserve(OldSize);
			std::vector<FDebugLine> NewDebugLines;
			std::vector<std::uint32_t> OldToNew(OldSize + 1, 0);
			struct FJumpFixup
			{
				std::size_t NewOperandOffset;
				std::size_t OldTarget;
			};
			std::vector<FJumpFixup> JumpFixups;

			const auto DebugAt = [&](const std::size_t Ip) -> FDebugLine
			{
				return Ip < DebugLines.size() ? DebugLines[Ip] : FDebugLine{};
			};
			const auto SetNewDebug = [&](const std::size_t Start, const FDebugLine& Line)
			{
				if (Line.Line == 0)
				{
					return;
				}
				if (NewDebugLines.size() < Start + 1)
				{
					NewDebugLines.resize(Start + 1);
				}
				NewDebugLines[Start] = Line;
			};

			for (std::size_t Ip = 0; Ip < OldSize;)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Op == EOpcode::Call && Size == 4)
				{
					const std::uint16_t FunctionIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					const std::uint8_t ArgCount = Code[Ip + 3];
					EOpcode ArithOp = EOpcode::Nop;
					if (ArgCount == 2 && FunctionIndex < Module.Functions.size() && IsTrivialBinaryArith(Module.Functions[FunctionIndex], ArithOp))
					{
						const std::size_t Start = NewCode.size();
						NewCode.push_back(static_cast<std::uint8_t>(ArithOp));
						SetNewDebug(Start, DebugAt(Ip));
						OldToNew[Ip] = static_cast<std::uint32_t>(Start);
						Ip += Size;
						continue;
					}
				}

				// Keep CallBoundMethod observable. Runtime instance-call interceptors are
				// installable after compilation, so replacing a call with IncField here
				// would permanently bypass the VM/JIT interception contract.

				// A discarded postfix increment on a local object's numeric field is
				// emitted conservatively as a load/old-value temporary/store sequence.
				// Once the complete sequence is visible, reduce it to the same checked
				// IncField operation used for `this` fields. Restricting the receiver to
				// LoadLocal preserves arbitrary receiver side effects and reference
				// semantics while removing two temporary slots and several FValue moves.
				constexpr std::size_t LocalFieldPostfixIncSize = 30u;
				if (Op == EOpcode::LoadLocal && Size == 3 && Ip + LocalFieldPostfixIncSize <= OldSize)
				{
					const std::uint16_t ReceiverSlot =
					    Bytecode::ReadU16LE(Code.data() + Ip + 1u);
					const bool bShape =
					    static_cast<EOpcode>(Code[Ip + 3u]) == EOpcode::LoadFieldKeep && static_cast<EOpcode>(Code[Ip + 6u]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[Ip + 9u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 12u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 15u]) == EOpcode::Add && static_cast<EOpcode>(Code[Ip + 16u]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[Ip + 19u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 22u]) == EOpcode::StoreField && static_cast<EOpcode>(Code[Ip + 25u]) == EOpcode::Pop && static_cast<EOpcode>(Code[Ip + 26u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 29u]) == EOpcode::Pop;
					if (bShape)
					{
						const std::uint16_t FieldName =
						    Bytecode::ReadU16LE(Code.data() + Ip + 4u);
						const std::uint16_t OldSlot =
						    Bytecode::ReadU16LE(Code.data() + Ip + 7u);
						const std::uint16_t ValueSlot =
						    Bytecode::ReadU16LE(Code.data() + Ip + 17u);
						const std::uint16_t ConstantIndex =
						    Bytecode::ReadU16LE(Code.data() + Ip + 13u);
						const bool bSameSlots =
						    Bytecode::ReadU16LE(Code.data() + Ip + 10u) == OldSlot && Bytecode::ReadU16LE(Code.data() + Ip + 20u) == ValueSlot && Bytecode::ReadU16LE(Code.data() + Ip + 23u) == FieldName && Bytecode::ReadU16LE(Code.data() + Ip + 27u) == OldSlot;
						const bool bConstOne = ConstantIndex < Module.Constants.size() && Module.Constants[ConstantIndex].Kind == EConstantKind::I32 && Module.Constants[ConstantIndex].I32 == 1;
						bool bInteriorJumpTarget = false;
						for (std::size_t Offset = 3u;
						    Offset < LocalFieldPostfixIncSize; ++Offset)
						{
							bInteriorJumpTarget = bInteriorJumpTarget || IsJumpTarget[Ip + Offset];
						}
						if (bSameSlots && bConstOne && !bInteriorJumpTarget)
						{
							const std::size_t Start = NewCode.size();
							NewCode.push_back(static_cast<std::uint8_t>(EOpcode::LoadLocal));
							Bytecode::WriteU16LE(NewCode, ReceiverSlot);
							NewCode.push_back(static_cast<std::uint8_t>(EOpcode::IncField));
							Bytecode::WriteU16LE(NewCode, FieldName);
							SetNewDebug(Start, DebugAt(Ip));
							OldToNew[Ip] = static_cast<std::uint32_t>(Start);
							for (std::size_t Skip = 1u;
							    Skip < LocalFieldPostfixIncSize; ++Skip)
							{
								OldToNew[Ip + Skip] = static_cast<std::uint32_t>(Start);
							}
							Ip += LocalFieldPostfixIncSize;
							continue;
						}
					}
				}

				// In-method `field = field + 1` -> LoadThis; IncField (int/uint/double).
				if (Op == EOpcode::LoadThisField && Size == 3 && Ip + 10 <= OldSize && !IsJumpTarget[Ip + 3] && !IsJumpTarget[Ip + 6] && !IsJumpTarget[Ip + 7])
				{
					const std::uint16_t FieldName = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					const EOpcode ConstOp = static_cast<EOpcode>(Code[Ip + 3]);
					const EOpcode AddOp = static_cast<EOpcode>(Code[Ip + 6]);
					const EOpcode StoreOp = static_cast<EOpcode>(Code[Ip + 7]);
					bool bConstOne = false;
					if ((ConstOp == EOpcode::ConstI32 || ConstOp == EOpcode::ConstF64) && AddOp == EOpcode::Add && StoreOp == EOpcode::StoreThisField && Bytecode::ReadU16LE(Code.data() + Ip + 8) == FieldName)
					{
						const std::uint16_t ConstIndex = Bytecode::ReadU16LE(Code.data() + Ip + 4);
						if (ConstIndex < Module.Constants.size())
						{
							const auto& Constant = Module.Constants[ConstIndex];
							if (ConstOp == EOpcode::ConstF64)
							{
								bConstOne = (Constant.Kind == EConstantKind::F64 && Constant.F64 == 1.0) || (Constant.Kind == EConstantKind::F32 && Constant.F32 == 1.0f);
							}
							else if (Constant.Kind == EConstantKind::I32)
							{
								bConstOne = Constant.I32 == 1;
							}
							else if (Constant.Kind == EConstantKind::U32)
							{
								bConstOne = Constant.U32 == 1u;
							}
						}
					}
					if (bConstOne)
					{
						std::size_t Consume = 10;
						if (Ip + 14 <= OldSize && !IsJumpTarget[Ip + 10] && !IsJumpTarget[Ip + 13] && static_cast<EOpcode>(Code[Ip + 10]) == EOpcode::LoadThisField && Bytecode::ReadU16LE(Code.data() + Ip + 11) == FieldName && static_cast<EOpcode>(Code[Ip + 13]) == EOpcode::Pop)
						{
							Consume = 14;
						}
						const std::size_t Start = NewCode.size();
						NewCode.push_back(static_cast<std::uint8_t>(EOpcode::LoadThis));
						NewCode.push_back(static_cast<std::uint8_t>(EOpcode::IncField));
						Bytecode::WriteU16LE(NewCode, FieldName);
						SetNewDebug(Start, DebugAt(Ip));
						OldToNew[Ip] = static_cast<std::uint32_t>(Start);
						for (std::size_t Skip = 1; Skip < Consume; ++Skip)
						{
							OldToNew[Ip + Skip] = static_cast<std::uint32_t>(Start);
						}
						Ip += Consume;
						continue;
					}
				}

				const std::size_t Start = NewCode.size();
				OldToNew[Ip] = static_cast<std::uint32_t>(Start);
				NewCode.insert(NewCode.end(), Code.begin() + Ip, Code.begin() + Ip + Size);
				SetNewDebug(Start, DebugAt(Ip));
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse)
				{
					const std::int32_t Offset = Bytecode::ReadI32LE(Code.data() + Ip + 1);
					const std::int64_t Target =
					    static_cast<std::int64_t>(Ip) + static_cast<std::int64_t>(Size) + Offset;
					JumpFixups.push_back({Start + 1, static_cast<std::size_t>(Target)});
				}
				else if (Op == EOpcode::JmpIfLocalGeImm || Op == EOpcode::BeginLocalStatic)
				{
					const std::int32_t Offset = Bytecode::ReadI32LE(Code.data() + Ip + Size - 4u);
					const std::int64_t Target =
					    static_cast<std::int64_t>(Ip) + static_cast<std::int64_t>(Size) + Offset;
					JumpFixups.push_back({Start + Size - 4u, static_cast<std::size_t>(Target)});
				}
				Ip += Size;
			}
			OldToNew[OldSize] = static_cast<std::uint32_t>(NewCode.size());

			for (const FJumpFixup& Fixup : JumpFixups)
			{
				const std::uint32_t NewTarget = OldToNew[Fixup.OldTarget];
				const std::int32_t NewOffset =
				    static_cast<std::int32_t>(NewTarget) - static_cast<std::int32_t>(Fixup.NewOperandOffset + 4);
				const auto Unsigned = static_cast<std::uint32_t>(NewOffset);
				NewCode[Fixup.NewOperandOffset] = static_cast<std::uint8_t>(Unsigned & 0xFFu);
				NewCode[Fixup.NewOperandOffset + 1] = static_cast<std::uint8_t>((Unsigned >> 8) & 0xFFu);
				NewCode[Fixup.NewOperandOffset + 2] = static_cast<std::uint8_t>((Unsigned >> 16) & 0xFFu);
				NewCode[Fixup.NewOperandOffset + 3] = static_cast<std::uint8_t>((Unsigned >> 24) & 0xFFu);
			}

			if (InitRanges)
			{
				for (FBytecodeGlobalInitRange& Range : *InitRanges)
				{
					if (Range.BeginOffset <= OldSize)
					{
						Range.BeginOffset = OldToNew[Range.BeginOffset];
					}
					if (Range.EndOffset <= OldSize)
					{
						Range.EndOffset = OldToNew[Range.EndOffset];
					}
				}
			}

			RemapInstructionMetadata(PauseMarkers, Segments, OldToNew);
			Code = std::move(NewCode);
			DebugLines = std::move(NewDebugLines);
		}
	}

	void VeroScriptCompiler::OptimizeFunctionBytecode(FBytecodeFunction& Function)
	{
		if (!Module || !Function.ExceptionRegions.empty())
		{
			return;
		}
		std::vector<FBytecodeGlobalInitRange> Ranges;
		for (const auto& Initializer : Function.LocalStaticInitializers)
		{
			Ranges.push_back({Initializer.GlobalSlot, Initializer.BeginOffset, Initializer.EndOffset});
			Ranges.push_back({Initializer.GlobalSlot, Initializer.BeginOffset, Initializer.ScopeEndOffset});
		}
		PeepholeFuseLocalArithmetic(
		    Module->Constants,
		    Function.Code,
		    Function.DebugLines,
		    Ranges.empty() ? nullptr : &Ranges,
		    &Function.LocalNames, Function.PauseMarkerOffsets);
		for (std::size_t Index = 0; Index < Function.LocalStaticInitializers.size(); ++Index)
		{
			auto& Initializer = Function.LocalStaticInitializers[Index];
			Initializer.BeginOffset = static_cast<std::uint32_t>(Ranges[Index * 2u].BeginOffset);
			Initializer.EndOffset = static_cast<std::uint32_t>(Ranges[Index * 2u].EndOffset);
			Initializer.ScopeEndOffset = static_cast<std::uint32_t>(Ranges[Index * 2u + 1u].EndOffset);
			Initializer.CommitOffset = Initializer.EndOffset - 3u;
		}
	}

	void VeroScriptCompiler::OptimizeInitBytecode()
	{
		if (!Module)
		{
			return;
		}
		PeepholeFuseLocalArithmetic(
		    Module->Constants,
		    Module->InitCode,
		    Module->InitDebugLines,
		    &Module->GlobalInitRanges,
		    nullptr, Module->InitPauseMarkerOffsets, &Module->PackageInitSegments);
	}

	void VeroScriptCompiler::OptimizeModuleCalls()
	{
		if (!Module)
		{
			return;
		}
		for (FBytecodeFunction& Function : Module->Functions)
		{
			if (Function.ExceptionRegions.empty())
			{
				std::vector<FBytecodeGlobalInitRange> Ranges;
				for (const auto& Initializer : Function.LocalStaticInitializers)
				{
					Ranges.push_back({Initializer.GlobalSlot, Initializer.BeginOffset, Initializer.EndOffset});
					Ranges.push_back({Initializer.GlobalSlot, Initializer.BeginOffset, Initializer.ScopeEndOffset});
				}
				PeepholeInlineTrivialCalls(*Module, Function.Code, Function.DebugLines,
				    Ranges.empty() ? nullptr : &Ranges, Function.PauseMarkerOffsets);
				for (std::size_t Index = 0; Index < Function.LocalStaticInitializers.size(); ++Index)
				{
					auto& Initializer = Function.LocalStaticInitializers[Index];
					Initializer.BeginOffset = static_cast<std::uint32_t>(Ranges[Index * 2u].BeginOffset);
					Initializer.EndOffset = static_cast<std::uint32_t>(Ranges[Index * 2u].EndOffset);
					Initializer.ScopeEndOffset = static_cast<std::uint32_t>(Ranges[Index * 2u + 1u].EndOffset);
					Initializer.CommitOffset = Initializer.EndOffset - 3u;
				}
			}
		}
		PeepholeInlineTrivialCalls(
		    *Module,
		    Module->InitCode,
		    Module->InitDebugLines,
		    &Module->GlobalInitRanges, Module->InitPauseMarkerOffsets, &Module->PackageInitSegments);
	}
}

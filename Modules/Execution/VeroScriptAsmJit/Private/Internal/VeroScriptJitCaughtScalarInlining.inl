// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		struct FScalarJumpFixup
		{
			std::size_t Operand;
			std::size_t Target;
		};

		void EmitScalarLocal(FBytecodeFunction& Output, EOpcode Op, std::uint16_t Slot)
		{
			Output.Code.push_back(static_cast<std::uint8_t>(Op));
			Bytecode::WriteU16LE(Output.Code, Slot);
		}

		void EmitScalarJump(FBytecodeFunction& Output, std::size_t Target,
		    std::vector<FScalarJumpFixup>& Fixups)
		{
			Output.Code.push_back(static_cast<std::uint8_t>(EOpcode::Jmp));
			Fixups.push_back({Output.Code.size(), Target});
			Bytecode::WriteI32LE(Output.Code, 0);
		}

		bool PatchScalarJumps(FBytecodeFunction& Output,
		    const std::vector<std::size_t>& Offsets,
		    const std::vector<FScalarJumpFixup>& Fixups)
		{
			for (const auto& Fixup : Fixups)
			{
				if (Fixup.Target >= Offsets.size() || Offsets[Fixup.Target] == std::numeric_limits<std::size_t>::max())
				{
					return false;
				}
				const auto Delta = static_cast<std::int64_t>(Offsets[Fixup.Target]) -
				    static_cast<std::int64_t>(Fixup.Operand + 4u);
				PatchI32(Output.Code, Fixup.Operand, static_cast<std::int32_t>(Delta));
			}
			return true;
		}

		bool CopyScalarInstruction(const FBytecodeFunction& Input, const std::size_t Ip,
		    EOpcode Op, std::size_t Size, std::uint16_t LocalBase,
		    FBytecodeFunction& Output, std::vector<FScalarJumpFixup>& Fixups)
		{
			const std::size_t Begin = Output.Code.size();
			Output.Code.insert(Output.Code.end(), Input.Code.begin() + Ip, Input.Code.begin() + Ip + Size);
			if (LocalBase != 0u)
			{
				switch (Op)
				{
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::JmpIfLocalGeImm:
					{
						const auto Slot = Bytecode::ReadU16LE(Input.Code.data() + Ip + 1u);
						PatchU16(Output.Code, Begin + 1u, static_cast<std::uint16_t>(LocalBase + Slot));
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						const auto Dest = Bytecode::ReadU16LE(Input.Code.data() + Ip + 1u);
						const auto Source = Bytecode::ReadU16LE(Input.Code.data() + Ip + 3u);
						PatchU16(Output.Code, Begin + 1u, static_cast<std::uint16_t>(LocalBase + Dest));
						PatchU16(Output.Code, Begin + 3u, static_cast<std::uint16_t>(LocalBase + Source));
						break;
					}
				default:
					{
						break;
					}
				}
			}
			std::size_t RelativeOperand = 0u;
			if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse)
			{
				RelativeOperand = 1u;
			}
			else if (Op == EOpcode::JmpIfLocalGeImm)
			{
				RelativeOperand = 7u;
			}
			if (RelativeOperand != 0u)
			{
				std::size_t Target = 0;
				if (!IsJumpTargetInRange(Ip + Size,
				        Bytecode::ReadI32LE(Input.Code.data() + Ip + RelativeOperand),
				        Input.Code.size(), Target))
				{
					return false;
				}
				Fixups.push_back({Begin + RelativeOperand, Target});
			}
			return true;
		}

		bool ReadI32CatchInlineSignature(const FBytecodeFunction& Function, std::vector<EStackTag>& Params)
		{
			constexpr std::string_view Prefix = "fn(";
			constexpr std::string_view Suffix = ")->int";
			std::string_view Type = Function.ClosureTypeName;
			if (Function.bIsMethod || Function.bReturnsVoid || Function.bReturnsRef ||
			    Function.bDeleted || Function.bIsAbstract || !Function.UnavailableReason.empty() ||
			    !Function.Captures.empty() || !Function.LocalStaticInitializers.empty() ||
			    Function.bUsesLocalStaticStorage || BytecodeFunctionHasRefParams(Function) ||
			    Type.size() < Prefix.size() + Suffix.size() || Type.substr(0u, Prefix.size()) != Prefix ||
			    Type.substr(Type.size() - Suffix.size()) != Suffix)
			{
				return false;
			}
			// The ordinary scalar ABI treats char and short as I32. Catch
			// matching observes their exact value type, so exclude them here.
			Type.remove_prefix(Prefix.size());
			Type.remove_suffix(Suffix.size());
			std::size_t Count = 0;
			while (!Type.empty())
			{
				const auto Separator = Type.find(',');
				if (Type.substr(0u, Separator) != "int" || ++Count > Function.ParamCount) return false;
				if (Separator == std::string_view::npos)
				{
					break;
				}
				Type.remove_prefix(Separator + 1u);
				if (Type.empty()) return false;
			}
			if (Count != Function.ParamCount) return false;
			Params.assign(Count, EStackTag::I32);
			return true;
		}

		bool IsCaughtScalarInstruction(const FBytecodeModule& Module, const FBytecodeFunction& Function,
		    std::size_t Ip, EOpcode Op)
		{
			if (Op == EOpcode::ConstI32)
			{
				const auto Constant = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
				return Constant < Module.Constants.size() && Module.Constants[Constant].Kind == EConstantKind::I32;
			}
			return Op != EOpcode::ConstF64 && IsLocalScalarLinearOpcode(Op);
		}

		bool CanMapCaughtScalarDiagnostics(const FBytecodeModule& Module,
		    const FBytecodeFunction& Caller, const FBytecodeFunction& Callee)
		{
			if (Callee.DebugLines.empty())
			{
				return Caller.DebugLines.empty() &&
				    Caller.Location.FileLabel == Callee.Location.FileLabel &&
				    Caller.Location.Line == Callee.Location.Line &&
				    Caller.Location.Column == Callee.Location.Column;
			}
			// A stripped library can share a call graph with mapped source.
			// Falling back to the caller's line would change the diagnostic.
			return Callee.DebugLines.front().Line != 0u &&
			    std::all_of(Callee.DebugLines.begin(), Callee.DebugLines.end(),
			        [&](const FDebugLine& Line)
			    {
				    return Line.Line == 0u || (Line.FileIndex < Module.DebugFiles.size() &&
				        !Module.DebugFiles[Line.FileIndex].empty());
			    });
		}

		struct FScalarStackRequirement
		{
			std::size_t Offset;
			std::size_t Depth;
		};

		bool ExpandCaughtI32Callee(const FBytecodeModule& Module,
		    const FBytecodeFunction& Callee, std::uint8_t ArgumentCount,
		    const FBytecodeCatchHandler& Handler, std::size_t CallDepth, FDebugLine CallLine,
		    FBytecodeFunction& Output, std::vector<FScalarJumpFixup>& CallerFixups,
		    std::vector<FScalarStackRequirement>& Requirements, bool& bSawThrow)
		{
			std::vector<EStackTag> Params;
			if (Callee.Code.empty() || Callee.Code.size() > 192u ||
			    !Callee.ExceptionRegions.empty() || Callee.ParamCount != ArgumentCount ||
			    Callee.LocalCount != ArgumentCount || !ReadI32CatchInlineSignature(Callee, Params) ||
			    Output.LocalCount + ArgumentCount > std::numeric_limits<std::uint16_t>::max())
			{
				return false;
			}
			std::vector<FScalarStackPoint> Points;
			if (!ValidateCfg(Module, Callee, Params, EStackTag::I32, false, nullptr, nullptr, nullptr, &Points))
			{
				return false;
			}

			const std::uint16_t LocalBase = Output.LocalCount;
			Output.LocalCount = static_cast<std::uint16_t>(LocalBase + ArgumentCount);
			const std::size_t Retained = CallDepth - ArgumentCount;
			for (std::uint16_t Param = ArgumentCount; Param != 0u; --Param)
			{
				const std::size_t Begin = Output.Code.size();
				Requirements.push_back({Begin, Retained + Param});
				EmitScalarLocal(Output, EOpcode::StoreLocal, static_cast<std::uint16_t>(LocalBase + Param - 1u));
				Output.DebugLines.resize(Output.Code.size());
				Output.DebugLines[Begin] = CallLine;
			}

			const std::size_t CodeSize = Callee.Code.size();
			std::vector<std::size_t> Offsets(CodeSize + 1u, std::numeric_limits<std::size_t>::max());
			std::vector<FScalarJumpFixup> Fixups;
			FDebugLine CurrentLine{};
			for (std::size_t Ip = 0; Ip < CodeSize;)
			{
				const auto Op = static_cast<EOpcode>(Callee.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0u || Size > CodeSize - Ip ||
				    (Op != EOpcode::Throw && !IsCaughtScalarInstruction(Module, Callee, Ip, Op)))
				{
					return false;
				}
				const std::size_t Begin = Output.Code.size();
				Offsets[Ip] = Begin;
				if (Ip < Callee.DebugLines.size() && Callee.DebugLines[Ip].Line != 0u)
				{
					CurrentLine = Callee.DebugLines[Ip];
				}
				if (Op == EOpcode::Throw)
				{
					if (Points[Ip].Depth != 1u || Points[Ip].Top != EStackTag::I32) return false;
					bSawThrow = true;
					Requirements.push_back({Begin, Retained + 1u});
					if (Handler.LocalSlot != Handler.RethrowSlot)
					{
						Output.Code.push_back(static_cast<std::uint8_t>(EOpcode::Dup));
						EmitScalarLocal(Output, EOpcode::StoreLocal, Handler.LocalSlot);
					}
					EmitScalarLocal(Output, EOpcode::StoreLocal, Handler.RethrowSlot);
					Output.Code.insert(Output.Code.end(), Retained, static_cast<std::uint8_t>(EOpcode::Pop));
					EmitScalarJump(Output, Handler.HandlerOffset, CallerFixups);
				}
				else if (Op == EOpcode::Return)
				{
					if (Ip + Size != CodeSize)
					{
						EmitScalarJump(Output, CodeSize, Fixups);
					}
				}
				else if (!CopyScalarInstruction(Callee, Ip, Op, Size, LocalBase, Output, Fixups))
				{
					return false;
				}
				if (Output.Code.size() > Begin)
				{
					Output.DebugLines.resize(Output.Code.size());
					Output.DebugLines[Begin] = CurrentLine;
				}
				Ip += Size;
			}
			Offsets[CodeSize] = Output.Code.size();
			// A cyclic leaf remains a standalone call. Its dispatch is already
			// amortized across its own loop, and expanding it would duplicate code.
			for (const auto& Fixup : Fixups)
			{
				if (Offsets[Fixup.Target] <= Fixup.Operand) return false;
			}
			return PatchScalarJumps(Output, Offsets, Fixups);
		}

		bool TryBuildCaughtI32Calls(const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, FBytecodeFunction& Output, std::uint16_t& OutSites)
		{
			std::vector<EStackTag> Params;
			if (Function.Code.size() > 4096u || Function.ExceptionRegions.size() != 1u ||
			    !ReadI32CatchInlineSignature(Function, Params))
			{
				return false;
			}
			const auto& Region = Function.ExceptionRegions.front();
			if (Region.FinallyOffset != std::numeric_limits<std::uint32_t>::max() || Region.Catches.size() != 1u)
			{
				return false;
			}
			const auto& Handler = Region.Catches.front();
			if (Handler.TypeName != "int" ||
			    Region.BeginOffset >= Region.EndOffset || Region.EndOffset > Function.Code.size() ||
			    Handler.LocalSlot < Function.ParamCount || Handler.LocalSlot >= Function.LocalCount ||
			    Handler.RethrowSlot < Function.ParamCount || Handler.RethrowSlot >= Function.LocalCount ||
			    Handler.HandlerOffset < Region.EndOffset || Handler.HandlerOffset >= Function.Code.size())
			{
				return false;
			}
			// The ordinary CFG treats calls as returning and yields the operands
			// retained by the caller under each call's arguments. The completed CFG
			// below also checks paths reached through the new catch edges.
			std::vector<FScalarStackPoint> Points;
			if (!ValidateCfg(Module, Function, Params, EStackTag::I32, false, nullptr, nullptr, nullptr, &Points))
			{
				return false;
			}

			const std::size_t CodeSize = Function.Code.size();
			std::vector<std::size_t> Offsets(CodeSize + 1u, std::numeric_limits<std::size_t>::max());
			std::vector<FScalarJumpFixup> Fixups;
			std::vector<FScalarStackRequirement> Requirements;
			Output = Function;
			Output.Code.clear();
			Output.DebugLines.clear();
			Output.ExceptionRegions.clear();
			Output.PauseMarkerOffsets.clear();
			bool bSawThrow = false;
			std::uint16_t Sites = 0;
			for (std::size_t Ip = 0; Ip < CodeSize;)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0u || Size > CodeSize - Ip) return false;
				const std::size_t Begin = Output.Code.size();
				Offsets[Ip] = Begin;
				const FDebugLine Line = Ip < Function.DebugLines.size() ? Function.DebugLines[Ip] : FDebugLine{};
				if (Op == EOpcode::Call)
				{
					const auto Target = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
					const auto Arity = Function.Code[Ip + 3u];
					if (Target >= Module.Functions.size() || &Function == &Module.Functions[Target] ||
					    Ip < Region.BeginOffset || Ip >= Region.EndOffset || Sites == ScalarInlineSitesPerPass ||
					    Points[Ip].Depth < Arity || Points[Ip].Depth - Arity > 16u ||
					    !CanMapCaughtScalarDiagnostics(Module, Function, Module.Functions[Target]) ||
					    !ExpandCaughtI32Callee(Module, Module.Functions[Target], Arity, Handler,
					        Points[Ip].Depth, Line, Output, Fixups, Requirements, bSawThrow))
					{
						return false;
					}
					++Sites;
				}
				else
				{
					if (!IsCaughtScalarInstruction(Module, Function, Ip, Op) ||
					    !CopyScalarInstruction(Function, Ip, Op, Size, 0u, Output, Fixups))
					{
						return false;
					}
					Output.DebugLines.resize(Output.Code.size());
					Output.DebugLines[Begin] = Line;
				}
				if (Output.Code.size() > CodeSize + ScalarInlineAddedBytesPerPass) return false;
				Ip += Size;
			}
			Offsets[CodeSize] = Output.Code.size();
			constexpr auto InvalidOffset = std::numeric_limits<std::size_t>::max();
			if (!bSawThrow || Offsets[Region.BeginOffset] == InvalidOffset ||
			    Offsets[Region.EndOffset] == InvalidOffset || Offsets[Handler.HandlerOffset] == InvalidOffset ||
			    !PatchScalarJumps(Output, Offsets, Fixups) ||
			    !ValidateCfg(Module, Output, Params, EStackTag::I32, false, nullptr, nullptr, nullptr, &Points))
			{
				return false;
			}
			for (const auto& Requirement : Requirements)
			{
				const auto& Point = Points[Requirement.Offset];
				if (Point.Depth != Requirement.Depth || Point.Top != EStackTag::I32) return false;
			}
			OutSites = Sites;
			return true;
		}

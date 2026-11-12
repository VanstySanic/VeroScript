// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		// Primitive operands stay unboxed between native CFG blocks. There are no host
		// checks or calls in this mode. Locals and every canonical check remain in Core.
		bool ExecuteScalarInstruction(const std::size_t Offset, int& Result)
		{
			if (!bCompiledActive || HasError() || HasPendingControlTransfer()) return false;
			const auto* Function = Vm.Frames.back().Function;
			if (!Function || Offset >= Function->Code.size()) return ExecuteInstruction(Offset, Result);
			if (!Vm.CheckExecutionStep(Function, Offset)) return false;
			const auto* Instruction = Function->Code.data() + Offset;
			const auto Op = static_cast<EOpcode>(*Instruction);
			const auto Size = Bytecode::InstructionSize(Op);
			Vm.Frames.back().InstructionPointer = Offset + Size;
			auto& State = *ScalarBlockState;
			const auto General = [&]()
			{
				for (std::size_t Index = 0; Index < State.Depth; ++Index)
				{
					const auto Value = State.Values[Index];
					Vm.Push(Value.IsBoolean ? FValue{Value.Value != 0} : FValue{Value.Value});
				}
				ScalarBlockState.reset();
				return !HasError() && ExecuteInstruction(Offset, Result, true);
			};
			if (!Size || Size > Function->Code.size() - Offset) return General();
			const auto Push = [&](const std::int32_t Value, const bool Boolean = false)
			{
				if (State.Depth == State.Values.size() || Vm.OperandStack.size() > Vm.MaxOperandStackDepth || State.Depth >= Vm.MaxOperandStackDepth - Vm.OperandStack.size())
				{
					return false;
				}
				State.Values[State.Depth++] = {Value, Boolean};
				return true;
			};
			Result = 0;
			switch (Op)
			{
			case EOpcode::Nop:
				return true;
			case EOpcode::Jmp:
				{
					Result = 1;
					return true;
				}
			case EOpcode::ConstI32:
				{
					const auto Index = Bytecode::ReadU16LE(Instruction + 1);
					if (Index >= Vm.ActiveModule->Constants.size()) return General();
					const auto& C = Vm.ActiveModule->Constants[Index];
					return C.Kind == EConstantKind::I32 && Push(C.I32) ? true : General();
				}
			case EOpcode::ConstBool:
				return Push(Instruction[1] != 0, true) ? true : General();
			case EOpcode::LoadLocal:
			case EOpcode::StoreLocal:
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
			case EOpcode::JmpIfLocalGeImm:
				{
					const auto Index = Bytecode::ReadU16LE(Instruction + 1);
					auto& Locals = Vm.Frames.back().Locals;
					if (Index >= Locals.size()) return General();
					auto* Value = std::get_if<std::int32_t>(&Locals[Index]);
					if (Op == EOpcode::LoadLocal)
					{
						if (Value) return Push(*Value) ? true : General();
						const auto* Boolean = std::get_if<bool>(&Locals[Index]);
						return Boolean && Push(*Boolean, true) ? true : General();
					}
					if (Op == EOpcode::StoreLocal)
					{
						if (!State.Depth || (!Value && !std::holds_alternative<bool>(Locals[Index]))) return General();
						const auto Source = State.Values[--State.Depth];
						Locals[Index] = Source.IsBoolean ? FValue{Source.Value != 0} : FValue{Source.Value};
						return true;
					}
					if (!Value) return General();
					if (Op == EOpcode::JmpIfLocalGeImm)
					{
						Result = *Value >= Bytecode::ReadI32LE(Instruction + 3);
						return true;
					}
					std::int32_t Right = 1;
					if (Op == EOpcode::AddLocalImm)
					{
						Right = Bytecode::ReadI32LE(Instruction + 3);
					}
					if (Op == EOpcode::AddLocalLocal)
					{
						const auto Source = Bytecode::ReadU16LE(Instruction + 3);
						if (Source >= Locals.size() || !std::holds_alternative<std::int32_t>(Locals[Source])) return General();
						Right = std::get<std::int32_t>(Locals[Source]);
					}
					std::int32_t Sum = 0;
					if (!Runtime::CheckedAdd(*Value, Right, Sum)) return General();
					*Value = Sum;
					return true;
				}
			case EOpcode::Dup:
				return State.Depth && Push(State.Values[State.Depth - 1].Value, State.Values[State.Depth - 1].IsBoolean) ? true : General();
			case EOpcode::Pop:
				{
					if (!State.Depth) return General();
					--State.Depth;
					return true;
				}
			case EOpcode::JmpIfFalse:
				{
					if (!State.Depth) return General();
					Result = State.Values[--State.Depth].Value == 0;
					return true;
				}
			case EOpcode::CallBuiltin:
				{
					if (Instruction[2] != 1 || static_cast<EBuiltinId>(Instruction[1]) != EBuiltinId::ToInt || !State.Depth)
					{
						return General();
					}
					State.Values[State.Depth - 1].IsBoolean = false;
					return true;
				}
			case EOpcode::Return:
				return General();
			default:
				{
					if (State.Depth < 2) return General();
					const bool LeftBoolean = State.Values[State.Depth - 2].IsBoolean;
					const bool RightBoolean = State.Values[State.Depth - 1].IsBoolean;
					if ((LeftBoolean || RightBoolean) && (!(LeftBoolean && RightBoolean) || (Op != EOpcode::Eq && Op != EOpcode::Ne)))
					{
						return General();
					}
					const auto Left = State.Values[State.Depth - 2].Value, Right = State.Values[State.Depth - 1].Value;
					std::int32_t Value = 0;
					bool Boolean = false;
					switch (Op)
					{
					case EOpcode::Eq:
						{
							Value = Left == Right;
							Boolean = true;
							break;
						}
					case EOpcode::Ne:
						{
							Value = Left != Right;
							Boolean = true;
							break;
						}
					case EOpcode::Lt:
						{
							Value = Left < Right;
							Boolean = true;
							break;
						}
					case EOpcode::Le:
						{
							Value = Left <= Right;
							Boolean = true;
							break;
						}
					case EOpcode::Gt:
						{
							Value = Left > Right;
							Boolean = true;
							break;
						}
					case EOpcode::Ge:
						{
							Value = Left >= Right;
							Boolean = true;
							break;
						}
					case EOpcode::BitAnd:
						{
							Value = Left & Right;
							break;
						}
					case EOpcode::BitOr:
						{
							Value = Left | Right;
							break;
						}
					case EOpcode::BitXor:
						{
							Value = Left ^ Right;
							break;
						}
					default:
						{
							if (!VmDetail::TryEvalInt32Arithmetic(Op, Left, Right, Value)) return General();
						}
					}
					State.Values[State.Depth - 2] = {Value, Boolean};
					--State.Depth;
					return true;
				}
			}
		}

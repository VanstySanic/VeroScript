// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		// Internal fragment: checked compiled instruction semantics in the private service section.

		bool ExecuteInstruction(const std::size_t Offset, int& Result, const bool StepChecked = false)
		{
			if (!bCompiledActive || (!StepChecked && !CheckActiveStep(Offset))) return false;
			const auto* Function = Vm.Frames.back().Function;
			const auto* Instruction = Function->Code.data() + Offset;
			const auto Op = static_cast<EOpcode>(*Instruction);
			const auto Size = Bytecode::InstructionSize(Op);
			if (Size > Function->Code.size() - Offset)
			{
				ReportError("truncated compiled instruction");
				return false;
			}
			Vm.Frames.back().InstructionPointer = Offset + Size;
			Vm.ActiveInstructionStart = Offset;
			const auto Need = [&](const std::size_t Count)
			{
				if (Vm.OperandStack.size() < CompiledStackBase || Vm.OperandStack.size() - CompiledStackBase < Count)
				{
					ReportError("compiled operand stack underflow");
					return false;
				}
				return true;
			};
			const auto Index = [&]()
			{
				return Bytecode::ReadU16LE(Instruction + 1);
			};
			Result = 0;
			// Inspect direct storage only after the ordinary step check: host checks
			// may have changed its representation. Other values retain the full path.
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
			case EOpcode::ConstF64:
				{
					const auto Constant = Bytecode::ReadU16LE(Instruction + 1);
					if (Constant >= Vm.ActiveModule->Constants.size())
					{
						ReportError("invalid compiled constant");
						return false;
					}
					const auto& Value = Vm.ActiveModule->Constants[Constant];
					if (Value.Kind == EConstantKind::I32)
					{
						Vm.Push(Value.I32);
					}
					else
					{
						Vm.Push(Runtime::ConstantToValue(*Vm.ActiveModule, Value));
					}
					return !HasError();
				}
			case EOpcode::ConstBool:
				{
					Vm.Push(Instruction[1] != 0);
					return !HasError();
				}
			case EOpcode::LoadLocal:
				{
					const auto Slot = Bytecode::ReadU16LE(Instruction + 1);
					const auto& Locals = Vm.Frames.back().Locals;
					if (Slot < Locals.size())
					{
						if (const auto* Value = std::get_if<std::int32_t>(&Locals[Slot]))
						{
							Vm.Push(*Value);
							return !HasError();
						}
					}
					if (Slot < Locals.size() && (Runtime::IsNumericValue(Locals[Slot]) || std::holds_alternative<bool>(Locals[Slot])))
					{
						Vm.Push(Locals[Slot]);
						return !HasError();
					}
					break;
				}
			case EOpcode::StoreLocal:
				{
					if (!Need(1)) return false;
					const auto Slot = Index();
					auto& Locals = Vm.Frames.back().Locals;
					const auto& Source = Vm.OperandStack.back();
					if (Slot < Locals.size() && Locals[Slot].index() == Source.index() && (Runtime::IsNumericValue(Source) || std::holds_alternative<bool>(Source)))
					{
						Locals[Slot] = std::move(Vm.OperandStack.back());
						Vm.OperandStack.pop_back();
						return true;
					}
					break;
				}
			case EOpcode::Add:
			case EOpcode::Sub:
			case EOpcode::Mul:
			case EOpcode::Div:
			case EOpcode::Mod:
				{
					if (!Need(2)) return false;
					if (!VmDetail::TryEvalScalarStackArithmetic(Op, Vm.OperandStack,
					        CompiledStackBase, Vm.MaxOperandStackDepth))
					{
						break;
					}
					return true;
				}
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
			case EOpcode::JmpIfLocalGeImm:
				{
					const auto Slot = Index();
					const auto& Locals = Vm.Frames.back().Locals;
					if (Slot < Locals.size() && std::holds_alternative<std::int32_t>(Locals[Slot]))
					{
						if (Op == EOpcode::JmpIfLocalGeImm)
						{
							Result = std::get<std::int32_t>(Locals[Slot]) >= Bytecode::ReadI32LE(Instruction + 3);
							return true;
						}
						if (Op != EOpcode::AddLocalLocal)
						{
							return Vm.JitAddLocalImm(Slot, Op == EOpcode::IncLocal ? 1 : Bytecode::ReadI32LE(Instruction + 3));
						}
						const auto Source = Bytecode::ReadU16LE(Instruction + 3);
						if (Source < Locals.size() && std::holds_alternative<std::int32_t>(Locals[Source]))
						{
							return Vm.JitAddLocalLocal(Slot, Source);
						}
					}
					break;
				}
			default:
				{
					break;
				}
			}
			struct FTemporaryValues
			{
				FVmJitRuntimeServices& Services;
				FValue A = std::int32_t{0}, B = std::int32_t{0}, Out = std::int32_t{0};
				~FTemporaryValues()
				{
					const auto Release = [&](FValue& Value)
					{
						// Scalars/owned text have no language destructor. Variant destruction
						// already releases their C++ storage without entering a VM service.
						if (!Runtime::IsNumericValue(Value) && !std::holds_alternative<bool>(Value) && !std::holds_alternative<std::string>(Value) && !std::holds_alternative<std::u16string>(Value))
						{
							Services.ReleaseValue(Value);
						}
					};
					Release(A);
					Release(B);
					Release(Out);
				}
			} Temps{*this};
			const auto PushResult = [&]()
			{
				Vm.Push(std::move(Temps.Out));
				Temps.Out = std::int32_t{0};
				return !HasError();
			};
			switch (Op)
			{
			case EOpcode::Nop:
				return true;
			case EOpcode::ConstI32:
			case EOpcode::ConstF64:
			case EOpcode::ConstStr:
				{
					if (Index() >= Vm.ActiveModule->Constants.size())
					{
						ReportError("invalid compiled constant");
						return false;
					}
					Temps.Out = Runtime::ConstantToValue(*Vm.ActiveModule, Vm.ActiveModule->Constants[Index()]);
					return PushResult();
				}
			case EOpcode::ConstBool:
				{
					Vm.Push(Instruction[1] != 0);
					return true;
				}
			case EOpcode::ConstNull:
				{
					Vm.Push(FObjectHandle{});
					return true;
				}
			case EOpcode::LoadLocal:
				return LoadLocal(FrameToken, Index(), Temps.Out) && PushResult();
			case EOpcode::LoadGlobal:
				return LoadGlobal(Index(), Temps.Out) && PushResult();
			case EOpcode::LoadThis:
				{
					if (!Vm.GetJitThisObject())
					{
						ReportError("this is only valid inside methods");
						return false;
					}
					Temps.Out = Vm.GetJitThisObject();
					return PushResult();
				}
			case EOpcode::LoadField:
			case EOpcode::LoadFieldKeep:
			case EOpcode::LoadThisField:
				{
					if (Op == EOpcode::LoadThisField)
					{
						Temps.A = Vm.GetJitThisObject();
					}
					else
					{
						if (!Need(1)) return false;
						Temps.A = Op == EOpcode::LoadFieldKeep ? Vm.OperandStack.back() : Vm.Pop();
					}
					return LoadField(Temps.A, Index(), Temps.Out) && PushResult();
				}
			case EOpcode::StoreField:
			case EOpcode::StoreThisField:
				{
					if (!Need(Op == EOpcode::StoreThisField ? 1 : 2)) return false;
					Temps.B = Vm.Pop();
					Temps.A = Op == EOpcode::StoreThisField ? FValue(Vm.GetJitThisObject()) : Vm.Pop();
					if (!StoreField(Temps.A, Index(), Temps.B)) return false;
					if (Op == EOpcode::StoreThisField) return true;
					Temps.Out = std::move(Temps.B);
					Temps.B = std::int32_t{0};
					return PushResult();
				}
			case EOpcode::IncField:
				{
					if (!Need(1)) return false;
					Temps.A = Vm.Pop();
					return IncrementField(Temps.A, Index());
				}
			case EOpcode::MakeLocalRef:
				return MakeLocalReference(FrameToken, Index(), Temps.Out) && PushResult();
			case EOpcode::MakeGlobalRef:
				return MakeGlobalReference(Index(), Temps.Out) && PushResult();
			case EOpcode::MakeThisFieldRef:
			case EOpcode::MakeFieldRef:
				{
					if (Op == EOpcode::MakeThisFieldRef)
					{
						Temps.A = Vm.GetJitThisObject();
					}
					else
					{
						if (!Need(1)) return false;
						Temps.A = Vm.Pop();
					}
					return MakeFieldReference(Temps.A, Index(), Temps.Out) && PushResult();
				}
			case EOpcode::CloneStruct:
				{
					if (!Need(1)) return false;
					Temps.Out = Vm.Pop();
					return CloneStruct(Temps.Out) && PushResult();
				}
			case EOpcode::StoreLocal:
			case EOpcode::StoreGlobal:
				{
					if (!Need(1)) return false;
					Temps.A = Vm.Pop();
					return Op == EOpcode::StoreLocal ? StoreLocal(FrameToken, Index(), Temps.A) : StoreGlobal(Index(), Temps.A);
				}
			case EOpcode::Dup:
				{
					if (!Need(1)) return false;
					Vm.Push(Vm.OperandStack.back());
					return true;
				}
			case EOpcode::Pop:
				{
					if (!Need(1)) return false;
					Temps.A = Vm.Pop();
					return true;
				}
			case EOpcode::Jmp:
				{
					Result = 1;
					return true;
				}
			case EOpcode::BeginLocalStatic:
				{
					bool Skip = false;
					if (!Vm.BeginLocalStatic(Index(), Skip, {}))
					{
						return false;
					}
					Result = Skip ? 1 : 0;
					return true;
				}
			case EOpcode::CommitLocalStatic:
				{
					if (!Need(1)) return false;
					return Vm.CommitLocalStatic(Index(), Vm.Pop(), {});
				}
			case EOpcode::JmpIfFalse:
				{
					if (!Need(1)) return false;
					Temps.A = Vm.Pop();
					if (!Vm.ExpectBool(Temps.A, {}))
					{
						return false;
					}
					Result = std::get<bool>(Temps.A) ? 0 : 1;
					return true;
				}
			case EOpcode::JmpIfLocalGeImm:
				{
					if (!LoadLocal(FrameToken, Index(), Temps.A)) return false;
					const auto Imm = Bytecode::ReadI32LE(Instruction + 3);
					if (const auto* I = std::get_if<std::int32_t>(&Temps.A))
					{
						Result = *I >= Imm;
					}
					else if (Runtime::IsUnsignedIntegerValue(Temps.A))
					{
						Result = Runtime::ToUnsignedInteger64(Temps.A) >= static_cast<std::uint64_t>(static_cast<std::uint32_t>(Imm));
					}
					else if (Runtime::IsNumericValue(Temps.A))
					{
						Result = !(Runtime::ToDouble(Temps.A) < static_cast<double>(Imm));
					}
					else
					{
						ReportError("JmpIfLocalGeImm requires numeric local");
						return false;
					}
					return true;
				}
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
				{
					if (!LoadLocal(FrameToken, Index(), Temps.A)) return false;
					if (Op == EOpcode::AddLocalLocal)
					{
						if (!LoadLocal(FrameToken, Bytecode::ReadU16LE(Instruction + 3), Temps.B) ||
						    !EvaluateArithmetic(EOpcode::Add, Temps.A, Temps.B, Temps.Out))
						{
							return false;
						}
					}
					else
					{
						const auto Imm = Op == EOpcode::IncLocal ? 1 : Bytecode::ReadI32LE(Instruction + 3);
						// Fused immediates preserve the resolved integer storage's width
						// and signedness, including reference parameters and captures.
						if (Runtime::IsIntegerValue(Temps.A))
						{
							Temps.Out = Temps.A;
							if (!Runtime::AddIntegerImmediate(Temps.Out, Imm))
							{
								ReportErrorId("diag.integer_overflow", {});
								return false;
							}
						}
						else if (!EvaluateArithmetic(EOpcode::Add, Temps.A, FValue{Imm}, Temps.Out))
						{
							return false;
						}
					}
					return StoreLocal(FrameToken, Index(), Temps.Out);
				}
			case EOpcode::Add:
			case EOpcode::Sub:
			case EOpcode::Mul:
			case EOpcode::Div:
			case EOpcode::Mod:
			case EOpcode::BitAnd:
			case EOpcode::BitOr:
			case EOpcode::BitXor:
			case EOpcode::Shl:
			case EOpcode::Shr:
			case EOpcode::Eq:
			case EOpcode::Ne:
			case EOpcode::Lt:
			case EOpcode::Le:
			case EOpcode::Gt:
			case EOpcode::Ge:
				{
					if (!Need(2)) return false;
					Temps.B = Vm.Pop();
					Temps.A = Vm.Pop();
					bool Ok = false;
					if (Op >= EOpcode::Eq && Op <= EOpcode::Ge)
					{
						bool Value = false;
						Ok = Vm.JitCompare(Op, Temps.A, Temps.B, Value);
						Temps.Out = Value;
					}
					else if (Op == EOpcode::BitAnd || Op == EOpcode::BitOr || Op == EOpcode::BitXor || Op == EOpcode::Shl || Op == EOpcode::Shr)
					{
						Ok = Vm.JitEvalBitwise(Op, Temps.A, Temps.B, Temps.Out);
					}
					else
					{
						Ok = Vm.JitEvalArithmetic(Op, Temps.A, Temps.B, Temps.Out);
					}
					return Ok && PushResult();
				}
			case EOpcode::Neg:
			case EOpcode::Not:
			case EOpcode::BitNot:
				{
					if (!Need(1)) return false;
					Temps.A = Vm.Pop();
					return Vm.JitEvalUnary(Op, Temps.A, Temps.Out) && PushResult();
				}
			case EOpcode::Call:
			case EOpcode::CallMethod:
			case EOpcode::CallBoundMethod:
			case EOpcode::CallBuiltin:
			case EOpcode::CallNative:
				{
					const auto Count = Op == EOpcode::CallBuiltin ? Instruction[2] : Instruction[3];
					if (!Need(Count + ((Op == EOpcode::CallMethod || Op == EOpcode::CallBoundMethod) ? 1u : 0u))) return false;
					// Allocate all temporary roots before removing any operand ownership.
					VmDetail::FArgumentBuffer Arguments(Count);
					FValue* const Args = Arguments.data();
					for (std::size_t I = Count; I != 0; --I)
					{
						Args[I - 1] = Vm.Pop();
					}
					bool HasResult = true;
					bool Called = false;
					if (Op == EOpcode::Call)
					{
						Called = InvokeFunction(Index(), {}, Args, Count, Temps.Out);
					}
					else if (Op == EOpcode::CallMethod)
					{
						Temps.A = Vm.Pop();
						Called = InvokeMethod(Index(), Temps.A, Args, Count, static_cast<std::uint32_t>(Offset), Temps.Out);
					}
					else if (Op == EOpcode::CallBoundMethod)
					{
						Temps.A = Vm.Pop();
						const auto* Receiver = std::get_if<FObjectHandle>(&Temps.A);
						if (Receiver && *Receiver)
						{
							Called = InvokeFunction(Index(), *Receiver, Args, Count, Temps.Out);
						}
						else
						{
							ReportError("bound method call requires object");
						}
					}
					else if (Op == EOpcode::CallNative)
					{
						Called = InvokeNative(Index(), Args, Count, Temps.Out);
					}
					else
					{
						Called = InvokeBuiltin(static_cast<EBuiltinId>(Instruction[1]), Args, Count, HasResult, Temps.Out);
					}
					const bool Released = Vm.ReleaseValueList(Args, Count, {});
					return Called && Released && (!HasResult || PushResult());
				}
			case EOpcode::Return:
				{
					if (!Need(1)) return false;
					Temps.A = Vm.Pop();
					if (!Function->bReturnsVoid)
					{
						if (!PrepareReturn(*Function, Temps.A)) return false;
						CompiledContext->ReturnValue = std::move(Temps.A);
						Temps.A = std::int32_t{0};
						CompiledContext->bHasReturn = true;
					}
					bCompiledReturned = true;
					Result = 2;
					return true;
				}
			default:
				{
					ReportError("unsupported compiled instruction");
					return false;
				}
			}
		}

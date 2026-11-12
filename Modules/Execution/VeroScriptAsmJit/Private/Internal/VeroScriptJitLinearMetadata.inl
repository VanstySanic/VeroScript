// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: scalar expression decoding and dispatch metadata. Included by the owning translation unit.

		bool JitTrampoline_Interpret(void* const VmContext)
		{
			return static_cast<FExecutionContext*>(VmContext)->JitInterpretCurrentFunction();
		}

		bool TryReadLinearScalarExpression(
			FExecutionContext& Vm,
			const FBytecodeModule& Module,
			const std::vector<std::uint8_t>& Code,
			std::size_t ExpressionIp,
			FValue& OutValue,
			bool& bOutAvailable,
			std::size_t& OutEnd,
			bool bAllowNestedContainerReads = false,
			std::size_t ExpressionLimit = std::numeric_limits<std::size_t>::max());

		bool TryReadLinearScalarOperand(
			FExecutionContext& Vm,
			const FBytecodeModule& Module,
			const std::vector<std::uint8_t>& Code,
			const std::size_t ScalarIp,
			FValue& OutValue,
			bool& bOutAvailable,
			std::size_t& OutEnd,
			const bool bAllowNestedContainerReads = false,
			const std::size_t ExpressionLimit = std::numeric_limits<std::size_t>::max())
		{
			bOutAvailable = false;
			OutEnd = ScalarIp;
			const std::size_t Limit = std::min(ExpressionLimit, Code.size());
			if (ScalarIp >= Limit)
			{
				return true;
			}
			const EOpcode ScalarOp = static_cast<EOpcode>(Code[ScalarIp]);
			if (ScalarOp != EOpcode::ConstBool && ScalarOp != EOpcode::ConstI32
				&& ScalarOp != EOpcode::ConstF64 && ScalarOp != EOpcode::ConstStr
				&& ScalarOp != EOpcode::LoadLocal
				&& ScalarOp != EOpcode::LoadGlobal)
			{
				return true;
			}
			OutEnd = ScalarIp + Bytecode::InstructionSize(ScalarOp);
			if (OutEnd > Limit)
			{
				return true;
			}
			if (ScalarOp == EOpcode::ConstBool)
			{
				OutValue = Code[ScalarIp + 1] != 0;
				bOutAvailable = true;
				return true;
			}
			if (ScalarOp == EOpcode::ConstStr)
			{
				const std::uint16_t ConstantIndex =
					Bytecode::ReadU16LE(Code.data() + ScalarIp + 1);
				if (ConstantIndex >= Module.Constants.size())
				{
					return true;
				}
				const auto& Constant = Module.Constants[ConstantIndex];
				if (Constant.Kind != EConstantKind::Str
					&& Constant.Kind != EConstantKind::WStr)
				{
					return true;
				}
				OutValue = JitHost::Services().ConstantToValue(Module, Constant);
				bOutAvailable = true;
				return true;
			}
			if (ScalarOp == EOpcode::LoadLocal)
			{
				if (!Vm.JitTryLoadScalarLocal(
					Bytecode::ReadU16LE(Code.data() + ScalarIp + 1),
					OutValue, bOutAvailable))
				{
					return false;
				}
				if (bOutAvailable)
				{
					return true;
				}
				if (!bAllowNestedContainerReads)
				{
					OutEnd = ScalarIp;
					return true;
				}

				// Treat a rooted local container read as one scalar operand.  The index is
				// itself decoded by this expression reader, so arbitrary arithmetic and
				// nested reads such as values[indices[(state + i) % n]] share the same
				// typed path instead of falling back at the first non-canonical index.
				// This probe is deliberately read-only: method calls and host properties
				// are not speculatively executed while a larger fast-path shape is tested.
				constexpr std::size_t MaxNestedScalarReadDepth = 16;
				thread_local std::size_t NestedScalarReadDepth = 0;
				if (NestedScalarReadDepth >= MaxNestedScalarReadDepth)
				{
					OutEnd = ScalarIp;
					return true;
				}
				struct FDepthGuard
				{
					explicit FDepthGuard(std::size_t& InDepth) : Depth(InDepth)
					{
						++Depth;
					}

					~FDepthGuard()
					{
						--Depth;
					}

					std::size_t& Depth;

				} DepthGuard(NestedScalarReadDepth);

				const std::uint16_t ContainerSlot =
					Bytecode::ReadU16LE(Code.data() + ScalarIp + 1);
				const std::size_t OperandIp = OutEnd;
				if (OperandIp >= Limit)
				{
					OutEnd = ScalarIp;
					return true;
				}

				// Stable script-object fields are direct slots, not user-defined getters.
				// Borrowing them is therefore side-effect free and preserves object lifetime
				// through the rooted local for the duration of this expression evaluation.
				if (static_cast<EOpcode>(Code[OperandIp]) == EOpcode::LoadField
					&& OperandIp + Bytecode::InstructionSize(EOpcode::LoadField)
						<= Limit)
				{
					FScriptObject* Object = nullptr;
					std::uint16_t FieldIndex = 0xFFFF;
					const FValue* Storage = nullptr;
					if (!Vm.JitBorrowLocalScalarField(
							ContainerSlot,
							Bytecode::ReadU16LE(Code.data() + OperandIp + 1),
							Object, FieldIndex, Storage))
					{
						return false;
					}
					if (Object && Storage)
					{
						OutValue = *Storage;
						bOutAvailable = true;
						OutEnd = OperandIp
							+ Bytecode::InstructionSize(EOpcode::LoadField);
						return true;
					}
				}

				FValue IndexValue;
				bool bIndexAvailable = false;
				std::size_t IndexEnd = OperandIp;
				if (!TryReadLinearScalarExpression(
						Vm, Module, Code, OperandIp,
						IndexValue, bIndexAvailable, IndexEnd, true, Limit))
				{
					return false;
				}
				if (bIndexAvailable && IndexEnd < Limit
					&& static_cast<EOpcode>(Code[IndexEnd]) == EOpcode::GetIndexCopy)
				{
					FValue Loaded;
					bool bMapHandled = false;
					bool bArrayHandled = false;
					// A nested typed read reports from its own GetIndexCopy rather than
					// inheriting the outer container LoadLocal location.
					Vm.JitRefineRuntimeErrorLocation(IndexEnd);
					if (!Vm.JitTryGetScalarIndexOnLocal(
							ContainerSlot, IndexValue, Loaded,
							bMapHandled, bArrayHandled))
					{
						return false;
					}
					Vm.JitRefineRuntimeErrorLocation(ScalarIp);
					if (bMapHandled || bArrayHandled)
					{
						if (bMapHandled)
						{
							Vm.RecordJitScalarMapFastPathGet();
						}
						if (bArrayHandled)
						{
							Vm.RecordJitArrayFastPathGet();
						}
						OutValue = std::move(Loaded);
						bOutAvailable = true;
						OutEnd = IndexEnd
							+ Bytecode::InstructionSize(EOpcode::GetIndexCopy);
						return true;
					}
				}
				OutEnd = ScalarIp;
				return true;
			}
			if (ScalarOp == EOpcode::LoadGlobal)
			{
				if (!Vm.JitTryLoadScalarGlobal(
						Bytecode::ReadU16LE(Code.data() + ScalarIp + 1),
						OutValue, bOutAvailable))
				{
					return false;
				}
				if (!bOutAvailable)
				{
					OutEnd = ScalarIp;
				}
				return true;
			}
			const std::uint16_t ConstantIndex =
				Bytecode::ReadU16LE(Code.data() + ScalarIp + 1);
			if (ConstantIndex >= Module.Constants.size())
			{
				return true;
			}
			const auto& Constant = Module.Constants[ConstantIndex];
			switch (Constant.Kind)
			{
			case EConstantKind::I8:
				{
					OutValue = Constant.I8;
					break;
				}
			case EConstantKind::U8:
				{
					OutValue = Constant.U8;
					break;
				}
			case EConstantKind::I16:
				{
					OutValue = Constant.I16;
					break;
				}
			case EConstantKind::U16:
				{
					OutValue = Constant.U16;
					break;
				}
			case EConstantKind::I32:
				{
					OutValue = Constant.I32;
					break;
				}
			case EConstantKind::U32:
				{
					OutValue = Constant.U32;
					break;
				}
			case EConstantKind::I64:
				{
					OutValue = Constant.I64;
					break;
				}
			case EConstantKind::U64:
				{
					OutValue = Constant.U64;
					break;
				}
			case EConstantKind::F32:
				{
					OutValue = Constant.F32;
					break;
				}
			case EConstantKind::F64:
				{
					OutValue = Constant.F64;
					break;
				}
			default:
				return true;
			}
			bOutAvailable = true;
			return true;
		}

		bool TryReadLinearScalarExpression(
			FExecutionContext& Vm,
			const FBytecodeModule& Module,
			const std::vector<std::uint8_t>& Code,
			const std::size_t ExpressionIp,
			FValue& OutValue,
			bool& bOutAvailable,
			std::size_t& OutEnd,
			const bool bAllowNestedContainerReads,
			const std::size_t ExpressionLimit)
		{
			const std::size_t Limit = std::min(ExpressionLimit, Code.size());
			if (!TryReadLinearScalarOperand(
					Vm, Module, Code, ExpressionIp,
					OutValue, bOutAvailable, OutEnd,
					bAllowNestedContainerReads, Limit)
				|| !bOutAvailable || OutEnd >= Limit)
			{
				return !Vm.HasError();
			}
			const auto ApplyCheckedCasts = [&] (
				FValue& Value,
				std::size_t& End,
				bool& bSupported) -> bool
			{
				bSupported = true;
				while (End < Limit
					&& static_cast<EOpcode>(Code[End]) == EOpcode::CheckedCast)
				{
					const std::size_t CastSize =
						Bytecode::InstructionSize(EOpcode::CheckedCast);
					if (CastSize > Limit - End)
					{
						bSupported = false;
						return true;
					}
					FValue Converted;
					bool bHandled = false;
					if (!Vm.JitTryCheckedScalarCast(
							Value,
							Bytecode::ReadU16LE(Code.data() + End + 1),
							Converted,
							bHandled))
					{
						return false;
					}
					if (!bHandled)
					{
						bSupported = false;
						return true;
					}
					Value = std::move(Converted);
					End += CastSize;
				}
				return true;
			};
			const auto ApplyScalarConversions = [&] (
				FValue& Value,
				std::size_t& End,
				bool& bSupported) -> bool
			{
				if (!ApplyCheckedCasts(Value, End, bSupported) || !bSupported)
				{
					return !Vm.HasError();
				}
				while (End + Bytecode::InstructionSize(EOpcode::CallBuiltin)
						<= Limit
					&& static_cast<EOpcode>(Code[End]) == EOpcode::CallBuiltin
					&& Code[End + 2] == 1)
				{
					FValue Converted;
					bool bHandled = false;
					if (!Vm.JitTryScalarBuiltinConversion(
							static_cast<EBuiltinId>(Code[End + 1]),
							Value, Converted, bHandled))
					{
						return false;
					}
					if (!bHandled)
					{
						bSupported = false;
						return true;
					}
					Value = std::move(Converted);
					End += Bytecode::InstructionSize(EOpcode::CallBuiltin);
				}
				return true;
			};
			bool bCastSupported = true;
			if (!ApplyScalarConversions(OutValue, OutEnd, bCastSupported))
			{
				return false;
			}
			if (!bCastSupported)
			{
				bOutAvailable = false;
				return true;
			}

			// Consume a bounded left-associative scalar RPN chain. This covers normal
			// compiler output such as `x * a + b`, `(x & mask)` and checked primitive
			// casts without turning this helper into a second general interpreter.
			// The bound prevents malformed bytecode from making an analysis probe
			// unbounded; longer expressions retain the ordinary opcode path.
			constexpr std::size_t MaxOperations = 16;
			for (std::size_t OperationCount = 0;
				OperationCount < MaxOperations && OutEnd < Limit;
				++OperationCount)
			{
				FValue Right;
				bool bRightAvailable = false;
				std::size_t RightEnd = OutEnd;
				if (!TryReadLinearScalarOperand(
						Vm, Module, Code, OutEnd,
						Right, bRightAvailable, RightEnd,
						bAllowNestedContainerReads, Limit))
				{
					return false;
				}
				if (!bRightAvailable)
				{
					return true;
				}
				if (!ApplyScalarConversions(Right, RightEnd, bCastSupported))
				{
					return false;
				}
				if (!bCastSupported || RightEnd >= Limit)
				{
					return true;
				}
				const EOpcode Operation = static_cast<EOpcode>(Code[RightEnd]);
				const bool bArithmetic = Operation == EOpcode::Add
					|| Operation == EOpcode::Sub || Operation == EOpcode::Mul
					|| Operation == EOpcode::Div || Operation == EOpcode::Mod;
				const bool bBitwise = Operation == EOpcode::BitAnd
					|| Operation == EOpcode::BitOr || Operation == EOpcode::BitXor
					|| Operation == EOpcode::Shl || Operation == EOpcode::Shr;
				if (!bArithmetic && !bBitwise)
				{
					return true;
				}

				FValue Result;
				bool bDirect = false;
				if (bArithmetic)
				{
					// Preserve the allocation-free common I32 case while using the shared
					// evaluator for every other exact numeric alternative.
					if (const auto* const LeftI32 =
							std::get_if<std::int32_t>(&OutValue))
					{
						if (const auto* const RightI32 =
								std::get_if<std::int32_t>(&Right))
						{
							std::int32_t DirectResult = 0;
							switch (Operation)
							{
							case EOpcode::Add:
								{
									bDirect = Runtime::CheckedAdd(
										*LeftI32, *RightI32, DirectResult);
									break;
								}
							case EOpcode::Sub:
								{
									bDirect = Runtime::CheckedSub(
										*LeftI32, *RightI32, DirectResult);
									break;
								}
							case EOpcode::Mul:
								{
									bDirect = Runtime::CheckedMul(
										*LeftI32, *RightI32, DirectResult);
									break;
								}
							case EOpcode::Div:
							case EOpcode::Mod:
								{
									if (*RightI32 != 0
										&& !(*LeftI32
											== std::numeric_limits<std::int32_t>::min()
											&& *RightI32 == -1))
									{
										DirectResult = Operation == EOpcode::Div
											? *LeftI32 / *RightI32
											: *LeftI32 % *RightI32;
										bDirect = true;
									}
									break;
								}
							default:
								{
									break;
								}
							}
							if (bDirect)
							{
								Result = DirectResult;
							}
						}
					}
					if (!bDirect
						&& !Vm.JitEvalArithmetic(
							Operation, OutValue, Right, Result))
					{
						return false;
					}
				}
				else if (!Vm.JitEvalBitwise(Operation, OutValue, Right, Result))
				{
					return false;
				}
				OutValue = std::move(Result);
				OutEnd = RightEnd + Bytecode::InstructionSize(Operation);
				if (!ApplyScalarConversions(OutValue, OutEnd, bCastSupported))
				{
					return false;
				}
				if (!bCastSupported)
				{
					bOutAvailable = false;
					return true;
				}
			}
			return true;
		}

		/**
		 * Recognize the compiler's value-discarded
		 * `map[key] = map[key] op scalar` sequence without evaluating either map
		 * read while probing the shape.  This must run before the broader scalar
		 * expression fusions: treating the first key local as the beginning of an
		 * arbitrary RPN expression can otherwise speculatively consume the following
		 * map read several times before the typed update path is reached.
		 */
		bool TryApplyScalarMapCompoundUpdate(
			FExecutionContext& Vm,
			const FBytecodeModule& Module,
			const std::vector<std::uint8_t>& Code,
			const std::uint16_t MapSlot,
			const std::size_t KeyIp,
			std::size_t& OutEnd,
			bool& bOutHandled,
			std::size_t& OutFailureIp)
		{
			bOutHandled = false;
			OutEnd = KeyIp;
			OutFailureIp = KeyIp;
			if (KeyIp >= Code.size())
			{
				return true;
			}

			const std::size_t LoadSize =
				Bytecode::InstructionSize(EOpcode::LoadLocal);
			const std::size_t GetSize =
				Bytecode::InstructionSize(EOpcode::GetIndexCopy);
			// Locate the compiler-emitted second map load structurally. The bytecode
			// duplicates the complete, pure key expression on both sides of `=`, so
			// byte equality supplies an unambiguous boundary even when the key contains
			// nested Array/Map reads. No expression is executed while candidate
			// boundaries are probed.
			std::size_t KeyEnd = KeyIp;
			std::size_t RepeatedKeyEnd = KeyIp;
			for (std::size_t Candidate = KeyIp; Candidate < Code.size();)
			{
				const EOpcode CandidateOp = static_cast<EOpcode>(Code[Candidate]);
				const std::size_t CandidateSize = Bytecode::InstructionSize(CandidateOp);
				if (CandidateSize == 0 || CandidateSize > Code.size() - Candidate)
				{
					return true;
				}
				if (Candidate > KeyIp && CandidateOp == EOpcode::LoadLocal
					&& Bytecode::ReadU16LE(Code.data() + Candidate + 1) == MapSlot)
				{
					const std::size_t ExpressionSize = Candidate - KeyIp;
					const std::size_t Repeated = Candidate + LoadSize;
					if (ExpressionSize <= Code.size() - Repeated
						&& std::equal(
							Code.begin() + static_cast<std::ptrdiff_t>(KeyIp),
							Code.begin() + static_cast<std::ptrdiff_t>(Candidate),
							Code.begin() + static_cast<std::ptrdiff_t>(Repeated)))
					{
						const std::size_t CandidateRepeatedEnd = Repeated + ExpressionSize;
						if (GetSize <= Code.size() - CandidateRepeatedEnd
							&& static_cast<EOpcode>(Code[CandidateRepeatedEnd])
								== EOpcode::GetIndexCopy)
						{
							KeyEnd = Candidate;
							RepeatedKeyEnd = CandidateRepeatedEnd;
							break;
						}
					}
				}
				Candidate += CandidateSize;
			}
			if (KeyEnd == KeyIp)
			{
				return true;
			}

			FValue KeyValue;
			bool bKeyAvailable = false;
			std::size_t EvaluatedKeyEnd = KeyIp;
			if (!TryReadLinearScalarExpression(
					Vm, Module, Code, KeyIp,
					KeyValue, bKeyAvailable, EvaluatedKeyEnd, true, KeyEnd))
			{
				return false;
			}
			if (!bKeyAvailable || EvaluatedKeyEnd != KeyEnd)
			{
				return true;
			}

			const std::size_t GetIp = RepeatedKeyEnd;
			const std::size_t OperandIp = GetIp + GetSize;
			FValue Operand;
			bool bOperandAvailable = false;
			std::size_t OperandEnd = OperandIp;
			// Keep the operand to one pure scalar load/constant. Evaluating a larger
			// expression while recognizing this shape could report (for example) its
			// division-by-zero before the source map read reports a missing key, which
			// would violate the bytecode evaluation order.
			if (!TryReadLinearScalarOperand(
					Vm, Module, Code, OperandIp,
					Operand, bOperandAvailable, OperandEnd))
			{
				OutFailureIp = OperandIp;
				return false;
			}
			if (!bOperandAvailable || OperandEnd >= Code.size())
			{
				return true;
			}

			const EOpcode Operation = static_cast<EOpcode>(Code[OperandEnd]);
			const bool bSupportedOperation = Operation == EOpcode::Add
				|| Operation == EOpcode::Sub || Operation == EOpcode::Mul
				|| Operation == EOpcode::Div || Operation == EOpcode::Mod
				|| Operation == EOpcode::BitAnd || Operation == EOpcode::BitOr
				|| Operation == EOpcode::BitXor || Operation == EOpcode::Shl
				|| Operation == EOpcode::Shr;
			if (!bSupportedOperation)
			{
				return true;
			}

			const std::size_t TailIp =
				OperandEnd + Bytecode::InstructionSize(Operation);
			constexpr std::size_t AssignmentTailSize = 14;
			if (TailIp + AssignmentTailSize > Code.size()
				|| static_cast<EOpcode>(Code[TailIp]) != EOpcode::StoreLocal
				|| static_cast<EOpcode>(Code[TailIp + 3]) != EOpcode::LoadLocal
				|| Bytecode::ReadU16LE(Code.data() + TailIp + 1)
					!= Bytecode::ReadU16LE(Code.data() + TailIp + 4)
				|| static_cast<EOpcode>(Code[TailIp + 6]) != EOpcode::SetIndex
				|| static_cast<EOpcode>(Code[TailIp + 7]) != EOpcode::StoreLocal
				|| Bytecode::ReadU16LE(Code.data() + TailIp + 8) != MapSlot
				|| static_cast<EOpcode>(Code[TailIp + 10]) != EOpcode::LoadLocal
				|| Bytecode::ReadU16LE(Code.data() + TailIp + 11)
					!= Bytecode::ReadU16LE(Code.data() + TailIp + 4)
				|| static_cast<EOpcode>(Code[TailIp + 13]) != EOpcode::Pop)
			{
				return true;
			}

			EJitScalarMapUpdateFailure Failure = EJitScalarMapUpdateFailure::None;
			// Generic arithmetic helpers report synchronously. Point them at the
			// operation before entering the VM bridge; structured raw-map failures are
			// reported below after selecting their precise read/operation location.
			Vm.JitRefineRuntimeErrorLocation(OperandIp);
			if (!Vm.JitTryUpdateScalarMapEntryOnLocal(
					MapSlot, KeyValue, Operation, Operand, bOutHandled, Failure))
			{
				OutFailureIp = Failure == EJitScalarMapUpdateFailure::MissingKey
					? GetIp
					: OperandIp;
				if (!Vm.HasError())
				{
					Vm.JitRefineRuntimeErrorLocation(OutFailureIp);
					switch (Failure)
					{
					case EJitScalarMapUpdateFailure::MissingKey:
						{
							Vm.JitReportRuntimeError("map key not found");
							break;
						}
					case EJitScalarMapUpdateFailure::IntegerOverflow:
						{
							Vm.JitReportRuntimeError("integer overflow");
							break;
						}
					case EJitScalarMapUpdateFailure::DivisionByZero:
						{
							Vm.JitReportRuntimeError("division by zero");
							break;
						}
					case EJitScalarMapUpdateFailure::ShiftOutOfRange:
						{
							Vm.JitReportRuntimeError("shift count out of range");
							break;
						}
					default:
						{
							Vm.JitReportRuntimeError("map update failed");
							break;
						}
					}
				}
				return false;
			}
			Vm.JitRefineRuntimeErrorLocation(KeyIp);
			if (bOutHandled)
			{
				OutEnd = TailIp + AssignmentTailSize;
			}
			return true;
		}

		bool TryAddExactScalar(
			FValue& InOutValue,
			const FValue& Addend,
			EJitScalarValueKind& OutKind,
			bool& bOutOverflow)
		{
			OutKind = EJitScalarValueKind::None;
			bOutOverflow = false;
			if (auto* const Left = std::get_if<std::int32_t>(&InOutValue))
			{
				const auto* const Right = std::get_if<std::int32_t>(&Addend);
				if (!Right)
				{
					return false;
				}
				std::int32_t Result = 0;
				bOutOverflow = !Runtime::CheckedAdd(*Left, *Right, Result);
				if (!bOutOverflow)
				{
					*Left = Result;
				}
				OutKind = EJitScalarValueKind::I32;
				return true;
			}
			if (auto* const Left = std::get_if<std::uint32_t>(&InOutValue))
			{
				const auto* const Right = std::get_if<std::uint32_t>(&Addend);
				if (!Right)
				{
					return false;
				}
				*Left = static_cast<std::uint32_t>(*Left + *Right);
				OutKind = EJitScalarValueKind::U32;
				return true;
			}
			if (auto* const Left = std::get_if<std::int64_t>(&InOutValue))
			{
				const auto* const Right = std::get_if<std::int64_t>(&Addend);
				if (!Right)
				{
					return false;
				}
				bOutOverflow = (*Right > 0
						&& *Left > std::numeric_limits<std::int64_t>::max() - *Right)
					|| (*Right < 0
						&& *Left < std::numeric_limits<std::int64_t>::min() - *Right);
				if (!bOutOverflow)
				{
					*Left += *Right;
				}
				OutKind = EJitScalarValueKind::I64;
				return true;
			}
			if (auto* const Left = std::get_if<std::uint64_t>(&InOutValue))
			{
				const auto* const Right = std::get_if<std::uint64_t>(&Addend);
				if (!Right)
				{
					return false;
				}
				*Left += *Right;
				OutKind = EJitScalarValueKind::U64;
				return true;
			}
			if (auto* const Left = std::get_if<float>(&InOutValue))
			{
				const auto* const Right = std::get_if<float>(&Addend);
				if (!Right)
				{
					return false;
				}
				*Left = static_cast<float>(*Left + *Right);
				OutKind = EJitScalarValueKind::F32;
				return true;
			}
			if (auto* const Left = std::get_if<double>(&InOutValue))
			{
				const auto* const Right = std::get_if<double>(&Addend);
				if (!Right)
				{
					return false;
				}
				*Left += *Right;
				OutKind = EJitScalarValueKind::F64;
				return true;
			}
			return false;
		}

		// Apply an exact repeated integer addition without changing the language's
		// checked-signed / wrapping-unsigned arithmetic semantics. The result is
		// committed only when every source-order addition is known to succeed. This
		// lets callers retain their ordinary per-iteration path on overflow, where
		// the precise failing counter and partially updated state remain observable.
		bool TryAddExactIntegerRepeated(
			FValue& InOutValue,
			const FValue& Addend,
			const std::size_t Count,
			EJitScalarValueKind& OutKind,
			bool& bOutWouldOverflow)
		{
			OutKind = EJitScalarValueKind::None;
			bOutWouldOverflow = false;
			if (auto* const Left = std::get_if<std::int32_t>(&InOutValue))
			{
				const auto* const Right = std::get_if<std::int32_t>(&Addend);
				if (!Right)
				{
					return false;
				}
				OutKind = EJitScalarValueKind::I32;
				const std::int64_t WideLeft = *Left;
				const std::int64_t WideRight = *Right;
				if ((WideRight > 0
						&& Count > static_cast<std::uint64_t>(
							(std::numeric_limits<std::int32_t>::max() - WideLeft) / WideRight))
					|| (WideRight < 0
						&& Count > static_cast<std::uint64_t>(
							(WideLeft - std::numeric_limits<std::int32_t>::min()) / -WideRight)))
				{
					bOutWouldOverflow = true;
					return true;
				}
				*Left = static_cast<std::int32_t>(
					WideLeft + WideRight * static_cast<std::int64_t>(Count));
				return true;
			}
			if (auto* const Left = std::get_if<std::uint32_t>(&InOutValue))
			{
				const auto* const Right = std::get_if<std::uint32_t>(&Addend);
				if (!Right)
				{
					return false;
				}
				OutKind = EJitScalarValueKind::U32;
				*Left = static_cast<std::uint32_t>(
					*Left + *Right * static_cast<std::uint32_t>(Count));
				return true;
			}
			if (auto* const Left = std::get_if<std::int64_t>(&InOutValue))
			{
				const auto* const Right = std::get_if<std::int64_t>(&Addend);
				if (!Right)
				{
					return false;
				}
				OutKind = EJitScalarValueKind::I64;
				if (*Right == 0 || Count == 0)
				{
					return true;
				}
				const std::uint64_t Magnitude = *Right > 0
					? static_cast<std::uint64_t>(*Right)
					: static_cast<std::uint64_t>(-(*Right + 1)) + 1u;
				const std::uint64_t Available = *Right > 0
					? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
						- static_cast<std::uint64_t>(*Left)
					: static_cast<std::uint64_t>(*Left)
						- static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::min());
				if (Count > Available / Magnitude)
				{
					bOutWouldOverflow = true;
					return true;
				}
				const std::uint64_t Delta = Magnitude * static_cast<std::uint64_t>(Count);
				// Reconstruct the signed result arithmetically instead of relying on the
				// host's signed-integer representation. The range proof above guarantees
				// that each branch stays within int64_t, including INT64_MIN.
				if (*Right > 0)
				{
					if (*Left >= 0)
					{
						*Left += static_cast<std::int64_t>(Delta);
					}
					else
					{
						const std::uint64_t LeftMagnitude =
							static_cast<std::uint64_t>(-(*Left + 1)) + 1u;
						if (Delta >= LeftMagnitude)
						{
							*Left = static_cast<std::int64_t>(Delta - LeftMagnitude);
						}
						else
						{
							const std::uint64_t ResultMagnitude = LeftMagnitude - Delta;
							*Left = -static_cast<std::int64_t>(ResultMagnitude - 1u) - 1;
						}
					}
				}
				else if (*Left <= 0)
				{
					const std::uint64_t LeftMagnitude =
						static_cast<std::uint64_t>(-(*Left + 1)) + 1u;
					const std::uint64_t ResultMagnitude = LeftMagnitude + Delta;
					*Left = -static_cast<std::int64_t>(ResultMagnitude - 1u) - 1;
				}
				else if (Delta <= static_cast<std::uint64_t>(*Left))
				{
					*Left = static_cast<std::int64_t>(
						static_cast<std::uint64_t>(*Left) - Delta);
				}
				else
				{
					const std::uint64_t ResultMagnitude =
						Delta - static_cast<std::uint64_t>(*Left);
					*Left = -static_cast<std::int64_t>(ResultMagnitude - 1u) - 1;
				}
				return true;
			}
			if (auto* const Left = std::get_if<std::uint64_t>(&InOutValue))
			{
				const auto* const Right = std::get_if<std::uint64_t>(&Addend);
				if (!Right)
				{
					return false;
				}
				OutKind = EJitScalarValueKind::U64;
				*Left += *Right * static_cast<std::uint64_t>(Count);
				return true;
			}
			return false;
		}

		bool TryResolveI32ConstGlobal(
			const FBytecodeModule& Module,
			const std::uint16_t GlobalSlot,
			std::int32_t& OutValue)
		{
			if (GlobalSlot >= Module.GlobalMetadata.size()
				|| !Module.GlobalMetadata[GlobalSlot].bConst)
			{
				return false;
			}
			for (const FBytecodeGlobalInitRange& Range : Module.GlobalInitRanges)
			{
				if (Range.GlobalSlot != GlobalSlot
					|| Range.BeginOffset > Range.EndOffset
					|| Range.EndOffset > Module.InitCode.size())
				{
					continue;
				}
				std::size_t Ip = Range.BeginOffset;
				while (Ip < Range.EndOffset
					&& static_cast<EOpcode>(Module.InitCode[Ip]) == EOpcode::Nop)
				{
					++Ip;
				}
				if (Ip + Bytecode::InstructionSize(EOpcode::ConstI32)
						+ Bytecode::InstructionSize(EOpcode::StoreGlobal)
						!= Range.EndOffset
					|| static_cast<EOpcode>(Module.InitCode[Ip]) != EOpcode::ConstI32)
				{
					continue;
				}
				const std::uint16_t ConstantIndex =
					Bytecode::ReadU16LE(Module.InitCode.data() + Ip + 1);
				Ip += Bytecode::InstructionSize(EOpcode::ConstI32);
				if (static_cast<EOpcode>(Module.InitCode[Ip]) != EOpcode::StoreGlobal
					|| Bytecode::ReadU16LE(Module.InitCode.data() + Ip + 1) != GlobalSlot
					|| ConstantIndex >= Module.Constants.size()
					|| Module.Constants[ConstantIndex].Kind != EConstantKind::I32)
				{
					continue;
				}
				OutValue = Module.Constants[ConstantIndex].I32;
				return true;
			}
			return false;
		}

		bool TryResolveI32InvariantOperand(
			const FBytecodeModule& Module,
			const std::vector<std::uint8_t>& Code,
			const std::size_t Ip,
			std::int32_t& OutValue)
		{
			if (Ip + 3u > Code.size())
			{
				return false;
			}
			const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
			const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (Op == EOpcode::LoadGlobal)
			{
				return TryResolveI32ConstGlobal(Module, Index, OutValue);
			}
			if (Op != EOpcode::ConstI32 || Index >= Module.Constants.size()
				|| Module.Constants[Index].Kind != EConstantKind::I32)
			{
				return false;
			}
			OutValue = Module.Constants[Index].I32;
			return true;
		}

		void CollectResolvedThisFieldMetadata(
			const FBytecodeModule& Module,
			const FBytecodeFunction& Function,
			FJitFunctionEntry& OutEntry)
		{
			if (!Function.bIsMethod || Function.bReturnsRef || Function.bIsAbstract
				|| Function.bDeleted || BytecodeFunctionHasRefParams(Function)
				|| !Function.ExceptionRegions.empty()
				|| Function.LocalCount < Function.ParamCount
				|| Function.OwnerTypeIndex >= Module.Types.size())
			{
				return;
			}
			const auto& Code = Function.Code;
			const auto ResolveOwnerFieldIndex = [&] (
				const std::uint16_t NameIndex,
				std::uint16_t& OutFieldIndex) -> bool
			{
				OutFieldIndex = 0xFFFF;
				if (NameIndex >= Module.StringPool.size())
				{
					return false;
				}
				const FBytecodeTypeDesc& Owner = Module.Types[Function.OwnerTypeIndex];
				const std::string& Name = Module.StringPool[NameIndex];
				for (std::size_t FieldIndex = 0; FieldIndex < Owner.Fields.size(); ++FieldIndex)
				{
					if (Owner.Fields[FieldIndex].Name == Name
						&& FieldIndex <= std::numeric_limits<std::uint16_t>::max())
					{
						OutFieldIndex = static_cast<std::uint16_t>(FieldIndex);
						return true;
					}
				}
				return false;
			};
			const auto MatchDirectParameterFieldAdd = [&] (
				const FBytecodeFunction& Candidate,
				std::uint16_t& OutNameIndex) -> bool
			{
				OutNameIndex = 0xFFFF;
				const auto& CandidateCode = Candidate.Code;
				if (!Candidate.bIsMethod || !Candidate.bReturnsVoid
					|| Candidate.bReturnsRef || Candidate.bIsVirtual
					|| Candidate.bIsAbstract || Candidate.bDeleted
					|| BytecodeFunctionHasRefParams(Candidate)
					|| !Candidate.ExceptionRegions.empty()
					|| Candidate.ParamCount != 1 || Candidate.LocalCount != 1
					|| Candidate.OwnerTypeIndex != Function.OwnerTypeIndex
					|| CandidateCode.size() != 18
					|| static_cast<EOpcode>(CandidateCode[0]) != EOpcode::LoadThisField
					|| static_cast<EOpcode>(CandidateCode[3]) != EOpcode::LoadLocal
					|| Bytecode::ReadU16LE(CandidateCode.data() + 4) != 0
					|| static_cast<EOpcode>(CandidateCode[6]) != EOpcode::Add
					|| static_cast<EOpcode>(CandidateCode[7]) != EOpcode::StoreThisField
					|| static_cast<EOpcode>(CandidateCode[10]) != EOpcode::LoadThisField
					|| static_cast<EOpcode>(CandidateCode[13]) != EOpcode::Pop
					|| static_cast<EOpcode>(CandidateCode[14]) != EOpcode::ConstI32
					|| static_cast<EOpcode>(CandidateCode[17]) != EOpcode::Return)
				{
					return false;
				}
				const std::uint16_t NameIndex =
					Bytecode::ReadU16LE(CandidateCode.data() + 1);
				const std::uint16_t SentinelIndex =
					Bytecode::ReadU16LE(CandidateCode.data() + 15);
				if (NameIndex != Bytecode::ReadU16LE(CandidateCode.data() + 8)
					|| NameIndex != Bytecode::ReadU16LE(CandidateCode.data() + 11)
					|| SentinelIndex >= Module.Constants.size()
					|| Module.Constants[SentinelIndex].Kind != EConstantKind::I32
					|| Module.Constants[SentinelIndex].I32 != 0)
				{
					return false;
				}
				OutNameIndex = NameIndex;
				return true;
			};
			// A nullary wrapper such as `bump() { this.add(1); }` is semantically
			// equivalent to its exact, statically bound one-parameter field leaf. Keep
			// this summary deliberately structural: arbitrary calls, virtual dispatch,
			// locals, exceptions, refs and extra side effects cannot enter it.
			if (Function.bReturnsVoid && !Function.bReturnsRef
				&& !Function.bIsVirtual && !Function.bIsAbstract && !Function.bDeleted
				&& !BytecodeFunctionHasRefParams(Function)
				&& Function.ExceptionRegions.empty()
				&& Function.ParamCount == 0 && Function.LocalCount == 0 && Code.size() == 13
				&& static_cast<EOpcode>(Code[0]) == EOpcode::LoadThis
				&& static_cast<EOpcode>(Code[1]) == EOpcode::ConstI32
				&& static_cast<EOpcode>(Code[4]) == EOpcode::CallBoundMethod
				&& Code[7] == 1
				&& static_cast<EOpcode>(Code[8]) == EOpcode::Pop
				&& static_cast<EOpcode>(Code[9]) == EOpcode::ConstI32
				&& static_cast<EOpcode>(Code[12]) == EOpcode::Return)
			{
				const std::uint16_t AddendIndex = Bytecode::ReadU16LE(Code.data() + 2);
				const std::uint16_t CalleeIndex = Bytecode::ReadU16LE(Code.data() + 5);
				const std::uint16_t SentinelIndex = Bytecode::ReadU16LE(Code.data() + 10);
				std::uint16_t NameIndex = 0xFFFF;
				std::uint16_t FieldIndex = 0xFFFF;
				if (AddendIndex < Module.Constants.size()
					&& Module.Constants[AddendIndex].Kind == EConstantKind::I32
					&& SentinelIndex < Module.Constants.size()
					&& Module.Constants[SentinelIndex].Kind == EConstantKind::I32
					&& Module.Constants[SentinelIndex].I32 == 0
					&& CalleeIndex < Module.Functions.size()
					&& MatchDirectParameterFieldAdd(Module.Functions[CalleeIndex], NameIndex)
					&& ResolveOwnerFieldIndex(NameIndex, FieldIndex))
				{
					OutEntry.bDirectThisFieldAddModuloI32 = true;
					OutEntry.DirectThisFieldIndex = FieldIndex;
					OutEntry.DirectThisFieldAddendI32 =
						Module.Constants[AddendIndex].I32;
					OutEntry.DirectThisArgumentModuloI32 = 0;
					OutEntry.DirectThisFieldModuloI32 = 0;
					OutEntry.ResolvedThisFieldOwnerTypeIndex = Function.OwnerTypeIndex;
					OutEntry.ResolvedThisFields.push_back(
						(static_cast<std::uint32_t>(NameIndex) << 16u) | FieldIndex);
				}
			}
			if (!Function.bReturnsVoid && Function.ParamCount == 1 && Code.size() == 8
				&& static_cast<EOpcode>(Code[0]) == EOpcode::LoadLocal
				&& Bytecode::ReadU16LE(Code.data() + 1) == 0
				&& static_cast<EOpcode>(Code[3]) == EOpcode::ConstI32
				&& static_cast<EOpcode>(Code[6]) == EOpcode::Add
				&& static_cast<EOpcode>(Code[7]) == EOpcode::Return)
			{
				const std::uint16_t AddendConstant = Bytecode::ReadU16LE(Code.data() + 4);
				if (AddendConstant < Module.Constants.size()
					&& Module.Constants[AddendConstant].Kind == EConstantKind::I32)
				{
					OutEntry.bDirectI32AddModulo = true;
					OutEntry.DirectI32Addend = Module.Constants[AddendConstant].I32;
					OutEntry.DirectI32Modulo = 0;
				}
			}
			else if (!Function.bReturnsVoid && Function.ParamCount == 1 && Code.size() == 12
				&& static_cast<EOpcode>(Code[0]) == EOpcode::LoadLocal
				&& Bytecode::ReadU16LE(Code.data() + 1) == 0
				&& static_cast<EOpcode>(Code[3]) == EOpcode::ConstI32
				&& static_cast<EOpcode>(Code[6]) == EOpcode::Add
				&& (static_cast<EOpcode>(Code[7]) == EOpcode::ConstI32
					|| static_cast<EOpcode>(Code[7]) == EOpcode::LoadGlobal)
				&& static_cast<EOpcode>(Code[10]) == EOpcode::Mod
				&& static_cast<EOpcode>(Code[11]) == EOpcode::Return)
			{
				const std::uint16_t AddendConstant = Bytecode::ReadU16LE(Code.data() + 4);
				std::int32_t Modulus = 0;
				if (AddendConstant < Module.Constants.size()
					&& Module.Constants[AddendConstant].Kind == EConstantKind::I32
					&& TryResolveI32InvariantOperand(Module, Code, 7, Modulus)
					&& Modulus != 0)
				{
					OutEntry.bDirectI32AddModulo = true;
					OutEntry.DirectI32Addend = Module.Constants[AddendConstant].I32;
					OutEntry.DirectI32Modulo = Modulus;
				}
			}
			std::vector<std::uint16_t> NameIndices;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Function.Code.size() - Ip)
				{
					return;
				}
				if (Op == EOpcode::LoadThisField || Op == EOpcode::StoreThisField || Op == EOpcode::InitializeThisField
					|| Op == EOpcode::MakeThisFieldRef)
				{
					const std::uint16_t NameIndex =
						Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
					if (NameIndex < Module.StringPool.size())
					{
						NameIndices.push_back(NameIndex);
					}
				}
				Ip += Size;
			}
			if (NameIndices.empty())
			{
				return;
			}
			std::sort(NameIndices.begin(), NameIndices.end());
			NameIndices.erase(
				std::unique(NameIndices.begin(), NameIndices.end()), NameIndices.end());
			const FBytecodeTypeDesc& Owner = Module.Types[Function.OwnerTypeIndex];
			for (const std::uint16_t NameIndex : NameIndices)
			{
				const std::string& Name = Module.StringPool[NameIndex];
				for (std::size_t FieldIndex = 0; FieldIndex < Owner.Fields.size(); ++FieldIndex)
				{
					if (Owner.Fields[FieldIndex].Name == Name
						&& FieldIndex <= std::numeric_limits<std::uint16_t>::max())
					{
						OutEntry.ResolvedThisFields.push_back(
							(static_cast<std::uint32_t>(NameIndex) << 16u)
							| static_cast<std::uint16_t>(FieldIndex));
						break;
					}
				}
			}
			OutEntry.ResolvedThisFieldOwnerTypeIndex = Function.OwnerTypeIndex;
			if (Function.bReturnsVoid && Function.ParamCount == 1 && Code.size() == 18
				&& static_cast<EOpcode>(Code[0]) == EOpcode::LoadThisField
				&& static_cast<EOpcode>(Code[3]) == EOpcode::LoadLocal
				&& Bytecode::ReadU16LE(Code.data() + 4) == 0
				&& static_cast<EOpcode>(Code[6]) == EOpcode::Add
				&& static_cast<EOpcode>(Code[7]) == EOpcode::StoreThisField
				&& static_cast<EOpcode>(Code[10]) == EOpcode::LoadThisField
				&& static_cast<EOpcode>(Code[13]) == EOpcode::Pop
				&& static_cast<EOpcode>(Code[14]) == EOpcode::ConstI32
				&& static_cast<EOpcode>(Code[17]) == EOpcode::Return)
			{
				const std::uint16_t NameIndex = Bytecode::ReadU16LE(Code.data() + 1);
				const std::uint16_t SentinelIndex = Bytecode::ReadU16LE(Code.data() + 15);
				if (NameIndex == Bytecode::ReadU16LE(Code.data() + 8)
					&& NameIndex == Bytecode::ReadU16LE(Code.data() + 11)
					&& SentinelIndex < Module.Constants.size()
					&& Module.Constants[SentinelIndex].Kind == EConstantKind::I32
					&& Module.Constants[SentinelIndex].I32 == 0)
				{
					const auto Found = std::lower_bound(
						OutEntry.ResolvedThisFields.begin(),
						OutEntry.ResolvedThisFields.end(),
						static_cast<std::uint32_t>(NameIndex) << 16u);
					if (Found != OutEntry.ResolvedThisFields.end()
						&& static_cast<std::uint16_t>(*Found >> 16u) == NameIndex)
					{
						OutEntry.bDirectThisFieldAddModuloI32 = true;
						OutEntry.DirectThisFieldIndex =
							static_cast<std::uint16_t>(*Found & 0xFFFFu);
						OutEntry.DirectThisFieldAddendI32 = 0;
						OutEntry.DirectThisArgumentModuloI32 = 0;
						OutEntry.DirectThisFieldModuloI32 = 0;
					}
				}
			}
			else if (Function.bReturnsVoid && Function.ParamCount == 1 && Code.size() == 22
				&& static_cast<EOpcode>(Code[0]) == EOpcode::LoadThisField
				&& static_cast<EOpcode>(Code[3]) == EOpcode::LoadLocal
				&& Bytecode::ReadU16LE(Code.data() + 4) == 0
				&& static_cast<EOpcode>(Code[6]) == EOpcode::Add
				&& (static_cast<EOpcode>(Code[7]) == EOpcode::ConstI32
					|| static_cast<EOpcode>(Code[7]) == EOpcode::LoadGlobal)
				&& static_cast<EOpcode>(Code[10]) == EOpcode::Mod
				&& static_cast<EOpcode>(Code[11]) == EOpcode::StoreThisField
				&& static_cast<EOpcode>(Code[14]) == EOpcode::LoadThisField
				&& static_cast<EOpcode>(Code[17]) == EOpcode::Pop
				&& static_cast<EOpcode>(Code[18]) == EOpcode::ConstI32
				&& static_cast<EOpcode>(Code[21]) == EOpcode::Return)
			{
				const std::uint16_t NameIndex = Bytecode::ReadU16LE(Code.data() + 1);
				std::int32_t Modulus = 0;
				if (NameIndex == Bytecode::ReadU16LE(Code.data() + 12)
					&& NameIndex == Bytecode::ReadU16LE(Code.data() + 15)
					&& TryResolveI32InvariantOperand(Module, Code, 7, Modulus)
					&& Modulus != 0)
				{
					const auto Found = std::lower_bound(
						OutEntry.ResolvedThisFields.begin(),
						OutEntry.ResolvedThisFields.end(),
						static_cast<std::uint32_t>(NameIndex) << 16u);
					if (Found != OutEntry.ResolvedThisFields.end()
						&& static_cast<std::uint16_t>(*Found >> 16u) == NameIndex)
					{
						OutEntry.bDirectThisFieldAddModuloI32 = true;
						OutEntry.DirectThisFieldIndex =
							static_cast<std::uint16_t>(*Found & 0xFFFFu);
						OutEntry.DirectThisFieldModuloI32 = Modulus;
					}
				}
			}
			else if (Function.bReturnsVoid && Function.ParamCount == 1 && Code.size() == 26
				&& static_cast<EOpcode>(Code[0]) == EOpcode::LoadThisField
				&& static_cast<EOpcode>(Code[3]) == EOpcode::LoadLocal
				&& Bytecode::ReadU16LE(Code.data() + 4) == 0
				&& (static_cast<EOpcode>(Code[6]) == EOpcode::ConstI32
					|| static_cast<EOpcode>(Code[6]) == EOpcode::LoadGlobal)
				&& static_cast<EOpcode>(Code[9]) == EOpcode::Mod
				&& static_cast<EOpcode>(Code[10]) == EOpcode::Add
				&& (static_cast<EOpcode>(Code[11]) == EOpcode::ConstI32
					|| static_cast<EOpcode>(Code[11]) == EOpcode::LoadGlobal)
				&& static_cast<EOpcode>(Code[14]) == EOpcode::Mod
				&& static_cast<EOpcode>(Code[15]) == EOpcode::StoreThisField
				&& static_cast<EOpcode>(Code[18]) == EOpcode::LoadThisField
				&& static_cast<EOpcode>(Code[21]) == EOpcode::Pop
				&& static_cast<EOpcode>(Code[22]) == EOpcode::ConstI32
				&& static_cast<EOpcode>(Code[25]) == EOpcode::Return)
			{
				const std::uint16_t NameIndex = Bytecode::ReadU16LE(Code.data() + 1);
				std::int32_t ArgumentModulus = 0;
				std::int32_t FieldModulus = 0;
				if (NameIndex == Bytecode::ReadU16LE(Code.data() + 16)
					&& NameIndex == Bytecode::ReadU16LE(Code.data() + 19)
					&& TryResolveI32InvariantOperand(Module, Code, 6, ArgumentModulus)
					&& TryResolveI32InvariantOperand(Module, Code, 11, FieldModulus)
					&& ArgumentModulus != 0 && FieldModulus != 0)
				{
					const auto Found = std::lower_bound(
						OutEntry.ResolvedThisFields.begin(),
						OutEntry.ResolvedThisFields.end(),
						static_cast<std::uint32_t>(NameIndex) << 16u);
					if (Found != OutEntry.ResolvedThisFields.end()
						&& static_cast<std::uint16_t>(*Found >> 16u) == NameIndex)
					{
						OutEntry.bDirectThisFieldAddModuloI32 = true;
						OutEntry.DirectThisFieldIndex =
							static_cast<std::uint16_t>(*Found & 0xFFFFu);
						OutEntry.DirectThisArgumentModuloI32 = ArgumentModulus;
						OutEntry.DirectThisFieldModuloI32 = FieldModulus;
					}
				}
			}
			else if (!Function.bReturnsVoid && !Function.bReturnsRef
				&& Function.ParamCount == 1 && Function.LocalCount == 1
				&& Code.size() == 30
				&& static_cast<EOpcode>(Code[0]) == EOpcode::LoadThisField
				&& static_cast<EOpcode>(Code[3]) == EOpcode::LoadLocal
				&& Bytecode::ReadU16LE(Code.data() + 4) == 0
				&& (static_cast<EOpcode>(Code[6]) == EOpcode::ConstI32
					|| static_cast<EOpcode>(Code[6]) == EOpcode::LoadGlobal)
				&& static_cast<EOpcode>(Code[9]) == EOpcode::Mod
				&& static_cast<EOpcode>(Code[10]) == EOpcode::Add
				&& static_cast<EOpcode>(Code[11]) == EOpcode::ConstI32
				&& static_cast<EOpcode>(Code[14]) == EOpcode::Add
				&& (static_cast<EOpcode>(Code[15]) == EOpcode::ConstI32
					|| static_cast<EOpcode>(Code[15]) == EOpcode::LoadGlobal)
				&& static_cast<EOpcode>(Code[18]) == EOpcode::Mod
				&& static_cast<EOpcode>(Code[19]) == EOpcode::StoreThisField
				&& static_cast<EOpcode>(Code[22]) == EOpcode::LoadThisField
				&& static_cast<EOpcode>(Code[25]) == EOpcode::Pop
				&& static_cast<EOpcode>(Code[26]) == EOpcode::LoadThisField
				&& static_cast<EOpcode>(Code[29]) == EOpcode::Return)
			{
				const std::uint16_t NameIndex = Bytecode::ReadU16LE(Code.data() + 1);
				const std::uint16_t AddendIndex = Bytecode::ReadU16LE(Code.data() + 12);
				std::int32_t ArgumentModulus = 0;
				std::int32_t FieldModulus = 0;
				if (NameIndex == Bytecode::ReadU16LE(Code.data() + 20)
					&& NameIndex == Bytecode::ReadU16LE(Code.data() + 23)
					&& NameIndex == Bytecode::ReadU16LE(Code.data() + 27)
					&& AddendIndex < Module.Constants.size()
					&& Module.Constants[AddendIndex].Kind == EConstantKind::I32
					&& TryResolveI32InvariantOperand(Module, Code, 6, ArgumentModulus)
					&& TryResolveI32InvariantOperand(Module, Code, 15, FieldModulus)
					&& ArgumentModulus != 0 && FieldModulus != 0)
				{
					const auto Found = std::lower_bound(
						OutEntry.ResolvedThisFields.begin(),
						OutEntry.ResolvedThisFields.end(),
						static_cast<std::uint32_t>(NameIndex) << 16u);
					if (Found != OutEntry.ResolvedThisFields.end()
						&& static_cast<std::uint16_t>(*Found >> 16u) == NameIndex)
					{
						OutEntry.bDirectThisFieldAddModuloI32 = true;
						OutEntry.DirectThisFieldIndex =
							static_cast<std::uint16_t>(*Found & 0xFFFFu);
						OutEntry.DirectThisFieldAddendI32 =
							Module.Constants[AddendIndex].I32;
						OutEntry.DirectThisArgumentModuloI32 = ArgumentModulus;
						OutEntry.DirectThisFieldModuloI32 = FieldModulus;
					}
				}
			}
		}

		void CollectLinearDispatchMetadata(
			const FBytecodeModule& Module,
			const FBytecodeFunction& Function,
			FJitFunctionEntry& OutEntry)
		{
			const auto& Code = Function.Code;
			const std::size_t CodeSize = Code.size();
			std::vector<std::uint8_t> LocalStoreCounts(Function.LocalCount);
			std::vector<std::size_t> LocalLastStore(
				Function.LocalCount, std::numeric_limits<std::size_t>::max());
			std::vector<bool> LocalAddressEscapes(Function.LocalCount);
			std::vector<std::pair<std::size_t, std::size_t>> BackEdges;
			for (std::size_t ScanIp = 0; ScanIp < CodeSize;)
			{
				const EOpcode ScanOp = static_cast<EOpcode>(Code[ScanIp]);
				const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
				if (ScanSize == 0 || ScanSize > CodeSize - ScanIp)
				{
					break;
				}
				const std::size_t ScanEnd = ScanIp + ScanSize;
				if (ScanOp == EOpcode::StoreLocal || ScanOp == EOpcode::BindLocal)
				{
					const std::uint16_t Slot =
						Bytecode::ReadU16LE(Code.data() + ScanIp + 1);
					if (Slot < Function.LocalCount)
					{
						LocalStoreCounts[Slot] = static_cast<std::uint8_t>(
							std::min<unsigned>(LocalStoreCounts[Slot] + 1u, 2u));
						LocalLastStore[Slot] = ScanIp;
						if (ScanOp == EOpcode::BindLocal)
						{
							LocalAddressEscapes[Slot] = true;
						}
					}
				}
				// Transfers and in-place writes bypass StoreLocal; escaping references can write too.
				else if (ScanOp == EOpcode::MakeLocalRef || ScanOp == EOpcode::TakeLocal
					|| ScanOp == EOpcode::IncLocal || ScanOp == EOpcode::AddLocalImm
					|| ScanOp == EOpcode::AddLocalLocal)
				{
					const std::uint16_t Slot =
						Bytecode::ReadU16LE(Code.data() + ScanIp + 1);
					if (Slot < Function.LocalCount)
					{
						LocalAddressEscapes[Slot] = true;
					}
				}
				if (ScanOp == EOpcode::Jmp || ScanOp == EOpcode::JmpIfFalse
					|| ScanOp == EOpcode::JmpIfLocalGeImm)
				{
					const std::size_t OffsetPos =
						ScanOp == EOpcode::JmpIfLocalGeImm ? 7u : 1u;
					std::size_t Target = 0;
					if (IsJumpTargetInRange(
							ScanEnd,
							Bytecode::ReadI32LE(Code.data() + ScanIp + OffsetPos),
							CodeSize,
							Target)
						&& Target < ScanIp)
					{
						BackEdges.emplace_back(Target, ScanIp);
					}
				}
				ScanIp = ScanEnd;
			}
			if (!BackEdges.empty())
			{
				OutEntry.InvariantLocalCompareSites.resize(CodeSize);
			}
			if (OutEntry.bUseScalarSetStorage)
			{
				OutEntry.ScalarSetCallKinds.resize(CodeSize);
			}
			for (std::size_t Ip = 0; Ip < CodeSize;)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > CodeSize - Ip)
				{
					break;
				}
				const std::size_t End = Ip + Size;
				if (Op == EOpcode::LoadLocal
					&& End + 9u <= CodeSize
					&& static_cast<EOpcode>(Code[End]) == EOpcode::LoadLocal)
				{
					const EOpcode CompareOp = static_cast<EOpcode>(Code[End + 3u]);
					const bool bComparison = CompareOp == EOpcode::Eq
						|| CompareOp == EOpcode::Ne || CompareOp == EOpcode::Lt
						|| CompareOp == EOpcode::Le || CompareOp == EOpcode::Gt
						|| CompareOp == EOpcode::Ge;
					if (bComparison
						&& static_cast<EOpcode>(Code[End + 4u]) == EOpcode::JmpIfFalse)
					{
						const std::uint16_t LeftSlot =
							Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						const std::uint16_t RightSlot =
							Bytecode::ReadU16LE(Code.data() + End + 1u);
						std::size_t LoopBegin = Ip;
						bool bInsideLoop = false;
						for (const auto& Edge : BackEdges)
						{
							if (Edge.first <= Ip && Ip <= Edge.second)
							{
								bInsideLoop = true;
								LoopBegin = std::min(LoopBegin, Edge.first);
							}
						}
						const auto IsImmutable = [&](const std::uint16_t Slot)
						{
							return Slot < Function.LocalCount
								&& !LocalAddressEscapes[Slot]
								&& LocalStoreCounts[Slot] <= 1u
								&& (LocalStoreCounts[Slot] == 0u
									|| LocalLastStore[Slot] < LoopBegin);
						};
						// A single bytecode store inside a loop still runs each iteration.
						// Only values initialized before every enclosing back edge are invariant.
						if (bInsideLoop && IsImmutable(LeftSlot) && IsImmutable(RightSlot))
						{
							OutEntry.InvariantLocalCompareSites[Ip] = 1u;
						}
					}
				}
				if (Op == EOpcode::CallMethod && End < CodeSize
					&& static_cast<EOpcode>(Code[End]) == EOpcode::StoreLocal)
				{
					OutEntry.bMayHaveDirectScalarMethodStore = true;
				}
				else if (Op == EOpcode::CallMethod && End < CodeSize
					&& static_cast<EOpcode>(Code[End]) == EOpcode::Add)
				{
					const std::size_t StoreIp =
						End + Bytecode::InstructionSize(EOpcode::Add);
					OutEntry.bMayHaveScalarMethodAccumulator =
						OutEntry.bMayHaveScalarMethodAccumulator
						|| (StoreIp < CodeSize
							&& static_cast<EOpcode>(Code[StoreIp]) == EOpcode::StoreLocal);
				}
				if (Op == EOpcode::CallBoundMethod && Code[Ip + 3] <= 1)
				{
					OutEntry.bMayHaveBoundScalarMethodCall = true;
				}
				if (OutEntry.bUseScalarSetStorage && Op == EOpcode::CallMethod)
				{
					const std::uint16_t MethodNameIndex =
						Bytecode::ReadU16LE(Code.data() + Ip + 1);
					if (MethodNameIndex < Module.StringPool.size())
					{
						const std::string& MethodName = Module.StringPool[MethodNameIndex];
						if (MethodName == "add")
						{
							OutEntry.ScalarSetCallKinds[Ip] = 1;
						}
						else if (MethodName == "has")
						{
							OutEntry.ScalarSetCallKinds[Ip] = 2;
						}
						else if (MethodName == "remove")
						{
							OutEntry.ScalarSetCallKinds[Ip] = 3;
						}
					}
				}
				Ip = End;
			}
		}

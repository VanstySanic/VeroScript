// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: typed LoadLocal hot paths and canonical loop reductions. Included by the owning translation unit.

			case EOpcode::LoadLocal:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					// Length reads observe the rooted value without materializing a
					// potentially large string copy. References and other values retain
					// the canonical local load and builtin boundary.
					if (Bytecode::InstructionSize(EOpcode::CallBuiltin) <= CodeSize - AfterIp &&
						static_cast<EOpcode>(Code[AfterIp]) == EOpcode::CallBuiltin &&
						static_cast<EBuiltinId>(Code[AfterIp + 1u]) == EBuiltinId::Len && Code[AfterIp + 2u] == 1u)
					{
						const FValue* Value = BorrowLocal(Slot);
						std::int64_t Length = 0;
						if (Value && Runtime::TryGetStringLength(*Value, Length))
						{
							Stack.emplace_back(Length);
							Ip = AfterIp + Bytecode::InstructionSize(EOpcode::CallBuiltin);
							break;
						}
					}
					// Filtered integral-set construction:
					//   for (...) if ((counter % divisor) == remainder) set.add(counter);
					// The complete body is side-effect-free apart from a non-escaping typed
					// Set, so execute the range directly and avoid a container call bridge for
					// every candidate. Non-canonical or exceptional remainder cases fall back.
					constexpr std::size_t FilteredSetBodySize = 27u;
					if (bUseScalarSetStorage
							&& Ip + FilteredSetBodySize
								+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
							&& static_cast<EOpcode>(Code[Ip + 3u]) == EOpcode::ConstI32
							&& static_cast<EOpcode>(Code[Ip + 6u]) == EOpcode::Mod
							&& static_cast<EOpcode>(Code[Ip + 7u]) == EOpcode::ConstI32
							&& static_cast<EOpcode>(Code[Ip + 10u]) == EOpcode::Eq
							&& static_cast<EOpcode>(Code[Ip + 11u]) == EOpcode::JmpIfFalse
							&& static_cast<EOpcode>(Code[Ip + 16u]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[Ip + 19u]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + Ip + 20u) == Slot
							&& static_cast<EOpcode>(Code[Ip + 22u]) == EOpcode::CallMethod
							&& Code[Ip + 25u] == 1u
							&& static_cast<EOpcode>(Code[Ip + 26u]) == EOpcode::Pop)
					{
						const std::uint16_t DivisorIndex =
								Bytecode::ReadU16LE(Code.data() + Ip + 4u);
						const std::uint16_t RemainderIndex =
								Bytecode::ReadU16LE(Code.data() + Ip + 8u);
						const std::uint16_t SetSlot =
								Bytecode::ReadU16LE(Code.data() + Ip + 17u);
						const std::uint16_t MethodNameIndex =
								Bytecode::ReadU16LE(Code.data() + Ip + 23u);
						std::size_t FalseTarget = 0;
						FCanonicalI32Loop Loop;
						FValue* const SetValue = BorrowLocal(SetSlot);
						auto* const SetHandle = SetValue ? std::get_if<FSetHandle>(SetValue) : nullptr;
						auto* const Entries = SetHandle && *SetHandle
								? std::get_if<FSet::FIntScalarElements>(
									&(*SetHandle)->ScalarElements)
							: nullptr;
						if (DivisorIndex < Module->Constants.size()
								&& RemainderIndex < Module->Constants.size()
								&& Module->Constants[DivisorIndex].Kind == EConstantKind::I32
								&& Module->Constants[RemainderIndex].Kind == EConstantKind::I32
								&& MethodNameIndex < Module->StringPool.size()
								&& Module->StringPool[MethodNameIndex] == "add"
								&& SetSlot != Slot && Entries
								&& (*SetHandle)->MutationOrigin.FieldIndex
									== static_cast<std::uint16_t>(-1)
								&& IsJumpTargetInRange(
									Ip + 16u, Bytecode::ReadI32LE(Code.data() + Ip + 12u),
									CodeSize, FalseTarget)
								&& FalseTarget == Ip + FilteredSetBodySize
								&& TryMatchCanonicalI32Loop(
									Ip, Ip + FilteredSetBodySize, Slot, Loop)
								&& LoopDoesNotAliasMutatedLocal(Loop, SetSlot))
						{
							const std::int32_t Divisor = Module->Constants[DivisorIndex].I32;
							const std::int32_t Remainder = Module->Constants[RemainderIndex].I32;
							const std::int32_t First = *Loop.Counter;
							const bool bExceptionalDivision = Divisor == 0
									|| (Divisor == -1
										&& First == std::numeric_limits<std::int32_t>::min());
							if (!bExceptionalDivision)
							{
								for (std::int64_t Current = First; Current < Loop.Limit; ++Current)
								{
									// Keep the observable loop state exact if insertion fails while
									// growing the flat set (for example, allocation failure).
									*Loop.Counter = static_cast<std::int32_t>(Current);
									const std::int32_t Value = static_cast<std::int32_t>(Current);
									if (Value % Divisor == Remainder)
									{
										(void)Entries->Insert(Value);
									}
								}
								*Loop.Counter = Loop.Limit;
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// Nested string-container reduction:
					//   total += stringMap[stringArray[counter & mask]]
					// Both containers are borrowed from rooted frame locals and the exact loop
					// body contains no writes to either handle.  Cache one bounded key period
					// so repeated string hashing and two generic GetIndex bridges disappear,
					// while checked i32 addition still runs in source order.
					constexpr std::size_t StringMapReductionBodySize = 22u;
					if (Ip + StringMapReductionBodySize <= CodeSize
							&& static_cast<EOpcode>(Code[Ip + 3u]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[Ip + 6u]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[Ip + 9u]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[Ip + 12u]) == EOpcode::ConstI32
							&& static_cast<EOpcode>(Code[Ip + 15u]) == EOpcode::BitAnd
							&& static_cast<EOpcode>(Code[Ip + 16u]) == EOpcode::GetIndexCopy
							&& static_cast<EOpcode>(Code[Ip + 17u]) == EOpcode::GetIndexCopy
							&& static_cast<EOpcode>(Code[Ip + 18u]) == EOpcode::Add
							&& static_cast<EOpcode>(Code[Ip + 19u]) == EOpcode::StoreLocal
							&& Bytecode::ReadU16LE(Code.data() + Ip + 20u) == Slot)
					{
						const std::uint16_t MapSlot = Bytecode::ReadU16LE(Code.data() + Ip + 4u);
						const std::uint16_t KeysSlot = Bytecode::ReadU16LE(Code.data() + Ip + 7u);
						const std::uint16_t CounterSlot = Bytecode::ReadU16LE(Code.data() + Ip + 10u);
						const std::uint16_t MaskIndex = Bytecode::ReadU16LE(Code.data() + Ip + 13u);
						FCanonicalI32Loop Loop;
						auto* const Accumulator = std::get_if<std::int32_t>(BorrowLocal(Slot));
						FValue* const MapValue = BorrowLocal(MapSlot);
						FValue* const KeysValue = BorrowLocal(KeysSlot);
						auto* const MapHandle = MapValue ? std::get_if<FMapHandle>(MapValue) : nullptr;
						auto* const KeysHandle = KeysValue
								? std::get_if<FDynamicArrayHandle>(KeysValue)
							: nullptr;
						if (MaskIndex < Module->Constants.size()
								&& Module->Constants[MaskIndex].Kind == EConstantKind::I32
								&& Module->Constants[MaskIndex].I32 >= 0
								&& Slot != MapSlot && Slot != KeysSlot
								&& Accumulator && MapHandle && *MapHandle && KeysHandle && *KeysHandle
								&& (*MapHandle)->KeyTypeName == "string"
								&& ((*MapHandle)->ValueTypeName == "int"
									|| (*MapHandle)->ValueTypeName == "int32")
								&& (*KeysHandle)->ElementTypeName == "string"
								&& !(*KeysHandle)->HasScalarElements()
								&& TryMatchCanonicalI32Loop(
									Ip, Ip + StringMapReductionBodySize, CounterSlot, Loop)
								&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
								&& MapSlot != Loop.CounterSlot && MapSlot != Loop.LimitSlot
								&& KeysSlot != Loop.CounterSlot && KeysSlot != Loop.LimitSlot)
						{
							const std::uint32_t Mask = static_cast<std::uint32_t>(
									Module->Constants[MaskIndex].I32);
							const auto FindStringI32 = [&MapHandle](
									const std::string& Key, bool& bOutFound) -> const std::int32_t*
							{
								if (const auto* const Entries =
											std::get_if<FMap::FStringI32Entries>(
												&(*MapHandle)->ScalarEntries))
								{
									const std::int32_t* const Found = Entries->Find(Key);
									bOutFound = Found != nullptr;
									return Found;
								}
								const auto Found = (*MapHandle)->StrEntries.find(Key);
								bOutFound = Found != (*MapHandle)->StrEntries.end();
								if (!bOutFound)
								{
									return nullptr;
								}
								return std::get_if<std::int32_t>(&Found->second);
							};
							std::size_t Period = 1u;
							while (Period <= Mask && Period < (std::size_t{1} << 16u))
							{
								Period <<= 1u;
							}
							std::vector<std::int32_t> Cycle;
							bool bCycleReady = Period > Mask;
							if (bCycleReady)
							{
								const std::size_t KeyCount = (*KeysHandle)->HasScalarElements()
										? (*KeysHandle)->ScalarSize()
									: (*KeysHandle)->Elements.size();
								Cycle.reserve(Period);
								for (std::size_t Offset = 0; Offset < Period; ++Offset)
								{
									const std::size_t KeyIndex = Offset & Mask;
									if (KeyIndex >= KeyCount)
									{
										bCycleReady = false;
										break;
									}
									const std::string* const Key = (*KeysHandle)->BorrowString(KeyIndex);
									bool bFound = false;
									const auto* const Value = Key
											? FindStringI32(*Key, bFound)
										: nullptr;
									if (!bFound || !Value)
									{
										bCycleReady = false;
										break;
									}
									Cycle.push_back(*Value);
								}
							}
							const std::int32_t First = *Loop.Counter;
							std::int32_t AccumulatedValue = *Accumulator;
							for (std::int64_t Current = First; Current < Loop.Limit; ++Current)
							{
								std::int32_t Value = 0;
								if (bCycleReady)
								{
									Value = Cycle[static_cast<std::uint32_t>(Current)
											& (Period - 1u)];
								}
								else
								{
									const std::size_t KeyIndex =
											static_cast<std::uint32_t>(Current) & Mask;
									const std::size_t KeyCount = (*KeysHandle)->HasScalarElements()
											? (*KeysHandle)->ScalarSize()
										: (*KeysHandle)->Elements.size();
									if (KeyIndex >= KeyCount)
									{
										*Accumulator = AccumulatedValue;
										*Loop.Counter = static_cast<std::int32_t>(Current);
										return ErrorAt(Ip + 16u, "array index out of bounds");
									}
									const std::string* const Key = (*KeysHandle)->BorrowString(KeyIndex);
									if (!Key)
									{
										*Accumulator = AccumulatedValue;
										*Loop.Counter = static_cast<std::int32_t>(Current);
										return ErrorAt(Ip + 16u, "array value type mismatch");
									}
									bool bFound = false;
									const auto* const Mapped = FindStringI32(*Key, bFound);
									if (!Mapped)
									{
										*Accumulator = AccumulatedValue;
										*Loop.Counter = static_cast<std::int32_t>(Current);
										return ErrorAt(Ip + 17u, bFound
												? "map value type mismatch" : "map key not found");
									}
									Value = *Mapped;
								}
								std::int32_t Sum = 0;
								if (!Runtime::CheckedAdd(AccumulatedValue, Value, Sum))
								{
									*Accumulator = AccumulatedValue;
									*Loop.Counter = static_cast<std::int32_t>(Current);
									return ErrorAt(Ip + 18u, "integer overflow");
								}
								AccumulatedValue = Sum;
							}
							const std::size_t Count = static_cast<std::size_t>(
									static_cast<std::int64_t>(Loop.Limit) - First);
							*Accumulator = AccumulatedValue;
							*Loop.Counter = Loop.Limit;
							Vm->RecordJitScalarMapFastPathGet(Count);
							Ip = Loop.ExitIp;
							break;
						}
					}
					// Scalar affine/modulo recurrence with a second modular accumulator.
					// Constants and loop bounds remain arbitrary; the helper versions the
					// loop only when range proofs allow all checked operations to be removed.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t BodyIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t BodySize = 18u;
						if (bUseI32ModuloAccumulatorLoop && Function.ExceptionRegions.empty()
								&& BodyIp + BodySize
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 4u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 9u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 10u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 11u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 14u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 15u]) == EOpcode::StoreLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t TermModulus = 0;
							std::int32_t AccumulatorModulus = 0;
							const std::uint16_t AccumulatorSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 1u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + BodySize + 1u);
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Accumulator =
									std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot));
							FCanonicalI32Loop Loop;
							if (AccumulatorSlot != Slot && CounterSlot != Slot
									&& CounterSlot != AccumulatorSlot
									&& Bytecode::ReadU16LE(Code.data() + BodyIp + 16u)
										== AccumulatorSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& ReadI32(BodyIp + 6u, TermModulus)
									&& ReadI32(BodyIp + 11u, AccumulatorModulus)
									&& State && Accumulator
									&& TryMatchCanonicalI32Loop(
										Ip, BodyIp + BodySize, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot))
							{
								const std::int32_t First = *Loop.Counter;
								const std::size_t Count = static_cast<std::size_t>(
										static_cast<std::int64_t>(Loop.Limit) - First);
								JitHotLoops::FAffineI32UpdateResult Result;
								const bool bOk = JitHotLoops::AccumulateAffineI32Modulo(
										Count, Multiplier, Increment, StateModulus, TermModulus,
										AccumulatorModulus, *State, *Accumulator, Result);
								*State = Result.State;
								*Accumulator = Result.Accumulator;
								*Loop.Counter = bOk ? Loop.Limit : static_cast<std::int32_t>(
										static_cast<std::int64_t>(First)
											+ static_cast<std::int64_t>(Result.CompletedIterations));
								if (!bOk)
								{
									using EFailure = JitHotLoops::EAffineI32UpdateFailure;
									switch (Result.Failure)
									{
									case EFailure::StateMultiplyOverflow:
										return ErrorAt(MulIp, "integer overflow");
									case EFailure::StateAddOverflow:
										return ErrorAt(AddIp, "integer overflow");
									case EFailure::StateDivisionByZero:
										return ErrorAt(StateModIp, "division by zero");
									case EFailure::StateDivisionOverflow:
										return ErrorAt(StateModIp, "integer overflow");
									case EFailure::TermDivisionByZero:
										return ErrorAt(BodyIp + 9u, "division by zero");
									case EFailure::TermDivisionOverflow:
										return ErrorAt(BodyIp + 9u, "integer overflow");
									case EFailure::AccumulatorAddOverflow:
										return ErrorAt(BodyIp + 10u, "integer overflow");
									case EFailure::AccumulatorDivisionByZero:
										return ErrorAt(BodyIp + 14u, "division by zero");
									case EFailure::AccumulatorDivisionOverflow:
										return ErrorAt(BodyIp + 14u, "integer overflow");
									default:
										return ErrorAt(BodyIp, "JIT scalar recurrence failed");
									}
								}
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// Typed affine map value update:
					//   state = (state * multiplier + increment) % modulus;
					//   values[state] = values[state] + scalar;
					// The state expression, constants, loop bounds and scalar value type are
					// all runtime-independent metadata. Dispatch the raw map representation
					// once, then keep the complete loop in native slots instead of rebuilding
					// key/value FValues on every iteration.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t ModulusIp = AddIp + 1u;
						const std::size_t ModIp = ModulusIp + ConstantSize;
						const std::size_t StateStoreIp = ModIp + 1u;
						const std::size_t BodyIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t BodySize = 31u;
						if (bUseScalarMapStorage && Function.ExceptionRegions.empty()
								&& BodyIp + BodySize
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[ModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[ModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 4u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 7u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u)
								&& static_cast<EOpcode>(Code[BodyIp + 9u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 10u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 12u]) == EOpcode::GetIndexCopy
								&& (static_cast<EOpcode>(Code[BodyIp + 13u]) == EOpcode::ConstI32
									|| static_cast<EOpcode>(Code[BodyIp + 13u]) == EOpcode::ConstF64)
								&& static_cast<EOpcode>(Code[BodyIp + 16u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 17u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[BodyIp + 20u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 18u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 21u)
								&& static_cast<EOpcode>(Code[BodyIp + 23u]) == EOpcode::SetIndex
								&& static_cast<EOpcode>(Code[BodyIp + 24u]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 25u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u)
								&& static_cast<EOpcode>(Code[BodyIp + 27u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 28u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 21u)
								&& static_cast<EOpcode>(Code[BodyIp + 30u]) == EOpcode::Pop)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t Modulus = 0;
							const std::uint16_t MapSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 1u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + BodySize + 1u);
							const std::uint16_t ResultSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 18u);
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							FValue* const MapValue = BorrowLocal(MapSlot);
							auto* const MapHandle = MapValue ? std::get_if<FMapHandle>(MapValue) : nullptr;
							FCanonicalI32Loop Loop;
							if (MapSlot != Slot && CounterSlot != Slot && MapSlot != CounterSlot
									&& ResultSlot != Slot && ResultSlot != MapSlot
									&& ResultSlot != CounterSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(ModulusIp, Modulus) && State && MapHandle && *MapHandle
									&& (*MapHandle)->MutationOrigin.FieldIndex == static_cast<std::uint16_t>(-1)
									&& TryMatchCanonicalI32Loop(Ip, BodyIp + BodySize, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, MapSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, ResultSlot))
							{
								enum class EFailure : std::uint8_t
								{
									None,
									StateMul,
									StateAdd,
									StateDivZero,
										StateDivOverflow,
									MissingKey,
									ValueOverflow,
								};
								EFailure Failure = EFailure::None;
								bool bStorageMatched = false;
								const std::int32_t FirstCounter = *Loop.Counter;
								const auto Run = [&](auto& Entries, const auto Delta)
								{
									using TValue = std::decay_t<decltype(Delta)>;
									bStorageMatched = true;
									std::int32_t CurrentState = *State;
									for (std::int64_t Current = *Loop.Counter;
										Current < Loop.Limit; ++Current)
									{
										*Loop.Counter = static_cast<std::int32_t>(Current);
										std::int32_t Product = 0;
										std::int32_t Sum = 0;
										if (!Runtime::CheckedMul(CurrentState, Multiplier, Product))
										{
											Failure = EFailure::StateMul;
											break;
										}
										if (!Runtime::CheckedAdd(Product, Increment, Sum))
										{
											Failure = EFailure::StateAdd;
											break;
										}
										if (Modulus == 0)
										{
											Failure = EFailure::StateDivZero;
											break;
										}
										if (Sum == std::numeric_limits<std::int32_t>::min() && Modulus == -1)
										{
											Failure = EFailure::StateDivOverflow;
											break;
										}
										CurrentState = Sum % Modulus;
										*State = CurrentState;
										TValue* const Stored = Entries.FindMutable(CurrentState);
										if (!Stored)
										{
											Failure = EFailure::MissingKey;
											break;
										}
										if constexpr (std::is_same_v<TValue, std::int32_t>)
										{
											TValue Updated = 0;
											if (!Runtime::CheckedAdd(*Stored, Delta, Updated))
											{
												Failure = EFailure::ValueOverflow;
												break;
											}
											*Stored = Updated;
										}
										else
										{
											*Stored += Delta;
										}
										*Loop.Counter = static_cast<std::int32_t>(Current + 1);
									}
								};
								const std::uint16_t DeltaIndex =
										Bytecode::ReadU16LE(Code.data() + BodyIp + 14u);
								if (DeltaIndex < Module->Constants.size())
								{
									const auto& Delta = Module->Constants[DeltaIndex];
									if (auto* I32Entries = std::get_if<FMap::FIntI32Entries>(
												&(*MapHandle)->ScalarEntries);
										I32Entries && Delta.Kind == EConstantKind::I32)
									{
										Run(*I32Entries, Delta.I32);
									}
									else if (auto* U32Entries = std::get_if<FMap::FIntU32Entries>(
												&(*MapHandle)->ScalarEntries);
										U32Entries && Delta.Kind == EConstantKind::U32)
									{
										Run(*U32Entries, Delta.U32);
									}
									else if (auto* F32Entries = std::get_if<FMap::FIntF32Entries>(
												&(*MapHandle)->ScalarEntries);
										F32Entries && Delta.Kind == EConstantKind::F32)
									{
										Run(*F32Entries, Delta.F32);
									}
									else if (auto* F64Entries = std::get_if<FMap::FIntF64Entries>(
												&(*MapHandle)->ScalarEntries);
										F64Entries && Delta.Kind == EConstantKind::F64)
									{
										Run(*F64Entries, Delta.F64);
									}
								}
								if (bStorageMatched)
								{
									if (Failure != EFailure::None)
									{
										switch (Failure)
										{
										case EFailure::StateMul:
											return ErrorAt(MulIp, "integer overflow");
										case EFailure::StateAdd:
											return ErrorAt(AddIp, "integer overflow");
										case EFailure::StateDivZero:
											return ErrorAt(ModIp, "division by zero");
										case EFailure::StateDivOverflow:
											return ErrorAt(ModIp, "integer overflow");
										case EFailure::MissingKey:
											return ErrorAt(BodyIp + 12u, "map key not found");
										case EFailure::ValueOverflow:
											return ErrorAt(BodyIp + 16u, "integer overflow");
										default:
											{
												break;
											}
										}
									}
									const std::size_t Count = static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - FirstCounter);
									Vm->RecordJitScalarMapFastPathGet(Count);
									Vm->RecordJitScalarMapFastPathSet(Count);
									Ip = Loop.ExitIp;
									break;
								}
							}
						}
					}
					// General typed i32 random-access loop. Unlike the unsigned masked
					// specialization below, this path retains checked signed arithmetic and
					// arbitrary positive/negative remainder semantics. The lowering is based
					// on typed bytecode operations and constants rather than their values.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t BodyIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t TypedBodySize = 49u;
						if ((bUseScalarArrayStorage || bUseScalarMapStorage)
								&& Function.ExceptionRegions.empty()
								&& BodyIp + TypedBodySize
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 4u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 9u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 10u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 12u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[BodyIp + 13u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 16u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 17u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[BodyIp + 20u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 18u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 21u)
								&& static_cast<EOpcode>(Code[BodyIp + 23u]) == EOpcode::SetIndex
								&& static_cast<EOpcode>(Code[BodyIp + 24u]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 25u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u)
								&& static_cast<EOpcode>(Code[BodyIp + 27u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 28u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 18u)
								&& static_cast<EOpcode>(Code[BodyIp + 30u]) == EOpcode::Pop
								&& static_cast<EOpcode>(Code[BodyIp + 31u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 34u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 35u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u)
								&& static_cast<EOpcode>(Code[BodyIp + 37u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 38u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 40u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[BodyIp + 41u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 42u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 45u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 46u]) == EOpcode::StoreLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t ElementIncrement = 0;
							std::int32_t AccumulatorModulus = 0;
							const std::uint16_t ArraySlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 1u);
							const std::uint16_t TemporarySlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 18u);
							const std::uint16_t AccumulatorSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 32u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 50u);
							FValue* const ArrayValue = BorrowLocal(ArraySlot);
							auto* const ArrayHandle = ArrayValue
									? std::get_if<FDynamicArrayHandle>(ArrayValue)
								: nullptr;
							auto* const Values = ArrayHandle && *ArrayHandle
									? std::get_if<std::vector<std::int32_t>>(
										&(*ArrayHandle)->ScalarElements)
								: nullptr;
							auto* const MapHandle = ArrayValue
									? std::get_if<FMapHandle>(ArrayValue)
								: nullptr;
							auto* const Entries = MapHandle && *MapHandle
									? std::get_if<FMap::FIntI32Entries>(
										&(*MapHandle)->ScalarEntries)
								: nullptr;
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Accumulator =
									std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot));
							FValue* const Temporary = BorrowLocal(TemporarySlot);
							FCanonicalI32Loop Loop;
							if (ArraySlot != Slot && AccumulatorSlot != Slot
									&& TemporarySlot != Slot && CounterSlot != Slot
									&& ArraySlot != AccumulatorSlot && ArraySlot != TemporarySlot
									&& TemporarySlot != AccumulatorSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& ReadI32(BodyIp + 13u, ElementIncrement)
									&& ReadI32(BodyIp + 42u, AccumulatorModulus)
									&& State && Accumulator && Temporary && (Values || Entries)
									&& (!Values || (*ArrayHandle)->MutationOrigin.FieldIndex
										== static_cast<std::uint16_t>(-1))
									&& (!Entries || (*MapHandle)->MutationOrigin.FieldIndex
										== static_cast<std::uint16_t>(-1))
									&& TryMatchCanonicalI32Loop(
										Ip, BodyIp + TypedBodySize, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, TemporarySlot))
							{
								const std::int32_t First = *Loop.Counter;
								const std::size_t Count = static_cast<std::size_t>(
										static_cast<std::int64_t>(Loop.Limit) - First);
								JitHotLoops::FAffineI32UpdateResult Result;
								const bool bOk = Values
										? JitHotLoops::UpdateAffineI32ArrayModulo(
											Values->data(), Values->size(), Count,
											Multiplier, Increment, StateModulus,
										  {JitHotLoops::EI32ElementDeltaKind::Constant,
												ElementIncrement, 1, 0},
											AccumulatorModulus, *State, *Accumulator, Result)
										: JitHotLoops::UpdateAffineI32MapModulo(
											*Entries, Count, Multiplier, Increment, StateModulus,
										  {JitHotLoops::EI32ElementDeltaKind::Constant,
												ElementIncrement, 1, 0},
										  AccumulatorModulus,
											*State, *Accumulator, Result);
								*State = Result.State;
								*Accumulator = Result.Accumulator;
								// StoreLocal(Temporary) occurs only after a successful element
								// read and checked add. Preserve its old value for failures that
								// happen earlier in the source expression.
								const bool bTemporaryWasStored = bOk
										|| Result.CompletedIterations != 0u
										|| Result.Failure
											== JitHotLoops::EAffineI32UpdateFailure::AccumulatorAddOverflow
										|| Result.Failure
											== JitHotLoops::EAffineI32UpdateFailure::AccumulatorDivisionByZero
										|| Result.Failure
											== JitHotLoops::EAffineI32UpdateFailure::AccumulatorDivisionOverflow;
								if (bTemporaryWasStored)
								{
									*Temporary = Result.LastElement;
								}
								*Loop.Counter = bOk ? Loop.Limit : static_cast<std::int32_t>(
										static_cast<std::int64_t>(First)
											+ static_cast<std::int64_t>(Result.CompletedIterations));
								if (!bOk)
								{
									using EFailure = JitHotLoops::EAffineI32UpdateFailure;
									switch (Result.Failure)
									{
									case EFailure::StateMultiplyOverflow:
										return ErrorAt(MulIp, "integer overflow");
									case EFailure::StateAddOverflow:
										return ErrorAt(AddIp, "integer overflow");
									case EFailure::StateDivisionByZero:
										return ErrorAt(StateModIp, "division by zero");
									case EFailure::StateDivisionOverflow:
										return ErrorAt(StateModIp, "integer overflow");
									case EFailure::IndexOutOfBounds:
										return ErrorAt(BodyIp + 12u,
											Values ? "array index out of bounds" : "map key not found");
									case EFailure::ElementAddOverflow:
										return ErrorAt(BodyIp + 16u, "integer overflow");
									case EFailure::AccumulatorAddOverflow:
										return ErrorAt(BodyIp + 41u, "integer overflow");
									case EFailure::AccumulatorDivisionByZero:
										return ErrorAt(BodyIp + 45u, "division by zero");
									case EFailure::AccumulatorDivisionOverflow:
										return ErrorAt(BodyIp + 45u, "integer overflow");
									default:
										return ErrorAt(BodyIp, "JIT typed array loop failed");
									}
								}
								if (Values)
								{
									Vm->RecordJitArrayFastPathGet(Count * 2u);
									Vm->RecordJitArrayFastPathSet(Count);
								}
								else
								{
									Vm->RecordJitScalarMapFastPathGet(Count * 2u);
									Vm->RecordJitScalarMapFastPathSet(Count);
								}
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// General typed indexed update. The index is an ordinary local computed
					// from the affine state, and both the element and accumulator retain their
					// source modulo operations. Matching slots and data-flow relationships (not
					// constant values) admits non-canonical sizes and independent index locals.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t IndexIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t IndexExpressionSize = 10u;
						const std::size_t BodyIp = IndexIp + IndexExpressionSize;
						constexpr std::size_t BodySize = 61u;
						if ((bUseScalarArrayStorage || bUseScalarMapStorage)
								&& Function.ExceptionRegions.empty()
								&& BodyIp + BodySize
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[IndexIp]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + IndexIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[IndexIp + 3u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[IndexIp + 6u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[IndexIp + 7u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 7u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u)
								&& static_cast<EOpcode>(Code[BodyIp + 9u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 12u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[BodyIp + 13u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 14u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 16u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 19u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 20u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 21u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 24u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 25u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 28u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 29u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[BodyIp + 32u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 30u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 33u)
								&& static_cast<EOpcode>(Code[BodyIp + 35u]) == EOpcode::SetIndex
								&& static_cast<EOpcode>(Code[BodyIp + 36u]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 37u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u)
								&& static_cast<EOpcode>(Code[BodyIp + 39u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 40u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 30u)
								&& static_cast<EOpcode>(Code[BodyIp + 42u]) == EOpcode::Pop
								&& static_cast<EOpcode>(Code[BodyIp + 43u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 46u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 47u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u)
								&& static_cast<EOpcode>(Code[BodyIp + 49u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 52u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[BodyIp + 53u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 54u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 57u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 58u]) == EOpcode::StoreLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t IndexModulus = 0;
							std::int32_t DeltaModulus = 0;
							std::int32_t DeltaAddend = 0;
							std::int32_t ElementModulus = 0;
							std::int32_t AccumulatorModulus = 0;
							const std::uint16_t IndexSlot =
									Bytecode::ReadU16LE(Code.data() + IndexIp + 8u);
							const std::uint16_t ContainerSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 1u);
							const std::uint16_t TemporarySlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 30u);
							const std::uint16_t AccumulatorSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 44u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + BodySize + 1u);
							const bool bIndexSlotsMatch =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 4u) == IndexSlot
									&& Bytecode::ReadU16LE(Code.data() + BodyIp + 10u) == IndexSlot
									&& Bytecode::ReadU16LE(Code.data() + BodyIp + 50u) == IndexSlot;
							FValue* const ContainerValue = BorrowLocal(ContainerSlot);
							auto* const ArrayHandle = ContainerValue
									? std::get_if<FDynamicArrayHandle>(ContainerValue)
								: nullptr;
							auto* const Values = ArrayHandle && *ArrayHandle
									? std::get_if<std::vector<std::int32_t>>(
										&(*ArrayHandle)->ScalarElements)
								: nullptr;
							auto* const MapHandle = ContainerValue
									? std::get_if<FMapHandle>(ContainerValue)
								: nullptr;
							auto* const Entries = MapHandle && *MapHandle
									? std::get_if<FMap::FIntI32Entries>(
										&(*MapHandle)->ScalarEntries)
								: nullptr;
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Index = std::get_if<std::int32_t>(BorrowLocal(IndexSlot));
							auto* const Accumulator =
									std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot));
							auto* const Temporary =
									std::get_if<std::int32_t>(BorrowLocal(TemporarySlot));
							FCanonicalI32Loop Loop;
							if (bIndexSlotsMatch && ContainerSlot != Slot && IndexSlot != Slot
									&& TemporarySlot != Slot && AccumulatorSlot != Slot
									&& CounterSlot != Slot && ContainerSlot != IndexSlot
									&& IndexSlot != TemporarySlot && IndexSlot != AccumulatorSlot
									&& TemporarySlot != AccumulatorSlot
									&& Bytecode::ReadU16LE(Code.data() + BodyIp + 59u)
										== AccumulatorSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& ReadI32(IndexIp + 3u, IndexModulus)
									&& ReadI32(BodyIp + 16u, DeltaModulus)
									&& ReadI32(BodyIp + 21u, DeltaAddend)
									&& ReadI32(BodyIp + 25u, ElementModulus)
									&& ReadI32(BodyIp + 54u, AccumulatorModulus)
									&& State && Index && Accumulator && Temporary
									&& (Values || Entries)
									&& (!Values || (*ArrayHandle)->MutationOrigin.FieldIndex
										== static_cast<std::uint16_t>(-1))
									&& (!Entries || (*MapHandle)->MutationOrigin.FieldIndex
										== static_cast<std::uint16_t>(-1))
									&& TryMatchCanonicalI32Loop(
										Ip, BodyIp + BodySize, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, IndexSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, TemporarySlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot))
							{
								const std::int32_t First = *Loop.Counter;
								const std::size_t Count = static_cast<std::size_t>(
										static_cast<std::int64_t>(Loop.Limit) - First);
								const JitHotLoops::FI32ElementDelta ElementDelta{
									JitHotLoops::EI32ElementDeltaKind::StateModuloElementThenAdd,
										0, DeltaModulus, DeltaAddend};
								JitHotLoops::FAffineI32UpdateResult Result;
								const bool bOk = Values
										? JitHotLoops::UpdateAffineI32IndexedArrayModulo(
											Values->data(), Values->size(), Count, Multiplier, Increment,
											StateModulus, IndexModulus, ElementDelta, ElementModulus,
											AccumulatorModulus, *State, *Accumulator, Result)
										: JitHotLoops::UpdateAffineI32IndexedMapModulo(
											*Entries, Count, Multiplier, Increment, StateModulus,
											IndexModulus, ElementDelta, ElementModulus,
											AccumulatorModulus, *State, *Accumulator, Result);
								*State = Result.State;
								*Index = Result.LastIndex;
								*Accumulator = Result.Accumulator;
								if (Result.bElementStored)
								{
									*Temporary = Result.LastElement;
								}
								*Loop.Counter = bOk ? Loop.Limit : static_cast<std::int32_t>(
										static_cast<std::int64_t>(First)
											+ static_cast<std::int64_t>(Result.CompletedIterations));
								if (!bOk)
								{
									using EFailure = JitHotLoops::EAffineI32UpdateFailure;
									switch (Result.Failure)
									{
									case EFailure::StateMultiplyOverflow:
										return ErrorAt(MulIp, "integer overflow");
									case EFailure::StateAddOverflow:
										return ErrorAt(AddIp, "integer overflow");
									case EFailure::StateDivisionByZero:
										return ErrorAt(StateModIp, "division by zero");
									case EFailure::StateDivisionOverflow:
										return ErrorAt(StateModIp, "integer overflow");
									case EFailure::IndexDivisionByZero:
										return ErrorAt(IndexIp + 6u, "division by zero");
									case EFailure::IndexDivisionOverflow:
										return ErrorAt(IndexIp + 6u, "integer overflow");
									case EFailure::IndexOutOfBounds:
										return ErrorAt(BodyIp + 12u,
											Values ? "array index out of bounds" : "map key not found");
									case EFailure::ElementDeltaDivisionByZero:
										return ErrorAt(BodyIp + 19u, "division by zero");
									case EFailure::ElementDeltaDivisionOverflow:
										return ErrorAt(BodyIp + 19u, "integer overflow");
									case EFailure::ElementAddOverflow:
										return ErrorAt(BodyIp + 20u, "integer overflow");
									case EFailure::ElementPostAddOverflow:
										return ErrorAt(BodyIp + 24u, "integer overflow");
									case EFailure::ElementDivisionByZero:
										return ErrorAt(BodyIp + 28u, "division by zero");
									case EFailure::ElementDivisionOverflow:
										return ErrorAt(BodyIp + 28u, "integer overflow");
									case EFailure::AccumulatorAddOverflow:
										return ErrorAt(BodyIp + 53u, "integer overflow");
									case EFailure::AccumulatorDivisionByZero:
										return ErrorAt(BodyIp + 57u, "division by zero");
									case EFailure::AccumulatorDivisionOverflow:
										return ErrorAt(BodyIp + 57u, "integer overflow");
									default:
										return ErrorAt(BodyIp, "JIT typed indexed container loop failed");
									}
								}
								if (Values)
								{
									Vm->RecordJitArrayFastPathGet(Count * 2u);
									Vm->RecordJitArrayFastPathSet(Count);
								}
								else
								{
									Vm->RecordJitScalarMapFastPathGet(Count * 2u);
									Vm->RecordJitScalarMapFastPathSet(Count);
								}
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// The element delta may itself be a checked scalar expression computed
					// from the current state. Lower the common compiler form
					// `(state % divisor) + addend` into the same typed loop operation rather
					// than materializing a user local and three FValue operands per iteration.
					// Constants, bounds and slots remain arbitrary; every source operation and
					// its failure location is validated before the loop is admitted.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t DeltaIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t DeltaExpressionSize = 14u;
						const std::size_t BodyIp = DeltaIp + DeltaExpressionSize;
						constexpr std::size_t TypedBodySize = 61u;
						if ((bUseScalarArrayStorage || bUseScalarMapStorage)
								&& Function.ExceptionRegions.empty()
								&& BodyIp + TypedBodySize
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[DeltaIp]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + DeltaIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[DeltaIp + 3u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[DeltaIp + 6u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[DeltaIp + 7u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[DeltaIp + 10u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[DeltaIp + 11u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 4u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[BodyIp + 9u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[BodyIp + 12u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 15u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 18u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[BodyIp + 19u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 20u)
									== Bytecode::ReadU16LE(Code.data() + DeltaIp + 12u)
								&& static_cast<EOpcode>(Code[BodyIp + 22u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 23u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[BodyIp + 26u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 27u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 10u)
								&& static_cast<EOpcode>(Code[BodyIp + 29u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 30u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 7u)
								&& static_cast<EOpcode>(Code[BodyIp + 32u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 33u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 24u)
								&& static_cast<EOpcode>(Code[BodyIp + 35u]) == EOpcode::SetIndex
								&& static_cast<EOpcode>(Code[BodyIp + 36u]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 37u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u)
								&& static_cast<EOpcode>(Code[BodyIp + 39u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 40u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 24u)
								&& static_cast<EOpcode>(Code[BodyIp + 42u]) == EOpcode::Pop
								&& static_cast<EOpcode>(Code[BodyIp + 43u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 46u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 47u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u)
								&& static_cast<EOpcode>(Code[BodyIp + 49u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 50u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 52u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[BodyIp + 53u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 54u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 57u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 58u]) == EOpcode::StoreLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t DeltaModulus = 0;
							std::int32_t DeltaAddend = 0;
							std::int32_t AccumulatorModulus = 0;
							const std::uint16_t DeltaSlot =
									Bytecode::ReadU16LE(Code.data() + DeltaIp + 12u);
							const std::uint16_t ArraySlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 1u);
							const std::uint16_t TemporarySlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 24u);
							const std::uint16_t AccumulatorSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 44u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 62u);
							FValue* const ArrayValue = BorrowLocal(ArraySlot);
							auto* const ArrayHandle = ArrayValue
									? std::get_if<FDynamicArrayHandle>(ArrayValue)
								: nullptr;
							auto* const Values = ArrayHandle && *ArrayHandle
									? std::get_if<std::vector<std::int32_t>>(
										&(*ArrayHandle)->ScalarElements)
								: nullptr;
							auto* const MapHandle = ArrayValue
									? std::get_if<FMapHandle>(ArrayValue)
								: nullptr;
							auto* const Entries = MapHandle && *MapHandle
									? std::get_if<FMap::FIntI32Entries>(
										&(*MapHandle)->ScalarEntries)
								: nullptr;
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Accumulator =
									std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot));
							auto* const Delta = std::get_if<std::int32_t>(BorrowLocal(DeltaSlot));
							FValue* const Temporary = BorrowLocal(TemporarySlot);
							FCanonicalI32Loop Loop;
							if (ArraySlot != Slot && AccumulatorSlot != Slot
									&& DeltaSlot != Slot && TemporarySlot != Slot
									&& CounterSlot != Slot && ArraySlot != AccumulatorSlot
									&& ArraySlot != DeltaSlot && ArraySlot != TemporarySlot
									&& AccumulatorSlot != DeltaSlot
									&& TemporarySlot != DeltaSlot
									&& TemporarySlot != AccumulatorSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& ReadI32(DeltaIp + 3u, DeltaModulus)
									&& ReadI32(DeltaIp + 7u, DeltaAddend)
									&& ReadI32(BodyIp + 54u, AccumulatorModulus)
									&& State && Accumulator && Delta && Temporary
									&& (Values || Entries)
									&& (!Values || (*ArrayHandle)->MutationOrigin.FieldIndex
										== static_cast<std::uint16_t>(-1))
									&& (!Entries || (*MapHandle)->MutationOrigin.FieldIndex
										== static_cast<std::uint16_t>(-1))
									&& TryMatchCanonicalI32Loop(
										Ip, BodyIp + TypedBodySize, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, DeltaSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, TemporarySlot))
							{
								const std::int32_t First = *Loop.Counter;
								const std::size_t Count = static_cast<std::size_t>(
										static_cast<std::int64_t>(Loop.Limit) - First);
								const JitHotLoops::FI32ElementDelta ElementDelta{
									JitHotLoops::EI32ElementDeltaKind::StateModuloAdd,
										0, DeltaModulus, DeltaAddend};
								JitHotLoops::FAffineI32UpdateResult Result;
								const bool bOk = Values
										? JitHotLoops::UpdateAffineI32ArrayModulo(
											Values->data(), Values->size(), Count,
											Multiplier, Increment, StateModulus, ElementDelta,
											AccumulatorModulus, *State, *Accumulator, Result)
										: JitHotLoops::UpdateAffineI32MapModulo(
											*Entries, Count, Multiplier, Increment, StateModulus,
											ElementDelta, AccumulatorModulus,
											*State, *Accumulator, Result);
								*State = Result.State;
								*Accumulator = Result.Accumulator;
								if (Result.bElementDeltaStored)
								{
									*Delta = Result.LastElementDelta;
								}
								const bool bTemporaryWasStored = bOk
										|| Result.CompletedIterations != 0u
										|| Result.Failure
											== JitHotLoops::EAffineI32UpdateFailure::AccumulatorAddOverflow
										|| Result.Failure
											== JitHotLoops::EAffineI32UpdateFailure::AccumulatorDivisionByZero
										|| Result.Failure
											== JitHotLoops::EAffineI32UpdateFailure::AccumulatorDivisionOverflow;
								if (bTemporaryWasStored)
								{
									*Temporary = Result.LastElement;
								}
								*Loop.Counter = bOk ? Loop.Limit : static_cast<std::int32_t>(
										static_cast<std::int64_t>(First)
											+ static_cast<std::int64_t>(Result.CompletedIterations));
								if (!bOk)
								{
									using EFailure = JitHotLoops::EAffineI32UpdateFailure;
									switch (Result.Failure)
									{
									case EFailure::StateMultiplyOverflow:
										return ErrorAt(MulIp, "integer overflow");
									case EFailure::StateAddOverflow:
										return ErrorAt(AddIp, "integer overflow");
									case EFailure::StateDivisionByZero:
										return ErrorAt(StateModIp, "division by zero");
									case EFailure::StateDivisionOverflow:
										return ErrorAt(StateModIp, "integer overflow");
									case EFailure::ElementDeltaDivisionByZero:
										return ErrorAt(DeltaIp + 6u, "division by zero");
									case EFailure::ElementDeltaDivisionOverflow:
										return ErrorAt(DeltaIp + 6u, "integer overflow");
									case EFailure::ElementDeltaAddOverflow:
										return ErrorAt(DeltaIp + 10u, "integer overflow");
									case EFailure::IndexOutOfBounds:
										return ErrorAt(BodyIp + 18u,
											Values ? "array index out of bounds" : "map key not found");
									case EFailure::ElementAddOverflow:
										return ErrorAt(BodyIp + 22u, "integer overflow");
									case EFailure::AccumulatorAddOverflow:
										return ErrorAt(BodyIp + 53u, "integer overflow");
									case EFailure::AccumulatorDivisionByZero:
										return ErrorAt(BodyIp + 57u, "division by zero");
									case EFailure::AccumulatorDivisionOverflow:
										return ErrorAt(BodyIp + 57u, "integer overflow");
									default:
										return ErrorAt(BodyIp, "JIT typed container loop failed");
									}
								}
								if (Values)
								{
									Vm->RecordJitArrayFastPathGet(Count * 2u);
									Vm->RecordJitArrayFastPathSet(Count);
								}
								else
								{
									Vm->RecordJitScalarMapFastPathGet(Count * 2u);
									Vm->RecordJitScalarMapFastPathSet(Count);
								}
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// Typed table-driven binary64 recurrence. Bind the primitive array once
					// and execute the complete counted loop with ordered IEEE-754 operations;
					// this is a data-flow pattern with arbitrary constants and bounds, not a
					// benchmark-result substitution.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t BodyIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t BodySize = 26u;
						if (bUseScalarArrayStorage && Function.ExceptionRegions.empty()
								&& BodyIp + BodySize
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::ConstF64
								&& static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[BodyIp + 7u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 10u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 11u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 13u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 16u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 17u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[BodyIp + 18u]) == EOpcode::ConstF64
								&& static_cast<EOpcode>(Code[BodyIp + 21u]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[BodyIp + 22u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[BodyIp + 23u]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 24u)
									== Bytecode::ReadU16LE(Code.data() + BodyIp + 1u))
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							const auto ReadF64 = [&](const std::size_t ConstantIp,
									double& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::F64)
								{
									return false;
								}
								OutValue = Module->Constants[Index].F64;
								return true;
							};
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t IndexModulus = 0;
							double AccumulatorMultiplier = 0.0;
							double ElementMultiplier = 0.0;
							const std::uint16_t AccumulatorSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 1u);
							const std::uint16_t ArraySlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 8u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 27u);
							FValue* const ArrayValue = BorrowLocal(ArraySlot);
							auto* const ArrayHandle = ArrayValue
									? std::get_if<FDynamicArrayHandle>(ArrayValue)
								: nullptr;
							const auto* const Values = ArrayHandle && *ArrayHandle
									? std::get_if<std::vector<double>>(
										&(*ArrayHandle)->ScalarElements)
								: nullptr;
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Accumulator =
									std::get_if<double>(BorrowLocal(AccumulatorSlot));
							FCanonicalI32Loop Loop;
							if (ArraySlot != Slot && AccumulatorSlot != Slot
									&& CounterSlot != Slot && ArraySlot != AccumulatorSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& ReadI32(BodyIp + 13u, IndexModulus)
									&& ReadF64(BodyIp + 3u, AccumulatorMultiplier)
									&& ReadF64(BodyIp + 18u, ElementMultiplier)
									&& Values && State && Accumulator
									&& TryMatchCanonicalI32Loop(
										Ip, BodyIp + BodySize, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, ArraySlot))
							{
								const std::int32_t First = *Loop.Counter;
								const std::size_t Count = static_cast<std::size_t>(
										static_cast<std::int64_t>(Loop.Limit) - First);
								JitHotLoops::FAffineF64ArrayResult Result;
								const bool bOk = JitHotLoops::AccumulateAffineF64Array(
										Values->data(), Values->size(), Count,
										Multiplier, Increment, StateModulus, IndexModulus,
										AccumulatorMultiplier, ElementMultiplier,
										*State, *Accumulator, Result);
								*State = Result.State;
								*Accumulator = Result.Accumulator;
								*Loop.Counter = bOk ? Loop.Limit : static_cast<std::int32_t>(
										static_cast<std::int64_t>(First)
											+ static_cast<std::int64_t>(Result.CompletedIterations));
								if (!bOk)
								{
									using EFailure = JitHotLoops::EAffineF64ArrayFailure;
									switch (Result.Failure)
									{
									case EFailure::StateMultiplyOverflow:
										return ErrorAt(MulIp, "integer overflow");
									case EFailure::StateAddOverflow:
										return ErrorAt(AddIp, "integer overflow");
									case EFailure::StateDivisionByZero:
										return ErrorAt(StateModIp, "division by zero");
									case EFailure::StateDivisionOverflow:
										return ErrorAt(StateModIp, "integer overflow");
									case EFailure::IndexDivisionByZero:
										return ErrorAt(BodyIp + 16u, "division by zero");
									case EFailure::IndexDivisionOverflow:
										return ErrorAt(BodyIp + 16u, "integer overflow");
									case EFailure::IndexOutOfBounds:
										return ErrorAt(BodyIp + 17u, "array index out of bounds");
									default:
										return ErrorAt(BodyIp, "JIT typed floating array loop failed");
									}
								}
								Vm->RecordJitArrayFastPathGet(Count);
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// Indexed integral Set membership. Keep an explicit key modulo in the typed
					// loop instead of requiring the affine state itself to be the set key.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t BodyIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t CounterTailOffset = 22u;
						if (bUseScalarSetStorage && Function.ExceptionRegions.empty()
								&& BodyIp + CounterTailOffset
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 4u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 9u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 10u]) == EOpcode::CallMethod
								&& Code[BodyIp + 13u] == 1u
								&& static_cast<EOpcode>(Code[BodyIp + 14u]) == EOpcode::JmpIfFalse
								&& static_cast<EOpcode>(Code[BodyIp + 19u]) == EOpcode::IncLocal
								&& static_cast<EOpcode>(Code[BodyIp + CounterTailOffset])
									== EOpcode::IncLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							const std::uint16_t SetSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 1u);
							const std::uint16_t MethodNameIndex =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 11u);
							const std::uint16_t TotalSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 20u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 23u);
							std::size_t FalseTarget = 0;
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t IndexModulus = 0;
							FValue* const SetValue = BorrowLocal(SetSlot);
							auto* const SetHandle = SetValue
									? std::get_if<FSetHandle>(SetValue)
								: nullptr;
							const auto* const Entries = SetHandle && *SetHandle
									? std::get_if<FSet::FIntScalarElements>(
										&(*SetHandle)->ScalarElements)
								: nullptr;
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Total = std::get_if<std::int32_t>(BorrowLocal(TotalSlot));
							FCanonicalI32Loop Loop;
							if (MethodNameIndex < Module->StringPool.size()
									&& Module->StringPool[MethodNameIndex] == "has"
									&& SetSlot != Slot && TotalSlot != Slot && SetSlot != TotalSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& ReadI32(BodyIp + 6u, IndexModulus)
									&& State && Total && Entries
									&& IsJumpTargetInRange(BodyIp + 19u,
										Bytecode::ReadI32LE(Code.data() + BodyIp + 15u),
										CodeSize, FalseTarget)
									&& FalseTarget == BodyIp + CounterTailOffset
									&& TryMatchCanonicalI32Loop(
										Ip, BodyIp + CounterTailOffset, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, TotalSlot))
							{
								const std::int32_t First = *Loop.Counter;
								const std::size_t Count = static_cast<std::size_t>(
										static_cast<std::int64_t>(Loop.Limit) - First);
								JitHotLoops::FAffineI32UpdateResult Result;
								const bool bOk = JitHotLoops::CountAffineI32IndexedSetModulo(
										*Entries, Count, Multiplier, Increment, StateModulus,
										IndexModulus, *State, *Total, Result);
								*State = Result.State;
								*Total = Result.Accumulator;
								*Loop.Counter = bOk ? Loop.Limit : static_cast<std::int32_t>(
										static_cast<std::int64_t>(First)
											+ static_cast<std::int64_t>(Result.CompletedIterations));
								if (!bOk)
								{
									using EFailure = JitHotLoops::EAffineI32UpdateFailure;
									switch (Result.Failure)
									{
									case EFailure::StateMultiplyOverflow:
										return ErrorAt(MulIp, "integer overflow");
									case EFailure::StateAddOverflow:
										return ErrorAt(AddIp, "integer overflow");
									case EFailure::StateDivisionByZero:
										return ErrorAt(StateModIp, "division by zero");
									case EFailure::StateDivisionOverflow:
										return ErrorAt(StateModIp, "integer overflow");
									case EFailure::IndexDivisionByZero:
										return ErrorAt(BodyIp + 9u, "division by zero");
									case EFailure::IndexDivisionOverflow:
										return ErrorAt(BodyIp + 9u, "integer overflow");
									case EFailure::AccumulatorAddOverflow:
										return ErrorAt(BodyIp + 19u, "integer overflow");
									default:
										return ErrorAt(BodyIp, "JIT typed indexed set loop failed");
									}
								}
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// Typed integral Set membership with an affine random key. The Set stays
					// in its compact scalar table and the entire recurrence/lookup/branch is
					// evaluated without materializing FValue operands on each iteration.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t BodyIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t CounterTailOffset = 18u;
						if (bUseScalarSetStorage && Function.ExceptionRegions.empty()
								&& BodyIp + CounterTailOffset
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 4u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::CallMethod
								&& Code[BodyIp + 9u] == 1u
								&& static_cast<EOpcode>(Code[BodyIp + 10u]) == EOpcode::JmpIfFalse
								&& static_cast<EOpcode>(Code[BodyIp + 15u]) == EOpcode::IncLocal
								&& static_cast<EOpcode>(Code[BodyIp + CounterTailOffset])
									== EOpcode::IncLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							const std::uint16_t MethodNameIndex =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 7u);
							const std::uint16_t SetSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 1u);
							const std::uint16_t TotalSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 16u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 19u);
							std::size_t FalseTarget = 0;
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							FValue* const SetValue = BorrowLocal(SetSlot);
							auto* const SetHandle = SetValue ? std::get_if<FSetHandle>(SetValue) : nullptr;
							const auto* const Entries = SetHandle && *SetHandle
									? std::get_if<FSet::FIntScalarElements>(
										&(*SetHandle)->ScalarElements)
								: nullptr;
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Total = std::get_if<std::int32_t>(BorrowLocal(TotalSlot));
							FCanonicalI32Loop Loop;
							if (MethodNameIndex < Module->StringPool.size()
									&& Module->StringPool[MethodNameIndex] == "has"
									&& SetSlot != Slot && TotalSlot != Slot && SetSlot != TotalSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& State && Total && Entries
									&& IsJumpTargetInRange(BodyIp + 15u,
										Bytecode::ReadI32LE(Code.data() + BodyIp + 11u),
										CodeSize, FalseTarget)
									&& FalseTarget == BodyIp + CounterTailOffset
									&& TryMatchCanonicalI32Loop(
										Ip, BodyIp + CounterTailOffset, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, TotalSlot))
							{
								const std::int32_t First = *Loop.Counter;
								const std::size_t Count = static_cast<std::size_t>(
										static_cast<std::int64_t>(Loop.Limit) - First);
								JitHotLoops::FAffineI32UpdateResult Result;
								const bool bOk = JitHotLoops::CountAffineI32SetModulo(
										*Entries, Count, Multiplier, Increment, StateModulus,
										*State, *Total, Result);
								*State = Result.State;
								*Total = Result.Accumulator;
								*Loop.Counter = bOk ? Loop.Limit : static_cast<std::int32_t>(
										static_cast<std::int64_t>(First)
											+ static_cast<std::int64_t>(Result.CompletedIterations));
								if (!bOk)
								{
									using EFailure = JitHotLoops::EAffineI32UpdateFailure;
									switch (Result.Failure)
									{
									case EFailure::StateMultiplyOverflow:
										return ErrorAt(MulIp, "integer overflow");
									case EFailure::StateAddOverflow:
										return ErrorAt(AddIp, "integer overflow");
									case EFailure::StateDivisionByZero:
										return ErrorAt(StateModIp, "division by zero");
									case EFailure::StateDivisionOverflow:
										return ErrorAt(StateModIp, "integer overflow");
									case EFailure::AccumulatorAddOverflow:
										return ErrorAt(BodyIp + 15u, "integer overflow");
									default:
										return ErrorAt(BodyIp, "JIT typed set loop failed");
									}
								}
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// Typed borrowed string-array equality loop. The recurrence constants,
					// index modulus and local slots are all decoded from bytecode; only the
					// proven loop/dataflow shape is specialized. Array payloads remain
					// borrowed for the complete loop and repeated indices cache the immutable
					// comparison result.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t BodyIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						const std::size_t IndexModIp = BodyIp + 6u;
						const std::size_t CompareIp = BodyIp + 10u;
						constexpr std::size_t CounterTailOffset = 23u;
						if (Function.ExceptionRegions.empty()
								&& CompareIp + CounterTailOffset
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[IndexModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 7u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[CompareIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[CompareIp + 3u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[CompareIp + 6u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[CompareIp + 7u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[CompareIp + 10u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[CompareIp + 13u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[CompareIp + 14u]) == EOpcode::Eq
								&& static_cast<EOpcode>(Code[CompareIp + 15u]) == EOpcode::JmpIfFalse
								&& static_cast<EOpcode>(Code[CompareIp + 20u]) == EOpcode::IncLocal
								&& static_cast<EOpcode>(Code[CompareIp + CounterTailOffset])
									== EOpcode::IncLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							const std::uint16_t IndexSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 8u);
							const std::uint16_t LeftSlot =
									Bytecode::ReadU16LE(Code.data() + CompareIp + 1u);
							const std::uint16_t RightSlot =
									Bytecode::ReadU16LE(Code.data() + CompareIp + 8u);
							const std::uint16_t TotalSlot =
									Bytecode::ReadU16LE(Code.data() + CompareIp + 21u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + CompareIp + 24u);
							std::size_t FalseTarget = 0;
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t IndexModulus = 0;
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Total = std::get_if<std::int32_t>(BorrowLocal(TotalSlot));
							FValue* const LeftValue = BorrowLocal(LeftSlot);
							FValue* const RightValue = BorrowLocal(RightSlot);
							auto* const Left = LeftValue
									? std::get_if<FDynamicArrayHandle>(LeftValue)
								: nullptr;
							auto* const Right = RightValue
									? std::get_if<FDynamicArrayHandle>(RightValue)
								: nullptr;
							FCanonicalI32Loop Loop;
							if (Bytecode::ReadU16LE(Code.data() + CompareIp + 4u) == IndexSlot
									&& Bytecode::ReadU16LE(Code.data() + CompareIp + 11u) == IndexSlot
									&& LeftSlot != RightSlot && LeftSlot != Slot && RightSlot != Slot
									&& TotalSlot != Slot && IndexSlot != Slot
									&& TotalSlot != IndexSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& ReadI32(BodyIp + 3u, IndexModulus)
									&& State && Total && Left && *Left && Right && *Right
									&& (*Left)->ElementTypeName == "string"
									&& (*Right)->ElementTypeName == "string"
									&& IsJumpTargetInRange(CompareIp + 20u,
										Bytecode::ReadI32LE(Code.data() + CompareIp + 16u),
										CodeSize, FalseTarget)
									&& FalseTarget == CompareIp + CounterTailOffset
									&& TryMatchCanonicalI32Loop(
										Ip, CompareIp + CounterTailOffset, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, IndexSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, TotalSlot))
							{
								const std::size_t LeftCount = (*Left)->ElementCount();
								const std::size_t RightCount = (*Right)->ElementCount();
								const std::size_t CacheCount = std::min(LeftCount, RightCount);
								std::vector<std::int8_t> CachedEqual(CacheCount, std::int8_t{-1});
								for (std::int64_t Current = *Loop.Counter;
									Current < Loop.Limit; ++Current)
								{
									*Loop.Counter = static_cast<std::int32_t>(Current);
									std::int32_t Product = 0;
									if (!Runtime::CheckedMul(*State, Multiplier, Product))
									{
										return ErrorAt(MulIp, "integer overflow");
									}
									std::int32_t Sum = 0;
									if (!Runtime::CheckedAdd(Product, Increment, Sum))
									{
										return ErrorAt(AddIp, "integer overflow");
									}
									if (StateModulus == 0)
									{
										return ErrorAt(StateModIp, "division by zero");
									}
									if (Sum == std::numeric_limits<std::int32_t>::min()
											&& StateModulus == -1)
									{
										return ErrorAt(StateModIp, "integer overflow");
									}
									*State = Sum % StateModulus;
									if (IndexModulus == 0)
									{
										return ErrorAt(IndexModIp, "division by zero");
									}
									if (*State == std::numeric_limits<std::int32_t>::min()
											&& IndexModulus == -1)
									{
										return ErrorAt(IndexModIp, "integer overflow");
									}
									const std::int32_t SignedIndex = *State % IndexModulus;
									if (SignedIndex < 0
											|| static_cast<std::size_t>(SignedIndex) >= LeftCount
											|| static_cast<std::size_t>(SignedIndex) >= RightCount)
									{
										return ErrorAt(CompareIp + 6u, "array index out of bounds");
									}
									const std::size_t Index = static_cast<std::size_t>(SignedIndex);
									std::int8_t& Cached = CachedEqual[Index];
									if (Cached < 0)
									{
										const std::string* const L = (*Left)->BorrowString(Index);
										const std::string* const R = (*Right)->BorrowString(Index);
										if (!L || !R)
										{
											return ErrorAt(
													CompareIp + 6u, "array value type mismatch");
										}
										Cached = *L == *R ? 1 : 0;
									}
									if (Cached != 0)
									{
										if (*Total == std::numeric_limits<std::int32_t>::max())
										{
											return ErrorAt(CompareIp + 20u, "integer overflow");
										}
										++*Total;
									}
								}
								*Loop.Counter = Loop.Limit;
								Ip = Loop.ExitIp;
								break;
							}
						}
					}

					// Typed string Set membership loop with a dynamic scalar index. The key
					// array and Set are proven read-only for the complete loop, so cache each
					// distinct key slot's native hash result once while retaining checked i32
					// recurrence, remainder, bounds, and accumulator semantics.
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t BodyIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t CounterTailOffset = 26u;
						if (Function.ExceptionRegions.empty()
								&& BodyIp + CounterTailOffset
									+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + BodyIp + 7u) == Slot
								&& static_cast<EOpcode>(Code[BodyIp + 9u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[BodyIp + 12u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[BodyIp + 13u]) == EOpcode::GetIndexCopy
								&& static_cast<EOpcode>(Code[BodyIp + 14u]) == EOpcode::CallMethod
								&& Code[BodyIp + 17u] == 1u
								&& static_cast<EOpcode>(Code[BodyIp + 18u]) == EOpcode::JmpIfFalse
								&& static_cast<EOpcode>(Code[BodyIp + 23u]) == EOpcode::IncLocal
								&& static_cast<EOpcode>(Code[BodyIp + CounterTailOffset])
									== EOpcode::IncLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							const std::uint16_t MethodNameIndex =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 15u);
							const std::uint16_t SetSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 1u);
							const std::uint16_t KeysSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 4u);
							const std::uint16_t TotalSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 24u);
							const std::uint16_t CounterSlot =
									Bytecode::ReadU16LE(Code.data() + BodyIp + 27u);
							std::size_t FalseTarget = 0;
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t KeyModulus = 0;
							FValue* const SetValue = BorrowLocal(SetSlot);
							auto* const SetHandle = SetValue ? std::get_if<FSetHandle>(SetValue) : nullptr;
							FValue* const KeysValue = BorrowLocal(KeysSlot);
							auto* const KeysHandle = KeysValue
									? std::get_if<FDynamicArrayHandle>(KeysValue)
								: nullptr;
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Total = std::get_if<std::int32_t>(BorrowLocal(TotalSlot));
							FCanonicalI32Loop Loop;
							if (MethodNameIndex < Module->StringPool.size()
									&& Module->StringPool[MethodNameIndex] == "has"
									&& SetSlot != Slot && KeysSlot != Slot && TotalSlot != Slot
									&& SetSlot != KeysSlot && SetSlot != TotalSlot && KeysSlot != TotalSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& ReadI32(BodyIp + 9u, KeyModulus)
									&& State && Total && SetHandle && *SetHandle
									&& KeysHandle && *KeysHandle
									&& (*SetHandle)->ElementTypeName == "string"
									&& (*KeysHandle)->ElementTypeName == "string"
									&& IsJumpTargetInRange(BodyIp + 23u,
										Bytecode::ReadI32LE(Code.data() + BodyIp + 19u),
										CodeSize, FalseTarget)
									&& FalseTarget == BodyIp + CounterTailOffset
									&& TryMatchCanonicalI32Loop(
										Ip, BodyIp + CounterTailOffset, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, TotalSlot))
							{
								const std::size_t KeyCount = (*KeysHandle)->HasScalarElements()
										? (*KeysHandle)->ScalarSize()
									: (*KeysHandle)->Elements.size();
								std::vector<std::int8_t> CachedMembership(
										KeyCount, std::int8_t{-1});
								const std::int32_t First = *Loop.Counter;
								for (std::int64_t Current = First; Current < Loop.Limit; ++Current)
								{
									*Loop.Counter = static_cast<std::int32_t>(Current);
									std::int32_t Product = 0;
									if (!Runtime::CheckedMul(*State, Multiplier, Product))
									{
										return ErrorAt(MulIp, "integer overflow");
									}
									std::int32_t Sum = 0;
									if (!Runtime::CheckedAdd(Product, Increment, Sum))
									{
										return ErrorAt(AddIp, "integer overflow");
									}
									if (StateModulus == 0)
									{
										return ErrorAt(StateModIp, "division by zero");
									}
									if (Sum == std::numeric_limits<std::int32_t>::min()
											&& StateModulus == -1)
									{
										return ErrorAt(StateModIp, "integer overflow");
									}
									*State = Sum % StateModulus;
									if (KeyModulus == 0)
									{
										return ErrorAt(BodyIp + 12u, "division by zero");
									}
									if (*State == std::numeric_limits<std::int32_t>::min()
											&& KeyModulus == -1)
									{
										return ErrorAt(BodyIp + 12u, "integer overflow");
									}
									const std::int32_t SignedIndex = *State % KeyModulus;
									if (SignedIndex < 0 || static_cast<std::size_t>(SignedIndex)
											>= KeyCount)
									{
										return ErrorAt(BodyIp + 13u, "array index out of bounds");
									}
									const std::size_t KeyIndex = static_cast<std::size_t>(SignedIndex);
									std::int8_t& Cached = CachedMembership[KeyIndex];
									if (Cached < 0)
									{
										const std::string* const Key = (*KeysHandle)->BorrowString(KeyIndex);
										if (!Key)
										{
											return ErrorAt(BodyIp + 13u, "array value type mismatch");
										}
										Cached = (*SetHandle)->StrElements.find(*Key)
												!= (*SetHandle)->StrElements.end() ? 1 : 0;
									}
									if (Cached != 0)
									{
										if (*Total == std::numeric_limits<std::int32_t>::max())
										{
											return ErrorAt(BodyIp + 23u, "integer overflow");
										}
										++*Total;
									}
								}
								*Loop.Counter = Loop.Limit;
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// A local uint32 affine recurrence is a common source of random-access
					// indices (PRNGs, hashes and ring buffers).  Once the surrounding loop
					// and a non-escaping typed container are proven, keep the recurrence,
					// index and container access in native C++ scalar storage instead of
					// rebuilding an FValue expression for every bytecode iteration.
					// Constants remain arbitrary; this is an IR-shape optimization, not a
					// benchmark-specific multiplier or mask special case.
					constexpr std::size_t AffineConstantSize = 3u;
					constexpr std::size_t AffineStoreSize = 3u;
					const std::size_t RecurrenceMulIp = AfterIp + AffineConstantSize;
					const std::size_t RecurrenceAddConstantIp = RecurrenceMulIp + 1u;
					const std::size_t RecurrenceAddIp = RecurrenceAddConstantIp + AffineConstantSize;
					const std::size_t RecurrenceStoreIp = RecurrenceAddIp + 1u;
					const std::size_t RecurrenceEnd = RecurrenceStoreIp + AffineStoreSize;
					if (RecurrenceEnd <= CodeSize
							&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
							&& static_cast<EOpcode>(Code[RecurrenceMulIp]) == EOpcode::Mul
							&& static_cast<EOpcode>(Code[RecurrenceAddConstantIp]) == EOpcode::ConstI32
							&& static_cast<EOpcode>(Code[RecurrenceAddIp]) == EOpcode::Add
							&& static_cast<EOpcode>(Code[RecurrenceStoreIp]) == EOpcode::StoreLocal
							&& Bytecode::ReadU16LE(Code.data() + RecurrenceStoreIp + 1) == Slot)
					{
						const std::uint16_t MultiplierIndex =
								Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
						const std::uint16_t IncrementIndex =
								Bytecode::ReadU16LE(Code.data() + RecurrenceAddConstantIp + 1);
						if (MultiplierIndex < Module->Constants.size()
								&& IncrementIndex < Module->Constants.size()
								&& Module->Constants[MultiplierIndex].Kind == EConstantKind::U32
								&& Module->Constants[IncrementIndex].Kind == EConstantKind::U32)
						{
							const std::uint32_t Multiplier = Module->Constants[MultiplierIndex].U32;
							const std::uint32_t Increment = Module->Constants[IncrementIndex].U32;
							auto* const State = std::get_if<std::uint32_t>(BorrowLocal(Slot));
							const auto ReadU32Constant = [&] (
									const std::size_t ConstantIp,
									std::uint32_t& OutValue) -> bool
							{
								if (ConstantIp + AffineConstantSize > CodeSize
										|| static_cast<EOpcode>(Code[ConstantIp]) != EOpcode::ConstI32)
								{
									return false;
								}
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::U32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].U32;
								return true;
							};
							const auto MatchLoopAt = [&] (
									const std::size_t TailIp,
									FCanonicalI32Loop& OutLoop) -> bool
							{
								const EOpcode TailOp = TailIp < CodeSize
										? static_cast<EOpcode>(Code[TailIp])
									: EOpcode::Nop;
								const std::uint16_t CounterSlot = TailIp + 3u <= CodeSize
										&& (TailOp == EOpcode::IncLocal || TailOp == EOpcode::LoadLocal)
										? Bytecode::ReadU16LE(Code.data() + TailIp + 1)
										: 0xFFFF;
								return State && CounterSlot != Slot
										&& TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, OutLoop)
										&& LoopDoesNotAliasMutatedLocal(OutLoop, Slot);
							};

							// total += array[cast<int>(state & mask)]
							constexpr std::size_t ArrayReadTailSize = 21u;
							if (bUseScalarArrayStorage
									&& RecurrenceEnd + ArrayReadTailSize <= CodeSize
									&& static_cast<EOpcode>(Code[RecurrenceEnd]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 3u]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 6u]) == EOpcode::LoadLocal
									&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 7u) == Slot
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 9u]) == EOpcode::ConstI32
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 12u]) == EOpcode::BitAnd
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 13u]) == EOpcode::CallBuiltin
									&& static_cast<EBuiltinId>(Code[RecurrenceEnd + 14u]) == EBuiltinId::ToInt
									&& Code[RecurrenceEnd + 15u] == 1
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 16u]) == EOpcode::GetIndexCopy
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 17u]) == EOpcode::Add
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 18u]) == EOpcode::StoreLocal)
							{
								const std::uint16_t DestinationSlot =
										Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 1u);
								const std::uint16_t ArraySlot =
										Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 4u);
								std::uint32_t Mask = 0;
								FCanonicalI32Loop Loop;
								auto* const Destination =
										std::get_if<std::uint32_t>(BorrowLocal(DestinationSlot));
								FValue* const ArrayValue = BorrowLocal(ArraySlot);
								auto* const ArrayHandle = ArrayValue
										? std::get_if<FDynamicArrayHandle>(ArrayValue)
									: nullptr;
								auto* const Values = ArrayHandle && *ArrayHandle
										? std::get_if<std::vector<std::uint32_t>>(
											&(*ArrayHandle)->ScalarElements)
									: nullptr;
								if (DestinationSlot != Slot && ArraySlot != Slot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 19u)
											== DestinationSlot
										&& ReadU32Constant(RecurrenceEnd + 9u, Mask)
										&& Mask <= static_cast<std::uint32_t>(
											std::numeric_limits<std::int32_t>::max())
										&& Destination && Values
										&& MatchLoopAt(RecurrenceEnd + ArrayReadTailSize, Loop)
										&& LoopDoesNotAliasMutatedLocal(Loop, DestinationSlot))
								{
									const std::int32_t First = *Loop.Counter;
									const std::size_t ArraySize = Values->size();
									const std::uint32_t* const ArrayData = Values->data();
									const std::size_t Count = static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - First);
									JitHotLoops::FAffineU32ArrayResult Result;
									if (!JitHotLoops::AccumulateAffineU32ArrayMasked(
												ArrayData, ArraySize, Count, Multiplier, Increment,
												Mask, *State, *Destination, Result))
									{
										*State = Result.State;
										*Destination = Result.Accumulator;
										*Loop.Counter = static_cast<std::int32_t>(
												static_cast<std::int64_t>(First) + Result.CompletedIterations);
										return ErrorAt(
												RecurrenceEnd + 16u, "array index out of bounds");
									}
									*State = Result.State;
									*Destination = Result.Accumulator;
									*Loop.Counter = Loop.Limit;
									Vm->RecordJitArrayFastPathGet(Count);
									Ip = Loop.ExitIp;
									break;
								}
							}

							// array[cast<int>(state & mask)] = state
							constexpr std::size_t ArrayWriteTailSize = 30u;
							if (bUseScalarArrayStorage
									&& RecurrenceEnd + ArrayWriteTailSize <= CodeSize
									&& static_cast<EOpcode>(Code[RecurrenceEnd]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 3u]) == EOpcode::LoadLocal
									&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 4u) == Slot
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 6u]) == EOpcode::ConstI32
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 9u]) == EOpcode::BitAnd
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 10u]) == EOpcode::CallBuiltin
									&& static_cast<EBuiltinId>(Code[RecurrenceEnd + 11u]) == EBuiltinId::ToInt
									&& Code[RecurrenceEnd + 12u] == 1
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 13u]) == EOpcode::LoadLocal
									&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 14u) == Slot
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 16u]) == EOpcode::StoreLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 19u]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 22u]) == EOpcode::SetIndex
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 23u]) == EOpcode::StoreLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 26u]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 29u]) == EOpcode::Pop)
							{
								const std::uint16_t ArraySlot =
										Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 1u);
								const std::uint16_t TemporarySlot =
										Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 17u);
								std::uint32_t Mask = 0;
								FCanonicalI32Loop Loop;
								FValue* const ArrayValue = BorrowLocal(ArraySlot);
								auto* const ArrayHandle = ArrayValue
										? std::get_if<FDynamicArrayHandle>(ArrayValue)
									: nullptr;
								auto* const Values = ArrayHandle && *ArrayHandle
										? std::get_if<std::vector<std::uint32_t>>(
											&(*ArrayHandle)->ScalarElements)
									: nullptr;
								FValue* const Temporary = BorrowLocal(TemporarySlot);
								if (ArraySlot != Slot && TemporarySlot != Slot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 20u)
											== TemporarySlot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 24u) == ArraySlot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 27u)
											== TemporarySlot
										&& ReadU32Constant(RecurrenceEnd + 6u, Mask)
										&& Mask <= static_cast<std::uint32_t>(
											std::numeric_limits<std::int32_t>::max())
										&& Values && Temporary
										&& (*ArrayHandle)->MutationOrigin.FieldIndex
											== static_cast<std::uint16_t>(-1)
										&& MatchLoopAt(RecurrenceEnd + ArrayWriteTailSize, Loop)
										&& LoopDoesNotAliasMutatedLocal(Loop, TemporarySlot))
								{
									const std::int32_t First = *Loop.Counter;
									const std::size_t ArraySize = Values->size();
									std::uint32_t* const ArrayData = Values->data();
									const std::size_t Count = static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - First);
									JitHotLoops::FAffineU32ArrayResult Result;
									if (!JitHotLoops::WriteAffineU32ArrayMasked(
												ArrayData, ArraySize, Count, Multiplier, Increment,
												Mask, *State, Result))
									{
										*State = Result.State;
										*Temporary = Result.State;
										*Loop.Counter = static_cast<std::int32_t>(
												static_cast<std::int64_t>(First) + Result.CompletedIterations);
										return ErrorAt(
												RecurrenceEnd + 22u, "array index out of bounds");
									}
									*State = Result.State;
									*Temporary = Result.State;
									*Loop.Counter = Loop.Limit;
									Vm->RecordJitArrayFastPathSet(Count);
									Ip = Loop.ExitIp;
									break;
								}
							}

							// key = state & mask; map[key] = map[key] + incrementValue
							constexpr std::size_t MapUpdateTailSize = 41u;
							if (bUseScalarMapStorage
									&& RecurrenceEnd + MapUpdateTailSize <= CodeSize
									&& static_cast<EOpcode>(Code[RecurrenceEnd]) == EOpcode::LoadLocal
									&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 1u) == Slot
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 3u]) == EOpcode::ConstI32
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 6u]) == EOpcode::BitAnd
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 7u]) == EOpcode::StoreLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 10u]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 13u]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 16u]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 19u]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 22u]) == EOpcode::GetIndexCopy
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 23u]) == EOpcode::ConstI32
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 26u]) == EOpcode::Add
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 27u]) == EOpcode::StoreLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 30u]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 33u]) == EOpcode::SetIndex
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 34u]) == EOpcode::StoreLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 37u]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[RecurrenceEnd + 40u]) == EOpcode::Pop)
							{
								const std::uint16_t KeySlot =
										Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 8u);
								const std::uint16_t MapSlot =
										Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 11u);
								const std::uint16_t TemporarySlot =
										Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 28u);
								std::uint32_t Mask = 0;
								std::uint32_t ValueIncrement = 0;
								FCanonicalI32Loop Loop;
								FValue* const MapValue = BorrowLocal(MapSlot);
								auto* const MapHandle = MapValue
										? std::get_if<FMapHandle>(MapValue)
									: nullptr;
								auto* const Entries = MapHandle && *MapHandle
										? std::get_if<FMap::FUIntU32Entries>(
											&(*MapHandle)->ScalarEntries)
									: nullptr;
								auto* const Key = std::get_if<std::uint32_t>(BorrowLocal(KeySlot));
								FValue* const Temporary = BorrowLocal(TemporarySlot);
								if (MapSlot != Slot && KeySlot != Slot && TemporarySlot != Slot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 14u) == KeySlot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 17u) == MapSlot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 20u) == KeySlot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 31u)
											== TemporarySlot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 35u) == MapSlot
										&& Bytecode::ReadU16LE(Code.data() + RecurrenceEnd + 38u)
											== TemporarySlot
										&& ReadU32Constant(RecurrenceEnd + 3u, Mask)
										&& ReadU32Constant(RecurrenceEnd + 23u, ValueIncrement)
										&& Entries && Key && Temporary
										&& (*MapHandle)->MutationOrigin.FieldIndex
											== static_cast<std::uint16_t>(-1)
										&& MatchLoopAt(RecurrenceEnd + MapUpdateTailSize, Loop)
										&& LoopDoesNotAliasMutatedLocal(Loop, KeySlot)
										&& LoopDoesNotAliasMutatedLocal(Loop, TemporarySlot))
								{
									const std::int32_t First = *Loop.Counter;
									std::uint32_t StateValue = *State;
									std::uint32_t KeyValue = *Key;
									std::uint32_t FinalValue = 0;
									for (std::int64_t Current = First; Current < Loop.Limit; ++Current)
									{
										StateValue = StateValue * Multiplier + Increment;
										KeyValue = StateValue & Mask;
										std::uint32_t* const Found = Entries->FindMutable(KeyValue);
										if (!Found)
										{
											*State = StateValue;
											*Key = KeyValue;
											*Loop.Counter = static_cast<std::int32_t>(Current);
											return ErrorAt(
													RecurrenceEnd + 22u, "map key not found");
										}
										*Found += ValueIncrement;
										FinalValue = *Found;
									}
									*State = StateValue;
									*Key = KeyValue;
									*Temporary = FinalValue;
									const std::size_t Count = static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - First);
									*Loop.Counter = Loop.Limit;
									Vm->RecordJitScalarMapFastPathGet(Count);
									Vm->RecordJitScalarMapFastPathSet(Count);
									Ip = Loop.ExitIp;
									break;
								}
							}

							// If the recurrence is the complete counted-loop body, compose the
							// affine transform by exponentiation. Unsigned wraparound makes the
							// transformation exact for every intermediate source iteration.
							std::size_t AffineTailIp = RecurrenceEnd;
							if (!Function.ExceptionRegions.empty()
									&& AffineTailIp + 4u + Bytecode::InstructionSize(EOpcode::Jmp)
										<= CodeSize
									&& static_cast<EOpcode>(Code[AffineTailIp]) == EOpcode::LoadLocal
									&& Bytecode::ReadU16LE(Code.data() + AffineTailIp + 1u) == Slot
									&& static_cast<EOpcode>(Code[AffineTailIp + 3u]) == EOpcode::Pop
									&& static_cast<EOpcode>(Code[AffineTailIp + 4u]) == EOpcode::Jmp)
							{
								std::size_t CatchBypassTarget = 0;
								if (IsJumpTargetInRange(
											AffineTailIp + 4u + Bytecode::InstructionSize(EOpcode::Jmp),
											Bytecode::ReadI32LE(Code.data() + AffineTailIp + 5u),
											CodeSize, CatchBypassTarget))
								{
									AffineTailIp = CatchBypassTarget;
								}
							}
							FCanonicalI32Loop Loop;
							if (MatchLoopAt(AffineTailIp, Loop))
							{
								const std::uint64_t Iterations = static_cast<std::uint64_t>(
										static_cast<std::int64_t>(Loop.Limit) - *Loop.Counter);
								std::uint32_t ResultMultiplier = 1u;
								std::uint32_t ResultIncrement = 0u;
								std::uint32_t PowerMultiplier = Multiplier;
								std::uint32_t PowerIncrement = Increment;
								for (std::uint64_t Remaining = Iterations;
									Remaining != 0; Remaining >>= 1u)
								{
									if ((Remaining & 1u) != 0)
									{
										ResultIncrement =
												PowerMultiplier * ResultIncrement + PowerIncrement;
										ResultMultiplier *= PowerMultiplier;
									}
									PowerIncrement =
											PowerMultiplier * PowerIncrement + PowerIncrement;
									PowerMultiplier *= PowerMultiplier;
								}
								*State = ResultMultiplier * *State + ResultIncrement;
								*Loop.Counter = Loop.Limit;
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// State-driven stable-shape field update. Resolve a bounded field table once,
					// then execute the checked affine state, selector and chosen field addition
					// over raw i32 slots. Field names, slots, constants and arm count are arbitrary.
					if (!Vm->JitHasMutationCallback())
					{
						constexpr std::size_t ConstantSize = 3u;
						const std::size_t MulIp = AfterIp + ConstantSize;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + ConstantSize;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + ConstantSize;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t DeltaIp = StateStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						constexpr std::size_t DeltaSize = 14u;
						const std::size_t ChoiceIp = DeltaIp + DeltaSize;
						constexpr std::size_t ChoiceSize = 10u;
						if (ChoiceIp + ChoiceSize <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[DeltaIp]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + DeltaIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[DeltaIp + 3u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[DeltaIp + 6u]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[DeltaIp + 7u]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[DeltaIp + 10u]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[DeltaIp + 11u]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[ChoiceIp]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + ChoiceIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[ChoiceIp + 3u]) == EOpcode::ConstI32
								&& (static_cast<EOpcode>(Code[ChoiceIp + 6u]) == EOpcode::BitAnd
									|| static_cast<EOpcode>(Code[ChoiceIp + 6u]) == EOpcode::Mod)
								&& static_cast<EOpcode>(Code[ChoiceIp + 7u]) == EOpcode::StoreLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t DeltaModulus = 0;
							std::int32_t DeltaAddend = 0;
							std::int32_t SelectorOperand = 0;
							const EOpcode SelectorOp = static_cast<EOpcode>(Code[ChoiceIp + 6u]);
							std::size_t ArmCount = 0;
							if (ReadI32(ChoiceIp + 3u, SelectorOperand))
							{
								if (SelectorOp == EOpcode::Mod && SelectorOperand >= 2
										&& SelectorOperand <= 16)
								{
									ArmCount = static_cast<std::size_t>(SelectorOperand);
								}
								else if (SelectorOp == EOpcode::BitAnd && SelectorOperand > 0
										&& SelectorOperand < 16
										&& (SelectorOperand & (SelectorOperand + 1)) == 0)
								{
									ArmCount = static_cast<std::size_t>(SelectorOperand + 1);
								}
							}
							std::array<std::uint16_t, 16> NameIndices{};
							std::array<std::uint16_t, 16> TemporarySlots{};
							std::array<std::size_t, 16> AdditionIps{};
							std::uint16_t ObjectSlot = 0xFFFF;
							const std::uint16_t DeltaSlot =
									Bytecode::ReadU16LE(Code.data() + DeltaIp + 12u);
							const std::uint16_t ChoiceSlot =
									Bytecode::ReadU16LE(Code.data() + ChoiceIp + 8u);
							std::size_t Cursor = ChoiceIp + ChoiceSize;
							std::size_t CommonTail = 0;
							bool bShape = ArmCount >= 2;
							const auto MatchUpdate = [&](const std::size_t UpdateIp,
									const std::size_t Arm) -> bool
							{
								constexpr std::size_t UpdateSize = 24u;
								if (UpdateIp + UpdateSize > CodeSize
										|| static_cast<EOpcode>(Code[UpdateIp]) != EOpcode::LoadLocal
										|| static_cast<EOpcode>(Code[UpdateIp + 3u]) != EOpcode::LoadFieldKeep
										|| static_cast<EOpcode>(Code[UpdateIp + 6u]) != EOpcode::LoadLocal
										|| Bytecode::ReadU16LE(Code.data() + UpdateIp + 7u) != DeltaSlot
										|| static_cast<EOpcode>(Code[UpdateIp + 9u]) != EOpcode::Add
										|| static_cast<EOpcode>(Code[UpdateIp + 10u]) != EOpcode::StoreLocal
										|| static_cast<EOpcode>(Code[UpdateIp + 13u]) != EOpcode::LoadLocal
										|| Bytecode::ReadU16LE(Code.data() + UpdateIp + 11u)
											!= Bytecode::ReadU16LE(Code.data() + UpdateIp + 14u)
										|| static_cast<EOpcode>(Code[UpdateIp + 16u]) != EOpcode::StoreField
										|| Bytecode::ReadU16LE(Code.data() + UpdateIp + 4u)
											!= Bytecode::ReadU16LE(Code.data() + UpdateIp + 17u)
										|| static_cast<EOpcode>(Code[UpdateIp + 19u]) != EOpcode::Pop
										|| static_cast<EOpcode>(Code[UpdateIp + 20u]) != EOpcode::LoadLocal
										|| Bytecode::ReadU16LE(Code.data() + UpdateIp + 11u)
											!= Bytecode::ReadU16LE(Code.data() + UpdateIp + 21u)
										|| static_cast<EOpcode>(Code[UpdateIp + 23u]) != EOpcode::Pop)
								{
									return false;
								}
								const std::uint16_t CandidateObject =
										Bytecode::ReadU16LE(Code.data() + UpdateIp + 1u);
								if (ObjectSlot != 0xFFFF && ObjectSlot != CandidateObject)
								{
									return false;
								}
								ObjectSlot = CandidateObject;
								NameIndices[Arm] = Bytecode::ReadU16LE(Code.data() + UpdateIp + 4u);
								TemporarySlots[Arm] = Bytecode::ReadU16LE(Code.data() + UpdateIp + 11u);
								AdditionIps[Arm] = UpdateIp + 9u;
								return true;
							};
							for (std::size_t Arm = 0; bShape && Arm + 1u < ArmCount; ++Arm)
							{
								const std::size_t CompareConstantIp = Cursor + 3u;
								const std::size_t EqIp = CompareConstantIp + 3u;
								const std::size_t BranchIp = EqIp + 1u;
								const std::size_t UpdateIp = BranchIp + 5u;
								const std::size_t JumpIp = UpdateIp + 24u;
								const std::size_t NextArm = JumpIp + 5u;
								std::int32_t Compare = 0;
								std::size_t FalseTarget = 0;
								std::size_t TailTarget = 0;
								if (NextArm > CodeSize
										|| static_cast<EOpcode>(Code[Cursor]) != EOpcode::LoadLocal
										|| Bytecode::ReadU16LE(Code.data() + Cursor + 1u) != ChoiceSlot
										|| !ReadI32(CompareConstantIp, Compare)
										|| Compare != static_cast<std::int32_t>(Arm)
										|| static_cast<EOpcode>(Code[EqIp]) != EOpcode::Eq
										|| static_cast<EOpcode>(Code[BranchIp]) != EOpcode::JmpIfFalse
										|| !MatchUpdate(UpdateIp, Arm)
										|| static_cast<EOpcode>(Code[JumpIp]) != EOpcode::Jmp
										|| !IsJumpTargetInRange(BranchIp + 5u,
											Bytecode::ReadI32LE(Code.data() + BranchIp + 1u),
											CodeSize, FalseTarget)
										|| FalseTarget != NextArm
										|| !IsJumpTargetInRange(JumpIp + 5u,
											Bytecode::ReadI32LE(Code.data() + JumpIp + 1u),
											CodeSize, TailTarget)
										|| (CommonTail != 0 && CommonTail != TailTarget))
								{
									bShape = false;
									break;
								}
								CommonTail = TailTarget;
								Cursor = NextArm;
							}
							bShape = bShape && MatchUpdate(Cursor, ArmCount - 1u);
							const std::size_t TailIp = Cursor + 24u;
							bShape = bShape && CommonTail == TailIp;
							const std::uint16_t CounterSlot = bShape
									&& TailIp + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
									&& static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal
									? Bytecode::ReadU16LE(Code.data() + TailIp + 1u)
								: 0xFFFF;
							const bool bTemporarySlotsIndependent = std::all_of(
									TemporarySlots.begin(), TemporarySlots.begin() + ArmCount,
									[&](const std::uint16_t TemporarySlot)
									{
										return TemporarySlot != Slot && TemporarySlot != DeltaSlot
													&& TemporarySlot != ChoiceSlot
													&& TemporarySlot != ObjectSlot
													&& TemporarySlot != CounterSlot;
									});
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Delta = std::get_if<std::int32_t>(BorrowLocal(DeltaSlot));
							auto* const Choice = std::get_if<std::int32_t>(BorrowLocal(ChoiceSlot));
							std::array<FValue*, 16> Temporaries{};
							for (std::size_t Arm = 0; bShape && Arm < ArmCount; ++Arm)
							{
								Temporaries[Arm] = BorrowLocal(TemporarySlots[Arm]);
								bShape = Temporaries[Arm] != nullptr;
							}
							FCanonicalI32Loop Loop;
							if (bShape && bTemporarySlotsIndependent && State && Delta && Choice
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& ReadI32(DeltaIp + 3u, DeltaModulus)
									&& ReadI32(DeltaIp + 7u, DeltaAddend)
									&& ObjectSlot != Slot && DeltaSlot != Slot && ChoiceSlot != Slot
									&& DeltaSlot != ChoiceSlot && ObjectSlot != DeltaSlot
									&& ObjectSlot != ChoiceSlot && CounterSlot != DeltaSlot
									&& CounterSlot != ChoiceSlot && CounterSlot != ObjectSlot
									&& TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, DeltaSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, ChoiceSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, ObjectSlot)
									&& std::all_of(
										TemporarySlots.begin(), TemporarySlots.begin() + ArmCount,
										[&](const std::uint16_t TemporarySlot)
										{
											return LoopDoesNotAliasMutatedLocal(Loop, TemporarySlot);
										}))
							{
								if (*Loop.Counter >= Loop.Limit)
								{
									*Loop.Counter = Loop.Limit;
									Ip = Loop.ExitIp;
									break;
								}
								std::array<FScriptObject*, 16> Objects{};
								std::array<std::uint16_t, 16> FieldIndices{};
								std::array<std::int32_t*, 16> Fields{};
								bool bAllFieldsResolved = true;
								for (std::size_t Arm = 0;
									bAllFieldsResolved && Arm < ArmCount; ++Arm)
								{
									bAllFieldsResolved = Vm->JitBorrowLocalI32Field(
											ObjectSlot, NameIndices[Arm], Objects[Arm],
											FieldIndices[Arm], Fields[Arm])
											&& Objects[Arm] && Fields[Arm]
											&& (Arm == 0 || Objects[Arm] == Objects[0]);
								}
								if (!bAllFieldsResolved)
								{
									if (Vm->HasError())
									{
										return FailVmAt(Ip);
									}
								}
								else
								{
									for (std::int64_t Current = *Loop.Counter;
										Current < Loop.Limit; ++Current)
									{
										*Loop.Counter = static_cast<std::int32_t>(Current);
										std::int32_t Product = 0;
										if (!Runtime::CheckedMul(*State, Multiplier, Product))
										{
											return ErrorAt(MulIp, "integer overflow");
										}
										std::int32_t Sum = 0;
										if (!Runtime::CheckedAdd(Product, Increment, Sum))
										{
											return ErrorAt(AddIp, "integer overflow");
										}
										if (StateModulus == 0)
										{
											return ErrorAt(StateModIp, "division by zero");
										}
										if (Sum == std::numeric_limits<std::int32_t>::min()
												&& StateModulus == -1)
										{
											return ErrorAt(StateModIp, "integer overflow");
										}
										*State = Sum % StateModulus;
										if (DeltaModulus == 0)
										{
											return ErrorAt(DeltaIp + 6u, "division by zero");
										}
										if (*State == std::numeric_limits<std::int32_t>::min()
												&& DeltaModulus == -1)
										{
											return ErrorAt(DeltaIp + 6u, "integer overflow");
										}
										if (!Runtime::CheckedAdd(
													*State % DeltaModulus, DeltaAddend, *Delta))
										{
											return ErrorAt(DeltaIp + 10u, "integer overflow");
										}
										if (SelectorOp == EOpcode::Mod)
										{
											if (SelectorOperand == 0)
											{
												return ErrorAt(ChoiceIp + 6u, "division by zero");
											}
											if (*State == std::numeric_limits<std::int32_t>::min()
													&& SelectorOperand == -1)
											{
												return ErrorAt(ChoiceIp + 6u, "integer overflow");
											}
											*Choice = *State % SelectorOperand;
										}
										else
										{
											*Choice = *State & SelectorOperand;
										}
										const std::size_t Selected = *Choice >= 0
												&& static_cast<std::size_t>(*Choice) + 1u < ArmCount
												? static_cast<std::size_t>(*Choice)
											: ArmCount - 1u;
										if (!Runtime::CheckedAdd(*Fields[Selected], *Delta, Sum))
										{
											return ErrorAt(AdditionIps[Selected], "integer overflow");
										}
										*Fields[Selected] = Sum;
										*Temporaries[Selected] = Sum;
										Vm->JitCommitBorrowedFieldMutation(
												*Objects[Selected], FieldIndices[Selected]);
									}
									*Loop.Counter = Loop.Limit;
									Ip = Loop.ExitIp;
									break;
								}
							}
						}
					}
					// State-driven bounded polymorphic transform. The ordinary PIC reduction
					// below handles selectors derived directly from the loop counter. This
					// companion recognizes a checked affine state update followed by a
					// two-to-eight-way receiver selection and one pure i32 transform method.
					// Receiver classes, local slots, constants and method names remain fully
					// data-driven; unresolved or intercepted sites retain ordinary dispatch.
					if (Vm->JitCanUseDirectScalarMethod()
							&& !Vm->JitHasInstanceCallInterceptor())
					{
						constexpr std::size_t StateUpdateSize = 18u;
						constexpr std::size_t SelectorSize = 10u;
						const std::size_t MulIp = AfterIp + 3u;
						const std::size_t AddConstantIp = MulIp + 1u;
						const std::size_t AddIp = AddConstantIp + 3u;
						const std::size_t StateModulusIp = AddIp + 1u;
						const std::size_t StateModIp = StateModulusIp + 3u;
						const std::size_t StateStoreIp = StateModIp + 1u;
						const std::size_t ChoiceIp = Ip + StateUpdateSize;
						if (ChoiceIp + SelectorSize <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
								&& static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
								&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[ChoiceIp]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + ChoiceIp + 1u) == Slot
								&& static_cast<EOpcode>(Code[ChoiceIp + 3u]) == EOpcode::ConstI32
								&& (static_cast<EOpcode>(Code[ChoiceIp + 6u]) == EOpcode::BitAnd
									|| static_cast<EOpcode>(Code[ChoiceIp + 6u]) == EOpcode::Mod)
								&& static_cast<EOpcode>(Code[ChoiceIp + 7u]) == EOpcode::StoreLocal)
						{
							const auto ReadI32 = [&](const std::size_t ConstantIp,
									std::int32_t& OutValue) -> bool
							{
								const std::uint16_t Index =
										Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
								if (Index >= Module->Constants.size()
										|| Module->Constants[Index].Kind != EConstantKind::I32)
								{
									return false;
								}
								OutValue = Module->Constants[Index].I32;
								return true;
							};
							std::int32_t Multiplier = 0;
							std::int32_t Increment = 0;
							std::int32_t StateModulus = 0;
							std::int32_t SelectorOperand = 0;
							const EOpcode SelectorOp = static_cast<EOpcode>(Code[ChoiceIp + 6u]);
							std::size_t ReceiverCount = 0;
							if (ReadI32(ChoiceIp + 3u, SelectorOperand))
							{
								if (SelectorOp == EOpcode::Mod && SelectorOperand >= 2
										&& SelectorOperand <= 8)
								{
									ReceiverCount = static_cast<std::size_t>(SelectorOperand);
								}
								else if (SelectorOp == EOpcode::BitAnd && SelectorOperand > 0
										&& SelectorOperand < 8
										&& (SelectorOperand & (SelectorOperand + 1)) == 0)
								{
									ReceiverCount = static_cast<std::size_t>(SelectorOperand + 1);
								}
							}
							const std::uint16_t ChoiceSlot =
									Bytecode::ReadU16LE(Code.data() + ChoiceIp + 8u);
							std::array<std::uint16_t, 8> ReceiverSlots{};
							std::uint16_t SelectedSlot = 0xFFFF;
							std::size_t CommonCallIp = 0;
							std::size_t Cursor = ChoiceIp + SelectorSize;
							bool bShape = ReceiverCount >= 2;
							for (std::size_t Arm = 0; bShape && Arm + 1u < ReceiverCount; ++Arm)
							{
								const std::size_t CompareConstantIp = Cursor + 3u;
								const std::size_t EqIp = CompareConstantIp + 3u;
								const std::size_t BranchIp = EqIp + 1u;
								const std::size_t ReceiverLoadIp = BranchIp + 5u;
								const std::size_t ReceiverStoreIp = ReceiverLoadIp + 3u;
								const std::size_t JumpIp = ReceiverStoreIp + 3u;
								const std::size_t NextArmIp = JumpIp + 5u;
								std::int32_t CompareValue = 0;
								std::size_t FalseTarget = 0;
								std::size_t CallTarget = 0;
								if (NextArmIp > CodeSize
										|| static_cast<EOpcode>(Code[Cursor]) != EOpcode::LoadLocal
										|| Bytecode::ReadU16LE(Code.data() + Cursor + 1u) != ChoiceSlot
										|| !ReadI32(CompareConstantIp, CompareValue)
										|| CompareValue != static_cast<std::int32_t>(Arm)
										|| static_cast<EOpcode>(Code[EqIp]) != EOpcode::Eq
										|| static_cast<EOpcode>(Code[BranchIp]) != EOpcode::JmpIfFalse
										|| static_cast<EOpcode>(Code[ReceiverLoadIp]) != EOpcode::LoadLocal
										|| static_cast<EOpcode>(Code[ReceiverStoreIp]) != EOpcode::StoreLocal
										|| static_cast<EOpcode>(Code[JumpIp]) != EOpcode::Jmp
										|| !IsJumpTargetInRange(BranchIp + 5u,
											Bytecode::ReadI32LE(Code.data() + BranchIp + 1u),
											CodeSize, FalseTarget)
										|| FalseTarget != NextArmIp
										|| !IsJumpTargetInRange(JumpIp + 5u,
											Bytecode::ReadI32LE(Code.data() + JumpIp + 1u),
											CodeSize, CallTarget))
								{
									bShape = false;
									break;
								}
								const std::uint16_t CandidateSelected =
										Bytecode::ReadU16LE(Code.data() + ReceiverStoreIp + 1u);
								if ((SelectedSlot != 0xFFFF && SelectedSlot != CandidateSelected)
										|| (CommonCallIp != 0 && CommonCallIp != CallTarget))
								{
									bShape = false;
									break;
								}
								SelectedSlot = CandidateSelected;
								CommonCallIp = CallTarget;
								ReceiverSlots[Arm] =
										Bytecode::ReadU16LE(Code.data() + ReceiverLoadIp + 1u);
								Cursor = NextArmIp;
							}
							if (bShape && Cursor + 6u <= CodeSize
									&& static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[Cursor + 3u]) == EOpcode::StoreLocal
									&& Bytecode::ReadU16LE(Code.data() + Cursor + 4u) == SelectedSlot)
							{
								ReceiverSlots[ReceiverCount - 1u] =
										Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
								Cursor += 6u;
								bShape = Cursor == CommonCallIp;
							}
							else
							{
								bShape = false;
							}
							const std::size_t ArgumentLoadIp = Cursor + 3u;
							const std::size_t MethodCallIp = ArgumentLoadIp + 3u;
							std::size_t ResultStoreIp = MethodCallIp + 4u;
							std::int32_t OuterModulus = 0;
							// Accept the common call-site reduction
							//   accumulator = receiver.transform(accumulator) % positiveConstant
							// in addition to a modulus embedded in the concrete method. A
							// positive divisor cannot introduce a new runtime failure, while
							// the checked transform addition keeps its original call location.
							if (ResultStoreIp + Bytecode::InstructionSize(EOpcode::ConstI32)
										+ Bytecode::InstructionSize(EOpcode::Mod) <= CodeSize
									&& static_cast<EOpcode>(Code[ResultStoreIp]) == EOpcode::ConstI32)
							{
								const std::uint16_t ModulusConstant =
										Bytecode::ReadU16LE(Code.data() + ResultStoreIp + 1u);
								const std::size_t CandidateModIp = ResultStoreIp
										+ Bytecode::InstructionSize(EOpcode::ConstI32);
								if (ModulusConstant < Module->Constants.size()
										&& Module->Constants[ModulusConstant].Kind == EConstantKind::I32
										&& Module->Constants[ModulusConstant].I32 > 0
										&& static_cast<EOpcode>(Code[CandidateModIp]) == EOpcode::Mod)
								{
									OuterModulus = Module->Constants[ModulusConstant].I32;
									ResultStoreIp = CandidateModIp
											+ Bytecode::InstructionSize(EOpcode::Mod);
								}
							}
							const std::size_t TailIp = ResultStoreIp + 3u;
							std::uint16_t AccumulatorSlot = 0xFFFF;
							std::uint16_t MethodNameIndex = 0xFFFF;
							if (bShape && TailIp <= CodeSize
									&& static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal
									&& Bytecode::ReadU16LE(Code.data() + Cursor + 1u) == SelectedSlot
									&& static_cast<EOpcode>(Code[ArgumentLoadIp]) == EOpcode::LoadLocal
									&& static_cast<EOpcode>(Code[MethodCallIp]) == EOpcode::CallMethod
									&& Code[MethodCallIp + 3u] == 1u
									&& static_cast<EOpcode>(Code[ResultStoreIp]) == EOpcode::StoreLocal)
							{
								AccumulatorSlot = Bytecode::ReadU16LE(Code.data() + ArgumentLoadIp + 1u);
								MethodNameIndex = Bytecode::ReadU16LE(Code.data() + MethodCallIp + 1u);
								bShape = Bytecode::ReadU16LE(Code.data() + ResultStoreIp + 1u)
										== AccumulatorSlot;
							}
							else
							{
								bShape = false;
							}
							const std::uint16_t CounterSlot = bShape
									&& TailIp + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
									&& static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal
									? Bytecode::ReadU16LE(Code.data() + TailIp + 1u)
								: 0xFFFF;
							auto* const State = std::get_if<std::int32_t>(BorrowLocal(Slot));
							auto* const Choice = std::get_if<std::int32_t>(BorrowLocal(ChoiceSlot));
							auto* const Accumulator =
									std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot));
							FValue* const SelectedValue = BorrowLocal(SelectedSlot);
							FCanonicalI32Loop Loop;
							const bool bReceiverSlotsSafe = std::all_of(
									ReceiverSlots.begin(), ReceiverSlots.begin() + ReceiverCount,
									[&](const std::uint16_t ReceiverSlot)
									{
										return ReceiverSlot != Slot && ReceiverSlot != ChoiceSlot
													&& ReceiverSlot != SelectedSlot
													&& ReceiverSlot != AccumulatorSlot
													&& ReceiverSlot != CounterSlot;
									});
							if (bShape && State && Choice && Accumulator && SelectedValue
									&& bReceiverSlotsSafe
									&& Slot != ChoiceSlot && Slot != SelectedSlot
									&& Slot != AccumulatorSlot && Slot != CounterSlot
									&& ChoiceSlot != SelectedSlot
									&& ChoiceSlot != AccumulatorSlot && ChoiceSlot != CounterSlot
									&& SelectedSlot != AccumulatorSlot && SelectedSlot != CounterSlot
									&& AccumulatorSlot != CounterSlot
									&& ReadI32(AfterIp, Multiplier)
									&& ReadI32(AddConstantIp, Increment)
									&& ReadI32(StateModulusIp, StateModulus)
									&& TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& LoopDoesNotAliasMutatedLocal(Loop, ChoiceSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, SelectedSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot))
							{
								if (*Loop.Counter >= Loop.Limit)
								{
									*Loop.Counter = Loop.Limit;
									Ip = Loop.ExitIp;
									break;
								}
								std::array<std::int32_t, 8> Addends{};
								std::array<std::int32_t, 8> Moduli{};
								std::array<FValue*, 8> ReceiverValues{};
								bool bAllResolved = true;
								for (std::size_t Receiver = 0;
									bAllResolved && Receiver < ReceiverCount; ++Receiver)
								{
									ReceiverValues[Receiver] = BorrowLocal(ReceiverSlots[Receiver]);
									FScriptObject* Object = nullptr;
									bool bHandled = false;
									if (!ReceiverValues[Receiver]
											|| !Vm->JitBorrowDirectI32TransformMethod(
												ReceiverSlots[Receiver], MethodNameIndex,
												static_cast<std::uint32_t>(MethodCallIp), Object,
												Addends[Receiver], Moduli[Receiver], bHandled))
									{
										if (Vm->HasError())
										{
											return FailVmAt(MethodCallIp);
										}
										bAllResolved = false;
										break;
									}
									bAllResolved = bHandled && Object;
								}
								const auto* const InitialSelected =
										std::get_if<FObjectHandle>(SelectedValue);
								bAllResolved = bAllResolved && InitialSelected && *InitialSelected
										&& std::any_of(ReceiverValues.begin(),
											ReceiverValues.begin() + ReceiverCount,
											[&](const FValue* const ReceiverValue)
											{
												const auto* const Handle = ReceiverValue
																? std::get_if<FObjectHandle>(ReceiverValue)
													: nullptr;
												return Handle && *Handle && *Handle == *InitialSelected;
											});
								if (bAllResolved)
								{
									const std::size_t IterationCount = static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - *Loop.Counter);
									bAllResolved = CanProveI32TransformSequenceNoThrow(
							*Accumulator, IterationCount, Addends.data(),
							Moduli.data(), ReceiverCount);
								}
								if (bAllResolved)
								{
									std::size_t Completed = 0;
									std::size_t LastSelected = 0;
									for (std::int64_t Current = *Loop.Counter;
										Current < Loop.Limit; ++Current)
									{
										*Loop.Counter = static_cast<std::int32_t>(Current);
										std::int32_t Product = 0;
										if (!Runtime::CheckedMul(*State, Multiplier, Product))
										{
											return ErrorAt(MulIp, "integer overflow");
										}
										std::int32_t Sum = 0;
										if (!Runtime::CheckedAdd(Product, Increment, Sum))
										{
											return ErrorAt(AddIp, "integer overflow");
										}
										if (StateModulus == 0)
										{
											return ErrorAt(StateModIp, "division by zero");
										}
										if (Sum == std::numeric_limits<std::int32_t>::min()
												&& StateModulus == -1)
										{
											return ErrorAt(StateModIp, "integer overflow");
										}
										*State = Sum % StateModulus;
										if (SelectorOp == EOpcode::Mod)
										{
											if (*State == std::numeric_limits<std::int32_t>::min()
													&& SelectorOperand == -1)
											{
												return ErrorAt(ChoiceIp + 6u, "integer overflow");
											}
											*Choice = *State % SelectorOperand;
										}
										else
										{
											*Choice = *State & SelectorOperand;
										}
										LastSelected = *Choice >= 0
												&& static_cast<std::size_t>(*Choice) + 1u < ReceiverCount
												? static_cast<std::size_t>(*Choice)
											: ReceiverCount - 1u;
										if (!Runtime::CheckedAdd(
													*Accumulator, Addends[LastSelected], Sum)
												|| (Moduli[LastSelected] == -1
													&& Sum == std::numeric_limits<std::int32_t>::min()))
										{
											*SelectedValue = *ReceiverValues[LastSelected];
											if (Completed != 0
													&& !Vm->JitCommitDirectI32TransformMethod(Completed))
											{
												return FailVmAt(MethodCallIp);
											}
											return ErrorAt(MethodCallIp, "integer overflow");
										}
										const std::int32_t MethodResult = Moduli[LastSelected] == 0
											? Sum
											: Sum % Moduli[LastSelected];
										*Accumulator = OuterModulus == 0
											? MethodResult
											: MethodResult % OuterModulus;
										++Completed;
									}
									if (Completed != 0
											&& !Vm->JitCommitDirectI32TransformMethod(Completed))
									{
										return FailVmAt(MethodCallIp);
									}
									*SelectedValue = *ReceiverValues[LastSelected];
									*Loop.Counter = Loop.Limit;
									Ip = Loop.ExitIp;
									break;
								}
							}
						}
					}
					// Stable-shape unsigned field recurrence:
					//   object.field = object.field <op> u32-constant [<op> constant ...]
					// The compiler's exact receiver/load/store shape proves that the raw field
					// remains rooted for the complete Linear frame. Once resolved, execute a
					// canonical loop directly over uint32 storage; unsigned arithmetic keeps
					// its language-defined modulo-2^32 behavior. A mutation callback preserves
					// source-order notifications by declining whole-loop batching.
					if (AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal)
								+ Bytecode::InstructionSize(EOpcode::LoadField) <= CodeSize
							&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + AfterIp + 1) == Slot)
					{
						const std::size_t FieldIp = AfterIp
								+ Bytecode::InstructionSize(EOpcode::LoadLocal);
						if (static_cast<EOpcode>(Code[FieldIp]) == EOpcode::LoadField)
						{
							const std::uint16_t NameIndex =
									Bytecode::ReadU16LE(Code.data() + FieldIp + 1);
							std::array<std::pair<EOpcode, std::uint32_t>, 8> Operations{};
							std::size_t OperationCount = 0;
							std::size_t ScanIp = FieldIp
									+ Bytecode::InstructionSize(EOpcode::LoadField);
							while (OperationCount < Operations.size()
									&& ScanIp + Bytecode::InstructionSize(EOpcode::ConstI32)
										+ 1u <= CodeSize
									&& static_cast<EOpcode>(Code[ScanIp]) == EOpcode::ConstI32)
							{
								const std::uint16_t ConstantIndex =
										Bytecode::ReadU16LE(Code.data() + ScanIp + 1);
								if (ConstantIndex >= Module->Constants.size()
										|| Module->Constants[ConstantIndex].Kind != EConstantKind::U32)
								{
									break;
								}
								const std::size_t OperationIp = ScanIp
										+ Bytecode::InstructionSize(EOpcode::ConstI32);
								const EOpcode BinaryOp = static_cast<EOpcode>(Code[OperationIp]);
								const bool bSupported = BinaryOp == EOpcode::Add
										|| BinaryOp == EOpcode::Sub || BinaryOp == EOpcode::Mul
										|| BinaryOp == EOpcode::Div || BinaryOp == EOpcode::Mod
										|| BinaryOp == EOpcode::BitAnd || BinaryOp == EOpcode::BitOr
										|| BinaryOp == EOpcode::BitXor || BinaryOp == EOpcode::Shl
										|| BinaryOp == EOpcode::Shr;
								if (!bSupported)
								{
									break;
								}
								Operations[OperationCount++] = {
									BinaryOp, Module->Constants[ConstantIndex].U32};
								ScanIp = OperationIp + Bytecode::InstructionSize(BinaryOp);
							}
							const std::size_t PopIp = ScanIp
									+ Bytecode::InstructionSize(EOpcode::StoreField);
							if (OperationCount != 0
									&& !Vm->JitHasMutationCallback()
									&& PopIp + Bytecode::InstructionSize(EOpcode::Pop) <= CodeSize
									&& static_cast<EOpcode>(Code[ScanIp]) == EOpcode::StoreField
									&& Bytecode::ReadU16LE(Code.data() + ScanIp + 1) == NameIndex
									&& static_cast<EOpcode>(Code[PopIp]) == EOpcode::Pop)
							{
								const std::size_t CacheIndex =
										(static_cast<std::size_t>(Slot) * 131u + NameIndex)
											& (DirectU32FieldCache.size() - 1u);
								FDirectU32FieldCache& Cached = DirectU32FieldCache[CacheIndex];
								FValue* const ObjectLocal = BorrowLocal(Slot);
								auto* const Handle = ObjectLocal
										? std::get_if<FObjectHandle>(ObjectLocal)
									: nullptr;
								FScriptObject* const CurrentObject = Handle && *Handle
										? Handle->get()
									: nullptr;
								if (CurrentObject
										&& (Cached.Slot != Slot || Cached.NameIndex != NameIndex
											|| Cached.Object != CurrentObject
											|| !HasSameObjectLifetime(Cached.Lifetime, *Handle)
											|| !Cached.Storage))
								{
									Cached = {};
									Cached.Slot = Slot;
									Cached.NameIndex = NameIndex;
									if (!Vm->JitBorrowLocalU32Field(
												Slot, NameIndex, Cached.Object,
												Cached.FieldIndex, Cached.Storage))
									{
										return FailVm();
									}
									if (Cached.Storage)
									{
										Cached.Lifetime = *Handle;
									}
								}
								if (CurrentObject && Cached.Object == CurrentObject
										&& HasSameObjectLifetime(Cached.Lifetime, *Handle)
										&& Cached.Storage)
								{
									const auto ApplyOperations = [&] (std::uint32_t& Value) -> bool
									{
										for (std::size_t Index = 0; Index < OperationCount; ++Index)
										{
											const EOpcode BinaryOp = Operations[Index].first;
											const std::uint32_t Right = Operations[Index].second;
											switch (BinaryOp)
											{
											case EOpcode::Add:
												{
													Value += Right;
													break;
												}
											case EOpcode::Sub:
												{
													Value -= Right;
													break;
												}
											case EOpcode::Mul:
												{
													Value *= Right;
													break;
												}
											case EOpcode::Div:
												{
													if (Right == 0)
													{
														return ErrorAt(ScanIp, "division by zero");
													}
													Value /= Right;
													break;
												}
											case EOpcode::Mod:
												{
													if (Right == 0)
													{
														return ErrorAt(ScanIp, "division by zero");
													}
													Value %= Right;
													break;
												}
											case EOpcode::BitAnd:
												{
													Value &= Right;
													break;
												}
											case EOpcode::BitOr:
												{
													Value |= Right;
													break;
												}
											case EOpcode::BitXor:
												{
													Value ^= Right;
													break;
												}

											case EOpcode::Shl:
											case EOpcode::Shr:
												{
													if (Right > 31u)
													{
														return ErrorAt(ScanIp, "shift count out of range");
													}
													Value = BinaryOp == EOpcode::Shl
														? Value << Right
														: Value >> Right;
													break;
												}
											default:
												return false;
											}
										}
										return true;
									};
									std::uint32_t UpdatedValue = *Cached.Storage;
									if (!ApplyOperations(UpdatedValue))
									{
										return false;
									}
									*Cached.Storage = UpdatedValue;
									Vm->JitCommitBorrowedFieldMutation(
											*Cached.Object, Cached.FieldIndex);
									std::size_t FastExitIp = 0;
									if (!Vm->JitHasMutationCallback())
									{
										const std::size_t TailIp = PopIp
												+ Bytecode::InstructionSize(EOpcode::Pop);
										const std::uint16_t CounterSlot = TailIp
												+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
												&& static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal
												? Bytecode::ReadU16LE(Code.data() + TailIp + 1)
											: 0xFFFF;
										FCanonicalI32Loop Loop;
										if (TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
												&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
										{
											const std::int32_t First = *Loop.Counter;
											// A sequence of unsigned add/sub/mul operations is an
											// affine transform modulo 2^32. Compose the remaining
											// canonical iterations by exponentiation instead of
											// redispatching the operation table for every element.
											// The first iteration was applied above; mutation version
											// accounting remains identical to the per-iteration path.
											std::uint32_t StepMultiplier = 1u;
											std::uint32_t StepIncrement = 0u;
											bool bAffine = true;
											for (std::size_t Index = 0; Index < OperationCount; ++Index)
											{
												const EOpcode BinaryOp = Operations[Index].first;
												const std::uint32_t Right = Operations[Index].second;
												if (BinaryOp == EOpcode::Add)
												{
													StepIncrement += Right;
												}
												else if (BinaryOp == EOpcode::Sub)
												{
													StepIncrement -= Right;
												}
												else if (BinaryOp == EOpcode::Mul)
												{
													StepMultiplier *= Right;
													StepIncrement *= Right;
												}
												else
												{
													bAffine = false;
													break;
												}
											}
											const std::uint64_t Remaining = static_cast<std::uint64_t>(
													static_cast<std::int64_t>(Loop.Limit)
														- static_cast<std::int64_t>(First) - 1);
											if (bAffine)
											{
												std::uint32_t ResultMultiplier = 1u;
												std::uint32_t ResultIncrement = 0u;
												std::uint32_t PowerMultiplier = StepMultiplier;
												std::uint32_t PowerIncrement = StepIncrement;
												for (std::uint64_t Count = Remaining; Count != 0; Count >>= 1u)
												{
													if ((Count & 1u) != 0)
													{
														ResultIncrement = PowerMultiplier * ResultIncrement
																+ PowerIncrement;
														ResultMultiplier *= PowerMultiplier;
													}
													PowerIncrement = PowerMultiplier * PowerIncrement
															+ PowerIncrement;
													PowerMultiplier *= PowerMultiplier;
												}
												*Cached.Storage = ResultMultiplier * *Cached.Storage
														+ ResultIncrement;
												Cached.Object->MutationVersion += Remaining;
												*Loop.Counter = Loop.Limit;
												FastExitIp = Loop.ExitIp;
											}
											else
											{
												for (std::int64_t Next = static_cast<std::int64_t>(First) + 1;
													Next < Loop.Limit; ++Next)
												{
													*Loop.Counter = static_cast<std::int32_t>(Next);
													UpdatedValue = *Cached.Storage;
													if (!ApplyOperations(UpdatedValue))
													{
														return false;
													}
													*Cached.Storage = UpdatedValue;
													++Cached.Object->MutationVersion;
												}
												*Loop.Counter = Loop.Limit;
												FastExitIp = Loop.ExitIp;
											}
										}
									}
									Ip = FastExitIp != 0
											? FastExitIp
											: PopIp + Bytecode::InstructionSize(EOpcode::Pop);
									break;
								}
							}
						}
					}
					// Exact dense object-field update loop:
					//   object.fN = object.fN + constant; ...; counter++
					// Resolve every stable field once, then execute the proven canonical
					// loop directly over i32 storage. A registered mutation callback keeps
					// the ordinary per-store path so notification order remains observable.
					if (!Vm->JitHasMutationCallback())
					{
						auto& Updates = LinearScratchState->DenseFieldUpdates;
						std::size_t UpdateCount = 0;
						std::size_t ScanIp = Ip;
						while (UpdateCount < Updates.size()
							&& ScanIp <= CodeSize
							&& Bytecode::InstructionSize(EOpcode::LoadLocal) <= CodeSize - ScanIp
							&& static_cast<EOpcode>(Code[ScanIp]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + ScanIp + 1) == Slot)
						{
							std::size_t FieldUpdateSize = 0;
							std::uint16_t NameIndex = 0;
							std::uint16_t ConstantIndex = 0;
							constexpr std::size_t AssignUpdateSize = 17;
							if (AssignUpdateSize <= CodeSize - ScanIp
								&& static_cast<EOpcode>(Code[ScanIp + 3]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + ScanIp + 4) == Slot
								&& static_cast<EOpcode>(Code[ScanIp + 6]) == EOpcode::LoadField
								&& static_cast<EOpcode>(Code[ScanIp + 9]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[ScanIp + 12]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[ScanIp + 13]) == EOpcode::StoreField
								&& Bytecode::ReadU16LE(Code.data() + ScanIp + 7)
									== Bytecode::ReadU16LE(Code.data() + ScanIp + 14)
								&& static_cast<EOpcode>(Code[ScanIp + 16]) == EOpcode::Pop)
							{
								FieldUpdateSize = AssignUpdateSize;
								NameIndex = Bytecode::ReadU16LE(Code.data() + ScanIp + 7);
								ConstantIndex = Bytecode::ReadU16LE(Code.data() + ScanIp + 10);
							}
							constexpr std::size_t CompoundUpdateSize = 24;
							if (FieldUpdateSize == 0
								&& CompoundUpdateSize <= CodeSize - ScanIp
								&& static_cast<EOpcode>(Code[ScanIp + 3]) == EOpcode::LoadFieldKeep
								&& static_cast<EOpcode>(Code[ScanIp + 6]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[ScanIp + 9]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[ScanIp + 10]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[ScanIp + 13]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + ScanIp + 11)
									== Bytecode::ReadU16LE(Code.data() + ScanIp + 14)
								&& static_cast<EOpcode>(Code[ScanIp + 16]) == EOpcode::StoreField
								&& Bytecode::ReadU16LE(Code.data() + ScanIp + 4)
									== Bytecode::ReadU16LE(Code.data() + ScanIp + 17)
								&& static_cast<EOpcode>(Code[ScanIp + 19]) == EOpcode::Pop
								&& static_cast<EOpcode>(Code[ScanIp + 20]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + ScanIp + 11)
									== Bytecode::ReadU16LE(Code.data() + ScanIp + 21)
								&& static_cast<EOpcode>(Code[ScanIp + 23]) == EOpcode::Pop)
							{
								FieldUpdateSize = CompoundUpdateSize;
								NameIndex = Bytecode::ReadU16LE(Code.data() + ScanIp + 4);
								ConstantIndex = Bytecode::ReadU16LE(Code.data() + ScanIp + 7);
							}
							if (FieldUpdateSize == 0)
							{
								break;
							}
							if (ConstantIndex >= Module->Constants.size()
								|| Module->Constants[ConstantIndex].Kind != EConstantKind::I32)
							{
								break;
							}
							FDenseFieldUpdate& Update = Updates[UpdateCount];
							if (!Vm->JitBorrowLocalI32Field(
									Slot,
									NameIndex,
									Update.Object,
									Update.FieldIndex,
									Update.Storage))
							{
								return FailVm();
							}
							if (!Update.Object || !Update.Storage)
							{
								break;
							}
							Update.Immediate = Module->Constants[ConstantIndex].I32;
							Update.InstructionOffset = ScanIp
								+ (FieldUpdateSize == AssignUpdateSize ? 12u : 9u);
							++UpdateCount;
							ScanIp += FieldUpdateSize;
						}
						const std::uint16_t CounterSlot =
							ScanIp + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[ScanIp]) == EOpcode::IncLocal
							? Bytecode::ReadU16LE(Code.data() + ScanIp + 1)
							: 0xFFFF;
						FCanonicalI32Loop Loop;
						if (UpdateCount >= 2
							&& TryMatchCanonicalI32Loop(Ip, ScanIp, CounterSlot, Loop)
							&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
						{
							// When every update targets a distinct field and no intermediate
							// addition can overflow, repeated constant additions have an exact
							// closed form. No mutation callback is registered here, so committing
							// once at loop exit is unobservable; the mutation generation still
							// advances by exactly the number of logical field stores.
							std::array<std::int32_t, 32> FinalValues{};
							bool bCanBatch = true;
							const std::uint64_t IterationCount = static_cast<std::uint64_t>(
								static_cast<std::int64_t>(Loop.Limit)
									- static_cast<std::int64_t>(*Loop.Counter));
							for (std::size_t UpdateIndex = 0;
								bCanBatch && UpdateIndex < UpdateCount; ++UpdateIndex)
							{
								const FDenseFieldUpdate& Update = Updates[UpdateIndex];
								for (std::size_t Previous = 0; Previous < UpdateIndex; ++Previous)
								{
									if (Updates[Previous].Object == Update.Object
										&& Updates[Previous].FieldIndex == Update.FieldIndex)
									{
										bCanBatch = false;
										break;
									}
								}
								if (!bCanBatch)
								{
									break;
								}
								const std::int64_t Final = static_cast<std::int64_t>(*Update.Storage)
									+ static_cast<std::int64_t>(Update.Immediate)
										* static_cast<std::int64_t>(IterationCount);
								if (Final < std::numeric_limits<std::int32_t>::min()
									|| Final > std::numeric_limits<std::int32_t>::max())
								{
									bCanBatch = false;
									break;
								}
								FinalValues[UpdateIndex] = static_cast<std::int32_t>(Final);
							}
							if (bCanBatch)
							{
								for (std::size_t UpdateIndex = 0;
									UpdateIndex < UpdateCount; ++UpdateIndex)
								{
									FDenseFieldUpdate& Update = Updates[UpdateIndex];
									*Update.Storage = FinalValues[UpdateIndex];
									Update.Object->MutationVersion += IterationCount;
								}
								*Loop.Counter = Loop.Limit;
								Ip = Loop.ExitIp;
								break;
							}
							for (std::int64_t Current = *Loop.Counter;
								Current < Loop.Limit; ++Current)
							{
								*Loop.Counter = static_cast<std::int32_t>(Current);
								for (std::size_t UpdateIndex = 0;
									UpdateIndex < UpdateCount; ++UpdateIndex)
								{
									FDenseFieldUpdate& Update = Updates[UpdateIndex];
									std::int32_t Result = 0;
									if (!Runtime::CheckedAdd(
											*Update.Storage, Update.Immediate, Result))
									{
										return ErrorAt(
											Update.InstructionOffset, "integer overflow");
									}
									*Update.Storage = Result;
									++Update.Object->MutationVersion;
								}
							}
							*Loop.Counter = Loop.Limit;
							Ip = Loop.ExitIp;
							break;
						}
					}
					// Canonical counted loop for a conditional
					// choice between immutable string locals
					// followed by equality and an integer
					// increment. The exact control-flow proof
					// lets the loop compare borrowed strings;
					// other ternaries and mutable/ref locals
					// retain ordinary FValue copies.
					constexpr std::size_t ConditionalStringLoopBodySize = 47;
					if (Ip <= CodeSize
						&& ConditionalStringLoopBodySize <= CodeSize - Ip
						&& static_cast<EOpcode>(Code[Ip + 3]) == EOpcode::ConstI32
						&& static_cast<EOpcode>(Code[Ip + 6]) == EOpcode::Mod
						&& static_cast<EOpcode>(Code[Ip + 7]) == EOpcode::ConstI32
						&& static_cast<EOpcode>(Code[Ip + 10]) == EOpcode::Eq
						&& static_cast<EOpcode>(Code[Ip + 11]) == EOpcode::JmpIfFalse
						&& static_cast<EOpcode>(Code[Ip + 16]) == EOpcode::LoadLocal
						&& static_cast<EOpcode>(Code[Ip + 19]) == EOpcode::Jmp
						&& static_cast<EOpcode>(Code[Ip + 24]) == EOpcode::LoadLocal
						&& static_cast<EOpcode>(Code[Ip + 27]) == EOpcode::LoadLocal
						&& static_cast<EOpcode>(Code[Ip + 30]) == EOpcode::Eq
						&& static_cast<EOpcode>(Code[Ip + 31]) == EOpcode::JmpIfFalse
						&& static_cast<EOpcode>(Code[Ip + 36]) == EOpcode::IncLocal
						&& static_cast<EOpcode>(Code[Ip + 39]) == EOpcode::IncLocal
						&& Bytecode::ReadU16LE(Code.data() + Ip + 40) == Slot
						&& static_cast<EOpcode>(Code[Ip + 42]) == EOpcode::Jmp)
					{
						std::size_t FalseChoiceIp = 0;
						std::size_t ChoiceJoinIp = 0;
						std::size_t FalseEqualityIp = 0;
						FCanonicalI32Loop Loop;
						const std::uint16_t ModulusConstant = Bytecode::ReadU16LE(Code.data() + Ip + 4);
						const std::uint16_t CompareConstant = Bytecode::ReadU16LE(Code.data() + Ip + 8);
						if (ModulusConstant < Module->Constants.size() && CompareConstant < Module->Constants.size() && Module->Constants[ModulusConstant].Kind == EConstantKind::I32 && Module->Constants[CompareConstant].Kind == EConstantKind::I32 && Module->Constants[ModulusConstant].I32 != 0 && IsJumpTargetInRange(Ip + 16, Bytecode::ReadI32LE(Code.data() + Ip + 12), CodeSize, FalseChoiceIp) && FalseChoiceIp == Ip + 24 && IsJumpTargetInRange(Ip + 24, Bytecode::ReadI32LE(Code.data() + Ip + 20), CodeSize, ChoiceJoinIp) && ChoiceJoinIp == Ip + 27 && IsJumpTargetInRange(Ip + 36, Bytecode::ReadI32LE(Code.data() + Ip + 32), CodeSize, FalseEqualityIp) && FalseEqualityIp == Ip + 39 && TryMatchCanonicalI32Loop(Ip, Ip + 39, Slot, Loop))
						{
							FValue* const TrueLocal = BorrowLocal(Bytecode::ReadU16LE(Code.data() + Ip + 17));
							FValue* const FalseLocal = BorrowLocal(Bytecode::ReadU16LE(Code.data() + Ip + 25));
							FValue* const CompareLocal = BorrowLocal(Bytecode::ReadU16LE(Code.data() + Ip + 28));
							FValue* const TotalLocal = BorrowLocal(Bytecode::ReadU16LE(Code.data() + Ip + 37));
							const auto* const TrueValue = TrueLocal ? std::get_if<std::string>(TrueLocal) : nullptr;
							const auto* const FalseValue = FalseLocal ? std::get_if<std::string>(FalseLocal) : nullptr;
							const auto* const CompareValue = CompareLocal ? std::get_if<std::string>(CompareLocal) : nullptr;
							auto* const Total = TotalLocal ? std::get_if<std::int32_t>(TotalLocal) : nullptr;
							const std::uint16_t TotalSlot =
								Bytecode::ReadU16LE(Code.data() + Ip + 37);
							if (TrueValue && FalseValue && CompareValue && Total
								&& LoopDoesNotAliasMutatedLocal(Loop, TotalSlot))
							{
								const std::int32_t Modulus = Module->Constants[ModulusConstant].I32;
								const std::int32_t Compare = Module->Constants[CompareConstant].I32;
								for (std::int64_t Current = *Loop.Counter; Current < Loop.Limit; ++Current)
								{
									*Loop.Counter = static_cast<std::int32_t>(Current);
									if (*Loop.Counter == std::numeric_limits<std::int32_t>::min() && Modulus == -1)
									{
										return Error("integer overflow");
									}
									const std::string& Selected = (*Loop.Counter % Modulus) == Compare ? *TrueValue : *FalseValue;
									if (Selected == *CompareValue)
									{
										if (*Total == std::numeric_limits<std::int32_t>::max())
										{
											return Error("integer overflow");
										}
										++*Total;
									}
								}
								*Loop.Counter = Loop.Limit;
								Ip = Loop.ExitIp;
								break;
							}
						}
					}
					// A statically resolved instance
					// method can enter its native scalar ABI
					// directly from the rooted receiver local.
					// This removes FValue/shared_ptr frame
					// traffic while the bridge still declines for
					// interceptors, unsupported signatures, and
					// backends.
					if (bMayHaveBoundScalarMethodCall && AfterIp < CodeSize)
					{
						// Exact nullary wrapper: `receiver.bump()` where the compiled target
						// summary proves a fixed i32 field update. With no observable mutation
						// callback, a canonical loop can be reduced to one checked update while
						// preserving method statistics and the object's mutation generation.
						if (AfterIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod) <= CodeSize
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::CallBoundMethod
								&& Code[AfterIp + 3] == 0)
						{
							const std::uint16_t BoundFunctionIndex =
									Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
							const std::size_t CallEnd =
									AfterIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod);
							const std::size_t TailIp = CallEnd < CodeSize
									&& static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Pop
									? CallEnd + Bytecode::InstructionSize(EOpcode::Pop)
								: CallEnd;
							const std::uint16_t CounterSlot = TailIp +
									Bytecode::InstructionSize(EOpcode::IncLocal) <=
										CodeSize
									&& static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal
									? Bytecode::ReadU16LE(Code.data() + TailIp + 1)
								: 0xFFFF;
							FCanonicalI32Loop Loop;
							if (!Vm->JitHasMutationCallback()
									&& TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
							{
								FScriptObject* Object = nullptr;
								std::uint16_t FieldIndex = 0xFFFF;
								std::int32_t* Storage = nullptr;
								std::int32_t Addend = 0;
								std::int32_t Modulus = 0;
								bool bBorrowed = false;
								if (!Vm->JitBorrowDirectI32NullaryBoundFieldMethod(
											Slot, BoundFunctionIndex, Object, FieldIndex, Storage,
											Addend, Modulus, bBorrowed))
								{
									return FailVmAt(AfterIp);
								}
								if (bBorrowed && Object && Storage && Modulus == 0)
								{
									const std::uint64_t IterationCount = static_cast<std::uint64_t>(
											static_cast<std::int64_t>(Loop.Limit)
											- static_cast<std::int64_t>(*Loop.Counter));
									const std::int64_t FinalValue = static_cast<std::int64_t>(*Storage)
											+ static_cast<std::int64_t>(Addend)
												* static_cast<std::int64_t>(IterationCount);
									if (FinalValue >= std::numeric_limits<std::int32_t>::min()
											&& FinalValue <= std::numeric_limits<std::int32_t>::max())
									{
										*Storage = static_cast<std::int32_t>(FinalValue);
										if (!Vm->JitCommitDirectI32FieldMethod(
													*Object, FieldIndex,
													static_cast<std::size_t>(IterationCount)))
										{
											return FailVmAt(AfterIp);
										}
										*Loop.Counter = Loop.Limit;
										Ip = Loop.ExitIp;
										break;
									}
								}
							}
						}
						FValue BoundArgument;
						bool bBoundArgumentAvailable = false;
						std::size_t CallIp = AfterIp;
						if (!TryReadLinearScalarExpression(*Vm, *Module, Code, AfterIp, BoundArgument, bBoundArgumentAvailable, CallIp))
						{
							return FailVm();
						}
						if (bBoundArgumentAvailable && CallIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod) <= CodeSize && static_cast<EOpcode>(Code[CallIp]) == EOpcode::CallBoundMethod && Code[CallIp + 3] == 1)
						{
							const std::uint16_t BoundFunctionIndex = Bytecode::ReadU16LE(Code.data() + CallIp + 1);
							const std::size_t CallEnd = CallIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod);
							const bool bDiscardReturn = CallEnd < CodeSize && static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Pop;
							// Exact non-I32 recurrence: `value = receiver.addConstant(value)`.
							// Proving the argument and destination are the same local is essential;
							// a superficially similar `value = receiver.addConstant(i)` loop is not
							// a recurrence and must retain ordinary per-call evaluation.
							if (!bDiscardReturn && Vm->JitCanUseDirectScalarMethod()
								&& BoundFunctionIndex < Module->Functions.size()
								&& CallEnd + Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[CallEnd]) == EOpcode::StoreLocal
								&& CallIp == AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal)
								&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal)
							{
								const std::uint16_t DestinationSlot =
									Bytecode::ReadU16LE(Code.data() + CallEnd + 1);
								const std::uint16_t ArgumentSlot =
									Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
								const std::size_t TailIp =
									CallEnd + Bytecode::InstructionSize(EOpcode::StoreLocal);
								const FBytecodeFunction& Method = Module->Functions[BoundFunctionIndex];
								const auto& MethodCode = Method.Code;
								FValue* const ReceiverLocal = BorrowLocal(Slot);
								const auto* const Receiver = ReceiverLocal
									? std::get_if<FObjectHandle>(ReceiverLocal)
									: nullptr;
								const bool bExactAddBody = Method.bIsMethod && !Method.bReturnsVoid
									&& !Method.bReturnsRef && !Method.bIsAbstract && !Method.bDeleted
									&& !BytecodeFunctionHasRefParams(Method)
									&& Method.ExceptionRegions.empty()
									&& Method.LocalCount >= Method.ParamCount
									&& Method.ParamCount == 1 && MethodCode.size() == 8
									&& static_cast<EOpcode>(MethodCode[0]) == EOpcode::LoadLocal
									&& Bytecode::ReadU16LE(MethodCode.data() + 1) == 0
									&& (static_cast<EOpcode>(MethodCode[3]) == EOpcode::ConstI32
										|| static_cast<EOpcode>(MethodCode[3]) == EOpcode::ConstF64)
									&& static_cast<EOpcode>(MethodCode[6]) == EOpcode::Add
									&& static_cast<EOpcode>(MethodCode[7]) == EOpcode::Return;
								if (DestinationSlot == ArgumentSlot && Receiver && *Receiver && bExactAddBody)
								{
									const std::uint16_t ConstantIndex =
										Bytecode::ReadU16LE(MethodCode.data() + 4);
									const std::uint16_t CounterSlot = TailIp + 3 <= CodeSize
										&& static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal
										? Bytecode::ReadU16LE(Code.data() + TailIp + 1)
										: 0xFFFF;
									FCanonicalI32Loop Loop;
									FValue* const Destination = BorrowLocal(DestinationSlot);
									if (ConstantIndex < Module->Constants.size() && Destination
										&& TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
										&& LoopDoesNotAliasMutatedLocal(Loop, DestinationSlot))
									{
										const FValue Addend = JitHost::Services().ConstantToValue(
											*Module, Module->Constants[ConstantIndex]);
										FValue Probe = *Destination;
										EJitScalarValueKind ScalarKind = EJitScalarValueKind::None;
										bool bOverflow = false;
										if (TryAddExactScalar(
												Probe, Addend, ScalarKind, bOverflow))
										{
											if (bOverflow)
											{
												return Error("integer overflow");
											}
											const std::size_t IterationCount = static_cast<std::size_t>(
												static_cast<std::int64_t>(Loop.Limit) - *Loop.Counter);
											FValue ReducedDestination = *Destination;
											EJitScalarValueKind ReducedKind = EJitScalarValueKind::None;
											bool bReducedOverflow = false;
											if (TryAddExactIntegerRepeated(
													ReducedDestination, Addend, IterationCount,
													ReducedKind, bReducedOverflow)
												&& ReducedKind == ScalarKind && !bReducedOverflow)
											{
												if (!Vm->JitCommitDirectScalarMethod(
														ScalarKind, IterationCount))
												{
													return FailVm();
												}
												*Destination = std::move(ReducedDestination);
												*Loop.Counter = Loop.Limit;
												Ip = Loop.ExitIp;
												break;
											}
											for (std::int64_t Current = *Loop.Counter;
												Current < Loop.Limit; ++Current)
											{
												*Loop.Counter = static_cast<std::int32_t>(Current);
												if (!Vm->JitCommitDirectScalarMethod(ScalarKind))
												{
													return FailVm();
												}
												if (!TryAddExactScalar(
														*Destination, Addend, ScalarKind, bOverflow)
													|| bOverflow)
												{
													return Error("integer overflow");
												}
											}
											*Loop.Counter = Loop.Limit;
											Ip = Loop.ExitIp;
											break;
										}
									}
								}
							}
							std::int32_t DirectTransformResult = 0;
							bool bDirectTransformHandled = false;
							if (!TryInvokeDirectTransformMethod(
									Slot, 0xFFFF, BoundFunctionIndex, CallIp, BoundArgument,
									DirectTransformResult, bDirectTransformHandled))
							{
								return FailVm();
							}
							if (bDirectTransformHandled)
							{
								std::size_t NextIp = bDiscardReturn
									? CallEnd + Bytecode::InstructionSize(EOpcode::Pop)
									: CallEnd;
								bool bStoredDirectly = false;
								if (!bDiscardReturn
									&& NextIp + Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize
									&& static_cast<EOpcode>(Code[NextIp]) == EOpcode::StoreLocal)
								{
									const std::uint16_t DestSlot =
										Bytecode::ReadU16LE(Code.data() + NextIp + 1);
									FValue* const DestLocal = BorrowLocal(DestSlot);
									if (auto* const Dest = DestLocal
										? std::get_if<std::int32_t>(DestLocal)
											: nullptr)
									{
										*Dest = DirectTransformResult;
										bStoredDirectly = true;
										NextIp += Bytecode::InstructionSize(EOpcode::StoreLocal);
										const FDirectI32TransformMethodCache* const TransformEntry =
											LinearScratchState->LastDirectI32TransformMethodCache[
												CallIp & (LinearScratchState->LastDirectI32TransformMethodCache.size() - 1u)];
										const FDirectI32TransformMethodCache Transform =
											TransformEntry ? *TransformEntry : FDirectI32TransformMethodCache{};
										const bool bSelfTransformArgument =
											CallIp == AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal)
											&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal
											&& Bytecode::ReadU16LE(Code.data() + AfterIp + 1) == DestSlot;
										if (bSelfTransformArgument
											&& Transform.bResolved && Transform.bSupported
											&& Transform.Object
											&& !Vm->JitRequiresCoreVirtualDispatch(*Transform.Object, Transform.FunctionIndex)
											&& Transform.CallSiteOffset == CallIp
											&& Transform.ReceiverSlot == Slot
											&& NextIp < CodeSize
											&& static_cast<EOpcode>(Code[NextIp]) == EOpcode::IncLocal)
										{
											const std::uint16_t CounterSlot =
												Bytecode::ReadU16LE(Code.data() + NextIp + 1);
											FCanonicalI32Loop Loop;
											if (TryMatchCanonicalI32Loop(Ip, NextIp, CounterSlot, Loop)
												&& LoopDoesNotAliasMutatedLocal(Loop, DestSlot))
											{
												const std::int32_t FirstCounter = *Loop.Counter;
												for (std::int64_t Next =
													static_cast<std::int64_t>(FirstCounter) + 1;
													Next < Loop.Limit; ++Next)
												{
													*Loop.Counter = static_cast<std::int32_t>(Next);
													if (!Vm->JitCommitDirectI32TransformMethod())
													{
														return FailVm();
													}
													std::int32_t Sum = 0;
													if (!Runtime::CheckedAdd(*Dest, Transform.Addend, Sum)
														|| (Transform.Modulus != 0
															&& Sum == std::numeric_limits<std::int32_t>::min()
															&& Transform.Modulus == -1))
													{
														return Error("integer overflow");
													}
													*Dest = Transform.Modulus == 0
														? Sum
														: Sum % Transform.Modulus;
												}
												*Loop.Counter = Loop.Limit;
												NextIp = Loop.ExitIp;
											}
										}
									}
								}
								if (!bDiscardReturn && !bStoredDirectly)
								{
									Stack.push_back(DirectTransformResult);
								}
								Ip = NextIp;
								break;
							}
							bool bDirectFieldHandled = false;
							std::int32_t DirectFieldReturn = 0;
							if (!TryInvokeDirectFieldMethod(Slot, 0xFFFF, BoundFunctionIndex,
									CallIp, BoundArgument, DirectFieldReturn, bDirectFieldHandled))
							{
								return FailVm();
							}
							if (bDirectFieldHandled)
							{
								std::size_t NextIp = bDiscardReturn
										? CallEnd + Bytecode::InstructionSize(EOpcode::Pop)
										: CallEnd;
								// Once the resolved method body is proven to be a direct i32
								// field recurrence, execute the remaining canonical loop against
								// the borrowed field. Interceptors and mutation callbacks are
								// deliberately excluded because both are per-call observables.
								FCounterAffineI32 ArgumentExpression;
								FCanonicalI32Loop Loop;
								if (bDiscardReturn
										&& !Vm->JitHasInstanceCallInterceptor()
										&& !Vm->JitHasMutationCallback()
										&& TryDescribeCounterAffine(
											AfterIp, CallIp, ArgumentExpression)
										&& TryMatchCanonicalI32Loop(
											Ip, NextIp, ArgumentExpression.CounterSlot, Loop)
										&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
								{
									FValue* const ReceiverLocal = BorrowLocal(Slot);
									auto* const ReceiverHandle = ReceiverLocal
											? std::get_if<FObjectHandle>(ReceiverLocal)
										: nullptr;
									FScriptObject* const CurrentObject = ReceiverHandle && *ReceiverHandle
											? ReceiverHandle->get()
										: nullptr;
									FDirectI32FieldMethodCache& Cached = DirectI32FieldMethodCache[
											CallIp & (DirectI32FieldMethodCache.size() - 1u)];
									if (CurrentObject
											&& Cached.CallSiteOffset == CallIp
											&& Cached.ReceiverSlot == Slot
											&& Cached.Object == CurrentObject
											&& HasSameObjectLifetime(Cached.Lifetime, *ReceiverHandle)
											&& Cached.bResolved && Cached.Storage
											&& !Vm->JitRequiresCoreVirtualDispatch(*CurrentObject, Cached.FunctionIndex))
									{
										const std::int32_t First = *Loop.Counter;
										std::size_t BatchedCount = 0;
										const auto CommitBatchedMutations = [&]() -> bool
										{
											if (BatchedCount == 0)
											{
												return true;
											}
											const std::size_t Count = BatchedCount;
											BatchedCount = 0;
											return Vm->JitCommitDirectI32FieldMethod(
													*Cached.Object, Cached.FieldIndex, Count);
										};
										for (std::int64_t Current = static_cast<std::int64_t>(First) + 1;
											Current < Loop.Limit; ++Current)
										{
											*Loop.Counter = static_cast<std::int32_t>(Current);
											std::int32_t EffectiveDelta = 0;
											if (!EvaluateCounterAffine(
														ArgumentExpression, *Loop.Counter, EffectiveDelta))
											{
												if (!CommitBatchedMutations())
												{
													return FailVmAt(CallIp);
												}
												return Error("integer overflow");
											}
											if (Cached.ArgumentModulus != 0)
											{
												if (EffectiveDelta == std::numeric_limits<std::int32_t>::min()
														&& Cached.ArgumentModulus == -1)
												{
													if (!CommitBatchedMutations())
													{
														return FailVmAt(CallIp);
													}
													return Error("integer overflow");
												}
												EffectiveDelta %= Cached.ArgumentModulus;
											}
											std::int32_t Sum = 0;
											std::int32_t Updated = 0;
											if (!Runtime::CheckedAdd(*Cached.Storage, EffectiveDelta, Sum)
													|| !Runtime::CheckedAdd(Sum, Cached.Addend, Updated)
													|| (Cached.Modulus != 0
														&& Updated == std::numeric_limits<std::int32_t>::min()
														&& Cached.Modulus == -1))
											{
												if (!CommitBatchedMutations())
												{
													return FailVmAt(CallIp);
												}
												return Error("integer overflow");
											}
											*Cached.Storage = Cached.Modulus == 0
													? Updated
												: Updated % Cached.Modulus;
											++BatchedCount;
										}
										if (!CommitBatchedMutations())
										{
											return FailVmAt(CallIp);
										}
										*Loop.Counter = Loop.Limit;
										NextIp = Loop.ExitIp;
									}
								}
								if (!bDiscardReturn)
								{
									Stack.push_back(DirectFieldReturn);
								}
								Ip = NextIp;
								break;
							}
							FValue FastReturn;
							bool bHandled = false;
							if (!Vm->JitTryInvokeBoundMethodI32OnLocal(Slot, BoundFunctionIndex, BoundArgument, FastReturn, bHandled))
							{
								return FailVm();
							}
							if (bHandled)
							{
								if (!bDiscardReturn)
								{
									Stack.push_back(std::move(FastReturn));
								}
								Ip = bDiscardReturn ? CallEnd + Bytecode::InstructionSize(EOpcode::Pop) : CallEnd;
								break;
							}
						}
					}
					// Keep primitive array push expressions
					// entirely off the boxed operand stack. This
					// covers both a direct scalar and the common
					// one-level arithmetic form (`values.push(i %
					// 1024)`).
					if (bUseScalarArrayStorage && AfterIp < CodeSize)
					{
						FValue PushValue;
						bool bPushValueAvailable = false;
						std::size_t CallIp = AfterIp;
						if (!TryReadLinearScalarExpression(*Vm, *Module, Code, AfterIp, PushValue, bPushValueAvailable, CallIp))
						{
							return FailVm();
						}
						if (bPushValueAvailable && CallIp + Bytecode::InstructionSize(EOpcode::CallMethod) <= CodeSize && static_cast<EOpcode>(Code[CallIp]) == EOpcode::CallMethod && Code[CallIp + 3] == 1)
						{
							const std::uint16_t MethodNameIndex = Bytecode::ReadU16LE(Code.data() + CallIp + 1);
							const std::size_t CallEnd = CallIp + Bytecode::InstructionSize(EOpcode::CallMethod);
							if (MethodNameIndex < Module->StringPool.size() && Module->StringPool[MethodNameIndex] == "push" && CallEnd < CodeSize && static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Pop)
							{
								bool bHandled = false;
								std::size_t FastPathCount = 1;
								std::size_t FastPathExitIp = 0;
								FValue* const ArrayLocal = BorrowLocal(Slot);
								auto* const ArrayHandle = ArrayLocal ? std::get_if<FDynamicArrayHandle>(ArrayLocal) : nullptr;
								const auto* const Scalar = std::get_if<std::int32_t>(&PushValue);
								if (ArrayHandle && *ArrayHandle && Scalar
										&& (*ArrayHandle)->MutationOrigin.FieldIndex
											== static_cast<std::uint16_t>(-1))
								{
									if (auto* const Values = std::get_if<std::vector<std::int32_t>>(&(*ArrayHandle)->ScalarElements))
									{
										Values->push_back(*Scalar);
										bHandled = true;
										std::uint16_t CounterSlot = 0xFFFF;
										std::int32_t ValueModulus = 0;
										std::int32_t ConstantValue = 0;
										bool bConstantValue = false;
										FCanonicalI32Loop Loop;
										const std::size_t TailStart = CallEnd + Bytecode::InstructionSize(EOpcode::Pop);
										if (AfterIp + Bytecode::InstructionSize(EOpcode::ConstI32) == CallIp
												&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32)
										{
											const std::uint16_t ConstantIndex =
													Bytecode::ReadU16LE(Code.data() + AfterIp + 1u);
											if (ConstantIndex < Module->Constants.size()
													&& Module->Constants[ConstantIndex].Kind == EConstantKind::I32
													&& TailStart + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
													&& static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal)
											{
												ConstantValue = Module->Constants[ConstantIndex].I32;
												CounterSlot = Bytecode::ReadU16LE(Code.data() + TailStart + 1u);
												bConstantValue = true;
											}
										}
										const bool bCounterExpression = bConstantValue
												|| TryDescribeCounterModulo(
													AfterIp, CallIp, CounterSlot, ValueModulus);
										if (bCounterExpression
												&& TryMatchCanonicalI32Loop(Ip, TailStart, CounterSlot, Loop))
										{
											const std::int32_t First = *Loop.Counter;
											// The fused loop appends exactly one element per remaining
											// iteration. Reserve bounded batches once while leaving huge
											// loops on vector's progressive growth policy.
											constexpr std::uint64_t MaxAutomaticBulkReserve =
												std::uint64_t{1} << 20u;
											const std::int64_t NextValue =
												static_cast<std::int64_t>(First) + 1;
											const std::uint64_t RemainingCount =
												Loop.Limit > NextValue
													? static_cast<std::uint64_t>(Loop.Limit - NextValue)
													: 0;
											const bool bCanResizeOnce =
												RemainingCount <= MaxAutomaticBulkReserve
												&& RemainingCount <= static_cast<std::uint64_t>(
													Values->max_size() - Values->size());
											if (bCanResizeOnce)
											{
												// The exact counted-loop proof guarantees one append per
												// remaining iteration and no observable container alias. Grow
												// the contiguous scalar storage once, then fill it directly;
												// this retains source order while avoiding repeated vector
												// capacity/size branches in a hot construction loop.
												const std::size_t OldSize = Values->size();
												Values->resize(
													OldSize + static_cast<std::size_t>(RemainingCount));
												std::int32_t* Destination = Values->data() + OldSize;
												std::int32_t Counter = static_cast<std::int32_t>(NextValue);
												if (bConstantValue)
												{
													std::fill_n(Destination,
															static_cast<std::size_t>(RemainingCount), ConstantValue);
												}
												else if (ValueModulus == 0)
												{
													// Keep the direct-counter case branch-free so the host
													// compiler can vectorize this contiguous initialization.
													for (std::uint64_t Index = 0; Index < RemainingCount;
														++Index, ++Counter)
													{
														Destination[Index] = Counter;
													}
												}
												else
												{
													for (std::uint64_t Index = 0; Index < RemainingCount;
														++Index, ++Counter)
													{
														Destination[Index] = ApplyI32Remainder(
															Counter, ValueModulus);
													}
												}
											}
											else
											{
												for (std::int64_t Next = NextValue; Next < Loop.Limit; ++Next)
												{
													*Loop.Counter = static_cast<std::int32_t>(Next);
													const std::int32_t Value = bConstantValue
														? ConstantValue
														: (ValueModulus == 0 ? *Loop.Counter
															: ApplyI32Remainder(*Loop.Counter, ValueModulus));
													Values->push_back(Value);
												}
											}
											*Loop.Counter = Loop.Limit;
											FastPathCount = static_cast<std::size_t>(static_cast<std::int64_t>(Loop.Limit) - First);
											FastPathExitIp = Loop.ExitIp;
										}
									}
								}
								const auto* const UnsignedScalar =
										std::get_if<std::uint32_t>(&PushValue);
								if (!bHandled && ArrayHandle && *ArrayHandle && UnsignedScalar
										&& (*ArrayHandle)->MutationOrigin.FieldIndex
											== static_cast<std::uint16_t>(-1))
								{
									auto* const Values = std::get_if<std::vector<std::uint32_t>>(
											&(*ArrayHandle)->ScalarElements);
									if (Values)
									{
										Values->push_back(*UnsignedScalar);
										bHandled = true;
										const std::size_t TailStart = CallEnd
												+ Bytecode::InstructionSize(EOpcode::Pop);
										std::uint16_t CounterSlot = 0xFFFF;
										std::uint32_t ConstantValue = 0;
										std::uint32_t Mask = 0;
										bool bConstantValue = false;
										bool bMaskedCounter = false;
										if (AfterIp + Bytecode::InstructionSize(EOpcode::ConstI32)
													== CallIp
												&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32)
										{
											const std::uint16_t ConstantIndex =
													Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
											if (ConstantIndex < Module->Constants.size()
													&& Module->Constants[ConstantIndex].Kind
														== EConstantKind::U32)
											{
												ConstantValue = Module->Constants[ConstantIndex].U32;
												bConstantValue = true;
											}
										}
										constexpr std::size_t MaskedCounterExpressionSize = 10u;
										if (!bConstantValue
												&& AfterIp + MaskedCounterExpressionSize == CallIp
												&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal
												&& static_cast<EOpcode>(Code[AfterIp + 3u]) == EOpcode::ConstI32
												&& static_cast<EOpcode>(Code[AfterIp + 6u]) == EOpcode::BitAnd
												&& (static_cast<EOpcode>(Code[AfterIp + 7u]) == EOpcode::CheckedCast
													|| static_cast<EOpcode>(Code[AfterIp + 7u]) == EOpcode::CallBuiltin))
										{
											const std::uint16_t MaskIndex =
													Bytecode::ReadU16LE(Code.data() + AfterIp + 4u);
											const bool bUnsignedTarget = [&]()
											{
												if (static_cast<EOpcode>(Code[AfterIp + 7u])
														== EOpcode::CallBuiltin)
												{
													return Code[AfterIp + 9u] == 1u
															&& static_cast<EBuiltinId>(Code[AfterIp + 8u])
																== EBuiltinId::ToUnsigned;
												}
												const std::uint16_t TypeNameIndex =
														Bytecode::ReadU16LE(Code.data() + AfterIp + 8u);
												return TypeNameIndex < Module->StringPool.size()
														&& (Module->StringPool[TypeNameIndex] == "uint"
															|| Module->StringPool[TypeNameIndex] == "uint32"
															|| Module->StringPool[TypeNameIndex] == "unsigned"
															|| Module->StringPool[TypeNameIndex] == "unsigned int");
											}();
											if (MaskIndex < Module->Constants.size()
													&& Module->Constants[MaskIndex].Kind == EConstantKind::I32
													&& Module->Constants[MaskIndex].I32 >= 0
													&& bUnsignedTarget)
											{
												CounterSlot = Bytecode::ReadU16LE(Code.data() + AfterIp + 1u);
												Mask = static_cast<std::uint32_t>(
														Module->Constants[MaskIndex].I32);
												bMaskedCounter = true;
											}
										}
										if (bConstantValue && TailStart + 3u <= CodeSize
												&& static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal)
										{
											CounterSlot = Bytecode::ReadU16LE(Code.data() + TailStart + 1u);
										}
										FCanonicalI32Loop Loop;
										if ((bConstantValue || bMaskedCounter)
												&& TryMatchCanonicalI32Loop(
													Ip, TailStart, CounterSlot, Loop))
										{
											const std::int32_t First = *Loop.Counter;
											const std::int64_t NextValue =
													static_cast<std::int64_t>(First) + 1;
											const std::uint64_t RemainingCount = Loop.Limit > NextValue
													? static_cast<std::uint64_t>(Loop.Limit - NextValue)
												: 0u;
											constexpr std::uint64_t MaxAutomaticBulkReserve =
													std::uint64_t{1} << 20u;
											if (RemainingCount <= MaxAutomaticBulkReserve
													&& RemainingCount <= static_cast<std::uint64_t>(
														Values->max_size() - Values->size()))
											{
												Values->reserve(Values->size()
														+ static_cast<std::size_t>(RemainingCount));
											}
											if (bConstantValue)
											{
												Values->insert(
														Values->end(),
														static_cast<std::size_t>(RemainingCount),
														ConstantValue);
											}
											else
											{
												for (std::int64_t Next = NextValue;
													Next < Loop.Limit; ++Next)
												{
													Values->push_back(
															static_cast<std::uint32_t>(Next) & Mask);
												}
											}
											*Loop.Counter = Loop.Limit;
											FastPathCount = static_cast<std::size_t>(
													static_cast<std::int64_t>(Loop.Limit) - First);
											FastPathExitIp = Loop.ExitIp;
										}
									}
								}
								if (!bHandled && !Vm->JitTryPushDynamicArrayScalarOnLocal(Slot, PushValue, bHandled))
								{
									return FailVm();
								}
								if (bHandled)
								{
									Vm->RecordJitArrayFastPathPush(FastPathCount);
									Ip = FastPathExitIp != 0 ? FastPathExitIp : CallEnd + Bytecode::InstructionSize(EOpcode::Pop);
									break;
								}
							}
						}
					}
					// Scalar map stores are deliberately checked
					// before the broader LoadLocal fusions. A
					// proven non-escaping map loop otherwise pays
					// every object/array/call pattern probe
					// before reaching its typed storage path on
					// each iteration.
					if (bUseScalarMapStorage)
					{
						std::size_t CompoundUpdateEnd = AfterIp;
						std::size_t CompoundUpdateFailureIp = AfterIp;
						bool bCompoundUpdateHandled = false;
						if (!TryApplyScalarMapCompoundUpdate(
									*Vm, *Module, Code, Slot, AfterIp,
									CompoundUpdateEnd, bCompoundUpdateHandled,
									CompoundUpdateFailureIp))
						{
							return FailVmAt(CompoundUpdateFailureIp);
						}
						if (bCompoundUpdateHandled)
						{
							Vm->RecordJitScalarMapFastPathGet();
							Vm->RecordJitScalarMapFastPathSet();
							Ip = CompoundUpdateEnd;
							break;
						}

						constexpr std::size_t ScalarMapI32AddTailSize = 24;
						if (AfterIp + ScalarMapI32AddTailSize <= CodeSize
							&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 6]) == EOpcode::ConstI32
							&& static_cast<EOpcode>(Code[AfterIp + 9]) == EOpcode::Add
							&& static_cast<EOpcode>(Code[AfterIp + 10]) == EOpcode::StoreLocal
							&& static_cast<EOpcode>(Code[AfterIp + 13]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + AfterIp + 11)
								== Bytecode::ReadU16LE(Code.data() + AfterIp + 14)
							&& static_cast<EOpcode>(Code[AfterIp + 16]) == EOpcode::SetIndex
							&& static_cast<EOpcode>(Code[AfterIp + 17]) == EOpcode::StoreLocal
							&& Bytecode::ReadU16LE(Code.data() + AfterIp + 18) == Slot
							&& static_cast<EOpcode>(Code[AfterIp + 20]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + AfterIp + 21)
								== Bytecode::ReadU16LE(Code.data() + AfterIp + 14)
							&& static_cast<EOpcode>(Code[AfterIp + 23]) == EOpcode::Pop)
						{
							const std::uint16_t ConstantIndex =
								Bytecode::ReadU16LE(Code.data() + AfterIp + 7);
							if (ConstantIndex < Module->Constants.size()
								&& Module->Constants[ConstantIndex].Kind == EConstantKind::I32)
							{
								bool bHandled = false;
								const std::uint16_t KeySlot =
									Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
								const std::uint16_t SourceSlot =
									Bytecode::ReadU16LE(Code.data() + AfterIp + 4);
								FCanonicalI32Loop Loop;
								if (KeySlot == SourceSlot
									&& TryMatchCanonicalI32Loop(
										Ip, AfterIp + ScalarMapI32AddTailSize, KeySlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
									&& !Vm->JitTrySetI32MapAffineRangeOnLocal(
										Slot, *Loop.Counter, Loop.Limit,
										Module->Constants[ConstantIndex].I32, bHandled))
								{
									return FailVm();
								}
								if (bHandled)
								{
									const std::size_t Count = static_cast<std::size_t>(
										static_cast<std::int64_t>(Loop.Limit)
											- static_cast<std::int64_t>(*Loop.Counter));
									*Loop.Counter = Loop.Limit;
									Vm->RecordJitScalarMapFastPathSet(Count);
									Ip = Loop.ExitIp;
									break;
								}
								if (!Vm->JitTrySetScalarMapI32AddOnLocals(
										Slot, KeySlot, SourceSlot,
										Module->Constants[ConstantIndex].I32, bHandled))
								{
									return FailVm();
								}
								if (bHandled)
								{
									Vm->RecordJitScalarMapFastPathSet();
									Ip = AfterIp + ScalarMapI32AddTailSize;
									break;
								}
							}
						}

						constexpr std::size_t LocalAssignmentTailSize = 20;
						if (AfterIp + LocalAssignmentTailSize <= CodeSize
							&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 6]) == EOpcode::StoreLocal
							&& static_cast<EOpcode>(Code[AfterIp + 9]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + AfterIp + 7)
								== Bytecode::ReadU16LE(Code.data() + AfterIp + 10)
							&& static_cast<EOpcode>(Code[AfterIp + 12]) == EOpcode::SetIndex
							&& static_cast<EOpcode>(Code[AfterIp + 13]) == EOpcode::StoreLocal
							&& Bytecode::ReadU16LE(Code.data() + AfterIp + 14) == Slot
							&& static_cast<EOpcode>(Code[AfterIp + 16]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + AfterIp + 17)
								== Bytecode::ReadU16LE(Code.data() + AfterIp + 10)
							&& static_cast<EOpcode>(Code[AfterIp + 19]) == EOpcode::Pop)
						{
							bool bHandled = false;
							const std::uint16_t KeySlot =
								Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
							const std::uint16_t SourceSlot =
								Bytecode::ReadU16LE(Code.data() + AfterIp + 4);
							FCanonicalI32Loop Loop;
							if (KeySlot == SourceSlot
								&& TryMatchCanonicalI32Loop(
									Ip, AfterIp + LocalAssignmentTailSize, KeySlot, Loop)
								&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
								&& !Vm->JitTrySetI32MapAffineRangeOnLocal(
									Slot, *Loop.Counter, Loop.Limit, 0, bHandled))
							{
								return FailVm();
							}
							if (bHandled)
							{
								const std::size_t Count = static_cast<std::size_t>(
									static_cast<std::int64_t>(Loop.Limit)
										- static_cast<std::int64_t>(*Loop.Counter));
								*Loop.Counter = Loop.Limit;
								Vm->RecordJitScalarMapFastPathSet(Count);
								Ip = Loop.ExitIp;
								break;
							}
							if (!Vm->JitTrySetScalarMapDirectFromLocals(
									Slot, KeySlot, SourceSlot, bHandled))
							{
								return FailVm();
							}
							if (bHandled)
							{
								Vm->RecordJitScalarMapFastPathSet();
								Ip = AfterIp + LocalAssignmentTailSize;
								break;
							}
						}
					}
					// A scalar index accumulator has a fixed
					// compiler shape and is common in Array/Map
					// reductions. Resolve it before dynamic-call,
					// comparison, and field patterns so the hot
					// loop pays only the relevant probe.
					if (bUseScalarArrayStorage
						&& AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal)
							<= CodeSize
						&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal)
					{
						const std::uint16_t ContainerSlot =
							Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
						FValue IndexValue;
						bool bIndexAvailable = false;
						const std::size_t IndexExpressionIp = AfterIp
							+ Bytecode::InstructionSize(EOpcode::LoadLocal);
						std::size_t IndexEnd = IndexExpressionIp;
						if (!TryReadLinearScalarExpression(
								*Vm, *Module, Code, IndexEnd,
								IndexValue, bIndexAvailable, IndexEnd))
						{
							return FailVm();
						}
						const std::size_t GetSize =
							Bytecode::InstructionSize(EOpcode::GetIndexCopy);
						const std::size_t AddSize =
							Bytecode::InstructionSize(EOpcode::Add);
						const std::size_t StoreSize =
							Bytecode::InstructionSize(EOpcode::StoreLocal);
						const EOpcode IndexStartOp = IndexExpressionIp < CodeSize
							? static_cast<EOpcode>(Code[IndexExpressionIp])
							: EOpcode::Nop;
						const bool bSimpleLegacyIndex =
							(IndexStartOp == EOpcode::ConstI32
								|| IndexStartOp == EOpcode::LoadLocal)
							&& IndexEnd == IndexExpressionIp
								+ Bytecode::InstructionSize(IndexStartOp);
						if (!bSimpleLegacyIndex && bIndexAvailable
							&& IndexEnd + GetSize + AddSize + StoreSize
								<= CodeSize
							&& static_cast<EOpcode>(Code[IndexEnd]) == EOpcode::GetIndexCopy
							&& static_cast<EOpcode>(Code[IndexEnd + GetSize]) == EOpcode::Add
							&& static_cast<EOpcode>(Code[IndexEnd + GetSize + AddSize])
								== EOpcode::StoreLocal
							&& Bytecode::ReadU16LE(
									Code.data() + IndexEnd + GetSize + AddSize + 1) == Slot)
						{
							bool bHandled = false;
							FValue* const DestLocal = BorrowLocal(Slot);
							FValue* const ArrayLocal = BorrowLocal(ContainerSlot);
							auto* const Dest = DestLocal
								? std::get_if<std::uint32_t>(DestLocal)
								: nullptr;
							auto* const ArrayHandle = ArrayLocal
								? std::get_if<FDynamicArrayHandle>(ArrayLocal)
								: nullptr;
							const auto* const Index = std::get_if<std::int32_t>(&IndexValue);
							const auto* const Values = ArrayHandle && *ArrayHandle
								? std::get_if<std::vector<std::uint32_t>>(
									&(*ArrayHandle)->ScalarElements)
								: nullptr;
							if (Dest && Values && Index)
							{
								bHandled = true;
								if (*Index < 0
									|| static_cast<std::size_t>(*Index) >= Values->size())
								{
									return ErrorAt(IndexEnd, "array index out of bounds");
								}
								*Dest += (*Values)[static_cast<std::size_t>(*Index)];
							}
							if (!bHandled && !Vm->JitTryAddLocalFromScalarArray(
									Slot, ContainerSlot, IndexValue, bHandled))
							{
								return FailVmAt(IndexEnd + GetSize);
							}
							if (bHandled)
							{
								Vm->RecordJitArrayFastPathGet();
								Ip = IndexEnd + GetSize + AddSize + StoreSize;
								break;
							}
						}
					}
					// Typed i32 Array reduction: `total = (total + values[index]) % modulus`.
					// The ordinary scalar-array path below covers addition without the final
					// remainder. Keeping this exact form here prevents a random/affine index
					// loop from falling back to the boxed Linear container bridge.
					if (bUseScalarArrayStorage
						&& AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal) <= CodeSize
						&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal)
					{
						const std::uint16_t ContainerSlot =
							Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
						FValue IndexValue;
						bool bIndexAvailable = false;
						std::size_t GetIndexIp =
							AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
						const std::size_t IndexExpressionIp = GetIndexIp;
						if (!TryReadLinearScalarExpression(
								*Vm, *Module, Code, GetIndexIp, IndexValue,
								bIndexAvailable, GetIndexIp))
						{
							return FailVm();
						}
						const std::size_t AddIp =
							GetIndexIp + Bytecode::InstructionSize(EOpcode::GetIndexCopy);
						const std::size_t ModulusIp =
							AddIp + Bytecode::InstructionSize(EOpcode::Add);
						FValue ModulusValue;
						bool bModulusAvailable = false;
						std::size_t ModIp = ModulusIp;
						if (!TryReadLinearScalarOperand(
								*Vm, *Module, Code, ModulusIp, ModulusValue,
								bModulusAvailable, ModIp))
						{
							return FailVm();
						}
						const std::size_t StoreIp =
							ModIp + Bytecode::InstructionSize(EOpcode::Mod);
						if (bIndexAvailable
							&& bModulusAvailable
							&& StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize
							&& static_cast<EOpcode>(Code[GetIndexIp]) == EOpcode::GetIndexCopy
							&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
							&& static_cast<EOpcode>(Code[ModIp]) == EOpcode::Mod
							&& static_cast<EOpcode>(Code[StoreIp]) == EOpcode::StoreLocal
							&& Bytecode::ReadU16LE(Code.data() + StoreIp + 1) == Slot)
						{
							if (const auto* const ModulusPtr =
									std::get_if<std::int32_t>(&ModulusValue))
							{
								FValue* const DestLocal = BorrowLocal(Slot);
								FValue* const ArrayLocal = BorrowLocal(ContainerSlot);
								auto* const Dest = DestLocal
									? std::get_if<std::int32_t>(DestLocal)
									: nullptr;
								auto* const ArrayHandle = ArrayLocal
									? std::get_if<FDynamicArrayHandle>(ArrayLocal)
									: nullptr;
								const auto* const Index = std::get_if<std::int32_t>(&IndexValue);
								const auto* const Values = ArrayHandle && *ArrayHandle
									? std::get_if<std::vector<std::int32_t>>(
										&(*ArrayHandle)->ScalarElements)
									: nullptr;
								const std::int32_t Modulus = *ModulusPtr;
								if (Dest && Values && Index && Modulus != 0)
								{
									if (*Index < 0
										|| static_cast<std::size_t>(*Index) >= Values->size())
									{
										return ErrorAt(GetIndexIp, "array index out of bounds");
									}
									std::int32_t Sum = 0;
									if (!Runtime::CheckedAdd(
											*Dest, (*Values)[static_cast<std::size_t>(*Index)], Sum)
										|| (Sum == std::numeric_limits<std::int32_t>::min()
											&& Modulus == -1))
									{
										return Error("integer overflow");
									}
									*Dest = Sum % Modulus;
									std::size_t FastPathCount = 1;
									std::size_t FastPathExitIp = 0;
									FCounterAffineI32 IndexExpression;
									FCanonicalI32Loop Loop;
									const std::size_t TailStart = StoreIp
										+ Bytecode::InstructionSize(EOpcode::StoreLocal);
									if (TryDescribeCounterAffine(
											IndexExpressionIp, GetIndexIp, IndexExpression)
										&& TryMatchCanonicalI32Loop(
											Ip, TailStart, IndexExpression.CounterSlot, Loop)
										&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
									{
										const std::int32_t First = *Loop.Counter;
										for (std::int64_t Next = static_cast<std::int64_t>(First) + 1;
											Next < Loop.Limit; ++Next)
										{
											*Loop.Counter = static_cast<std::int32_t>(Next);
											std::int32_t BatchIndex = 0;
											if (!EvaluateCounterAffine(
													IndexExpression, *Loop.Counter, BatchIndex))
											{
												return Error("integer overflow");
											}
											if (BatchIndex < 0
												|| static_cast<std::size_t>(BatchIndex) >= Values->size())
											{
												return ErrorAt(GetIndexIp, "array index out of bounds");
											}
											if (!Runtime::CheckedAdd(
													*Dest,
													(*Values)[static_cast<std::size_t>(BatchIndex)], Sum)
												|| (Sum == std::numeric_limits<std::int32_t>::min()
													&& Modulus == -1))
											{
												return Error("integer overflow");
											}
											*Dest = Sum % Modulus;
										}
										*Loop.Counter = Loop.Limit;
										FastPathCount = static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - First);
										FastPathExitIp = Loop.ExitIp;
									}
									Vm->RecordJitArrayFastPathGet(FastPathCount);
									Ip = FastPathExitIp != 0
										? FastPathExitIp
										: StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
									break;
								}
							}
						}
					}
					if (bUseScalarMapStorage && AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal) <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal)
					{
						const std::uint16_t ContainerSlot = Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
						FValue KeyValue;
						bool bKeyAvailable = false;
						std::size_t GetIndexIp = AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
						const std::size_t KeyExpressionIp = GetIndexIp;
						if (!TryReadLinearScalarExpression(*Vm, *Module, Code, GetIndexIp, KeyValue, bKeyAvailable, GetIndexIp))
						{
							return FailVm();
						}
						const std::size_t AddIp = GetIndexIp + Bytecode::InstructionSize(EOpcode::GetIndexCopy);
						const std::size_t ModulusIp = AddIp + Bytecode::InstructionSize(EOpcode::Add);
						FValue ModulusValue;
						bool bModulusAvailable = false;
						std::size_t ModIp = ModulusIp;
						if (!TryReadLinearScalarOperand(
								*Vm, *Module, Code, ModulusIp, ModulusValue,
								bModulusAvailable, ModIp))
						{
							return FailVm();
						}
						const std::size_t StoreIp = ModIp + Bytecode::InstructionSize(EOpcode::Mod);
						if (bKeyAvailable && bModulusAvailable && StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize && static_cast<EOpcode>(Code[GetIndexIp]) == EOpcode::GetIndexCopy && static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add && static_cast<EOpcode>(Code[ModIp]) == EOpcode::Mod && static_cast<EOpcode>(Code[StoreIp]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + StoreIp + 1) == Slot)
						{
							if (const auto* const ModulusPtr =
									std::get_if<std::int32_t>(&ModulusValue))
							{
								bool bHandled = false;
								std::size_t FastPathCount = 1;
								std::size_t FastPathExitIp = 0;
								FValue* const DestLocal = BorrowLocal(Slot);
								FValue* const MapLocal = BorrowLocal(ContainerSlot);
								auto* const Dest = DestLocal ? std::get_if<std::int32_t>(DestLocal) : nullptr;
								auto* const MapHandle = MapLocal ? std::get_if<FMapHandle>(MapLocal) : nullptr;
								const auto* const Key = std::get_if<std::int32_t>(&KeyValue);
								const std::int32_t Modulus = *ModulusPtr;
								if (Dest && MapHandle && *MapHandle && Key && Modulus != 0)
								{
									const auto* const Entries = std::get_if<FMap::FIntI32Entries>(&(*MapHandle)->ScalarEntries);
									if (Entries)
									{
										bHandled = true;
										const std::int32_t* Found = Entries->Find(*Key);
										if (!Found)
										{
											return Error("map key not found");
										}
										std::int32_t Sum = 0;
										if (!Runtime::CheckedAdd(*Dest, *Found, Sum) || (Sum == std::numeric_limits<std::int32_t>::min() && Modulus == -1))
										{
											return Error("integer overflow");
										}
										*Dest = Sum % Modulus;
										FCounterAffineI32 KeyExpression;
										FCanonicalI32Loop Loop;
										const std::size_t TailStart = StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
										if (TryDescribeCounterAffine(KeyExpressionIp, GetIndexIp, KeyExpression)
											&& TryMatchCanonicalI32Loop(Ip, TailStart, KeyExpression.CounterSlot, Loop)
											&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
										{
											const std::int32_t First = *Loop.Counter;
											for (std::int64_t Next = static_cast<std::int64_t>(First) + 1; Next < Loop.Limit; ++Next)
											{
												*Loop.Counter = static_cast<std::int32_t>(Next);
												std::int32_t BatchKey = 0;
												if (!EvaluateCounterAffine(KeyExpression, *Loop.Counter, BatchKey))
												{
													return Error("integer overflow");
												}
												Found = Entries->Find(BatchKey);
												if (!Found)
												{
													return Error("map key not found");
												}
												if (!Runtime::CheckedAdd(*Dest, *Found, Sum) || (Sum == std::numeric_limits<std::int32_t>::min() && Modulus == -1))
												{
													return Error("integer overflow");
												}
												*Dest = Sum % Modulus;
											}
											*Loop.Counter = Loop.Limit;
											FastPathCount = static_cast<std::size_t>(static_cast<std::int64_t>(Loop.Limit) - First);
											FastPathExitIp = Loop.ExitIp;
										}
									}
								}
								if (!bHandled && !Vm->JitTryAddLocalFromScalarMapModuloI32(Slot, ContainerSlot, KeyValue, Modulus, bHandled))
								{
									return FailVm();
								}
								if (bHandled)
								{
									Vm->RecordJitScalarMapFastPathGet(FastPathCount);
									Ip = FastPathExitIp != 0 ? FastPathExitIp : StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
									break;
								}
							}
						}
					}
					constexpr std::size_t EarlyLocalAddIndexTailSize = 11;
					if (AfterIp + EarlyLocalAddIndexTailSize <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal && (static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::ConstI32 || static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::LoadLocal) && static_cast<EOpcode>(Code[AfterIp + 6]) == EOpcode::GetIndexCopy && static_cast<EOpcode>(Code[AfterIp + 7]) == EOpcode::Add && static_cast<EOpcode>(Code[AfterIp + 8]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + AfterIp + 9) == Slot)
					{
						FValue KeyValue;
						bool bKeyAvailable = false;
						std::size_t KeyEnd = AfterIp + 3;
						if (!TryReadLinearScalarOperand(*Vm, *Module, Code, AfterIp + 3, KeyValue, bKeyAvailable, KeyEnd))
						{
							return FailVm();
						}
						if (bKeyAvailable && KeyEnd == AfterIp + 6)
						{
							const std::uint16_t ContainerSlot = Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
							const std::size_t TailStart = AfterIp + EarlyLocalAddIndexTailSize;
							const std::uint16_t CounterSlot = TailStart + 3 <= CodeSize
									&& static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal
									? Bytecode::ReadU16LE(Code.data() + TailStart + 1)
								: 0xFFFF;
							FCanonicalI32Loop InvariantLoop;
							if (TryMatchCanonicalI32Loop(
										Ip, TailStart, CounterSlot, InvariantLoop)
									&& LoopDoesNotAliasMutatedLocal(InvariantLoop, Slot))
							{
								const EOpcode KeyOp = static_cast<EOpcode>(Code[AfterIp + 3]);
								const std::uint16_t KeySlot = KeyOp == EOpcode::LoadLocal
										? Bytecode::ReadU16LE(Code.data() + AfterIp + 4)
									: 0xFFFF;
								FValue* const DirectKey = KeyOp == EOpcode::LoadLocal
										? BorrowLocal(KeySlot)
									: nullptr;
								const bool bInvariantKey = KeyOp == EOpcode::ConstI32
										|| (KeyOp == EOpcode::LoadLocal
											&& DirectKey && DirectKey->index() == KeyValue.index()
											&& KeySlot != Slot && KeySlot != ContainerSlot
											&& KeySlot != InvariantLoop.CounterSlot
											&& KeySlot != InvariantLoop.LimitSlot);
								if (bInvariantKey)
								{
									FValue StableValue;
									bool bStableMap = false;
									bool bStableArray = false;
									if (!Vm->JitTryGetScalarIndexOnLocal(
											ContainerSlot, KeyValue, StableValue,
											bStableMap, bStableArray))
									{
										return FailVmAt(AfterIp + 3u);
									}
									FValue* const Destination = BorrowLocal(Slot);
									FValue Probe = Destination ? *Destination : FValue{std::int32_t{0}};
									EJitScalarValueKind ScalarKind = EJitScalarValueKind::None;
									bool bOverflow = false;
									if ((bStableMap || bStableArray) && Destination
											&& TryAddExactScalar(
												Probe, StableValue, ScalarKind, bOverflow))
									{
										if (bOverflow)
										{
											return Error("integer overflow");
										}
										const std::size_t IterationCount = static_cast<std::size_t>(
												static_cast<std::int64_t>(InvariantLoop.Limit)
												- *InvariantLoop.Counter);
										FValue ReducedDestination = *Destination;
										EJitScalarValueKind ReducedKind = EJitScalarValueKind::None;
										bool bReducedOverflow = false;
										if (TryAddExactIntegerRepeated(
													ReducedDestination, StableValue, IterationCount,
													ReducedKind, bReducedOverflow)
												&& ReducedKind == ScalarKind && !bReducedOverflow)
										{
											*Destination = std::move(ReducedDestination);
											*InvariantLoop.Counter = InvariantLoop.Limit;
											if (bStableMap)
											{
												Vm->RecordJitScalarMapFastPathGet(IterationCount);
											}
											else
											{
												Vm->RecordJitArrayFastPathGet(IterationCount);
											}
											Ip = InvariantLoop.ExitIp;
											break;
										}
										for (std::int64_t Current = *InvariantLoop.Counter;
											Current < InvariantLoop.Limit; ++Current)
										{
											*InvariantLoop.Counter = static_cast<std::int32_t>(Current);
											if (!TryAddExactScalar(
														*Destination, StableValue, ScalarKind, bOverflow)
													|| bOverflow)
											{
												return Error("integer overflow");
											}
										}
										*InvariantLoop.Counter = InvariantLoop.Limit;
										if (bStableMap)
										{
											Vm->RecordJitScalarMapFastPathGet(IterationCount);
										}
										else
										{
											Vm->RecordJitArrayFastPathGet(IterationCount);
										}
										Ip = InvariantLoop.ExitIp;
										break;
									}
								}
							}
							bool bMapHandled = false;
							if (bUseScalarMapStorage && !Vm->JitTryAddLocalFromScalarMap(Slot, ContainerSlot, KeyValue, bMapHandled))
							{
								return FailVm();
							}
							if (bMapHandled)
							{
								Vm->RecordJitScalarMapFastPathGet();
								Ip = AfterIp + EarlyLocalAddIndexTailSize;
								break;
							}
							bool bArrayHandled = false;
							std::size_t ArrayFastPathCount = 1;
							std::size_t ArrayFastPathExitIp = 0;
							FValue* const DestLocal = BorrowLocal(Slot);
							FValue* const ArrayLocal = BorrowLocal(ContainerSlot);
							auto* const Dest = DestLocal ? std::get_if<std::int32_t>(DestLocal) : nullptr;
							auto* const ArrayHandle = ArrayLocal ? std::get_if<FDynamicArrayHandle>(ArrayLocal) : nullptr;
							const auto* const Index = std::get_if<std::int32_t>(&KeyValue);
							if (Dest && ArrayHandle && *ArrayHandle && Index)
							{
								const auto* const Values = std::get_if<std::vector<std::int32_t>>(&(*ArrayHandle)->ScalarElements);
								if (Values)
								{
									bArrayHandled = true;
									if (*Index < 0 || static_cast<std::size_t>(*Index) >= Values->size())
									{
										return Error("array index out of bounds");
									}
									std::int32_t Sum = 0;
									if (!Runtime::CheckedAdd(*Dest, (*Values)[static_cast<std::size_t>(*Index)], Sum))
									{
										return Error("integer overflow");
									}
									*Dest = Sum;
									FCounterAffineI32 IndexExpression;
									FCanonicalI32Loop Loop;
									const std::size_t IndexedTailStart = AfterIp + EarlyLocalAddIndexTailSize;
									if (TryDescribeCounterAffine(AfterIp + 3, KeyEnd, IndexExpression)
										&& TryMatchCanonicalI32Loop(Ip, IndexedTailStart, IndexExpression.CounterSlot, Loop)
										&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
									{
										const std::int32_t First = *Loop.Counter;
										const std::int64_t NextValue =
											static_cast<std::int64_t>(First) + 1;
										const bool bDirectContiguousRange =
											IndexExpression.Multiplier == 1
											&& IndexExpression.Addend == 0
											&& IndexExpression.Modulus == 0
											&& NextValue >= 0
											&& Loop.Limit >= NextValue
											&& static_cast<std::uint64_t>(Loop.Limit)
												<= static_cast<std::uint64_t>(Values->size());
										if (bDirectContiguousRange)
										{
											const std::size_t BeginIndex =
												static_cast<std::size_t>(NextValue);
											const std::size_t Count = static_cast<std::size_t>(
												static_cast<std::int64_t>(Loop.Limit) - NextValue);
											for (std::size_t Offset = 0; Offset < Count; ++Offset)
											{
												if (!Runtime::CheckedAdd(
														*Dest, (*Values)[BeginIndex + Offset], Sum))
												{
													*Loop.Counter = static_cast<std::int32_t>(
														NextValue + static_cast<std::int64_t>(Offset));
													return Error("integer overflow");
												}
												*Dest = Sum;
											}
										}
										else
										{
											for (std::int64_t Next = NextValue; Next < Loop.Limit; ++Next)
											{
												*Loop.Counter = static_cast<std::int32_t>(Next);
												std::int32_t BatchIndex = 0;
												if (!EvaluateCounterAffine(
														IndexExpression, *Loop.Counter, BatchIndex))
												{
													return Error("integer overflow");
												}
												if (BatchIndex < 0
													|| static_cast<std::size_t>(BatchIndex) >= Values->size())
												{
													return Error("array index out of bounds");
												}
												if (!Runtime::CheckedAdd(
														*Dest,
														(*Values)[static_cast<std::size_t>(BatchIndex)], Sum))
												{
													return Error("integer overflow");
												}
												*Dest = Sum;
											}
										}
										*Loop.Counter = Loop.Limit;
										ArrayFastPathCount = static_cast<std::size_t>(static_cast<std::int64_t>(Loop.Limit) - First);
										ArrayFastPathExitIp = Loop.ExitIp;
									}
								}
							}
							if (!bArrayHandled && !Vm->JitTryAddLocalFromScalarArray(Slot, ContainerSlot, KeyValue, bArrayHandled))
							{
								return FailVmAt(AfterIp + 7u);
							}
							if (bArrayHandled)
							{
								Vm->RecordJitArrayFastPathGet(ArrayFastPathCount);
								Ip = ArrayFastPathExitIp != 0 ? ArrayFastPathExitIp : AfterIp + EarlyLocalAddIndexTailSize;
								break;
							}
						}
					}
					// Index reads and scalar Set calls have an
					// exact, side-effect-free bytecode prefix.
					// Handle them before the broader
					// object/field/call fusions below so a hot
					// container loop does not re-run unrelated
					// pattern probes on every iteration.
					if (bUseScalarSetStorage && ScalarSetCallKinds != nullptr && AfterIp < CodeSize)
					{
						const EOpcode FirstArgumentOp = static_cast<EOpcode>(Code[AfterIp]);
						const std::size_t SimpleArgumentEnd = AfterIp + Bytecode::InstructionSize(FirstArgumentOp);
						FValue ArgumentValue;
						bool bArgumentAvailable = false;
						std::size_t CallIp = AfterIp;
						if (!TryReadLinearScalarExpression(*Vm, *Module, Code, AfterIp, ArgumentValue, bArgumentAvailable, CallIp))
						{
							return FailVm();
						}
						// Direct local/constant arguments already
						// have a slightly cheaper specialized path
						// below. This branch extends it to one
						// arithmetic expression without touching
						// the boxed operand stack.
						if (bArgumentAvailable && CallIp != SimpleArgumentEnd && CallIp < ScalarSetCallKindCount && CallIp + Bytecode::InstructionSize(EOpcode::CallMethod) <= CodeSize && static_cast<EOpcode>(Code[CallIp]) == EOpcode::CallMethod && Code[CallIp + 3] == 1 && ScalarSetCallKinds[CallIp] != 0)
						{
							const std::size_t CallEnd = CallIp + Bytecode::InstructionSize(EOpcode::CallMethod);
							const bool bDiscardReturn = CallEnd < CodeSize && static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Pop;
							FValue FastReturnValue;
							bool bFastContainerCall = false;
							std::size_t SetFastPathExitIp = 0;
							const auto Operation = static_cast<EJitScalarSetOperation>(ScalarSetCallKinds[CallIp] - 1);
							FValue* const SetLocal = BorrowLocal(Slot);
							auto* const SetHandle = SetLocal ? std::get_if<FSetHandle>(SetLocal) : nullptr;
							const auto* const Element = std::get_if<std::int32_t>(&ArgumentValue);
							if (SetHandle && *SetHandle && Element && (*SetHandle)->MutationOrigin.FieldIndex == static_cast<std::uint16_t>(-1))
							{
								if (auto* const Elements = std::get_if<FSet::FIntScalarElements>(&(*SetHandle)->ScalarElements))
								{
									if (Operation == EJitScalarSetOperation::Has)
									{
										FastReturnValue = Elements->Contains(*Element);
										bFastContainerCall = true;
										FCounterAffineI32 ElementExpression;
										const std::size_t BranchIp = CallEnd;
										const std::size_t TrueIp = BranchIp + Bytecode::InstructionSize(EOpcode::JmpIfFalse);
										if (BranchIp + Bytecode::InstructionSize(EOpcode::JmpIfFalse) + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize && static_cast<EOpcode>(Code[BranchIp]) == EOpcode::JmpIfFalse && static_cast<EOpcode>(Code[TrueIp]) == EOpcode::IncLocal && TryDescribeCounterAffine(AfterIp, CallIp, ElementExpression))
										{
											const std::size_t TailStart = TrueIp + Bytecode::InstructionSize(EOpcode::IncLocal);
											std::size_t FalseTarget = 0;
											FCanonicalI32Loop Loop;
											FValue* const TotalLocal = BorrowLocal(Bytecode::ReadU16LE(Code.data() + TrueIp + 1));
											auto* const Total = TotalLocal ? std::get_if<std::int32_t>(TotalLocal) : nullptr;
											const std::uint16_t TotalSlot =
												Bytecode::ReadU16LE(Code.data() + TrueIp + 1);
											if (Total
												&& IsJumpTargetInRange(TrueIp, Bytecode::ReadI32LE(Code.data() + BranchIp + 1), CodeSize, FalseTarget)
												&& FalseTarget == TailStart
												&& TryMatchCanonicalI32Loop(Ip, TailStart, ElementExpression.CounterSlot, Loop)
												&& LoopDoesNotAliasMutatedLocal(Loop, TotalSlot))
											{
												auto IncrementTotal = [&]() -> bool
												{
													if (*Total == std::numeric_limits<std::int32_t>::max())
													{
														return Error("integer overflow");
													}
													++*Total;
													return true;
												};
												if (std::get<bool>(FastReturnValue) && !IncrementTotal())
												{
													return false;
												}
												for (std::int64_t Next = static_cast<std::int64_t>(*Loop.Counter) + 1; Next < Loop.Limit; ++Next)
												{
													*Loop.Counter = static_cast<std::int32_t>(Next);
													std::int32_t BatchElement = 0;
													if (!EvaluateCounterAffine(ElementExpression, *Loop.Counter, BatchElement))
													{
														return Error("integer overflow");
													}
													if (Elements->Contains(BatchElement) && !IncrementTotal())
													{
														return false;
													}
												}
												*Loop.Counter = Loop.Limit;
												SetFastPathExitIp = Loop.ExitIp;
											}
										}
									}
									else if (Operation == EJitScalarSetOperation::Remove)
									{
										FastReturnValue = Elements->Erase(*Element);
										bFastContainerCall = true;
									}
									else if (bDiscardReturn)
									{
										(void)Elements->Insert(*Element);
										bFastContainerCall = true;
									}
								}
							}
							if (!bFastContainerCall && !Vm->JitTryCallScalarSetMethodOnLocal(Slot, ArgumentValue, Operation, bDiscardReturn, FastReturnValue, bFastContainerCall))
							{
								return FailVm();
							}
							if (bFastContainerCall)
							{
								if (SetFastPathExitIp != 0)
								{
									Ip = SetFastPathExitIp;
									break;
								}
								if (!bDiscardReturn)
								{
									Stack.push_back(std::move(FastReturnValue));
								}
								Ip = bDiscardReturn ? CallEnd + Bytecode::InstructionSize(EOpcode::Pop) : CallEnd;
								break;
							}
						}
					}
					if (AfterIp < CodeSize)
					{
						const EOpcode ScalarOp = static_cast<EOpcode>(Code[AfterIp]);
						const bool bScalarOperand =
							ScalarOp == EOpcode::ConstBool || ScalarOp == EOpcode::ConstI32 || ScalarOp == EOpcode::ConstF64 || ScalarOp == EOpcode::LoadLocal;
						if (bScalarOperand)
						{
							const std::size_t ScalarEnd = AfterIp + Bytecode::InstructionSize(ScalarOp);
							const bool bPotentialIndexRead = ScalarEnd < CodeSize && static_cast<EOpcode>(Code[ScalarEnd]) == EOpcode::GetIndexCopy;
							const bool bPotentialSetCall = bUseScalarSetStorage && ScalarSetCallKinds != nullptr && ScalarEnd < ScalarSetCallKindCount && ScalarEnd + Bytecode::InstructionSize(EOpcode::CallMethod) <= CodeSize && static_cast<EOpcode>(Code[ScalarEnd]) == EOpcode::CallMethod && Code[ScalarEnd + 3] == 1 && ScalarSetCallKinds[ScalarEnd] != 0;
							if (ScalarEnd <= CodeSize && (bPotentialIndexRead || bPotentialSetCall))
							{
								if (bPotentialSetCall && ScalarOp == EOpcode::LoadLocal)
								{
									const std::size_t CallEnd = ScalarEnd + Bytecode::InstructionSize(EOpcode::CallMethod);
									const bool bDiscardReturn = CallEnd < CodeSize && static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Pop;
									const auto SetOperation = static_cast<EJitScalarSetOperation>(
										ScalarSetCallKinds[ScalarEnd] - 1);
									if (SetOperation == EJitScalarSetOperation::Add && bDiscardReturn)
									{
										const std::size_t TailStart = CallEnd
											+ Bytecode::InstructionSize(EOpcode::Pop);
										const std::uint16_t ArgumentSlot =
											Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
										FCanonicalI32Loop Loop;
										if (TryMatchCanonicalI32Loop(
												Ip, TailStart, ArgumentSlot, Loop)
											&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
										{
											bool bBulkHandled = false;
											if (!Vm->JitTryInsertI32SetRangeOnLocal(
													Slot, *Loop.Counter, Loop.Limit, bBulkHandled))
											{
												return FailVm();
											}
											if (bBulkHandled)
											{
												*Loop.Counter = Loop.Limit;
												Ip = Loop.ExitIp;
												break;
											}
										}
									}
									FValue FastReturnValue;
									bool bFastContainerCall = false;
									if (!Vm->JitTryCallScalarSetMethodOnLocals(Slot,
											Bytecode::ReadU16LE(Code.data() + AfterIp + 1),
											SetOperation,
											bDiscardReturn,
											FastReturnValue,
											bFastContainerCall))
									{
										return FailVm();
									}
									if (bFastContainerCall)
									{
										if (SetOperation == EJitScalarSetOperation::Has
												&& !bDiscardReturn
												&& std::holds_alternative<bool>(FastReturnValue)
												&& CallEnd + Bytecode::InstructionSize(EOpcode::JmpIfFalse)
													+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
												&& static_cast<EOpcode>(Code[CallEnd]) == EOpcode::JmpIfFalse)
										{
											const std::size_t TrueIp = CallEnd
													+ Bytecode::InstructionSize(EOpcode::JmpIfFalse);
											if (static_cast<EOpcode>(Code[TrueIp]) == EOpcode::IncLocal)
											{
												const std::size_t TailStart = TrueIp
														+ Bytecode::InstructionSize(EOpcode::IncLocal);
												const std::uint16_t CounterSlot =
														TailStart + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
															&& static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal
														? Bytecode::ReadU16LE(Code.data() + TailStart + 1)
													: 0xFFFF;
												const std::uint16_t TotalSlot =
														Bytecode::ReadU16LE(Code.data() + TrueIp + 1);
												const std::uint16_t KeySlot =
														Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
												FValue* const DirectKey = BorrowLocal(KeySlot);
												std::size_t FalseTarget = 0;
												FCanonicalI32Loop Loop;
												FValue* const TotalLocal = BorrowLocal(TotalSlot);
												auto* const Total = TotalLocal
														? std::get_if<std::int32_t>(TotalLocal)
													: nullptr;
												if (Total && DirectKey
														&& IsJumpTargetInRange(TrueIp,
															Bytecode::ReadI32LE(Code.data() + CallEnd + 1),
															CodeSize, FalseTarget)
														&& FalseTarget == TailStart
														&& TryMatchCanonicalI32Loop(
															Ip, TailStart, CounterSlot, Loop)
														&& LoopDoesNotAliasMutatedLocal(Loop, TotalSlot)
														&& KeySlot != Slot && KeySlot != TotalSlot
														&& KeySlot != Loop.CounterSlot
														&& KeySlot != Loop.LimitSlot)
												{
													const bool bContains = std::get<bool>(FastReturnValue);
													for (std::int64_t Current = *Loop.Counter;
														Current < Loop.Limit; ++Current)
													{
														*Loop.Counter = static_cast<std::int32_t>(Current);
														if (bContains)
														{
															if (*Total == std::numeric_limits<std::int32_t>::max())
															{
																return ErrorAt(TrueIp, "integer overflow");
															}
															++*Total;
														}
													}
													*Loop.Counter = Loop.Limit;
													Ip = Loop.ExitIp;
													break;
												}
											}
										}
										if (!bDiscardReturn)
										{
											Stack.push_back(std::move(FastReturnValue));
										}
										Ip = bDiscardReturn ? CallEnd + Bytecode::InstructionSize(EOpcode::Pop) : CallEnd;
										break;
									}
								}
								FValue ScalarValue;
								bool bScalarAvailable = false;
								std::size_t ReadEnd = AfterIp;
								if (!TryReadLinearScalarOperand(*Vm, *Module, Code, AfterIp, ScalarValue, bScalarAvailable, ReadEnd))
								{
									return FailVm();
								}
								if (bScalarAvailable && ReadEnd == ScalarEnd && bPotentialIndexRead)
								{
									FValue FastValue;
									bool bFastMapGet = false;
									bool bFastArrayGet = false;
									if (!Vm->JitTryGetScalarIndexOnLocal(Slot, ScalarValue, FastValue, bFastMapGet, bFastArrayGet))
									{
										return FailVmAt(AfterIp);
									}
									if (bFastMapGet || bFastArrayGet)
									{
										if (bFastMapGet)
										{
											Vm->RecordJitScalarMapFastPathGet();
										}
										else
										{
											Vm->RecordJitArrayFastPathGet();
										}
										Stack.push_back(std::move(FastValue));
										Ip = ScalarEnd + Bytecode::InstructionSize(EOpcode::GetIndexCopy);
										break;
									}
								}
								if (bPotentialSetCall && bScalarAvailable && ReadEnd == ScalarEnd)
								{
									const std::size_t CallEnd = ScalarEnd + Bytecode::InstructionSize(EOpcode::CallMethod);
									const bool bDiscardReturn = CallEnd < CodeSize && static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Pop;
									const auto SetOperation = static_cast<EJitScalarSetOperation>(
										ScalarSetCallKinds[ScalarEnd] - 1);
									FValue FastReturnValue;
									bool bFastContainerCall = false;
									if (!Vm->JitTryCallScalarSetMethodOnLocal(Slot,
												ScalarValue,
											SetOperation,
												bDiscardReturn,
												FastReturnValue,
												bFastContainerCall))
									{
										return FailVm();
									}
									if (bFastContainerCall)
									{
										// Exact invariant membership loop:
										//   while (counter < limit) { if (set.has(key)) total++; counter++; }
										// A constant, or a distinct local key, and an exact body make both the
										// The membership and lookup result stay stable. Retain checked increments so overflow is
										// still reported at the same source iteration.
										if (SetOperation == EJitScalarSetOperation::Has && !bDiscardReturn
												&& std::holds_alternative<bool>(FastReturnValue)
												&& CallEnd + Bytecode::InstructionSize(EOpcode::JmpIfFalse)
													+ Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
												&& static_cast<EOpcode>(Code[CallEnd]) == EOpcode::JmpIfFalse)
										{
											const std::size_t TrueIp = CallEnd
													+ Bytecode::InstructionSize(EOpcode::JmpIfFalse);
											if (static_cast<EOpcode>(Code[TrueIp]) == EOpcode::IncLocal)
											{
												const std::size_t TailStart = TrueIp
														+ Bytecode::InstructionSize(EOpcode::IncLocal);
												const std::uint16_t CounterSlot =
														TailStart + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
															&& static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal
														? Bytecode::ReadU16LE(Code.data() + TailStart + 1)
													: 0xFFFF;
												const std::uint16_t TotalSlot = Bytecode::ReadU16LE(Code.data() + TrueIp + 1);
												const std::uint16_t KeySlot = ScalarOp == EOpcode::LoadLocal
														? Bytecode::ReadU16LE(Code.data() + AfterIp + 1)
													: 0xFFFF;
												FValue* const DirectKey = ScalarOp == EOpcode::LoadLocal
														? BorrowLocal(KeySlot)
													: nullptr;
												std::size_t FalseTarget = 0;
												FCanonicalI32Loop Loop;
												FValue* const TotalLocal = BorrowLocal(TotalSlot);
												auto* const Total = TotalLocal
														? std::get_if<std::int32_t>(TotalLocal)
													: nullptr;
												if (Total
														&& IsJumpTargetInRange(TrueIp,
															Bytecode::ReadI32LE(Code.data() + CallEnd + 1),
															CodeSize, FalseTarget)
														&& FalseTarget == TailStart
														&& TryMatchCanonicalI32Loop(Ip, TailStart, CounterSlot, Loop)
														&& LoopDoesNotAliasMutatedLocal(Loop, TotalSlot)
														&& (ScalarOp != EOpcode::LoadLocal
															|| (DirectKey && DirectKey->index() == ScalarValue.index()
																&& KeySlot != Slot && KeySlot != TotalSlot
																&& KeySlot != Loop.CounterSlot
																&& KeySlot != Loop.LimitSlot)))
												{
													const bool bContains = std::get<bool>(FastReturnValue);
													for (std::int64_t Current = *Loop.Counter;
														Current < Loop.Limit; ++Current)
													{
														*Loop.Counter = static_cast<std::int32_t>(Current);
														if (bContains)
														{
															if (*Total == std::numeric_limits<std::int32_t>::max())
															{
																return ErrorAt(TrueIp, "integer overflow");
															}
															++*Total;
														}
													}
													*Loop.Counter = Loop.Limit;
													Ip = Loop.ExitIp;
													break;
												}
											}
										}
										if (!bDiscardReturn)
										{
											Stack.push_back(std::move(FastReturnValue));
										}
										Ip = bDiscardReturn ? CallEnd + Bytecode::InstructionSize(EOpcode::Pop) : CallEnd;
										break;
									}
								}
							}
						}
					}
					// A scalar method result assigned directly to
					// a local does not need to enter the boxed
					// operand stack. Support the two most common
					// ABI arities; larger calls retain the
					// authoritative generic path.
					if (bMayHaveDirectScalarMethodStore)
					{
						auto& DirectArguments = LinearScratchState->ScalarArguments;
						std::uint8_t DirectArgumentCount = 0;
						std::size_t DirectCallIp = AfterIp;
						while (DirectArgumentCount < 2 && DirectCallIp < CodeSize && static_cast<EOpcode>(Code[DirectCallIp]) != EOpcode::CallMethod)
						{
							const EOpcode ArgumentOp = static_cast<EOpcode>(Code[DirectCallIp]);
							if (ArgumentOp != EOpcode::ConstBool && ArgumentOp != EOpcode::ConstI32 && ArgumentOp != EOpcode::ConstF64 && ArgumentOp != EOpcode::LoadLocal)
							{
								break;
							}
							const std::size_t ArgumentEnd = DirectCallIp + Bytecode::InstructionSize(ArgumentOp);
							if (ArgumentEnd > CodeSize)
							{
								break;
							}
							++DirectArgumentCount;
							DirectCallIp = ArgumentEnd;
						}
						if (DirectCallIp + Bytecode::InstructionSize(EOpcode::CallMethod) <= CodeSize && static_cast<EOpcode>(Code[DirectCallIp]) == EOpcode::CallMethod && Code[DirectCallIp + 3] == DirectArgumentCount)
						{
							const std::size_t StoreIp = DirectCallIp + Bytecode::InstructionSize(EOpcode::CallMethod);
							if (StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize && static_cast<EOpcode>(Code[StoreIp]) == EOpcode::StoreLocal)
							{
								std::size_t ArgumentReadIp = AfterIp;
								for (std::uint8_t ArgumentIndex = 0; ArgumentIndex < DirectArgumentCount; ++ArgumentIndex)
								{
									bool bAvailable = false;
									std::size_t ArgumentEnd = ArgumentReadIp;
									if (!TryReadLinearScalarOperand(
											*Vm, *Module, Code, ArgumentReadIp, DirectArguments[ArgumentIndex], bAvailable, ArgumentEnd))
									{
										return FailVm();
									}
									if (!bAvailable)
									{
										DirectArgumentCount = 0xFF;
										break;
									}
									ArgumentReadIp = ArgumentEnd;
								}
								if (DirectArgumentCount != 0xFF)
								{
									FValue FastReturnValue;
									bool bFastScalarCall = false;
									bool bDirectTransformCall = false;
									if (DirectArgumentCount == 1)
									{
										std::int32_t DirectResult = 0;
										if (!TryInvokeDirectTransformMethod(
													Slot,
													Bytecode::ReadU16LE(Code.data() + DirectCallIp + 1),
													0xFFFF,
													DirectCallIp,
													DirectArguments[0],
													DirectResult,
													bFastScalarCall))
										{
											return FailVm();
										}
										if (bFastScalarCall)
										{
											FastReturnValue = DirectResult;
											bDirectTransformCall = true;
										}
									}
									if (!bFastScalarCall
											&& !Vm->JitTryCallScalarMethodOnLocal(
												Slot,
												Bytecode::ReadU16LE(Code.data() + DirectCallIp + 1),
											DirectArgumentCount == 0 ? nullptr : DirectArguments.data(),
												DirectArgumentCount,
												static_cast<std::uint32_t>(DirectCallIp),
												FastReturnValue,
												bFastScalarCall))
									{
										return FailVm();
									}
									if (bFastScalarCall)
									{
										const std::uint16_t DestSlot =
												Bytecode::ReadU16LE(Code.data() + StoreIp + 1);
										std::size_t NextIp =
												StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
										FValue* const DirectDest = BorrowLocal(DestSlot);
										const auto* const DirectResult =
												std::get_if<std::int32_t>(&FastReturnValue);
										auto* const DirectStorage = DirectDest
												? std::get_if<std::int32_t>(DirectDest)
											: nullptr;
										if (DirectResult && DirectStorage)
										{
											*DirectStorage = *DirectResult;
											const FDirectI32TransformMethodCache* const FirstTransformEntry =
												bDirectTransformCall
													? LinearScratchState->LastDirectI32TransformMethodCache[
														DirectCallIp & (LinearScratchState->LastDirectI32TransformMethodCache.size() - 1u)]
													: nullptr;
											const FDirectI32TransformMethodCache FirstTransform =
												FirstTransformEntry
													? *FirstTransformEntry
												: FDirectI32TransformMethodCache{};
											const bool bFirstSelfTransformArgument =
												DirectArgumentCount == 1
												&& DirectCallIp == AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal)
												&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal
												&& Bytecode::ReadU16LE(Code.data() + AfterIp + 1) == DestSlot;
											const auto ApplyTransform = [DirectStorage](
												const FDirectI32TransformMethodCache& Transform)
											{
												std::int32_t Sum = 0;
												if (!Runtime::CheckedAdd(*DirectStorage, Transform.Addend, Sum)
													|| (Transform.Modulus != 0
														&& Sum == std::numeric_limits<std::int32_t>::min()
														&& Transform.Modulus == -1))
												{
													return false;
												}
												*DirectStorage = Transform.Modulus == 0
													? Sum
													: Sum % Transform.Modulus;
												return true;
											};
											bool bSecondTransformApplied = false;
											const std::size_t SecondReceiverIp = NextIp;
											const std::size_t SecondArgumentIp = SecondReceiverIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
											const std::size_t SecondCallIp = SecondArgumentIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
											const std::size_t SecondStoreIp = SecondCallIp + Bytecode::InstructionSize(EOpcode::CallMethod);
											const std::size_t TailStart = SecondStoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
											if (bDirectTransformCall && bFirstSelfTransformArgument && TailStart <= CodeSize && static_cast<EOpcode>(Code[SecondReceiverIp]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[SecondArgumentIp]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + SecondArgumentIp + 1) == DestSlot && static_cast<EOpcode>(Code[SecondCallIp]) == EOpcode::CallMethod && Code[SecondCallIp + 3] == 1 && static_cast<EOpcode>(Code[SecondStoreIp]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + SecondStoreIp + 1) == DestSlot)
											{
												const std::uint16_t SecondReceiverSlot = Bytecode::ReadU16LE(Code.data() + SecondReceiverIp + 1);
												std::int32_t SecondResult = 0;
												bool bSecondTransform = false;
												FValue SecondArgument = *DirectStorage;
												if (!TryInvokeDirectTransformMethod(SecondReceiverSlot,
														Bytecode::ReadU16LE(Code.data() + SecondCallIp + 1),
														0xFFFF,
														SecondCallIp,
														SecondArgument,
														SecondResult,
														bSecondTransform))
												{
													return FailVm();
												}
												if (bSecondTransform)
												{
													bSecondTransformApplied = true;
													*DirectStorage = SecondResult;
													NextIp = TailStart;
													const FDirectI32TransformMethodCache* const SecondTransformEntry =
														LinearScratchState->LastDirectI32TransformMethodCache[
															SecondCallIp & (LinearScratchState->LastDirectI32TransformMethodCache.size() - 1u)];
													const FDirectI32TransformMethodCache SecondTransform =
														SecondTransformEntry
															? *SecondTransformEntry
														: FDirectI32TransformMethodCache{};
													const std::uint16_t CounterSlot =
														TailStart < CodeSize && Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize - TailStart && static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal
															? Bytecode::ReadU16LE(Code.data() + TailStart + 1)
															: 0xFFFF;
													FCanonicalI32Loop Loop;
													if (FirstTransform.bResolved && FirstTransform.bSupported
														&& FirstTransform.CallSiteOffset == DirectCallIp
														&& FirstTransform.ReceiverSlot == Slot
														&& SecondTransform.bResolved && SecondTransform.bSupported
														&& SecondTransform.CallSiteOffset == SecondCallIp
														&& SecondTransform.ReceiverSlot == SecondReceiverSlot
														&& TryMatchCanonicalI32Loop(Ip, TailStart, CounterSlot, Loop)
														&& LoopDoesNotAliasMutatedLocal(Loop, DestSlot))
													{
														const std::int32_t FirstCounter = *Loop.Counter;
														for (std::int64_t Next = static_cast<std::int64_t>(FirstCounter) + 1; Next < Loop.Limit; ++Next)
														{
															*Loop.Counter = static_cast<std::int32_t>(Next);
															if (!Vm->JitCommitDirectI32TransformMethod()
																|| !ApplyTransform(FirstTransform)
																|| !Vm->JitCommitDirectI32TransformMethod()
																|| !ApplyTransform(SecondTransform))
															{
																return Vm->HasError() ? FailVm() : Error("integer overflow");
															}
														}
														*Loop.Counter = Loop.Limit;
														NextIp = Loop.ExitIp;
													}
												}
											}
											if (bDirectTransformCall && bFirstSelfTransformArgument
												&& !bSecondTransformApplied
												&& FirstTransform.bResolved && FirstTransform.bSupported
												&& FirstTransform.CallSiteOffset == DirectCallIp
												&& FirstTransform.ReceiverSlot == Slot
												&& NextIp < CodeSize
												&& static_cast<EOpcode>(Code[NextIp]) == EOpcode::IncLocal)
											{
												const std::uint16_t CounterSlot =
													Bytecode::ReadU16LE(Code.data() + NextIp + 1);
												FCanonicalI32Loop Loop;
												if (TryMatchCanonicalI32Loop(Ip, NextIp, CounterSlot, Loop)
													&& LoopDoesNotAliasMutatedLocal(Loop, DestSlot))
												{
													const std::int32_t FirstCounter = *Loop.Counter;
													for (std::int64_t Next =
														static_cast<std::int64_t>(FirstCounter) + 1;
														Next < Loop.Limit; ++Next)
													{
														*Loop.Counter = static_cast<std::int32_t>(Next);
														if (!Vm->JitCommitDirectI32TransformMethod()
															|| !ApplyTransform(FirstTransform))
														{
															return Vm->HasError() ? FailVm() : Error("integer overflow");
														}
													}
													*Loop.Counter = Loop.Limit;
													NextIp = Loop.ExitIp;
												}
											}
										}
										else if (!Vm->JitStoreLocal(DestSlot, std::move(FastReturnValue)))
										{
											return FailVm();
										}
										Ip = NextIp;
										break;
									}
								}
							}
						}
					}
					// Fuse the common dynamic-dispatch
					// accumulator shape without materializing the
					// receiver, argument, return value, or Add
					// result on the boxed operand stack:
					//   LoadLocal dest; LoadLocal receiver;
					//   [scalar arg]; CallMethod; Add; StoreLocal
					//   dest
					// Every evaluated operand is a
					// local/constant, so this preserves
					// evaluation order. The VM bridge still owns
					// virtual resolution, interceptor fallback,
					// JIT epoch guards, overflow, and ref writes.
					if (bMayHaveScalarMethodAccumulator && AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal) <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal)
					{
						const std::uint16_t ReceiverSlot = Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
						const std::size_t ArgumentIp = AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
						auto& MethodArguments = LinearScratchState->ScalarArguments;
						std::size_t CallIp = ArgumentIp;
						std::uint8_t MethodArgumentCount = 0;
						while (MethodArgumentCount < 2 && CallIp < CodeSize && static_cast<EOpcode>(Code[CallIp]) != EOpcode::CallMethod)
						{
							bool bArgumentAvailable = false;
							std::size_t ArgumentEnd = CallIp;
							if (!TryReadLinearScalarOperand(*Vm, *Module, Code, CallIp, MethodArguments[MethodArgumentCount], bArgumentAvailable, ArgumentEnd))
							{
								return FailVm();
							}
							if (!bArgumentAvailable)
							{
								break;
							}
							++MethodArgumentCount;
							CallIp = ArgumentEnd;
						}
						if (CallIp + Bytecode::InstructionSize(EOpcode::CallMethod) <= CodeSize && static_cast<EOpcode>(Code[CallIp]) == EOpcode::CallMethod && Code[CallIp + 3] == MethodArgumentCount)
						{
							const std::size_t CallEnd = CallIp + Bytecode::InstructionSize(EOpcode::CallMethod);
							const std::size_t StoreIp = CallEnd + Bytecode::InstructionSize(EOpcode::Add);
							if (StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize && static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Add && static_cast<EOpcode>(Code[StoreIp]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + StoreIp + 1) == Slot)
							{
								FValue FastReturnValue;
								bool bFastScalarCall = false;
								const std::uint16_t MethodNameIndex = Bytecode::ReadU16LE(Code.data() + CallIp + 1);
								if (MethodArgumentCount == 0)
								{
									std::int32_t RawReturnValue = 0;
									bool bRawCall = false;
									bool bStableConstant = false;
									if (!Vm->JitTryCallNullaryI32MethodOnLocal(
											ReceiverSlot, MethodNameIndex,
											static_cast<std::uint32_t>(CallIp), RawReturnValue,
											bRawCall, bStableConstant))
									{
										return FailVm();
									}
									if (bRawCall)
									{
										if (!Vm->JitAddLocalImm(Slot, RawReturnValue))
										{
											return FailVm();
										}
										std::size_t NextIp = StoreIp
												+ Bytecode::InstructionSize(EOpcode::StoreLocal);
										// Two constant nullary virtual methods in one canonical loop
										// are immutable for the lifetime of the loaded generation.
										// Resolve each receiver once, then retain checked additions;
										// interceptable or non-constant methods never enter this path.
										const std::size_t SecondReceiverIp = NextIp
												+ Bytecode::InstructionSize(EOpcode::LoadLocal);
										const std::size_t SecondCallIp = SecondReceiverIp
												+ Bytecode::InstructionSize(EOpcode::LoadLocal);
										const std::size_t SecondAddIp = SecondCallIp
												+ Bytecode::InstructionSize(EOpcode::CallMethod);
										const std::size_t SecondStoreIp = SecondAddIp
												+ Bytecode::InstructionSize(EOpcode::Add);
										const std::size_t TailStart = SecondStoreIp
												+ Bytecode::InstructionSize(EOpcode::StoreLocal);
										if (bStableConstant && TailStart <= CodeSize
												&& static_cast<EOpcode>(Code[NextIp]) == EOpcode::LoadLocal
												&& Bytecode::ReadU16LE(Code.data() + NextIp + 1) == Slot
												&& static_cast<EOpcode>(Code[SecondReceiverIp]) == EOpcode::LoadLocal
												&& static_cast<EOpcode>(Code[SecondCallIp]) == EOpcode::CallMethod
												&& Code[SecondCallIp + 3] == 0
												&& static_cast<EOpcode>(Code[SecondAddIp]) == EOpcode::Add
												&& static_cast<EOpcode>(Code[SecondStoreIp]) == EOpcode::StoreLocal
												&& Bytecode::ReadU16LE(Code.data() + SecondStoreIp + 1) == Slot)
										{
											const std::uint16_t SecondReceiverSlot =
													Bytecode::ReadU16LE(Code.data() + SecondReceiverIp + 1);
											std::int32_t SecondValue = 0;
											bool bSecondRawCall = false;
											bool bSecondStableConstant = false;
											if (!Vm->JitTryCallNullaryI32MethodOnLocal(
														SecondReceiverSlot,
														Bytecode::ReadU16LE(Code.data() + SecondCallIp + 1),
														static_cast<std::uint32_t>(SecondCallIp), SecondValue,
														bSecondRawCall, bSecondStableConstant))
											{
												return FailVm();
											}
											if (bSecondRawCall)
											{
												if (!Vm->JitAddLocalImm(Slot, SecondValue))
												{
													return FailVm();
												}
												NextIp = TailStart;
												const std::uint16_t CounterSlot =
														TailStart + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
															&& static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal
														? Bytecode::ReadU16LE(Code.data() + TailStart + 1)
														: 0xFFFF;
												FCanonicalI32Loop Loop;
												if (bSecondStableConstant
														&& TryMatchCanonicalI32Loop(Ip, TailStart, CounterSlot, Loop)
														&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
												{
													FValue* const Destination = BorrowLocal(Slot);
													auto* const Total = Destination
															? std::get_if<std::int32_t>(Destination)
														: nullptr;
													if (Total)
													{
														const std::int64_t PairAddend =
																static_cast<std::int64_t>(RawReturnValue) + SecondValue;
														const std::size_t RemainingCount = static_cast<std::size_t>(
																static_cast<std::int64_t>(Loop.Limit)
																- *Loop.Counter - 1);
														const bool bMonotonicPair =
																(RawReturnValue >= 0 && SecondValue >= 0)
																|| (RawReturnValue <= 0 && SecondValue <= 0);
														const bool bRepresentablePair =
																PairAddend >= std::numeric_limits<std::int32_t>::min()
																&& PairAddend <= std::numeric_limits<std::int32_t>::max();
														FValue ReducedTotal = *Total;
														const FValue PairValue = bRepresentablePair
																? FValue{static_cast<std::int32_t>(PairAddend)}
															: FValue{std::int32_t{0}};
														EJitScalarValueKind ReducedKind = EJitScalarValueKind::None;
														bool bReducedOverflow = false;
														const bool bReduced = bMonotonicPair && bRepresentablePair
																&& TryAddExactIntegerRepeated(
																	ReducedTotal, PairValue, RemainingCount,
																	ReducedKind, bReducedOverflow)
																&& ReducedKind == EJitScalarValueKind::I32
																&& !bReducedOverflow;
														if (bReduced)
														{
															*Total = std::get<std::int32_t>(ReducedTotal);
														}
														else
														{
															for (std::int64_t Current =
																	static_cast<std::int64_t>(*Loop.Counter) + 1;
																Current < Loop.Limit; ++Current)
															{
																*Loop.Counter = static_cast<std::int32_t>(Current);
																std::int32_t Sum = 0;
																if (!Runtime::CheckedAdd(*Total, RawReturnValue, Sum))
																{
																	return ErrorAt(CallIp, "integer overflow");
																}
																*Total = Sum;
																if (!Runtime::CheckedAdd(*Total, SecondValue, Sum))
																{
																	return ErrorAt(SecondCallIp, "integer overflow");
																}
																*Total = Sum;
															}
														}
														*Loop.Counter = Loop.Limit;
														NextIp = Loop.ExitIp;
													}
												}
											}
										}
										Ip = NextIp;
										break;
									}
								}
								// The accumulator form has the same direct-i32 transform
								// semantics as the direct-store form above, but previously
								// skipped that cache and entered the scalar ABI bridge for every
								// iteration:
								//   total = total + receiver.method(const-or-local);
								// A direct transform is only advertised for a proven pure
								// `arg + constant` / modulo body.  Resolve/epoch/interceptor
								// guards stay in TryInvokeDirectTransformMethod, then retain the
								// two separate checked additions required by source semantics.
								if (MethodArgumentCount == 1)
								{
									std::int32_t DirectResult = 0;
									bool bDirectTransform = false;
									if (!TryInvokeDirectTransformMethod(
												ReceiverSlot,
												MethodNameIndex,
												0xFFFF,
												CallIp,
												MethodArguments[0],
												DirectResult,
												bDirectTransform))
									{
										return FailVm();
									}
									if (bDirectTransform)
									{
										if (!Vm->JitAddLocalImm(Slot, DirectResult))
										{
											return FailVmAt(CallIp);
										}
										Ip = StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
										break;
									}
								}
								if (!Vm->JitTryCallScalarMethodOnLocal(ReceiverSlot,
											MethodNameIndex,
											MethodArgumentCount == 0 ? nullptr : MethodArguments.data(),
											MethodArgumentCount,
											static_cast<std::uint32_t>(CallIp),
											FastReturnValue,
											bFastScalarCall))
								{
									return FailVm();
								}
								if (bFastScalarCall)
								{
									if (!Vm->JitAddValueToLocal(Slot, FastReturnValue))
									{
										return FailVm();
									}
									Ip = StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
									break;
								}
							}
						}
					}
					// Two rooted locals commonly feed a
					// loop/conditional branch:
					//   LoadLocal a; LoadLocal b; Compare;
					//   JmpIfFalse rel
					// The CFG has already validated this
					// sequence. Compare the rooted locals in
					// place so Linear JIT does not box two values
					// and perform a generic comparison on every
					// iteration.
					constexpr std::size_t LocalCompareBranchTailSize = 9;
					if (AfterIp + LocalCompareBranchTailSize <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::Lt && static_cast<EOpcode>(Code[AfterIp + 4]) == EOpcode::JmpIfFalse)
					{
						bool bJump = false;
						if (!Vm->JitLocalGeLocal(Slot, Bytecode::ReadU16LE(Code.data() + AfterIp + 1), bJump))
						{
							return FailVm();
						}
						const std::size_t BranchEnd = AfterIp + LocalCompareBranchTailSize;
						if (bJump)
						{
							std::size_t Target = 0;
							if (!IsJumpTargetInRange(BranchEnd, Bytecode::ReadI32LE(Code.data() + AfterIp + 5), CodeSize, Target))
							{
								return Error("JIT linear: jump out of range");
							}
							Ip = Target;
						}
						else
						{
							Ip = BranchEnd;
						}
						break;
					}
					const EOpcode LocalCompareOp = AfterIp + 3 < CodeSize ? static_cast<EOpcode>(Code[AfterIp + 3]) : EOpcode::Nop;
					if (AfterIp + LocalCompareBranchTailSize <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal && (LocalCompareOp == EOpcode::Eq || LocalCompareOp == EOpcode::Ne || LocalCompareOp == EOpcode::Le || LocalCompareOp == EOpcode::Gt || LocalCompareOp == EOpcode::Ge) && static_cast<EOpcode>(Code[AfterIp + 4]) == EOpcode::JmpIfFalse)
					{
						bool bCondition = false;
						const std::uint16_t RightSlot = Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
						const bool bInvariantCandidate =
							InvariantLocalCompareSites != nullptr && Ip < InvariantLocalCompareSiteCount && InvariantLocalCompareSites[Ip] != 0;
						const std::size_t CacheIndex = Ip & (InvariantCompareOffsets.size() - 1u);
						bool bInvariant = false;
						bool bCompared = true;
						if (bInvariantCandidate && InvariantCompareOffsets[CacheIndex] == static_cast<std::uint32_t>(Ip))
						{
							bCondition = InvariantCompareResults[CacheIndex];
							bInvariant = true;
						}
						else
						{
							bCompared = Vm->JitCompareLocals(LocalCompareOp, Slot, RightSlot, bCondition);
							// A stable slot may still hold a mutable struct, host value, or variant.
							// Cache only scalar comparisons and class identity, whose result cannot
							// change without replacing or exposing either local slot.
							bInvariant = bInvariantCandidate && bCompared
								&& IsImmutableComparisonValue(BorrowLocal(Slot))
								&& IsImmutableComparisonValue(BorrowLocal(RightSlot));
							if (bInvariant)
							{
								InvariantCompareOffsets[CacheIndex] = static_cast<std::uint32_t>(Ip);
								InvariantCompareResults[CacheIndex] = bCondition;
							}
						}
						if (!bCompared)
						{
							return FailVm();
						}
						const std::size_t BranchEnd = AfterIp + LocalCompareBranchTailSize;
						if (bInvariant
								&& BranchEnd + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
								&& static_cast<EOpcode>(Code[BranchEnd]) == EOpcode::IncLocal)
						{
							const std::size_t TailStart = BranchEnd
									+ Bytecode::InstructionSize(EOpcode::IncLocal);
							const std::uint16_t CounterSlot =
									TailStart + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
										&& static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal
									? Bytecode::ReadU16LE(Code.data() + TailStart + 1)
								: 0xFFFF;
							const std::uint16_t TotalSlot = Bytecode::ReadU16LE(Code.data() + BranchEnd + 1);
							std::size_t FalseTarget = 0;
							FCanonicalI32Loop Loop;
							FValue* const TotalLocal = BorrowLocal(TotalSlot);
							auto* const Total = TotalLocal
									? std::get_if<std::int32_t>(TotalLocal)
								: nullptr;
							if (Total
									&& IsJumpTargetInRange(BranchEnd,
										Bytecode::ReadI32LE(Code.data() + AfterIp + 5),
										CodeSize, FalseTarget)
									&& FalseTarget == TailStart
									&& TryMatchCanonicalI32Loop(Ip, TailStart, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, TotalSlot))
							{
								for (std::int64_t Current = *Loop.Counter;
									Current < Loop.Limit; ++Current)
								{
									*Loop.Counter = static_cast<std::int32_t>(Current);
									if (bCondition)
									{
										if (*Total == std::numeric_limits<std::int32_t>::max())
										{
											return Error("integer overflow");
										}
										++*Total;
									}
								}
								*Loop.Counter = Loop.Limit;
								Ip = Loop.ExitIp;
								break;
							}
						}
						if (!bCondition)
						{
							std::size_t Target = 0;
							if (!IsJumpTargetInRange(BranchEnd, Bytecode::ReadI32LE(Code.data() + AfterIp + 5), CodeSize, Target))
							{
								return Error("JIT linear: jump out of range");
							}
							Ip = Target;
						}
						else
						{
							Ip = BranchEnd;
						}
						break;
					}
					const auto TryReadScalarAt = [&](const std::size_t ScalarIp, FValue& OutValue, bool& bOutAvailable, std::size_t& OutEnd)
					{
						return TryReadLinearScalarOperand(*Vm, *Module, Code, ScalarIp, OutValue, bOutAvailable, OutEnd);
					};
					if (AfterIp < CodeSize)
					{
						// Exact stable-field reduction:
						//   for (...; counter < limit; counter++) total += object.field;
						// The body cannot replace the object, mutate the field, invoke host code, or
						// observe allocation identity. Resolve the rooted object/field once, then
						// retain per-iteration scalar arithmetic so signed overflow and floating-point
						// rounding occur at the same source iteration as the ordinary Linear path.
						constexpr std::size_t StableFieldReductionTailSize = 10;
						if (AfterIp + StableFieldReductionTailSize <= CodeSize
							&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::LoadField
							&& static_cast<EOpcode>(Code[AfterIp + 6]) == EOpcode::Add
							&& static_cast<EOpcode>(Code[AfterIp + 7]) == EOpcode::StoreLocal
							&& Bytecode::ReadU16LE(Code.data() + AfterIp + 8) == Slot)
						{
							const std::uint16_t ObjectSlot =
								Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
							const std::uint16_t NameIndex =
								Bytecode::ReadU16LE(Code.data() + AfterIp + 4);
							const std::size_t TailIp = AfterIp + StableFieldReductionTailSize;
							const std::uint16_t CounterSlot = TailIp + 3 <= CodeSize
								&& static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal
								? Bytecode::ReadU16LE(Code.data() + TailIp + 1)
								: 0xFFFF;
							FCanonicalI32Loop Loop;
							if (TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
								&& LoopDoesNotAliasMutatedLocal(Loop, Slot))
							{
								FValue* const Destination = BorrowLocal(Slot);
								FScriptObject* Object = nullptr;
								std::uint16_t FieldIndex = 0xFFFF;
								const FValue* Field = nullptr;
								if (Destination
									&& Vm->JitBorrowLocalScalarField(
										ObjectSlot, NameIndex, Object, FieldIndex, Field)
									&& Object && Field && Field != Destination)
								{
									FValue Probe = *Destination;
									EJitScalarValueKind ScalarKind = EJitScalarValueKind::None;
									bool bOverflow = false;
									if (TryAddExactScalar(Probe, *Field, ScalarKind, bOverflow))
									{
										if (bOverflow)
										{
											return Error("integer overflow");
										}
										for (std::int64_t Current = *Loop.Counter;
											Current < Loop.Limit; ++Current)
										{
											*Loop.Counter = static_cast<std::int32_t>(Current);
											if (!TryAddExactScalar(
													*Destination, *Field, ScalarKind, bOverflow)
												|| bOverflow)
											{
												return Error("integer overflow");
											}
										}
										*Loop.Counter = Loop.Limit;
										Ip = Loop.ExitIp;
										break;
									}
								}
							}
						}
						// Statement-form `dest += object.field` no
						// longer needs the historical reload/pop
						// tail. Borrow both rooted locals and
						// update the destination directly while
						// retaining field PIC, overflow and
						// ref-write semantics in the VM bridge.
						constexpr std::size_t ShortLocalAddFieldTailSize = 10;
						if (AfterIp + ShortLocalAddFieldTailSize <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::LoadField && static_cast<EOpcode>(Code[AfterIp + 6]) == EOpcode::Add && static_cast<EOpcode>(Code[AfterIp + 7]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + AfterIp + 8) == Slot)
						{
							bool bHandled = false;
							if (!Vm->JitTryAddLocalFromLocalField(Slot,
										Bytecode::ReadU16LE(Code.data() + AfterIp + 1),
										Bytecode::ReadU16LE(Code.data() + AfterIp + 4),
										static_cast<std::uint32_t>(AfterIp),
										bHandled))
							{
								return FailVm();
							}
							if (bHandled)
							{
								Ip = AfterIp + ShortLocalAddFieldTailSize;
								break;
							}
						}
						// Statement-form `accumulator +=
						// object.field` has a stable compiler
						// shape. Both values remain rooted in frame
						// locals, so update the scalar destination
						// in place without copying the object
						// handle or materializing a temporary field
						// FValue on every loop.
						constexpr std::size_t LocalAddFieldTailSize = 14;
						if (AfterIp + LocalAddFieldTailSize <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::LoadField && static_cast<EOpcode>(Code[AfterIp + 6]) == EOpcode::Add && static_cast<EOpcode>(Code[AfterIp + 7]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + AfterIp + 8) == Slot && static_cast<EOpcode>(Code[AfterIp + 10]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + AfterIp + 11) == Slot && static_cast<EOpcode>(Code[AfterIp + 13]) == EOpcode::Pop)
						{
							bool bHandled = false;
							if (!Vm->JitTryAddLocalFromLocalField(Slot,
										Bytecode::ReadU16LE(Code.data() + AfterIp + 1),
										Bytecode::ReadU16LE(Code.data() + AfterIp + 4),
										static_cast<std::uint32_t>(AfterIp),
										bHandled))
							{
								return FailVm();
							}
							if (bHandled)
							{
								Ip = AfterIp + LocalAddFieldTailSize;
								break;
							}
						}
						// Direct scalar assignment uses the same
						// hidden result temporary as other index
						// assignments. Read a local/constant key
						// and value, then write the typed table
						// without materializing the container
						// handle or assignment temporary on the
						// operand stack.
						{
							std::size_t IndexEnd = AfterIp;
							std::size_t ValueEnd = AfterIp;
							constexpr std::size_t AssignmentTailSize = 14;
							FValue IndexValue;
							FValue SourceValue;
							bool bIndexAvailable = false;
							bool bValueAvailable = false;
							if (TryReadLinearScalarExpression(*Vm, *Module, Code, AfterIp, IndexValue, bIndexAvailable, IndexEnd) && bIndexAvailable && TryReadLinearScalarExpression(*Vm, *Module, Code, IndexEnd, SourceValue, bValueAvailable, ValueEnd) && bValueAvailable && ValueEnd + AssignmentTailSize <= CodeSize && static_cast<EOpcode>(Code[ValueEnd]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[ValueEnd + 3]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + ValueEnd + 1) == Bytecode::ReadU16LE(Code.data() + ValueEnd + 4) && static_cast<EOpcode>(Code[ValueEnd + 6]) == EOpcode::SetIndex && static_cast<EOpcode>(Code[ValueEnd + 7]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + ValueEnd + 8) == Slot && static_cast<EOpcode>(Code[ValueEnd + 10]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + ValueEnd + 11) == Bytecode::ReadU16LE(Code.data() + ValueEnd + 4) && static_cast<EOpcode>(Code[ValueEnd + 13]) == EOpcode::Pop)
							{
								if (!Vm->HasError())
								{
									bool bHandled = false;
									FValue* const DirectArrayLocal = BorrowLocal(Slot);
									auto* const DirectArrayHandle = DirectArrayLocal
											? std::get_if<FDynamicArrayHandle>(DirectArrayLocal)
										: nullptr;
									auto* const DirectValues = DirectArrayHandle && *DirectArrayHandle
											&& (*DirectArrayHandle)->MutationOrigin.FieldIndex
												== static_cast<std::uint16_t>(-1)
											? std::get_if<std::vector<std::uint32_t>>(
												&(*DirectArrayHandle)->ScalarElements)
										: nullptr;
									const auto* const FastU32Index =
											std::get_if<std::int32_t>(&IndexValue);
									const auto* const Source =
											std::get_if<std::uint32_t>(&SourceValue);
									if (DirectValues && FastU32Index && Source)
									{
										bHandled = true;
										if (*FastU32Index < 0
												|| static_cast<std::size_t>(*FastU32Index) >= DirectValues->size())
										{
											return ErrorAt(IndexEnd, "array index out of bounds");
										}
										(*DirectValues)[static_cast<std::size_t>(*FastU32Index)] = *Source;
									}
									if (!bHandled && !Vm->JitTrySetDynamicArrayScalarOnLocal(
												Slot, IndexValue, SourceValue, bHandled))
									{
										return FailVm();
									}
									if (bHandled)
									{
										std::size_t FastPathCount = 1;
										std::size_t FastPathExitIp = 0;
										const std::size_t TailStart = ValueEnd + AssignmentTailSize;
										const EOpcode IndexOp = static_cast<EOpcode>(Code[AfterIp]);
										const std::size_t SimpleIndexEnd = AfterIp
												+ Bytecode::InstructionSize(IndexOp);
										std::uint16_t ValueCounterSlot = 0xFFFF;
										std::uint16_t ValueAddendSlot = 0xFFFF;
										std::int32_t ValueModulus = 0;
										const std::uint16_t TailCounterSlot =
												TailStart + Bytecode::InstructionSize(EOpcode::IncLocal) <= CodeSize
													&& static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal
												? Bytecode::ReadU16LE(Code.data() + TailStart + 1)
											: 0xFFFF;
										FCanonicalI32Loop Loop;
										const std::uint16_t IndexSlot = IndexOp == EOpcode::LoadLocal
												? Bytecode::ReadU16LE(Code.data() + AfterIp + 1)
											: 0xFFFF;
										FValue* const DirectIndex = IndexOp == EOpcode::LoadLocal
												? BorrowLocal(IndexSlot)
											: nullptr;
										FValue* const ArrayLocal = BorrowLocal(Slot);
										auto* const ArrayHandle = ArrayLocal
												? std::get_if<FDynamicArrayHandle>(ArrayLocal)
											: nullptr;
										auto* const Values = ArrayHandle && *ArrayHandle
												&& (*ArrayHandle)->MutationOrigin.FieldIndex
													== static_cast<std::uint16_t>(-1)
												? std::get_if<std::vector<std::int32_t>>(&(*ArrayHandle)->ScalarElements)
												: nullptr;
										const auto* const Index = std::get_if<std::int32_t>(&IndexValue);
										const bool bValueUsesCounterModulo =
												TryDescribeCounterModulo(
													IndexEnd, ValueEnd, ValueCounterSlot, ValueModulus);
										const bool bValueUsesInvariantAdd = !bValueUsesCounterModulo
												&& TryDescribeCounterAddLocal(
													IndexEnd, ValueEnd, ValueCounterSlot, ValueAddendSlot);
										FValue* const AddendLocal = bValueUsesInvariantAdd
												? BorrowLocal(ValueAddendSlot)
											: nullptr;
										const auto* const ValueAddend = AddendLocal
												? std::get_if<std::int32_t>(AddendLocal)
											: nullptr;
										if (bUseScalarArrayStorage && Values && Index
												&& IndexEnd == SimpleIndexEnd
												&& (IndexOp == EOpcode::ConstI32 || IndexOp == EOpcode::LoadLocal)
												&& (bValueUsesCounterModulo
													|| (bValueUsesInvariantAdd && ValueAddend))
												&& ValueCounterSlot == TailCounterSlot
												&& TryMatchCanonicalI32Loop(Ip, TailStart, TailCounterSlot, Loop)
												&& LoopDoesNotAliasMutatedLocal(Loop, Slot)
												&& (!bValueUsesInvariantAdd
													|| (ValueAddendSlot != Slot
														&& ValueAddendSlot != Loop.CounterSlot
														&& ValueAddendSlot != Loop.LimitSlot
														&& LocalIsNotMutatedInRange(
															Ip, TailStart, ValueAddendSlot)))
												&& (IndexOp != EOpcode::LoadLocal
													|| (DirectIndex && DirectIndex->index() == IndexValue.index()
														&& IndexSlot != Slot && IndexSlot != Loop.CounterSlot
														&& IndexSlot != Loop.LimitSlot)))
										{
											const std::int32_t First = *Loop.Counter;
											bool bReducedToFinalStore = false;
											if (static_cast<std::int64_t>(First) + 1 < Loop.Limit)
											{
												const std::int32_t FinalCounter = Loop.Limit - 1;
												std::int32_t FinalValue = 0;
												if (bValueUsesInvariantAdd)
												{
													bReducedToFinalStore = Runtime::CheckedAdd(
															FinalCounter, *ValueAddend, FinalValue);
												}
												else
												{
													FinalValue = ValueModulus == 0
															? FinalCounter
															: ApplyI32Remainder(FinalCounter, ValueModulus);
													bReducedToFinalStore = true;
												}
												if (bReducedToFinalStore)
												{
													(*Values)[static_cast<std::size_t>(*Index)] = FinalValue;
												}
											}
											if (!bReducedToFinalStore)
											{
												for (std::int64_t Next = static_cast<std::int64_t>(First) + 1;
													Next < Loop.Limit; ++Next)
												{
													*Loop.Counter = static_cast<std::int32_t>(Next);
													std::int32_t Value = 0;
													if (bValueUsesInvariantAdd)
													{
														if (!Runtime::CheckedAdd(
																	*Loop.Counter, *ValueAddend, Value))
														{
															return Error("integer overflow");
														}
													}
													else
													{
														Value = ValueModulus == 0
																? *Loop.Counter
																: ApplyI32Remainder(*Loop.Counter, ValueModulus);
													}
													(*Values)[static_cast<std::size_t>(*Index)] = Value;
												}
											}
											*Loop.Counter = Loop.Limit;
											FastPathCount = static_cast<std::size_t>(
													static_cast<std::int64_t>(Loop.Limit) - First);
											FastPathExitIp = Loop.ExitIp;
										}
										Vm->RecordJitArrayFastPathSet(FastPathCount);
										Ip = FastPathExitIp != 0 ? FastPathExitIp : TailStart;
										break;
									}
								}
							}
						}
						if (bUseScalarMapStorage)
						{
							std::size_t KeyEnd = AfterIp;
							std::size_t ValueEnd = AfterIp;
							constexpr std::size_t AssignmentTailSize = 14;
							FValue KeyValue;
							FValue SourceValue;
							bool bKeyAvailable = false;
							bool bValueAvailable = false;
							if (TryReadLinearScalarExpression(*Vm, *Module, Code, AfterIp, KeyValue, bKeyAvailable, KeyEnd) && bKeyAvailable && TryReadLinearScalarExpression(*Vm, *Module, Code, KeyEnd, SourceValue, bValueAvailable, ValueEnd) && bValueAvailable && ValueEnd + AssignmentTailSize <= CodeSize && static_cast<EOpcode>(Code[ValueEnd]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[ValueEnd + 3]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + ValueEnd + 1) == Bytecode::ReadU16LE(Code.data() + ValueEnd + 4) && static_cast<EOpcode>(Code[ValueEnd + 6]) == EOpcode::SetIndex && static_cast<EOpcode>(Code[ValueEnd + 7]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + ValueEnd + 8) == Slot && static_cast<EOpcode>(Code[ValueEnd + 10]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + ValueEnd + 11) == Bytecode::ReadU16LE(Code.data() + ValueEnd + 4) && static_cast<EOpcode>(Code[ValueEnd + 13]) == EOpcode::Pop)
							{
								if (!Vm->HasError())
								{
									bool bHandled = false;
									std::size_t FastPathCount = 1;
									std::size_t FastPathExitIp = 0;
									FValue* const MapLocal = BorrowLocal(Slot);
									auto* const MapHandle = MapLocal
											? std::get_if<FMapHandle>(MapLocal)
										: nullptr;
									auto* const Entries = MapHandle && *MapHandle
											? std::get_if<FMap::FIntI32Entries>(
												&(*MapHandle)->ScalarEntries)
										: nullptr;
									std::uint16_t KeyCounterSlot = 0xFFFF;
									std::uint16_t ValueCounterSlot = 0xFFFF;
									std::int32_t KeyModulus = 0;
									std::int32_t ValueModulus = 0;
									std::int32_t ValueAddend = 0;
									bool bValueUsesAddend = false;
									FCanonicalI32Loop Loop;
									const std::size_t TailStart =
											ValueEnd + AssignmentTailSize;
									const bool bValueDescribed =
											TryDescribeCounterModulo(
												KeyEnd, ValueEnd, ValueCounterSlot, ValueModulus)
											|| ((bValueUsesAddend = TryDescribeCounterAddImmediate(
													KeyEnd, ValueEnd, ValueCounterSlot, ValueAddend)));
									const bool bCanonicalBatch = Entries && MapHandle && *MapHandle
											&& (*MapHandle)->MutationOrigin.FieldIndex
												== static_cast<std::uint16_t>(-1)
											&& TryDescribeCounterModulo(
												AfterIp, KeyEnd, KeyCounterSlot, KeyModulus)
											&& bValueDescribed && KeyCounterSlot == ValueCounterSlot
											&& TryMatchCanonicalI32Loop(
												Ip, TailStart, KeyCounterSlot, Loop);
									const std::int32_t First = bCanonicalBatch
											? *Loop.Counter
										: 0;
									const std::int64_t NextValue =
											static_cast<std::int64_t>(First) + 1;
									const std::uint64_t TotalCount = bCanonicalBatch
											? static_cast<std::uint64_t>(
												static_cast<std::int64_t>(Loop.Limit) - First)
											: 0;
									const std::uint64_t RemainingCount = bCanonicalBatch
											&& Loop.Limit > NextValue
												? static_cast<std::uint64_t>(Loop.Limit - NextValue)
												: 0;
									constexpr std::uint64_t MaxAutomaticBulkReserve =
											std::uint64_t{1} << 20u;
									bool bUseGeneratedBatch = bCanonicalBatch && KeyModulus == 0
											&& TotalCount <= MaxAutomaticBulkReserve
											&& TotalCount <= static_cast<std::uint64_t>(
												std::numeric_limits<std::size_t>::max()
													- Entries->size());
									if (bUseGeneratedBatch && bValueUsesAddend
											&& RemainingCount != 0)
									{
										std::int32_t LastValue = 0;
										bUseGeneratedBatch = Runtime::CheckedAdd(
												static_cast<std::int32_t>(Loop.Limit - 1),
												ValueAddend,
												LastValue);
									}
									if (bUseGeneratedBatch)
									{
										Entries->reserve(
												Entries->size()
													+ static_cast<std::size_t>(TotalCount));
									}
									if (!Vm->JitTrySetScalarMapDirectOnLocal(Slot, KeyValue, SourceValue, bHandled))
									{
										return FailVm();
									}
									if (bHandled)
									{
										if (bCanonicalBatch)
										{
											// A direct counter key is unique for every admitted i32
											// iteration. The table was reserved before the first store;
											// generated insertion therefore performs no intermediate grow.
											if (bUseGeneratedBatch)
											{
												const std::size_t Count =
														static_cast<std::size_t>(RemainingCount);
												Entries->InsertOrAssignGenerated(
														Count,
														[&](const std::size_t Offset,
															std::int32_t& OutKey,
															std::int32_t& OutValue)
														{
															OutKey = static_cast<std::int32_t>(
																		NextValue + static_cast<std::int64_t>(Offset));
															if (bValueUsesAddend)
															{
																OutValue = static_cast<std::int32_t>(
																			static_cast<std::int64_t>(OutKey) + ValueAddend);
															}
															else
															{
																OutValue = ValueModulus == 0
																			? OutKey
																			: ApplyI32Remainder(OutKey, ValueModulus);
															}
														});
											}
											else
											{
												for (std::int64_t Next = NextValue; Next < Loop.Limit; ++Next)
												{
													*Loop.Counter = static_cast<std::int32_t>(Next);
													const std::int32_t BatchKey = KeyModulus == 0
															? *Loop.Counter
															: ApplyI32Remainder(*Loop.Counter, KeyModulus);
													std::int32_t BatchValue = 0;
													if (bValueUsesAddend)
													{
														if (!Runtime::CheckedAdd(
																	*Loop.Counter, ValueAddend, BatchValue))
														{
															return Error("integer overflow");
														}
													}
													else
													{
														BatchValue = ValueModulus == 0
																? *Loop.Counter
																: ApplyI32Remainder(*Loop.Counter, ValueModulus);
													}
													Entries->InsertOrAssign(BatchKey, BatchValue);
												}
											}
											*Loop.Counter = Loop.Limit;
											FastPathCount = static_cast<std::size_t>(
													static_cast<std::int64_t>(Loop.Limit) - First);
											FastPathExitIp = Loop.ExitIp;
										}
										Vm->RecordJitScalarMapFastPathSet(FastPathCount);
										Ip = FastPathExitIp != 0 ? FastPathExitIp : TailStart;
										break;
									}
								}
							}
						}
						// Stable-shape scalar field reduction. The
						// exact bytecode form keeps receiver
						// evaluation and mutation ordering
						// unchanged while letting the
						// generation-qualified field PIC resolve
						// the slot only once.
						if (AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal) + Bytecode::InstructionSize(EOpcode::LoadField) <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + AfterIp + 1) == Slot)
						{
							const std::size_t LoadFieldIp = AfterIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
							if (static_cast<EOpcode>(Code[LoadFieldIp]) == EOpcode::LoadField)
							{
								const std::uint16_t NameIndex = Bytecode::ReadU16LE(Code.data() + LoadFieldIp + 1);
								FValue Delta;
								bool bDeltaAvailable = false;
								std::size_t AddIp = LoadFieldIp + Bytecode::InstructionSize(EOpcode::LoadField);
								const std::size_t DeltaExpressionIp = AddIp;
								if (!TryReadLinearScalarExpression(*Vm, *Module, Code, AddIp, Delta, bDeltaAvailable, AddIp))
								{
									return FailVm();
								}
								const std::size_t ModulusIp = AddIp + Bytecode::InstructionSize(EOpcode::Add);
								const std::size_t ModIp = ModulusIp + Bytecode::InstructionSize(EOpcode::ConstI32);
								const std::size_t StoreFieldIp = ModIp + Bytecode::InstructionSize(EOpcode::Mod);
								const std::size_t PopIp = StoreFieldIp + Bytecode::InstructionSize(EOpcode::StoreField);
								if (bDeltaAvailable && PopIp + Bytecode::InstructionSize(EOpcode::Pop) <= CodeSize && static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add && static_cast<EOpcode>(Code[ModulusIp]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[ModIp]) == EOpcode::Mod && static_cast<EOpcode>(Code[StoreFieldIp]) == EOpcode::StoreField && Bytecode::ReadU16LE(Code.data() + StoreFieldIp + 1) == NameIndex && static_cast<EOpcode>(Code[PopIp]) == EOpcode::Pop)
								{
									const std::uint16_t ConstantIndex = Bytecode::ReadU16LE(Code.data() + ModulusIp + 1);
									if (ConstantIndex < Module->Constants.size() && Module->Constants[ConstantIndex].Kind == EConstantKind::I32)
									{
										bool bHandled = false;
										std::size_t FastPathExitIp = 0;
										const auto* const DeltaI32 = std::get_if<std::int32_t>(&Delta);
										const std::int32_t Modulus = Module->Constants[ConstantIndex].I32;
										FValue* const ObjectLocal = BorrowLocal(Slot);
										auto* const ObjectHandle = ObjectLocal ? std::get_if<FObjectHandle>(ObjectLocal) : nullptr;
										FDirectI32FieldCache& Cached = DirectI32FieldCache[Slot & (DirectI32FieldCache.size() - 1u)];
										FScriptObject* const CurrentObject = ObjectHandle && *ObjectHandle ? ObjectHandle->get() : nullptr;
										if (DeltaI32 && Modulus != 0 && CurrentObject
												&& !Vm->JitHasMutationCallback())
										{
											if (Cached.Slot != Slot
												|| Cached.NameIndex != NameIndex
												|| Cached.Object != CurrentObject
												|| !HasSameObjectLifetime(Cached.Lifetime, *ObjectHandle)
												|| !Cached.Storage)
											{
												Cached = {};
												Cached.Slot = Slot;
												Cached.NameIndex = NameIndex;
												if (!Vm->JitBorrowLocalI32Field(Slot, NameIndex, Cached.Object, Cached.FieldIndex, Cached.Storage))
												{
													return FailVm();
												}
												if (Cached.Storage)
												{
													Cached.Lifetime = *ObjectHandle;
												}
											}
											if (Cached.Object == CurrentObject
													&& HasSameObjectLifetime(Cached.Lifetime, *ObjectHandle)
													&& Cached.Storage)
											{
												bHandled = true;
												std::int32_t Sum = 0;
												if (!Runtime::CheckedAdd(*Cached.Storage, *DeltaI32, Sum) || (Sum == std::numeric_limits<std::int32_t>::min() && Modulus == -1))
												{
													return Error("integer overflow");
												}
												*Cached.Storage = Sum % Modulus;
												Vm->JitCommitBorrowedFieldMutation(*Cached.Object, Cached.FieldIndex);
												if (!Vm->JitHasMutationCallback())
												{
													std::uint16_t CounterSlot = 0xFFFF;
													std::int32_t DeltaModulus = 0;
													FCanonicalI32Loop Loop;
													const std::size_t TailStart = PopIp + Bytecode::InstructionSize(EOpcode::Pop);
													if (TryDescribeCounterModulo(DeltaExpressionIp, AddIp, CounterSlot, DeltaModulus) && TryMatchCanonicalI32Loop(Ip, TailStart, CounterSlot, Loop))
													{
														for (std::int64_t Next = static_cast<std::int64_t>(*Loop.Counter) + 1; Next < Loop.Limit; ++Next)
														{
															*Loop.Counter = static_cast<std::int32_t>(Next);
															const std::int32_t BatchDelta = DeltaModulus == 0
																? *Loop.Counter
																: ApplyI32Remainder(*Loop.Counter, DeltaModulus);
															if (!Runtime::CheckedAdd(*Cached.Storage, BatchDelta, Sum) || (Sum == std::numeric_limits<std::int32_t>::min() && Modulus == -1))
															{
																return Error("integer overflow");
															}
															*Cached.Storage = Sum % Modulus;
															++Cached.Object->MutationVersion;
														}
														*Loop.Counter = Loop.Limit;
														FastPathExitIp = Loop.ExitIp;
													}
												}
											}
										}
										if (!bHandled && !Vm->JitTryUpdateLocalFieldI32Modulo(Slot, NameIndex, Delta, Modulus, static_cast<std::uint32_t>(Ip), bHandled))
										{
											return FailVm();
										}
										if (bHandled)
										{
											Ip = FastPathExitIp != 0 ? FastPathExitIp : PopIp + Bytecode::InstructionSize(EOpcode::Pop);
											break;
										}
									}
								}
							}
						}
						// Compound field addition normally uses a
						// temporary so its expression result
						// remains available:
						//   LoadLocal s; LoadFieldKeep f; ConstI32
						//   c; Add; StoreLocal t; LoadLocal t;
						//   StoreField f; Pop; LoadLocal t; Pop
						// When the result is immediately discarded,
						// update the rooted local object's scalar
						// field directly. This preserves overflow
						// checks and the field mutation barrier
						// while avoiding temporary
						// FValue/shared_ptr traffic. It also makes
						// `field += c` use the same hot path as
						// `field = field + c`.
						constexpr std::size_t LocalFieldCompoundAddTailSize = 21;
						if (AfterIp + LocalFieldCompoundAddTailSize <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadFieldKeep && static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[AfterIp + 6]) == EOpcode::Add && static_cast<EOpcode>(Code[AfterIp + 7]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[AfterIp + 10]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + AfterIp + 8) == Bytecode::ReadU16LE(Code.data() + AfterIp + 11) && static_cast<EOpcode>(Code[AfterIp + 13]) == EOpcode::StoreField && Bytecode::ReadU16LE(Code.data() + AfterIp + 1) == Bytecode::ReadU16LE(Code.data() + AfterIp + 14) && static_cast<EOpcode>(Code[AfterIp + 16]) == EOpcode::Pop && static_cast<EOpcode>(Code[AfterIp + 17]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + AfterIp + 8) == Bytecode::ReadU16LE(Code.data() + AfterIp + 18) && static_cast<EOpcode>(Code[AfterIp + 20]) == EOpcode::Pop)
						{
							const std::uint16_t ConstantIndex = Bytecode::ReadU16LE(Code.data() + AfterIp + 4);
							if (ConstantIndex < Module->Constants.size() && Module->Constants[ConstantIndex].Kind == EConstantKind::I32)
							{
								bool bHandled = false;
								if (!TryAddCachedI32FieldImmediate(
										Slot, Bytecode::ReadU16LE(Code.data() + AfterIp + 1), Module->Constants[ConstantIndex].I32, bHandled))
								{
									return FailVmAt(AfterIp + 6u);
								}
								if (bHandled)
								{
									Ip = AfterIp + LocalFieldCompoundAddTailSize;
									break;
								}
							}
						}
						// The compiler's ordinary assignment
						// sequence roots the target object on the
						// operand stack before evaluating the right
						// side. For this exact side-effect-free
						// scalar form, the frame local is already
						// an equally strong root, so avoid the
						// shared_ptr copy:
						//   LoadLocal s; LoadLocal s; LoadField f;
						//   ConstI32 c; Add; StoreField f; Pop
						constexpr std::size_t LocalFieldAddTailSize = 14;
						if (AfterIp + LocalFieldAddTailSize <= CodeSize && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + AfterIp + 1) == Slot && static_cast<EOpcode>(Code[AfterIp + 3]) == EOpcode::LoadField && static_cast<EOpcode>(Code[AfterIp + 6]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[AfterIp + 9]) == EOpcode::Add && static_cast<EOpcode>(Code[AfterIp + 10]) == EOpcode::StoreField && Bytecode::ReadU16LE(Code.data() + AfterIp + 4) == Bytecode::ReadU16LE(Code.data() + AfterIp + 11) && static_cast<EOpcode>(Code[AfterIp + 13]) == EOpcode::Pop)
						{
							const std::uint16_t ConstantIndex = Bytecode::ReadU16LE(Code.data() + AfterIp + 7);
							if (ConstantIndex < Module->Constants.size() && Module->Constants[ConstantIndex].Kind == EConstantKind::I32)
							{
								bool bHandled = false;
								if (!TryAddCachedI32FieldImmediate(
										Slot, Bytecode::ReadU16LE(Code.data() + AfterIp + 4), Module->Constants[ConstantIndex].I32, bHandled))
								{
									return FailVmAt(AfterIp + 9u);
								}
								if (bHandled)
								{
									Ip = AfterIp + LocalFieldAddTailSize;
									break;
								}
							}
						}
						const EOpcode NextOp = static_cast<EOpcode>(Code[AfterIp]);
						const bool bSimpleScalar =
								NextOp == EOpcode::ConstBool || NextOp == EOpcode::ConstI32
								|| NextOp == EOpcode::ConstF64 || NextOp == EOpcode::ConstStr
								|| NextOp == EOpcode::LoadLocal;
						const auto TryReadSimpleScalar = [&](FValue& OutValue, bool& bOutAvailable)
						{
							std::size_t IgnoredEnd = AfterIp;
							return TryReadScalarAt(AfterIp, OutValue, bOutAvailable, IgnoredEnd);
						};
						if (NextOp == EOpcode::LoadField && AfterIp + Bytecode::InstructionSize(NextOp) <= CodeSize)
						{
							FValue FieldValue;
							if (!Vm->JitLoadLocalField(Slot, Bytecode::ReadU16LE(Code.data() + AfterIp + 1), FieldValue))
							{
								return FailVm();
							}
							Stack.push_back(std::move(FieldValue));
							Ip = AfterIp + Bytecode::InstructionSize(NextOp);
							break;
						}
						// A local scalar container and its index expression are already
						// rooted in the frame. Decode the complete side-effect-free scalar
						// expression and borrow the container instead of copying its shared
						// handle through the operand stack. This covers arbitrary arithmetic,
						// checked numeric conversions and nested scalar container reads; it is
						// intentionally not limited to a single constant/local operand.
						FValue* const CandidateIndexContainer = BorrowLocal(Slot);
						// Do not speculate through the left side of an indexed assignment.
						// Its bytecode roots the target container before evaluating the key
						// and right side; decoding that whole suffix as a read expression could
						// otherwise execute a failing RHS merely while probing a fast path.
						bool bIndexedAssignmentTarget = false;
						std::size_t AssignmentScan = AfterIp;
						std::size_t AssignmentScanCount = 0;
						while (AssignmentScan < CodeSize && AssignmentScanCount++ < 64u)
						{
							const EOpcode ScanOp = static_cast<EOpcode>(Code[AssignmentScan]);
							const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
							if (ScanSize == 0 || ScanSize > CodeSize - AssignmentScan)
							{
								break;
							}
							if (ScanOp == EOpcode::SetIndex)
							{
								const std::size_t StoreContainerIp = AssignmentScan + ScanSize;
								bIndexedAssignmentTarget = StoreContainerIp
										+ Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize
										&& static_cast<EOpcode>(Code[StoreContainerIp])
											== EOpcode::StoreLocal
										&& Bytecode::ReadU16LE(Code.data() + StoreContainerIp + 1u) == Slot;
								break;
							}
							if (ScanOp == EOpcode::Jmp || ScanOp == EOpcode::JmpIfFalse
									|| ScanOp == EOpcode::Return || ScanOp == EOpcode::Throw)
							{
								break;
							}
							AssignmentScan += ScanSize;
						}
						if (!bIndexedAssignmentTarget && CandidateIndexContainer
								&& (std::holds_alternative<FDynamicArrayHandle>(*CandidateIndexContainer)
									|| std::holds_alternative<FMapHandle>(*CandidateIndexContainer)))
						{
							FValue IndexValue;
							bool bIndexAvailable = false;
							std::size_t IndexEnd = AfterIp;
							if (!TryReadLinearScalarExpression(
										*Vm, *Module, Code, AfterIp,
										IndexValue, bIndexAvailable, IndexEnd, true))
							{
								return FailVmAt(AfterIp);
							}
							if (bIndexAvailable && IndexEnd < CodeSize
									&& static_cast<EOpcode>(Code[IndexEnd]) == EOpcode::GetIndexCopy)
							{
								FValue FastValue;
								bool bFastMapGet = false;
								bool bFastArrayGet = false;
								// Bridge helpers report synchronously through the VM. Point the
								// active location at the actual indexing opcode before entering
								// them, then restore the current Linear instruction on success.
								Vm->JitRefineRuntimeErrorLocation(IndexEnd);
								if (!Vm->JitTryGetScalarIndexOnLocal(
											Slot, IndexValue, FastValue,
											bFastMapGet, bFastArrayGet))
								{
									return FailVmAt(IndexEnd);
								}
								Vm->JitRefineRuntimeErrorLocation(Ip);
								if (bFastMapGet || bFastArrayGet)
								{
									if (bFastMapGet)
									{
										Vm->RecordJitScalarMapFastPathGet();
									}
									else
									{
										Vm->RecordJitArrayFastPathGet();
									}
									Stack.push_back(std::move(FastValue));
									Ip = IndexEnd
											+ Bytecode::InstructionSize(EOpcode::GetIndexCopy);
									break;
								}
							}
						}
						if (NextOp == EOpcode::CallMethod && AfterIp + Bytecode::InstructionSize(NextOp) <= CodeSize && Code[AfterIp + 3] == 0)
						{
							const std::uint16_t MethodNameIndex = Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
							FValue FastReturnValue;
							bool bFastScalarCall = false;
							if (!Vm->JitTryCallScalarMethodOnLocalNoArgs(
									Slot, MethodNameIndex, static_cast<std::uint32_t>(AfterIp), FastReturnValue, bFastScalarCall))
							{
								return FailVm();
							}
							if (bFastScalarCall)
							{
								Stack.push_back(std::move(FastReturnValue));
								Ip = AfterIp + Bytecode::InstructionSize(NextOp);
								break;
							}
							if (!Vm->JitCallMethodOnLocal(Slot, MethodNameIndex, nullptr, 0, static_cast<std::uint32_t>(AfterIp)))
							{
								return FailVm();
							}
							FValue ReturnValue;
							if (!Vm->JitTakeReturn(ReturnValue))
							{
								return FailVm();
							}
							Stack.push_back(std::move(ReturnValue));
							Ip = AfterIp + Bytecode::InstructionSize(NextOp);
							break;
						}
						// One pure scalar argument is the common
						// dynamic-call shape. It is safe to borrow
						// the receiver because evaluating a
						// constant/local cannot rebind or destroy
						// the receiver.
						if (bSimpleScalar)
						{
							const std::size_t CallIp = AfterIp + Bytecode::InstructionSize(NextOp);
							if (CallIp + Bytecode::InstructionSize(EOpcode::CallMethod) <= CodeSize && static_cast<EOpcode>(Code[CallIp]) == EOpcode::CallMethod && Code[CallIp + 3] == 1)
							{
								if (NextOp == EOpcode::LoadLocal)
								{
									const std::uint16_t MethodNameIndex = Bytecode::ReadU16LE(Code.data() + CallIp + 1);
									const std::size_t CallEnd = CallIp + Bytecode::InstructionSize(EOpcode::CallMethod);
									const bool bDiscardReturn = CallEnd < CodeSize && static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Pop;
									FValue FastReturnValue;
									bool bFastContainerCall = false;
									if (!Vm->JitTryCallScalarContainerMethodOnLocals(Slot,
												MethodNameIndex,
												Bytecode::ReadU16LE(Code.data() + AfterIp + 1),
												bDiscardReturn,
												FastReturnValue,
												bFastContainerCall))
									{
										return FailVm();
									}
									if (bFastContainerCall)
									{
										if (!bDiscardReturn)
										{
											Stack.push_back(std::move(FastReturnValue));
										}
										Ip = bDiscardReturn ? CallEnd + Bytecode::InstructionSize(EOpcode::Pop) : CallEnd;
										break;
									}
								}
								FValue Argument;
								bool bArgumentAvailable = false;
								if (!TryReadSimpleScalar(Argument, bArgumentAvailable))
								{
									return FailVm();
								}
								if (bArgumentAvailable)
								{
									const std::uint16_t MethodNameIndex = Bytecode::ReadU16LE(Code.data() + CallIp + 1);
									const std::size_t CallEnd = CallIp + Bytecode::InstructionSize(EOpcode::CallMethod);
									if (MethodNameIndex < Module->StringPool.size() && Module->StringPool[MethodNameIndex] == "push" && CallEnd < CodeSize && static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Pop)
									{
										bool bFastArrayPush = false;
										if (!Vm->JitTryPushDynamicArrayScalarOnLocal(Slot, Argument, bFastArrayPush))
										{
											return FailVm();
										}
										if (bFastArrayPush)
										{
											Vm->RecordJitArrayFastPathPush();
											Ip = CallEnd + Bytecode::InstructionSize(EOpcode::Pop);
											break;
										}
									}
									FValue FastReturnValue;
									bool bFastScalarCall = false;
									const bool bDiscardContainerReturn = CallEnd < CodeSize && static_cast<EOpcode>(Code[CallEnd]) == EOpcode::Pop;
									if (!Vm->JitTryCallScalarContainerMethodOnLocal(
											Slot, MethodNameIndex, &Argument, 1, bDiscardContainerReturn, FastReturnValue, bFastScalarCall))
									{
										return FailVm();
									}
									if (bFastScalarCall)
									{
										if (!bDiscardContainerReturn)
										{
											Stack.push_back(std::move(FastReturnValue));
										}
										Ip = bDiscardContainerReturn ? CallEnd + Bytecode::InstructionSize(EOpcode::Pop) : CallEnd;
										break;
									}
									// NativeCfg scalar method ABIs
									// intentionally exclude owned
									// strings. String locals are exposed
									// here only for typed Set operations;
									// do not resolve an object method
									// that must fall back immediately
									// after its parameter pack check.
									const bool bObjectScalarArgument =
										!std::holds_alternative<std::string>(Argument) && !std::holds_alternative<std::u16string>(Argument);
									if (bObjectScalarArgument)
									{
										std::int32_t DirectResult = 0;
										if (!TryInvokeDirectTransformMethod(
												Slot, MethodNameIndex, 0xFFFF, CallIp,
												Argument, DirectResult, bFastScalarCall))
										{
											return FailVm();
										}
										if (bFastScalarCall)
										{
											FastReturnValue = DirectResult;
										}
									}
									if (bObjectScalarArgument && !bFastScalarCall)
									{
										bool bDirectFieldHandled = false;
										std::int32_t DirectFieldReturn = 0;
										if (!TryInvokeDirectFieldMethod(
												Slot, MethodNameIndex, 0xFFFF, CallIp,
												Argument, DirectFieldReturn, bDirectFieldHandled))
										{
											return FailVm();
										}
										if (bDirectFieldHandled)
										{
											FastReturnValue = DirectFieldReturn;
											bFastScalarCall = true;
										}
									}
									if (bObjectScalarArgument && !bFastScalarCall && !Vm->JitTryCallScalarMethodOnLocal(Slot, MethodNameIndex, &Argument, 1, static_cast<std::uint32_t>(CallIp), FastReturnValue, bFastScalarCall))
									{
										return FailVm();
									}
									if (bFastScalarCall)
									{
										Stack.push_back(std::move(FastReturnValue));
										Ip = CallIp + Bytecode::InstructionSize(EOpcode::CallMethod);
										break;
									}
								}
							}
						}
					}
					// Borrowed string-array comparison:
					//   left[indexA] ==/!= right[indexB]
					// All four inputs remain rooted locals, so neither indexed value needs
					// an owned FValue/string copy.
					constexpr std::size_t StringArrayCompareTailSize = 12u;
					if (AfterIp + StringArrayCompareTailSize <= CodeSize
							&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 3u]) == EOpcode::GetIndexCopy
							&& static_cast<EOpcode>(Code[AfterIp + 4u]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 7u]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 10u]) == EOpcode::GetIndexCopy
							&& (static_cast<EOpcode>(Code[AfterIp + 11u]) == EOpcode::Eq
								|| static_cast<EOpcode>(Code[AfterIp + 11u]) == EOpcode::Ne))
					{
						bool bResult = false;
						bool bHandled = false;
						const EOpcode CompareOp = static_cast<EOpcode>(Code[AfterIp + 11u]);
						if (!Vm->JitTryCompareStringArrayElements(
									Slot,
									Bytecode::ReadU16LE(Code.data() + AfterIp + 1u),
									Bytecode::ReadU16LE(Code.data() + AfterIp + 5u),
									Bytecode::ReadU16LE(Code.data() + AfterIp + 8u),
									CompareOp == EOpcode::Eq,
									bResult,
									bHandled))
						{
							return FailVmAt(AfterIp + 3u);
						}
						if (bHandled)
						{
							Stack.emplace_back(bResult);
							Ip = AfterIp + StringArrayCompareTailSize;
							break;
						}
					}

					// Fuse the common field-update/dataflow pair emitted by
					// `object.field++; accumulator += object.field`. Matching names and
					// slots makes this shape-independent: one stable shape lookup serves
					// every arm of a branch-selected field update.
					constexpr std::size_t IncAccumulateFieldTailSize = 16u;
					if (AfterIp + IncAccumulateFieldTailSize <= CodeSize
							&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::IncField
							&& static_cast<EOpcode>(Code[AfterIp + 3u]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 6u]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[AfterIp + 9u]) == EOpcode::LoadField
							&& static_cast<EOpcode>(Code[AfterIp + 12u]) == EOpcode::Add
							&& static_cast<EOpcode>(Code[AfterIp + 13u]) == EOpcode::StoreLocal)
					{
						const std::uint16_t FieldName =
								Bytecode::ReadU16LE(Code.data() + AfterIp + 1u);
						const std::uint16_t AccumulatorSlot =
								Bytecode::ReadU16LE(Code.data() + AfterIp + 4u);
						if (Bytecode::ReadU16LE(Code.data() + AfterIp + 7u) == Slot
								&& Bytecode::ReadU16LE(Code.data() + AfterIp + 10u) == FieldName
								&& Bytecode::ReadU16LE(Code.data() + AfterIp + 14u)
									== AccumulatorSlot)
						{
							bool bHandled = false;
							EJitFieldAccumulateFailure Failure =
									EJitFieldAccumulateFailure::None;
							if (!Vm->JitTryIncAndAccumulateLocalI32Field(
										Slot, FieldName, AccumulatorSlot, bHandled,
										Failure))
							{
								const std::size_t ErrorIp = Failure
										== EJitFieldAccumulateFailure::AccumulatorAddition
										? AfterIp + 12u
										: AfterIp;
								return FailVmAt(ErrorIp);
							}
							if (bHandled)
							{
								Ip = AfterIp + IncAccumulateFieldTailSize;
								break;
							}
						}
					}

					// The frame-local object remains rooted for the complete Linear span.
					// Fuse a generic postfix/prefix field increment before materializing a
					// copied object FValue on the operand stack. This is independent of loop
					// shape and applies equally to straight-line and branch-heavy code.
					if (AfterIp + Bytecode::InstructionSize(EOpcode::IncField) <= CodeSize
							&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::IncField)
					{
						bool bHandled = false;
						if (!Vm->JitTryIncLocalField(
									Slot,
									Bytecode::ReadU16LE(Code.data() + AfterIp + 1),
									bHandled))
						{
							return FailVmAt(AfterIp);
						}
						if (bHandled)
						{
							Ip = AfterIp + Bytecode::InstructionSize(EOpcode::IncField);
							break;
						}
					}

					FValue V;
					if (!Vm->JitLoadLocal(Slot, V))
					{
						return FailVm();
					}
					Stack.push_back(std::move(V));
					Ip = AfterIp;
					break;
				}

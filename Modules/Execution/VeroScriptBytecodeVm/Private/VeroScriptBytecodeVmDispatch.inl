// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// One shared dispatch body. The ordinary preprocessed body stays unchanged.
		static_assert(!bBatchArithmetic || (bGoverned && !bInstrumented));
		const auto StateView = State;
		[[maybe_unused]] std::uint32_t LastBreakpointLine = 0;
		[[maybe_unused]] std::string LastBreakpointFile;
		const auto HasTrivialRelease = [](const FValue& Value)
		{
			return Runtime::IsNumericValue(Value) || std::holds_alternative<bool>(Value) || std::holds_alternative<std::string>(Value) || std::holds_alternative<std::u16string>(Value) || std::holds_alternative<FEnumValue>(Value);
		};
		const auto RecycleFrame = [&](FCallFrame& Finished, std::size_t LocalCount)
		{
			// Core selects instrumented dispatch when diagnostic tokens are enabled.
			// Leave ordinary dispatch free of the optional completion machinery.
			if constexpr (bInstrumented)
			{
				if (Finished.DiagnosticToken)
				{
					Services.EndFunctionDiagnostic(Finished.DiagnosticToken);
					Finished.DiagnosticToken = 0;
				}
			}
			// Drop these owners before subsequent return cleanup observes them.
			Finished.ThisObject.reset();
			Finished.ReturnValue = std::int32_t{0};
			auto& FreeFrames = StateView.GetFreeFrames();
			if (!VmDetail::CanRecycleFrame(FreeFrames.size(), Finished.Locals.capacity())) return;
			if (Finished.Locals.size() == LocalCount)
			{
				std::fill(Finished.Locals.begin(), Finished.Locals.end(), FValue{std::int32_t{0}});
			}
			else
			{
				// ReleaseValueList cleared the vector. Refill only when retained.
				Finished.Locals.resize(LocalCount);
			}
			Finished.Function = nullptr;
			Finished.InstructionPointer = 0;
			Finished.LastInstructionOffset = 0;
			Finished.StackBase = 0;
			Finished.bHasReturn = false;
			Finished.bExpressionEval = false;
			Finished.bDisableNestedJit = false;
			// Entry checks the function/module/generation before reusing lookup caches.
			// Keep their metadata with the pooled local storage until then.
			FreeFrames.push_back(std::move(Finished));
		};
		const FSourceLocation InitializerLocation;
#if VEROSCRIPT_VM_ENHANCED_DISPATCH
		[[maybe_unused]] VmIndexedExpressions::FCache LocalExpressions;
		LocalExpressions.BindState(StateView, bEnableIndexedExpressions);
#else
		[[maybe_unused]] VmLocalExpressions::FCache LocalExpressions;
#endif
		while (!StateView.GetErrorFlag())
		{
			if (StateView.GetFrames().size() <= StateView.GetStopFrameDepth())
			{
				return true;
			}
			if constexpr (bGlobalInitializing)
			{
				if (StateView.HasDemandGlobalInitialization() && !Services.PrepareGlobalInitialization()) return false;
			}
			FCallFrame& Frame = StateView.GetFrames().back();
			const FBytecodeFunction* ActiveFunction = Frame.Function;
			const std::vector<std::uint8_t>& ActiveCode = ActiveFunction
			    ? ActiveFunction->Code
			    : StateView.GetActiveModule()->InitCode;
			if constexpr (bBatchArithmetic)
			{
				const auto Offset = Frame.InstructionPointer;
				// The quota dispatcher already borrows the active frame and code.
				// Marker, cleanup, reuse and failure boundaries use the shared checker.
				if (Offset < ActiveCode.size() && static_cast<EOpcode>(ActiveCode[Offset]) != EOpcode::Nop &&
				    !StateView.GetExecutionCleanup() && !StateView.GetResetRequiredFlag() &&
				    StateView.GetExecutionGovernance().Charge(1, StateView.GetExecutionLimits().MaxInstructions))
				{
					StateView.GetActiveInstructionStart() = Offset;
					Frame.LastInstructionOffset = Offset;
				}
				else if (!CheckExecutionStep<true>(ActiveFunction, Offset))
				{
					return false;
				}
			}
			else if constexpr (bGoverned)
			{
				if (!CheckExecutionStep<false>(ActiveFunction, Frame.InstructionPointer)) return false;
			}
			if constexpr (bInstrumented)
			{
				if (StateView.GetHotReloadInitializingFlag() && ActiveFunction == nullptr)
				{
					// Skip bytes belonging to retained globals' initializers, but the
					// punch-hole map (built in ExecuteInternal) already re-enabled any
					// NON-retained initializer nested inside a retained outer range, so
					// nested generic static inits still run. A retained global's own
					// StoreGlobal is separately ignored (see EOpcode::StoreGlobal), so
					// running a punched hole never clobbers a retained slot.
					if (Frame.InstructionPointer < StateView.GetHotReloadSkipInitByte().size() && StateView.GetHotReloadSkipInitByte()[Frame.InstructionPointer])
					{
						while (Frame.InstructionPointer < ActiveCode.size() && Frame.InstructionPointer < StateView.GetHotReloadSkipInitByte().size() && StateView.GetHotReloadSkipInitByte()[Frame.InstructionPointer])
						{
							++Frame.InstructionPointer;
						}
						continue;
					}
				}
			}
			// Plain dispatch borrows the function's location. Only a mapped
			// debugger instruction needs a private line and column override.
			const FSourceLocation* Location = ActiveFunction ? &ActiveFunction->Location : &InitializerLocation;
			[[maybe_unused]] FSourceLocation DebugLocation;
			if constexpr (bInstrumented)
			{
				if (StateView.GetDebugger() && StateView.GetActiveModule())
				{
					const std::vector<FDebugLine>& DebugLines = ActiveFunction
					    ? ActiveFunction->DebugLines
					    : StateView.GetActiveModule()->InitDebugLines;
					if (Frame.InstructionPointer < DebugLines.size() && DebugLines[Frame.InstructionPointer].Line != 0)
					{
						const FDebugLine& DebugLine = DebugLines[Frame.InstructionPointer];
						DebugLocation = *Location;
						DebugLocation.Line = DebugLine.Line;
						DebugLocation.Column = DebugLine.Column;
						Location = &DebugLocation;
						if (DebugLine.FileIndex < StateView.GetActiveModule()->DebugFiles.size() && !StateView.GetActiveModule()->DebugFiles[DebugLine.FileIndex].empty())
						{
							DebugLocation.FileLabel = StateView.GetActiveModule()->DebugFiles[DebugLine.FileIndex];
						}
					}
				}
			}

			const FSourceLocation& SourceLocation = *Location;
			if (Frame.InstructionPointer >= ActiveCode.size())
			{
				if (Frame.bExpressionEval)
				{
					const std::size_t FinishedDepth = StateView.GetFrames().size() - 1;
					const std::size_t ResultStackDepth = StateView.GetOperandStack().size();
					if (!Services.DiscardExecutionAbove(
					        FinishedDepth,
					        ResultStackDepth,
					        SourceLocation))
					{
						return false;
					}
					continue;
				}
				if (ActiveFunction && !ActiveFunction->bReturnsVoid && !Frame.bHasReturn)
				{
					Services.ReportErrorId(
					    "diag.missing_return_in_function",
					    ActiveFunction->Location,
					    {ActiveFunction->Name});
					return false;
				}
				const bool bVoidFunction = ActiveFunction && ActiveFunction->bReturnsVoid;
				const std::size_t StackBase = Frame.StackBase;
				FValue PreservedReturn;
				bool bPreserveReturn = false;
				if (!bVoidFunction && StateView.GetOperandStack().size() > StackBase)
				{
					PreservedReturn = std::move(StateView.GetOperandStack().back());
					StateView.GetOperandStack().pop_back();
					bPreserveReturn = true;
				}
				// Release leftover operand temps first so shared LoadLocal struct views
				// drop before locals are destroyed (use_count-based ReleaseValue).
				while (StateView.GetOperandStack().size() > StackBase)
				{
					FValue Discarded = Pop();
					if (!Services.ReleaseValue(Discarded, SourceLocation))
					{
						return false;
					}
				}
				// Plain values cannot invoke a language destructor or change error
				// state. A non-final receiver is equally inert; constructors and pending
				// transfers retain the complete teardown path below.
				const FCallFrame& CompletedFrame = StateView.GetFrames().back();
				if (!StateView.GetErrorFlag() && !StateView.GetPendingExceptionFlag() && !StateView.GetExitRequestedFlag() && CompletedFrame.FinallyContinuations.empty() && HasTrivialRelease(CompletedFrame.ReturnValue) && (!CompletedFrame.ThisObject || (!CompletedFrame.ThisObject->bConstructionIncomplete && CompletedFrame.ThisObject.use_count() > 1)) && std::all_of(CompletedFrame.Locals.begin(), CompletedFrame.Locals.end(), HasTrivialRelease))
				{
					FCallFrame Finished = std::move(StateView.GetFrames().back());
					PopRuntimeFrame();
					RecycleFrame(Finished, Finished.Locals.size());
					if (bVoidFunction)
					{
						Push(std::int32_t{0});
					}
					else if (bPreserveReturn)
					{
						Push(std::move(PreservedReturn));
					}
					continue;
				}
				// Move locals out before nested destructor Run() can reallocate Frames.
				FCallFrame Finished = std::move(StateView.GetFrames().back());
				PopRuntimeFrame();
				std::weak_ptr<FScriptObject> ConstructedObject;
				std::uint16_t ConstructedType = 0xFFFF;
				std::optional<FObjectLifetimeScope> ConstructionStage;
				if (Finished.ThisObject && Finished.ThisObject->bConstructionIncomplete && Finished.Function && Finished.Function->bReturnsVoid && Finished.Function->bIsMethod && Finished.Function->OwnerTypeIndex < StateView.GetActiveModule()->Types.size() && MethodLinkageBareLeaf(Finished.Function->Name) == Host.ModuleMeta_TypeSimpleName(StateView.GetActiveModule()->Types[Finished.Function->OwnerTypeIndex].Name))
				{
					ConstructedObject = Finished.ThisObject;
					ConstructedType = Finished.Function->OwnerTypeIndex;
					ConstructionStage.emplace(StateView.GetObjectLifetimeScope(), StateView.GetFrames().size(), Finished.ThisObject, ConstructedType);
				}
				// Save outer teardown refine state: nested DestroyObject→Run() may also
				// pop frames and must not wipe the caller's refine context.
				const bool bPrevTeardownActive = StateView.GetTeardownErrorActiveFlag();
				const FBytecodeFunction* const PrevTeardownFunction = StateView.GetTeardownErrorFunction();
				const std::size_t PrevTeardownIp = StateView.GetTeardownErrorIp();
				const std::size_t PrevTeardownDepth = StateView.GetTeardownFrameDepth();
				StateView.GetTeardownErrorActiveFlag() = true;
				StateView.GetTeardownErrorFunction() = Finished.Function;
				StateView.GetTeardownErrorIp() = StateView.GetActiveInstructionStart();
				StateView.GetTeardownFrameDepth() = StateView.GetFrames().size();
				const bool bHadPendingError = StateView.GetErrorFlag();
				FScriptError PendingError = StateView.GetError();
				StateView.GetErrorFlag() = false;
				StateView.GetError() = {};
				bool bCleanupOk = true;
				FScriptError FirstCleanupError;
				const auto RecordCleanupFailure = [&]()
				{
					VmDetail::RecordCleanupFailure(bCleanupOk, FirstCleanupError, StateView.GetErrorFlag(), StateView.GetError());
				};
				std::vector<FValue>& LocalsToRelease = Finished.Locals;
				const std::size_t LocalCount = LocalsToRelease.size();
				if (!Services.ReleaseFinallyContinuations(Finished, SourceLocation))
				{
					RecordCleanupFailure();
				}
				if (!Services.ReleaseValueList(LocalsToRelease, SourceLocation))
				{
					RecordCleanupFailure();
				}
				if (Finished.ThisObject)
				{
					if ((!bCleanupOk || StateView.GetErrorFlag() || StateView.GetPendingExceptionFlag()) && !Services.DestroyAbortedConstruction(Finished, SourceLocation))
					{
						RecordCleanupFailure();
					}
					FValue ReceiverOwner = std::move(Finished.ThisObject);
					Finished.ThisObject.reset();
					if (!Services.ReleaseValue(ReceiverOwner, SourceLocation))
					{
						RecordCleanupFailure();
					}
				}
				bCleanupOk = VmDetail::FinishCleanup(bCleanupOk, bHadPendingError, std::move(PendingError),
				    std::move(FirstCleanupError), StateView.GetExitRequestedFlag(), StateView.GetErrorFlag(), StateView.GetError());
				StateView.GetTeardownErrorActiveFlag() = bPrevTeardownActive;
				StateView.GetTeardownErrorFunction() = PrevTeardownFunction;
				StateView.GetTeardownErrorIp() = PrevTeardownIp;
				StateView.GetTeardownFrameDepth() = PrevTeardownDepth;
				RecycleFrame(Finished, LocalCount);
				if (!bCleanupOk)
				{
					if (bPreserveReturn)
					{
						const bool bHadCleanupError = StateView.GetErrorFlag();
						FScriptError CleanupError = std::move(StateView.GetError());
						StateView.GetErrorFlag() = false;
						StateView.GetError() = {};
						const bool bReturnReleased = Services.ReleaseValue(PreservedReturn, SourceLocation);
						FScriptError ReturnCleanupError = std::move(StateView.GetError());
						(void)VmDetail::FinishCleanup(bReturnReleased, bHadCleanupError, std::move(CleanupError),
						    std::move(ReturnCleanupError), StateView.GetExitRequestedFlag(), StateView.GetErrorFlag(), StateView.GetError());
					}
					return false;
				}
				if (bVoidFunction)
				{
					if (!StateView.GetPendingExceptionFlag())
					{
						if (auto Object = ConstructedObject.lock())
						{
							Services.CompleteObjectConstruction(Object, ConstructedType);
						}
					}
					Push(std::int32_t{0});
				}
				else if (bPreserveReturn)
				{
					Push(std::move(PreservedReturn));
				}
				continue;
			}

			if constexpr (bInstrumented)
			{
				Services.DebugBeforeInstruction(ActiveFunction, SourceLocation, LastBreakpointLine, LastBreakpointFile);
				if (StateView.GetErrorFlag()) return false;
			}

			const std::uint8_t* const Instruction = ActiveCode.data() + Frame.InstructionPointer;
			const EOpcode Opcode = static_cast<EOpcode>(Instruction[0]);
			const std::size_t InstructionByteSize = InstructionByteSizes[static_cast<std::uint8_t>(Opcode)];
			if (InstructionByteSize == 0 || Frame.InstructionPointer + InstructionByteSize > ActiveCode.size())
			{
				// Invalid encodings must report this opcode, not the previous one.
				StateView.GetActiveInstructionStart() = Frame.InstructionPointer;
				if (ActiveFunction && !ActiveFunction->ExceptionRegions.empty())
				{
					Frame.LastInstructionOffset = StateView.GetActiveInstructionStart();
				}
				Services.ReportErrorId("diag.truncated_or_invalid_instruction", SourceLocation);
				return false;
			}
			// Uninstrumented governed dispatch already recorded this instruction
			// before charging and polling. A debugger can change the cursor.
			if constexpr (!bGoverned || bInstrumented)
			{
				StateView.GetActiveInstructionStart() = Frame.InstructionPointer;
				if (ActiveFunction && !ActiveFunction->ExceptionRegions.empty())
				{
					Frame.LastInstructionOffset = StateView.GetActiveInstructionStart();
				}
			}
			Frame.InstructionPointer += InstructionByteSize;
			switch (Opcode)
			{
			case EOpcode::Nop:
				{
					break;
				}
			case EOpcode::ConstI32:
				{
					const std::uint16_t Index = Bytecode::ReadU16LE(Instruction + 1);
					if (Index >= StateView.GetActiveModule()->Constants.size())
					{
						ReportError("invalid constant index", SourceLocation);
						return false;
					}
					const FBytecodeConstant& Constant = StateView.GetActiveModule()->Constants[Index];
					if (Constant.Kind == EConstantKind::I32)
					{
						Push(Constant.I32);
					}
					else
					{
						Push(Host.Runtime_ConstantToValue(*StateView.GetActiveModule(), Constant));
					}
					break;
				}
			case EOpcode::ConstF64:
				{
					const std::uint16_t Index = Bytecode::ReadU16LE(Instruction + 1);
					if (Index >= StateView.GetActiveModule()->Constants.size())
					{
						ReportError("invalid constant index", SourceLocation);
						return false;
					}
					Push(Host.Runtime_ConstantToValue(*StateView.GetActiveModule(), StateView.GetActiveModule()->Constants[Index]));
					break;
				}
			case EOpcode::ConstBool:
				{
					Push(Instruction[1] != 0);
					break;
				}
			case EOpcode::ConstStr:
				{
					const std::uint16_t Index = Bytecode::ReadU16LE(Instruction + 1);
					if (Index >= StateView.GetActiveModule()->Constants.size())
					{
						ReportError("invalid constant index", SourceLocation);
						return false;
					}
					Push(Host.Runtime_ConstantToValue(*StateView.GetActiveModule(), StateView.GetActiveModule()->Constants[Index]));
					break;
				}
			case EOpcode::LoadLocal:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Instruction + 1);
					if (Slot >= Frame.Locals.size())
					{
						Services.ReportErrorId("diag.invalid_local_slot", SourceLocation);
						return false;
					}
					FValue& Local = Frame.Locals[Slot];
					// `throw local` is the dominant typed-exception shape. In the
					// non-debug VM, transfer a primitive directly from its local slot
					// instead of pushing it only to pop it in the next dispatch.
					// Governed execution must check/count the Throw separately.
					if constexpr (!bInstrumented && !bGoverned)
					{
						if (ActiveFunction && !ActiveFunction->ExceptionRegions.empty() && Frame.InstructionPointer < ActiveCode.size() && static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::Throw && (Runtime::IsNumericValue(Local) || std::holds_alternative<bool>(Local)))
						{
							const std::size_t ThrowOffset = Frame.InstructionPointer;
							Frame.LastInstructionOffset = ThrowOffset;
							Frame.InstructionPointer += Bytecode::InstructionSize(EOpcode::Throw);
							StateView.GetActiveInstructionStart() = ThrowOffset;
							FValue Exception = Local;
							bool bHandledInCurrentFrame = false;
							if (!Services.TryHandleCurrentFrameScriptException(
							        Exception, SourceLocation, bHandledInCurrentFrame))
							{
								return false;
							}
							if (!bHandledInCurrentFrame)
							{
								return Services.RaiseScriptException(
								    std::move(Exception), SourceLocation);
							}
							break;
						}
					}
					// Primitive locals are direct storage and have no script-visible
					// release hook. Avoid walking the reference alternatives in VM code
					// that cannot enter the native scalar backend.
					if (const auto* const I32 = std::get_if<std::int32_t>(&Local))
					{
						// Compare-and-branch is common in variable-bound loops. Avoid
						// creating two operand variants and a bool just to consume them.
						// Debug stops and execution budgets still visit every opcode.
						if constexpr (!bInstrumented && !bGoverned)
						{
							const std::size_t Next = Frame.InstructionPointer;
							const std::size_t LoadSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
							const std::size_t CompareSize = Bytecode::InstructionSize(EOpcode::Lt);
							const std::size_t BranchSize = Bytecode::InstructionSize(EOpcode::JmpIfFalse);
							const std::size_t TailSize = LoadSize + CompareSize + BranchSize;
							if (ActiveCode.size() - Next >= TailSize && static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::LoadLocal && static_cast<EOpcode>(ActiveCode[Next + LoadSize]) == EOpcode::Lt && static_cast<EOpcode>(ActiveCode[Next + LoadSize + CompareSize]) == EOpcode::JmpIfFalse)
							{
								const std::uint16_t OtherSlot = Bytecode::ReadU16LE(ActiveCode.data() + Next + 1);
								if (OtherSlot < Frame.Locals.size())
								{
									if (const auto* const OtherI32 =
									        std::get_if<std::int32_t>(&Frame.Locals[OtherSlot]))
									{
										const std::size_t BranchIp = Next + LoadSize + CompareSize;
										const std::int32_t Offset = Bytecode::ReadI32LE(
										    ActiveCode.data() + BranchIp + 1);
										Frame.InstructionPointer = Next + TailSize;
										StateView.GetActiveInstructionStart() = BranchIp;
										if (ActiveFunction && !ActiveFunction->ExceptionRegions.empty())
										{
											Frame.LastInstructionOffset = BranchIp;
										}
										if (*I32 >= *OtherI32 && !TryJump(Frame, ActiveCode.size(), Offset, SourceLocation))
										{
											return false;
										}
										break;
									}
								}
							}
						}
						if constexpr (!bInstrumented && !bGoverned)
						{
							const auto Next = Frame.InstructionPointer;
#if VEROSCRIPT_VM_ENHANCED_DISPATCH
							if (StateView.GetOperandStack().size() == Frame.StackBase && ActiveFunction && Next < ActiveCode.size() &&
							    (static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::LoadLocal || static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::ConstI32))
							{
								VmIndexedExpressions::FConditionalBranch Branch;
								const auto Result = LocalExpressions.TryExecute(*I32, *StateView.GetActiveModule(), *ActiveFunction,
								    Frame.InstructionPointer, Frame.Locals,
								    StateView.GetOperandStack().size() <= MaxOperandStackDepth ? MaxOperandStackDepth - StateView.GetOperandStack().size() : 0u, Branch);
								if (Result == VmIndexedExpressions::EFusionResult::Store)
								{
									CompleteFusion(Frame, EOpcode::StoreLocal);
									break;
								}
								if (Result == VmIndexedExpressions::EFusionResult::Branch)
								{
									Frame.InstructionPointer = Branch.After;
									CompleteFusion(Frame, EOpcode::JmpIfFalse);
									if (!Branch.Condition && !TryJump(Frame, ActiveCode.size(), Branch.Offset, SourceLocation)) return false;
									break;
								}
							}
#else
							if (StateView.GetOperandStack().size() == Frame.StackBase && ActiveFunction && ActiveFunction->LocalCount > ActiveFunction->ParamCount + ActiveFunction->Captures.size() && Next < ActiveCode.size() && (static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::LoadLocal || static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::ConstI32) && LocalExpressions.TryExecute(*I32, *StateView.GetActiveModule(), *ActiveFunction, Frame.InstructionPointer, Frame.Locals, StateView.GetOperandStack().size() <= MaxOperandStackDepth ? MaxOperandStackDepth - StateView.GetOperandStack().size() : 0u))
							{
								CompleteFusion(Frame, EOpcode::StoreLocal);
								break;
							}
#endif
						}
#if !VEROSCRIPT_VM_ENHANCED_DISPATCH
						if constexpr (bBatchArithmetic)
						{
							const auto Next = Frame.InstructionPointer;
							if (ActiveFunction && StateView.GetOperandStack().size() == Frame.StackBase &&
							    ActiveFunction->LocalCount > ActiveFunction->ParamCount + ActiveFunction->Captures.size() &&
							    Next < ActiveCode.size() &&
							    (static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::LoadLocal ||
							        static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::ConstI32) &&
							    TryFuseMeteredLocalArithmetic(LocalExpressions, StateView, Frame, *I32, MaxOperandStackDepth))
							{
								CompleteFusion(Frame, EOpcode::StoreLocal);
								Frame.LastInstructionOffset = StateView.GetActiveInstructionStart();
								break;
							}
						}
#endif
						Push(*I32);
						break;
					}
					if constexpr (!bInstrumented && !bGoverned)
					{
						if (std::holds_alternative<std::int64_t>(Local) || std::holds_alternative<double>(Local))
						{
							const auto Next = Frame.InstructionPointer;
							const auto StackSize = StateView.GetOperandStack().size();
							if (StackSize == Frame.StackBase && StackSize < MaxOperandStackDepth
							    && ActiveFunction && Next < ActiveCode.size()
							    && (static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::LoadLocal
							        || static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::ConstI32
							        || static_cast<EOpcode>(ActiveCode[Next]) == EOpcode::ConstF64)
							    && LocalExpressions.TryExecuteWide(Local, *StateView.GetActiveModule(), *ActiveFunction,
							        Frame.InstructionPointer, Frame.Locals, MaxOperandStackDepth - StackSize))
							{
								CompleteFusion(Frame, EOpcode::StoreLocal);
								break;
							}
						}
					}
					if (Runtime::IsNumericValue(Local) || std::holds_alternative<bool>(Local))
					{
						if (const auto* const I64 = std::get_if<std::int64_t>(&Local))
						{
							Push(*I64);
						}
						else if (const auto* const F64 = std::get_if<double>(&Local))
						{
							Push(*F64);
						}
						else
						{
							Push(Local);
						}
						break;
					}
					if (std::holds_alternative<std::string>(Local) || std::holds_alternative<std::u16string>(Local))
					{
						// Keep string recognition after the existing primitive fast paths.
						// Numeric loads must not pay for text lookahead on every iteration.
						if constexpr (!bInstrumented && !bGoverned)
						{
							std::int64_t Length = 0;
							const std::size_t StackRoom = StateView.GetOperandStack().size() <= MaxOperandStackDepth
							    ? MaxOperandStackDepth - StateView.GetOperandStack().size()
							    : 0u;
#if VEROSCRIPT_VM_ENHANCED_DISPATCH
							if (Frame.InstructionPointer < ActiveCode.size() &&
							    (static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::LoadLocal ||
								        static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::ConstI32))
							{
								const auto Read = TryFuseTextRead<true>(Local, Frame, ActiveCode, !bGlobalInitializing);
								if (Read != ETextReadFusion::None)
								{
									if (Read == ETextReadFusion::Failed) return false;
									break;
								}
							}
							if (Frame.InstructionPointer < ActiveCode.size() &&
								static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::Call &&
								!bGlobalInitializing && TryFuseTextIdentity(Local, Frame, ActiveCode, Slot))
							{
								break;
							}
							const auto Fusion = VmStringLocals::TryFuseTextLoad(Local, *StateView.GetActiveModule(),
#else
							const auto Fusion = VmStringLocals::TryFuseLoad(Local, *StateView.GetActiveModule(),
#endif
							    ActiveCode, Frame.InstructionPointer, Slot, Frame.Locals, StackRoom, Length);
							if (Fusion != VmStringLocals::EFusionResult::None)
							{
								CompleteFusion(Frame, Fusion == VmStringLocals::EFusionResult::Length
								        ? EOpcode::CallBuiltin
								        : EOpcode::StoreLocal);
								if (Fusion == VmStringLocals::EFusionResult::Length)
								{
									Push(Length);
								}
								break;
							}
#if VEROSCRIPT_VM_ENHANCED_DISPATCH
							if (ActiveFunction && ActiveFunction->LocalCount <= 2u &&
							    VmStringLocals::CanTransferOptimizedTextArgument(Local, *ActiveFunction, Frame.InstructionPointer, StackRoom))
#else
							if (ActiveFunction && ActiveFunction->LocalCount == 2u &&
							    VmStringLocals::CanTransferTextArgument(Local, *ActiveFunction, Frame.InstructionPointer, StackRoom))
#endif
							{
								Push(std::move(Local));
								break;
							}
						}
						Push(Local);
						break;
					}
					// Object locals are already direct storage. Avoid probing every
					// reference alternative on dynamic receiver selection paths.
					if (std::holds_alternative<FObjectHandle>(Local))
					{
						Push(Local);
						break;
					}
					if constexpr (!bInstrumented && !bGoverned)
					{
						if ((std::holds_alternative<FDynamicArrayHandle>(Local) || std::holds_alternative<FMapHandle>(Local)) &&
						    TryFuseLocalScalarIndex(ActiveCode, Frame, Slot))
						{
							break;
						}
					}
					FValue* const Resolved = ResolveRefStorage(Local);
					if (!Resolved)
					{
						ReportError("invalid ref binding", SourceLocation);
						return false;
					}
					if constexpr (!bInstrumented && !bGoverned)
					{
#if VEROSCRIPT_VM_ENHANCED_DISPATCH
						if (std::holds_alternative<std::string>(*Resolved) && Frame.InstructionPointer < ActiveCode.size() &&
						    (static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::LoadLocal ||
						        static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::ConstI32))
						{
							const auto Read = TryFuseTextRead<false>(*Resolved, Frame, ActiveCode, !bGlobalInitializing);
							if (Read != ETextReadFusion::None)
							{
								if (Read == ETextReadFusion::Failed) return false;
								break;
							}
						}
#else
#endif
						std::int64_t Length = 0;
						if (StateView.GetOperandStack().size() < MaxOperandStackDepth && Frame.InstructionPointer < ActiveCode.size() && static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::CallBuiltin && VmStringLocals::TryFuseLength(*Resolved, ActiveCode, Frame.InstructionPointer, Length))
						{
							CompleteFusion(Frame, EOpcode::CallBuiltin);
							Push(Length);
							break;
						}
					}
					Push(*Resolved);
					break;
				}
			case EOpcode::StoreLocal:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Instruction + 1);
					if (Slot >= Frame.Locals.size())
					{
						Services.ReportErrorId("diag.invalid_local_slot", SourceLocation);
						return false;
					}
					FValue& Local = Frame.Locals[Slot];
					const bool bDirectPrimitive = Runtime::IsNumericValue(Local) || std::holds_alternative<bool>(Local) || std::holds_alternative<std::string>(Local) || std::holds_alternative<std::u16string>(Local);
					// A same-kind primitive assignment needs neither a reference walk
					// nor an intermediate variant. Keep the ordinary underflow/release
					// path for every other value and preserve the frame stack boundary.
					auto& Stack = StateView.GetOperandStack();
					if (bDirectPrimitive && Stack.size() > Frame.StackBase && Local.index() == Stack.back().index())
					{
						Local = std::move(Stack.back());
						Stack.pop_back();
						break;
					}
					FValue NewValue = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					// Same-kind primitive replacement cannot run a language destructor.
					// Variant assignment still releases owned string storage normally.
					if (bDirectPrimitive && Local.index() == NewValue.index())
					{
						Local = std::move(NewValue);
						break;
					}
					// Replacing a non-final script-object handle cannot run a language
					// destructor. Perform the shared_ptr assignment directly; a final
					// owner deliberately falls through to AssignValue/ReleaseValue.
					if (auto* const Previous =
					        std::get_if<FObjectHandle>(&Local))
					{
						if (auto* const Next = std::get_if<FObjectHandle>(&NewValue);
						    Next && (!*Previous || (*Previous)->bDestroyed || (*Previous)->bDestroying || Previous->use_count() != 1))
						{
							*Previous = std::move(*Next);
							break;
						}
					}
					FValue* const Resolved = ResolveRefStorage(Frame.Locals[Slot]);
					if (!Resolved)
					{
						ReportError("invalid ref binding", SourceLocation);
						return false;
					}
					const bool bReference = Resolved != &Local;
					// Releasing the previous value can reenter Run() and relocate Frames.
					const std::size_t FrameIndex = StateView.GetFrames().size() - 1u;
					if (!Services.AssignValue(*Resolved, std::move(NewValue), SourceLocation))
					{
						return false;
					}
					if (bReference && !StateView.GetExitRequestedFlag())
					{
						Services.NotifyReferenceMutation(StateView.GetFrames()[FrameIndex].Locals[Slot]);
					}
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					break;
				}
			case EOpcode::BindLocal:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Instruction + 1);
					if (Slot >= Frame.Locals.size())
					{
						Services.ReportErrorId("diag.invalid_local_slot", SourceLocation);
						return false;
					}
					if (!Services.AssignValue(Frame.Locals[Slot], Pop(), SourceLocation))
					{
						return false;
					}
					break;
				}
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Instruction + 1);
					if (Slot >= Frame.Locals.size())
					{
						Services.ReportErrorId("diag.invalid_local_slot", SourceLocation);
						return false;
					}
					FValue& Local = Frame.Locals[Slot];
					const std::int32_t Immediate = (Opcode == EOpcode::IncLocal)
					    ? std::int32_t{1}
					    : Bytecode::ReadI32LE(Instruction + 3);
					// Keep the overwhelmingly common i32 loop-counter path independent
					// from the generic integer classifier. Wider/unsigned integers retain
					// their width-aware semantics in the branch below.
					if (auto* const I32 = std::get_if<std::int32_t>(&Local))
					{
						std::int32_t Result = 0;
						if (!Runtime::CheckedAdd(*I32, Immediate, Result))
						{
							Services.ReportErrorId("diag.integer_overflow", SourceLocation);
							return false;
						}
						*I32 = Result;
						break;
					}
					// Preserve the declared integer width; signed overflow is diagnosed
					// and unsigned arithmetic wraps at that width.
					if (Runtime::IsIntegerValue(Local))
					{
						if (!AddIntegerImmediate(Local, Immediate))
						{
							Services.ReportErrorId("diag.integer_overflow", SourceLocation);
							return false;
						}
						break;
					}
					FValue* const Resolved = ResolveRefStorage(Local);
					if (!Resolved)
					{
						ReportError("invalid ref binding", SourceLocation);
						return false;
					}
					FValue Result;
					if (Runtime::IsIntegerValue(*Resolved))
					{
						Result = *Resolved;
						if (!AddIntegerImmediate(Result, Immediate))
						{
							Services.ReportErrorId("diag.integer_overflow", SourceLocation);
							return false;
						}
					}
					else if (!Services.EvalArithmetic(EOpcode::Add, *Resolved, FValue{Immediate}, Result, SourceLocation))
					{
						return false;
					}
					*Resolved = std::move(Result);
					if (Resolved != &Local)
					{
						Services.NotifyReferenceMutation(Local);
					}
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					break;
				}
			case EOpcode::AddLocalLocal:
				{
					const std::uint16_t DestSlot = Bytecode::ReadU16LE(Instruction + 1);
					const std::uint16_t SrcSlot = Bytecode::ReadU16LE(Instruction + 3);
					if (DestSlot >= Frame.Locals.size() || SrcSlot >= Frame.Locals.size())
					{
						Services.ReportErrorId("diag.invalid_local_slot", SourceLocation);
						return false;
					}
					FValue& DestLocal = Frame.Locals[DestSlot];
					FValue& SrcLocal = Frame.Locals[SrcSlot];
					if (auto* const DestI32 = std::get_if<std::int32_t>(&DestLocal))
					{
						if (const auto* const SrcI32 = std::get_if<std::int32_t>(&SrcLocal))
						{
							std::int32_t Result = 0;
							if (!Runtime::CheckedAdd(*DestI32, *SrcI32, Result))
							{
								Services.ReportErrorId("diag.integer_overflow", SourceLocation);
								return false;
							}
							*DestI32 = Result;
							break;
						}
					}
					if (auto* const DestF64 = std::get_if<double>(&DestLocal))
					{
						if (const auto* const SrcF64 = std::get_if<double>(&SrcLocal))
						{
							*DestF64 += *SrcF64;
							break;
						}
					}
					if (auto* const DestI64 = std::get_if<std::int64_t>(&DestLocal))
					{
						if (const auto* const SrcI64 = std::get_if<std::int64_t>(&SrcLocal))
						{
							std::int64_t Result = 0;
							if (VmDetail::EvalSigned64Arithmetic(EOpcode::Add, *DestI64, *SrcI64, Result)
							    != VmDetail::ESignedArithmeticResult::Ok)
							{
								Services.ReportErrorId("diag.integer_overflow", SourceLocation);
								return false;
							}
							*DestI64 = Result;
							break;
						}
					}
					FValue* const Src = ResolveRefStorage(SrcLocal);
					FValue* const Dest = ResolveRefStorage(DestLocal);
					if (!Src || !Dest)
					{
						ReportError("invalid ref binding", SourceLocation);
						return false;
					}
					if (!Host.Runtime_TryAppendStringValue(*Dest, *Src))
					{
						FValue Result;
						if (!Services.EvalArithmetic(EOpcode::Add, *Dest, *Src, Result, SourceLocation))
						{
							return false;
						}
						*Dest = std::move(Result);
					}
					if (Dest != &DestLocal)
					{
						Services.NotifyReferenceMutation(DestLocal);
					}
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					break;
				}
			case EOpcode::JmpIfLocalGeImm:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Instruction + 1);
					const std::int32_t Immediate = Bytecode::ReadI32LE(Instruction + 3);
					const std::int32_t Offset = Bytecode::ReadI32LE(Instruction + 7);
					if (Slot >= Frame.Locals.size())
					{
						Services.ReportErrorId("diag.invalid_local_slot", SourceLocation);
						return false;
					}
					FValue& Local = Frame.Locals[Slot];
					// Loop counters compiled as i32 never require reference unwrapping.
					// Checking the exact alternative first avoids probing every ref kind
					// on each back-edge while preserving the generic reference path.
					if (const auto* const I32 = std::get_if<std::int32_t>(&Local))
					{
						if (*I32 >= Immediate && !TryJump(Frame, ActiveCode.size(), Offset, SourceLocation))
						{
							return false;
						}
						break;
					}
					FValue* const Resolved = ResolveRefStorage(Local);
					if (!Resolved)
					{
						ReportError("invalid ref binding", SourceLocation);
						return false;
					}
					// Peephole of LoadLocal; ConstI32; Lt; JmpIfFalse → jump iff !(local < imm).
					// For i32 that is local >= imm; for double use inverted Lt (not >=) so NaN
					// still exits the loop (IEEE: NaN < x and NaN >= x are both false).
					bool bShouldJump = false;
					if (Runtime::IsUnsignedIntegerValue(*Resolved))
					{
						// Compiler-generated unsigned bounds are exact, non-negative
						// values carried in the shared 32-bit immediate field.
						bShouldJump = ToUnsignedInteger64(*Resolved) >= static_cast<std::uint64_t>(
						                                                    static_cast<std::uint32_t>(Immediate));
					}
					else if (Runtime::IsNumericValue(*Resolved))
					{
						bShouldJump = !(ToDouble(*Resolved) < static_cast<double>(Immediate));
					}
					else
					{
						ReportError("JmpIfLocalGeImm requires numeric local", SourceLocation);
						return false;
					}
					if (bShouldJump && !TryJump(Frame, ActiveCode.size(), Offset, SourceLocation))
					{
						return false;
					}
					break;
				}
			case EOpcode::IncField:
				{
					const std::uint16_t NameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (!StateView.GetActiveModule() || NameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid field name", SourceLocation);
						return false;
					}
					FValue ObjectValue = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					auto* ObjectHandle = std::get_if<FObjectHandle>(&ObjectValue);
					if (!ObjectHandle || !*ObjectHandle)
					{
						ReportError("IncField requires object", SourceLocation);
						return false;
					}
					const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**ObjectHandle);
					const std::uint16_t FieldIndex = FindFieldIndex(Frame, TypeIndex, NameIndex);
					if (FieldIndex == static_cast<std::uint16_t>(-1))
					{
						Services.ReportErrorId(
						    "diag.unknown_field",
						    SourceLocation,
						    {StateView.GetActiveModule()->StringPool[NameIndex]});
						return false;
					}
					if (FieldIndex >= (*ObjectHandle)->Fields.size())
					{
						ReportError("invalid field index", SourceLocation);
						return false;
					}
					FValue* const ResolvedField =
					    UnwrapClosureReferenceCell(&(*ObjectHandle)->Fields[FieldIndex]);
					if (!ResolvedField)
					{
						ReportError("invalid referenced field storage", SourceLocation);
						return false;
					}
					FValue& FieldSlot = *ResolvedField;
					if (Runtime::IsIntegerValue(FieldSlot))
					{
						if (!AddIntegerImmediate(FieldSlot, 1))
						{
							Services.ReportErrorId("diag.integer_overflow", SourceLocation);
							return false;
						}
						Services.NotifyFieldMutation(**ObjectHandle, FieldIndex);
						break;
					}
					if (auto* F64 = std::get_if<double>(&FieldSlot))
					{
						*F64 += 1.0;
						Services.NotifyFieldMutation(**ObjectHandle, FieldIndex);
						break;
					}
					if (auto* F32 = std::get_if<float>(&FieldSlot))
					{
						*F32 += 1.0f;
						Services.NotifyFieldMutation(**ObjectHandle, FieldIndex);
						break;
					}
					// Defensive: unexpected field kinds should not be fused by the compiler.
					FValue Result;
					if (!Services.EvalArithmetic(EOpcode::Add, FieldSlot, FValue{std::int32_t{1}}, Result, SourceLocation))
					{
						return false;
					}
					// EvalArithmetic only yields scalar/string; SetField still handles assign.
					if (!Services.SetField(**ObjectHandle, FieldIndex, Result, SourceLocation))
					{
						return false;
					}
					break;
				}

			case EOpcode::BeginLocalStatic:
				{
					bool bSkip = false;
					if (!Services.BeginLocalStatic(Bytecode::ReadU16LE(Instruction + 1), bSkip, SourceLocation)) return false;
					if (bSkip && !TryJump(Frame, ActiveCode.size(), Bytecode::ReadI32LE(Instruction + 3), SourceLocation)) return false;
					break;
				}
			case EOpcode::CommitLocalStatic:
				{
					FValue Value = Pop();
					if (StateView.GetErrorFlag() || !Services.CommitLocalStatic(Bytecode::ReadU16LE(Instruction + 1), std::move(Value), SourceLocation))
					{
						return false;
					}
					break;
				}
			case EOpcode::LoadGlobal:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Instruction + 1);
					if ((!ActiveFunction || ActiveFunction->bUsesLocalStaticStorage) && Slot < StateView.GetActiveModule()->GlobalMetadata.size() && StateView.GetActiveModule()->GlobalMetadata[Slot].bLocalStatic && !Services.CheckLocalStaticAccess(Slot, SourceLocation))
					{
						return false;
					}
					if (Slot >= StateView.GetGlobals().size())
					{
						Services.ReportErrorId("diag.invalid_global_slot", SourceLocation);
						return false;
					}
					FValue* const Resolved = ResolveRefStorage(StateView.GetGlobals()[Slot]);
					if (!Resolved)
					{
						ReportError("invalid ref binding", SourceLocation);
						return false;
					}
					if constexpr (!bInstrumented && !bGoverned)
					{
#if VEROSCRIPT_VM_ENHANCED_DISPATCH
						if (std::holds_alternative<std::string>(*Resolved) && Frame.InstructionPointer < ActiveCode.size() &&
						    (static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::LoadLocal ||
						        static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::ConstI32))
						{
							const auto Read = TryFuseTextRead<false>(*Resolved, Frame, ActiveCode, !bGlobalInitializing);
							if (Read != ETextReadFusion::None)
							{
								if (Read == ETextReadFusion::Failed) return false;
								break;
							}
						}
#else
#endif
						std::int64_t Length = 0;
						if (StateView.GetOperandStack().size() < MaxOperandStackDepth && Frame.InstructionPointer < ActiveCode.size() && static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::CallBuiltin && VmStringLocals::TryFuseLength(*Resolved, ActiveCode, Frame.InstructionPointer, Length))
						{
							CompleteFusion(Frame, EOpcode::CallBuiltin);
							Push(Length);
							break;
						}
					}
					Push(*Resolved);
					break;
				}
			case EOpcode::StoreGlobal:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Instruction + 1);
					if ((!ActiveFunction || ActiveFunction->bUsesLocalStaticStorage) && Slot < StateView.GetActiveModule()->GlobalMetadata.size() && StateView.GetActiveModule()->GlobalMetadata[Slot].bLocalStatic && !Services.CheckLocalStaticAccess(Slot, SourceLocation))
					{
						return false;
					}
					if (Slot >= StateView.GetGlobals().size())
					{
						Services.ReportErrorId("diag.invalid_global_slot", SourceLocation);
						return false;
					}
					if (StateView.GetHotReloadInitializingFlag() && Slot < StateView.GetHotReloadRetainedGlobalSlots().size() && StateView.GetHotReloadRetainedGlobalSlots()[Slot])
					{
						FValue IgnoredInitializer = Pop();
						if (!Services.ReleaseValue(IgnoredInitializer, SourceLocation))
						{
							return false;
						}
						break;
					}
					FValue* const Resolved = ResolveRefStorage(StateView.GetGlobals()[Slot]);
					if (!Resolved)
					{
						ReportError("invalid ref binding", SourceLocation);
						return false;
					}
					if (!Services.AssignValue(*Resolved, Pop(), SourceLocation))
					{
						return false;
					}
					break;
				}
			case EOpcode::LoadField:
				{
					const std::uint16_t NameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (NameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid field name", SourceLocation);
						return false;
					}
					const std::string& FieldName = StateView.GetActiveModule()->StringPool[NameIndex];
					FValue ObjectValue = Pop();
					auto* ObjectHandle = std::get_if<FObjectHandle>(&ObjectValue);
					if (!ObjectHandle || !*ObjectHandle)
					{
						ReportError("member access requires object", SourceLocation);
						return false;
					}
					const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**ObjectHandle);
					const std::uint16_t FieldIndex = FindFieldIndex(Frame, TypeIndex, NameIndex);
					if (FieldIndex == static_cast<std::uint16_t>(-1))
					{
						Services.ReportErrorId("diag.unknown_field", SourceLocation, {FieldName});
						return false;
					}
					if constexpr (!bInstrumented && !bGoverned)
					{
						// Read only while the receiver is owned locally. No callback or
						// borrowed value crosses the load/len pair; other consumers use GetField.
						if (FieldIndex < (*ObjectHandle)->Fields.size() && Frame.InstructionPointer < ActiveCode.size() && static_cast<EOpcode>(ActiveCode[Frame.InstructionPointer]) == EOpcode::CallBuiltin)
						{
							const auto* Storage = ResolveRefStorage((*ObjectHandle)->Fields[FieldIndex]);
							std::int64_t Length = 0;
							if (Storage && VmStringLocals::TryFuseLength(*Storage, ActiveCode, Frame.InstructionPointer, Length))
							{
								CompleteFusion(Frame, EOpcode::CallBuiltin);
								Push(Length);
								break;
							}
						}
					}
					FValue FieldValue;
					if (!Services.GetField(**ObjectHandle, FieldIndex, FieldValue, SourceLocation))
					{
						return false;
					}
					Push(FieldValue);
					break;
				}
			case EOpcode::LoadFieldKeep:
				{
					const std::uint16_t NameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (NameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid field name", SourceLocation);
						return false;
					}
					const std::string& FieldName = StateView.GetActiveModule()->StringPool[NameIndex];
					FValue ObjectValue = Pop();
					auto* ObjectHandle = std::get_if<FObjectHandle>(&ObjectValue);
					if (!ObjectHandle || !*ObjectHandle)
					{
						ReportError("member access requires object", SourceLocation);
						return false;
					}
					const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**ObjectHandle);
					const std::uint16_t FieldIndex = FindFieldIndex(Frame, TypeIndex, NameIndex);
					if (FieldIndex == static_cast<std::uint16_t>(-1))
					{
						Services.ReportErrorId("diag.unknown_field", SourceLocation, {FieldName});
						return false;
					}
					FValue FieldValue;
					if (!Services.GetField(**ObjectHandle, FieldIndex, FieldValue, SourceLocation))
					{
						return false;
					}
					Push(std::move(ObjectValue));
					Push(std::move(FieldValue));
					break;
				}
			case EOpcode::StoreField:
				{
					const std::uint16_t NameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (NameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid field name", SourceLocation);
						return false;
					}
					const std::string& FieldName = StateView.GetActiveModule()->StringPool[NameIndex];
					FValue Value = Pop();
					FValue ObjectValue = Pop();
					auto* ObjectHandle = std::get_if<FObjectHandle>(&ObjectValue);
					if (!ObjectHandle || !*ObjectHandle)
					{
						ReportError("member assignment requires object", SourceLocation);
						return false;
					}
					const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**ObjectHandle);
					const std::uint16_t FieldIndex = FindFieldIndex(Frame, TypeIndex, NameIndex);
					if (FieldIndex == static_cast<std::uint16_t>(-1))
					{
						Services.ReportErrorId("diag.unknown_field", SourceLocation, {FieldName});
						return false;
					}
					if (!Services.SetField(**ObjectHandle, FieldIndex, Value, SourceLocation))
					{
						return false;
					}
					Push(Value);
					break;
				}
			case EOpcode::LoadThisField:
				{
					// Operand is a StringPool field name resolved against the runtime layout.
					const std::uint16_t FieldNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (!Frame.ThisObject)
					{
						ReportError("this field access outside method", SourceLocation);
						return false;
					}
					if (!StateView.GetActiveModule() || FieldNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid this field name", SourceLocation);
						return false;
					}
					const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*Frame.ThisObject);
					const std::uint16_t FieldIndex = FindFieldIndex(Frame, TypeIndex, FieldNameIndex);
					if (FieldIndex == static_cast<std::uint16_t>(-1))
					{
						Services.ReportError(
						    "unknown field: " + StateView.GetActiveModule()->StringPool[FieldNameIndex],
						    SourceLocation);
						return false;
					}
					if (FieldIndex >= Frame.ThisObject->Fields.size())
					{
						ReportError("invalid field index", SourceLocation);
						return false;
					}
					FValue& FieldSlot = Frame.ThisObject->Fields[FieldIndex];
					if (std::holds_alternative<std::int32_t>(FieldSlot))
					{
						Push(std::get<std::int32_t>(FieldSlot));
						break;
					}
					FValue FieldValue;
					if (!Services.GetField(*Frame.ThisObject, FieldIndex, FieldValue, SourceLocation))
					{
						return false;
					}
					Push(FieldValue);
					break;
				}
			case EOpcode::InitializeThisField:
			case EOpcode::StoreThisField:
				{
					const bool bNotifyMutation = Opcode == EOpcode::StoreThisField;
					const std::uint16_t FieldNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (!Frame.ThisObject)
					{
						ReportError("this field assignment outside method", SourceLocation);
						return false;
					}
					if (!StateView.GetActiveModule() || FieldNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid this field name", SourceLocation);
						return false;
					}
					const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*Frame.ThisObject);
					const std::uint16_t FieldIndex = FindFieldIndex(Frame, TypeIndex, FieldNameIndex);
					if (FieldIndex == static_cast<std::uint16_t>(-1))
					{
						Services.ReportError(
						    "unknown field: " + StateView.GetActiveModule()->StringPool[FieldNameIndex],
						    SourceLocation);
						return false;
					}
					if (FieldIndex >= Frame.ThisObject->Fields.size())
					{
						ReportError("invalid field index", SourceLocation);
						return false;
					}
					FValue Value = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					FValue& FieldSlot = Frame.ThisObject->Fields[FieldIndex];
					if (std::holds_alternative<std::int32_t>(FieldSlot) && std::holds_alternative<std::int32_t>(Value))
					{
						FieldSlot = std::get<std::int32_t>(Value);
						if (bNotifyMutation)
						{
							Services.NotifyFieldMutation(*Frame.ThisObject, FieldIndex);
						}
						break;
					}
					if (!Services.SetField(*Frame.ThisObject, FieldIndex, Value, SourceLocation, bNotifyMutation))
					{
						return false;
					}
					break;
				}
			case EOpcode::CloneValueOfType:
				{
					const auto Name = Bytecode::ReadU16LE(Instruction + 1);
					if (Name >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid value copy view", SourceLocation);
						return false;
					}
					FValue Value = Pop(), Copy;
					const auto Direct = Bytecode::ReadU16LE(Instruction + 3);
					if (Direct > 1)
					{
						ReportError("invalid value copy initialization mode", SourceLocation);
						return false;
					}
					if (!Services.CopyLanguageValue(Value, Copy, SourceLocation, Direct != 0, StateView.GetActiveModule()->StringPool[Name]))
					{
						(void)Services.ReleaseValue(Value, SourceLocation);
						return false;
					}
					if (!Services.ReleaseValue(Value, SourceLocation))
					{
						(void)Services.ReleaseValue(Copy, SourceLocation);
						return false;
					}
					Push(std::move(Copy));
					break;
				}
			case EOpcode::CloneStruct:
				{
					FValue Value = Pop();
					if (const auto* HostHandle = std::get_if<FHostObjectHandle>(&Value);
					    HostHandle && *HostHandle && StateView.GetActiveNatives())
					{
						const std::string CloneName =
						    MakeHostCloneNativeName((*HostHandle)->TypeName);
						const std::uint16_t CloneIndex =
						    Services.FindActiveNativeIndexByName(CloneName);
						if (CloneIndex != static_cast<std::uint16_t>(-1))
						{
							const FHostValueCallback StampCallback =
							    (*HostHandle)->CloneCallback
							    ? (*HostHandle)->CloneCallback
							    : (*StateView.GetActiveNatives())[CloneIndex].Callback;
							void* const StampUserData =
							    (*HostHandle)->CloneCallback
							    ? (*HostHandle)->CloneUserData
							    : (*StateView.GetActiveNatives())[CloneIndex].UserData;
							const auto StampOwner = (*HostHandle)->CloneCallback
							    ? (*HostHandle)->CloneOwner
							    : (*StateView.GetActiveNatives())[CloneIndex].CallbackOwner;
							Push(std::move(Value));
							if (!Services.CallNative(CloneIndex, 1, SourceLocation))
							{
								return false;
							}
							if (const auto* Cloned =
							        std::get_if<FHostObjectHandle>(&StateView.GetOperandStack().back());
							    Cloned && *Cloned)
							{
								(*Cloned)->CloneCallback = StampCallback;
								(*Cloned)->CloneUserData = StampUserData;
								(*Cloned)->CloneOwner = StampOwner;
							}
							break;
						}
					}
					FValue Cloned;
					if (!Services.CopyLanguageValue(Value, Cloned, SourceLocation))
					{
						(void)Services.ReleaseValue(Value, SourceLocation);
						return false;
					}
					if (!Services.ReleaseValue(Value, SourceLocation))
					{
						(void)Services.ReleaseValue(Cloned, SourceLocation);
						return false;
					}
					Push(std::move(Cloned));
					break;
				}
			case EOpcode::MakeLocalRef:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Instruction + 1);
					if (StateView.GetFrames().empty() || Slot >= StateView.GetFrames().back().Locals.size())
					{
						ReportError("invalid local ref slot", SourceLocation);
						return false;
					}
					Push(VmDetail::MakeLocalReference(StateView.GetFrames().back().Locals[Slot],
					    static_cast<std::uint32_t>(StateView.GetFrames().size() - 1), Slot));
					break;
				}
			case EOpcode::MakeGlobalRef:
				{
					const std::uint16_t Slot = Bytecode::ReadU16LE(Instruction + 1);
					if ((!ActiveFunction || ActiveFunction->bUsesLocalStaticStorage) && Slot < StateView.GetActiveModule()->GlobalMetadata.size() && StateView.GetActiveModule()->GlobalMetadata[Slot].bLocalStatic && !Services.CheckLocalStaticAccess(Slot, SourceLocation))
					{
						return false;
					}
					if (Slot >= StateView.GetGlobals().size())
					{
						Services.ReportErrorId("diag.invalid_global_slot", SourceLocation);
						return false;
					}
					FGlobalRef Ref;
					Ref.Slot = Slot;
					Push(Ref);
					break;
				}
			case EOpcode::MakeThisFieldRef:
				{
					const std::uint16_t FieldNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (StateView.GetFrames().empty() || !StateView.GetFrames().back().ThisObject)
					{
						ReportError("this field ref requires object", SourceLocation);
						return false;
					}
					if (!StateView.GetActiveModule() || FieldNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid this field name", SourceLocation);
						return false;
					}
					const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*StateView.GetFrames().back().ThisObject);
					const std::uint16_t FieldIndex = FindFieldIndex(Frame,
					    TypeIndex, FieldNameIndex);
					if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= StateView.GetFrames().back().ThisObject->Fields.size())
					{
						ReportError("invalid this field ref", SourceLocation);
						return false;
					}
					FThisFieldRef Ref;
					Ref.Object = StateView.GetFrames().back().ThisObject;
					Ref.FieldIndex = FieldIndex;
					Push(Ref);
					break;
				}
			case EOpcode::MakeFieldRef:
				{
					const std::uint16_t FieldNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					FValue ObjectValue = Pop();
					if (!StateView.GetActiveModule() || FieldNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid field name", SourceLocation);
						return false;
					}
					auto* ObjectHandle = std::get_if<FObjectHandle>(&ObjectValue);
					if (!ObjectHandle || !*ObjectHandle)
					{
						ReportError("field ref requires object", SourceLocation);
						return false;
					}
					const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**ObjectHandle);
					const std::uint16_t FieldIndex = FindFieldIndex(Frame,
					    TypeIndex, FieldNameIndex);
					if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= (*ObjectHandle)->Fields.size())
					{
						Services.ReportError(
						    "unknown field: " + StateView.GetActiveModule()->StringPool[FieldNameIndex],
						    SourceLocation);
						return false;
					}
					FThisFieldRef Ref;
					Ref.Object = *ObjectHandle;
					Ref.FieldIndex = FieldIndex;
					Push(Ref);
					break;
				}
			case EOpcode::GetIndexRef:
			case EOpcode::GetIndexPathRef:
				{
					const std::uint16_t ArraySlot = Bytecode::ReadU16LE(Instruction + 1);
					FValue IndexValue = Pop();
					if (StateView.GetFrames().empty() || ArraySlot >= StateView.GetFrames().back().Locals.size())
					{
						Services.ReportErrorId("diag.invalid_index_ref_array_slot", SourceLocation);
						return false;
					}
					if (!Runtime::IsIntegerValue(IndexValue))
					{
						Services.ReportErrorId("diag.index_ref_index_must_be_int", SourceLocation);
						return false;
					}
					std::size_t ResolvedIndex = 0;
					if (!Host.Runtime_TryGetSizeValue(IndexValue, ResolvedIndex))
					{
						if (Opcode == EOpcode::GetIndexRef)
						{
							Services.ReportErrorId("diag.index_ref_index_out_of_bounds", SourceLocation);
							return false;
						}
						ResolvedIndex = std::numeric_limits<std::size_t>::max();
					}
					FIndexRef Ref;
					Ref.FrameIndex = static_cast<std::uint32_t>(StateView.GetFrames().size() - 1);
					Ref.ArraySlot = ArraySlot;
					// A plain assignment resolves its path after evaluating the RHS;
					// an invalid index must not suppress those RHS side effects.
					Ref.Index = static_cast<std::int64_t>(ResolvedIndex);
					FValue Binding = std::move(Ref);
					if (Opcode == EOpcode::GetIndexRef && !Services.BindElementReference(Binding, SourceLocation)) return false;
					Push(std::move(Binding));
					break;
				}
			case EOpcode::GetMapEntryRef:
				{
					const std::uint16_t MapSlot = Bytecode::ReadU16LE(Instruction + 1);
					FValue KeyValue = Pop();
					if (StateView.GetFrames().empty() || MapSlot >= StateView.GetFrames().back().Locals.size())
					{
						Services.ReportErrorId("diag.invalid_index_ref_array_slot", SourceLocation);
						return false;
					}
					FValue* const MapStorage = ResolveRefStorage(StateView.GetFrames().back().Locals[MapSlot]);
					if (!MapStorage)
					{
						ReportError("invalid map entry ref", SourceLocation);
						return false;
					}
					FMapEntryKey EntryKey;
					auto BindMapKey =
					    [&](const std::string& KeyTypeName, const char* MissingMsg) -> bool
					{
						if (VmDetail::IsSigned32KeyStorageType(KeyTypeName))
						{
							std::int32_t Key = 0;
							if (!VmDetail::TryGetSigned32Key(KeyValue, KeyTypeName, Key))
							{
								Services.ReportError("map key must be " + KeyTypeName, SourceLocation);
								return false;
							}
							EntryKey = Key;
							return true;
						}
						if (KeyTypeName == "unsigned int")
						{
							const auto* Key = std::get_if<std::uint32_t>(&KeyValue);
							if (!Key)
							{
								Services.ReportError(
								    "map key must be unsigned int", SourceLocation);
								return false;
							}
							EntryKey = *Key;
							return true;
						}
						if (KeyTypeName == "int64")
						{
							const auto* Key = std::get_if<std::int64_t>(&KeyValue);
							if (!Key)
							{
								ReportError("map key must be int64", SourceLocation);
								return false;
							}
							EntryKey = *Key;
							return true;
						}
						if (KeyTypeName == "uint64")
						{
							const auto* Key = std::get_if<std::uint64_t>(&KeyValue);
							if (!Key)
							{
								ReportError("map key must be uint64", SourceLocation);
								return false;
							}
							EntryKey = *Key;
							return true;
						}
						if (KeyTypeName == "string")
						{
							const auto* Key = std::get_if<std::string>(&KeyValue);
							if (!Key)
							{
								ReportError("map key must be string", SourceLocation);
								return false;
							}
							EntryKey = *Key;
							return true;
						}
						if (KeyTypeName == "wstring")
						{
							const auto* Key = std::get_if<std::u16string>(&KeyValue);
							if (!Key)
							{
								ReportError("map key must be wstring", SourceLocation);
								return false;
							}
							EntryKey = *Key;
							return true;
						}
						if (const auto* Key = std::get_if<FEnumValue>(&KeyValue);
						    Key && Key->TypeName == Runtime::EnumValueTypeName(*StateView.GetActiveModule(), KeyTypeName))
						{
							EntryKey = *Key;
							return true;
						}
						if (const auto* Key = std::get_if<FHostObjectHandle>(&KeyValue);
						    Key && *Key && Services.ValueMatchesElementType(KeyValue, KeyTypeName, SourceLocation))
						{
							EntryKey = *Key;
							return true;
						}
						Services.ReportError(MissingMsg, SourceLocation);
						return false;
					};
					if (const auto* MapHandle = std::get_if<FMapHandle>(MapStorage))
					{
						if (!*MapHandle)
						{
							ReportError("index access requires map", SourceLocation);
							return false;
						}
						if (!BindMapKey((*MapHandle)->KeyTypeName, "invalid map key type"))
						{
							return false;
						}
						if (std::holds_alternative<std::int32_t>(EntryKey))
						{
							if ((*MapHandle)->IntEntries.find(std::get<std::int32_t>(EntryKey)) == (*MapHandle)->IntEntries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::uint32_t>(EntryKey))
						{
							if ((*MapHandle)->UIntEntries.find(std::get<std::uint32_t>(EntryKey)) == (*MapHandle)->UIntEntries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::string>(EntryKey))
						{
							if ((*MapHandle)->StrEntries.find(std::get<std::string>(EntryKey)) == (*MapHandle)->StrEntries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::int64_t>(EntryKey))
						{
							if ((*MapHandle)->Int64Entries.find(std::get<std::int64_t>(EntryKey)) == (*MapHandle)->Int64Entries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::uint64_t>(EntryKey))
						{
							if ((*MapHandle)->UInt64Entries.find(std::get<std::uint64_t>(EntryKey)) == (*MapHandle)->UInt64Entries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::u16string>(EntryKey) && (*MapHandle)->WStrEntries.find(std::get<std::u16string>(EntryKey)) == (*MapHandle)->WStrEntries.end())
						{
							ReportError("map key not found", SourceLocation);
							return false;
						}
						else if (std::holds_alternative<FEnumValue>(EntryKey) && (*MapHandle)->EnumEntries.find(std::get<FEnumValue>(EntryKey)) == (*MapHandle)->EnumEntries.end())
						{
							ReportError("map key not found", SourceLocation);
							return false;
						}
						else if (std::holds_alternative<FHostObjectHandle>(EntryKey) && !Services.FindHostMapValue(**MapHandle, std::get<FHostObjectHandle>(EntryKey)))
						{
							ReportError("map key not found", SourceLocation);
							return false;
						}
					}
					else if (const auto* OrderedMapHandle = std::get_if<FOrderedMapHandle>(MapStorage))
					{
						if (!*OrderedMapHandle)
						{
							ReportError("index access requires ordered_map", SourceLocation);
							return false;
						}
						if (!BindMapKey(
						        (*OrderedMapHandle)->KeyTypeName,
						        "invalid ordered_map key type"))
						{
							return false;
						}
						if (std::holds_alternative<std::int32_t>(EntryKey))
						{
							if ((*OrderedMapHandle)->IntEntries.find(std::get<std::int32_t>(EntryKey)) == (*OrderedMapHandle)->IntEntries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::uint32_t>(EntryKey))
						{
							if ((*OrderedMapHandle)->UIntEntries.find(std::get<std::uint32_t>(EntryKey)) == (*OrderedMapHandle)->UIntEntries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::int64_t>(EntryKey))
						{
							if ((*OrderedMapHandle)->Int64Entries.find(std::get<std::int64_t>(EntryKey)) == (*OrderedMapHandle)->Int64Entries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::uint64_t>(EntryKey))
						{
							if ((*OrderedMapHandle)->UInt64Entries.find(std::get<std::uint64_t>(EntryKey)) == (*OrderedMapHandle)->UInt64Entries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::string>(EntryKey))
						{
							if ((*OrderedMapHandle)->StrEntries.find(std::get<std::string>(EntryKey)) == (*OrderedMapHandle)->StrEntries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
						}
						else if (std::holds_alternative<std::u16string>(EntryKey) && (*OrderedMapHandle)->WStrEntries.find(std::get<std::u16string>(EntryKey)) == (*OrderedMapHandle)->WStrEntries.end())
						{
							ReportError("ordered_map key not found", SourceLocation);
							return false;
						}
						else if (std::holds_alternative<FEnumValue>(EntryKey) && (*OrderedMapHandle)->EnumEntries.find(std::get<FEnumValue>(EntryKey)) == (*OrderedMapHandle)->EnumEntries.end())
						{
							ReportError("ordered_map key not found", SourceLocation);
							return false;
						}
						else if (std::holds_alternative<FHostObjectHandle>(EntryKey) && !Services.FindHostMapValue(**OrderedMapHandle, std::get<FHostObjectHandle>(EntryKey)))
						{
							ReportError("ordered_map key not found", SourceLocation);
							return false;
						}
					}
					else
					{
						ReportError("map entry ref requires map or ordered_map", SourceLocation);
						return false;
					}
					FMapEntryRef Ref;
					Ref.FrameIndex = static_cast<std::uint32_t>(StateView.GetFrames().size() - 1);
					Ref.MapSlot = MapSlot;
					Ref.SetKey(std::move(EntryKey));
					FValue Binding = std::move(Ref);
					if (!Services.BindElementReference(Binding, SourceLocation)) return false;
					Push(std::move(Binding));
					break;
				}
			case EOpcode::Add:
			case EOpcode::Sub:
			case EOpcode::Mul:
			case EOpcode::Div:
			case EOpcode::Mod:
				{
					if (!StateView.GetErrorFlag() && VmDetail::TryEvalScalarStackArithmetic(Opcode, StateView.GetOperandStack(), Frame.StackBase, MaxOperandStackDepth))
					{
						break;
					}
					FValue Right = Pop();
					FValue Left = Pop();
					FValue Result;
					if (!Services.EvalArithmetic(Opcode, Left, Right, Result, SourceLocation))
					{
						return false;
					}
					Push(std::move(Result));
					break;
				}
			case EOpcode::Eq:
			case EOpcode::Ne:
				{
					if (!StateView.GetErrorFlag() && VmDetail::TryEvalScalarStackEquality(Opcode, StateView.GetOperandStack(), Frame.StackBase, MaxOperandStackDepth))
					{
						break;
					}
					FValue Right = Pop();
					FValue Left = Pop();
					if (std::holds_alternative<FScriptVariantHandle>(Left) || std::holds_alternative<FScriptVariantHandle>(Right))
					{
						if (!Services.ValidateVariantValue(Left) || !Services.ValidateVariantValue(Right))
						{
							(void)Services.ReleaseValue(Right, SourceLocation);
							(void)Services.ReleaseValue(Left, SourceLocation);
							return false;
						}
					}
					if ((Runtime::IsSignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right)) || (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right)))
					{
						ReportError("cannot mix signed and unsigned", SourceLocation);
						return false;
					}
					const bool bLeftString = Runtime::IsStringValue(Left);
					const bool bRightString = Runtime::IsStringValue(Right);
					const bool bLeftWString = Runtime::IsWStringValue(Left);
					const bool bRightWString = Runtime::IsWStringValue(Right);
					if (bLeftString || bRightString || bLeftWString || bRightWString)
					{
						if ((bLeftWString || bRightWString) && (bLeftString || bRightString))
						{
							ReportError("cannot mix string and wstring comparison", SourceLocation);
							return false;
						}
						if ((bLeftString && !bRightString) || (bRightString && !bLeftString) || (bLeftWString && !bRightWString) || (bRightWString && !bLeftWString))
						{
							ReportError("cannot compare mismatched string types", SourceLocation);
							return false;
						}
					}
					bool bEqual = false;
					if (Runtime::IsNumericValue(Left) && Runtime::IsNumericValue(Right))
					{
						if (Runtime::IsSignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right))
						{
							const std::int64_t L = ToSignedInteger64(Left);
							const std::int64_t R = ToSignedInteger64(Right);
							bEqual = L == R;
						}
						else if (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right))
						{
							const std::uint64_t L = ToUnsignedInteger64(Left);
							const std::uint64_t R = ToUnsignedInteger64(Right);
							bEqual = L == R;
						}
						else
						{
							bEqual = ToDouble(Left) == ToDouble(Right);
						}
					}
					else
					{
						bEqual = Host.Runtime_ValuesEqual(*StateView.GetActiveModule(), Left, Right);
					}
					if (!Services.ReleaseValue(Right, SourceLocation) || !Services.ReleaseValue(Left, SourceLocation)) return false;
					Push(Opcode == EOpcode::Eq ? bEqual : !bEqual);
					break;
				}
			case EOpcode::Lt:
			case EOpcode::Le:
			case EOpcode::Gt:
			case EOpcode::Ge:
				{
					// Compare scalar counters in place: moving two whole variants out
					// and constructing a third one dominates variable-bound VM loops.
					const std::size_t StackSize = StateView.GetOperandStack().size();
					if (StackSize >= 2 && StackSize - 2 >= Frame.StackBase && std::holds_alternative<std::int32_t>(StateView.GetOperandStack()[StackSize - 2]) && std::holds_alternative<std::int32_t>(StateView.GetOperandStack()[StackSize - 1]))
					{
						const std::int32_t L = std::get<std::int32_t>(StateView.GetOperandStack()[StackSize - 2]);
						const std::int32_t R = std::get<std::int32_t>(StateView.GetOperandStack()[StackSize - 1]);
						bool Result = false;
						switch (Opcode)
						{
						case EOpcode::Lt:
							{
								Result = L < R;
								break;
							}
						case EOpcode::Le:
							{
								Result = L <= R;
								break;
							}
						case EOpcode::Gt:
							{
								Result = L > R;
								break;
							}
						case EOpcode::Ge:
							{
								Result = L >= R;
								break;
							}
						default:
							{
								break;
							}
						}
						StateView.GetOperandStack()[StackSize - 2] = Result;
						StateView.GetOperandStack().pop_back();
						break;
					}
					FValue Right = Pop();
					FValue Left = Pop();
					if (Runtime::IsWStringValue(Left) && Runtime::IsWStringValue(Right))
					{
						const std::u16string& L = std::get<std::u16string>(Left);
						const std::u16string& R = std::get<std::u16string>(Right);
						bool Result = false;
						switch (Opcode)
						{
						case EOpcode::Lt:
							{
								Result = L < R;
								break;
							}
						case EOpcode::Le:
							{
								Result = L <= R;
								break;
							}
						case EOpcode::Gt:
							{
								Result = L > R;
								break;
							}
						case EOpcode::Ge:
							{
								Result = L >= R;
								break;
							}
						default:
							{
								break;
							}
						}
						Push(Result);
						break;
					}
					if (!Runtime::IsNumericValue(Left) || !Runtime::IsNumericValue(Right))
					{
						ReportError("comparison operands must be numeric", SourceLocation);
						return false;
					}
					if ((Runtime::IsSignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right)) || (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right)))
					{
						ReportError("cannot mix signed and unsigned", SourceLocation);
						return false;
					}
					if (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right))
					{
						const std::uint64_t L = ToUnsignedInteger64(Left);
						const std::uint64_t R = ToUnsignedInteger64(Right);
						bool Result = false;
						switch (Opcode)
						{
						case EOpcode::Lt:
							{
								Result = L < R;
								break;
							}
						case EOpcode::Le:
							{
								Result = L <= R;
								break;
							}
						case EOpcode::Gt:
							{
								Result = L > R;
								break;
							}
						case EOpcode::Ge:
							{
								Result = L >= R;
								break;
							}
						default:
							{
								break;
							}
						}
						Push(Result);
						break;
					}
					if (Runtime::IsSignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right))
					{
						const std::int64_t L = ToSignedInteger64(Left);
						const std::int64_t R = ToSignedInteger64(Right);
						const bool Result = Opcode == EOpcode::Lt ? L < R
						    : Opcode == EOpcode::Le               ? L <= R
						    : Opcode == EOpcode::Gt               ? L > R
						                                          : L >= R;
						Push(Result);
						break;
					}
					const double L = ToDouble(Left);
					const double R = ToDouble(Right);
					bool Result = false;
					switch (Opcode)
					{
					case EOpcode::Lt:
						{
							Result = L < R;
							break;
						}
					case EOpcode::Le:
						{
							Result = L <= R;
							break;
						}
					case EOpcode::Gt:
						{
							Result = L > R;
							break;
						}
					case EOpcode::Ge:
						{
							Result = L >= R;
							break;
						}
					default:
						{
							break;
						}
					}
					Push(Result);
					break;
				}
			case EOpcode::Not:
				{
					FValue Value = Pop();
					if (!Services.ExpectBool(Value, SourceLocation))
					{
						return false;
					}
					Push(!std::get<bool>(Value));
					break;
				}
			case EOpcode::Neg:
				{
					FValue Value = Pop();
					if (Runtime::IsUnsignedIntegerValue(Value))
					{
						if (const auto* U64 = std::get_if<std::uint64_t>(&Value))
						{
							Push(std::uint64_t{0} - *U64);
						}
						else
						{
							Push(static_cast<std::uint32_t>(
							    0u - static_cast<std::uint32_t>(ToUnsignedInteger64(Value))));
						}
					}
					else if (Runtime::IsSignedIntegerValue(Value))
					{
						if (const auto* I64 = std::get_if<std::int64_t>(&Value))
						{
							if (*I64 == std::numeric_limits<std::int64_t>::min())
							{
								Services.ReportErrorId("diag.integer_overflow", SourceLocation);
								return false;
							}
							Push(-*I64);
						}
						else
						{
							const std::int32_t IntValue =
							    static_cast<std::int32_t>(ToSignedInteger64(Value));
							if (IntValue == std::numeric_limits<std::int32_t>::min())
							{
								Services.ReportErrorId("diag.integer_overflow", SourceLocation);
								return false;
							}
							Push(static_cast<std::int32_t>(-IntValue));
						}
					}
					else if (std::holds_alternative<double>(Value))
					{
						Push(-std::get<double>(Value));
					}
					else if (std::holds_alternative<float>(Value))
					{
						Push(-std::get<float>(Value));
					}
					else
					{
						ReportError("invalid operand for unary operator", SourceLocation);
						return false;
					}
					break;
				}
			case EOpcode::BitNot:
				{
					FValue Value = Pop();
					if (Runtime::IsUnsignedIntegerValue(Value))
					{
						if (const auto* U64 = std::get_if<std::uint64_t>(&Value))
						{
							Push(~*U64);
						}
						else
						{
							Push(static_cast<std::uint32_t>(
							    ~static_cast<std::uint32_t>(ToUnsignedInteger64(Value))));
						}
					}
					else if (Runtime::IsSignedIntegerValue(Value))
					{
						if (const auto* I64 = std::get_if<std::int64_t>(&Value))
						{
							Push(~*I64);
						}
						else
						{
							Push(static_cast<std::int32_t>(
							    ~static_cast<std::int32_t>(ToSignedInteger64(Value))));
						}
					}
					else
					{
						ReportError("bitwise not requires integer operand", SourceLocation);
						return false;
					}
					break;
				}
			case EOpcode::BitAnd:
			case EOpcode::BitOr:
			case EOpcode::BitXor:
			case EOpcode::Shl:
			case EOpcode::Shr:
				{
					FValue Right = Pop();
					FValue Left = Pop();
					const bool bIsShift = Opcode == EOpcode::Shl || Opcode == EOpcode::Shr;
					if (!bIsShift && ((Runtime::IsSignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right)) || (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right))))
					{
						ReportError("cannot mix signed and unsigned", SourceLocation);
						return false;
					}

					auto TryGetShiftCount = [&](std::uint32_t& OutCount) -> bool
					{
						if (Runtime::IsUnsignedIntegerValue(Right))
						{
							const std::uint64_t Count = ToUnsignedInteger64(Right);
							if (Count > std::numeric_limits<std::uint32_t>::max())
							{
								ReportError("shift count out of range", SourceLocation);
								return false;
							}
							OutCount = static_cast<std::uint32_t>(Count);
							return true;
						}
						if (Runtime::IsSignedIntegerValue(Right))
						{
							const std::int64_t SignedCount = ToSignedInteger64(Right);
							if (SignedCount < 0 || SignedCount > std::numeric_limits<std::uint32_t>::max())
							{
								ReportError("shift count out of range", SourceLocation);
								return false;
							}
							OutCount = static_cast<std::uint32_t>(SignedCount);
							return true;
						}
						ReportError("bitwise operands must be integers", SourceLocation);
						return false;
					};

					if (bIsShift)
					{
						std::uint32_t Count = 0;
						if (!TryGetShiftCount(Count))
						{
							return false;
						}
						const bool bWideLeft =
						    std::holds_alternative<std::int64_t>(Left) || std::holds_alternative<std::uint64_t>(Left);
						if (Count > (bWideLeft ? 63u : 31u))
						{
							ReportError("shift count out of range", SourceLocation);
							return false;
						}
						if (Runtime::IsUnsignedIntegerValue(Left))
						{
							if (const auto* U64 = std::get_if<std::uint64_t>(&Left))
							{
								Push(Opcode == EOpcode::Shl ? *U64 << Count : *U64 >> Count);
								break;
							}
							const std::uint32_t L = static_cast<std::uint32_t>(
							    ToUnsignedInteger64(Left));
							if (Opcode == EOpcode::Shl)
							{
								Push(static_cast<std::uint32_t>(L << Count));
							}
							else
							{
								Push(static_cast<std::uint32_t>(L >> Count));
							}
							break;
						}
						if (Runtime::IsSignedIntegerValue(Left))
						{
							if (const auto* I64 = std::get_if<std::int64_t>(&Left))
							{
								Push(Opcode == EOpcode::Shl
								        ? static_cast<std::int64_t>(
								              static_cast<std::uint64_t>(*I64) << Count)
								        : static_cast<std::int64_t>(*I64 >> Count));
								break;
							}
							const std::int32_t L = static_cast<std::int32_t>(
							    ToSignedInteger64(Left));
							if (Opcode == EOpcode::Shl)
							{
								Push(static_cast<std::int32_t>(static_cast<std::uint32_t>(L) << Count));
							}
							else
							{
								Push(static_cast<std::int32_t>(L >> Count));
							}
							break;
						}
						ReportError("bitwise operands must be integers", SourceLocation);
						return false;
					}

					if (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right))
					{
						if (std::holds_alternative<std::uint64_t>(Left) || std::holds_alternative<std::uint64_t>(Right))
						{
							const std::uint64_t L = ToUnsignedInteger64(Left);
							const std::uint64_t R = ToUnsignedInteger64(Right);
							Push(Opcode == EOpcode::BitAnd     ? L & R
							        : Opcode == EOpcode::BitOr ? L | R
							                                   : L ^ R);
							break;
						}
						const std::uint32_t L = static_cast<std::uint32_t>(
						    ToUnsignedInteger64(Left));
						const std::uint32_t R = static_cast<std::uint32_t>(
						    ToUnsignedInteger64(Right));
						switch (Opcode)
						{
						case EOpcode::BitAnd:
							{
								Push(static_cast<std::uint32_t>(L & R));
								break;
							}
						case EOpcode::BitOr:
							{
								Push(static_cast<std::uint32_t>(L | R));
								break;
							}
						case EOpcode::BitXor:
							{
								Push(static_cast<std::uint32_t>(L ^ R));
								break;
							}
						default:
							{
								ReportError("invalid bitwise opcode", SourceLocation);
								return false;
							}
						}
						break;
					}
					if (Runtime::IsSignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right))
					{
						if (std::holds_alternative<std::int64_t>(Left) || std::holds_alternative<std::int64_t>(Right))
						{
							const std::int64_t L = ToSignedInteger64(Left);
							const std::int64_t R = ToSignedInteger64(Right);
							Push(Opcode == EOpcode::BitAnd     ? L & R
							        : Opcode == EOpcode::BitOr ? L | R
							                                   : L ^ R);
							break;
						}
						const std::int32_t L = static_cast<std::int32_t>(
						    ToSignedInteger64(Left));
						const std::int32_t R = static_cast<std::int32_t>(
						    ToSignedInteger64(Right));
						switch (Opcode)
						{
						case EOpcode::BitAnd:
							{
								Push(static_cast<std::int32_t>(L & R));
								break;
							}
						case EOpcode::BitOr:
							{
								Push(static_cast<std::int32_t>(L | R));
								break;
							}
						case EOpcode::BitXor:
							{
								Push(static_cast<std::int32_t>(L ^ R));
								break;
							}
						default:
							{
								ReportError("invalid bitwise opcode", SourceLocation);
								return false;
							}
						}
						break;
					}
					ReportError("bitwise operands must be integers", SourceLocation);
					return false;
				}
			case EOpcode::Jmp:
				{
					const std::int32_t Offset = Bytecode::ReadI32LE(Instruction + 1);
					if (!TryJump(Frame, ActiveCode.size(), Offset, SourceLocation))
					{
						return false;
					}
					break;
				}
			case EOpcode::JmpIfFalse:
				{
					const std::int32_t Offset = Bytecode::ReadI32LE(Instruction + 1);
					bool bCondition = false;
					if (StateView.GetOperandStack().size() > Frame.StackBase && std::holds_alternative<bool>(StateView.GetOperandStack().back()))
					{
						// Loop conditions already hold a bool. Consume it in place while
						// preserving the frame boundary and this instruction's budget check.
						bCondition = std::get<bool>(StateView.GetOperandStack().back());
						StateView.GetOperandStack().pop_back();
					}
					else
					{
						FValue Condition = Pop();
						if (StateView.GetErrorFlag() || !Services.ExpectBool(Condition, SourceLocation))
						{
							return false;
						}
						bCondition = std::get<bool>(Condition);
					}
					if (!bCondition && !TryJump(Frame, ActiveCode.size(), Offset, SourceLocation))
					{
						return false;
					}
					break;
				}
			case EOpcode::Call:
				{
					const std::uint16_t FunctionIndex = Bytecode::ReadU16LE(Instruction + 1);
					const std::uint8_t ArgumentCount = Instruction[3];
					VmDetail::FArgumentBuffer StackArgs(ArgumentCount <= 8u ? ArgumentCount : 0u);
					std::vector<FValue> HeapArgs;
					FValue* Arguments = nullptr;
					if (!PopOperandArguments(ArgumentCount, StackArgs.data(), HeapArgs, Arguments))
					{
						return false;
					}
					const bool bCallOk = Services.CallFunctionAt(FunctionIndex, Arguments, ArgumentCount, nullptr);
					if (!ReleasePoppedArguments(Arguments, ArgumentCount, SourceLocation))
					{
						return false;
					}
					if (!bCallOk)
					{
						return false;
					}
					break;
				}
			case EOpcode::CallMethod:
				{
					const std::uint16_t MethodNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					const std::uint8_t ArgumentCount = Instruction[3];
					VmDetail::FArgumentBuffer StackArgs(ArgumentCount <= 8u ? ArgumentCount : 0u);
					std::vector<FValue> HeapArgs;
					FValue* Arguments = nullptr;
					if (!PopOperandArguments(ArgumentCount, StackArgs.data(), HeapArgs, Arguments))
					{
						return false;
					}
					FValue ThisValue = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					const bool bCallOk = Services.ExecuteCallMethod(
					    MethodNameIndex,
					    ThisValue,
					    Arguments,
					    ArgumentCount,
					    static_cast<std::uint32_t>(StateView.GetActiveInstructionStart()),
					    SourceLocation,
					    false);
					if (!Services.ReleaseValue(ThisValue, SourceLocation))
					{
						return false;
					}
					if (!bCallOk)
					{
						return false;
					}
					break;
				}
			case EOpcode::CallBoundMethod:
				{
					const std::uint16_t FunctionIndex = Bytecode::ReadU16LE(Instruction + 1);
					const std::uint8_t ArgumentCount = Instruction[3];
					VmDetail::FArgumentBuffer StackArgs(ArgumentCount <= 8u ? ArgumentCount : 0u);
					std::vector<FValue> HeapArgs;
					FValue* Arguments = nullptr;
					if (!PopOperandArguments(ArgumentCount, StackArgs.data(), HeapArgs, Arguments))
					{
						return false;
					}
					FValue ThisValue = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					const auto* ObjectHandle = std::get_if<FObjectHandle>(&ThisValue);
					if (!ObjectHandle || !*ObjectHandle)
					{
						ReportError("bound method call requires object", SourceLocation);
						return false;
					}
					const bool bCallOk = !StateView.GetInstanceCallInterceptor()
					    ? Services.CallFunctionAt(
					          FunctionIndex,
					          Arguments,
					          ArgumentCount,
					          *ObjectHandle)
					    : Services.InvokeResolvedInstanceMethod(
					          FunctionIndex,
					          *ObjectHandle,
					          Arguments,
					          ArgumentCount,
					          SourceLocation,
					          false);
					if (!ReleasePoppedArguments(Arguments, ArgumentCount, SourceLocation))
					{
						return false;
					}
					if (!bCallOk && (*ObjectHandle)->bConstructionIncomplete && FunctionIndex < StateView.GetActiveModule()->Functions.size())
					{
						const auto& Function = StateView.GetActiveModule()->Functions[FunctionIndex];
						if (Function.bReturnsVoid && Function.bIsMethod && Function.OwnerTypeIndex < StateView.GetActiveModule()->Types.size() && MethodLinkageBareLeaf(Function.Name) == Host.ModuleMeta_TypeSimpleName(StateView.GetActiveModule()->Types[Function.OwnerTypeIndex].Name))
						{
							(void)Services.DestroyObject(*ObjectHandle, SourceLocation);
						}
					}
					if (!Services.ReleaseValue(ThisValue, SourceLocation))
					{
						return false;
					}
					if (!bCallOk)
					{
						return false;
					}
					break;
				}
			case EOpcode::MakeClosure:
				{
					const std::uint16_t FunctionIndex = Bytecode::ReadU16LE(Instruction + 1);
					const std::uint8_t CaptureCount = Instruction[3];
					if (!StateView.GetActiveModule() || FunctionIndex >= StateView.GetActiveModule()->Functions.size())
					{
						ReportError("invalid function index for MakeClosure", SourceLocation);
						return false;
					}
					if (CaptureCount > StateView.GetOperandStack().size())
					{
						ReportError("closure capture operand stack underflow", SourceLocation);
						return false;
					}
					std::vector<FValue> Captures(CaptureCount);
					for (int Index = static_cast<int>(CaptureCount) - 1; Index >= 0; --Index)
					{
						Captures[static_cast<std::size_t>(Index)] = Pop();
						if (StateView.GetErrorFlag())
						{
							return false;
						}
					}
					const FObjectHandle Receiver = StateView.GetFrames().empty()
					    ? FObjectHandle{}
					    : StateView.GetFrames().back().ThisObject;
					FFunctionHandle FunctionValue;
					if (!Services.CreateClosure(FunctionIndex, std::move(Captures), Receiver, FunctionValue, SourceLocation)) return false;
					Push(FFunctionHandle{std::move(FunctionValue)});
					break;
				}
			case EOpcode::CallIndirect:
				{
					const std::uint8_t ArgumentCount = Instruction[1];
					FValue CalleeValue = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					const auto* FunctionHandle = std::get_if<FFunctionHandle>(&CalleeValue);
					if (!FunctionHandle || !*FunctionHandle)
					{
						ReportError("CallIndirect requires a function value", SourceLocation);
						return false;
					}
					VmDetail::FArgumentBuffer StackArgs(ArgumentCount <= 8u ? ArgumentCount : 0u);
					std::vector<FValue> HeapArgs;
					FValue* Arguments = nullptr;
					if (!PopOperandArguments(ArgumentCount, StackArgs.data(), HeapArgs, Arguments))
					{
						return false;
					}
					const FFunctionValue& FunctionValue = **FunctionHandle;
					if (!StateView.GetActiveModule() || FunctionValue.ProgramGeneration != StateView.GetProgramGeneration() || FunctionValue.ProgramOwner != StateView.GetProgramOwner() || FunctionValue.FunctionIndex >= StateView.GetActiveModule()->Functions.size())
					{
						ReportError("CallIndirect target is not part of the active module", SourceLocation);
						return false;
					}
					const FBytecodeFunction& Target =
					    StateView.GetActiveModule()->Functions[FunctionValue.FunctionIndex];
					if (FunctionValue.FunctionName.empty() || FunctionValue.FunctionName != Target.Name || FunctionValue.TypeName.empty() || FunctionValue.TypeName != Target.ClosureTypeName)
					{
						ReportError("CallIndirect target is stale or has an incompatible signature", SourceLocation);
						return false;
					}
					// Captures are installed into the callee frame after CallFunctionAt.
					// Keep the authoritative frame for captures that were not inlined.
					FValue ScalarReturn;
					if (!FunctionValue.Captures.empty() && !FunctionValue.CapturedThis && Services.TryCallScalarExpression(FunctionValue.FunctionIndex, Arguments, ArgumentCount, {}, &FunctionValue.Captures, ScalarReturn))
					{
						if (!ReleasePoppedArguments(Arguments, ArgumentCount, SourceLocation)) return false;
						Push(std::move(ScalarReturn));
						break;
					}
					const bool bAllowJit = FunctionValue.Captures.empty();
					const bool bCallOk = Services.CallFunctionAt(
					    FunctionValue.FunctionIndex,
					    Arguments,
					    ArgumentCount,
					    FunctionValue.CapturedThis,
					    bAllowJit);
					if (!ReleasePoppedArguments(Arguments, ArgumentCount, SourceLocation))
					{
						return false;
					}
					if (!bCallOk)
					{
						return false;
					}
					if (!StateView.GetFrames().empty())
					{
						FCallFrame& CalleeFrame = StateView.GetFrames().back();
						const std::size_t ParamCount =
						    CalleeFrame.Function ? CalleeFrame.Function->ParamCount : 0;
						for (std::size_t CaptureIndex = 0;
						    CaptureIndex < FunctionValue.Captures.size();
						    ++CaptureIndex)
						{
							const std::size_t Slot = ParamCount + CaptureIndex;
							if (Slot >= CalleeFrame.Locals.size())
							{
								ReportError("closure capture slot out of range", SourceLocation);
								return false;
							}
							CalleeFrame.Locals[Slot] = FunctionValue.Captures[CaptureIndex];
						}
					}
					break;
				}
			case EOpcode::CallBuiltin:
				{
					const EBuiltinId Builtin = static_cast<EBuiltinId>(Instruction[1]);
					const std::uint8_t ArgumentCount = Instruction[2];
					if (!Services.CallBuiltin(Builtin, ArgumentCount))
					{
						return false;
					}
					if (StateView.GetExitRequestedFlag())
					{
						return true;
					}
					break;
				}
			case EOpcode::CallMemory:
			case EOpcode::CallNative:
				{
					std::size_t NativeIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (Opcode == EOpcode::CallMemory)
					{
						const auto Count = Host.StandardNativeCount();
						if (NativeIndex >= Count || !StateView.GetActiveNatives() || StateView.GetActiveNatives()->size() < Count)
						{
							ReportError("invalid memory member", SourceLocation);
							return false;
						}
						NativeIndex += StateView.GetActiveNatives()->size() - Count;
					}
					const std::uint8_t ArgumentCount = Instruction[3];
					if (Opcode == EOpcode::CallMemory)
					{
						bool Handled = false;
						if (!Services.TryCallStandardValueLeaf(Bytecode::ReadU16LE(Instruction + 1), ArgumentCount,
						        SourceLocation, Handled))
						{
							return false;
						}
						if (Handled)
						{
							break;
						}
					}
					if (!Services.CallNative(static_cast<std::uint16_t>(NativeIndex), ArgumentCount, SourceLocation))
					{
						return false;
					}
					break;
				}
			case EOpcode::TakeLocal:
				{
					const auto Slot = Bytecode::ReadU16LE(Instruction + 1);
					if (Slot >= Frame.Locals.size())
					{
						ReportError("invalid owned temporary slot", SourceLocation);
						return false;
					}
					FValue Value = std::move(Frame.Locals[Slot]);
					Frame.Locals[Slot] = std::int32_t{0};
					Push(std::move(Value));
					break;
				}
			case EOpcode::DefaultCopyConstruct:
				{
					const auto Target = Frame.ThisObject;
					const auto Owner = Frame.Function->OwnerTypeIndex;
					if (Frame.Function->SpecialMemberKind != ESpecialMemberKind::CopyConstructor || !Frame.Function->bDefaulted || Frame.Locals.empty())
					{
						ReportError("invalid default copy construction instruction", SourceLocation);
						return false;
					}
					const auto* SourceValue = ResolveRefStorage(Frame.Locals[0]);
					const auto* Handle = SourceValue ? std::get_if<FObjectHandle>(SourceValue) : nullptr;
					const FObjectHandle Source = Handle ? *Handle : nullptr;
					if (!Services.CopyConstructFields(Target, Source, Owner, SourceLocation)) return false;
					if (Target && Target->RuntimeTypeIndex == Owner)
					{
						Target->bConstructionIncomplete = false;
						Services.ClearObjectCopyBases(*Target);
					}
					break;
				}
			case EOpcode::SameObject:
				{
					const auto R = Pop(), L = Pop();
					const auto* LH = std::get_if<FObjectHandle>(&L);
					const auto* RH = std::get_if<FObjectHandle>(&R);
					Push(LH && RH && *LH == *RH);
					break;
				}
			case EOpcode::AssignCoreMetadata:
				{
					const FValue R = Pop(), L = Pop();
					const auto* LH = std::get_if<FObjectHandle>(&L);
					const auto* RH = std::get_if<FObjectHandle>(&R);
					if (!LH || !RH || !*LH || !*RH || (*LH)->GetTypeName() != (*RH)->GetTypeName() || (*LH)->ProgramOwner != (*RH)->ProgramOwner)
					{
						ReportError("incompatible core descriptor assignment", SourceLocation);
						return false;
					}
					auto& T = **LH;
					const auto& S = **RH;
					T.DescriptorData = S.DescriptorData;
					++T.MutationVersion;
					Push(std::int32_t{0});
					break;
				}
			case EOpcode::AssignHostBase:
			case EOpcode::EqualHostBase:
				{
					FValue R = Pop(), L = Pop();
					const auto* LV = ResolveRefStorage(L);
					const auto* RV = ResolveRefStorage(R);
					const auto* LH = LV ? std::get_if<FObjectHandle>(LV) : nullptr;
					const auto* RH = RV ? std::get_if<FObjectHandle>(RV) : nullptr;
					const auto Target = LH ? *LH : nullptr, Source = RH ? *RH : nullptr;
					if (!Target || !Source || !Target->GetHostBase() || !Source->GetHostBase() || Target->GetHostBase()->TypeName != Source->GetHostBase()->TypeName)
					{
						ReportError("invalid host base special member operands", SourceLocation);
						return false;
					}
					if (Opcode == EOpcode::EqualHostBase)
					{
						Push(Host.Runtime_ValuesEqual(*StateView.GetActiveModule(), FValue{Target->GetHostBase()}, FValue{Source->GetHostBase()}));
					}
					else
					{
						FValue Copy;
						if (!Services.CopyLanguageValue(FValue{Source->GetHostBase()}, Copy, SourceLocation, true)) return false;
						const auto* HostCopy = std::get_if<FHostObjectHandle>(&Copy);
						if (!HostCopy || !*HostCopy)
						{
							ReportError("invalid host base copy result", SourceLocation);
							return false;
						}
						FValue Previous = std::move(Target->GetHostBase());
						Services.SetObjectHostBase(*Target, *HostCopy);
						Target->GetHostBase()->ScriptOwner = Target;
						++Target->MutationVersion;
						if (!Services.ReleaseValue(Previous, SourceLocation)) return false;
						Push(std::int32_t{0});
					}
					break;
				}
			case EOpcode::NewObject:
			case EOpcode::NewUninitializedObject:
				{
					const std::uint16_t TypeIndex = Bytecode::ReadU16LE(Instruction + 1);
					const FObjectHandle Object = Services.CreateObject(TypeIndex, SourceLocation, Opcode == EOpcode::NewUninitializedObject);
					if (!Object)
					{
						return false;
					}
					Push(Object);
					break;
				}
			case EOpcode::NewNativeArray:
			case EOpcode::NewUninitializedNativeArray:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid native array type", SourceLocation);
						return false;
					}
					Push(Services.CreateNativeArray(StateView.GetActiveModule()->StringPool[TypeNameIndex], SourceLocation,
					    Opcode == EOpcode::NewUninitializedNativeArray));
					break;
				}
			case EOpcode::NewDynamicArray:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid dynamic array type", SourceLocation);
						return false;
					}
					const FDynamicArrayHandle Array =
					    Services.CreateDynamicArray(StateView.GetActiveModule()->StringPool[TypeNameIndex], SourceLocation);
					if (!Array)
					{
						return false;
					}
					Push(Array);
					break;
				}
			case EOpcode::NewMap:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid map type", SourceLocation);
						return false;
					}
					const FMapHandle Map = Services.CreateMap(StateView.GetActiveModule()->StringPool[TypeNameIndex], SourceLocation);
					if (!Map)
					{
						return false;
					}
					Push(Map);
					break;
				}
			case EOpcode::NewSet:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid set type", SourceLocation);
						return false;
					}
					const FSetHandle Set = Services.CreateSet(StateView.GetActiveModule()->StringPool[TypeNameIndex], SourceLocation);
					if (!Set)
					{
						return false;
					}
					Push(Set);
					break;
				}
			case EOpcode::NewQueue:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid queue type", SourceLocation);
						return false;
					}
					const FQueueHandle Queue =
					    Services.CreateQueue(StateView.GetActiveModule()->StringPool[TypeNameIndex], SourceLocation);
					if (!Queue)
					{
						return false;
					}
					Push(Queue);
					break;
				}
			case EOpcode::NewChannel:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid channel type", SourceLocation);
						return false;
					}
					const FChannelHandle Channel =
					    Services.CreateChannel(StateView.GetActiveModule()->StringPool[TypeNameIndex], SourceLocation);
					if (!Channel)
					{
						return false;
					}
					Push(Channel);
					break;
				}
			case EOpcode::NewStack:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid stack type", SourceLocation);
						return false;
					}
					const FStackHandle Stack =
					    Services.CreateStack(StateView.GetActiveModule()->StringPool[TypeNameIndex], SourceLocation);
					if (!Stack)
					{
						return false;
					}
					Push(Stack);
					break;
				}
			case EOpcode::NewOrderedMap:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid ordered_map type", SourceLocation);
						return false;
					}
					const FOrderedMapHandle OrderedMap =
					    Services.CreateOrderedMap(StateView.GetActiveModule()->StringPool[TypeNameIndex], SourceLocation);
					if (!OrderedMap)
					{
						return false;
					}
					Push(OrderedMap);
					break;
				}
			case EOpcode::NewDeque:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid deque type", SourceLocation);
						return false;
					}
					const FDequeHandle Deque =
					    Services.CreateDeque(StateView.GetActiveModule()->StringPool[TypeNameIndex], SourceLocation);
					if (!Deque)
					{
						return false;
					}
					Push(Deque);
					break;
				}
			case EOpcode::GetIndexCopy:
				{
					FValue IndexValue = Pop();
					FValue ContainerValue = Pop();
					std::string_view ElementType;
					if (const auto* Fixed = std::get_if<FNativeArray>(&ContainerValue))
					{
						ElementType = Fixed->ElementTypeName();
					}
					else if (const auto* Dynamic = std::get_if<FDynamicArrayHandle>(&ContainerValue); Dynamic && *Dynamic)
					{
						ElementType = (*Dynamic)->ElementTypeName;
					}
					else if (const auto* Map = std::get_if<FMapHandle>(&ContainerValue); Map && *Map)
					{
						ElementType = (*Map)->ValueTypeName;
					}
					else if (const auto* Ordered = std::get_if<FOrderedMapHandle>(&ContainerValue); Ordered && *Ordered)
					{
						ElementType = (*Ordered)->ValueTypeName;
					}
					else if (const auto* Deque = std::get_if<FDequeHandle>(&ContainerValue); Deque && *Deque)
					{
						ElementType = (*Deque)->ElementTypeName;
					}
					const auto PushVisible = [&](const FValue& Stored) -> bool
					{
						const FValue* const Visible = UnwrapClosureReferenceCell(&Stored);
						if (!Visible)
						{
							ReportError("invalid captured reference cell", SourceLocation);
							return false;
						}
						if (std::holds_alternative<FObjectHandle>(*Visible) || std::holds_alternative<FHostObjectHandle>(*Visible) || std::holds_alternative<FNativeArray>(*Visible))
						{
							FValue Copy;
							if (!Services.CopyLanguageValue(*Visible, Copy, SourceLocation, false, ElementType)) return false;
							Push(std::move(Copy));
						}
						else
						{
							Push(*Visible);
						}
						return !StateView.GetErrorFlag();
					};
					const auto PushArrayElement = [&](const FDynamicArray& Array, std::size_t Index)
					{
						if (Array.HasScalarElements())
						{
							FValue Element;
							if (!Array.TryGetScalar(Index, Element))
							{
								ReportError("array index out of bounds", SourceLocation);
								return false;
							}
							Push(std::move(Element));
							return !StateView.GetErrorFlag();
						}
						if (Index >= Array.Elements.size())
						{
							ReportError("array index out of bounds", SourceLocation);
							return false;
						}
						return PushVisible(Array.Elements[Index]);
					};
					// Fast path: array<T> with int32 index.
					if (const auto* Arr = std::get_if<FDynamicArrayHandle>(&ContainerValue))
					{
						if (*Arr && std::holds_alternative<std::int32_t>(IndexValue))
						{
							const std::int32_t Index = std::get<std::int32_t>(IndexValue);
							if (Index < 0)
							{
								ReportError("array index out of bounds", SourceLocation);
								return false;
							}
							if (!PushArrayElement(**Arr, static_cast<std::size_t>(Index))) return false;
							break;
						}
					}
					if (const auto* MapHandle = std::get_if<FMapHandle>(&ContainerValue))
					{
						if (!*MapHandle)
						{
							ReportError("index access requires map", SourceLocation);
							return false;
						}
						if (VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
						{
							std::int32_t Key = 0;
							if (!VmDetail::TryGetSigned32Key(
							        IndexValue, (*MapHandle)->KeyTypeName, Key))
							{
								Services.ReportError("map key must be " + (*MapHandle)->KeyTypeName, SourceLocation);
								return false;
							}
							const auto It = (*MapHandle)->IntEntries.find(Key);
							if (It == (*MapHandle)->IntEntries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*MapHandle)->KeyTypeName == "unsigned int")
						{
							const auto* Key = std::get_if<std::uint32_t>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be unsigned int", SourceLocation);
								return false;
							}
							const auto It = (*MapHandle)->UIntEntries.find(*Key);
							if (It == (*MapHandle)->UIntEntries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*MapHandle)->KeyTypeName == "int64")
						{
							const auto* Key = std::get_if<std::int64_t>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be int64", SourceLocation);
								return false;
							}
							const auto It = (*MapHandle)->Int64Entries.find(*Key);
							if (It == (*MapHandle)->Int64Entries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*MapHandle)->KeyTypeName == "uint64")
						{
							const auto* Key = std::get_if<std::uint64_t>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be uint64", SourceLocation);
								return false;
							}
							const auto It = (*MapHandle)->UInt64Entries.find(*Key);
							if (It == (*MapHandle)->UInt64Entries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*MapHandle)->KeyTypeName == "string")
						{
							const auto* Key = std::get_if<std::string>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be string", SourceLocation);
								return false;
							}
							const auto It = (*MapHandle)->StrEntries.find(*Key);
							if (It == (*MapHandle)->StrEntries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*MapHandle)->KeyTypeName == "wstring")
						{
							const auto* Key = std::get_if<std::u16string>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be wstring", SourceLocation);
								return false;
							}
							const auto It = (*MapHandle)->WStrEntries.find(*Key);
							if (It == (*MapHandle)->WStrEntries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if (const auto* Key = std::get_if<FEnumValue>(&IndexValue);
						    Key && Key->TypeName == Runtime::EnumValueTypeName(*StateView.GetActiveModule(), (*MapHandle)->KeyTypeName))
						{
							const auto It = (*MapHandle)->EnumEntries.find(*Key);
							if (It == (*MapHandle)->EnumEntries.end())
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if (const auto* Key = std::get_if<FHostObjectHandle>(&IndexValue);
						    Key && *Key && Services.ValueMatchesElementType(IndexValue, (*MapHandle)->KeyTypeName, SourceLocation))
						{
							const auto* It = Services.FindHostMapValue(**MapHandle, *Key);
							if (!It)
							{
								ReportError("map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(*It))
							{
								return false;
							}
							break;
						}
						ReportError("invalid map key type", SourceLocation);
						return false;
					}
					if (const auto* OrderedMapHandle = std::get_if<FOrderedMapHandle>(&ContainerValue))
					{
						if (!*OrderedMapHandle)
						{
							ReportError("index access requires ordered_map", SourceLocation);
							return false;
						}
						if (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
						{
							std::int32_t Key = 0;
							if (!VmDetail::TryGetSigned32Key(
							        IndexValue, (*OrderedMapHandle)->KeyTypeName, Key))
							{
								Services.ReportError(
								    "ordered_map key must be " + (*OrderedMapHandle)->KeyTypeName,
								    SourceLocation);
								return false;
							}
							const auto It = (*OrderedMapHandle)->IntEntries.find(Key);
							if (It == (*OrderedMapHandle)->IntEntries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
						{
							const auto* Key = std::get_if<std::uint32_t>(&IndexValue);
							if (!Key)
							{
								Services.ReportError(
								    "ordered_map key must be unsigned int", SourceLocation);
								return false;
							}
							const auto It = (*OrderedMapHandle)->UIntEntries.find(*Key);
							if (It == (*OrderedMapHandle)->UIntEntries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*OrderedMapHandle)->KeyTypeName == "int64")
						{
							const auto* Key = std::get_if<std::int64_t>(&IndexValue);
							if (!Key)
							{
								ReportError("ordered_map key must be int64", SourceLocation);
								return false;
							}
							const auto It = (*OrderedMapHandle)->Int64Entries.find(*Key);
							if (It == (*OrderedMapHandle)->Int64Entries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*OrderedMapHandle)->KeyTypeName == "uint64")
						{
							const auto* Key = std::get_if<std::uint64_t>(&IndexValue);
							if (!Key)
							{
								ReportError("ordered_map key must be uint64", SourceLocation);
								return false;
							}
							const auto It = (*OrderedMapHandle)->UInt64Entries.find(*Key);
							if (It == (*OrderedMapHandle)->UInt64Entries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*OrderedMapHandle)->KeyTypeName == "string")
						{
							const auto* Key = std::get_if<std::string>(&IndexValue);
							if (!Key)
							{
								ReportError("ordered_map key must be string", SourceLocation);
								return false;
							}
							const auto It = (*OrderedMapHandle)->StrEntries.find(*Key);
							if (It == (*OrderedMapHandle)->StrEntries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if ((*OrderedMapHandle)->KeyTypeName == "wstring")
						{
							const auto* Key = std::get_if<std::u16string>(&IndexValue);
							if (!Key)
							{
								ReportError("ordered_map key must be wstring", SourceLocation);
								return false;
							}
							const auto It = (*OrderedMapHandle)->WStrEntries.find(*Key);
							if (It == (*OrderedMapHandle)->WStrEntries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if (const auto* Key = std::get_if<FEnumValue>(&IndexValue);
						    Key && Key->TypeName == Runtime::EnumValueTypeName(*StateView.GetActiveModule(), (*OrderedMapHandle)->KeyTypeName))
						{
							const auto It = (*OrderedMapHandle)->EnumEntries.find(*Key);
							if (It == (*OrderedMapHandle)->EnumEntries.end())
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(It->second))
							{
								return false;
							}
							break;
						}
						if (const auto* Key = std::get_if<FHostObjectHandle>(&IndexValue);
						    Key && *Key && Services.ValueMatchesElementType(IndexValue, (*OrderedMapHandle)->KeyTypeName, SourceLocation))
						{
							const auto* It = Services.FindHostMapValue(**OrderedMapHandle, *Key);
							if (!It)
							{
								ReportError("ordered_map key not found", SourceLocation);
								return false;
							}
							if (!PushVisible(*It))
							{
								return false;
							}
							break;
						}
						ReportError("invalid ordered_map key type", SourceLocation);
						return false;
					}
					if (!Runtime::IsIntegerValue(IndexValue))
					{
						ReportError("array index must be an integer", SourceLocation);
						return false;
					}
					std::size_t ResolvedIndex = 0;
					if (!Host.Runtime_TryGetSizeValue(IndexValue, ResolvedIndex))
					{
						Services.ReportError(std::holds_alternative<FDequeHandle>(ContainerValue)
						        ? "deque index out of bounds"
						        : "array index out of bounds",
						    SourceLocation);
						return false;
					}
					const std::size_t Index = ResolvedIndex;
					if (const auto* NativeArray = std::get_if<FNativeArray>(&ContainerValue))
					{
						if (static_cast<std::uint64_t>(Index) >= NativeArray->Length() || static_cast<std::size_t>(Index) >= NativeArray->Storage().Elements.size())
						{
							ReportError("array index out of bounds", SourceLocation);
							return false;
						}
						if (!PushVisible(NativeArray->Storage().Elements[static_cast<std::size_t>(Index)]))
						{
							return false;
						}
						break;
					}
					if (const auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&ContainerValue))
					{
						if (!*DynamicArray)
						{
							ReportError("index access requires array", SourceLocation);
							return false;
						}
						if (!PushArrayElement(**DynamicArray, Index)) return false;
						break;
					}
					if (const auto* DequeHandle = std::get_if<FDequeHandle>(&ContainerValue))
					{
						if (!*DequeHandle)
						{
							ReportError("index access requires deque", SourceLocation);
							return false;
						}
						if (static_cast<std::size_t>(Index) >= (*DequeHandle)->Elements.size())
						{
							ReportError("deque index out of bounds", SourceLocation);
							return false;
						}
						if (!PushVisible((*DequeHandle)->Elements[static_cast<std::size_t>(Index)]))
						{
							return false;
						}
						break;
					}
					ReportError("index access requires array", SourceLocation);
					return false;
				}
			case EOpcode::InitIndex:
			case EOpcode::SetIndex:
				{
					FValue Value = Pop();
					FValue IndexValue = Pop();
					FValue ContainerValue = Pop();
					// These direct scalar stores own neither a cleanup callback nor
					// a field mutation origin. Keep the authoritative guards on all
					// other stores, including reentry into the language copy operation.
					if (Opcode == EOpcode::SetIndex)
					{
						if (const auto* Integer = std::get_if<std::int32_t>(&Value);
						    Integer && !StateView.GetErrorFlag() && !StateView.IsCopyingLanguageValue() &&
						    StateView.GetActiveContainerCopyReads().empty() &&
						    TryWritePlainIntIndex(ContainerValue, IndexValue, *Integer))
						{
							Push(std::move(ContainerValue));
							break;
						}
					}
					if (Opcode == EOpcode::InitIndex && !std::holds_alternative<FNativeArray>(ContainerValue))
					{
						ReportError("array initialization requires a native array", SourceLocation);
						return false;
					}
					if (!Services.CheckContainerCopyMutation(ContainerValue, SourceLocation)) return false;
					VmDetail::FContainerCopyReadScope CopyReadScope(StateView.GetActiveContainerCopyReads(),
					    VmDetail::ValueCopyMayInvokeCallbacks(Value) ? Services.ContainerStorageIdentity(ContainerValue) : nullptr);
					// Keep a shared-handle copy because successful branches move
					// ContainerValue back onto the operand stack before leaving this
					// scope.  The origin stamped by GetField then remains available to
					// the common mutation barrier for every container specialization.
					FValue MutationContainer = ContainerValue;
					struct FSetIndexMutationGuard
					{
						~FSetIndexMutationGuard()
						{
							if (Vm && Container && bError && !*bError)
							{
								Vm->NotifyContainerMutation(
								    *Container, EMutationKind::ContainerSet);
							}
						}

						IVmExecutionServices* Vm = nullptr;
						FValue* Container = nullptr;
						const bool* bError = nullptr;

					} MutationGuard{&Services, &MutationContainer, &StateView.GetErrorFlag()};
					const auto AssignEntryValue = [&](FValue& Destination)
					{
						return Services.AssignClonedValue(Destination, Value, SourceLocation);
					};
					auto* MapHandle = std::get_if<FMapHandle>(&ContainerValue);
					if (MapHandle)
					{
						if (!*MapHandle)
						{
							ReportError("index assign requires map", SourceLocation);
							return false;
						}
						if (!Services.ValueMatchesElementType(Value, (*MapHandle)->ValueTypeName, SourceLocation))
						{
							Services.ReportError(
							    "map value type mismatch for " + (*MapHandle)->TypeName,
							    SourceLocation);
							return false;
						}
						if (VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
						{
							std::int32_t Key = 0;
							if (!VmDetail::TryGetSigned32Key(
							        IndexValue, (*MapHandle)->KeyTypeName, Key))
							{
								Services.ReportError("map key must be " + (*MapHandle)->KeyTypeName, SourceLocation);
								return false;
							}
							if (!VmDetail::AssignMapEntry((*MapHandle)->IntEntries, Key, AssignEntryValue))
							{
								return false;
							}
							Push(std::move(ContainerValue));
							break;
						}
						if ((*MapHandle)->KeyTypeName == "unsigned int")
						{
							const auto* Key = std::get_if<std::uint32_t>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be unsigned int", SourceLocation);
								return false;
							}
							if (!VmDetail::AssignMapEntry((*MapHandle)->UIntEntries, *Key, AssignEntryValue))
							{
								return false;
							}
							Push(std::move(ContainerValue));
							break;
						}
						if ((*MapHandle)->KeyTypeName == "int64")
						{
							const auto* Key = std::get_if<std::int64_t>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be int64", SourceLocation);
								return false;
							}
							if (!VmDetail::AssignMapEntry((*MapHandle)->Int64Entries, *Key, AssignEntryValue))
							{
								return false;
							}
							Push(std::move(ContainerValue));
							break;
						}
						if ((*MapHandle)->KeyTypeName == "uint64")
						{
							const auto* Key = std::get_if<std::uint64_t>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be uint64", SourceLocation);
								return false;
							}
							if (!VmDetail::AssignMapEntry((*MapHandle)->UInt64Entries, *Key, AssignEntryValue))
							{
								return false;
							}
							Push(std::move(ContainerValue));
							break;
						}
						if ((*MapHandle)->KeyTypeName == "string")
						{
							const auto* Key = std::get_if<std::string>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be string", SourceLocation);
								return false;
							}
							if (!VmDetail::AssignMapEntry((*MapHandle)->StrEntries, *Key, AssignEntryValue))
							{
								return false;
							}
							Push(std::move(ContainerValue));
							break;
						}
						if ((*MapHandle)->KeyTypeName == "wstring")
						{
							const auto* Key = std::get_if<std::u16string>(&IndexValue);
							if (!Key)
							{
								ReportError("map key must be wstring", SourceLocation);
								return false;
							}
							if (!VmDetail::AssignMapEntry((*MapHandle)->WStrEntries, *Key, AssignEntryValue))
							{
								return false;
							}
							Push(std::move(ContainerValue));
							break;
						}
						if (const auto* Key = std::get_if<FEnumValue>(&IndexValue);
						    Key && Key->TypeName == Runtime::EnumValueTypeName(*StateView.GetActiveModule(), (*MapHandle)->KeyTypeName))
						{
							if (!VmDetail::AssignMapEntry((*MapHandle)->EnumEntries, *Key, AssignEntryValue))
							{
								return false;
							}
							Push(std::move(ContainerValue));
							break;
						}
						if (const auto* Key = std::get_if<FHostObjectHandle>(&IndexValue);
						    Key && *Key && Services.ValueMatchesElementType(IndexValue, (*MapHandle)->KeyTypeName, SourceLocation))
						{
							if (!Services.AssignHostMapValue(**MapHandle, *Key, Value, SourceLocation)) return false;
							Push(std::move(ContainerValue));
							break;
						}
						ReportError("invalid map key type", SourceLocation);
						return false;
					}
					{
						auto* OrderedMapHandle = std::get_if<FOrderedMapHandle>(&ContainerValue);
						if (OrderedMapHandle)
						{
							if (!*OrderedMapHandle)
							{
								ReportError("index assign requires ordered_map", SourceLocation);
								return false;
							}
							if (!Services.ValueMatchesElementType(
							        Value,
							        (*OrderedMapHandle)->ValueTypeName,
							        SourceLocation))
							{
								Services.ReportError(
								    "ordered_map value type mismatch for " + (*OrderedMapHandle)->TypeName,
								    SourceLocation);
								return false;
							}
							if (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
							{
								std::int32_t Key = 0;
								if (!VmDetail::TryGetSigned32Key(
								        IndexValue, (*OrderedMapHandle)->KeyTypeName, Key))
								{
									Services.ReportError(
									    "ordered_map key must be " + (*OrderedMapHandle)->KeyTypeName,
									    SourceLocation);
									return false;
								}
								if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->IntEntries, Key, AssignEntryValue, &(*OrderedMapHandle)->IntKeyOrder))
								{
									return false;
								}
								Push(std::move(ContainerValue));
								break;
							}
							if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
							{
								const auto* Key = std::get_if<std::uint32_t>(&IndexValue);
								if (!Key)
								{
									Services.ReportError(
									    "ordered_map key must be unsigned int", SourceLocation);
									return false;
								}
								if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->UIntEntries, *Key, AssignEntryValue, &(*OrderedMapHandle)->UIntKeyOrder))
								{
									return false;
								}
								Push(std::move(ContainerValue));
								break;
							}
							if ((*OrderedMapHandle)->KeyTypeName == "int64")
							{
								const auto* Key = std::get_if<std::int64_t>(&IndexValue);
								if (!Key)
								{
									Services.ReportError(
									    "ordered_map key must be int64", SourceLocation);
									return false;
								}
								if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->Int64Entries, *Key, AssignEntryValue, &(*OrderedMapHandle)->Int64KeyOrder))
								{
									return false;
								}
								Push(std::move(ContainerValue));
								break;
							}
							if ((*OrderedMapHandle)->KeyTypeName == "uint64")
							{
								const auto* Key = std::get_if<std::uint64_t>(&IndexValue);
								if (!Key)
								{
									Services.ReportError(
									    "ordered_map key must be uint64", SourceLocation);
									return false;
								}
								if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->UInt64Entries, *Key, AssignEntryValue, &(*OrderedMapHandle)->UInt64KeyOrder))
								{
									return false;
								}
								Push(std::move(ContainerValue));
								break;
							}
							if ((*OrderedMapHandle)->KeyTypeName == "string")
							{
								const auto* Key = std::get_if<std::string>(&IndexValue);
								if (!Key)
								{
									ReportError("ordered_map key must be string", SourceLocation);
									return false;
								}
								if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->StrEntries, *Key, AssignEntryValue, &(*OrderedMapHandle)->StrKeyOrder))
								{
									return false;
								}
								Push(std::move(ContainerValue));
								break;
							}
							if ((*OrderedMapHandle)->KeyTypeName == "wstring")
							{
								const auto* Key = std::get_if<std::u16string>(&IndexValue);
								if (!Key)
								{
									ReportError("ordered_map key must be wstring", SourceLocation);
									return false;
								}
								if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->WStrEntries, *Key, AssignEntryValue, &(*OrderedMapHandle)->WStrKeyOrder))
								{
									return false;
								}
								Push(std::move(ContainerValue));
								break;
							}
							if (const auto* Key = std::get_if<FEnumValue>(&IndexValue);
							    Key && Key->TypeName == Runtime::EnumValueTypeName(*StateView.GetActiveModule(), (*OrderedMapHandle)->KeyTypeName))
							{
								if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->EnumEntries, *Key, AssignEntryValue, &(*OrderedMapHandle)->EnumKeyOrder))
								{
									return false;
								}
								Push(std::move(ContainerValue));
								break;
							}
							if (const auto* Key = std::get_if<FHostObjectHandle>(&IndexValue);
							    Key && *Key && Services.ValueMatchesElementType(IndexValue, (*OrderedMapHandle)->KeyTypeName, SourceLocation))
							{
								if (!Services.AssignHostMapValue(**OrderedMapHandle, *Key, Value, SourceLocation)) return false;
								Push(std::move(ContainerValue));
								break;
							}
							ReportError("invalid ordered_map key type", SourceLocation);
							return false;
						}
					}
					if (!Runtime::IsIntegerValue(IndexValue))
					{
						ReportError("array index must be an integer", SourceLocation);
						return false;
					}
					std::size_t ResolvedIndex = 0;
					if (!Host.Runtime_TryGetSizeValue(IndexValue, ResolvedIndex))
					{
						Services.ReportError(std::holds_alternative<FDequeHandle>(ContainerValue)
						        ? "deque index out of bounds"
						        : "array index out of bounds",
						    SourceLocation);
						return false;
					}
					const std::size_t Index = ResolvedIndex;
					auto* NativeArray = std::get_if<FNativeArray>(&ContainerValue);
					if (NativeArray)
					{
						if (static_cast<std::uint64_t>(Index) >= NativeArray->Length() || static_cast<std::size_t>(Index) >= NativeArray->Storage().Elements.size())
						{
							ReportError("array index out of bounds", SourceLocation);
							return false;
						}
						if (!Services.ValueMatchesElementType(Value, NativeArray->ElementTypeName(), SourceLocation))
						{
							Services.ReportError(
							    "array element type mismatch for " + NativeArray->TypeName(),
							    SourceLocation);
							return false;
						}
						if (!(Opcode == EOpcode::InitIndex
						            ? Services.AssignValue(NativeArray->Storage().Elements[static_cast<std::size_t>(Index)], std::move(Value), SourceLocation)
						            : Services.AssignClonedValue(NativeArray->Storage().Elements[static_cast<std::size_t>(Index)], Value, SourceLocation)))
						{
							return false;
						}
						Push(std::move(ContainerValue));
						break;
					}
					auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&ContainerValue);
					if (DynamicArray && *DynamicArray)
					{
						const std::size_t Size = (*DynamicArray)->HasScalarElements()
						    ? (*DynamicArray)->ScalarSize()
						    : (*DynamicArray)->Elements.size();
						if (static_cast<std::size_t>(Index) >= Size)
						{
							ReportError("array index out of bounds", SourceLocation);
							return false;
						}
						if (!Services.ValueMatchesElementType(Value, (*DynamicArray)->ElementTypeName, SourceLocation))
						{
							Services.ReportError(
							    "array element type mismatch for array<" + (*DynamicArray)->ElementTypeName + ">",
							    SourceLocation);
							return false;
						}
						if ((*DynamicArray)->HasScalarElements())
						{
							if (!(*DynamicArray)->TrySetScalar(static_cast<std::size_t>(Index), Value))
							{
								ReportError("invalid scalar array storage", SourceLocation);
								return false;
							}
						}
						else if (!Services.AssignClonedValue(
						             (*DynamicArray)->Elements[static_cast<std::size_t>(Index)],
						             Value,
						             SourceLocation))
						{
							return false;
						}
						Push(std::move(ContainerValue));
						break;
					}
					auto* DequeHandle = std::get_if<FDequeHandle>(&ContainerValue);
					if (DequeHandle && *DequeHandle)
					{
						if (static_cast<std::size_t>(Index) >= (*DequeHandle)->Elements.size())
						{
							ReportError("deque index out of bounds", SourceLocation);
							return false;
						}
						if (!Services.ValueMatchesElementType(Value, (*DequeHandle)->ElementTypeName, SourceLocation))
						{
							Services.ReportError(
							    "deque element type mismatch for deque<" + (*DequeHandle)->ElementTypeName + ">",
							    SourceLocation);
							return false;
						}
						if (!Services.AssignClonedValue(
						        (*DequeHandle)->Elements[static_cast<std::size_t>(Index)],
						        Value,
						        SourceLocation))
						{
							return false;
						}
						Push(std::move(ContainerValue));
						break;
					}
					ReportError("index assignment requires array", SourceLocation);
					return false;
				}
			case EOpcode::Dup:
				{
					if (StateView.GetOperandStack().size() <= Frame.StackBase)
					{
						ReportError("dup on empty stack", SourceLocation);
						return false;
					}
					Push(StateView.GetOperandStack().back());
					break;
				}
			case EOpcode::Return:
				{
					FValue ReturnValue = Pop();
					if (StateView.GetErrorFlag() || !Services.BeginReturnOrComplete(StateView.GetFrames().size() - 1u, std::move(ReturnValue), SourceLocation))
					{
						return false;
					}
					break;
				}
			case EOpcode::Throw:
				{
					FValue Exception = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					bool bHandledInCurrentFrame = false;
					if (!Services.TryHandleCurrentFrameScriptException(
					        Exception, SourceLocation, bHandledInCurrentFrame))
					{
						return false;
					}
					if (bHandledInCurrentFrame)
					{
						break;
					}
					return Services.RaiseScriptException(std::move(Exception), SourceLocation);
				}
			case EOpcode::EndFinally:
				{
					if (Frame.FinallyContinuations.empty())
					{
						ReportError("end-finally without pending transfer", SourceLocation);
						return false;
					}
					FCallFrame::FFinallyContinuation Continuation =
					    std::move(Frame.FinallyContinuations.back());
					Frame.FinallyContinuations.pop_back();
					switch (Continuation.Kind)
					{
					case FCallFrame::EFinallyContinuationKind::Jump:
						{
							if (!Services.TryTransferToOffset(StateView.GetFrames().size() - 1u, Continuation.TargetOffset, SourceLocation))
							{
								return false;
							}
							break;
						}
					case FCallFrame::EFinallyContinuationKind::Return:
						{
							if (!Services.BeginReturnOrComplete(StateView.GetFrames().size() - 1u, std::move(Continuation.Value), SourceLocation))
							{
								return false;
							}
							break;
						}
					case FCallFrame::EFinallyContinuationKind::Exception:
						{
							(void)Services.RaiseScriptException(
							    std::move(Continuation.Value), SourceLocation);
							if (!Continuation.Error.Message.empty())
							{
								StateView.GetErrorFlag() = true;
								StateView.GetError() = std::move(Continuation.Error);
							}
							return false;
						}
					case FCallFrame::EFinallyContinuationKind::FatalError:
						{
							StateView.GetErrorFlag() = true;
							StateView.GetError() = std::move(Continuation.Error);
							return false;
						}
					default:
						{
							break;
						}
					}
					break;
				}
			case EOpcode::DerefRef:
				{
					FValue Binding = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					FValue* const Storage = ResolveRefStorage(Binding);
					if (!Storage || Storage == &Binding)
					{
						ReportError("reference-returning call produced an invalid binding", SourceLocation);
						return false;
					}
					Push(*Storage);
					break;
				}
			case EOpcode::CheckedCast:
				{
					const std::uint16_t TypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					if (TypeNameIndex >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid checked cast target", SourceLocation);
						return false;
					}
					FValue Value = Pop();
					const std::string& TargetTypeName =
					    StateView.GetActiveModule()->StringPool[TypeNameIndex];
					const bool VariantSource = std::holds_alternative<FScriptVariantHandle>(Value);
					if (VariantSource)
					{
						const bool EmptyVariant = !Host.VariantHasValue(Value);
						FValue Extracted;
						const bool Ok = Services.ExtractVariantValue(Value, TargetTypeName, Extracted);
						(void)Services.ReleaseValue(Value, SourceLocation);
						if (!Ok || StateView.GetErrorFlag())
						{
							if (!StateView.GetErrorFlag())
							{
								if (EmptyVariant)
								{
									Services.ReportErrorId("diag.variant_empty_cast", SourceLocation);
								}
								else
								{
									Services.ReportError("cannot cast variant to " + TargetTypeName, SourceLocation);
								}
							}
							return false;
						}
						Value = std::move(Extracted);
					}
					if (StateView.GetErrorFlag() || (!VariantSource && !Services.CheckedCastValue(Value, TargetTypeName, SourceLocation)))
					{
						return false;
					}
					const std::uint16_t TargetTypeIndex = Services.FindTypeIndex(TargetTypeName);
					if (!VariantSource && TargetTypeIndex != static_cast<std::uint16_t>(-1) && StateView.GetActiveModule()->Types[TargetTypeIndex].Kind == EScriptObjectKind::Struct)
					{
						FValue Cloned;
						if (!Services.CopyLanguageValue(Value, Cloned, SourceLocation, true, TargetTypeName))
						{
							(void)Services.ReleaseValue(Value, SourceLocation);
							return false;
						}
						if (!Services.ReleaseValue(Value, SourceLocation))
						{
							(void)Services.ReleaseValue(Cloned, SourceLocation);
							return false;
						}
						Value = std::move(Cloned);
					}
					Push(std::move(Value));
					break;
				}
			case EOpcode::TryCast:
				{
					const std::uint16_t TargetTypeNameIndex = Bytecode::ReadU16LE(Instruction + 1);
					const std::uint16_t OptionalTypeIndex = Bytecode::ReadU16LE(Instruction + 3);
					if (TargetTypeNameIndex >= StateView.GetActiveModule()->StringPool.size() || OptionalTypeIndex >= StateView.GetActiveModule()->Types.size())
					{
						ReportError("invalid try_cast metadata", SourceLocation);
						return false;
					}
					FValue Candidate = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					const bool VariantSource = std::holds_alternative<FScriptVariantHandle>(Candidate);
					bool bHasValue = false;
					if (VariantSource)
					{
						FValue Extracted;
						bHasValue = Services.ExtractVariantValue(Candidate, StateView.GetActiveModule()->StringPool[TargetTypeNameIndex], Extracted);
						(void)Services.ReleaseValue(Candidate, SourceLocation);
						if (StateView.GetErrorFlag()) return false;
						Candidate = std::move(Extracted);
					}
					else
					{
						bHasValue = Services.TryCheckedCastValue(Candidate, StateView.GetActiveModule()->StringPool[TargetTypeNameIndex]);
					}
					if (bHasValue && !VariantSource)
					{
						const std::uint16_t TargetTypeIndex = Services.FindTypeIndex(
						    StateView.GetActiveModule()->StringPool[TargetTypeNameIndex]);
						if (TargetTypeIndex != static_cast<std::uint16_t>(-1) && StateView.GetActiveModule()->Types[TargetTypeIndex].Kind == EScriptObjectKind::Struct)
						{
							FValue Cloned;
							if (!Services.CloneValueOrReport(Candidate, Cloned, SourceLocation))
							{
								return false;
							}
							Candidate = std::move(Cloned);
						}
					}
					const FObjectHandle OptionalObject = Services.CreateObject(OptionalTypeIndex, SourceLocation);
					if (!OptionalObject)
					{
						return false;
					}
					std::uint16_t PresentFieldIndex = static_cast<std::uint16_t>(-1);
					std::uint16_t ValueFieldIndex = static_cast<std::uint16_t>(-1);
					if (!Host.ModuleMeta_FindFieldIndex(
					        *StateView.GetActiveModule(), OptionalObject->GetTypeName(), "present", PresentFieldIndex) ||
					    !Host.ModuleMeta_FindFieldIndex(
					        *StateView.GetActiveModule(), OptionalObject->GetTypeName(), "value", ValueFieldIndex) ||
					    PresentFieldIndex >= OptionalObject->Fields.size() || ValueFieldIndex >= OptionalObject->Fields.size())
					{
						ReportError("invalid optional<T> runtime representation", SourceLocation);
						return false;
					}
					OptionalObject->Fields[PresentFieldIndex] = bHasValue;
					if (bHasValue)
					{
						OptionalObject->Fields[ValueFieldIndex] = std::move(Candidate);
					}
					else if (!Services.ReleaseValue(Candidate, SourceLocation))
					{
						return false;
					}
					Push(OptionalObject);
					break;
				}
			case EOpcode::Pop:
				{
					FValue Discarded = Pop();
					if (StateView.GetErrorFlag())
					{
						return false;
					}
					// Scalars need no ReleaseValue walk (no owned handles).
					if (std::holds_alternative<std::int8_t>(Discarded) || std::holds_alternative<std::uint8_t>(Discarded) || std::holds_alternative<std::int16_t>(Discarded) || std::holds_alternative<std::uint16_t>(Discarded) || std::holds_alternative<std::int32_t>(Discarded) || std::holds_alternative<std::int64_t>(Discarded) || std::holds_alternative<float>(Discarded) || std::holds_alternative<double>(Discarded) || std::holds_alternative<bool>(Discarded) || std::holds_alternative<std::uint32_t>(Discarded) || std::holds_alternative<std::uint64_t>(Discarded))
					{
						break;
					}
					if (!Services.ReleaseValue(Discarded, SourceLocation))
					{
						return false;
					}
					break;
				}
			case EOpcode::BoxVariant:
			case EOpcode::VariantIs:
				{
					const auto Index = Bytecode::ReadU16LE(Instruction + 1);
					if (Index >= StateView.GetActiveModule()->StringPool.size())
					{
						ReportError("invalid variant type operand", SourceLocation);
						return false;
					}
					FValue Source = Pop(), Result;
					bool Ok = !StateView.GetErrorFlag();
					try
					{
						if (Ok && Opcode == EOpcode::BoxVariant)
						{
							Ok = Services.BoxVariantValue(Source, StateView.GetActiveModule()->StringPool[Index], Result);
						}
						else if (Ok)
						{
							Ok = Services.ValidateVariantValue(Source);
							if (Ok)
							{
								Result = Host.VariantIs(Source, StateView.GetActiveModule()->StringPool[Index]);
							}
						}
					}
					catch (...)
					{
						(void)Services.ReleaseValue(Source, SourceLocation);
						throw;
					}
					const bool Released = Services.ReleaseValue(Source, SourceLocation);
					if (!Ok || !Released || StateView.GetErrorFlag())
					{
						(void)Services.ReleaseValue(Result, SourceLocation);
						return false;
					}
					Push(std::move(Result));
					break;
				}
			case EOpcode::ConstNull:
				{
					Push(FObjectHandle{});
					break;
				}
			case EOpcode::LoadThis:
				{
					if (!Frame.ThisObject)
					{
						ReportError("'this' is only valid inside methods", SourceLocation);
						return false;
					}
					Push(Frame.ThisObject);
					break;
				}
			default:
				{
					Services.ReportError(
					    "unknown opcode " + std::to_string(Instruction[0]) + " at byte " + std::to_string(StateView.GetActiveInstructionStart()),
					    SourceLocation);
					return false;
				}
			}
		}

		return !StateView.GetErrorFlag();

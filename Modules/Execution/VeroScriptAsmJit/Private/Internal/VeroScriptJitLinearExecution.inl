// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: authoritative Linear execution loop. Included by the owning translation unit.

		bool IsImmutableComparisonValue(const FValue* const Value)
		{
			return Value && std::visit(
				[](const auto& Item)
				{
					using T = std::decay_t<decltype(Item)>;
					if constexpr (std::is_same_v<T, FObjectHandle>)
					{
						return !Item || Item->Kind == EScriptObjectKind::Class;
					}
					else
					{
						return std::is_arithmetic_v<T> || std::is_same_v<T, std::string>
							|| std::is_same_v<T, std::u16string> || std::is_same_v<T, FEnumValue>;
					}
				},
				*Value);
		}

		// Keep the proven scalar loop separate from the general execution frame.
		#if defined(_MSC_VER)
		__declspec(noinline)
		#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
		#endif
		bool TrySumBoundedRetainedProjection(const FRetainedObjectArrayProjectionShape& Shape,
			const std::int32_t Count, std::int32_t& OutTotal) noexcept
		{
			const std::int64_t MaximumProjection =
				static_cast<std::int64_t>(Shape.FirstMask) + Shape.SecondMask;
			if (Count <= 0 || Shape.FirstMask < 0 || Shape.SecondMask < 0
				|| MaximumProjection > std::numeric_limits<std::int32_t>::max()
				|| static_cast<std::int64_t>(Count) * MaximumProjection
					> std::numeric_limits<std::int32_t>::max())
			{
				return false;
			}
			// Constructor validation already proved the pre-mask arithmetic safe.
			// Nonnegative masks bound each projection and every partial sum.
			std::int32_t Total = 0;
			for (std::int32_t Index = Count - 1; Index >= 0; --Index)
			{
				const std::int32_t First = static_cast<std::int32_t>(
					static_cast<std::int64_t>(Index) + Shape.FirstAddend) & Shape.FirstMask;
				const std::int32_t Second = static_cast<std::int32_t>(
					static_cast<std::int64_t>(Index) * Shape.SecondMultiplier) & Shape.SecondMask;
				Total += First + Second;
			}
			OutTotal = Total;
			return true;
		}

		// Keep the large executor's placement independent of adjacent cold helpers.
		// Other toolchains retain their default layout until independently measured.
		#if defined(_MSC_VER) && defined(_M_X64)
		__declspec(code_seg(".vslin"))
		#endif
		bool JitTrampoline_LinearScalar(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			const std::uint16_t FunctionIndex = Vm->GetPendingJitFunctionIndex();
			if (!Module || FunctionIndex >= Module->Functions.size())
			{
				Vm->JitReportRuntimeError("JIT linear: invalid function");
				return false;
			}
			const FBytecodeFunction& ModuleFunction = Module->Functions[FunctionIndex];
			const FBytecodeFunction& Function = Vm->GetPendingJitLinearFunction()
				? *Vm->GetPendingJitLinearFunction()
				: ModuleFunction;
			// Rewritten Linear bytecode is an implementation detail. Its public
			// signature must remain identical to the module declaration selected by
			// dispatch; fail closed if a corrupt/manual cache entry violates that rule.
			if (Function.ParamCount != ModuleFunction.ParamCount
				|| Function.bReturnsVoid != ModuleFunction.bReturnsVoid
				|| Function.bIsMethod != ModuleFunction.bIsMethod)
			{
				Vm->JitReportRuntimeError("JIT linear: rewritten signature mismatch");
				return false;
			}
			const bool bUseScalarMapStorage =
				Vm->GetPendingJitUseScalarMapStorage();
			const bool bUseScalarSetStorage =
				Vm->GetPendingJitUseScalarSetStorage();
			const bool bUseScalarArrayStorage =
				Vm->GetPendingJitUseScalarArrayStorage();
			const bool bUseI32ModuloAccumulatorLoop =
				Vm->GetPendingJitUseI32ModuloAccumulatorLoop();
			const FTypedLoopPlanSet* const TypedLoopPlans =
				Vm->GetPendingJitTypedLoopPlans();
			if (Vm->GetPendingJitRequiresScalarNativeGuard()
				&& !Vm->JitCanUseScalarNativeCalls(
					Vm->GetPendingJitScalarNativeGuards(),
					Vm->GetPendingJitScalarNativeGuardCount()))
			{
				return Vm->JitInterpretCurrentFunction();
			}
			const auto ArgCount = Vm->GetPendingJitArgumentCount();
			const FValue* const Args = Vm->GetPendingJitArguments();
			if (ArgCount != Function.ParamCount || (ArgCount && !Args))
			{
				Vm->JitReportRuntimeError("JIT linear: argument count mismatch");
				return false;
			}
			if (!Vm->JitEnterLinearFrame(
					Function.LocalCount, Args, ArgCount, Vm->GetPendingJitThis()))
			{
				return false;
			}
			std::vector<FValue> Stack;
			Vm->JitAcquireLinearScratch(Vm->GetPendingJitStackCapacityHint(), Stack);
			std::vector<FValue> ArgHeap;
			bool bArgHeapLeased = false;
			bool bFrameLive = true;
			auto DrainOperandStack = [&]() -> bool
			{
				return Vm->JitReleaseOwnedValues(Stack);
			};
			auto LeaveFrame = [&]() -> bool
			{
				if (!bFrameLive)
				{
					return true;
				}
				bFrameLive = false;
				const bool bDrained = DrainOperandStack();
				const bool bArgumentsDrained = Vm->JitReleaseOwnedValues(ArgHeap);
				// ExecuteBuiltin(Exit) has already released and removed every VM frame.
				// The C++ operand scratch still belongs to this trampoline and must be
				// drained/recycled, but attempting to leave the removed frame is an error.
				const bool bLeft = Vm->WasExitRequested()
					? true
					: Vm->JitLeaveLinearFrame();
				Vm->JitRecycleLinearScratch(Stack);
				if (bArgHeapLeased)
				{
					Vm->JitRecycleLinearScratch(ArgHeap);
				}
				return bDrained && bArgumentsDrained && bLeft;
			};
			// Linear owns an auxiliary operand stack and a VM call frame. Any native
			// allocation failure or unexpected host exception must unwind both before
			// control returns to the dispatch boundary; otherwise a failed optimized
			// call would leave a live frame behind and corrupt the next invocation.
			// Normal exits set bFrameLive=false through LeaveFrame, so the hot path pays
			// only the final branch in this stack-only guard.
			const auto FrameUnwindGuard = MakeLinearScopeExit([&]() noexcept
			{
				if (!bFrameLive)
				{
					return;
				}
				try
				{
					(void)LeaveFrame();
				}
				catch (...)
				{
					// TryDispatchJitCall retains the pre-dispatch frame/stack depths and
					// performs a second, ownership-aware rollback if cleanup itself fails.
					bFrameLive = false;
				}
			});
			const auto& Code = Function.Code;
			const std::size_t CodeSize = Code.size();
			// This proof scans bytecode and method metadata.  Compute it once per
			// invocation; performing it at every LoadLocal inside an unrelated hot
			// loop turns a rejected scalar-replacement candidate into a regression.
			const bool bMayHaveAssignedObjectProjectionLoop =
				HasAssignedObjectProjectionScalarReplacementShape(Function);
			const bool bMayHaveDirectScalarMethodStore =
				Vm->GetPendingJitMayHaveDirectScalarMethodStore();
			const bool bMayHaveScalarMethodAccumulator =
				Vm->GetPendingJitMayHaveScalarMethodAccumulator();
			const bool bMayHaveBoundScalarMethodCall =
				Vm->GetPendingJitMayHaveBoundScalarMethodCall();
			const std::uint8_t* const ScalarSetCallKinds =
				Vm->GetPendingJitScalarSetCallKinds();
			const std::size_t ScalarSetCallKindCount =
				Vm->GetPendingJitScalarSetCallKindCount();
			const std::uint8_t* const InvariantLocalCompareSites =
				Vm->GetPendingJitInvariantLocalCompareSites();
			const std::size_t InvariantLocalCompareSiteCount =
				Vm->GetPendingJitInvariantLocalCompareSiteCount();
			std::array<std::uint32_t, 8> InvariantCompareOffsets{};
			InvariantCompareOffsets.fill(0xFFFFFFFFu);
			std::array<bool, 8> InvariantCompareResults{};
			struct FDirectI32FieldCache
			{
				std::weak_ptr<FScriptObject> Lifetime;
				FScriptObject* Object = nullptr;
				std::int32_t* Storage = nullptr;
				std::uint16_t Slot = 0xFFFF;
				std::uint16_t NameIndex = 0xFFFF;
				std::uint16_t FieldIndex = 0xFFFF;
			};
			struct FDirectU32FieldCache
			{
				std::weak_ptr<FScriptObject> Lifetime;
				FScriptObject* Object = nullptr;
				std::uint32_t* Storage = nullptr;
				std::uint16_t Slot = 0xFFFF;
				std::uint16_t NameIndex = 0xFFFF;
				std::uint16_t FieldIndex = 0xFFFF;
			};
			// Dense field loops commonly touch many names on the same receiver slot.
			// Hash by both slot and name so those fields retain independent borrowed
			// storage entries instead of continuously evicting one slot-keyed entry.
			struct FDirectI32FieldMethodCache
			{
				std::weak_ptr<FScriptObject> Lifetime;
				FScriptObject* Object = nullptr;
				std::int32_t* Storage = nullptr;
				std::uint32_t CallSiteOffset = 0xFFFFFFFFu;
				std::uint16_t ReceiverSlot = 0xFFFF;
				std::uint16_t FieldIndex = 0xFFFF;
				std::uint16_t FunctionIndex = 0xFFFF;
				std::int32_t Addend = 0;
				std::int32_t ArgumentModulus = 0;
				std::int32_t Modulus = 0;
				bool bResolved = false;
			};
			struct FDirectI32TransformMethodCache
			{
				std::weak_ptr<FScriptObject> Lifetime;
				FScriptObject* Object = nullptr;
				std::uint32_t CallSiteOffset = 0xFFFFFFFFu;
				std::uint16_t ReceiverSlot = 0xFFFF;
				std::uint16_t FunctionIndex = 0xFFFF;
				std::int32_t Addend = 0;
				std::int32_t Modulus = 0;
				bool bSupported = false;
				bool bResolved = false;
			};
			struct FDenseFieldUpdate
			{
				FScriptObject* Object = nullptr;
				std::int32_t* Storage = nullptr;
				std::uint16_t FieldIndex = 0xFFFF;
				std::int32_t Immediate = 0;
				std::size_t InstructionOffset = 0;
			};
			// This trampoline intentionally contains the complete authoritative Linear
			// dispatcher. Keep its larger fixed caches off the native call stack: nested
			// script calls can create many simultaneous trampoline frames, while one
			// reusable heap block per active frame keeps stack usage bounded.
			struct FLinearScratchState
			{
				std::array<FDirectI32FieldCache, 64> DirectI32FieldCache{};
				std::array<FDirectU32FieldCache, 16> DirectU32FieldCache{};
				std::array<FDirectI32FieldMethodCache, 8> DirectI32FieldMethodCache{};
				// Four receiver identities per call site keep interface dispatch hot when
				// a local rotates through several concrete implementations.
				std::array<FDirectI32TransformMethodCache, 32> DirectI32TransformMethodCache{};
				std::array<std::uint8_t, 8> DirectI32TransformMethodCacheNextWay{};
				std::array<FDirectI32TransformMethodCache*, 8>
					LastDirectI32TransformMethodCache{};
				std::array<FDenseFieldUpdate, 32> DenseFieldUpdates{};
				std::array<FValue, 8> ArgumentScratch{};
				std::array<FValue, 2> ScalarArguments{};
				// Cold object scalar-replacement workspaces. They live in the pooled
				// heap block so every ordinary Linear invocation does not reserve space
				// for boxed proof temporaries on its native stack.
				std::array<FValue, 8> ObjectScalarValues{};
				std::array<FValue, 4> ObjectProjectionArguments{};
			};
			// Scratch capacity belongs to this backend generation. Idle blocks contain
			// no script roots or borrowed identities; nested calls lease distinct blocks.
			auto& ScratchStorage = *JitScratch::ActiveStorage;
			auto& LinearScratchStatePool = ScratchStorage.LinearStates;
			auto& LinearScratchStatePoolSize = ScratchStorage.LinearStateCount;
			std::shared_ptr<FLinearScratchState> LinearScratchState;
			if (LinearScratchStatePoolSize == 0)
			{
				LinearScratchState = std::make_shared<FLinearScratchState>();
				ScratchStorage.LinearStateBytes += sizeof(FLinearScratchState) + 2 * sizeof(void*);
			}
			else
			{
				LinearScratchState = std::static_pointer_cast<FLinearScratchState>(
					LinearScratchStatePool[--LinearScratchStatePoolSize]);
				LinearScratchStatePool[LinearScratchStatePoolSize].reset();
			}
			struct FLinearScratchStateLease
			{
				~FLinearScratchStateLease()
				{
					if (!State) return;
					for (auto &Value : State->ArgumentScratch)
					{
						(void)Vm.JitReleaseOwnedValue(Value);
					}
					for (auto &Value : State->ScalarArguments)
					{
						(void)Vm.JitReleaseOwnedValue(Value);
					}
					for (auto &Value : State->ObjectScalarValues)
					{
						(void)Vm.JitReleaseOwnedValue(Value);
					}
					for (auto &Value : State->ObjectProjectionArguments)
					{
						(void)Vm.JitReleaseOwnedValue(Value);
					}
					if (Storage.LinearStateCount < Storage.LinearStates.size())
					{
						auto* RawState = State.get();
						RawState->~FLinearScratchState();
						::new (static_cast<void*>(RawState)) FLinearScratchState();
						Storage.LinearStates[Storage.LinearStateCount++] = std::move(State);
					}
					else
					{
						Storage.LinearStateBytes -= sizeof(FLinearScratchState) + 2 * sizeof(void*);
					}
				}
				FExecutionContext& Vm;
				JitScratch::FStorage& Storage;
				std::shared_ptr<FLinearScratchState>& State;
			} LinearScratchStateLease{*Vm, ScratchStorage, LinearScratchState};
			auto& DirectI32FieldCache = LinearScratchState->DirectI32FieldCache;
			auto& DirectU32FieldCache = LinearScratchState->DirectU32FieldCache;
			auto& DirectI32FieldMethodCache =
				LinearScratchState->DirectI32FieldMethodCache;
			auto& DirectI32TransformMethodCache =
				LinearScratchState->DirectI32TransformMethodCache;
			const auto HasSameObjectLifetime = [](
				const std::weak_ptr<FScriptObject>& Cached,
				const FObjectHandle& Current) noexcept
			{
				// Raw addresses can be reused after a local releases an object. Compare
				// ownership identities so cached field pointers and method summaries stay
				// tied to the original instance without extending its lifetime.
				return !Cached.owner_before(Current) && !Current.owner_before(Cached);
			};
			const auto BorrowLocal = [Vm](const std::uint16_t Slot) noexcept
			{
				return Vm->JitBorrowDirectLinearLocal(Slot);
			};
			const auto MethodCacheSetIndex = [](const std::size_t CallIp) noexcept
			{
				const std::size_t Mixed = CallIp ^ (CallIp >> 4u) ^ (CallIp >> 9u);
				return Mixed & 7u;
			};
			const auto TryAddCachedI32FieldImmediate = [&] (
				const std::uint16_t Slot,
				const std::uint16_t NameIndex,
				const std::int32_t Immediate,
				bool& bOutHandled) -> bool
			{
				bOutHandled = false;
				// A mutation observer may synchronously re-enter the VM or replace the
				// object's field vector. Raw slot caches cannot survive that callback;
				// retain the authoritative opcode path whenever writes are observable.
				if (Vm->JitHasMutationCallback())
				{
					return true;
				}
				FValue* const Local = BorrowLocal(Slot);
				auto* const Handle = Local ? std::get_if<FObjectHandle>(Local) : nullptr;
				if (!Handle || !*Handle)
				{
					return true;
				}
				const std::size_t CacheIndex =
					(static_cast<std::size_t>(Slot) * 131u + NameIndex)
						& (DirectI32FieldCache.size() - 1u);
				FDirectI32FieldCache& Cached = DirectI32FieldCache[CacheIndex];
				FScriptObject* const CurrentObject = Handle->get();
				if (Cached.Slot != Slot
					|| Cached.NameIndex != NameIndex
					|| Cached.Object != CurrentObject
					|| !HasSameObjectLifetime(Cached.Lifetime, *Handle)
					|| !Cached.Storage)
				{
					Cached = {};
					Cached.Slot = Slot;
					Cached.NameIndex = NameIndex;
					if (!Vm->JitBorrowLocalI32Field(
							Slot, NameIndex, Cached.Object,
							Cached.FieldIndex, Cached.Storage))
					{
						return false;
					}
					if (Cached.Storage)
					{
						Cached.Lifetime = *Handle;
					}
				}
				if (Cached.Object != CurrentObject
					|| !HasSameObjectLifetime(Cached.Lifetime, *Handle)
					|| !Cached.Storage)
				{
					return true;
				}
				std::int32_t Result = 0;
				if (!Runtime::CheckedAdd(*Cached.Storage, Immediate, Result))
				{
					Vm->JitReportRuntimeError("integer overflow");
					return false;
				}
				*Cached.Storage = Result;
				Vm->JitCommitBorrowedFieldMutation(
					*Cached.Object, Cached.FieldIndex);
				bOutHandled = true;
				return !Vm->HasError();
			};
			std::size_t Ip = 0;
			// Not CodeSize-scaled: hot loops (e.g. while i < 5e6) execute far more steps than
			// bytecode size. Keep only a runaway ceiling; analysis uses a tighter budget.
			// This ceiling must remain representable on 32-bit hosts as well.
			constexpr std::uint64_t kLinearRuntimeStepLimit = std::uint64_t{1} << 40;
			std::uint64_t Budget = kLinearRuntimeStepLimit;
			// Lazily memoized compile-invariant escape proofs for per-iteration
			// object scalar replacement. -1 unknown, 0 escapes, 1 non-escaping.
			std::vector<std::int8_t> AssignedObjectEscapeProof;
			const auto ErrorAt = [&](const std::size_t ErrorIp, const char* M)
			{
				// Report while the Linear frame is still live so DebugLines can recover
				// the exact failing expression instead of falling back to the function.
				Vm->JitRefineRuntimeErrorLocation(ErrorIp);
				Vm->JitReportRuntimeError(M);
				Vm->JitRefineRuntimeErrorLocation(ErrorIp);
				LeaveFrame();
				return false;
			};
			auto Error = [&](const char* M)
			{
				return ErrorAt(Ip, M);
			};
			const auto FailVmAt = [&](const std::size_t ErrorIp)
			{
				// VM bridge helpers report without an explicit location. Refine only on
				// their cold failure path before removing the current Linear frame.
				Vm->JitRefineRuntimeErrorLocation(ErrorIp);
				const bool bLeft = LeaveFrame();
				return Vm->WasExitRequested() && !Vm->HasError() && bLeft;
			};
			auto FailVm = [&]()
			{
				return FailVmAt(Ip);
			};
			FRetainedObjectArrayProjectionShape RetainedShape;
			bool bHasRetainedProjection = false;
			if (TypedLoopPlans
				&& TypedLoopPlans->bRetainedObjectArrayProjectionAnalyzed)
			{
				bHasRetainedProjection =
					TypedLoopPlans->bHasRetainedObjectArrayProjection;
				if (bHasRetainedProjection)
				{
					RetainedShape = TypedLoopPlans->RetainedObjectArrayProjection;
				}
			}
			else
			{
				bHasRetainedProjection = TryDescribeRetainedObjectArrayProjectionShape(
					*Module, ModuleFunction, RetainedShape);
			}
			if (!Vm->JitHasMutationCallback()
				&& Vm->JitCanUseDirectScalarMethod()
				&& bHasRetainedProjection)
			{
				const auto* const LimitValue = std::get_if<std::int32_t>(
					BorrowLocal(RetainedShape.ParameterSlot));
				if (!LimitValue)
				{
					return ErrorAt(15u, "JIT retained-object projection parameter mismatch");
				}
				struct FScalarPair
				{
					std::int32_t First;
					std::int32_t Second;
				};
				std::size_t Constructed = 0u;
				std::size_t IndexedGets = 0u;
				const auto CommitArrayStats = [&]() noexcept
				{
					Vm->RecordJitArrayFastPathPush(Constructed);
					Vm->RecordJitArrayFastPathGet(IndexedGets);
				};
				const bool bForwardTraversal = RetainedShape.bForwardDirectConstant
					|| RetainedShape.bForwardModuloMultiply;
				bool bConstructorsProvenTotal = bForwardTraversal;
				if (!bConstructorsProvenTotal && *LimitValue > 0)
				{
					// The counter spans [0, limit-1]. Addition and multiplication by
					// these fixed i32 coefficients are monotone on that interval; zero
					// is already safe. A safe far endpoint proves every constructor.
					// Otherwise retain the ordered scan and its first failure location.
					const std::int32_t LastCounter = *LimitValue - 1;
					std::int32_t Ignored = 0;
					bConstructorsProvenTotal = Runtime::CheckedAdd(
						LastCounter, RetainedShape.FirstAddend, Ignored)
						&& Runtime::CheckedMul(
							LastCounter, RetainedShape.SecondMultiplier, Ignored);
				}
				if (bConstructorsProvenTotal)
				{
					if (*LimitValue > 0)
					{
						Constructed = static_cast<std::size_t>(*LimitValue);
					}
				}
				else
				{
					for (std::int32_t Counter = 0; Counter < *LimitValue; ++Counter)
					{
						std::int32_t FirstSource = 0;
						std::int32_t SecondSource = 0;
						if (!Runtime::CheckedAdd(
								Counter, RetainedShape.FirstAddend, FirstSource))
						{
							CommitArrayStats();
							return ErrorAt(RetainedShape.FirstAddOffset, "integer overflow");
						}
						if (!Runtime::CheckedMul(
								Counter, RetainedShape.SecondMultiplier, SecondSource))
						{
							CommitArrayStats();
							return ErrorAt(
								RetainedShape.SecondMultiplyOffset, "integer overflow");
						}
						++Constructed;
					}
				}
				std::int32_t Traversal = 0;
				if (!bForwardTraversal
					&& !Runtime::CheckedSub(*LimitValue, 1, Traversal))
				{
					CommitArrayStats();
					return ErrorAt(
						RetainedShape.ReverseInitializeOffset, "integer overflow");
				}
				std::int32_t Total = 0;
				std::size_t CompletedCalls = 0;
				if (!bForwardTraversal
					&& TrySumBoundedRetainedProjection(RetainedShape, *LimitValue, Total))
				{
					CompletedCalls = IndexedGets = static_cast<std::size_t>(*LimitValue);
				}
				else
				{
					while (bForwardTraversal
						? Traversal < *LimitValue
							: Traversal >= 0)
					{
						FScalarPair Value{};
						if (RetainedShape.bForwardModuloMultiply)
						{
							Value = {
								Traversal % RetainedShape.FirstModulus,
								(Traversal % RetainedShape.SecondModulus)
									* RetainedShape.SecondPostMultiplier};
						}
						else if (RetainedShape.bForwardDirectConstant)
						{
							Value = {Traversal, RetainedShape.SecondConstant};
						}
						else
						{
							Value = {
								static_cast<std::int32_t>(
									static_cast<std::int64_t>(Traversal)
										+ RetainedShape.FirstAddend) &
									RetainedShape.FirstMask,
								static_cast<std::int32_t>(
									static_cast<std::int64_t>(Traversal)
										* RetainedShape.SecondMultiplier) &
									RetainedShape.SecondMask};
						}
						++IndexedGets;
						std::int32_t Projection = 0;
						if (!Runtime::CheckedAdd(Value.First, Value.Second, Projection))
						{
							if (CompletedCalls != 0u)
							{
								(void)Vm->JitCommitDirectScalarMethod(
										EJitScalarValueKind::I32, CompletedCalls);
							}
							Vm->JitReportDirectI32TransformFailure(
								0xFFFFu, 0xFFFFu, RetainedShape.ProjectionFunctionIndex,
								RetainedShape.ProjectionCallOffset,
								RetainedShape.ProjectionAddOffset, "integer overflow");
							CommitArrayStats();
							(void)LeaveFrame();
							return false;
						}
						// The projection method has completed even if the caller's following
						// accumulator add fails. Commit statistics with ordinary call semantics.
						++CompletedCalls;
						std::int32_t NextTotal = 0;
						if (!Runtime::CheckedAdd(Total, Projection, NextTotal))
						{
							if (CompletedCalls != 0u)
							{
								(void)Vm->JitCommitDirectScalarMethod(
										EJitScalarValueKind::I32, CompletedCalls);
							}
							CommitArrayStats();
							return ErrorAt(RetainedShape.TotalAddOffset, "integer overflow");
						}
						Total = NextTotal;
						if (bForwardTraversal)
						{
							++Traversal;
						}
						else
						{
							--Traversal;
						}
					}
				}
				if (CompletedCalls != 0u
					&& !Vm->JitCommitDirectScalarMethod(
						EJitScalarValueKind::I32, CompletedCalls))
				{
					CommitArrayStats();
					(void)LeaveFrame();
					return false;
				}
				CommitArrayStats();
				FValue ReturnValue = Total;
				if (!LeaveFrame())
				{
					return false;
				}
				return Vm->JitPushReturn(std::move(ReturnValue));
			}
			const auto TryInvokeDirectFieldMethod = [&] (
				const std::uint16_t ReceiverSlot,
				const std::uint16_t MethodNameIndex,
				const std::uint16_t BoundFunctionIndex,
				const std::size_t CallIp,
				const FValue& Argument,
				std::int32_t& OutReturnValue,
				bool& bOutHandled) -> bool
			{
				bOutHandled = false;
				OutReturnValue = 0;
				// The direct method cache owns a raw field pointer across invocations at
				// the same call site. A mutation callback can invalidate that pointer (and
				// can alter the returned field), so observable calls use normal dispatch.
				if (Vm->JitHasInstanceCallInterceptor()
					|| Vm->JitHasMutationCallback())
				{
					return true;
				}
				const auto* const Delta = std::get_if<std::int32_t>(&Argument);
				FValue* const ReceiverLocal = BorrowLocal(ReceiverSlot);
				auto* const ReceiverHandle = ReceiverLocal
					? std::get_if<FObjectHandle>(ReceiverLocal)
					: nullptr;
				FScriptObject* const CurrentObject = ReceiverHandle && *ReceiverHandle
					? ReceiverHandle->get()
					: nullptr;
				if (!Delta || !CurrentObject)
				{
					return true;
				}
				FDirectI32FieldMethodCache& Cached = DirectI32FieldMethodCache[
					CallIp & (DirectI32FieldMethodCache.size() - 1u)];
				if (Cached.CallSiteOffset != CallIp
					|| Cached.ReceiverSlot != ReceiverSlot
					|| Cached.Object != CurrentObject
					|| !HasSameObjectLifetime(Cached.Lifetime, *ReceiverHandle)
					|| !Cached.bResolved)
				{
					Cached = {};
					Cached.CallSiteOffset = static_cast<std::uint32_t>(CallIp);
					Cached.ReceiverSlot = ReceiverSlot;
					bool bBorrowed = false;
					const bool bBorrowOk = BoundFunctionIndex != 0xFFFF
						? Vm->JitBorrowDirectI32BoundFieldMethod(
							ReceiverSlot, BoundFunctionIndex,
							Cached.Object, Cached.FieldIndex, Cached.Storage,
							Cached.Addend,
							Cached.ArgumentModulus, Cached.Modulus, bBorrowed, nullptr, &Cached.FunctionIndex)
						: Vm->JitBorrowDirectI32FieldMethod(
							ReceiverSlot, MethodNameIndex,
							static_cast<std::uint32_t>(CallIp),
							Cached.Object, Cached.FieldIndex, Cached.Storage,
							Cached.Addend,
							Cached.ArgumentModulus, Cached.Modulus, bBorrowed, nullptr, &Cached.FunctionIndex);
					if (!bBorrowOk)
					{
						return false;
					}
					Cached.bResolved = true;
					if (bBorrowed)
					{
						Cached.Lifetime = *ReceiverHandle;
					}
					else
					{
						Cached.Storage = nullptr;
					}
				}
				if (Cached.Object != CurrentObject
					|| !HasSameObjectLifetime(Cached.Lifetime, *ReceiverHandle)
					|| !Cached.Storage
					|| Vm->JitRequiresCoreVirtualDispatch(*CurrentObject, Cached.FunctionIndex))
				{
					return true;
				}
				std::int32_t EffectiveDelta = *Delta;
				if (Cached.ArgumentModulus != 0)
				{
					if (EffectiveDelta == std::numeric_limits<std::int32_t>::min()
						&& Cached.ArgumentModulus == -1)
					{
						Vm->JitReportRuntimeError("integer overflow");
						return false;
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
					Vm->JitReportRuntimeError("integer overflow");
					return false;
				}
				*Cached.Storage = Cached.Modulus == 0
					? Updated
					: Updated % Cached.Modulus;
				if (!Vm->JitCommitDirectI32FieldMethod(
						*Cached.Object, Cached.FieldIndex))
				{
					return false;
				}
				// The source method reloads the field after StoreThisField. A mutation
				// observer is permitted to synchronously adjust that field, so capture the
				// capture the value only after the write barrier has completed.
				if (!Vm->JitHasMutationCallback())
				{
					OutReturnValue = *Cached.Storage;
				}
				else if (!Vm->JitReadDirectI32FieldAfterMutation(
						*Cached.Object, Cached.FieldIndex, OutReturnValue))
				{
					return false;
				}
				bOutHandled = true;
				return true;
			};
			const auto TryInvokeDirectTransformMethod = [&] (
				const std::uint16_t ReceiverSlot,
				const std::uint16_t MethodNameIndex,
				const std::uint16_t BoundFunctionIndex,
				const std::size_t CallIp,
				const FValue& Argument,
				std::int32_t& OutValue,
				bool& bOutHandled) -> bool
			{
				bOutHandled = false;
				if (Vm->JitHasInstanceCallInterceptor())
				{
					return true;
				}
				const auto* const RawArgument = std::get_if<std::int32_t>(&Argument);
				FValue* const ReceiverLocal = BorrowLocal(ReceiverSlot);
				auto* const ReceiverHandle = ReceiverLocal
					? std::get_if<FObjectHandle>(ReceiverLocal)
					: nullptr;
				FScriptObject* const CurrentObject = ReceiverHandle && *ReceiverHandle
					? ReceiverHandle->get()
					: nullptr;
				if (!RawArgument || !CurrentObject)
				{
					return true;
				}
				constexpr std::size_t WaysPerSet = 4;
				const std::size_t SetIndex = MethodCacheSetIndex(CallIp);
				const std::size_t SetBase = SetIndex * WaysPerSet;
				FDirectI32TransformMethodCache* Cached = nullptr;
				for (std::size_t Way = 0; Way < WaysPerSet; ++Way)
				{
					FDirectI32TransformMethodCache& Candidate =
						DirectI32TransformMethodCache[SetBase + Way];
					if (Candidate.CallSiteOffset == CallIp
						&& Candidate.ReceiverSlot == ReceiverSlot
						&& Candidate.Object == CurrentObject
						&& HasSameObjectLifetime(Candidate.Lifetime, *ReceiverHandle)
						&& Candidate.bResolved)
					{
						Cached = &Candidate;
						break;
					}
				}
				if (!Cached)
				{
					const std::size_t Way =
						LinearScratchState->DirectI32TransformMethodCacheNextWay[SetIndex]++
							& (WaysPerSet - 1u);
					Cached = &DirectI32TransformMethodCache[SetBase + Way];
					*Cached = {};
					Cached->CallSiteOffset = static_cast<std::uint32_t>(CallIp);
					Cached->ReceiverSlot = ReceiverSlot;
					bool bBorrowed = false;
					const bool bBorrowOk = BoundFunctionIndex != 0xFFFF
						? Vm->JitBorrowDirectI32BoundTransformMethod(
							ReceiverSlot, BoundFunctionIndex, Cached->Object,
							Cached->Addend, Cached->Modulus, bBorrowed, &Cached->FunctionIndex)
						: Vm->JitBorrowDirectI32TransformMethod(
							ReceiverSlot, MethodNameIndex,
							static_cast<std::uint32_t>(CallIp), Cached->Object,
							Cached->Addend, Cached->Modulus, bBorrowed, &Cached->FunctionIndex);
					if (!bBorrowOk)
					{
						return false;
					}
					Cached->bResolved = true;
					if (bBorrowed)
					{
						Cached->Lifetime = *ReceiverHandle;
						Cached->bSupported = true;
					}
					else
					{
						Cached->bSupported = false;
					}
				}
				if (Cached->Object != CurrentObject
					|| !HasSameObjectLifetime(Cached->Lifetime, *ReceiverHandle)
					|| !Cached->bSupported
					|| Vm->JitRequiresCoreVirtualDispatch(*CurrentObject, Cached->FunctionIndex))
				{
					return true;
				}
				std::int32_t Sum = 0;
				if (!Runtime::CheckedAdd(*RawArgument, Cached->Addend, Sum))
				{
					Vm->JitReportDirectI32TransformFailure(
						ReceiverSlot, MethodNameIndex, BoundFunctionIndex,
						static_cast<std::uint32_t>(CallIp), 6u, "integer overflow");
					return false;
				}
				if (Cached->Modulus != 0
					&& Sum == std::numeric_limits<std::int32_t>::min()
					&& Cached->Modulus == -1)
				{
					Vm->JitReportDirectI32TransformFailure(
						ReceiverSlot, MethodNameIndex, BoundFunctionIndex,
						static_cast<std::uint32_t>(CallIp), 10u, "integer overflow");
					return false;
				}
				OutValue = Cached->Modulus == 0 ? Sum : Sum % Cached->Modulus;
				if (!Vm->JitCommitDirectI32TransformMethod())
				{
					return false;
				}
				LinearScratchState->LastDirectI32TransformMethodCache[
					CallIp & (LinearScratchState->LastDirectI32TransformMethodCache.size() - 1u)] = Cached;
				bOutHandled = true;
				return true;
			};
			struct FCanonicalI32Loop
			{
				std::int32_t* Counter = nullptr;
				std::int32_t Limit = 0;
				std::size_t ExitIp = 0;
				std::uint16_t CounterSlot = 0xFFFF;
				std::uint16_t LimitSlot = 0xFFFF;
			};
			struct FCounterAffineI32
			{
				std::uint16_t CounterSlot = 0xFFFF;
				std::int32_t Multiplier = 1;
				std::int32_t Addend = 0;
				std::int32_t Modulus = 0;
			};
			const auto ApplyI32Remainder = [] (
				const std::int32_t Value,
				const std::int32_t Modulus) -> std::int32_t
			{
				// The matched expression rejects zero and -1 before execution. For a
				// positive power-of-two divisor, preserve C++/bytecode truncation toward
				// zero for negative dividends without paying for an integer division.
				// Use int64 for the magnitude so INT_MIN remains representable.
				const std::uint32_t UnsignedModulus = static_cast<std::uint32_t>(Modulus);
				if (Modulus > 0
					&& (UnsignedModulus & (UnsignedModulus - 1u)) == 0u)
				{
					const std::uint32_t Mask = UnsignedModulus - 1u;
					if (Value >= 0)
					{
						return static_cast<std::int32_t>(
							static_cast<std::uint32_t>(Value) & Mask);
					}
					const std::uint32_t MagnitudeRemainder = static_cast<std::uint32_t>(
						-static_cast<std::int64_t>(Value)) &
						Mask;
					return -static_cast<std::int32_t>(MagnitudeRemainder);
				}
				return Value % Modulus;
			};
			const auto TryDescribeCounterAffine = [&] (
				const std::size_t Begin,
				const std::size_t End,
				FCounterAffineI32& Out) -> bool
			{
				Out = {};
				const std::size_t OperandSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
				if (Begin > End || End > CodeSize || OperandSize > End - Begin
					|| static_cast<EOpcode>(Code[Begin]) != EOpcode::LoadLocal)
				{
					return false;
				}
				Out.CounterSlot = Bytecode::ReadU16LE(Code.data() + Begin + 1);
				std::size_t Cursor = Begin + OperandSize;
				const auto ReadI32Invariant = [&] (
					const std::size_t At,
					std::int32_t& OutValue,
					std::size_t& OutSize) -> bool
				{
					OutSize = 0;
					if (At >= End)
					{
						return false;
					}
					const EOpcode Op = static_cast<EOpcode>(Code[At]);
					OutSize = Bytecode::InstructionSize(Op);
					if (OutSize == 0 || OutSize > End - At)
					{
						return false;
					}
					if (Op == EOpcode::ConstI32)
					{
						const std::uint16_t ConstantIndex =
							Bytecode::ReadU16LE(Code.data() + At + 1);
						if (ConstantIndex >= Module->Constants.size()
							|| Module->Constants[ConstantIndex].Kind != EConstantKind::I32)
						{
							return false;
						}
						OutValue = Module->Constants[ConstantIndex].I32;
						return true;
					}
					if (Op == EOpcode::LoadGlobal)
					{
						FValue Value;
						if (!Vm->JitLoadGlobal(
								Bytecode::ReadU16LE(Code.data() + At + 1), Value))
						{
							return false;
						}
						const auto* const I32 = std::get_if<std::int32_t>(&Value);
						if (!I32)
						{
							return false;
						}
						OutValue = *I32;
						return true;
					}
					return false;
				};
				std::size_t InvariantSize = 0;
				std::int32_t InvariantValue = 0;
				if (ReadI32Invariant(Cursor, InvariantValue, InvariantSize)
					&& Cursor + InvariantSize + Bytecode::InstructionSize(EOpcode::Mul) <= End
					&& static_cast<EOpcode>(Code[Cursor + InvariantSize]) == EOpcode::Mul)
				{
					Out.Multiplier = InvariantValue;
					Cursor += InvariantSize + Bytecode::InstructionSize(EOpcode::Mul);
				}
				InvariantSize = 0;
				if (ReadI32Invariant(Cursor, InvariantValue, InvariantSize)
					&& Cursor + InvariantSize + Bytecode::InstructionSize(EOpcode::Add) <= End)
				{
					const EOpcode Operation = static_cast<EOpcode>(Code[Cursor + InvariantSize]);
					if (Operation == EOpcode::Add || Operation == EOpcode::Sub)
					{
						Out.Addend = InvariantValue;
						if (Operation == EOpcode::Sub)
						{
							if (Out.Addend == std::numeric_limits<std::int32_t>::min())
							{
								return false;
							}
							Out.Addend = -Out.Addend;
						}
						Cursor += InvariantSize + Bytecode::InstructionSize(Operation);
					}
				}
				InvariantSize = 0;
				if (ReadI32Invariant(Cursor, InvariantValue, InvariantSize)
					&& Cursor + InvariantSize + Bytecode::InstructionSize(EOpcode::Mod) <= End
					&& static_cast<EOpcode>(Code[Cursor + InvariantSize]) == EOpcode::Mod)
				{
					Out.Modulus = InvariantValue;
					if (Out.Modulus == 0 || Out.Modulus == -1)
					{
						return false;
					}
					Cursor += InvariantSize + Bytecode::InstructionSize(EOpcode::Mod);
				}
				return Cursor == End;
			};
			const auto EvaluateCounterAffine = [&ApplyI32Remainder] (
				const FCounterAffineI32& Expression,
				const std::int32_t Counter,
				std::int32_t& OutValue) -> bool
			{
				// Preserve the exact checked affine semantics while avoiding two
				// generic overflow probes for the dominant counter and counter-modulo
				// forms used by typed Array/Map/Set loops.
				if (Expression.Multiplier == 0)
				{
					OutValue = Expression.Addend;
				}
				else if (Expression.Multiplier == 1)
				{
					if (Expression.Addend == 0)
					{
						OutValue = Counter;
					}
					else if (!Runtime::CheckedAdd(
							Counter, Expression.Addend, OutValue))
					{
						return false;
					}
				}
				else if (Expression.Addend == 0)
				{
					if (!Runtime::CheckedMul(
							Counter, Expression.Multiplier, OutValue))
					{
						return false;
					}
				}
				else if (!Runtime::CheckedMul(
						Counter, Expression.Multiplier, OutValue)
					|| !Runtime::CheckedAdd(OutValue, Expression.Addend, OutValue))
				{
					return false;
				}
				if (Expression.Modulus != 0)
				{
					OutValue = ApplyI32Remainder(OutValue, Expression.Modulus);
				}
				return true;
			};
			const auto TryDescribeCounterModulo =
				[&](const std::size_t Begin, const std::size_t End, std::uint16_t& OutCounterSlot, std::int32_t& OutModulus) -> bool
				{
					FCounterAffineI32 Expression;
					if (!TryDescribeCounterAffine(Begin, End, Expression)
						|| Expression.Multiplier != 1 || Expression.Addend != 0)
					{
						return false;
					}
					OutCounterSlot = Expression.CounterSlot;
					OutModulus = Expression.Modulus;
					return true;
				};
			const auto CanProveI32TransformSequenceNoThrow = [](
				const std::int32_t InitialValue,
				const std::size_t IterationCount,
				const std::int32_t* const Addends,
				const std::int32_t* const Moduli,
				const std::size_t TransformCount) noexcept
			{
				if (IterationCount == 0 || TransformCount == 0)
				{
					return true;
				}
				std::int32_t MinimumAddend = Addends[0];
				std::int32_t MaximumAddend = Addends[0];
				std::int64_t MaximumRemainderMagnitude = 0;
				bool bEveryTransformReduces = true;
				bool bHasNegativeOneModulus = false;
				for (std::size_t Index = 0; Index < TransformCount; ++Index)
				{
					std::int32_t FirstSum = 0;
					if (!Runtime::CheckedAdd(InitialValue, Addends[Index], FirstSum)
						|| (Moduli[Index] == -1
							&& FirstSum == std::numeric_limits<std::int32_t>::min()))
					{
						return false;
					}
					MinimumAddend = std::min(MinimumAddend, Addends[Index]);
					MaximumAddend = std::max(MaximumAddend, Addends[Index]);
					if (Moduli[Index] == 0)
					{
						bEveryTransformReduces = false;
						continue;
					}
					bHasNegativeOneModulus = bHasNegativeOneModulus
						|| Moduli[Index] == -1;
					const std::int64_t Magnitude = Moduli[Index] < 0
						? -static_cast<std::int64_t>(Moduli[Index])
						: static_cast<std::int64_t>(Moduli[Index]);
					MaximumRemainderMagnitude = std::max(
						MaximumRemainderMagnitude, Magnitude - 1);
				}
				if (bEveryTransformReduces)
				{
					const std::int64_t LowestSum =
						-MaximumRemainderMagnitude + MinimumAddend;
					const std::int64_t HighestSum =
						MaximumRemainderMagnitude + MaximumAddend;
					return LowestSum >= std::numeric_limits<std::int32_t>::min()
						&& HighestSum <= std::numeric_limits<std::int32_t>::max()
						&& (!bHasNegativeOneModulus
							|| LowestSum > std::numeric_limits<std::int32_t>::min());
				}
				// A zero summary modulus means that method returns the checked sum as-is.
				// Bound every possible receiver sequence conservatively. Non-zero modulo
				// operations only reduce magnitude and therefore cannot invalidate it.
				const std::int64_t Count = static_cast<std::int64_t>(IterationCount);
				const std::int64_t Lowest = std::min<std::int64_t>(InitialValue, 0)
					+ Count * std::min<std::int64_t>(MinimumAddend, 0);
				const std::int64_t Highest = std::max<std::int64_t>(InitialValue, 0)
					+ Count * std::max<std::int64_t>(MaximumAddend, 0);
				return Lowest >= std::numeric_limits<std::int32_t>::min()
					&& Highest <= std::numeric_limits<std::int32_t>::max()
					&& (!bHasNegativeOneModulus
						|| Lowest > std::numeric_limits<std::int32_t>::min());
			};
			const auto TryDescribeCounterAddImmediate =
				[&](const std::size_t Begin, const std::size_t End,
					std::uint16_t& OutCounterSlot,
					std::int32_t& OutAddend) -> bool
				{
					OutCounterSlot = 0xFFFF;
					OutAddend = 0;
					constexpr std::size_t ExpressionSize = 3u + 3u + 1u;
					if (Begin > End || ExpressionSize != End - Begin
						|| static_cast<EOpcode>(Code[Begin]) != EOpcode::LoadLocal
						|| static_cast<EOpcode>(Code[Begin + 3]) != EOpcode::ConstI32
						|| static_cast<EOpcode>(Code[Begin + 6]) != EOpcode::Add)
					{
						return false;
					}
					const std::uint16_t ConstantIndex =
						Bytecode::ReadU16LE(Code.data() + Begin + 4);
					if (ConstantIndex >= Module->Constants.size()
						|| Module->Constants[ConstantIndex].Kind != EConstantKind::I32)
					{
						return false;
					}
					OutCounterSlot = Bytecode::ReadU16LE(Code.data() + Begin + 1);
					OutAddend = Module->Constants[ConstantIndex].I32;
					return true;
				};
			const auto TryDescribeCounterAddLocal =
				[&](const std::size_t Begin, const std::size_t End,
					std::uint16_t& OutCounterSlot,
					std::uint16_t& OutAddendSlot) -> bool
				{
					OutCounterSlot = 0xFFFF;
					OutAddendSlot = 0xFFFF;
					constexpr std::size_t ExpressionSize = 3u + 3u + 1u;
					if (Begin > End || ExpressionSize != End - Begin
						|| static_cast<EOpcode>(Code[Begin]) != EOpcode::LoadLocal
						|| static_cast<EOpcode>(Code[Begin + 3]) != EOpcode::LoadLocal
						|| static_cast<EOpcode>(Code[Begin + 6]) != EOpcode::Add)
					{
						return false;
					}
					OutCounterSlot = Bytecode::ReadU16LE(Code.data() + Begin + 1);
					OutAddendSlot = Bytecode::ReadU16LE(Code.data() + Begin + 4);
					return OutCounterSlot != OutAddendSlot;
				};
			const auto TryMatchCanonicalI32Loop =
				[&](
					const std::size_t BodyStart,
					const std::size_t TailStart,
					const std::uint16_t ExpectedCounterSlot,
					FCanonicalI32Loop& OutLoop) -> bool
				{
					OutLoop = {};
					const std::size_t IncSize = Bytecode::InstructionSize(EOpcode::IncLocal);
					const std::size_t JmpSize = Bytecode::InstructionSize(EOpcode::Jmp);
					if (ExpectedCounterSlot == 0xFFFF || TailStart > CodeSize)
					{
						return false;
					}
					std::size_t BackJumpIp = 0;
					if (IncSize + JmpSize <= CodeSize - TailStart
						&& static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal
						&& Bytecode::ReadU16LE(Code.data() + TailStart + 1) == ExpectedCounterSlot
						&& static_cast<EOpcode>(Code[TailStart + IncSize]) == EOpcode::Jmp)
					{
						BackJumpIp = TailStart + IncSize;
					}
					else
					{
						// The unoptimized postfix form preserves the old counter in a
						// compiler temporary, then discards that expression result. Accept
						// only the exact equivalent of `counter++`; all aliases and constants
						// are verified before the loop may be collapsed.
						constexpr std::size_t VerboseTailSize = 3u + 3u + 3u + 3u + 1u
							+ 3u + 3u + 1u + 5u;
						if (VerboseTailSize > CodeSize - TailStart
							|| static_cast<EOpcode>(Code[TailStart]) != EOpcode::LoadLocal
							|| Bytecode::ReadU16LE(Code.data() + TailStart + 1) != ExpectedCounterSlot
							|| static_cast<EOpcode>(Code[TailStart + 3]) != EOpcode::StoreLocal)
						{
							return false;
						}
						const std::uint16_t TemporarySlot =
							Bytecode::ReadU16LE(Code.data() + TailStart + 4);
						const std::size_t LoadTemporaryIp = TailStart + 6;
						const std::size_t ConstantIp = TailStart + 9;
						const std::size_t AddIp = TailStart + 12;
						const std::size_t StoreCounterIp = TailStart + 13;
						const std::size_t DiscardLoadIp = TailStart + 16;
						const std::size_t PopIp = TailStart + 19;
						BackJumpIp = TailStart + 20;
						const std::uint16_t ConstantIndex =
							Bytecode::ReadU16LE(Code.data() + ConstantIp + 1);
						if (TemporarySlot == ExpectedCounterSlot
							|| static_cast<EOpcode>(Code[LoadTemporaryIp]) != EOpcode::LoadLocal
							|| Bytecode::ReadU16LE(Code.data() + LoadTemporaryIp + 1) != TemporarySlot
							|| static_cast<EOpcode>(Code[ConstantIp]) != EOpcode::ConstI32
							|| ConstantIndex >= Module->Constants.size()
							|| Module->Constants[ConstantIndex].Kind != EConstantKind::I32
							|| Module->Constants[ConstantIndex].I32 != 1
							|| static_cast<EOpcode>(Code[AddIp]) != EOpcode::Add
							|| static_cast<EOpcode>(Code[StoreCounterIp]) != EOpcode::StoreLocal
							|| Bytecode::ReadU16LE(Code.data() + StoreCounterIp + 1) != ExpectedCounterSlot
							|| static_cast<EOpcode>(Code[DiscardLoadIp]) != EOpcode::LoadLocal
							|| Bytecode::ReadU16LE(Code.data() + DiscardLoadIp + 1) != TemporarySlot
							|| static_cast<EOpcode>(Code[PopIp]) != EOpcode::Pop
							|| static_cast<EOpcode>(Code[BackJumpIp]) != EOpcode::Jmp)
						{
							return false;
						}
					}
					const std::size_t BackJumpEnd = BackJumpIp + JmpSize;
					std::size_t HeaderIp = 0;
					if (!IsJumpTargetInRange(BackJumpEnd, Bytecode::ReadI32LE(Code.data() + BackJumpIp + 1), CodeSize, HeaderIp))
					{
						return false;
					}
					FValue* const CounterLocal = BorrowLocal(ExpectedCounterSlot);
					auto* const Counter = CounterLocal
						? std::get_if<std::int32_t>(CounterLocal)
						: nullptr;
					if (!Counter)
					{
						return false;
					}
					const std::size_t ImmediateHeaderSize =
						Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm);
					if (HeaderIp <= CodeSize
						&& ImmediateHeaderSize <= CodeSize - HeaderIp
						&& static_cast<EOpcode>(Code[HeaderIp]) == EOpcode::JmpIfLocalGeImm
						&& Bytecode::ReadU16LE(Code.data() + HeaderIp + 1) == ExpectedCounterSlot
						&& HeaderIp + ImmediateHeaderSize == BodyStart)
					{
						const std::int32_t Limit = Bytecode::ReadI32LE(Code.data() + HeaderIp + 3);
						std::size_t ExitIp = 0;
						if (*Counter >= Limit
							|| !IsJumpTargetInRange(
								HeaderIp + ImmediateHeaderSize,
								Bytecode::ReadI32LE(Code.data() + HeaderIp + 7),
								CodeSize, ExitIp)
							|| ExitIp != BackJumpEnd)
						{
							return false;
						}
						OutLoop.Counter = Counter;
						OutLoop.Limit = Limit;
						OutLoop.ExitIp = ExitIp;
						OutLoop.CounterSlot = ExpectedCounterSlot;
						return true;
					}
					constexpr std::size_t HeaderSize = 3u + 3u + 1u + 5u;
					if (HeaderIp > CodeSize || HeaderSize > CodeSize - HeaderIp
						|| static_cast<EOpcode>(Code[HeaderIp]) != EOpcode::LoadLocal
						|| Bytecode::ReadU16LE(Code.data() + HeaderIp + 1) != ExpectedCounterSlot)
					{
						return false;
					}
					const EOpcode LimitOp = static_cast<EOpcode>(Code[HeaderIp + 3]);
					if ((LimitOp != EOpcode::LoadLocal && LimitOp != EOpcode::ConstI32)
						|| static_cast<EOpcode>(Code[HeaderIp + 6]) != EOpcode::Lt
						|| static_cast<EOpcode>(Code[HeaderIp + 7]) != EOpcode::JmpIfFalse
						|| HeaderIp + HeaderSize != BodyStart)
					{
						return false;
					}
					std::size_t ExitIp = 0;
					if (!IsJumpTargetInRange(HeaderIp + HeaderSize, Bytecode::ReadI32LE(Code.data() + HeaderIp + 8), CodeSize, ExitIp) || ExitIp != BackJumpEnd)
					{
						return false;
					}
					const std::uint16_t LimitOperand = Bytecode::ReadU16LE(Code.data() + HeaderIp + 4);
					std::int32_t Limit = 0;
					std::uint16_t LimitSlot = 0xFFFF;
					if (LimitOp == EOpcode::LoadLocal)
					{
						LimitSlot = LimitOperand;
						FValue* const LimitLocal = BorrowLocal(LimitSlot);
						const auto* const LocalLimit = LimitLocal
							? std::get_if<std::int32_t>(LimitLocal)
							: nullptr;
						if (!LocalLimit)
						{
							return false;
						}
						Limit = *LocalLimit;
					}
					else
					{
						if (LimitOperand >= Module->Constants.size()
							|| Module->Constants[LimitOperand].Kind != EConstantKind::I32)
						{
							return false;
						}
						Limit = Module->Constants[LimitOperand].I32;
					}
					if (!Counter || *Counter >= Limit)
					{
						return false;
					}
					OutLoop.Counter = Counter;
					OutLoop.Limit = Limit;
					OutLoop.ExitIp = ExitIp;
					OutLoop.CounterSlot = ExpectedCounterSlot;
					OutLoop.LimitSlot = LimitSlot;
					return true;
				};
			const auto LoopDoesNotAliasMutatedLocal = [](
				const FCanonicalI32Loop& Loop, const std::uint16_t MutatedSlot) noexcept
			{
				return MutatedSlot != Loop.CounterSlot && MutatedSlot != Loop.LimitSlot;
			};
			const auto LocalIsNotMutatedInRange = [&] (
				const std::size_t Begin,
				const std::size_t End,
				const std::uint16_t Slot) -> bool
			{
				if (Slot == 0xFFFF || Begin > End || End > CodeSize)
				{
					return false;
				}
				for (std::size_t ScanIp = Begin; ScanIp < End;)
				{
					const EOpcode ScanOp = static_cast<EOpcode>(Code[ScanIp]);
					const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
					if (ScanSize == 0 || ScanSize > End - ScanIp)
					{
						return false;
					}
					if ((ScanOp == EOpcode::StoreLocal || ScanOp == EOpcode::BindLocal
						|| ScanOp == EOpcode::IncLocal || ScanOp == EOpcode::AddLocalImm
						|| ScanOp == EOpcode::MakeLocalRef)
						&& Bytecode::ReadU16LE(Code.data() + ScanIp + 1) == Slot)
					{
						return false;
					}
					if (ScanOp == EOpcode::AddLocalLocal
						&& Bytecode::ReadU16LE(Code.data() + ScanIp + 1) == Slot)
					{
						return false;
					}
					ScanIp += ScanSize;
				}
				return true;
			};
			const auto TryRunI32CatchAccumulatorLoop = [&] (
				const std::size_t ThrowIp,
				const std::size_t HandlerOffset,
				std::size_t& OutExitIp,
				bool& bOutHandled) -> bool
			{
				bOutHandled = false;
				OutExitIp = HandlerOffset;
				const std::size_t LoadSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
				const std::size_t StoreSize = Bytecode::InstructionSize(EOpcode::StoreLocal);
				const std::size_t AddSize = Bytecode::InstructionSize(EOpcode::Add);
				const std::size_t JmpSize = Bytecode::InstructionSize(EOpcode::Jmp);
				if (ThrowIp < LoadSize
					|| static_cast<EOpcode>(Code[ThrowIp - LoadSize]) != EOpcode::LoadLocal
					|| HandlerOffset > CodeSize
					|| LoadSize * 2u + AddSize + StoreSize + JmpSize
						> CodeSize - HandlerOffset)
				{
					return true;
				}
				const std::uint16_t CounterSlot =
					Bytecode::ReadU16LE(Code.data() + ThrowIp - LoadSize + 1);
				const FBytecodeCatchHandler* MatchedHandler = nullptr;
				for (const FBytecodeExceptionRegion& Region : Function.ExceptionRegions)
				{
					if (ThrowIp < Region.BeginOffset || ThrowIp >= Region.EndOffset)
					{
						continue;
					}
					for (const FBytecodeCatchHandler& Handler : Region.Catches)
					{
						if (Handler.HandlerOffset == HandlerOffset)
						{
							MatchedHandler = &Handler;
							break;
						}
					}
					if (MatchedHandler)
					{
						break;
					}
				}
				if (!MatchedHandler || (MatchedHandler->TypeName != "int"
						&& MatchedHandler->TypeName != "int32"))
				{
					return true;
				}
				std::size_t ScanIp = HandlerOffset;
				if (static_cast<EOpcode>(Code[ScanIp]) != EOpcode::LoadLocal)
				{
					return true;
				}
				const std::uint16_t AccumulatorSlot =
					Bytecode::ReadU16LE(Code.data() + ScanIp + 1);
				ScanIp += LoadSize;
				if (static_cast<EOpcode>(Code[ScanIp]) != EOpcode::LoadLocal
					|| Bytecode::ReadU16LE(Code.data() + ScanIp + 1)
						!= MatchedHandler->LocalSlot)
				{
					return true;
				}
				ScanIp += LoadSize;
				const std::size_t AddIp = ScanIp;
				if (static_cast<EOpcode>(Code[ScanIp]) != EOpcode::Add)
				{
					return true;
				}
				ScanIp += AddSize;
				if (static_cast<EOpcode>(Code[ScanIp]) != EOpcode::StoreLocal
					|| Bytecode::ReadU16LE(Code.data() + ScanIp + 1) != AccumulatorSlot)
				{
					return true;
				}
				ScanIp += StoreSize;
				// Compound assignment is an expression. In statement position the
				// compiler may still materialize and discard its result.
				if (LoadSize + Bytecode::InstructionSize(EOpcode::Pop)
					<= CodeSize - ScanIp
					&& static_cast<EOpcode>(Code[ScanIp]) == EOpcode::LoadLocal
					&& Bytecode::ReadU16LE(Code.data() + ScanIp + 1) == AccumulatorSlot
					&& static_cast<EOpcode>(Code[ScanIp + LoadSize]) == EOpcode::Pop)
				{
					ScanIp += LoadSize + Bytecode::InstructionSize(EOpcode::Pop);
				}
				if (JmpSize > CodeSize - ScanIp
					|| static_cast<EOpcode>(Code[ScanIp]) != EOpcode::Jmp)
				{
					return true;
				}
				std::size_t TailStart = 0;
				if (!IsJumpTargetInRange(
						ScanIp + JmpSize,
						Bytecode::ReadI32LE(Code.data() + ScanIp + 1),
						CodeSize,
						TailStart))
				{
					return true;
				}
				FCanonicalI32Loop Loop;
				if (!TryMatchCanonicalI32Loop(
						ThrowIp - LoadSize, TailStart, CounterSlot, Loop)
					|| !LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot))
				{
					return true;
				}
				auto* const Accumulator = std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot));
				auto* const CatchLocal = std::get_if<std::int32_t>(
					BorrowLocal(MatchedHandler->LocalSlot));
				auto* const RethrowLocal = std::get_if<std::int32_t>(
					BorrowLocal(MatchedHandler->RethrowSlot));
				if (!Accumulator || !CatchLocal || !RethrowLocal || !Loop.Counter)
				{
					return true;
				}
				const std::int32_t First = *Loop.Counter;
				for (std::int64_t Current = First; Current < Loop.Limit; ++Current)
				{
					std::int32_t Sum = 0;
					if (!Runtime::CheckedAdd(
							*Accumulator, static_cast<std::int32_t>(Current), Sum))
					{
						*Loop.Counter = static_cast<std::int32_t>(Current);
						*CatchLocal = *Loop.Counter;
						*RethrowLocal = *Loop.Counter;
						return ErrorAt(AddIp, "integer overflow");
					}
					*Accumulator = Sum;
				}
				if (Loop.Limit > First)
				{
					*CatchLocal = Loop.Limit - 1;
					*RethrowLocal = Loop.Limit - 1;
				}
				*Loop.Counter = Loop.Limit;
				OutExitIp = Loop.ExitIp;
				bOutHandled = true;
				return true;
			};
			auto& ArgScratch = LinearScratchState->ArgumentScratch;
			auto ReleaseTemporary = [&](FValue& Value) -> bool
			{
				FValue Owned = std::move(Value);
				Value = std::int32_t{0};
				return Vm->JitReleaseOwnedValue(Owned);
			};
			auto ReleaseCollectedArgs = [&](const std::uint8_t N) -> bool
			{
				FValue* const Values = N <= 8 ? ArgScratch.data() : ArgHeap.data();
				bool bOk = true;
				for (std::uint8_t I = N; I > 0; --I)
				{
					// Release in reverse evaluation order, matching frame-local teardown.
					// Raw FValue destruction is insufficient for script objects because
					// their language destructor is driven by ReleaseValue.
					bOk = ReleaseTemporary(Values[I - 1]) && bOk;
				}
				if (N > 8)
				{
					ArgHeap.clear();
				}
				return bOk;
			};
			auto DropCollectedArgs = [&](const std::uint8_t N)
			{
				FValue* const Values = N <= 8 ? ArgScratch.data() : ArgHeap.data();
				for (std::uint8_t I = N; I > 0; --I)
				{
					// Dynamic/container CallMethod parameters are borrowed by VM
					// convention (for example array.push(structValue)); dropping the
					// transport copy must not run a second language destructor.
					Values[I - 1] = std::int32_t{0};
				}
				if (N > 8)
				{
					ArgHeap.clear();
				}
			};
			auto CollectArgs = [&](const std::uint8_t N, FValue*& OutArgs) -> bool
			{
				if (Stack.size() < N)
				{
					return false;
				}
				if (N <= 8)
				{
					for (std::uint8_t I = N; I > 0; --I)
					{
						ArgScratch[I - 1] = std::move(Stack.back());
						Stack.pop_back();
					}
					OutArgs = ArgScratch.data();
					return true;
				}
				if (!bArgHeapLeased)
				{
					Vm->JitAcquireLinearArgumentScratch(N, ArgHeap);
					bArgHeapLeased = true;
				}
				ArgHeap.resize(N);
				for (std::uint8_t I = N; I > 0; --I)
				{
					ArgHeap[I - 1] = std::move(Stack.back());
					Stack.pop_back();
				}
				OutArgs = ArgHeap.data();
				return true;
			};
			bool bRuntimeArrayPlanDeclined = false;
			while (Ip < CodeSize)
			{
				if (Vm->WasExitRequested())
				{
					return LeaveFrame() && !Vm->HasError();
				}
				if (Budget-- == 0)
				{
					return Error("JIT linear: step budget exceeded");
				}
				if (TypedLoopPlans && Ip < TypedLoopPlans->PlanAtOffset.size()
					&& TypedLoopPlans->PlanAtOffset[Ip] != 0)
				{
					const auto PlanIndex = TypedLoopPlans->PlanAtOffset[Ip] - 1u;
					const bool bRuntimeBoundArrayPush = PlanIndex < TypedLoopPlans->Plans.size() &&
					    TypedLoopPlans->Plans[PlanIndex].bRuntimeBoundArrayPush;
					if (!bRuntimeBoundArrayPush || !bRuntimeArrayPlanDeclined)
					{
						const FTypedLoopExecutionResult TypedResult =
							ExecuteTypedLoopPlan(*Vm, *TypedLoopPlans, Ip);
						if (TypedResult.Status == ETypedLoopExecution::Failed)
						{
							return FailVmAt(TypedResult.ErrorBytecodeOffset);
						}
						if (TypedResult.Status == ETypedLoopExecution::Completed)
						{
							Ip = TypedResult.NextBytecodeOffset;
							continue;
						}
						bRuntimeArrayPlanDeclined |= bRuntimeBoundArrayPush;
					}
				}
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t AfterIp = Ip + Bytecode::InstructionSize(Op);
				// Whole-loop scalar replacement for a state-driven, non-escaping trivial
				// object projection.  The older per-iteration path below removes allocation
				// but still repeats bytecode-shape, method and ownership analysis on every
				// iteration.  Here the complete counted loop is proven once, scalar field
				// expressions are evaluated directly, and no object/ref-count/destructor
				// traffic is materialized.  Unsupported expressions or observable identity
				// retain the authoritative per-iteration/VM path.
				if (Op == EOpcode::LoadLocal
					&& bMayHaveAssignedObjectProjectionLoop
					&& Vm->JitCanUseDirectScalarMethod()
					&& !Vm->JitHasInstanceCallInterceptor()
					&& !Vm->JitHasMutationCallback())
				{
					const auto ReadI32Constant = [&](const std::size_t ConstantIp,
						std::int32_t& Out) -> bool
					{
						if (ConstantIp + Bytecode::InstructionSize(EOpcode::ConstI32)
								> CodeSize
							|| static_cast<EOpcode>(Code[ConstantIp]) != EOpcode::ConstI32)
						{
							return false;
						}
						const std::uint16_t Index =
							Bytecode::ReadU16LE(Code.data() + ConstantIp + 1u);
						if (Index >= Module->Constants.size())
						{
							return false;
						}
						const FBytecodeConstant& Constant = Module->Constants[Index];
						switch (Constant.Kind)
						{
						case EConstantKind::I8:
							{
								Out = Constant.I8;
								return true;
							}
						case EConstantKind::U8:
							{
								Out = Constant.U8;
								return true;
							}
						case EConstantKind::I16:
							{
								Out = Constant.I16;
								return true;
							}
						case EConstantKind::U16:
							{
								Out = Constant.U16;
								return true;
							}
						case EConstantKind::I32:
							{
								Out = Constant.I32;
								return true;
							}
						default:
							return false;
						}
					};
					const std::size_t MulIp = AfterIp
						+ Bytecode::InstructionSize(EOpcode::ConstI32);
					const std::size_t IncrementIp = MulIp
						+ Bytecode::InstructionSize(EOpcode::Mul);
					const std::size_t AddIp = IncrementIp
						+ Bytecode::InstructionSize(EOpcode::ConstI32);
					const std::size_t StateModulusIp = AddIp
						+ Bytecode::InstructionSize(EOpcode::Add);
					const std::size_t StateModIp = StateModulusIp
						+ Bytecode::InstructionSize(EOpcode::ConstI32);
					std::size_t StateStoreIp = StateModIp
						+ Bytecode::InstructionSize(EOpcode::Mod);
					if (StateStoreIp < CodeSize
						&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::Nop)
					{
						StateStoreIp += Bytecode::InstructionSize(EOpcode::Nop);
					}
					const std::size_t ObjectIp = StateStoreIp
						+ Bytecode::InstructionSize(EOpcode::StoreLocal);
					std::int32_t StateMultiplier = 0;
					std::int32_t StateIncrement = 0;
					std::int32_t StateModulus = 0;
					bool bScalarObjectLoop = ObjectIp
						+ Bytecode::InstructionSize(EOpcode::NewObject)
						+ Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize
						&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32
						&& static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul
						&& static_cast<EOpcode>(Code[IncrementIp]) == EOpcode::ConstI32
						&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
						&& static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32
						&& static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod
						&& static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal
						&& Bytecode::ReadU16LE(Code.data() + Ip + 1u)
							== Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u)
						&& static_cast<EOpcode>(Code[ObjectIp]) == EOpcode::NewObject
						&& static_cast<EOpcode>(Code[ObjectIp
							+ Bytecode::InstructionSize(EOpcode::NewObject)])
							== EOpcode::StoreLocal
						&& ReadI32Constant(AfterIp, StateMultiplier)
						&& ReadI32Constant(IncrementIp, StateIncrement)
						&& ReadI32Constant(StateModulusIp, StateModulus);
					if (bScalarObjectLoop)
					{
						const std::uint16_t StateSlot =
							Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						const std::uint16_t TypeIndex =
							Bytecode::ReadU16LE(Code.data() + ObjectIp + 1u);
						const std::size_t ObjectStoreIp = ObjectIp
							+ Bytecode::InstructionSize(EOpcode::NewObject);
						const std::uint16_t ObjectSlot =
							Bytecode::ReadU16LE(Code.data() + ObjectStoreIp + 1u);
						struct FScalarExpressionInstruction
						{
							EOpcode Opcode = EOpcode::Nop;
							std::int32_t Immediate = 0;
							std::uint16_t LocalSlot = 0xFFFFu;
							std::int32_t* Local = nullptr;
							std::size_t SourceIp = 0;
							std::int32_t InvariantDivisor = 0;
							std::uint64_t Reciprocal = 0;
							std::uint32_t Mask = 0;
							bool bPowerOfTwo = false;
							bool bHasInvariantDivisor = false;
						};
						// This cold proof workspace is substantially larger than the hot
						// dispatcher state. Keeping it in the monolithic trampoline's native
						// stack frame charged every Linear call (and every recursive frame)
						// even when no object-projection pattern was present. Allocate it only
						// after the structural prefix has matched.
						auto ExpressionPlans = std::make_unique<
							std::array<FScalarExpressionInstruction, 32>[]>(8);
						std::array<std::uint8_t, 8> ExpressionCounts{};
						std::array<std::uint16_t, 8> AssignedNames{};
						std::array<std::int32_t, 8> AssignedValues{};
						std::size_t AssignmentCount = 0;
						std::size_t Cursor = ObjectStoreIp
							+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						const auto IsScalarExpressionOpcode = [](const EOpcode Candidate)
						{
							switch (Candidate)
							{
							case EOpcode::ConstI32:
							case EOpcode::LoadLocal:
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
							case EOpcode::Neg:
							case EOpcode::BitNot:
								return true;
							default:
								return false;
							}
						};
						while (AssignmentCount < AssignedNames.size()
							&& Cursor + Bytecode::InstructionSize(EOpcode::LoadLocal)
								<= CodeSize
							&& static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + Cursor + 1u) == ObjectSlot)
						{
							const std::size_t ExpressionBegin = Cursor
								+ Bytecode::InstructionSize(EOpcode::LoadLocal);
							std::size_t ScanIp = ExpressionBegin;
							std::size_t OperationCount = 0;
							while (ScanIp < CodeSize
								&& static_cast<EOpcode>(Code[ScanIp]) != EOpcode::StoreField
								&& OperationCount++ < 32u)
							{
								const EOpcode ExpressionOp = static_cast<EOpcode>(Code[ScanIp]);
								const std::size_t ExpressionSize =
									Bytecode::InstructionSize(ExpressionOp);
								if (!IsScalarExpressionOpcode(ExpressionOp)
									|| ExpressionSize == 0 || ExpressionSize > CodeSize - ScanIp)
								{
									ScanIp = CodeSize;
									break;
								}
								FScalarExpressionInstruction& ExpressionInstruction =
									ExpressionPlans[AssignmentCount][OperationCount - 1u];
								ExpressionInstruction.Opcode = ExpressionOp;
								ExpressionInstruction.SourceIp = ScanIp;
								if (ExpressionOp == EOpcode::ConstI32)
								{
									if (!ReadI32Constant(ScanIp, ExpressionInstruction.Immediate))
									{
										ScanIp = CodeSize;
										break;
									}
								}
								else if (ExpressionOp == EOpcode::LoadLocal)
								{
									ExpressionInstruction.LocalSlot =
										Bytecode::ReadU16LE(Code.data() + ScanIp + 1u);
								}
								else if ((ExpressionOp == EOpcode::Div
										|| ExpressionOp == EOpcode::Mod)
									&& OperationCount >= 2u)
								{
									const FScalarExpressionInstruction& Right =
										ExpressionPlans[AssignmentCount][OperationCount - 2u];
									if (Right.Opcode == EOpcode::ConstI32 && Right.Immediate > 0)
									{
										const std::uint32_t Divisor =
											static_cast<std::uint32_t>(Right.Immediate);
										ExpressionInstruction.InvariantDivisor = Right.Immediate;
										ExpressionInstruction.Reciprocal =
											(std::uint64_t{1} << 32u) / Divisor;
										ExpressionInstruction.Mask = Divisor - 1u;
										ExpressionInstruction.bPowerOfTwo =
											(Divisor & (Divisor - 1u)) == 0u;
										ExpressionInstruction.bHasInvariantDivisor = true;
									}
								}
								ScanIp += ExpressionSize;
							}
							if (ScanIp + Bytecode::InstructionSize(EOpcode::StoreField)
									+ Bytecode::InstructionSize(EOpcode::Pop) > CodeSize
								|| static_cast<EOpcode>(Code[ScanIp]) != EOpcode::StoreField
								|| static_cast<EOpcode>(Code[ScanIp
									+ Bytecode::InstructionSize(EOpcode::StoreField)]) != EOpcode::Pop)
							{
								break;
							}
							ExpressionCounts[AssignmentCount] =
								static_cast<std::uint8_t>(OperationCount);
							AssignedNames[AssignmentCount] =
								Bytecode::ReadU16LE(Code.data() + ScanIp + 1u);
							++AssignmentCount;
							Cursor = ScanIp + Bytecode::InstructionSize(EOpcode::StoreField)
								+ Bytecode::InstructionSize(EOpcode::Pop);
						}
						const std::size_t AccumulatorLoadIp = Cursor;
						const std::size_t ReceiverLoadIp = AccumulatorLoadIp
							+ Bytecode::InstructionSize(EOpcode::LoadLocal);
						std::size_t ProjectionCallIp = ReceiverLoadIp
							+ Bytecode::InstructionSize(EOpcode::LoadLocal);
						std::uint16_t ProjectionArgumentSlot = 0xFFFFu;
						if (ProjectionCallIp < CodeSize
							&& static_cast<EOpcode>(Code[ProjectionCallIp]) == EOpcode::LoadLocal)
						{
							ProjectionArgumentSlot = Bytecode::ReadU16LE(
								Code.data() + ProjectionCallIp + 1u);
							ProjectionCallIp += Bytecode::InstructionSize(EOpcode::LoadLocal);
						}
						const std::uint8_t ProjectionArgumentCount =
							ProjectionArgumentSlot == 0xFFFFu ? 0u : 1u;
						const std::size_t AccumulatorAddIp = ProjectionCallIp
							+ Bytecode::InstructionSize(EOpcode::CallBoundMethod);
						std::size_t AccumulatorStoreIp = AccumulatorAddIp
							+ Bytecode::InstructionSize(EOpcode::Add);
						std::int32_t AccumulatorModulus = 0;
						std::size_t AccumulatorModIp = AccumulatorStoreIp;
						if (AccumulatorStoreIp < CodeSize
							&& static_cast<EOpcode>(Code[AccumulatorStoreIp]) == EOpcode::ConstI32
							&& ReadI32Constant(AccumulatorStoreIp, AccumulatorModulus))
						{
							AccumulatorModIp = AccumulatorStoreIp
								+ Bytecode::InstructionSize(EOpcode::ConstI32);
							if (AccumulatorModIp < CodeSize
								&& static_cast<EOpcode>(Code[AccumulatorModIp]) == EOpcode::Mod)
							{
								AccumulatorStoreIp = AccumulatorModIp
									+ Bytecode::InstructionSize(EOpcode::Mod);
							}
							else
							{
								AccumulatorModulus = 0;
							}
						}
						const std::size_t TailIp = AccumulatorStoreIp
							+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						const std::uint16_t AccumulatorSlot = AccumulatorLoadIp + 3u <= CodeSize
							? Bytecode::ReadU16LE(Code.data() + AccumulatorLoadIp + 1u)
							: 0xFFFFu;
						const std::uint16_t CounterSlot = TailIp + 3u <= CodeSize
							&& static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal
							? Bytecode::ReadU16LE(Code.data() + TailIp + 1u)
							: 0xFFFFu;
						const std::uint16_t MethodIndex = ProjectionCallIp + 4u <= CodeSize
							? Bytecode::ReadU16LE(Code.data() + ProjectionCallIp + 1u)
							: 0xFFFFu;
						FCanonicalI32Loop Loop;
						bScalarObjectLoop = AssignmentCount != 0
							&& TypeIndex < Module->Types.size()
							&& MethodIndex < Module->Functions.size()
							&& AccumulatorStoreIp + 3u <= CodeSize
							&& static_cast<EOpcode>(Code[AccumulatorLoadIp]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[ReceiverLoadIp]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + ReceiverLoadIp + 1u)
								== ObjectSlot
							&& static_cast<EOpcode>(Code[ProjectionCallIp])
								== EOpcode::CallBoundMethod
							&& Code[ProjectionCallIp + 3u] == ProjectionArgumentCount
							&& static_cast<EOpcode>(Code[AccumulatorAddIp]) == EOpcode::Add
							&& static_cast<EOpcode>(Code[AccumulatorStoreIp]) == EOpcode::StoreLocal
							&& Bytecode::ReadU16LE(Code.data() + AccumulatorStoreIp + 1u)
								== AccumulatorSlot
							&& TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
							&& LoopDoesNotAliasMutatedLocal(Loop, StateSlot)
							&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot)
							&& LoopDoesNotAliasMutatedLocal(Loop, ObjectSlot);
						if (bScalarObjectLoop)
						{
							const FBytecodeTypeDesc& Type = Module->Types[TypeIndex];
							const FBytecodeFunction& Method = Module->Functions[MethodIndex];
							const bool bHasClassDefaults = std::any_of(
								Type.DeclMeta.begin(), Type.DeclMeta.end(),
								[](const auto& Entry)
								{
									return Entry.first == ClassDefaultsLinkageMetaKey;
								});
							const auto IsTrivialI32Field = [](const FBytecodeFieldDesc& Field)
							{
								if (!Field.bHasDefault || Field.bHasInitCode)
								{
									return false;
								}
								const EConstantKind Kind = Field.DefaultValue.Kind;
								return Kind == EConstantKind::I8 || Kind == EConstantKind::U8
									|| Kind == EConstantKind::I16 || Kind == EConstantKind::U16
									|| Kind == EConstantKind::I32;
							};
							bScalarObjectLoop = !Type.bIsAbstract && Type.BaseTypeIndices.empty()
								&& Type.HostBaseTypeName.empty()
								&& Type.DestructorFunctionIndex == 0xFFFFu && !bHasClassDefaults
								&& Type.Fields.size() <= AssignedNames.size()
								&& std::all_of(Type.Fields.begin(), Type.Fields.end(),
									IsTrivialI32Field)
								&& Method.bIsMethod && Method.OwnerTypeIndex == TypeIndex
								&& Method.ParamCount == ProjectionArgumentCount
								&& Method.LocalCount == Method.ParamCount
								&& !Method.bReturnsVoid && !Method.bReturnsRef
								&& !Method.bIsVirtual && !Method.bIsAbstract
								&& !Method.bDeleted && Method.ExceptionRegions.empty()
								&& !BytecodeFunctionHasRefParams(Method);

							// The object local must be wholly contained in the eliminated region.
							for (std::size_t ScanIp = 0;
								bScalarObjectLoop && ScanIp < CodeSize;)
							{
								if (ScanIp >= ObjectIp && ScanIp < TailIp)
								{
									ScanIp = TailIp;
									continue;
								}
								const EOpcode ScanOp = static_cast<EOpcode>(Code[ScanIp]);
								const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
								if (ScanSize == 0 || ScanSize > CodeSize - ScanIp)
								{
									bScalarObjectLoop = false;
									break;
								}
								if ((ScanOp == EOpcode::LoadLocal || ScanOp == EOpcode::StoreLocal
										|| ScanOp == EOpcode::MakeLocalRef || ScanOp == EOpcode::BindLocal)
									&& ScanSize >= 3u
									&& Bytecode::ReadU16LE(Code.data() + ScanIp + 1u) == ObjectSlot)
								{
									bScalarObjectLoop = false;
								}
								ScanIp += ScanSize;
							}

							// Validate the projection expression once. It may read assigned/default
							// i32 fields and apply checked i32 arithmetic before its sole return.
							std::size_t ProjectionReturnIp = 0;
							std::size_t ProjectionDepth = 0;
							for (std::size_t MethodIp = 0;
								bScalarObjectLoop && MethodIp < Method.Code.size();)
							{
								const EOpcode MethodOp = static_cast<EOpcode>(Method.Code[MethodIp]);
								const std::size_t MethodSize = Bytecode::InstructionSize(MethodOp);
								if (MethodSize == 0 || MethodSize > Method.Code.size() - MethodIp)
								{
									bScalarObjectLoop = false;
									break;
								}
								if (MethodOp == EOpcode::LoadThisField || MethodOp == EOpcode::ConstI32
									|| (MethodOp == EOpcode::LoadLocal
										&& Bytecode::ReadU16LE(Method.Code.data() + MethodIp + 1u)
											< ProjectionArgumentCount))
								{
									if (++ProjectionDepth > 16u)
									{
										bScalarObjectLoop = false;
									}
								}
								else if (MethodOp == EOpcode::Add || MethodOp == EOpcode::Sub
									|| MethodOp == EOpcode::Mul || MethodOp == EOpcode::Div
									|| MethodOp == EOpcode::Mod || MethodOp == EOpcode::BitAnd
									|| MethodOp == EOpcode::BitOr || MethodOp == EOpcode::BitXor
									|| MethodOp == EOpcode::Shl || MethodOp == EOpcode::Shr)
								{
									if (ProjectionDepth < 2u)
									{
										bScalarObjectLoop = false;
									}
									else
									{
										--ProjectionDepth;
									}
								}
								else if (MethodOp == EOpcode::Return)
								{
									ProjectionReturnIp = MethodIp;
									bScalarObjectLoop = ProjectionDepth == 1u
										&& MethodIp + MethodSize == Method.Code.size();
									break;
								}
								else
								{
									bScalarObjectLoop = false;
								}
								MethodIp += MethodSize;
							}
							bScalarObjectLoop = bScalarObjectLoop && ProjectionReturnIp != 0u;
							std::int32_t* const State = bScalarObjectLoop
								? std::get_if<std::int32_t>(BorrowLocal(StateSlot))
								: nullptr;
							std::int32_t* const Accumulator = bScalarObjectLoop
								? std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot))
								: nullptr;
							std::int32_t* const ProjectionArgument =
								bScalarObjectLoop && ProjectionArgumentCount != 0u
								? std::get_if<std::int32_t>(BorrowLocal(ProjectionArgumentSlot))
								: nullptr;
							bScalarObjectLoop = State && Accumulator
								&& (ProjectionArgumentCount == 0u || ProjectionArgument);
							for (std::size_t Assignment = 0;
								bScalarObjectLoop && Assignment < AssignmentCount; ++Assignment)
							{
								for (std::size_t Instruction = 0;
									Instruction < ExpressionCounts[Assignment]; ++Instruction)
								{
									FScalarExpressionInstruction& ExpressionInstruction =
										ExpressionPlans[Assignment][Instruction];
									if (ExpressionInstruction.Opcode != EOpcode::LoadLocal)
									{
										continue;
									}
									ExpressionInstruction.Local = std::get_if<std::int32_t>(
										BorrowLocal(ExpressionInstruction.LocalSlot));
									bScalarObjectLoop = ExpressionInstruction.Local != nullptr;
									if (!bScalarObjectLoop)
									{
										break;
									}
								}
							}
							if (bScalarObjectLoop)
							{
								std::int32_t StateValue = *State;
								std::int32_t AccumulatorValue = *Accumulator;
								std::int32_t CounterValue = *Loop.Counter;
								const auto StoreLoopState = [&]()
								{
									*State = StateValue;
									*Accumulator = AccumulatorValue;
									*Loop.Counter = CounterValue;
								};
								const auto CommitCompleted = [&](const std::size_t Count) -> bool
								{
									StoreLoopState();
									return Count == 0u || Vm->JitCommitDirectScalarMethod(
										EJitScalarValueKind::I32, Count);
								};
								const auto ReadScalarSource = [&](const std::int32_t* const Source)
								{
									if (Source == State)
									{
										return StateValue;
									}
									if (Source == Accumulator)
									{
										return AccumulatorValue;
									}
									if (Source == Loop.Counter)
									{
										return CounterValue;
									}
									return *Source;
								};
								struct FPreparedDivisor
								{
									std::int32_t Divisor = 0;
									std::uint64_t Reciprocal = 0;
									std::uint32_t Mask = 0;
									bool bPowerOfTwo = false;
								};
								const auto PrepareDivisor = [](const std::int32_t Divisor)
								{
									FPreparedDivisor Prepared;
									Prepared.Divisor = Divisor;
									if (Divisor > 0)
									{
										const std::uint32_t Value = static_cast<std::uint32_t>(Divisor);
										Prepared.Reciprocal = (std::uint64_t{1} << 32u) / Value;
										Prepared.Mask = Value - 1u;
										Prepared.bPowerOfTwo = (Value & (Value - 1u)) == 0u;
									}
									return Prepared;
								};
								const auto FastRemainder = [](const std::int32_t Left,
									const FPreparedDivisor& Prepared)
								{
									if (Left < 0 || Prepared.Divisor <= 0)
									{
										return Left % Prepared.Divisor;
									}
									const std::uint32_t Value = static_cast<std::uint32_t>(Left);
									if (Prepared.bPowerOfTwo)
									{
										return static_cast<std::int32_t>(Value & Prepared.Mask);
									}
									const std::uint32_t Divisor =
										static_cast<std::uint32_t>(Prepared.Divisor);
									const std::uint64_t Quotient =
										(std::uint64_t{Value} * Prepared.Reciprocal) >> 32u;
									std::uint32_t Remainder = Value - static_cast<std::uint32_t>(
										Quotient * Divisor);
									if (Remainder >= Divisor)
									{
										Remainder -= Divisor;
									}
									return static_cast<std::int32_t>(Remainder);
								};
								const auto FastQuotient = [](const std::int32_t Left,
									const FPreparedDivisor& Prepared)
								{
									if (Left < 0 || Prepared.Divisor <= 0)
									{
										return Left / Prepared.Divisor;
									}
									const std::uint32_t Value = static_cast<std::uint32_t>(Left);
									if (Prepared.bPowerOfTwo)
									{
										std::uint32_t Shift = 0;
										std::uint32_t Divisor =
											static_cast<std::uint32_t>(Prepared.Divisor);
										while (Divisor > 1u)
										{
											Divisor >>= 1u;
											++Shift;
										}
										return static_cast<std::int32_t>(Value >> Shift);
									}
									const std::uint32_t Divisor =
										static_cast<std::uint32_t>(Prepared.Divisor);
									std::uint64_t Quotient =
										(std::uint64_t{Value} * Prepared.Reciprocal) >> 32u;
									const std::uint32_t Remainder = Value
										- static_cast<std::uint32_t>(Quotient * Divisor);
									if (Remainder >= Divisor)
									{
										++Quotient;
									}
									return static_cast<std::int32_t>(Quotient);
								};
								enum class EFastScalarExpression : std::uint8_t
								{
									None = 0,
									Local,
									LocalDiv,
									LocalMod,
									LocalDivMod,
								};
								struct FFastScalarExpression
								{
									EFastScalarExpression Kind = EFastScalarExpression::None;
									std::int32_t* Source = nullptr;
									FPreparedDivisor First;
									FPreparedDivisor Second;
								};
								std::array<FFastScalarExpression, 8> FastExpressions{};
								for (std::size_t Assignment = 0;
									Assignment < AssignmentCount; ++Assignment)
								{
									const auto& Expression = ExpressionPlans[Assignment];
									auto& Fast = FastExpressions[Assignment];
									if (ExpressionCounts[Assignment] == 1u
										&& Expression[0].Opcode == EOpcode::LoadLocal)
									{
										Fast.Kind = EFastScalarExpression::Local;
										Fast.Source = Expression[0].Local;
									}
									else if (ExpressionCounts[Assignment] == 3u
										&& Expression[0].Opcode == EOpcode::LoadLocal
										&& Expression[1].Opcode == EOpcode::ConstI32
										&& (Expression[2].Opcode == EOpcode::Div
											|| Expression[2].Opcode == EOpcode::Mod)
										&& Expression[2].bHasInvariantDivisor)
									{
										Fast.Kind = Expression[2].Opcode == EOpcode::Div
											? EFastScalarExpression::LocalDiv
											: EFastScalarExpression::LocalMod;
										Fast.Source = Expression[0].Local;
										Fast.First = PrepareDivisor(Expression[1].Immediate);
									}
									else if (ExpressionCounts[Assignment] == 5u
										&& Expression[0].Opcode == EOpcode::LoadLocal
										&& Expression[1].Opcode == EOpcode::ConstI32
										&& Expression[2].Opcode == EOpcode::Div
										&& Expression[3].Opcode == EOpcode::ConstI32
										&& Expression[4].Opcode == EOpcode::Mod
										&& Expression[2].bHasInvariantDivisor
										&& Expression[4].bHasInvariantDivisor)
									{
										Fast.Kind = EFastScalarExpression::LocalDivMod;
										Fast.Source = Expression[0].Local;
										Fast.First = PrepareDivisor(Expression[1].Immediate);
										Fast.Second = PrepareDivisor(Expression[3].Immediate);
									}
								}
								const FPreparedDivisor PreparedStateModulus =
									PrepareDivisor(StateModulus);
								const FPreparedDivisor PreparedAccumulatorModulus =
									PrepareDivisor(AccumulatorModulus);
								const auto EvaluateExpression = [&](const std::size_t Assignment,
									std::int32_t& Out, std::size_t& OutErrorIp,
									const char*& OutError) -> bool
								{
									std::array<std::int32_t, 16> Values{};
									std::size_t Depth = 0;
									for (std::size_t Instruction = 0;
										Instruction < ExpressionCounts[Assignment]; ++Instruction)
									{
										const FScalarExpressionInstruction& Current =
											ExpressionPlans[Assignment][Instruction];
										OutErrorIp = Current.SourceIp;
										if (Current.Opcode == EOpcode::ConstI32)
										{
											if (Depth == Values.size())
											{
												return false;
											}
											Values[Depth++] = Current.Immediate;
											continue;
										}
										if (Current.Opcode == EOpcode::LoadLocal)
										{
											if (!Current.Local || Depth == Values.size())
											{
												return false;
											}
											Values[Depth++] = ReadScalarSource(Current.Local);
											continue;
										}
										if (Current.Opcode == EOpcode::Neg
											|| Current.Opcode == EOpcode::BitNot)
										{
											if (Depth == 0u)
											{
												return false;
											}
											if (Current.Opcode == EOpcode::Neg)
											{
												if (Values[Depth - 1u]
													== std::numeric_limits<std::int32_t>::min())
												{
													OutError = "integer overflow";
													return false;
												}
												Values[Depth - 1u] = -Values[Depth - 1u];
											}
											else
											{
												Values[Depth - 1u] = ~Values[Depth - 1u];
											}
											continue;
										}
										if (Depth < 2u)
										{
											return false;
										}
										const std::int32_t Right = Values[--Depth];
										const std::int32_t Left = Values[Depth - 1u];
										std::int32_t Value = 0;
										switch (Current.Opcode)
										{
										case EOpcode::Add:
											{
												if (!Runtime::CheckedAdd(Left, Right, Value))
												{
													OutError = "integer overflow";
												}
												break;
											}
										case EOpcode::Sub:
											{
												if (!Runtime::CheckedSub(Left, Right, Value))
												{
													OutError = "integer overflow";
												}
												break;
											}
										case EOpcode::Mul:
											{
												if (!Runtime::CheckedMul(Left, Right, Value))
												{
													OutError = "integer overflow";
												}
												break;
											}
										case EOpcode::Div:
										case EOpcode::Mod:
											{
												if (Current.bHasInvariantDivisor && Left >= 0
													&& Right == Current.InvariantDivisor)
												{
													const std::uint32_t UnsignedLeft =
														static_cast<std::uint32_t>(Left);
													if (Current.Opcode == EOpcode::Mod
														&& Current.bPowerOfTwo)
													{
														Value = static_cast<std::int32_t>(
															UnsignedLeft & Current.Mask);
														break;
													}
													const std::uint32_t Divisor =
														static_cast<std::uint32_t>(Right);
													std::uint64_t Quotient =
														(std::uint64_t{UnsignedLeft} * Current.Reciprocal) >> 32u;
													std::uint32_t Remainder = UnsignedLeft
														- static_cast<std::uint32_t>(Quotient * Divisor);
													if (Remainder >= Divisor)
													{
														Remainder -= Divisor;
														++Quotient;
													}
													Value = Current.Opcode == EOpcode::Div
														? static_cast<std::int32_t>(Quotient)
														: static_cast<std::int32_t>(Remainder);
												}
												else if (Right == 0)
												{
													OutError = "division by zero";
												}
												else if (Left == std::numeric_limits<std::int32_t>::min()
													&& Right == -1)
												{
													OutError = "integer overflow";
												}
												else
												{
													Value = Current.Opcode == EOpcode::Div
														? Left / Right
														: Left % Right;
												}
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

										case EOpcode::Shl:
										case EOpcode::Shr:
											{
												if (Right < 0 || Right >= 32)
												{
													OutError = "invalid shift count";
												}
												else
												{
													Value = Current.Opcode == EOpcode::Shl
													? static_cast<std::int32_t>(
														static_cast<std::uint32_t>(Left) << Right)
													: static_cast<std::int32_t>(Left >> Right);
												}
												break;
											}
										default:
											return false;
										}
										if (OutError)
										{
											return false;
										}
										Values[Depth - 1u] = Value;
									}
									if (Depth != 1u)
									{
										return false;
									}
									Out = Values[0];
									return true;
								};
								struct FFastProjectionOperand
								{
									std::int8_t Assignment = -1;
									std::int32_t DefaultValue = 0;
								};
								const auto ResolveProjectionOperand = [&](const std::uint16_t Name,
									FFastProjectionOperand& Out) -> bool
								{
									for (std::size_t Assignment = AssignmentCount;
										Assignment > 0u; --Assignment)
									{
										if (AssignedNames[Assignment - 1u] == Name)
										{
											Out.Assignment = static_cast<std::int8_t>(Assignment - 1u);
											return true;
										}
									}
									if (Name >= Module->StringPool.size())
									{
										return false;
									}
									const std::string& FieldName = Module->StringPool[Name];
									for (const FBytecodeFieldDesc& Field : Type.Fields)
									{
										if (Field.Name != FieldName)
										{
											continue;
										}
										const FValue Default = JitHost::Services().ConstantToValue(
											*Module, Field.DefaultValue);
										const auto* const Value = std::get_if<std::int32_t>(&Default);
										if (!Value)
										{
											return false;
										}
										Out.DefaultValue = *Value;
										return true;
									}
									return false;
								};
								FFastProjectionOperand FastProjectionLeft;
								FFastProjectionOperand FastProjectionRight;
								EOpcode FastProjectionOpcode = EOpcode::Nop;
								const std::size_t ProjectionSecondIp =
									Bytecode::InstructionSize(EOpcode::LoadThisField);
								const std::size_t ProjectionOperatorIp = ProjectionSecondIp
									+ Bytecode::InstructionSize(EOpcode::LoadThisField);
								const std::size_t ProjectionFastReturnIp = ProjectionOperatorIp + 1u;
								bool bFastBinaryProjection = Method.Code.size()
									== ProjectionFastReturnIp + Bytecode::InstructionSize(EOpcode::Return)
									&& ProjectionReturnIp == ProjectionFastReturnIp
									&& static_cast<EOpcode>(Method.Code[0]) == EOpcode::LoadThisField
									&& static_cast<EOpcode>(Method.Code[ProjectionSecondIp])
										== EOpcode::LoadThisField
									&& static_cast<EOpcode>(Method.Code[ProjectionFastReturnIp])
										== EOpcode::Return;
								if (bFastBinaryProjection)
								{
									FastProjectionOpcode = static_cast<EOpcode>(
										Method.Code[ProjectionOperatorIp]);
									bFastBinaryProjection =
										(FastProjectionOpcode == EOpcode::Add
											|| FastProjectionOpcode == EOpcode::Sub
											|| FastProjectionOpcode == EOpcode::Mul
											|| FastProjectionOpcode == EOpcode::Div
											|| FastProjectionOpcode == EOpcode::Mod
											|| FastProjectionOpcode == EOpcode::BitAnd
											|| FastProjectionOpcode == EOpcode::BitOr
											|| FastProjectionOpcode == EOpcode::BitXor
											|| FastProjectionOpcode == EOpcode::Shl
											|| FastProjectionOpcode == EOpcode::Shr)
										&& ResolveProjectionOperand(Bytecode::ReadU16LE(
											Method.Code.data() + 1u), FastProjectionLeft)
										&& ResolveProjectionOperand(Bytecode::ReadU16LE(
											Method.Code.data() + ProjectionSecondIp + 1u),
											FastProjectionRight);
								}
								const auto ReadFastProjectionOperand = [&](const FFastProjectionOperand& Operand)
								{
									return Operand.Assignment >= 0
										? AssignedValues[static_cast<std::size_t>(Operand.Assignment)]
										: Operand.DefaultValue;
								};
								std::size_t Completed = 0;
								for (std::int64_t Current = CounterValue;
									Current < Loop.Limit; ++Current)
								{
									CounterValue = static_cast<std::int32_t>(Current);
									std::int32_t Product = 0;
									std::int32_t Sum = 0;
									if (!Runtime::CheckedMul(StateValue, StateMultiplier, Product))
									{
										if (!CommitCompleted(Completed))
										{
											return FailVmAt(MulIp);
										}
										return ErrorAt(MulIp, "integer overflow");
									}
									if (!Runtime::CheckedAdd(Product, StateIncrement, Sum))
									{
										if (!CommitCompleted(Completed))
										{
											return FailVmAt(AddIp);
										}
										return ErrorAt(AddIp, "integer overflow");
									}
									if (StateModulus == 0
										|| (Sum == std::numeric_limits<std::int32_t>::min()
											&& StateModulus == -1))
									{
										if (!CommitCompleted(Completed))
										{
											return FailVmAt(StateModIp);
										}
										return ErrorAt(StateModIp, StateModulus == 0
											? "division by zero" : "integer overflow");
									}
									StateValue = FastRemainder(Sum, PreparedStateModulus);
									for (std::size_t Assignment = 0;
										Assignment < AssignmentCount; ++Assignment)
									{
										std::size_t ExpressionErrorIp = Ip;
										const char* ExpressionError = nullptr;
										const FFastScalarExpression& Fast = FastExpressions[Assignment];
										bool bEvaluated = Fast.Source != nullptr;
										switch (Fast.Kind)
										{
										case EFastScalarExpression::Local:
											{
												AssignedValues[Assignment] = ReadScalarSource(Fast.Source);
												break;
											}
										case EFastScalarExpression::LocalDiv:
											{
												AssignedValues[Assignment] = FastQuotient(
													ReadScalarSource(Fast.Source), Fast.First);
												break;
											}
										case EFastScalarExpression::LocalMod:
											{
												AssignedValues[Assignment] = FastRemainder(
													ReadScalarSource(Fast.Source), Fast.First);
												break;
											}
										case EFastScalarExpression::LocalDivMod:
											{
												AssignedValues[Assignment] = FastRemainder(
													FastQuotient(ReadScalarSource(Fast.Source), Fast.First),
													Fast.Second);
												break;
											}
										case EFastScalarExpression::None:
											{
												bEvaluated = false;
												break;
											}
										default:
											{
												break;
											}
										}
										if (!bEvaluated
											&& !EvaluateExpression(Assignment, AssignedValues[Assignment],
												ExpressionErrorIp, ExpressionError))
										{
											if (!CommitCompleted(Completed))
											{
												return FailVmAt(ExpressionErrorIp);
											}
											return ExpressionError
												? ErrorAt(ExpressionErrorIp, ExpressionError)
												: false;
										}
									}

									if (bFastBinaryProjection)
									{
										const std::int32_t Left =
											ReadFastProjectionOperand(FastProjectionLeft);
										const std::int32_t Right =
											ReadFastProjectionOperand(FastProjectionRight);
										std::int32_t ProjectionValue = 0;
										const char* ProjectionError = nullptr;
										switch (FastProjectionOpcode)
										{
										case EOpcode::Add:
											{
												if (!Runtime::CheckedAdd(Left, Right, ProjectionValue))
												{
													ProjectionError = "integer overflow";
												}
												break;
											}
										case EOpcode::Sub:
											{
												if (!Runtime::CheckedSub(Left, Right, ProjectionValue))
												{
													ProjectionError = "integer overflow";
												}
												break;
											}
										case EOpcode::Mul:
											{
												if (!Runtime::CheckedMul(Left, Right, ProjectionValue))
												{
													ProjectionError = "integer overflow";
												}
												break;
											}
										case EOpcode::Div:
										case EOpcode::Mod:
											{
												if (Right == 0)
												{
													ProjectionError = "division by zero";
												}
												else if (Left == std::numeric_limits<std::int32_t>::min()
													&& Right == -1)
												{
													ProjectionError = "integer overflow";
												}
												else
												{
													ProjectionValue = FastProjectionOpcode == EOpcode::Div
														? Left / Right
														: Left % Right;
												}
												break;
											}
										case EOpcode::BitAnd:
											{
												ProjectionValue = Left & Right;
												break;
											}
										case EOpcode::BitOr:
											{
												ProjectionValue = Left | Right;
												break;
											}
										case EOpcode::BitXor:
											{
												ProjectionValue = Left ^ Right;
												break;
											}

										case EOpcode::Shl:
										case EOpcode::Shr:
											{
												if (Right < 0 || Right >= 32)
												{
													ProjectionError = "invalid shift count";
												}
												else
												{
													ProjectionValue = FastProjectionOpcode == EOpcode::Shl
													? static_cast<std::int32_t>(
														static_cast<std::uint32_t>(Left) << Right)
													: static_cast<std::int32_t>(Left >> Right);
												}
												break;
											}
										default:
											return false;
										}
										if (ProjectionError)
										{
											if (!CommitCompleted(Completed))
											{
												return FailVmAt(ProjectionCallIp);
											}
											return ErrorAt(ProjectionCallIp, ProjectionError);
										}
										if (!Runtime::CheckedAdd(AccumulatorValue, ProjectionValue, Sum))
										{
											if (!CommitCompleted(Completed))
											{
												return FailVmAt(AccumulatorAddIp);
											}
											return ErrorAt(AccumulatorAddIp, "integer overflow");
										}
										if (AccumulatorModulus != 0)
										{
											if (Sum == std::numeric_limits<std::int32_t>::min()
												&& AccumulatorModulus == -1)
											{
												if (!CommitCompleted(Completed))
												{
													return FailVmAt(AccumulatorModIp);
												}
												return ErrorAt(AccumulatorModIp, "integer overflow");
											}
											Sum = FastRemainder(Sum, PreparedAccumulatorModulus);
										}
										AccumulatorValue = Sum;
										CounterValue = static_cast<std::int32_t>(Current + 1);
										++Completed;
										continue;
									}

									std::array<std::int32_t, 16> ProjectionStack{};
									std::size_t Depth = 0;
									const auto FindFieldValue = [&](const std::uint16_t Name,
										std::int32_t& Out) -> bool
									{
										for (std::size_t Assignment = AssignmentCount;
											Assignment > 0; --Assignment)
										{
											if (AssignedNames[Assignment - 1u] == Name)
											{
												Out = AssignedValues[Assignment - 1u];
												return true;
											}
										}
										if (Name >= Module->StringPool.size())
										{
											return false;
										}
										const std::string& FieldName = Module->StringPool[Name];
										for (const FBytecodeFieldDesc& Field : Type.Fields)
										{
											if (Field.Name != FieldName)
											{
												continue;
											}
											const FValue Default = JitHost::Services().ConstantToValue(
												*Module, Field.DefaultValue);
											const auto* Value = std::get_if<std::int32_t>(&Default);
											if (!Value)
											{
												return false;
											}
											Out = *Value;
											return true;
										}
										return false;
									};
									for (std::size_t MethodIp = 0; MethodIp < ProjectionReturnIp;)
									{
										const EOpcode MethodOp = static_cast<EOpcode>(Method.Code[MethodIp]);
										if (MethodOp == EOpcode::LoadThisField)
										{
											std::int32_t Value = 0;
											if (!FindFieldValue(Bytecode::ReadU16LE(
													Method.Code.data() + MethodIp + 1u),
													Value))
											{
												if (!CommitCompleted(Completed))
												{
													return FailVmAt(ProjectionCallIp);
												}
												return false;
											}
											ProjectionStack[Depth++] = Value;
										}
										else if (MethodOp == EOpcode::LoadLocal)
										{
											const std::uint16_t Slot = Bytecode::ReadU16LE(
											Method.Code.data() + MethodIp + 1u);
											if (Slot >= ProjectionArgumentCount || !ProjectionArgument
											|| Depth == ProjectionStack.size())
											{
												if (!CommitCompleted(Completed))
												{
													return FailVmAt(ProjectionCallIp);
												}
												return false;
											}
											ProjectionStack[Depth++] = ReadScalarSource(ProjectionArgument);
										}
										else if (MethodOp == EOpcode::ConstI32)
										{
											const std::uint16_t ConstantIndex = Bytecode::ReadU16LE(
												Method.Code.data() + MethodIp + 1u);
											if (ConstantIndex >= Module->Constants.size())
											{
												return false;
											}
											const FValue ConstantValue = JitHost::Services().ConstantToValue(
												*Module, Module->Constants[ConstantIndex]);
											const auto* Scalar = std::get_if<std::int32_t>(&ConstantValue);
											if (!Scalar)
											{
												return false;
											}
											const std::int32_t Value = *Scalar;
											ProjectionStack[Depth++] = Value;
										}
										else
										{
											const std::int32_t Right = ProjectionStack[--Depth];
											const std::int32_t Left = ProjectionStack[Depth - 1u];
											std::int32_t Value = 0;
											const char* ArithmeticError = nullptr;
											switch (MethodOp)
											{
											case EOpcode::Add:
												{
													if (!Runtime::CheckedAdd(Left, Right, Value))
													{
														ArithmeticError = "integer overflow";
													}
													break;
												}
											case EOpcode::Sub:
												{
													if (!Runtime::CheckedSub(Left, Right, Value))
													{
														ArithmeticError = "integer overflow";
													}
													break;
												}
											case EOpcode::Mul:
												{
													if (!Runtime::CheckedMul(Left, Right, Value))
													{
														ArithmeticError = "integer overflow";
													}
													break;
												}
											case EOpcode::Div:
											case EOpcode::Mod:
												{
													if (Right == 0)
													{
														ArithmeticError = "division by zero";
													}
													else if (Left == std::numeric_limits<std::int32_t>::min()
														&& Right == -1)
													{
														ArithmeticError = "integer overflow";
													}
													else
													{
														Value = MethodOp == EOpcode::Div
															? Left / Right
															: Left % Right;
													}
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

											case EOpcode::Shl:
											case EOpcode::Shr:
												{
													if (Right < 0 || Right >= 32)
													{
														ArithmeticError = "invalid shift count";
													}
													else
													{
														Value = MethodOp == EOpcode::Shl
														? static_cast<std::int32_t>(
															static_cast<std::uint32_t>(Left) << Right)
														: static_cast<std::int32_t>(Left >> Right);
													}
													break;
												}
											default:
												return false;
											}
											if (ArithmeticError)
											{
												if (!CommitCompleted(Completed))
												{
													return FailVmAt(ProjectionCallIp);
												}
												return ErrorAt(ProjectionCallIp, ArithmeticError);
											}
											ProjectionStack[Depth - 1u] = Value;
										}
										MethodIp += Bytecode::InstructionSize(MethodOp);
									}
									if (!Runtime::CheckedAdd(AccumulatorValue, ProjectionStack[0], Sum))
									{
										if (!CommitCompleted(Completed))
										{
											return FailVmAt(AccumulatorAddIp);
										}
										return ErrorAt(AccumulatorAddIp, "integer overflow");
									}
									if (AccumulatorModulus != 0)
									{
										if (Sum == std::numeric_limits<std::int32_t>::min()
											&& AccumulatorModulus == -1)
										{
											if (!CommitCompleted(Completed))
											{
												return FailVmAt(AccumulatorModIp);
											}
											return ErrorAt(AccumulatorModIp, "integer overflow");
										}
										Sum = FastRemainder(
											Sum, PreparedAccumulatorModulus);
									}
									AccumulatorValue = Sum;
									CounterValue = static_cast<std::int32_t>(Current + 1);
									++Completed;
								}
								CounterValue = Loop.Limit;
								if (!CommitCompleted(Completed))
								{
									return FailVmAt(ProjectionCallIp);
								}
								Ip = Loop.ExitIp;
								continue;
							}
						}
					}
				}
				// Per-iteration scalar replacement for a non-escaping trivial object:
				//   T value = T(); value.a = scalarExpr; ...;
				//   accumulator = (accumulator + value.pureProjection()) % optionalModulus;
				//
				// Unlike the whole-loop reducers below, this proof does not require a
				// canonical loop header or a particular scalar expression. It removes the
				// allocation/reference-count/ownership trampoline for straight-line code and
				// arbitrary control-flow loops alike. The object is replaced only when every
				// field is a trivial scalar, no destructor/default phase/interceptor/mutation
				// callback can observe identity, and the local is dead after the projection.
				if (Op == EOpcode::NewObject
					&& AfterIp + Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize
					&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::StoreLocal
					&& Vm->JitCanUseDirectScalarMethod()
					&& !Vm->JitHasInstanceCallInterceptor()
					&& !Vm->JitHasMutationCallback())
				{
					const std::uint16_t TypeIndex =
						Bytecode::ReadU16LE(Code.data() + Ip + 1u);
					const std::uint16_t ObjectSlot =
						Bytecode::ReadU16LE(Code.data() + AfterIp + 1u);
					if (TypeIndex < Module->Types.size())
					{
						const FBytecodeTypeDesc& Type = Module->Types[TypeIndex];
						const auto IsTrivialScalarField = [](const FBytecodeFieldDesc& Field)
						{
							return Field.bHasDefault && !Field.bHasInitCode
								&& JitHost::Services().IsPrimitiveValueTypeName(Field.TypeName)
								&& Field.TypeName != "string" && Field.TypeName != "wstring";
						};
						const bool bHasClassDefaults = std::any_of(
							Type.DeclMeta.begin(), Type.DeclMeta.end(),
							[](const auto& Entry)
							{
								return Entry.first == ClassDefaultsLinkageMetaKey;
							});
						if (!Type.bIsAbstract && Type.BaseTypeIndices.empty()
							&& Type.HostBaseTypeName.empty()
							&& Type.DestructorFunctionIndex == 0xFFFF
							&& !bHasClassDefaults
							&& Type.Fields.size() <= 8u
							&& std::all_of(
								Type.Fields.begin(), Type.Fields.end(), IsTrivialScalarField))
						{
							std::array<std::uint16_t, 8> AssignedNames{};
							auto& AssignedValues = LinearScratchState->ObjectScalarValues;
							std::size_t AssignmentCount = 0;
							std::size_t Cursor = AfterIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
							while (AssignmentCount < AssignedNames.size()
								&& Cursor + Bytecode::InstructionSize(EOpcode::LoadLocal)
									<= CodeSize
								&& static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + Cursor + 1u) == ObjectSlot)
							{
								const std::size_t ExpressionIp = Cursor
									+ Bytecode::InstructionSize(EOpcode::LoadLocal);
								bool bValueAvailable = false;
								std::size_t ExpressionEnd = ExpressionIp;
								if (!TryReadLinearScalarExpression(
										*Vm, *Module, Code, ExpressionIp,
										AssignedValues[AssignmentCount],
										bValueAvailable, ExpressionEnd))
								{
									return FailVmAt(ExpressionIp);
								}
								if (!bValueAvailable)
								{
									break;
								}
								std::size_t FieldStoreIp = ExpressionEnd;
								std::size_t NextAssignmentIp = CodeSize;
								if (ExpressionEnd < CodeSize
									&& static_cast<EOpcode>(Code[ExpressionEnd]) == EOpcode::StoreLocal)
								{
									constexpr std::size_t TempAssignmentTailSize = 14u;
									if (ExpressionEnd + TempAssignmentTailSize > CodeSize
										|| static_cast<EOpcode>(Code[ExpressionEnd + 3u]) != EOpcode::LoadLocal
										|| static_cast<EOpcode>(Code[ExpressionEnd + 6u]) != EOpcode::StoreField
										|| static_cast<EOpcode>(Code[ExpressionEnd + 9u]) != EOpcode::Pop
										|| static_cast<EOpcode>(Code[ExpressionEnd + 10u]) != EOpcode::LoadLocal
										|| static_cast<EOpcode>(Code[ExpressionEnd + 13u]) != EOpcode::Pop
										|| Bytecode::ReadU16LE(Code.data() + ExpressionEnd + 1u)
											!= Bytecode::ReadU16LE(Code.data() + ExpressionEnd + 4u)
										|| Bytecode::ReadU16LE(Code.data() + ExpressionEnd + 1u)
											!= Bytecode::ReadU16LE(Code.data() + ExpressionEnd + 11u))
									{
										break;
									}
									FieldStoreIp = ExpressionEnd + 6u;
									NextAssignmentIp = ExpressionEnd + TempAssignmentTailSize;
								}
								else
								{
									if (ExpressionEnd + Bytecode::InstructionSize(EOpcode::StoreField)
											+ Bytecode::InstructionSize(EOpcode::Pop) > CodeSize
										|| static_cast<EOpcode>(Code[ExpressionEnd]) != EOpcode::StoreField
										|| static_cast<EOpcode>(Code[ExpressionEnd
											+ Bytecode::InstructionSize(EOpcode::StoreField)]) != EOpcode::Pop)
									{
										break;
									}
									NextAssignmentIp = ExpressionEnd
										+ Bytecode::InstructionSize(EOpcode::StoreField)
										+ Bytecode::InstructionSize(EOpcode::Pop);
								}
								AssignedNames[AssignmentCount] =
									Bytecode::ReadU16LE(Code.data() + FieldStoreIp + 1u);
								++AssignmentCount;
								Cursor = NextAssignmentIp;
							}

							const std::size_t AccumulatorLoadIp = Cursor;
							const std::size_t ReceiverLoadIp = AccumulatorLoadIp
								+ Bytecode::InstructionSize(EOpcode::LoadLocal);
							auto& ProjectionArguments =
								LinearScratchState->ObjectProjectionArguments;
							std::size_t ProjectionArgumentCount = 0;
							std::size_t CallIp = ReceiverLoadIp
								+ Bytecode::InstructionSize(EOpcode::LoadLocal);
							while (CallIp < CodeSize
								&& static_cast<EOpcode>(Code[CallIp]) != EOpcode::CallBoundMethod
								&& ProjectionArgumentCount < ProjectionArguments.size())
							{
								bool bArgumentAvailable = false;
								std::size_t ArgumentEnd = CallIp;
								if (!TryReadLinearScalarExpression(
										*Vm, *Module, Code, CallIp,
										ProjectionArguments[ProjectionArgumentCount],
										bArgumentAvailable, ArgumentEnd))
								{
									return FailVmAt(CallIp);
								}
								if (!bArgumentAvailable || ArgumentEnd <= CallIp)
								{
									break;
								}
								++ProjectionArgumentCount;
								CallIp = ArgumentEnd;
							}
							const std::size_t AddIp = CallIp
								+ Bytecode::InstructionSize(EOpcode::CallBoundMethod);
							std::size_t StoreIp = AddIp + Bytecode::InstructionSize(EOpcode::Add);
							std::int32_t AccumulatorModulus = 0;
							std::size_t ModIp = StoreIp;
							if (AssignmentCount != 0 && StoreIp <= CodeSize
								&& static_cast<EOpcode>(Code[AccumulatorLoadIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[ReceiverLoadIp]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + ReceiverLoadIp + 1u) == ObjectSlot
								&& static_cast<EOpcode>(Code[CallIp]) == EOpcode::CallBoundMethod
								&& Code[CallIp + 3u] == ProjectionArgumentCount
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add)
							{
								if (StoreIp < CodeSize
									&& static_cast<EOpcode>(Code[StoreIp]) != EOpcode::StoreLocal)
								{
									FValue ModulusValue;
									bool bModulusAvailable = false;
									std::size_t ModulusEnd = StoreIp;
									if (!TryReadLinearScalarOperand(
											*Vm, *Module, Code, StoreIp, ModulusValue,
											bModulusAvailable, ModulusEnd))
									{
										return FailVmAt(StoreIp);
									}
									const auto* const Modulus =
										std::get_if<std::int32_t>(&ModulusValue);
									if (bModulusAvailable && Modulus
										&& ModulusEnd < CodeSize
										&& static_cast<EOpcode>(Code[ModulusEnd]) == EOpcode::Mod)
									{
										AccumulatorModulus = *Modulus;
										ModIp = ModulusEnd;
										StoreIp = ModulusEnd
											+ Bytecode::InstructionSize(EOpcode::Mod);
									}
								}
								const std::uint16_t AccumulatorSlot =
									Bytecode::ReadU16LE(Code.data() + AccumulatorLoadIp + 1u);
								const std::uint16_t MethodIndex =
									Bytecode::ReadU16LE(Code.data() + CallIp + 1u);
								const std::size_t RegionEnd = StoreIp
									+ Bytecode::InstructionSize(EOpcode::StoreLocal);
								bool bObjectEscapes = RegionEnd > CodeSize
									|| static_cast<EOpcode>(Code[StoreIp]) != EOpcode::StoreLocal
									|| Bytecode::ReadU16LE(Code.data() + StoreIp + 1u)
										!= AccumulatorSlot
									|| AccumulatorSlot == ObjectSlot
									|| MethodIndex >= Module->Functions.size();
								// Scan the complete function outside the eliminated region, not only
								// its textual suffix. A later back-edge may target bytecode before the
								// allocation. Any read/reference or replacement assignment to the same
								// local there may observe identity or value-release semantics that scalar
								// replacement intentionally does not materialize.
								if (!bObjectEscapes)
								{
									if (AssignedObjectEscapeProof.empty())
									{
										AssignedObjectEscapeProof.assign(CodeSize, std::int8_t{-1});
									}
									std::int8_t& CachedProof = AssignedObjectEscapeProof[Ip];
									if (CachedProof >= 0)
									{
										bObjectEscapes = CachedProof == 0;
									}
									else
									{
										for (std::size_t ScanIp = 0;
											!bObjectEscapes && ScanIp < CodeSize;)
										{
											if (ScanIp >= Ip && ScanIp < RegionEnd)
											{
												ScanIp = RegionEnd;
												continue;
											}
											const EOpcode ScanOp = static_cast<EOpcode>(Code[ScanIp]);
											const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
											if (ScanSize == 0 || ScanSize > CodeSize - ScanIp)
											{
												bObjectEscapes = true;
												break;
											}
											if ((ScanOp == EOpcode::LoadLocal
													|| ScanOp == EOpcode::StoreLocal
													|| ScanOp == EOpcode::MakeLocalRef
													|| ScanOp == EOpcode::BindLocal)
												&& ScanSize >= 3u
												&& Bytecode::ReadU16LE(Code.data() + ScanIp + 1u)
													== ObjectSlot)
											{
												bObjectEscapes = true;
											}
											ScanIp += ScanSize;
										}
										CachedProof = bObjectEscapes ? std::int8_t{0} : std::int8_t{1};
									}
								}

								if (!bObjectEscapes)
								{
									const FBytecodeFunction& Method =
										Module->Functions[MethodIndex];
									const auto& MethodCode = Method.Code;
									std::array<std::int32_t, 16> ProjectionStack{};
									std::size_t ProjectionDepth = 0;
									std::int32_t Projection = 0;
									bool bSawProjectionReturn = false;
									bool bPureProjection = Method.bIsMethod
										&& Method.OwnerTypeIndex == TypeIndex
										&& Method.ParamCount == ProjectionArgumentCount
										&& Method.LocalCount == Method.ParamCount
										&& !Method.bReturnsVoid
										&& !Method.bReturnsRef && !Method.bIsAbstract
										&& !Method.bDeleted && !BytecodeFunctionHasRefParams(Method)
										&& Method.ExceptionRegions.empty();
									for (std::size_t MethodIp = 0;
										bPureProjection && MethodIp < MethodCode.size();)
									{
										const EOpcode MethodOp =
											static_cast<EOpcode>(MethodCode[MethodIp]);
										const std::size_t MethodSize = Bytecode::InstructionSize(MethodOp);
										if (MethodSize == 0 || MethodSize > MethodCode.size() - MethodIp)
										{
											bPureProjection = false;
											break;
										}
										if (MethodOp == EOpcode::Return)
										{
											bPureProjection = ProjectionDepth == 1u
												&& MethodIp + MethodSize == MethodCode.size();
											if (bPureProjection)
											{
												Projection = ProjectionStack[0];
												bSawProjectionReturn = true;
											}
											break;
										}
										if (MethodOp == EOpcode::LoadThisField)
										{
											if (ProjectionDepth == ProjectionStack.size())
											{
												bPureProjection = false;
												break;
											}
											const std::uint16_t Name = Bytecode::ReadU16LE(
												MethodCode.data() + MethodIp + 1u);
											const FValue* FieldValue = nullptr;
											FValue DefaultFieldValue;
											for (std::size_t Assignment = AssignmentCount;
												Assignment > 0; --Assignment)
											{
												if (AssignedNames[Assignment - 1u] == Name)
												{
													FieldValue = &AssignedValues[Assignment - 1u];
													break;
												}
											}
											if (!FieldValue && Name < Module->StringPool.size())
											{
												const std::string& FieldName = Module->StringPool[Name];
												for (const FBytecodeFieldDesc& Field : Type.Fields)
												{
													if (Field.Name == FieldName && Field.bHasDefault)
													{
														DefaultFieldValue =
															JitHost::Services().ConstantToValue(*Module, Field.DefaultValue);
														FieldValue = &DefaultFieldValue;
														break;
													}
												}
											}
											const auto* Scalar = FieldValue
												? std::get_if<std::int32_t>(FieldValue)
												: nullptr;
											if (!Scalar)
											{
												bPureProjection = false;
												break;
											}
											ProjectionStack[ProjectionDepth++] = *Scalar;
										}
										else if (MethodOp == EOpcode::LoadLocal)
										{
											const std::uint16_t Slot = Bytecode::ReadU16LE(
												MethodCode.data() + MethodIp + 1u);
											const auto* Scalar = Slot < ProjectionArgumentCount
												? std::get_if<std::int32_t>(&ProjectionArguments[Slot])
												: nullptr;
											if (!Scalar || ProjectionDepth == ProjectionStack.size())
											{
												bPureProjection = false;
												break;
											}
											ProjectionStack[ProjectionDepth++] = *Scalar;
										}
										else if (MethodOp == EOpcode::ConstI32)
										{
											std::int32_t Scalar = 0;
											const std::uint16_t ConstantIndex = Bytecode::ReadU16LE(
												MethodCode.data() + MethodIp + 1u);
											bool bConstantAvailable = ConstantIndex < Module->Constants.size();
											if (bConstantAvailable)
											{
												const auto& Constant = Module->Constants[ConstantIndex];
												switch (Constant.Kind)
												{
												case EConstantKind::I8:
													{
														Scalar = Constant.I8;
														break;
													}
												case EConstantKind::U8:
													{
														Scalar = Constant.U8;
														break;
													}
												case EConstantKind::I16:
													{
														Scalar = Constant.I16;
														break;
													}
												case EConstantKind::U16:
													{
														Scalar = Constant.U16;
														break;
													}
												case EConstantKind::I32:
													{
														Scalar = Constant.I32;
														break;
													}
												default:
													{
														bConstantAvailable = false;
														break;
													}
												}
											}
											if (!bConstantAvailable
												|| ProjectionDepth == ProjectionStack.size())
											{
												bPureProjection = false;
												break;
											}
											ProjectionStack[ProjectionDepth++] = Scalar;
										}
										else if (MethodOp == EOpcode::Add || MethodOp == EOpcode::Sub
											|| MethodOp == EOpcode::Mul || MethodOp == EOpcode::Div
											|| MethodOp == EOpcode::Mod || MethodOp == EOpcode::BitAnd
											|| MethodOp == EOpcode::BitOr || MethodOp == EOpcode::BitXor
											|| MethodOp == EOpcode::Shl || MethodOp == EOpcode::Shr)
										{
											if (ProjectionDepth < 2u)
											{
												bPureProjection = false;
												break;
											}
											const std::int32_t Right = ProjectionStack[--ProjectionDepth];
											const std::int32_t Left = ProjectionStack[ProjectionDepth - 1u];
											std::int32_t Scalar = 0;
											const char* ArithmeticError = nullptr;
											switch (MethodOp)
											{
											case EOpcode::Add:
												{
													if (!Runtime::CheckedAdd(Left, Right, Scalar))
													{
														ArithmeticError = "integer overflow";
													}
													break;
												}
											case EOpcode::Sub:
												{
													if (!Runtime::CheckedSub(Left, Right, Scalar))
													{
														ArithmeticError = "integer overflow";
													}
													break;
												}
											case EOpcode::Mul:
												{
													if (!Runtime::CheckedMul(Left, Right, Scalar))
													{
														ArithmeticError = "integer overflow";
													}
													break;
												}
											case EOpcode::Div:
												{
													if (Right == 0)
													{
														ArithmeticError = "division by zero";
													}
													else if (Left == std::numeric_limits<std::int32_t>::min()
														&& Right == -1)
													{
														ArithmeticError = "integer overflow";
													}
													else
													{
														Scalar = Left / Right;
													}
													break;
												}
											case EOpcode::Mod:
												{
													if (Right == 0)
													{
														ArithmeticError = "division by zero";
													}
													else if (Left == std::numeric_limits<std::int32_t>::min()
														&& Right == -1)
													{
														ArithmeticError = "integer overflow";
													}
													else
													{
														Scalar = Left % Right;
													}
													break;
												}
											case EOpcode::BitAnd:
												{
													Scalar = Left & Right;
													break;
												}
											case EOpcode::BitOr:
												{
													Scalar = Left | Right;
													break;
												}
											case EOpcode::BitXor:
												{
													Scalar = Left ^ Right;
													break;
												}
											case EOpcode::Shl:
												{
													if (Right < 0 || Right >= 32)
													{
														ArithmeticError = "invalid shift count";
													}
													else
													{
														Scalar = static_cast<std::int32_t>(
															static_cast<std::uint32_t>(Left) << Right);
													}
													break;
												}
											case EOpcode::Shr:
												{
													if (Right < 0 || Right >= 32)
													{
														ArithmeticError = "invalid shift count";
													}
													else
													{
														Scalar = Left >> Right;
													}
													break;
												}
											default:
												{
													ArithmeticError = "unsupported scalar projection";
													break;
												}
											}
											if (ArithmeticError)
											{
												return ErrorAt(CallIp, ArithmeticError);
											}
											ProjectionStack[ProjectionDepth - 1u] = Scalar;
										}
										else
										{
											bPureProjection = false;
										}
										MethodIp += MethodSize;
									}

									auto* const Accumulator =
										std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot));
									if (bPureProjection && bSawProjectionReturn && Accumulator)
									{
										std::int32_t Sum = 0;
										if (!Runtime::CheckedAdd(*Accumulator, Projection, Sum))
										{
											return ErrorAt(AddIp, "integer overflow");
										}
										if (AccumulatorModulus == 0 && ModIp != StoreIp)
										{
											return ErrorAt(ModIp, "division by zero");
										}
										if (AccumulatorModulus == -1
											&& Sum == std::numeric_limits<std::int32_t>::min())
										{
											return ErrorAt(ModIp, "integer overflow");
										}
										*Accumulator = ModIp == StoreIp
											? Sum
											: Sum % AccumulatorModulus;
										if (!Vm->JitCommitDirectScalarMethod(
												EJitScalarValueKind::I32))
										{
											return FailVmAt(CallIp);
										}
										Ip = RegionEnd;
										continue;
									}
								}
							}
						}
					}
				}
				// Scalar-replace an exact constructor/projection pair whose object never
				// escapes the loop. Constructor parameters become virtual fields, so the
				// allocation, reference counting, constructor/method frames and ownership
				// trampoline disappear while all checked i32 arithmetic remains ordered.
				if (Op == EOpcode::NewObject
					&& AfterIp + Bytecode::InstructionSize(EOpcode::Dup)
						+ Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize
					&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::Dup
					&& Vm->JitCanUseDirectScalarMethod()
					&& !Vm->JitHasInstanceCallInterceptor()
					&& !Vm->JitHasMutationCallback())
				{
					const std::uint16_t TypeIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					const std::size_t TempStoreIp =
						AfterIp + Bytecode::InstructionSize(EOpcode::Dup);
					// Class construction keeps its result below the receiver on the
					// stack. Older bytecode and value construction use a local temp.
					const bool bHasTempStore = static_cast<EOpcode>(Code[TempStoreIp]) == EOpcode::StoreLocal;
					if (TypeIndex < Module->Types.size()
						&& !Module->Types[TypeIndex].bIsAbstract
						&& Module->Types[TypeIndex].BaseTypeIndices.empty()
						&& Module->Types[TypeIndex].HostBaseTypeName.empty()
						&& Module->Types[TypeIndex].DestructorFunctionIndex == 0xFFFF
						&& std::none_of(
							Module->Types[TypeIndex].DeclMeta.begin(),
							Module->Types[TypeIndex].DeclMeta.end(),
							[](const auto& Entry)
							{
								return Entry.first == ClassDefaultsLinkageMetaKey;
							})
						&& std::all_of(
							Module->Types[TypeIndex].Fields.begin(),
							Module->Types[TypeIndex].Fields.end(),
							[](const FBytecodeFieldDesc& Field)
							{
								return !Field.bHasInitCode
									&& JitHost::Services().IsPrimitiveValueTypeName(Field.TypeName)
									&& Field.TypeName != "string" && Field.TypeName != "wstring";
							}))
					{
						const std::uint16_t TempSlot =
							bHasTempStore ? Bytecode::ReadU16LE(Code.data() + TempStoreIp + 1) : 0xFFFFu;
						auto& ConstructorArgs = LinearScratchState->ObjectScalarValues;
						std::array<std::size_t, 8> ArgumentBegins{};
						std::array<std::size_t, 8> ArgumentEnds{};
						std::size_t ArgumentCount = 0;
						std::size_t ConstructorCallIp =
							TempStoreIp + (bHasTempStore ? Bytecode::InstructionSize(EOpcode::StoreLocal) : 0u);
						bool bArgumentShape = true;
						while (ConstructorCallIp < CodeSize
							&& static_cast<EOpcode>(Code[ConstructorCallIp])
								!= EOpcode::CallBoundMethod)
						{
							if (ArgumentCount == ConstructorArgs.size())
							{
								bArgumentShape = false;
								break;
							}
							const std::size_t Begin = ConstructorCallIp;
							bool bAvailable = false;
							std::size_t End = Begin;
							if (!TryReadLinearScalarExpression(
									*Vm, *Module, Code, Begin,
									ConstructorArgs[ArgumentCount], bAvailable, End))
							{
								return FailVmAt(Begin);
							}
							if (!bAvailable || End <= Begin)
							{
								bArgumentShape = false;
								break;
							}
							ArgumentBegins[ArgumentCount] = Begin;
							ArgumentEnds[ArgumentCount] = End;
							++ArgumentCount;
							ConstructorCallIp = End;
						}
						if (bArgumentShape && ArgumentCount != 0
							&& ConstructorCallIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod)
								<= CodeSize
							&& static_cast<EOpcode>(Code[ConstructorCallIp])
								== EOpcode::CallBoundMethod
							&& Code[ConstructorCallIp + 3] == ArgumentCount)
						{
							const std::uint16_t ConstructorIndex =
								Bytecode::ReadU16LE(Code.data() + ConstructorCallIp + 1);
							const std::size_t ConstructorCallEnd = ConstructorCallIp
								+ Bytecode::InstructionSize(EOpcode::CallBoundMethod);
							const std::size_t ReloadTempIp = ConstructorCallEnd
								+ Bytecode::InstructionSize(EOpcode::Pop);
							const std::size_t ObjectStoreIp = ReloadTempIp
								+ (bHasTempStore ? Bytecode::InstructionSize(EOpcode::LoadLocal) : 0u);
							const std::size_t AccumulatorLoadIp = ObjectStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
							const std::size_t ReceiverLoadIp = AccumulatorLoadIp
								+ Bytecode::InstructionSize(EOpcode::LoadLocal);
							const std::size_t ProjectionCallIp = ReceiverLoadIp
								+ Bytecode::InstructionSize(EOpcode::LoadLocal);
							const std::size_t AddIp = ProjectionCallIp
								+ Bytecode::InstructionSize(EOpcode::CallBoundMethod);
							if (ConstructorIndex < Module->Functions.size()
								&& AddIp + Bytecode::InstructionSize(EOpcode::Add) <= CodeSize
								&& static_cast<EOpcode>(Code[ConstructorCallEnd]) == EOpcode::Pop
								&& (!bHasTempStore || (static_cast<EOpcode>(Code[ReloadTempIp]) == EOpcode::LoadLocal
									&& Bytecode::ReadU16LE(Code.data() + ReloadTempIp + 1) == TempSlot))
								&& static_cast<EOpcode>(Code[ObjectStoreIp]) == EOpcode::StoreLocal
								&& static_cast<EOpcode>(Code[AccumulatorLoadIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[ReceiverLoadIp]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[ProjectionCallIp]) == EOpcode::CallBoundMethod
								&& Code[ProjectionCallIp + 3] == 0
								&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add)
							{
								const std::uint16_t ObjectSlot =
									Bytecode::ReadU16LE(Code.data() + ObjectStoreIp + 1);
								const std::uint16_t AccumulatorSlot =
									Bytecode::ReadU16LE(Code.data() + AccumulatorLoadIp + 1);
								const std::uint16_t ProjectionIndex =
									Bytecode::ReadU16LE(Code.data() + ProjectionCallIp + 1);
								std::size_t StoreIp = AddIp + Bytecode::InstructionSize(EOpcode::Add);
								std::int32_t AccumulatorModulus = 0;
								if (StoreIp < CodeSize
									&& static_cast<EOpcode>(Code[StoreIp]) != EOpcode::StoreLocal)
								{
									FValue ModulusValue;
									bool bModulusAvailable = false;
									std::size_t ModIp = StoreIp;
									if (!TryReadLinearScalarOperand(
											*Vm, *Module, Code, StoreIp, ModulusValue,
											bModulusAvailable, ModIp))
									{
										return FailVmAt(StoreIp);
									}
									const auto* const Modulus =
										std::get_if<std::int32_t>(&ModulusValue);
									if (!bModulusAvailable || !Modulus || *Modulus == 0
										|| ModIp >= CodeSize
										|| static_cast<EOpcode>(Code[ModIp]) != EOpcode::Mod)
									{
										StoreIp = CodeSize;
									}
									else
									{
										AccumulatorModulus = *Modulus;
										StoreIp = ModIp + Bytecode::InstructionSize(EOpcode::Mod);
									}
								}
								if (ProjectionIndex < Module->Functions.size()
									&& StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal) <= CodeSize
									&& static_cast<EOpcode>(Code[StoreIp]) == EOpcode::StoreLocal
									&& Bytecode::ReadU16LE(Code.data() + StoreIp + 1) == AccumulatorSlot
									&& Bytecode::ReadU16LE(Code.data() + ReceiverLoadIp + 1) == ObjectSlot
									&& ObjectSlot != TempSlot && ObjectSlot != AccumulatorSlot
									&& TempSlot != AccumulatorSlot)
								{
									const FBytecodeTypeDesc& Type = Module->Types[TypeIndex];
									const FBytecodeFunction& Constructor = Module->Functions[ConstructorIndex];
									const FBytecodeFunction& Projection = Module->Functions[ProjectionIndex];
									const bool bHasClassDefaults = std::any_of(
										Type.DeclMeta.begin(), Type.DeclMeta.end(),
										[](const auto& Entry)
										{
											return Entry.first == ClassDefaultsLinkageMetaKey;
										});
									const auto IsSideEffectFreeFieldDefault = [](const FBytecodeFieldDesc& Field)
									{
										if (Field.bHasInitCode
											|| !JitHost::Services().IsPrimitiveValueTypeName(Field.TypeName)
											|| Field.TypeName == "string" || Field.TypeName == "wstring")
										{
											return false;
										}
										if (!Field.bHasDefault)
										{
											return true;
										}
										const EConstantKind Kind = Field.DefaultValue.Kind;
										return Kind == EConstantKind::I8 || Kind == EConstantKind::U8
												|| Kind == EConstantKind::I16 || Kind == EConstantKind::U16
												|| Kind == EConstantKind::I32 || Kind == EConstantKind::U32
												|| Kind == EConstantKind::I64 || Kind == EConstantKind::U64
												|| Kind == EConstantKind::F32 || Kind == EConstantKind::F64
												|| Kind == EConstantKind::Bool;
									};
									std::array<std::uint16_t, 8> ParameterFieldNames{};
									bool bConstructorShape = !Type.bIsAbstract
										&& Type.BaseTypeIndices.empty() && Type.HostBaseTypeName.empty()
										&& Type.DestructorFunctionIndex == 0xFFFF
										&& !bHasClassDefaults
										&& std::all_of(
											Type.Fields.begin(), Type.Fields.end(),
											IsSideEffectFreeFieldDefault)
										&& Constructor.bIsMethod
										&& Constructor.OwnerTypeIndex == TypeIndex
										&& Constructor.ParamCount == ArgumentCount
										&& Constructor.LocalCount == ArgumentCount
										&& Constructor.bReturnsVoid && !Constructor.bReturnsRef
										&& !Constructor.bIsVirtual && !Constructor.bIsAbstract
										&& !Constructor.bDeleted && !BytecodeFunctionHasRefParams(Constructor)
										&& Constructor.ExceptionRegions.empty()
										&& Constructor.Code.size() == ArgumentCount * 10u + 4u;
									for (std::size_t I = 0; bConstructorShape && I < ArgumentCount; ++I)
									{
										const std::size_t Base = I * 10u;
										const std::uint16_t Name =
											Bytecode::ReadU16LE(Constructor.Code.data() + Base + 4);
										bConstructorShape =
											static_cast<EOpcode>(Constructor.Code[Base]) == EOpcode::LoadLocal
											&& Bytecode::ReadU16LE(Constructor.Code.data() + Base + 1) == I
											&& static_cast<EOpcode>(Constructor.Code[Base + 3]) == EOpcode::StoreThisField
											&& static_cast<EOpcode>(Constructor.Code[Base + 6]) == EOpcode::LoadThisField
											&& Bytecode::ReadU16LE(Constructor.Code.data() + Base + 7) == Name
											&& static_cast<EOpcode>(Constructor.Code[Base + 9]) == EOpcode::Pop;
										ParameterFieldNames[I] = Name;
									}
									const std::size_t ConstructorTail = ArgumentCount * 10u;
									bConstructorShape = bConstructorShape
										&& static_cast<EOpcode>(Constructor.Code[ConstructorTail]) == EOpcode::ConstI32
										&& static_cast<EOpcode>(Constructor.Code[ConstructorTail + 3]) == EOpcode::Return;
									std::array<std::uint8_t, 8> ProjectionParameters{};
									std::size_t ProjectionParameterCount = 0;
									bool bProjectionShape = bConstructorShape
										&& Projection.bIsMethod && !Projection.bReturnsVoid
										&& !Projection.bReturnsRef && Projection.ParamCount == 0
										&& Projection.OwnerTypeIndex == TypeIndex
										&& !Projection.bIsAbstract && !Projection.bDeleted
										&& !BytecodeFunctionHasRefParams(Projection)
										&& Projection.ExceptionRegions.empty();
									for (std::size_t ProjectionIp = 0;
										bProjectionShape && ProjectionIp < Projection.Code.size();)
									{
										if (static_cast<EOpcode>(Projection.Code[ProjectionIp]) != EOpcode::LoadThisField
											|| ProjectionParameterCount == ProjectionParameters.size())
										{
											bProjectionShape = false;
											break;
										}
										const std::uint16_t Name =
											Bytecode::ReadU16LE(Projection.Code.data() + ProjectionIp + 1);
										std::size_t ParameterIndex = 0;
										while (ParameterIndex < ArgumentCount
											&& ParameterFieldNames[ParameterIndex] != Name)
										{
											++ParameterIndex;
										}
										if (ParameterIndex == ArgumentCount)
										{
											bProjectionShape = false;
											break;
										}
										ProjectionParameters[ProjectionParameterCount++] =
											static_cast<std::uint8_t>(ParameterIndex);
										ProjectionIp += Bytecode::InstructionSize(EOpcode::LoadThisField);
										if (ProjectionParameterCount > 1)
										{
											if (ProjectionIp >= Projection.Code.size()
												|| static_cast<EOpcode>(Projection.Code[ProjectionIp]) != EOpcode::Add)
											{
												bProjectionShape = false;
												break;
											}
											ProjectionIp += Bytecode::InstructionSize(EOpcode::Add);
										}
										if (ProjectionIp < Projection.Code.size()
											&& static_cast<EOpcode>(Projection.Code[ProjectionIp]) == EOpcode::Return)
										{
											bProjectionShape = ProjectionIp + 1u == Projection.Code.size();
											break;
										}
									}
									std::array<FCounterAffineI32, 8> ArgumentExpressions{};
									std::array<bool, 8> bArgumentAffine{};
									for (std::size_t I = 0; bProjectionShape && I < ArgumentCount; ++I)
									{
										if (TryDescribeCounterAffine(
												ArgumentBegins[I], ArgumentEnds[I], ArgumentExpressions[I]))
										{
											bArgumentAffine[I] = true;
										}
										else if (ArgumentEnds[I] - ArgumentBegins[I] != 3u)
										{
											bProjectionShape = false;
										}
									}
									const std::size_t TailIp = StoreIp
										+ Bytecode::InstructionSize(EOpcode::StoreLocal);
									const std::uint16_t CounterSlot = TailIp + 3u <= CodeSize
										&& static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal
										? Bytecode::ReadU16LE(Code.data() + TailIp + 1)
										: 0xFFFF;
									for (std::size_t I = 0; bProjectionShape && I < ArgumentCount; ++I)
									{
										if (bArgumentAffine[I]
											&& ArgumentExpressions[I].CounterSlot != CounterSlot)
										{
											bProjectionShape = false;
										}
									}
									FCanonicalI32Loop Loop;
									bool bObjectEscapes = false;
									for (std::size_t ScanIp = TailIp; !bObjectEscapes && ScanIp < CodeSize;)
									{
										const EOpcode ScanOp = static_cast<EOpcode>(Code[ScanIp]);
										const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
										if (ScanSize == 0 || ScanSize > CodeSize - ScanIp)
										{
											bObjectEscapes = true;
											break;
										}
										if ((ScanOp == EOpcode::LoadLocal || ScanOp == EOpcode::MakeLocalRef)
											&& (Bytecode::ReadU16LE(Code.data() + ScanIp + 1) == ObjectSlot
												|| Bytecode::ReadU16LE(Code.data() + ScanIp + 1) == TempSlot))
										{
											bObjectEscapes = true;
										}
										ScanIp += ScanSize;
									}
									auto* const Accumulator = std::get_if<std::int32_t>(
										BorrowLocal(AccumulatorSlot));
									if (bProjectionShape && ProjectionParameterCount != 0
										&& !bObjectEscapes && Accumulator
										&& TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
										&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot)
										&& LoopDoesNotAliasMutatedLocal(Loop, ObjectSlot)
										&& (!bHasTempStore || LoopDoesNotAliasMutatedLocal(Loop, TempSlot)))
									{
										const std::int32_t First = *Loop.Counter;
										std::size_t Completed = 0;
										for (std::int64_t Current = First; Current < Loop.Limit; ++Current)
										{
											*Loop.Counter = static_cast<std::int32_t>(Current);
											for (std::size_t I = 0; I < ArgumentCount; ++I)
											{
												if (!bArgumentAffine[I])
												{
													continue;
												}
												std::int32_t Value = 0;
												if (!EvaluateCounterAffine(
														ArgumentExpressions[I], *Loop.Counter, Value))
												{
													return ErrorAt(ArgumentBegins[I], "integer overflow");
												}
												ConstructorArgs[I] = Value;
											}
											const auto* FirstValue = std::get_if<std::int32_t>(
												&ConstructorArgs[ProjectionParameters[0]]);
											if (!FirstValue)
											{
												break;
											}
											std::int32_t ProjectionValue = *FirstValue;
											bool bProjectionOk = true;
											for (std::size_t I = 1; I < ProjectionParameterCount; ++I)
											{
												const auto* Value = std::get_if<std::int32_t>(
													&ConstructorArgs[ProjectionParameters[I]]);
												bProjectionOk = Value
													&& Runtime::CheckedAdd(ProjectionValue, *Value, ProjectionValue);
												if (!bProjectionOk)
												{
													break;
												}
											}
											std::int32_t Sum = 0;
											if (!bProjectionOk
												|| !Runtime::CheckedAdd(*Accumulator, ProjectionValue, Sum)
												|| (AccumulatorModulus == -1
													&& Sum == std::numeric_limits<std::int32_t>::min()))
											{
												return ErrorAt(AddIp, "integer overflow");
											}
											*Accumulator = AccumulatorModulus == 0
												? Sum
												: Sum % AccumulatorModulus;
											++Completed;
										}
										if (Completed == static_cast<std::size_t>(
												static_cast<std::int64_t>(Loop.Limit) - First))
										{
											if (!Vm->JitCommitDirectScalarMethod(
													EJitScalarValueKind::I32, Completed))
											{
												return FailVmAt(ProjectionCallIp);
											}
											*Loop.Counter = Loop.Limit;
											Ip = Loop.ExitIp;
											continue;
										}
									}
								}
							}
						}
					}
				}
				// Normalize the equally common source spelling where every branch calls
				// the same virtual method directly:
				//   if (choice == 0) total = a.step(total); else ...
				// The chain is validated as one closed canonical loop, then each concrete
				// receiver is resolved once into the same bounded transform PIC used by a
				// selected-receiver call.  Two through eight arms are supported, with
				// either modulo selection or a power-of-two bit mask.
				if (Op == EOpcode::LoadLocal
					&& AfterIp + 7u <= CodeSize
					&& Vm->JitCanUseDirectScalarMethod()
					&& !Vm->JitHasInstanceCallInterceptor())
				{
					const std::uint16_t CounterSlot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					std::int32_t SelectorOperand = 0;
					const std::size_t SelectorOperandIp = AfterIp;
					const std::size_t SelectorOpIp = SelectorOperandIp + 3u;
					const std::size_t ChoiceStoreIp = SelectorOpIp + 1u;
					const EOpcode SelectorOp = SelectorOpIp < CodeSize
						? static_cast<EOpcode>(Code[SelectorOpIp])
						: EOpcode::Nop;
					std::int32_t ReceiverCountI32 = 0;
					if (TryResolveI32InvariantOperand(
							*Module, Code, SelectorOperandIp, SelectorOperand)
						&& static_cast<EOpcode>(Code[ChoiceStoreIp]) == EOpcode::StoreLocal)
					{
						if (SelectorOp == EOpcode::Mod)
						{
							ReceiverCountI32 = SelectorOperand;
						}
						else if (SelectorOp == EOpcode::BitAnd
							&& SelectorOperand > 0 && SelectorOperand < 8
							&& (SelectorOperand & (SelectorOperand + 1)) == 0)
						{
							ReceiverCountI32 = SelectorOperand + 1;
						}
						if (ReceiverCountI32 >= 2 && ReceiverCountI32 <= 8)
						{
							const std::size_t ReceiverCount =
								static_cast<std::size_t>(ReceiverCountI32);
							const std::uint16_t ChoiceSlot =
								Bytecode::ReadU16LE(Code.data() + ChoiceStoreIp + 1);
							std::array<std::uint16_t, 8> ReceiverSlots{};
							std::array<std::size_t, 8> CallIps{};
							std::size_t Cursor = ChoiceStoreIp
								+ Bytecode::InstructionSize(EOpcode::StoreLocal);
							std::size_t CommonTailIp = 0;
							std::uint16_t AccumulatorSlot = 0xFFFF;
							std::uint16_t MethodNameIndex = 0xFFFF;
							bool bDirectBranchShape = true;
							for (std::size_t Arm = 0; Arm + 1u < ReceiverCount; ++Arm)
							{
								constexpr std::size_t ArmSize = 30u;
								if (Cursor + ArmSize > CodeSize
									|| static_cast<EOpcode>(Code[Cursor]) != EOpcode::LoadLocal
									|| Bytecode::ReadU16LE(Code.data() + Cursor + 1u) != ChoiceSlot
									|| static_cast<EOpcode>(Code[Cursor + 3u]) != EOpcode::ConstI32
									|| static_cast<EOpcode>(Code[Cursor + 6u]) != EOpcode::Eq
									|| static_cast<EOpcode>(Code[Cursor + 7u]) != EOpcode::JmpIfFalse
									|| static_cast<EOpcode>(Code[Cursor + 12u]) != EOpcode::LoadLocal
									|| static_cast<EOpcode>(Code[Cursor + 15u]) != EOpcode::LoadLocal
									|| static_cast<EOpcode>(Code[Cursor + 18u]) != EOpcode::CallMethod
									|| Code[Cursor + 21u] != 1
									|| static_cast<EOpcode>(Code[Cursor + 22u]) != EOpcode::StoreLocal
									|| static_cast<EOpcode>(Code[Cursor + 25u]) != EOpcode::Jmp)
								{
									bDirectBranchShape = false;
									break;
								}
								std::int32_t CompareValue = 0;
								std::size_t FalseTarget = 0;
								std::size_t TailTarget = 0;
								const std::uint16_t ArmAccumulator =
									Bytecode::ReadU16LE(Code.data() + Cursor + 16u);
								const std::uint16_t ArmMethod =
									Bytecode::ReadU16LE(Code.data() + Cursor + 19u);
								if (!TryResolveI32InvariantOperand(*Module, Code, Cursor + 3u, CompareValue)
									|| CompareValue != static_cast<std::int32_t>(Arm)
									|| Bytecode::ReadU16LE(Code.data() + Cursor + 23u)
										!= ArmAccumulator
									|| !IsJumpTargetInRange(
										Cursor + 12u, Bytecode::ReadI32LE(Code.data() + Cursor + 8u),
										CodeSize, FalseTarget)
									|| FalseTarget != Cursor + ArmSize
									|| !IsJumpTargetInRange(
										Cursor + ArmSize, Bytecode::ReadI32LE(Code.data() + Cursor + 26u),
										CodeSize, TailTarget)
									|| (AccumulatorSlot != 0xFFFF && AccumulatorSlot != ArmAccumulator)
									|| (MethodNameIndex != 0xFFFF && MethodNameIndex != ArmMethod)
									|| (CommonTailIp != 0 && CommonTailIp != TailTarget))
								{
									bDirectBranchShape = false;
									break;
								}
								ReceiverSlots[Arm] =
									Bytecode::ReadU16LE(Code.data() + Cursor + 13u);
								CallIps[Arm] = Cursor + 18u;
								AccumulatorSlot = ArmAccumulator;
								MethodNameIndex = ArmMethod;
								CommonTailIp = TailTarget;
								Cursor += ArmSize;
							}
							constexpr std::size_t FinalArmSize = 13u;
							if (bDirectBranchShape && Cursor + FinalArmSize <= CodeSize
								&& static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[Cursor + 3u]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[Cursor + 6u]) == EOpcode::CallMethod
								&& Code[Cursor + 9u] == 1
								&& static_cast<EOpcode>(Code[Cursor + 10u]) == EOpcode::StoreLocal
								&& Bytecode::ReadU16LE(Code.data() + Cursor + 4u) == AccumulatorSlot
								&& Bytecode::ReadU16LE(Code.data() + Cursor + 7u) == MethodNameIndex
								&& Bytecode::ReadU16LE(Code.data() + Cursor + 11u) == AccumulatorSlot
								&& Cursor + FinalArmSize == CommonTailIp)
							{
								ReceiverSlots[ReceiverCount - 1u] =
									Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
								CallIps[ReceiverCount - 1u] = Cursor + 6u;
								FCanonicalI32Loop Loop;
								auto* const Accumulator =
									std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot));
								auto* const Choice =
									std::get_if<std::int32_t>(BorrowLocal(ChoiceSlot));
								const bool bReceiverSlotsSafe = std::all_of(
									ReceiverSlots.begin(), ReceiverSlots.begin() + ReceiverCount,
									[&](const std::uint16_t ReceiverSlot)
									{
										return ReceiverSlot != CounterSlot
												&& ReceiverSlot != AccumulatorSlot
												&& ReceiverSlot != ChoiceSlot;
									});
								if (Accumulator && Choice && bReceiverSlotsSafe
									&& TryMatchCanonicalI32Loop(Ip, CommonTailIp, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot)
									&& LoopDoesNotAliasMutatedLocal(Loop, ChoiceSlot)
									&& std::none_of(
										ReceiverSlots.begin(), ReceiverSlots.begin() + ReceiverCount,
										[&](const std::uint16_t ReceiverSlot)
										{
											return ReceiverSlot == Loop.LimitSlot;
										}))
								{
									std::array<std::int32_t, 8> Addends{};
									std::array<std::int32_t, 8> Moduli{};
									bool bAllResolved = true;
									for (std::size_t ReceiverIndex = 0;
										bAllResolved && ReceiverIndex < ReceiverCount; ++ReceiverIndex)
									{
										FScriptObject* Object = nullptr;
										bool bHandled = false;
										if (!Vm->JitBorrowDirectI32TransformMethod(
												ReceiverSlots[ReceiverIndex], MethodNameIndex,
												static_cast<std::uint32_t>(CallIps[ReceiverIndex]), Object,
												Addends[ReceiverIndex], Moduli[ReceiverIndex], bHandled))
										{
											if (Vm->HasError())
											{
												return FailVmAt(CallIps[ReceiverIndex]);
											}
											bAllResolved = false;
											break;
										}
										bAllResolved = bHandled && Object;
									}
									if (bAllResolved)
									{
										const std::int32_t First = *Loop.Counter;
										const std::size_t IterationCount = First < Loop.Limit
											? static_cast<std::size_t>(
												static_cast<std::int64_t>(Loop.Limit) - First)
											: 0u;
										bAllResolved = CanProveI32TransformSequenceNoThrow(
							*Accumulator, IterationCount, Addends.data(),
							Moduli.data(), ReceiverCount);
									}
									if (bAllResolved)
									{
										const std::int32_t First = *Loop.Counter;
										const std::size_t IterationCount = static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - First);
										const bool bNoModulus = std::all_of(
											Moduli.begin(), Moduli.begin() + ReceiverCount,
											[](const std::int32_t Modulus)
											{
												return Modulus == 0;
											});
										const bool bNonNegative = std::all_of(
											Addends.begin(), Addends.begin() + ReceiverCount,
											[](const std::int32_t Addend)
											{
												return Addend >= 0;
											});
										const bool bNonPositive = std::all_of(
											Addends.begin(), Addends.begin() + ReceiverCount,
											[](const std::int32_t Addend)
											{
												return Addend <= 0;
											});
										const bool bPeriodicSelector = SelectorOp == EOpcode::BitAnd
											|| First >= 0;
										if (bNoModulus && bPeriodicSelector
											&& (bNonNegative || bNonPositive))
										{
											std::uint64_t CycleMagnitude = 0;
											std::uint64_t RemainderMagnitude = 0;
											const std::size_t SampleCount =
												std::min(ReceiverCount, IterationCount);
											for (std::size_t Offset = 0; Offset < SampleCount; ++Offset)
											{
												const std::int32_t Current = static_cast<std::int32_t>(
													static_cast<std::int64_t>(First) + Offset);
												const std::int32_t Selected = SelectorOp == EOpcode::BitAnd
													? (Current & SelectorOperand)
													: (Current % SelectorOperand);
												const std::size_t SelectedIndex = Selected >= 0
													&& Selected < ReceiverCountI32 - 1
													? static_cast<std::size_t>(Selected)
													: ReceiverCount - 1u;
												const std::int64_t SignedAddend = Addends[SelectedIndex];
												CycleMagnitude += static_cast<std::uint64_t>(
													bNonNegative ? SignedAddend : -SignedAddend);
												if (Offset < IterationCount % ReceiverCount)
												{
													RemainderMagnitude += static_cast<std::uint64_t>(
														bNonNegative ? SignedAddend : -SignedAddend);
												}
											}
											const std::uint64_t FullCycles =
												IterationCount / ReceiverCount;
											const std::uint64_t AvailableMagnitude =
												static_cast<std::uint64_t>(bNonNegative
													? static_cast<std::int64_t>(
														std::numeric_limits<std::int32_t>::max())
														-
															static_cast<std::int64_t>(*Accumulator)
													: static_cast<std::int64_t>(*Accumulator)
														- static_cast<std::int64_t>(
															std::numeric_limits<std::int32_t>::min()));
											const bool bCycleFits = RemainderMagnitude <= AvailableMagnitude
												&& (CycleMagnitude == 0
													|| FullCycles <= (AvailableMagnitude - RemainderMagnitude)
														/ CycleMagnitude);
											if (bCycleFits)
											{
												const std::uint64_t Magnitude =
													FullCycles * CycleMagnitude + RemainderMagnitude;
												const std::int64_t FinalValue = bNonNegative
													? static_cast<std::int64_t>(*Accumulator)
														+ static_cast<std::int64_t>(Magnitude)
													: static_cast<std::int64_t>(*Accumulator)
														- static_cast<std::int64_t>(Magnitude);
												if (!Vm->JitCommitDirectI32TransformMethod(IterationCount))
												{
													return FailVmAt(CallIps[0]);
												}
												*Accumulator = static_cast<std::int32_t>(FinalValue);
												const std::int32_t LastCounter = Loop.Limit - 1;
												*Choice = SelectorOp == EOpcode::BitAnd
													? (LastCounter & SelectorOperand)
													: (LastCounter % SelectorOperand);
												*Loop.Counter = Loop.Limit;
												Ip = Loop.ExitIp;
												continue;
											}
										}
										std::size_t Completed = 0;
										std::int32_t AccumulatedValue = *Accumulator;
										for (std::int64_t Current = First;
											Current < Loop.Limit; ++Current)
										{
											const std::int32_t Selected = SelectorOp == EOpcode::BitAnd
												? (static_cast<std::int32_t>(Current) & SelectorOperand)
												: (static_cast<std::int32_t>(Current) % SelectorOperand);
											const std::size_t SelectedIndex = Selected >= 0
												&& Selected < ReceiverCountI32 - 1
												? static_cast<std::size_t>(Selected)
												: ReceiverCount - 1u;
											std::int32_t Sum = 0;
											if (!Runtime::CheckedAdd(
													AccumulatedValue, Addends[SelectedIndex], Sum)
												|| (Moduli[SelectedIndex] == -1
													&& Sum == std::numeric_limits<std::int32_t>::min()))
											{
												*Accumulator = AccumulatedValue;
												*Choice = Selected;
												*Loop.Counter = static_cast<std::int32_t>(Current);
												if (Completed != 0
													&& !Vm->JitCommitDirectI32TransformMethod(Completed))
												{
													return FailVmAt(CallIps[SelectedIndex]);
												}
												return ErrorAt(CallIps[SelectedIndex], "integer overflow");
											}
											AccumulatedValue = Moduli[SelectedIndex] == 0
												? Sum
												: Sum % Moduli[SelectedIndex];
											++Completed;
										}
										if (!Vm->JitCommitDirectI32TransformMethod(Completed))
										{
											return FailVmAt(CallIps[0]);
										}
										*Accumulator = AccumulatedValue;
										const std::int32_t LastCounter = Loop.Limit - 1;
										*Choice = SelectorOp == EOpcode::BitAnd
											? (LastCounter & SelectorOperand)
											: (LastCounter % SelectorOperand);
										*Loop.Counter = Loop.Limit;
										Ip = Loop.ExitIp;
										continue;
									}
								}
							}
						}
					}
				}
				// Lower a cyclic polymorphic receiver selection followed by one i32
				// transform call into a compact typed loop. This is a true bounded
				// polymorphic inline cache (up to eight receiver states). Positive modulo
				// selectors and equivalent power-of-two masks share the same proof; unrelated
				// megamorphic sites retain normal dispatch and cache replacement semantics.
				if (Op == EOpcode::LoadLocal
					&& AfterIp + 7u <= CodeSize
					&& Vm->JitCanUseDirectScalarMethod()
					&& !Vm->JitHasInstanceCallInterceptor())
				{
					const std::uint16_t CounterSlot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					std::int32_t SelectorOperand = 0;
					std::int32_t ReceiverCountI32 = 0;
					const std::size_t SelectorOperandIp = AfterIp;
					const std::size_t SelectorOpIp = SelectorOperandIp + 3u;
					const std::size_t ChoiceStoreIp = SelectorOpIp + 1u;
					const EOpcode SelectorOp = SelectorOpIp < CodeSize
						? static_cast<EOpcode>(Code[SelectorOpIp])
						: EOpcode::Nop;
					if (TryResolveI32InvariantOperand(
							*Module, Code, SelectorOperandIp, SelectorOperand)
						&& static_cast<EOpcode>(Code[ChoiceStoreIp]) == EOpcode::StoreLocal)
					{
						if (SelectorOp == EOpcode::Mod)
						{
							ReceiverCountI32 = SelectorOperand;
						}
						else if (SelectorOp == EOpcode::BitAnd
							&& SelectorOperand > 0 && SelectorOperand < 8
							&& (SelectorOperand & (SelectorOperand + 1)) == 0)
						{
							ReceiverCountI32 = SelectorOperand + 1;
						}
						const bool bSupportedSelector =
							ReceiverCountI32 >= 2 && ReceiverCountI32 <= 8;
						const std::size_t ReceiverCount = bSupportedSelector
							? static_cast<std::size_t>(ReceiverCountI32)
							: 0u;
						const std::uint16_t ChoiceSlot =
							Bytecode::ReadU16LE(Code.data() + ChoiceStoreIp + 1);
						std::array<std::uint16_t, 8> ReceiverSlots{};
						std::size_t Cursor = ChoiceStoreIp
							+ Bytecode::InstructionSize(EOpcode::StoreLocal);
						std::size_t CommonCallIp = 0;
						std::uint16_t SelectedSlot = 0xFFFF;
						bool bSelectionShape = bSupportedSelector;
						for (std::size_t Arm = 0; Arm + 1 < ReceiverCount; ++Arm)
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
								|| Bytecode::ReadU16LE(Code.data() + Cursor + 1) != ChoiceSlot
								|| !TryResolveI32InvariantOperand(
									*Module, Code, CompareConstantIp, CompareValue)
								|| CompareValue != static_cast<std::int32_t>(Arm)
								|| static_cast<EOpcode>(Code[EqIp]) != EOpcode::Eq
								|| static_cast<EOpcode>(Code[BranchIp]) != EOpcode::JmpIfFalse
								|| static_cast<EOpcode>(Code[ReceiverLoadIp]) != EOpcode::LoadLocal
								|| static_cast<EOpcode>(Code[ReceiverStoreIp]) != EOpcode::StoreLocal
								|| static_cast<EOpcode>(Code[JumpIp]) != EOpcode::Jmp
								|| !IsJumpTargetInRange(
									BranchIp + 5u, Bytecode::ReadI32LE(Code.data() + BranchIp + 1),
									CodeSize, FalseTarget)
								|| FalseTarget != NextArmIp
								|| !IsJumpTargetInRange(
									JumpIp + 5u, Bytecode::ReadI32LE(Code.data() + JumpIp + 1),
									CodeSize, CallTarget))
							{
								bSelectionShape = false;
								break;
							}
							const std::uint16_t ArmSelectedSlot =
								Bytecode::ReadU16LE(Code.data() + ReceiverStoreIp + 1);
							if ((SelectedSlot != 0xFFFF && SelectedSlot != ArmSelectedSlot)
								|| (CommonCallIp != 0 && CommonCallIp != CallTarget))
							{
								bSelectionShape = false;
								break;
							}
							SelectedSlot = ArmSelectedSlot;
							CommonCallIp = CallTarget;
							ReceiverSlots[Arm] =
								Bytecode::ReadU16LE(Code.data() + ReceiverLoadIp + 1);
							Cursor = NextArmIp;
						}
						if (bSelectionShape && Cursor + 6u <= CodeSize
							&& static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[Cursor + 3u]) == EOpcode::StoreLocal
							&& Bytecode::ReadU16LE(Code.data() + Cursor + 4u) == SelectedSlot)
						{
							ReceiverSlots[ReceiverCount - 1u] =
								Bytecode::ReadU16LE(Code.data() + Cursor + 1);
							Cursor += 6u;
							bSelectionShape = Cursor == CommonCallIp;
						}
						else
						{
							bSelectionShape = false;
						}
						std::size_t MethodCallIp = 0;
						std::size_t TailIp = 0;
						std::uint16_t AccumulatorSlot = 0xFFFF;
						std::uint16_t ResultSlot = 0xFFFF;
						std::uint16_t MethodNameIndex = 0xFFFF;
						std::int32_t AdditiveArgument = 0;
						bool bAdditiveAccumulator = false;
						bool bCallShape = false;

						// Canonical transform: total = selected.step(total).
						const std::size_t ArgumentLoadIp = Cursor + 3u;
						const std::size_t TransformCallIp = ArgumentLoadIp + 3u;
						const std::size_t TransformStoreIp = TransformCallIp + 4u;
						const std::size_t TransformTailIp = TransformStoreIp + 3u;
						if (bSelectionShape && TransformTailIp <= CodeSize
							&& static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal
							&& Bytecode::ReadU16LE(Code.data() + Cursor + 1) == SelectedSlot
							&& static_cast<EOpcode>(Code[ArgumentLoadIp]) == EOpcode::LoadLocal
							&& static_cast<EOpcode>(Code[TransformCallIp]) == EOpcode::CallMethod
							&& Code[TransformCallIp + 3] == 1
							&& static_cast<EOpcode>(Code[TransformStoreIp]) == EOpcode::StoreLocal)
						{
							MethodCallIp = TransformCallIp;
							TailIp = TransformTailIp;
							AccumulatorSlot = Bytecode::ReadU16LE(Code.data() + ArgumentLoadIp + 1);
							ResultSlot = Bytecode::ReadU16LE(Code.data() + TransformStoreIp + 1);
							MethodNameIndex = Bytecode::ReadU16LE(Code.data() + TransformCallIp + 1);
							bCallShape = ResultSlot == AccumulatorSlot;
						}
						else
						{
							// Equivalent additive form: total = total + selected.step(k).
							// Restrict k to an i32 literal.  It is then immutable, has no
							// evaluation side effects, and the transform result can be proven
							// for every polymorphic receiver before the loop is collapsed.
							const std::size_t AdditiveReceiverIp = Cursor + 3u;
							const std::size_t AdditiveArgumentIp = AdditiveReceiverIp + 3u;
							const std::size_t AdditiveCallIp = AdditiveArgumentIp + 3u;
							const std::size_t AdditiveAddIp = AdditiveCallIp + 4u;
							const std::size_t AdditiveStoreIp = AdditiveAddIp + 1u;
							const std::size_t AdditiveTailIp = AdditiveStoreIp + 3u;
							if (bSelectionShape && AdditiveTailIp <= CodeSize
								&& static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal
								&& static_cast<EOpcode>(Code[AdditiveReceiverIp]) == EOpcode::LoadLocal
								&& Bytecode::ReadU16LE(Code.data() + AdditiveReceiverIp + 1) == SelectedSlot
								&& static_cast<EOpcode>(Code[AdditiveArgumentIp]) == EOpcode::ConstI32
								&& static_cast<EOpcode>(Code[AdditiveCallIp]) == EOpcode::CallMethod
								&& Code[AdditiveCallIp + 3] == 1
								&& static_cast<EOpcode>(Code[AdditiveAddIp]) == EOpcode::Add
								&& static_cast<EOpcode>(Code[AdditiveStoreIp]) == EOpcode::StoreLocal)
							{
								const std::uint16_t ArgumentConstant =
									Bytecode::ReadU16LE(Code.data() + AdditiveArgumentIp + 1);
								if (ArgumentConstant < Module->Constants.size()
									&& Module->Constants[ArgumentConstant].Kind == EConstantKind::I32)
								{
									MethodCallIp = AdditiveCallIp;
									TailIp = AdditiveTailIp;
									AccumulatorSlot = Bytecode::ReadU16LE(Code.data() + Cursor + 1);
									ResultSlot = Bytecode::ReadU16LE(Code.data() + AdditiveStoreIp + 1);
									MethodNameIndex = Bytecode::ReadU16LE(Code.data() + AdditiveCallIp + 1);
									AdditiveArgument = Module->Constants[ArgumentConstant].I32;
									bAdditiveAccumulator = true;
									bCallShape = ResultSlot == AccumulatorSlot;
								}
							}
						}
						if (bCallShape)
						{
							FCanonicalI32Loop Loop;
							auto* const Accumulator =
								std::get_if<std::int32_t>(BorrowLocal(AccumulatorSlot));
							const bool bReceiverSlotsSafe = std::all_of(
								ReceiverSlots.begin(), ReceiverSlots.begin() + ReceiverCount,
								[&](const std::uint16_t ReceiverSlot)
								{
									return ReceiverSlot != CounterSlot
											&& ReceiverSlot != AccumulatorSlot
											&& ReceiverSlot != SelectedSlot
											&& ReceiverSlot != ChoiceSlot;
								});
							if (Accumulator && ResultSlot == AccumulatorSlot
								&& bReceiverSlotsSafe
								&& TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
								&& std::none_of(
									ReceiverSlots.begin(), ReceiverSlots.begin() + ReceiverCount,
									[&](const std::uint16_t ReceiverSlot)
									{
										return ReceiverSlot == Loop.LimitSlot;
									})
								&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot)
								&& LoopDoesNotAliasMutatedLocal(Loop, SelectedSlot)
								&& LoopDoesNotAliasMutatedLocal(Loop, ChoiceSlot))
							{
								std::array<std::int32_t, 8> Addends{};
								std::array<std::int32_t, 8> Moduli{};
								std::array<FValue*, 8> ReceiverValues{};
								FValue* const SelectedValue = BorrowLocal(SelectedSlot);
								auto* const ChoiceValue =
									std::get_if<std::int32_t>(BorrowLocal(ChoiceSlot));
								bool bAllResolved = SelectedValue && ChoiceValue;
								const std::int32_t First = *Loop.Counter;
								if (First < Loop.Limit)
								{
									for (std::size_t ReceiverIndex = 0;
										bAllResolved && ReceiverIndex < ReceiverCount;
										++ReceiverIndex)
									{
										ReceiverValues[ReceiverIndex] =
											BorrowLocal(ReceiverSlots[ReceiverIndex]);
										FScriptObject* Object = nullptr;
										bool bHandled = false;
										if (!ReceiverValues[ReceiverIndex]
											|| !Vm->JitBorrowDirectI32TransformMethod(
												ReceiverSlots[ReceiverIndex], MethodNameIndex,
												static_cast<std::uint32_t>(MethodCallIp), Object,
												Addends[ReceiverIndex], Moduli[ReceiverIndex], bHandled))
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
								}
								// In the additive form the method receives an invariant literal,
								// rather than the accumulator.  Materialize each pure direct
								// transform result once, but decline the specialization if that
								// inner operation could fail: the generic path is then solely
								// responsible for the exact iteration and diagnostic location.
								if (bAllResolved && bAdditiveAccumulator)
								{
									for (std::size_t ReceiverIndex = 0;
										ReceiverIndex < ReceiverCount; ++ReceiverIndex)
									{
										std::int32_t TransformResult = 0;
										if (!Runtime::CheckedAdd(
												AdditiveArgument,
												Addends[ReceiverIndex],
												TransformResult)
											|| (Moduli[ReceiverIndex] == -1
												&& TransformResult == std::numeric_limits<std::int32_t>::min()))
										{
											bAllResolved = false;
											break;
										}
										Addends[ReceiverIndex] = Moduli[ReceiverIndex] == 0
											? TransformResult
											: TransformResult % Moduli[ReceiverIndex];
										// The inner modulo has been applied; the outer addition
										// itself is never modulo-reduced.
										Moduli[ReceiverIndex] = 0;
									}
								}
								// Assigning the selected interface local can release its previous
								// object. Collapse the assignments only when that initial object is
								// independently rooted by one of the receiver locals; otherwise a
								// destructor (and its observable side effects) would be delayed from
								// the first source iteration until the reduced loop exits.
								if (bAllResolved)
								{
									const auto* const InitialSelectedHandle =
										std::get_if<FObjectHandle>(SelectedValue);
									bAllResolved = InitialSelectedHandle && *InitialSelectedHandle
										&& std::any_of(
											ReceiverValues.begin(),
											ReceiverValues.begin() + ReceiverCount,
											[&](const FValue* const ReceiverValue)
											{
												const auto* const ReceiverHandle = ReceiverValue
															? std::get_if<FObjectHandle>(ReceiverValue)
													: nullptr;
												return ReceiverHandle && *ReceiverHandle
															&& *ReceiverHandle == *InitialSelectedHandle;
											});
								}
								if (bAllResolved)
								{
									const std::size_t IterationCount = First < Loop.Limit
										? static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - First)
										: 0u;
									bAllResolved = CanProveI32TransformSequenceNoThrow(
										*Accumulator, IterationCount, Addends.data(),
										Moduli.data(), ReceiverCount);
								}
								if (bAllResolved)
								{
									const std::size_t IterationCount = First < Loop.Limit
										? static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - First)
										: 0u;
									const bool bNoModulus = std::all_of(
										Moduli.begin(), Moduli.begin() + ReceiverCount,
										[](const std::int32_t Modulus)
										{
											return Modulus == 0;
										});
									const bool bNonNegative = std::all_of(
										Addends.begin(), Addends.begin() + ReceiverCount,
										[](const std::int32_t Addend)
										{
											return Addend >= 0;
										});
									const bool bNonPositive = std::all_of(
										Addends.begin(), Addends.begin() + ReceiverCount,
										[](const std::int32_t Addend)
										{
											return Addend <= 0;
										});
									const bool bPeriodicSelector = SelectorOp == EOpcode::BitAnd
										|| First >= 0;
									if (IterationCount != 0 && bNoModulus && bPeriodicSelector
										&& (bNonNegative || bNonPositive))
									{
										std::uint64_t CycleMagnitude = 0;
										std::uint64_t RemainderMagnitude = 0;
										const std::size_t SampleCount =
											std::min(ReceiverCount, IterationCount);
										for (std::size_t Offset = 0; Offset < SampleCount; ++Offset)
										{
											const std::int32_t Current = static_cast<std::int32_t>(
												static_cast<std::int64_t>(First) + Offset);
											const std::int32_t Choice = SelectorOp == EOpcode::BitAnd
												? (Current & SelectorOperand)
												: (Current % SelectorOperand);
											const std::size_t SelectedIndex = Choice >= 0
												&& Choice < ReceiverCountI32 - 1
												? static_cast<std::size_t>(Choice)
												: ReceiverCount - 1u;
											const std::int64_t SignedAddend = Addends[SelectedIndex];
											CycleMagnitude += static_cast<std::uint64_t>(
												bNonNegative ? SignedAddend : -SignedAddend);
											if (Offset < IterationCount % ReceiverCount)
											{
												RemainderMagnitude += static_cast<std::uint64_t>(
													bNonNegative ? SignedAddend : -SignedAddend);
											}
										}
										const std::uint64_t FullCycles = IterationCount / ReceiverCount;
										const std::uint64_t AvailableMagnitude = static_cast<std::uint64_t>(
											bNonNegative
												? static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())
													- static_cast<std::int64_t>(*Accumulator)
												: static_cast<std::int64_t>(*Accumulator)
													- static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()));
										const bool bCycleFits = RemainderMagnitude <= AvailableMagnitude
											&& (CycleMagnitude == 0
												|| FullCycles <= (AvailableMagnitude - RemainderMagnitude)
													/ CycleMagnitude);
										if (bCycleFits)
										{
											const std::uint64_t Magnitude =
												FullCycles * CycleMagnitude + RemainderMagnitude;
											const std::int64_t FinalValue = bNonNegative
												? static_cast<std::int64_t>(*Accumulator)
													+ static_cast<std::int64_t>(Magnitude)
												: static_cast<std::int64_t>(*Accumulator)
													- static_cast<std::int64_t>(Magnitude);
											const std::int32_t LastCounter = Loop.Limit - 1;
											const std::int32_t LastChoice = SelectorOp == EOpcode::BitAnd
												? (LastCounter & SelectorOperand)
												: (LastCounter % SelectorOperand);
											const std::size_t LastSelectedIndex = LastChoice >= 0
												&& LastChoice < ReceiverCountI32 - 1
												? static_cast<std::size_t>(LastChoice)
												: ReceiverCount - 1u;
											if (!Vm->JitCommitDirectI32TransformMethod(IterationCount))
											{
												return FailVmAt(MethodCallIp);
											}
											*Accumulator = static_cast<std::int32_t>(FinalValue);
											*SelectedValue = *ReceiverValues[LastSelectedIndex];
											*ChoiceValue = LastChoice;
											*Loop.Counter = Loop.Limit;
											Ip = Loop.ExitIp;
											continue;
										}
									}
									std::size_t Completed = 0;
									for (std::int64_t Current = First;
										Current < Loop.Limit; ++Current)
									{
										*Loop.Counter = static_cast<std::int32_t>(Current);
										const std::int32_t Choice = SelectorOp == EOpcode::BitAnd
											? (*Loop.Counter & SelectorOperand)
											: (*Loop.Counter % SelectorOperand);
										const std::size_t SelectedIndex = Choice >= 0
											&& Choice < ReceiverCountI32 - 1
											? static_cast<std::size_t>(Choice)
											: ReceiverCount - 1u;
										*SelectedValue = *ReceiverValues[SelectedIndex];
										*ChoiceValue = Choice;
										std::int32_t Sum = 0;
										if (!Runtime::CheckedAdd(
												*Accumulator, Addends[SelectedIndex], Sum)
											|| (Moduli[SelectedIndex] == -1
												&& Sum == std::numeric_limits<std::int32_t>::min()))
										{
											if (Completed != 0
												&& !Vm->JitCommitDirectI32TransformMethod(Completed))
											{
												return FailVmAt(MethodCallIp);
											}
											return ErrorAt(MethodCallIp, "integer overflow");
										}
										*Accumulator = Moduli[SelectedIndex] == 0
											? Sum
											: Sum % Moduli[SelectedIndex];
										++Completed;
									}
									if (!Vm->JitCommitDirectI32TransformMethod(Completed))
									{
										return FailVmAt(MethodCallIp);
									}
									*Loop.Counter = Loop.Limit;
									Ip = Loop.ExitIp;
									continue;
								}
							}
						}
					}
				}
				// Scalar-replace a newly constructed, non-escaping object in the exact
				// canonical reduction below:
				//   T value = T(); total += value.scalarProjection();
				// The deliberately narrow proof excludes inheritance, host state,
				// initialization code, destructors, interception and every later use of
				// the object local. Consequently neither identity nor lifetime work is
				// observable, while checked arithmetic still runs once per source
				// iteration and reports overflow at the same counter value.
				if (Op == EOpcode::NewObject && AfterIp + 3 <= CodeSize
					&& static_cast<EOpcode>(Code[AfterIp]) == EOpcode::StoreLocal
					&& Vm->JitCanUseDirectScalarMethod())
				{
					const std::uint16_t TypeIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					const std::uint16_t ObjectSlot = Bytecode::ReadU16LE(Code.data() + AfterIp + 1);
					const std::size_t AccumulatorIp =
						AfterIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
					const std::size_t ReceiverIp =
						AccumulatorIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
					const std::size_t CallIp = ReceiverIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
					const std::size_t AddIp =
						CallIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod);
					const std::size_t StoreIp = AddIp + Bytecode::InstructionSize(EOpcode::Add);
					const std::size_t TailIp = StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
					if (TypeIndex < Module->Types.size()
						&& TailIp <= CodeSize
						&& static_cast<EOpcode>(Code[AccumulatorIp]) == EOpcode::LoadLocal
						&& static_cast<EOpcode>(Code[ReceiverIp]) == EOpcode::LoadLocal
						&& Bytecode::ReadU16LE(Code.data() + ReceiverIp + 1) == ObjectSlot
						&& static_cast<EOpcode>(Code[CallIp]) == EOpcode::CallBoundMethod
						&& Code[CallIp + 3] == 0
						&& static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add
						&& static_cast<EOpcode>(Code[StoreIp]) == EOpcode::StoreLocal)
					{
						const std::uint16_t AccumulatorSlot =
							Bytecode::ReadU16LE(Code.data() + AccumulatorIp + 1);
						const std::uint16_t DestinationSlot =
							Bytecode::ReadU16LE(Code.data() + StoreIp + 1);
						const std::uint16_t MethodFunctionIndex =
							Bytecode::ReadU16LE(Code.data() + CallIp + 1);
						if (AccumulatorSlot == DestinationSlot
							&& AccumulatorSlot != ObjectSlot
							&& MethodFunctionIndex < Module->Functions.size())
						{
							const FBytecodeTypeDesc& Type = Module->Types[TypeIndex];
							const FBytecodeFunction& Method =
								Module->Functions[MethodFunctionIndex];
							const auto& MethodCode = Method.Code;
							const auto IsDirectScalarConstant = [](const EConstantKind Kind)
							{
								return Kind == EConstantKind::I32 || Kind == EConstantKind::U32
									|| Kind == EConstantKind::I64 || Kind == EConstantKind::U64
									|| Kind == EConstantKind::F32 || Kind == EConstantKind::F64;
							};
							const bool bTrivialFields = std::all_of(
								Type.Fields.begin(), Type.Fields.end(), [&](const FBytecodeFieldDesc& Field)
								{
									return Field.bHasDefault && !Field.bHasInitCode
											&& IsDirectScalarConstant(Field.DefaultValue.Kind);
								});
							if (!Type.bIsAbstract
								&& Type.BaseTypeIndices.empty() && Type.HostBaseTypeName.empty()
								&& Type.DestructorFunctionIndex == 0xFFFF && bTrivialFields
								&& !Method.bReturnsVoid && !Method.bReturnsRef
								&& Method.bIsMethod && !Method.bIsAbstract && !Method.bDeleted
								&& !BytecodeFunctionHasRefParams(Method)
								&& Method.ExceptionRegions.empty()
								&& Method.LocalCount >= Method.ParamCount
								&& Method.OwnerTypeIndex == TypeIndex
								&& Method.ParamCount == 0 && MethodCode.size() == 8
								&& static_cast<EOpcode>(MethodCode[0]) == EOpcode::LoadThisField
								&& static_cast<EOpcode>(MethodCode[3]) == EOpcode::LoadThisField
								&& static_cast<EOpcode>(MethodCode[6]) == EOpcode::Add
								&& static_cast<EOpcode>(MethodCode[7]) == EOpcode::Return)
							{
								const std::uint16_t LeftName = Bytecode::ReadU16LE(MethodCode.data() + 1);
								const std::uint16_t RightName = Bytecode::ReadU16LE(MethodCode.data() + 4);
								auto FindDefault = [&](const std::uint16_t NameIndex) -> const FBytecodeConstant*
								{
									if (NameIndex >= Module->StringPool.size())
									{
										return nullptr;
									}
									const std::string& Name = Module->StringPool[NameIndex];
									for (const FBytecodeFieldDesc& Field : Type.Fields)
									{
										if (Field.Name == Name && Field.bHasDefault
											&& !Field.bHasInitCode
											&& IsDirectScalarConstant(Field.DefaultValue.Kind))
										{
											return &Field.DefaultValue;
										}
									}
									return nullptr;
								};
								const FBytecodeConstant* const Left = FindDefault(LeftName);
								const FBytecodeConstant* const Right = FindDefault(RightName);
								bool bObjectEscapes = false;
								for (std::size_t ScanIp = TailIp; ScanIp < CodeSize;)
								{
									const EOpcode ScanOp = static_cast<EOpcode>(Code[ScanIp]);
									const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
									if (ScanSize == 0 || ScanSize > CodeSize - ScanIp)
									{
										bObjectEscapes = true;
										break;
									}
									const bool bReadsSlot = ScanSize >= 3
										&& (ScanOp == EOpcode::LoadLocal
											|| ScanOp == EOpcode::MakeLocalRef
											|| ScanOp == EOpcode::GetIndexRef
											|| ScanOp == EOpcode::GetIndexPathRef
											|| ScanOp == EOpcode::GetMapEntryRef
											|| ScanOp == EOpcode::IncLocal
											|| ScanOp == EOpcode::AddLocalImm
											|| ScanOp == EOpcode::JmpIfLocalGeImm)
										&& Bytecode::ReadU16LE(Code.data() + ScanIp + 1) == ObjectSlot;
									const bool bReadsAsSecondSlot = ScanOp == EOpcode::AddLocalLocal
										&& ScanSize >= 5
										&& (Bytecode::ReadU16LE(Code.data() + ScanIp + 1) == ObjectSlot
											|| Bytecode::ReadU16LE(Code.data() + ScanIp + 3) == ObjectSlot);
									bool bUnsafeBackEdge = false;
									if (ScanOp == EOpcode::Jmp || ScanOp == EOpcode::JmpIfFalse)
									{
										std::size_t TargetIp = 0;
										bUnsafeBackEdge =
											(!IsJumpTargetInRange(
												ScanIp + ScanSize,
												Bytecode::ReadI32LE(Code.data() + ScanIp + 1),
												CodeSize, TargetIp)
												|| TargetIp < TailIp)
											&& ScanIp != TailIp + Bytecode::InstructionSize(EOpcode::IncLocal);
									}
									else if (ScanOp == EOpcode::JmpIfLocalGeImm)
									{
										std::size_t TargetIp = 0;
										bUnsafeBackEdge = !IsJumpTargetInRange(
											ScanIp + ScanSize,
											Bytecode::ReadI32LE(Code.data() + ScanIp + 7),
											CodeSize, TargetIp)
											|| TargetIp < TailIp;
									}
									if (bReadsSlot || bReadsAsSecondSlot || bUnsafeBackEdge)
									{
										bObjectEscapes = true;
										break;
									}
									ScanIp += ScanSize;
								}
								FValue* const Accumulator = BorrowLocal(AccumulatorSlot);
								const std::uint16_t CounterSlot = TailIp + 3 <= CodeSize
									&& static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal
									? Bytecode::ReadU16LE(Code.data() + TailIp + 1)
									: 0xFFFF;
								FCanonicalI32Loop Loop;
								if (Left && Right && Accumulator && !bObjectEscapes
									&& TryMatchCanonicalI32Loop(Ip, TailIp, CounterSlot, Loop)
									&& LoopDoesNotAliasMutatedLocal(Loop, AccumulatorSlot))
								{
									FValue Projection = JitHost::Services().ConstantToValue(*Module, *Left);
									const FValue ProjectionAddend =
										JitHost::Services().ConstantToValue(*Module, *Right);
									EJitScalarValueKind ProjectionKind = EJitScalarValueKind::None;
									bool bProjectionOverflow = false;
									FValue AccumulatorProbe = *Accumulator;
									EJitScalarValueKind AccumulatorKind = EJitScalarValueKind::None;
									bool bAccumulatorOverflow = false;
									const bool bProjectionSupported = TryAddExactScalar(
										Projection, ProjectionAddend,
										ProjectionKind, bProjectionOverflow);
									const bool bAccumulatorSupported = bProjectionSupported
										&& TryAddExactScalar(
											AccumulatorProbe, Projection,
											AccumulatorKind, bAccumulatorOverflow)
										&& AccumulatorKind == ProjectionKind;
									if (bAccumulatorSupported)
									{
										const std::size_t IterationCount = static_cast<std::size_t>(
											static_cast<std::int64_t>(Loop.Limit) - *Loop.Counter);
										FValue ReducedAccumulator = *Accumulator;
										EJitScalarValueKind ReducedKind = EJitScalarValueKind::None;
										bool bReducedOverflow = false;
										if (!bProjectionOverflow
											&& TryAddExactIntegerRepeated(
												ReducedAccumulator, Projection, IterationCount,
												ReducedKind, bReducedOverflow)
											&& ReducedKind == ProjectionKind && !bReducedOverflow)
										{
											if (!Vm->JitCommitDirectScalarMethod(
													ProjectionKind, IterationCount))
											{
												return FailVm();
											}
											*Accumulator = std::move(ReducedAccumulator);
											*Loop.Counter = Loop.Limit;
											Ip = Loop.ExitIp;
											continue;
										}
										for (std::int64_t Current = *Loop.Counter;
											Current < Loop.Limit; ++Current)
										{
											*Loop.Counter = static_cast<std::int32_t>(Current);
											if (!Vm->JitCommitDirectScalarMethod(ProjectionKind))
											{
												return FailVm();
											}
											if (bProjectionOverflow)
											{
												return Error("integer overflow");
											}
											if (!TryAddExactScalar(
													*Accumulator, Projection,
													AccumulatorKind, bAccumulatorOverflow)
												|| bAccumulatorOverflow)
											{
												return Error("integer overflow");
											}
										}
										*Loop.Counter = Loop.Limit;
										Ip = Loop.ExitIp;
										continue;
									}
								}
							}
						}
					}
				}
				switch (Op)
				{
				case EOpcode::Nop:
					{
						Ip = AfterIp;
						break;
					}
				case EOpcode::ConstI32:
					{
						const auto& C = Module->Constants[Bytecode::ReadU16LE(Code.data() + Ip + 1)];
						if (C.Kind == EConstantKind::I32)
						{
							Stack.push_back(C.I32);
						}
						else if (C.Kind == EConstantKind::U32)
						{
							Stack.push_back(C.U32);
						}
						else
						{
							Stack.push_back(JitHost::Services().ConstantToValue(*Module, C));
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::ConstF64:
					{
						const auto& C = Module->Constants[Bytecode::ReadU16LE(Code.data() + Ip + 1)];
						if (C.Kind == EConstantKind::F64)
						{
							Stack.push_back(C.F64);
						}
						else
						{
							Stack.push_back(JitHost::Services().ConstantToValue(*Module, C));
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::ConstBool:
					{
						Stack.push_back(Code[Ip + 1] != 0);
						Ip = AfterIp;
						break;
					}
				case EOpcode::ConstStr:
					{
						Stack.push_back(JitHost::Services().ConstantToValue(*Module, Module->Constants[Bytecode::ReadU16LE(Code.data() + Ip + 1)]));
						Ip = AfterIp;
						break;
					}
				#include "VeroScriptJitLinearLoadLocal.inl"
				case EOpcode::TakeLocal:
					{
						FValue Value;
						if (!Vm->JitTakeLocal(Bytecode::ReadU16LE(Code.data() + Ip + 1), Value)) return FailVm();
						Stack.push_back(std::move(Value));
						Ip = AfterIp;
						break;
					}
				case EOpcode::StoreLocal:
					{
						FValue V = std::move(Stack.back());
						Stack.pop_back();
						if (!Vm->JitStoreLocal(Bytecode::ReadU16LE(Code.data() + Ip + 1), std::move(V)))
						{
							return FailVm();
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::LoadGlobal:
					{
						FValue V;
						if (!Vm->JitLoadGlobal(Bytecode::ReadU16LE(Code.data() + Ip + 1), V))
						{
							return FailVm();
						}
						Stack.push_back(std::move(V));
						Ip = AfterIp;
						break;
					}
				case EOpcode::StoreGlobal:
					{
						FValue V = std::move(Stack.back());
						Stack.pop_back();
						if (!Vm->JitStoreGlobal(Bytecode::ReadU16LE(Code.data() + Ip + 1), std::move(V)))
						{
							return FailVm();
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
					{
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto Imm = Op == EOpcode::IncLocal ? 1 : Bytecode::ReadI32LE(Code.data() + Ip + 3);
						if (!Vm->JitAddLocalImm(S, Imm))
						{
							return FailVm();
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						const auto D = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 3);
						if (!Vm->JitAddLocalLocal(D, S))
						{
							return FailVm();
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
					{
						FValue Right = std::move(Stack.back());
						Stack.pop_back();
						FValue Left = std::move(Stack.back());
						Stack.pop_back();
						FValue Result;
						Vm->JitSetActiveInstructionOffset(Ip);
						if (!Vm->JitEvalArithmetic(Op, Left, Right, Result))
						{
							return FailVm();
						}
						Stack.push_back(std::move(Result));
						Ip = AfterIp;
						break;
					}
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						FValue Right = std::move(Stack.back());
						Stack.pop_back();
						FValue Left = std::move(Stack.back());
						Stack.pop_back();
						FValue Result;
						Vm->JitSetActiveInstructionOffset(Ip);
						if (!Vm->JitEvalBitwise(Op, Left, Right, Result))
						{
							return FailVm();
						}
						Stack.push_back(std::move(Result));
						Ip = AfterIp;
						break;
					}
				case EOpcode::Eq:
				case EOpcode::Ne:
				case EOpcode::Lt:
				case EOpcode::Le:
				case EOpcode::Gt:
				case EOpcode::Ge:
					{
						FValue Right = std::move(Stack.back());
						Stack.pop_back();
						FValue Left = std::move(Stack.back());
						Stack.pop_back();
						bool Result = false;
						Vm->JitSetActiveInstructionOffset(Ip);
						if (!Vm->JitCompare(Op, Left, Right, Result))
						{
							return FailVm();
						}
						Stack.push_back(Result);
						Ip = AfterIp;
						break;
					}
				case EOpcode::Neg:
				case EOpcode::BitNot:
				case EOpcode::Not:
					{
						{
							FValue Result;
							Vm->JitSetActiveInstructionOffset(Ip);
							if (!Vm->JitEvalUnary(Op, Stack.back(), Result))
							{
								return FailVm();
							}
							Stack.back() = std::move(Result);
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::Dup:
					{
						Stack.push_back(Stack.back());
						Ip = AfterIp;
						break;
					}
				case EOpcode::Pop:
					{
						{
							FValue Discarded = std::move(Stack.back());
							Stack.pop_back();
							if (!Vm->JitReleaseOwnedValue(Discarded))
							{
								return FailVm();
							}
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::Call:
					{
						const auto C = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto N = Code[Ip + 3];
						FValue* A = nullptr;
						if (!CollectArgs(N, A))
						{
							return Error("JIT linear: Call underflow");
						}
						Vm->JitSetActiveInstructionOffset(Ip);
						if (!Vm->JitInvokeCall(C, A, N))
						{
							(void)ReleaseCollectedArgs(N);
							return FailVm();
						}
						FValue R;
						if (!Vm->JitTakeReturn(R))
						{
							(void)ReleaseCollectedArgs(N);
							return FailVm();
						}
						if (!ReleaseCollectedArgs(N))
						{
							(void)Vm->JitReleaseOwnedValue(R);
							return FailVm();
						}
						if (!Module->Functions[C].bReturnsVoid)
						{
							Stack.push_back(std::move(R));
						}
						else
						{
							Stack.push_back(std::int32_t{0});
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::CallBoundMethod:
					{
						const auto C = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto N = Code[Ip + 3];
						FValue* A = nullptr;
						if (!CollectArgs(N, A))
						{
							return Error("JIT linear: CallBoundMethod underflow");
						}
						if (Stack.empty())
						{
							(void)ReleaseCollectedArgs(N);
							return Error("JIT linear: CallBoundMethod missing this");
						}
						FValue ThisObj = std::move(Stack.back());
						Stack.pop_back();
						bool bFastArrayPush = false;
						if (N == 1 && C < Module->Functions.size())
						{
							const std::string& CalleeName = Module->Functions[C].Name;
							if (MethodLinkageBareLeaf(CalleeName) == "push")
							{
								if (!Vm->JitTryPushDynamicArrayScalar(
										ThisObj, A[0], bFastArrayPush))
								{
									(void)ReleaseCollectedArgs(N);
									(void)Vm->JitReleaseOwnedValue(ThisObj);
									return FailVm();
								}
							}
						}
						if (bFastArrayPush)
						{
							Vm->RecordJitArrayFastPathPush();
							const bool bArgsReleased = ReleaseCollectedArgs(N);
							const bool bReceiverReleased =
								Vm->JitReleaseOwnedValue(ThisObj);
							if (!bArgsReleased || !bReceiverReleased)
							{
								return FailVm();
							}
							Stack.push_back(std::int32_t{0});
							Ip = AfterIp;
							break;
						}
						if (!Vm->JitInvokeBoundMethod(C, ThisObj, A, N))
						{
							(void)ReleaseCollectedArgs(N);
							(void)Vm->JitReleaseOwnedValue(ThisObj);
							return FailVm();
						}
						FValue R;
						if (!Vm->JitTakeReturn(R))
						{
							(void)ReleaseCollectedArgs(N);
							(void)Vm->JitReleaseOwnedValue(ThisObj);
							return FailVm();
						}
						const bool bArgsReleased = ReleaseCollectedArgs(N);
						const bool bReceiverReleased =
							Vm->JitReleaseOwnedValue(ThisObj);
						if (!bArgsReleased || !bReceiverReleased)
						{
							(void)Vm->JitReleaseOwnedValue(R);
							return FailVm();
						}
						if (!Module->Functions[C].bReturnsVoid)
						{
							Stack.push_back(std::move(R));
						}
						else
						{
							Stack.push_back(std::int32_t{0});
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::NewObject:
				case EOpcode::NewUninitializedObject:
					{
						FValue Obj;
						if (!Vm->JitNewObject(Bytecode::ReadU16LE(Code.data() + Ip + 1), Obj,
							Op == EOpcode::NewUninitializedObject))
						{
							return FailVm();
						}
						Stack.push_back(std::move(Obj));
						Ip = AfterIp;
						break;
					}
				case EOpcode::ConstNull:
					{
						Stack.push_back(FObjectHandle{});
						Ip = AfterIp;
						break;
					}
				case EOpcode::LoadThis:
					{
						Stack.push_back(Vm->GetJitThisObject());
						Ip = AfterIp;
						break;
					}
				case EOpcode::LoadThisField:
					{
						FValue V;
						if (!Vm->JitLoadThisField(Bytecode::ReadU16LE(Code.data() + Ip + 1), V))
						{
							return FailVm();
						}
						Stack.push_back(std::move(V));
						Ip = AfterIp;
						break;
					}
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
					{
						FValue V = std::move(Stack.back());
						Stack.pop_back();
						const auto NameIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (!(Op == EOpcode::InitializeThisField
							? Vm->JitInitializeThisField(NameIndex, std::move(V))
							: Vm->JitStoreThisField(NameIndex, std::move(V))))
						{
							return FailVm();
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::LoadField:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						FValue Obj = std::move(Stack.back());
						Stack.pop_back();
						FValue V;
						if (!Vm->JitLoadField(Obj, Name, V))
						{
							(void)Vm->JitReleaseOwnedValue(Obj);
							return FailVm();
						}
						if (!Vm->JitReleaseOwnedValue(Obj))
						{
							(void)Vm->JitReleaseOwnedValue(V);
							return FailVm();
						}
						Stack.push_back(std::move(V));
						Ip = AfterIp;
						break;
					}
				case EOpcode::LoadFieldKeep:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						FValue Obj = Stack.back();
						FValue V;
						if (!Vm->JitLoadField(Obj, Name, V))
						{
							return FailVm();
						}
						Stack.push_back(std::move(V));
						Ip = AfterIp;
						break;
					}
				case EOpcode::StoreField:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						FValue V = std::move(Stack.back());
						Stack.pop_back();
						FValue Obj = std::move(Stack.back());
						Stack.pop_back();
						if (!Vm->JitStoreField(Obj, Name, V))
						{
							(void)Vm->JitReleaseOwnedValue(Obj);
							(void)Vm->JitReleaseOwnedValue(V);
							return FailVm();
						}
						if (!Vm->JitReleaseOwnedValue(Obj))
						{
							(void)Vm->JitReleaseOwnedValue(V);
							return FailVm();
						}
						Stack.push_back(std::move(V));
						Ip = AfterIp;
						break;
					}
				case EOpcode::IncField:
					{
						FValue Obj = std::move(Stack.back());
						Stack.pop_back();
						if (!Vm->JitIncField(Obj, Bytecode::ReadU16LE(Code.data() + Ip + 1)))
						{
							(void)Vm->JitReleaseOwnedValue(Obj);
							return FailVm();
						}
						if (!Vm->JitReleaseOwnedValue(Obj))
						{
							return FailVm();
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::CloneStruct:
					{
						if (!Vm->JitCloneStruct(Stack.back()))
						{
							return FailVm();
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::NewDynamicArray:
				case EOpcode::NewMap:
				case EOpcode::NewSet:
				case EOpcode::NewQueue:
				case EOpcode::NewStack:
				case EOpcode::NewOrderedMap:
				case EOpcode::NewDeque:
				case EOpcode::NewChannel:
				case EOpcode::NewNativeArray:
					{
						FValue V;
						if (!Vm->JitNewContainer(Op, Bytecode::ReadU16LE(Code.data() + Ip + 1), V))
						{
							return FailVm();
						}
						if (Op == EOpcode::NewDynamicArray && bUseScalarArrayStorage)
						{
							(void)Vm->JitEnableScalarArrayStorage(V);
						}
						else if (Op == EOpcode::NewMap && bUseScalarMapStorage)
						{
							(void)Vm->JitEnableScalarMapStorage(V);
						}
						else if (Op == EOpcode::NewSet && bUseScalarSetStorage)
						{
							(void)Vm->JitEnableScalarSetStorage(V);
						}
						Stack.push_back(std::move(V));
						Ip = AfterIp;
						break;
					}
				case EOpcode::GetIndexCopy:
					{
						FValue Index = std::move(Stack.back());
						Stack.pop_back();
						FValue Container = std::move(Stack.back());
						Stack.pop_back();
						FValue ScalarMapValue;
						bool bFastScalarMapGet = false;
						if (!Vm->JitTryGetScalarMap(
								Container, Index, ScalarMapValue, bFastScalarMapGet))
						{
							(void)Vm->JitReleaseOwnedValue(Container);
							return FailVm();
						}
						if (bFastScalarMapGet)
						{
							Vm->RecordJitScalarMapFastPathGet();
							if (!Vm->JitReleaseOwnedValue(Container))
							{
								return FailVm();
							}
							Stack.push_back(std::move(ScalarMapValue));
							Ip = AfterIp;
							break;
						}
						bool bFastArrayGet = false;
						if (const auto* const I32Index = std::get_if<std::int32_t>(&Index))
						{
							FValue Value;
							if (!Vm->JitTryGetDynamicArrayScalar(
									Container, *I32Index, Value, bFastArrayGet))
							{
								(void)Vm->JitReleaseOwnedValue(Container);
								return FailVm();
							}
							if (bFastArrayGet)
							{
								Vm->RecordJitArrayFastPathGet();
								if (!Vm->JitReleaseOwnedValue(Container))
								{
									return FailVm();
								}
								Stack.push_back(std::move(Value));
								Ip = AfterIp;
								break;
							}
						}
						FValue V;
						if (!Vm->JitGetIndex(Container, Index, V))
						{
							(void)Vm->JitReleaseOwnedValue(Container);
							return FailVm();
						}
						if (!Vm->JitReleaseOwnedValue(Container))
						{
							(void)Vm->JitReleaseOwnedValue(V);
							return FailVm();
						}
						Stack.push_back(std::move(V));
						Ip = AfterIp;
						break;
					}
				case EOpcode::SetIndex:
					{
						FValue Value = std::move(Stack.back());
						Stack.pop_back();
						FValue Index = std::move(Stack.back());
						Stack.pop_back();
						FValue Container = std::move(Stack.back());
						Stack.pop_back();
						bool bFastScalarMapSet = false;
						if (!Vm->JitTrySetScalarMap(
								Container, Index, Value, bFastScalarMapSet))
						{
							(void)Vm->JitReleaseOwnedValue(Container);
							(void)Vm->JitReleaseOwnedValue(Value);
							return FailVm();
						}
						if (bFastScalarMapSet)
						{
							Vm->RecordJitScalarMapFastPathSet();
							// A handled typed-map store proves Value is one of the raw
							// scalar alternatives. Its ordinary variant destruction is
							// sufficient; the general ownership walker is unnecessary.
							Stack.push_back(std::move(Container));
							Ip = AfterIp;
							break;
						}
						bool bFastArraySet = false;
						if (const auto* const I32Index = std::get_if<std::int32_t>(&Index))
						{
							if (!Vm->JitTrySetDynamicArrayScalar(
									Container, *I32Index, Value, bFastArraySet))
							{
								(void)Vm->JitReleaseOwnedValue(Value);
								(void)Vm->JitReleaseOwnedValue(Container);
								return FailVm();
							}
						}
						if (bFastArraySet)
						{
							Vm->RecordJitArrayFastPathSet();
							// The specialized setter handled only i32/u32/f64 values,
							// none of which require VM ownership teardown.
							Stack.push_back(std::move(Container));
							Ip = AfterIp;
							break;
						}
						FValue Out;
						if (!Vm->JitSetIndex(Container, Index, Value, Out) || Vm->HasError())
						{
							(void)Vm->JitReleaseOwnedValue(Container);
							(void)Vm->JitReleaseOwnedValue(Value);
							return FailVm();
						}
						const bool bValueReleased = Vm->JitReleaseOwnedValue(Value);
						const bool bContainerReleased =
							Vm->JitReleaseOwnedValue(Container);
						if (!bValueReleased || !bContainerReleased)
						{
							(void)Vm->JitReleaseOwnedValue(Out);
							return FailVm();
						}
						Stack.push_back(std::move(Out));
						Ip = AfterIp;
						break;
					}
				case EOpcode::CallMethod:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto N = Code[Ip + 3];
						FValue* A = nullptr;
						if (!CollectArgs(N, A))
						{
							return Error("JIT linear: CallMethod underflow");
						}
						if (Stack.empty())
						{
							DropCollectedArgs(N);
							return Error("JIT linear: CallMethod missing receiver");
						}
						FValue ThisObj = std::move(Stack.back());
						Stack.pop_back();
						bool bFastArrayPush = false;
						if (N == 1 && Name < Module->StringPool.size() && Module->StringPool[Name] == "push")
						{
							if (!Vm->JitTryPushDynamicArrayScalar(
									ThisObj, A[0], bFastArrayPush))
							{
								DropCollectedArgs(N);
								(void)Vm->JitReleaseOwnedValue(ThisObj);
								return FailVm();
							}
						}
						if (bFastArrayPush)
						{
							Vm->RecordJitArrayFastPathPush();
							DropCollectedArgs(N);
							Stack.push_back(std::move(ThisObj));
							Ip = AfterIp;
							break;
						}
						if (!Vm->JitCallMethod(
								Name, ThisObj, A, N, static_cast<std::uint32_t>(Ip)))
						{
							DropCollectedArgs(N);
							(void)Vm->JitReleaseOwnedValue(ThisObj);
							return FailVm();
						}
						FValue R;
						if (!Vm->JitTakeReturn(R))
						{
							DropCollectedArgs(N);
							(void)Vm->JitReleaseOwnedValue(ThisObj);
							return FailVm();
						}
						DropCollectedArgs(N);
						if (!Vm->JitReleaseOwnedValue(ThisObj))
						{
							(void)Vm->JitReleaseOwnedValue(R);
							return FailVm();
						}
						Stack.push_back(std::move(R));
						Ip = AfterIp;
						break;
					}
				case EOpcode::CallNative:
					{
						const auto NativeIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto N = Code[Ip + 3];
						FValue* A = nullptr;
						if (!CollectArgs(N, A))
						{
							return Error("JIT linear: CallNative underflow");
						}
						FValue R;
						if (!Vm->JitCallNative(NativeIndex, A, N, R))
						{
							(void)ReleaseCollectedArgs(N);
							return FailVm();
						}
						if (!ReleaseCollectedArgs(N))
						{
							(void)Vm->JitReleaseOwnedValue(R);
							return FailVm();
						}
						Stack.push_back(std::move(R));
						Ip = AfterIp;
						break;
					}
				case EOpcode::MakeClosure:
					{
						const auto C = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto N = Code[Ip + 3];
						FValue* Caps = nullptr;
						if (!CollectArgs(N, Caps))
						{
							return Error("JIT linear: MakeClosure underflow");
						}
						FValue Closure;
						if (!Vm->JitMakeClosure(C, Caps, N, Closure))
						{
							(void)ReleaseCollectedArgs(N);
							return FailVm();
						}
						if (!ReleaseCollectedArgs(N))
						{
							(void)Vm->JitReleaseOwnedValue(Closure);
							return FailVm();
						}
						Stack.push_back(std::move(Closure));
						Ip = AfterIp;
						break;
					}
				case EOpcode::CallIndirect:
					{
						const auto N = Code[Ip + 1];
						if (Stack.empty())
						{
							return Error("JIT linear: CallIndirect missing callee");
						}
						FValue Callee = std::move(Stack.back());
						Stack.pop_back();
						FValue* A = nullptr;
						if (!CollectArgs(N, A))
						{
							(void)Vm->JitReleaseOwnedValue(Callee);
							return Error("JIT linear: CallIndirect underflow");
						}
						if (!Vm->JitCallIndirect(Callee, A, N))
						{
							(void)ReleaseCollectedArgs(N);
							(void)Vm->JitReleaseOwnedValue(Callee);
							return FailVm();
						}
						if (Vm->WasExitRequested())
						{
							const bool bArgsReleased = ReleaseCollectedArgs(N);
							const bool bCalleeReleased =
								Vm->JitReleaseOwnedValue(Callee);
							const bool bLeft = LeaveFrame();
							return bArgsReleased && bCalleeReleased && bLeft
								&& !Vm->HasError();
						}
						FValue R;
						if (!Vm->JitTakeReturn(R))
						{
							(void)ReleaseCollectedArgs(N);
							(void)Vm->JitReleaseOwnedValue(Callee);
							return FailVm();
						}
						const bool bArgsReleased = ReleaseCollectedArgs(N);
						const bool bCalleeReleased =
							Vm->JitReleaseOwnedValue(Callee);
						if (!bArgsReleased || !bCalleeReleased)
						{
							(void)Vm->JitReleaseOwnedValue(R);
							return FailVm();
						}
						Stack.push_back(std::move(R));
						Ip = AfterIp;
						break;
					}
				case EOpcode::CallBuiltin:
					{
						const auto B = static_cast<EBuiltinId>(Code[Ip + 1]);
						const auto N = Code[Ip + 2];
						FValue* A = nullptr;
						if (!CollectArgs(N, A))
						{
							return Error("JIT linear: CallBuiltin underflow");
						}
						bool bHasResult = false;
						FValue Result;
						if (!Vm->JitInvokeBuiltin(B, A, N, bHasResult, Result))
						{
							(void)ReleaseCollectedArgs(N);
							return FailVm();
						}
						if (!ReleaseCollectedArgs(N))
						{
							(void)Vm->JitReleaseOwnedValue(Result);
							return FailVm();
						}
						if (bHasResult)
						{
							Stack.push_back(std::move(Result));
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::Jmp:
					{
						std::size_t T = 0;
						if (!IsJumpTargetInRange(
								AfterIp,
								Bytecode::ReadI32LE(Code.data() + Ip + 1),
								CodeSize,
								T))
						{
							return Error("JIT linear: jump out of range");
						}
						Ip = T;
						break;
					}
				case EOpcode::JmpIfFalse:
					{
						bool C = false;
						if (!Vm->JitEvaluateCondition(Stack.back(), C))
						{
							return FailVm();
						}
						Stack.pop_back();
						if (!C)
						{
							std::size_t T = 0;
							if (!IsJumpTargetInRange(
									AfterIp,
									Bytecode::ReadI32LE(Code.data() + Ip + 1),
									CodeSize,
									T))
							{
								return Error("JIT linear: jump out of range");
							}
							Ip = T;
						}
						else
						{
							Ip = AfterIp;
						}
						break;
					}
				case EOpcode::JmpIfLocalGeImm:
					{
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto I = Bytecode::ReadI32LE(Code.data() + Ip + 3);
						bool Jump = false;
						if (!Vm->JitLocalGeImm(S, I, Jump))
						{
							return FailVm();
						}
						if (Jump)
						{
							std::size_t T = 0;
							if (!IsJumpTargetInRange(
									AfterIp,
									Bytecode::ReadI32LE(Code.data() + Ip + 7),
									CodeSize,
									T))
							{
								return Error("JIT linear: jump out of range");
							}
							Ip = T;
						}
						else
						{
							Ip = AfterIp;
						}
						break;
					}
				case EOpcode::MakeLocalRef:
					{
						FValue Ref;
						if (!Vm->JitMakeLocalRef(Bytecode::ReadU16LE(Code.data() + Ip + 1), Ref))
						{
							return FailVm();
						}
						Stack.push_back(std::move(Ref));
						Ip = AfterIp;
						break;
					}
				case EOpcode::MakeGlobalRef:
					{
						FValue Ref;
						if (!Vm->JitMakeGlobalRef(Bytecode::ReadU16LE(Code.data() + Ip + 1), Ref))
						{
							return FailVm();
						}
						Stack.push_back(std::move(Ref));
						Ip = AfterIp;
						break;
					}
				case EOpcode::MakeThisFieldRef:
					{
						FValue Ref;
						if (!Vm->JitMakeThisFieldRef(Bytecode::ReadU16LE(Code.data() + Ip + 1), Ref))
						{
							return FailVm();
						}
						Stack.push_back(std::move(Ref));
						Ip = AfterIp;
						break;
					}
				case EOpcode::MakeFieldRef:
					{
						FValue Obj = std::move(Stack.back());
						Stack.pop_back();
						FValue Ref;
						if (!Vm->JitMakeFieldRef(
								std::move(Obj),
								Bytecode::ReadU16LE(Code.data() + Ip + 1),
								Ref))
						{
							return FailVm();
						}
						Stack.push_back(std::move(Ref));
						Ip = AfterIp;
						break;
					}
				case EOpcode::GetIndexRef:
				case EOpcode::GetIndexPathRef:
					{
						FValue Index = std::move(Stack.back());
						Stack.pop_back();
						FValue Ref;
						if (!Vm->JitGetIndexRef(
								Bytecode::ReadU16LE(Code.data() + Ip + 1),
								std::move(Index),
								Ref, Op == EOpcode::GetIndexRef))
						{
							return FailVm();
						}
						Stack.push_back(std::move(Ref));
						Ip = AfterIp;
						break;
					}
				case EOpcode::GetMapEntryRef:
					{
						FValue Key = std::move(Stack.back());
						Stack.pop_back();
						FValue Ref;
						if (!Vm->JitGetMapEntryRef(
								Bytecode::ReadU16LE(Code.data() + Ip + 1),
								std::move(Key),
								Ref))
						{
							return FailVm();
						}
						Stack.push_back(std::move(Ref));
						Ip = AfterIp;
						break;
					}
				case EOpcode::BindLocal:
					{
						FValue V = std::move(Stack.back());
						Stack.pop_back();
						if (!Vm->JitBindLocal(Bytecode::ReadU16LE(Code.data() + Ip + 1), std::move(V)))
						{
							return FailVm();
						}
						Ip = AfterIp;
						break;
					}
				case EOpcode::Throw:
					{
						if (Stack.empty())
						{
							return Error("JIT linear: throw requires value");
						}
						FValue Exception = std::move(Stack.back());
						Stack.pop_back();
						if (!DrainOperandStack())
						{
							(void)Vm->JitReleaseOwnedValue(Exception);
							return FailVm();
						}
						std::size_t HandlerOffset = 0;
						if (!Vm->JitHandleLinearThrow(
								std::move(Exception), Ip, HandlerOffset))
						{
							return FailVm();
						}
						std::size_t CatchExitIp = HandlerOffset;
						bool bCatchLoopHandled = false;
						if (!TryRunI32CatchAccumulatorLoop(
								Ip, HandlerOffset, CatchExitIp, bCatchLoopHandled))
						{
							return false;
						}
						Ip = bCatchLoopHandled ? CatchExitIp : HandlerOffset;
						break;
					}
				case EOpcode::Return:
					{
						if (Function.bReturnsVoid)
						{
							return LeaveFrame();
						}
						if (Stack.empty())
						{
							return Error("JIT linear: missing return value");
						}
						{
							FValue Result = std::move(Stack.back());
							Stack.pop_back();
							if (!Vm->PrepareReturn(Result))
							{
								(void)Vm->JitReleaseOwnedValue(Result);
								return FailVm();
							}
							// Drain leftover temps before releasing locals (shared object views).
							if (!DrainOperandStack())
							{
								(void)Vm->JitReleaseOwnedValue(Result);
								bFrameLive = false;
								(void)Vm->JitLeaveLinearFrame();
								return false;
							}
							bFrameLive = false;
							if (!Vm->JitLeaveLinearFrame())
							{
								(void)Vm->JitReleaseOwnedValue(Result);
								Vm->JitRecycleLinearScratch(Stack);
								if (bArgHeapLeased)
								{
									Vm->JitRecycleLinearScratch(ArgHeap);
								}
								return false;
							}
							Vm->JitRecycleLinearScratch(Stack);
							if (bArgHeapLeased)
							{
								Vm->JitRecycleLinearScratch(ArgHeap);
							}
							return Vm->JitPushReturn(std::move(Result));
						}
					}
				default:
					return Error("JIT linear: unsupported opcode at runtime");
				}
			}
			if (Function.bReturnsVoid)
			{
				return LeaveFrame();
			}
			return Error("JIT linear: fell off function end");
		}

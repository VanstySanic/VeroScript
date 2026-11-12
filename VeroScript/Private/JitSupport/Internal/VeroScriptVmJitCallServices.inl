// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		//~ Begin IJitCallServices Interface
		bool InvokeBuiltin(const EBuiltinId Builtin, const FValue* const Arguments,
			const std::uint8_t ArgumentCount, bool& OutHasResult, FValue& OutResult) noexcept override
		{
			OutHasResult = false;
			bool bHasResult = false;
			const bool bOk = CallService(Arguments, ArgumentCount, OutResult, false, [&](FValue& Value)
			{
				return Vm.JitInvokeBuiltin(Builtin, Arguments, ArgumentCount, bHasResult, Value);
			});
			OutHasResult = bOk && bHasResult;
			return bOk;
		}

		bool InvokeNative(const std::uint16_t NativeIndex, const FValue* const Arguments,
			const std::uint8_t ArgumentCount, FValue& OutResult) noexcept override
		{
			return CallService(Arguments, ArgumentCount, OutResult, false, [&](FValue& Value)
			{
				return Vm.JitCallNative(NativeIndex, Arguments, ArgumentCount, Value);
			});
		}

		bool InvokeMethod(const std::uint16_t NameIndex, const FValue& Receiver,
			const FValue* const Arguments, const std::uint8_t ArgumentCount,
			const std::uint32_t CallSiteOffset, FValue& OutResult) noexcept override
		{
			return CallService(Arguments, ArgumentCount, OutResult, true, [&](FValue&)
			{
				return Vm.JitCallMethod(NameIndex, Receiver, Arguments, ArgumentCount, CallSiteOffset);
			});
		}

		bool InvokeIndirect(const FValue& Callable, const FValue* const Arguments,
			const std::uint8_t ArgumentCount, FValue& OutResult) noexcept override
		{
			return CallService(Arguments, ArgumentCount, OutResult, true, [&](FValue&)
			{
				return Vm.JitCallIndirect(Callable, Arguments, ArgumentCount);
			});
		}

		bool MakeClosure(const std::uint16_t FunctionIndex, const FValue* const Captures,
			const std::uint8_t CaptureCount, FValue& OutClosure) noexcept override
		{
			return ProduceValue(OutClosure, [&](FValue& Value)
			{
				if (CaptureCount != 0 && !Captures)
				{
					ReportError("missing JIT closure captures");
					return false;
				}
				if (Vm.ActiveModule && FunctionIndex < Vm.ActiveModule->Functions.size() && Vm.ActiveModule->Functions[FunctionIndex].bIsMethod && !ValidateFrame(FrameToken))
				{
					return false;
				}
				return Vm.JitMakeClosure(FunctionIndex, Captures, CaptureCount, Value);
			});
		}

		bool InterpretInvocation(const FValue* const Arguments, const std::uint8_t ArgumentCount,
			const FObjectHandle& Receiver, FValue& OutResult) noexcept override
		{
			const std::size_t DepthBefore = Vm.Frames.size();
			const std::size_t StackBefore = Vm.OperandStack.size();
			const std::size_t SavedStop = Vm.StopFrameDepth;
			std::vector<FValue> Prepared;
			const bool bOk = Guard([&]()
			{
				if (FrameToken.Identity != 0 || Vm.ActiveJitCallDepth == 0
					|| InvocationFunctionIndex >= static_cast<std::size_t>(0x10000))
				{
					ReportError("invalid interpreter invocation from JIT");
					return false;
				}
				const auto Index = static_cast<std::uint16_t>(InvocationFunctionIndex);
				if (!CheckExecutionGovernance() || !PrepareArguments(Index, Receiver, Arguments, ArgumentCount, Prepared)) return false;
				const auto& Function = Vm.ActiveModule->Functions[Index];
				// The tagged frame represents the already-counted backend invocation.
				// Interpreter return/unwind owns its normal locals and return handling.
				if (!Vm.EnterJitRuntimeFrame(&Function, Function.LocalCount, Prepared.data(), ArgumentCount,
											 Receiver, AllocateJitProgramIdentity()))
				{
					return false;
				}
				Vm.StopFrameDepth = DepthBefore;
				if (!Vm.Run() || HasError() || HasPendingControlTransfer()) return false;
				if (Vm.Frames.size() != DepthBefore || Vm.OperandStack.size() != StackBefore + 1)
				{
					ReportError("interpreted JIT invocation left unbalanced execution state");
					return false;
				}
				return Vm.AssignValue(OutResult, Vm.Pop(), {});
			});
			Vm.StopFrameDepth = SavedStop;
			if (!bOk)
			{
				(void)Guard([&]()
							{
								return Vm.DiscardExecutionAbove(DepthBefore, StackBefore, {});
							});
			}
			const bool bReleased = Guard([&]()
										 {
											 return Vm.ReleaseValueList(Prepared, {});
										 });
			return bOk && bReleased;
		}

		bool ResolveMethod(const FObjectHandle& Receiver, const std::uint16_t NameIndex,
			const std::uint8_t ArgumentCount, const std::uint32_t CallSiteOffset,
			std::uint16_t& OutFunctionIndex, bool& OutHandled) noexcept override
		{
			OutFunctionIndex = 0xFFFF;
			OutHandled = false;
			return Guard([&]()
			{
				if (HasInstanceInterceptor() || !Receiver || !Vm.ActiveModule) return true;
				if (!ValidateFrame(FrameToken)) return false;
				if (!Vm.ResolveScriptObjectMethod(Receiver, NameIndex, ArgumentCount, CallSiteOffset,
												  Vm.Frames.back(), OutFunctionIndex, {}))
				{
					return false;
				}
				OutHandled = OutFunctionIndex < Vm.ActiveModule->Functions.size();
				return true;
			});
		}

		FJitCallDepthBudget BorrowCallDepthBudget() noexcept override
		{
			const auto InterpreterDepth = Vm.Frames.size() - Vm.ActiveBackendFrameCredits;
			const auto TotalLimit = InterpreterDepth < Vm.CallDepthLimit ? Vm.CallDepthLimit - InterpreterDepth : 0;
			const auto NativeLimit = Vm.BackendCache ? Vm.BackendCache->GetDescription().MaxInvocationDepth : 0;
			const auto DirectLimit = Vm.BackendCache ? Vm.BackendCache->GetDescription().MaxDirectScalarDepth : 0;
			return {&Vm.ActiveJitCallDepth, NativeLimit, TotalLimit, DirectLimit ? DirectLimit : NativeLimit};
		}

		bool EnterOptimizedCall() noexcept override
		{
			return Guard([&]()
			{
				if (!CheckExecutionGovernance()) return false;
				if (Vm.GetLogicalCallDepth() >= Vm.CallDepthLimit)
				{
					Vm.ReportErrorId("diag.call_stack_overflow");
					return false;
				}
				++Vm.ActiveJitCallDepth;
				++OptimizedCallDepth;
				return true;
			});
		}

		void LeaveOptimizedCall() noexcept override
		{
			if (OptimizedCallDepth == 0)
			{
				ReportError("unbalanced optimized call scope");
				return;
			}
			--OptimizedCallDepth;
			--Vm.ActiveJitCallDepth;
		}

		bool HasInstanceInterceptor() const noexcept override
		{
			return static_cast<bool>(Vm.InstanceCallInterceptor);
		}

		bool HasMutationObserver() const noexcept override
		{
			return static_cast<bool>(Vm.MutationCallback);
		}

		std::size_t GetRemainingCallDepth() const noexcept override
		{
			const auto Depth = Vm.GetLogicalCallDepth();
			return Depth < Vm.CallDepthLimit ? Vm.CallDepthLimit - Depth : 0;
		}
		//~ End IJitCallServices Interface

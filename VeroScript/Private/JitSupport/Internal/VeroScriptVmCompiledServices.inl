// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		ICompiledFunctionServices* GetCompiledFunctionServices() noexcept override
		{
			return this;
		}

		bool EnableScalarBlocks() noexcept override
		{
			return Guard([&]()
			{
				if (bCompiledActive || Vm.ExternalExecutionCheck || Vm.Debugger || Vm.MutationCallback || !Vm.ActiveModule || InvocationFunctionIndex >= Vm.ActiveModule->Functions.size())
				{
					return false;
				}
				const auto& Function = Vm.ActiveModule->Functions[InvocationFunctionIndex];
				if (Function.bReturnsRef || BytecodeFunctionHasRefParams(Function) || !Function.ExceptionRegions.empty() || Function.bUsesLocalStaticStorage)
				{
					return false;
				}
				ScalarBlockState = std::make_unique<FScalarBlockState>();
				return true;
			});
		}

		bool Begin(FJitCallContext& Context) noexcept override
		{
			return Guard([&]()
			{
				if (bCompiledActive || !Context.bEntered || Context.FunctionIndex != InvocationFunctionIndex)
				{
					ReportError("invalid compiled function activation");
					return false;
				}
				CompiledFrameBase = Vm.Frames.size();
				CompiledStackBase = Vm.OperandStack.size();
				CompiledContext = &Context;
				bCompiledActive = true;
				bCompiledReturned = false;
				return Enter({Context.FunctionIndex, nullptr}, Context.Arguments, Context.ArgumentCount, Context.Receiver, FrameToken);
			});
		}

		bool Finish(FJitCallContext& Context) noexcept override
		{
			if (!bCompiledActive) return !HasError();
			return Guard([&]()
			{
				if (CompiledContext != &Context)
				{
					ReportError("compiled activation context mismatch");
					return false;
				}
				const bool bReturned = bCompiledReturned && !HasError() && !HasPendingControlTransfer();
				bCompiledActive = false;
				bCompiledReturned = false;
				CompiledContext = nullptr;
				FrameToken = {};
				bool bFinished = true;
				if (bReturned)
				{
					bFinished = Vm.LeaveJitRuntimeFrame(&Vm.ActiveModule->Functions[InvocationFunctionIndex]);
				}
				const bool bDiscarded = Vm.DiscardExecutionAbove(CompiledFrameBase, CompiledStackBase, {});
				return bFinished && bDiscarded;
			});
		}

		int Execute(const std::size_t Offset) noexcept override
		{
			int Result = -1;
			const bool Ok = Guard([&]()
			{
				return ScalarBlockState
				    ? ExecuteScalarInstruction(Offset, Result)
				    : ExecuteInstruction(Offset, Result);
			});
			return Ok && !HasError() && !HasPendingControlTransfer() ? Result : -1;
		}

		int ExecuteBlock(const FCompiledInstruction* Instructions, const std::size_t Count) noexcept override
		{
			int Result = -1;
			const bool Ok = Guard([&]()
			{
				if (!Instructions || Count == 0) return false;
				for (std::size_t Index = 0; Index < Count; ++Index)
				{
					const bool StepOk = ScalarBlockState ? ExecuteScalarInstruction(Instructions[Index].Offset, Result)
					                                     : ExecuteInstruction(Instructions[Index].Offset, Result);
					if (!StepOk || HasError() || HasPendingControlTransfer())
					{
						return false;
					}
					if (Result != 0) return true;
				}
				return true;
			});
			return Ok && !HasError() && !HasPendingControlTransfer() ? Result : -1;
		}

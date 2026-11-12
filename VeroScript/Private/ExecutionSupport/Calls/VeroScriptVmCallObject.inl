// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		const auto* ObjectHandle = std::get_if<FObjectHandle>(&ThisValue);
		if (!ObjectHandle || !*ObjectHandle)
		{
			ReportError("method call requires object", SourceLocation);
			return false;
		}
		// The caller retains ThisValue for the complete dispatch. Do not take an
		// extra shared-ownership copy on every dynamic script-object call.
		const FObjectHandle& ThisObject = *ObjectHandle;
		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		if (!ResolveScriptObjectMethod(
				ThisObject,
				MethodNameIndex,
				ArgumentCount,
				CallSiteOffset,
				Frame,
				FunctionIndex,
				SourceLocation))
		{
			return false;
		}
		if (!InstanceCallInterceptor)
		{
			const std::size_t DepthBefore = Frames.size();
			const std::size_t StackBefore = OperandStack.size();
			if (!CallFunctionAt(FunctionIndex, Arguments, ArgumentCount, ThisObject))
			{
				return false;
			}
			return !bRunNestedToCompletion
				|| JitFinishNestedCall(DepthBefore, StackBefore);
		}
		return InvokeResolvedInstanceMethod(
			FunctionIndex,
			ThisObject,
			Arguments,
			ArgumentCount,
			SourceLocation,
			bRunNestedToCompletion);

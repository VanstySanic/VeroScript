// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace VeroScript
{
	enum class EInstanceCallInterceptResult : std::uint8_t
	{
		// Continue through the original script method call path.
		Unhandled,
		// Use OutReturn (or the value produced by Proceed) and skip the original call.
		Handled,
		// Abort the call and propagate OutError as a script runtime error.
		Failed,
	};

	/**
	 * Identity of the fully resolved script method target.
	 *
	 * Numeric identities are meaningful only together with ProgramGeneration.
	 * String views remain valid only while the interceptor callback is executing.
	 */
	struct FResolvedInstanceMethod
	{
		std::uint64_t ProgramGeneration = 0;
		std::uint64_t ModuleIdentity = 0;
		std::uint64_t DeclaringTypeIdentity = 0;
		std::uint64_t MethodIdentity = 0;
		std::uint32_t FunctionIndex = 0;
		std::string_view ModuleName;
		std::string_view DeclaringTypeName;
		std::string_view MethodName;
		std::string_view QualifiedName;
		bool bVirtual = false;
		bool bConst = false;
		bool bReturnsVoid = true;
	};

	/**
	 * C++17-compatible non-owning view over instance-call arguments.
	 * The view and its elements are valid only for the duration of the interceptor call.
	 */
	// FInstanceCallInterceptContext stores this view by value and needs its definition.
	class FValueView
	{
	public:
		FValueView() = default;
		FValueView(const FValue* InData, const std::size_t InSize)
			: Data(InData), Size(InSize)
		{
		}

		const FValue* data() const
		{
			return Data;
		}
		std::size_t size() const
		{
			return Size;
		}
		bool empty() const
		{
			return Size == 0;
		}
		const FValue& operator[](const std::size_t Index) const
		{
			return Data[Index];
		}
		const FValue* begin() const
		{
			return Data;
		}
		const FValue* end() const
		{
			return Data ? Data + Size : nullptr;
		}

	private:
		const FValue* Data = nullptr;
		std::size_t Size = 0;
	};

	/**
	 * Context for one resolved script instance-method invocation.
	 *
	 * The entire context, including Proceed, is callback-scoped and must not be
	 * retained or invoked asynchronously.
	 *
	 * Proceed executes this exact resolved target once and bypasses interception for
	 * that call only. Calls made by the method body remain interceptable. Proceed must
	 * be called at most once and an interceptor that calls it must return Handled.
	 */
	struct FInstanceCallInterceptContext
	{
		FValue Receiver;
		FResolvedInstanceMethod Target;
		FValueView Arguments;
		std::function<bool(FValue& OutReturn, FScriptError& OutError)> Proceed;
	};

	using FInstanceCallInterceptor = std::function<EInstanceCallInterceptResult(
		const FInstanceCallInterceptContext& Context,
		FValue& OutReturn,
		FScriptError& OutError)>;
}

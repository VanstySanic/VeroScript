// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	namespace StandardLibrary
	{
		struct FServiceContext;
		struct FServiceResult;
	}
	namespace Runtime
	{
		// Reflection method calls require a tagged full call signature, including
		// ref/const directions. Field operations carry only their value type.
		inline constexpr std::string_view ReflectionCallSignaturePrefix = "@invoke:";

		inline bool IsIntegerValue(const FValue& Value)
		{
			return std::holds_alternative<std::int32_t>(Value) || std::holds_alternative<std::uint32_t>(Value) || std::holds_alternative<std::int64_t>(Value) || std::holds_alternative<std::uint64_t>(Value) || std::holds_alternative<std::int8_t>(Value) || std::holds_alternative<std::uint8_t>(Value) || std::holds_alternative<std::int16_t>(Value) || std::holds_alternative<std::uint16_t>(Value);
		}
		/** Read an integer without narrowing or accepting floating-point values. */
		bool TryGetInteger64(const FValue& Value, std::int64_t& OutInteger);
		/** Read a non-negative integer that fits both int64 and native size_t. */
		bool TryGetSizeValue(const FValue& Value, std::size_t& OutSize);
		inline bool IsSignedIntegerValue(const FValue& Value)
		{
			return std::holds_alternative<std::int32_t>(Value) || std::holds_alternative<std::int64_t>(Value) || std::holds_alternative<std::int8_t>(Value) || std::holds_alternative<std::int16_t>(Value);
		}
		inline bool IsUnsignedIntegerValue(const FValue& Value)
		{
			return std::holds_alternative<std::uint32_t>(Value) || std::holds_alternative<std::uint64_t>(Value) || std::holds_alternative<std::uint8_t>(Value) || std::holds_alternative<std::uint16_t>(Value);
		}
		inline bool IsNumericValue(const FValue& Value)
		{
			return IsIntegerValue(Value) || std::holds_alternative<float>(Value) || std::holds_alternative<double>(Value);
		}
		inline bool IsStringValue(const FValue& Value)
		{
			return std::holds_alternative<std::string>(Value);
		}
		inline bool IsWStringValue(const FValue& Value)
		{
			return std::holds_alternative<std::u16string>(Value);
		}
		// Pure string queries and direct owned-string updates shared by execution paths.
		inline bool TryGetStringLength(const FValue& Value, std::int64_t& OutLength) noexcept
		{
			std::size_t Length = 0;
			if (const auto* Text = std::get_if<std::string>(&Value))
			{
				Length = Text->size();
			}
			else if (const auto* WideText = std::get_if<std::u16string>(&Value))
			{
				Length = WideText->size();
			}
			else
			{
				return false;
			}
			if (Length > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
			OutLength = static_cast<std::int64_t>(Length);
			return true;
		}
		// Preserve byte/code-unit semantics and standard string alias/exception
		// guarantees. A single unit needs no generic range-copy operation; capture
		// it before push_back can invalidate a view into the destination itself.
		inline void AppendString(std::string& Target, const std::string_view Suffix)
		{
			if (Suffix.size() == 1u)
			{
				const char Unit = Suffix.front();
				Target.push_back(Unit);
			}
			else if (!Suffix.empty())
			{
				Target.append(Suffix.data(), Suffix.size());
			}
		}
		inline void AppendString(std::u16string& Target, const std::u16string_view Suffix)
		{
			if (Suffix.size() == 1u)
			{
				const char16_t Unit = Suffix.front();
				Target.push_back(Unit);
			}
			else if (!Suffix.empty())
			{
				Target.append(Suffix.data(), Suffix.size());
			}
		}
		inline bool TryAppendStringConstant(FValue& Target, const FBytecodeModule& Module,
		    const FBytecodeConstant& Constant)
		{
			if (auto* Text = std::get_if<std::string>(&Target);
			    Text && Constant.Kind == EConstantKind::Str && Constant.StringIndex < Module.StringPool.size())
			{
				AppendString(*Text, Module.StringPool[Constant.StringIndex]);
				return true;
			}
			if (auto* Text = std::get_if<std::u16string>(&Target);
			    Text && Constant.Kind == EConstantKind::WStr && Constant.StringIndex < Module.WStringPool.size())
			{
				AppendString(*Text, Module.WStringPool[Constant.StringIndex]);
				return true;
			}
			return false;
		}
		// Same-width string append only; mismatch leaves Target unchanged. Allows self-append.
		bool TryAppendStringValue(FValue& Target, const FValue& Suffix);
		bool IsUnsignedTypeName(std::string_view TypeName);
		/**
		 * True for the built-in scalar value types (including exact-width spelling
		 * aliases such as int8/int32/uint64). These are real FValue alternatives;
		 * aliases only select the same-width identity and never widen storage.
		 */
		bool IsPrimitiveValueTypeName(std::string_view TypeName);
		bool ValueMatchesPrimitiveTypeName(
		    const FValue& Value,
		    std::string_view TypeName);
		/**
		 * Apply the language's checked scalar conversion to Value. Integer
		 * conversions truncate floating inputs toward zero and reject overflow.
		 * Returns false for both non-primitive target names and invalid values.
		 */
		bool CoerceValueToPrimitiveTypeName(
		    std::string_view TypeName,
		    FValue& Value,
		    std::string& OutError);
		double ToDouble(const FValue& Value);
		std::int64_t ToSignedInteger64(const FValue& Value);
		std::uint64_t ToUnsignedInteger64(const FValue& Value);
		bool AddIntegerImmediate(FValue& Value, std::int32_t Immediate);
		// These checked primitives sit in the innermost VM/JIT scalar and typed-
		// container loops. Keep their single authoritative implementation inline so
		// every backend can fold proven ranges without crossing a runtime call boundary.
		// The widened operation also avoids relying on compiler-specific overflow
		// intrinsics and therefore preserves identical C++17 semantics on every host.
		[[nodiscard]] inline bool CheckedAdd(
		    const std::int32_t Left,
		    const std::int32_t Right,
		    std::int32_t& OutValue) noexcept
		{
			const std::int64_t Result = static_cast<std::int64_t>(Left) + static_cast<std::int64_t>(Right);
			if (Result < std::numeric_limits<std::int32_t>::min() || Result > std::numeric_limits<std::int32_t>::max())
			{
				return false;
			}
			OutValue = static_cast<std::int32_t>(Result);
			return true;
		}

		[[nodiscard]] inline bool CheckedSub(
		    const std::int32_t Left,
		    const std::int32_t Right,
		    std::int32_t& OutValue) noexcept
		{
			const std::int64_t Result = static_cast<std::int64_t>(Left) - static_cast<std::int64_t>(Right);
			if (Result < std::numeric_limits<std::int32_t>::min() || Result > std::numeric_limits<std::int32_t>::max())
			{
				return false;
			}
			OutValue = static_cast<std::int32_t>(Result);
			return true;
		}

		[[nodiscard]] inline bool CheckedMul(
		    const std::int32_t Left,
		    const std::int32_t Right,
		    std::int32_t& OutValue) noexcept
		{
			const std::int64_t Result = static_cast<std::int64_t>(Left) * static_cast<std::int64_t>(Right);
			if (Result < std::numeric_limits<std::int32_t>::min() || Result > std::numeric_limits<std::int32_t>::max())
			{
				return false;
			}
			OutValue = static_cast<std::int32_t>(Result);
			return true;
		}
		std::string ValueToString(const FValue& Value);
		// Write a value without appending a newline or forcing a flush. Wstring
		// values use the native wide-console path on Windows when available.
		bool WriteValue(std::ostream& OutputStream, const FValue& Value);
		// Print value for host output; wstring uses wide console API on Windows when possible.
		bool WriteValueLine(std::ostream& OutputStream, const FValue& Value);
		// Capability probes are conservative. Embedded hosts can explicitly expose
		// an output terminal through Console::IHost; plain/redirected sinks stay plain.
		bool IsInteractiveInput(const std::istream& InputStream);
		bool IsInteractiveOutput(const std::ostream& OutputStream);
		// Clear the visible output window and move to its origin. Native Windows
		// controls and supported POSIX/host terminals are supported. False means
		// unsupported or failed; terminal input is not required.
		bool ClearInteractiveOutput(std::ostream& OutputStream);
		FValue ConstantToValue(const FBytecodeModule& Module, const FBytecodeConstant& Constant);
		/**
		 * Deep-copy a script object. Class HostBase is shared; Struct HostBase uses the
		 * stamped @clone callback. Returns nullptr if Struct HostBase cannot be cloned,
		 * or if a malformed cyclic / excessively deep value graph is encountered.
		 */
		FObjectHandle CloneStruct(const FScriptObject& Object);
		/**
		 * Deep-copy value types (structs / T[N] / stamped host structs). Returns false when
		 * a stamped `@clone` (nested or direct, including Struct HostBase) cannot run;
		 * malformed cyclic or excessively deep value containment is also rejected. Out
		 * is then unspecified. Unstamped bare host handles are shared (reference).
		 * Opcode/Jit CloneStruct may still deep-copy unstamped hosts via ActiveNatives.
		 */
		bool TryCloneValue(const FValue& Value, FValue& Out);
		// Recursive value equality is bounded and evaluates stable entry snapshots;
		// malformed cyclic value containment cannot recurse without a limit.
		bool ValuesEqual(const FBytecodeModule& Module, const FValue& Left, const FValue& Right);
		bool IsNativeArrayTypeName(std::string_view TypeName);
		bool IsDynamicArrayTypeName(std::string_view TypeName);
		bool IsMapTypeName(std::string_view TypeName);
		bool IsMultiMapTypeName(std::string_view TypeName);
		bool IsSetTypeName(std::string_view TypeName);
		bool IsMultiSetTypeName(std::string_view TypeName);
		bool IsOrderedMultiSetTypeName(std::string_view TypeName);
		bool IsQueueTypeName(std::string_view TypeName);
		bool IsPriorityQueueTypeName(std::string_view TypeName);
		bool IsAllowedPriorityQueueElementType(std::string_view TypeName);
		bool IsStackTypeName(std::string_view TypeName);
		bool IsOrderedMapTypeName(std::string_view TypeName);
		bool IsOrderedMultiMapTypeName(std::string_view TypeName);
		bool IsDequeTypeName(std::string_view TypeName);
		bool IsChannelTypeName(std::string_view TypeName);
		bool IsFunctionTypeName(std::string_view TypeName);
		// Soft builtin generic struct pair<K,V> (first/second).
		bool IsPairTypeName(std::string_view TypeName);
		bool ParsePairType(
		    std::string_view TypeName,
		    std::string& OutFirstType,
		    std::string& OutSecondType);
		std::string MakePairTypeName(std::string_view FirstType, std::string_view SecondType);
		// Stdlib multicast_delegate / OneParam / TwoParams (mangled #… / module prefix ok).
		bool IsMulticastDelegateTypeName(std::string_view TypeName);
		bool ParseFunctionType(
		    std::string_view TypeName,
		    std::vector<std::string>& OutParamTypes,
		    std::string& OutReturnType);
		std::string MakeFunctionTypeName(
		    const std::vector<std::string>& ParamTypes,
		    std::string_view ReturnType);
		bool IsReferenceTypeName(std::string_view TypeName);
		bool IsConstReferenceTypeName(std::string_view TypeName);
		std::string_view StripReferenceTypeName(std::string_view TypeName);
		bool IsCommonContainerTypeName(std::string_view TypeName);
		bool ParseDynamicArrayElementType(std::string_view TypeName, std::string& OutElementType);
		bool SplitMapType(
		    std::string_view TypeName,
		    std::string& OutKeyType,
		    std::string& OutValueType);
		bool ParseMapType(
		    std::string_view TypeName,
		    std::string& OutKeyType,
		    std::string& OutValueType);
		bool SplitSetElementType(std::string_view TypeName, std::string& OutElementType);
		bool ParseSetElementType(std::string_view TypeName, std::string& OutElementType);
		bool ParseMultiSetElementType(std::string_view TypeName, std::string& OutElementType);
		bool ParseQueueElementType(std::string_view TypeName, std::string& OutElementType);
		bool ParseStackElementType(std::string_view TypeName, std::string& OutElementType);
		bool ParseChannelElementType(std::string_view TypeName, std::string& OutElementType);
		bool IsAllowedChannelElementType(std::string_view ElementType);
		bool SplitOrderedMapType(
		    std::string_view TypeName,
		    std::string& OutKeyType,
		    std::string& OutValueType);
		bool ParseOrderedMapType(
		    std::string_view TypeName,
		    std::string& OutKeyType,
		    std::string& OutValueType);
		bool IsAllowedMapOrSetKeyType(std::string_view KeyType);
		bool ParseDequeElementType(std::string_view TypeName, std::string& OutElementType);
		/**
		 * Peel the leftmost dimension: "int[2][3]" → OutElementType "int[3]", OutLength 2.
		 * Recursive peels yield nested multi-dim types (storage is nested FNativeArray).
		 */
		bool ParseNativeArrayType(
		    std::string_view TypeName,
		    std::string& OutElementType,
		    std::uint32_t& OutLength);
		// Max elements per T[N] dimension (inclusive).
		inline constexpr std::uint32_t MaxNativeArrayDimensionLength = 1u << 16;
		// Max nested T[N][M]... dimensions. This bounds recursive construction/copy.
		inline constexpr std::size_t MaxNativeArrayDimensions = 64;
		// Max flattened element count for nested T[N][M]… (product of dimensions).
		inline constexpr std::uint64_t MaxNativeArrayTotalElements = 1u << 20;
		bool ValidateNativeArrayTypeLimits(
		    std::string_view TypeName,
		    std::string* OutError = nullptr);
		bool IsScriptArgsArrayTypeName(std::string_view TypeName);
		FNativeArray CreateScriptArgsArray(const std::vector<std::string_view>& Args);

		// I/O Format 1.0: apply fmt with string args; false → OutError set.
		bool TryFormatString(
		    const std::string& Fmt,
		    const std::vector<std::string>& Args,
		    std::string& OutResult,
		    std::string& OutError,
		    const StandardLibrary::FServiceContext* Context = nullptr,
		    StandardLibrary::FServiceResult* Failure = nullptr);

		// Safe printf-style formatting over already stringified scalar arguments.
		// Supports %% and d/i/u/o/x/X/f/F/e/E/g/G/c/s conversions. Dynamic
		// width/precision (`*`), %n and pointer formatting are intentionally absent.
		bool TryPrintfFormatString(
		    const std::string& Fmt,
		    const std::vector<std::string>& Args,
		    std::string& OutResult,
		    std::string& OutError,
		    const StandardLibrary::FServiceContext* Context = nullptr,
		    StandardLibrary::FServiceResult* Failure = nullptr);

		bool TryParseInt32(std::string_view Text, std::int32_t& OutValue, std::string& OutError);
		bool TryParseDouble(std::string_view Text, double& OutValue, std::string& OutError);

		bool Utf8ToUtf16(std::string_view Text, std::u16string& OutValue, std::string* OutError = nullptr);
		std::string Utf16ToUtf8(std::u16string_view Text);

		std::uint64_t Fnv1a64(std::uint64_t Hash, const void* Data, std::size_t Size);
		std::uint64_t Fnv1a64(std::uint64_t Hash, std::string_view Text);

		void RebuildModuleIndices(FBytecodeModule& Module);

		std::string GetExecutableDirectory();
		/**
		 * Per-user bytecode cache root for the current platform.
		 * Windows: %LOCALAPPDATA%/VeroScript/cache
		 * macOS:   $HOME/Library/Caches/VeroScript/cache
		 * Unix:    $XDG_CACHE_HOME/VeroScript/cache or $HOME/.cache/VeroScript/cache
		 * Empty means no usable user cache location could be determined.
		 */
		std::string GetUserCacheDirectory();
	}
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptMemory.h"

#include <string>
#include <string_view>
#include <vector>
#include <iosfwd>
#include <optional>

namespace VeroScript
{
	struct FBytecodeFunction;
	/** Per-operation limits; zero adds no limit. Existing VM/memory limits still apply. */
	struct FStandardLibraryLimits
	{
		std::uint64_t MaxTextBytes = 0;
		std::uint64_t MaxReadBytes = 0;
		std::uint64_t MaxSplitParts = 0;
	};

	namespace StandardLibrary
	{
		struct FTaskLayout
		{
			std::uint16_t Id = 0, Result = 0;
		};
		/** Trusted standard task shape; shared by callable construction and task operations. */
		bool ResolveTaskLayout(const FBytecodeModule& Module, std::string_view TypeName,
		    FTaskLayout& Output, std::string_view ElementType = {});
		bool IsDiscreteArgument(std::string_view FunctionName, std::size_t Index) noexcept;
		bool IsDiscreteArgument(const FBytecodeModule& Module, const FBytecodeFunction& Function, std::size_t Index);
		/** Standalone emitters must reject these operations before producing output. */
		bool UsesRuntimeFoundation(const FBytecodeModule& Module);
		enum class EServiceStatus : std::uint8_t
		{
			Ok,
			InvalidText,
			OutOfRange,
			InvalidArgument,
			LimitExceeded,
			Interrupted,
			IoError
		};

		struct FServiceResult
		{
			EServiceStatus Status = EServiceStatus::Ok;
			std::size_t Offset = 0;
			explicit operator bool() const noexcept
			{
				return Status == EServiceStatus::Ok;
			}
			bool IsParseFailure() const noexcept
			{
				return Status == EServiceStatus::InvalidText || Status == EServiceStatus::OutOfRange;
			}
		};

		/** Adapts the active VM work meter; helpers never create/reset execution budgets. */
		struct FServiceContext
		{
			FStandardLibraryLimits Limits;
			FMemoryWork Work;
			FServiceResult Check(std::uint64_t Units = 0) const
			{
				return Work.Check && !Work.Check(Work.UserData, Units)
				    ? FServiceResult{EServiceStatus::Interrupted, 0}
				    : FServiceResult{};
			}
			FServiceResult CheckText(std::uint64_t Bytes) const
			{
				if (Bytes > static_cast<std::uint64_t>(INT64_MAX) || (Limits.MaxTextBytes && Bytes > Limits.MaxTextBytes))
				{
					return {EServiceStatus::LimitExceeded, 0};
				}
				return Check();
			}
		};

		bool IsAsciiWhitespace(unsigned char Byte) noexcept;
		bool IsScalarParseType(std::string_view Type, bool IntegerOnly = false) noexcept;
		bool CheckedNumericAdd(const FValue& Left, const FValue& Right, FValue& Output);
		FServiceResult NextRandomUInt(FDynamicArray& Engine, std::uint32_t& Output, FMemoryWork Work = {});
		FServiceResult RandomIndex(FDynamicArray& Engine, std::int64_t UpperExclusive, std::int64_t& Output, FMemoryWork Work = {});
		/** Output is unchanged on failure. Offset refers to the original byte string. */
		FServiceResult ParseScalar(std::string_view Text, std::string_view Type, int Base,
		    bool TrimWhitespace, FValue& Output, const FServiceContext& Context = {});

		FServiceResult StartsWith(std::string_view Text, std::string_view Prefix, bool& Output,
		    const FServiceContext& Context = {});
		FServiceResult EndsWith(std::string_view Text, std::string_view Suffix, bool& Output,
		    const FServiceContext& Context = {});
		FServiceResult LastFind(std::string_view Text, std::string_view Needle, std::int64_t& Output,
		    const FServiceContext& Context = {});
		FServiceResult FirstFind(std::string_view Text, std::string_view Needle, std::int64_t& Output,
		    const FServiceContext& Context = {});
		FServiceResult Repeat(std::string_view Text, std::int64_t Count, std::string& Output,
		    const FServiceContext& Context = {});
		FServiceResult Replace(std::string_view Text, std::string_view From, std::string_view To,
		    std::string& Output, const FServiceContext& Context = {});
		FServiceResult Trim(std::string_view Text, bool Left, bool Right, std::string& Output,
		    const FServiceContext& Context = {});
		FServiceResult Split(std::string_view Text, std::string_view Separator,
		    std::vector<std::string>& Output, const FServiceContext& Context = {});
		FServiceResult Join(const std::vector<std::string_view>& Parts, std::string_view Separator,
		    std::string& Output, const FServiceContext& Context = {});
		/** InvalidText only denotes malformed UTF-8, never cancellation/limits. */
		FServiceResult ValidateUtf8(std::string_view Text, const FServiceContext& Context = {});

		enum class EPathStyle : std::uint32_t
		{
			Native = 0,
			Posix = 1,
			Windows = 2
		};
		struct FLexicalPath
		{
			bool Absolute = false;
			std::string Root;
			std::string Normal;
		};
		FServiceResult LexicalPath(std::string_view Path, EPathStyle Style, FLexicalPath& Output,
		    const FServiceContext& Context = {});

		FServiceResult ReadInput(std::istream& Input, bool Token, std::optional<std::string>& Output,
		    const FServiceContext& Context = {});
		FServiceResult ReadText(std::istream& Input, std::string& Output, const FServiceContext& Context = {});
		FServiceResult ReadBytes(std::istream& Input, FMemoryContext& Memory, std::optional<std::uint64_t> Maximum,
		    FValue& Output, FMemoryResult& MemoryError, const FServiceContext& Context = {});
		FServiceResult WriteBytes(std::ostream& Output, const FMemoryLease& Lease, const FServiceContext& Context = {});
		const IHostBindingCatalog& GetServiceBindingCatalog();
	}
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "SchemaRegex.h"
#include "SchemaSemantics.h"
#include "VeroScriptJsonRegexSymbols.h"
#include <libregexp.h>
#include <cstdlib>
#include <climits>
#include <cstddef>

namespace VeroScript::Json::Detail
{
	struct FRegexWork
	{
		std::exception_ptr Failure;
		std::uintptr_t StackBase = 0;
		std::size_t Bytes = 0;
	};

	struct alignas(std::max_align_t) FRegexAllocation
	{
		std::size_t Bytes;
	};

	static int CheckRegex(FRegexWork& Work, std::uint64_t Units) noexcept
	{
		try
		{
			FSchemaSemantics::Check(Units);
			return 0;
		}
		catch (...)
		{
			if (!Work.Failure)
			{
				Work.Failure = std::current_exception();
			}
			return 1;
		}
	}

	struct FSchemaRegex::FImpl
	{
		~FImpl()
		{
			lre_realloc(nullptr, Code, 0);
		}
		std::uint8_t* Code = nullptr;
	};

	FSchemaRegex::FSchemaRegex(std::string_view Pattern)
	{
		FSchemaSemantics::Check(Pattern.size() + 1);
		if (Pattern.size() > 4096)
		{
			Fail(EError::Limit, "Schema regex pattern exceeds its limit");
		}
		ValidateUtf8(Pattern);
		FRegexWork Work;
		Work.StackBase = reinterpret_cast<std::uintptr_t>(&Work);
		char Error[256]{};
		int Length = 0;
		auto Compiled = std::make_shared<FImpl>();
		Compiled->Code =
			lre_compile(&Length, Error, sizeof(Error), Pattern.data(), Pattern.size(), LRE_FLAG_UNICODE, &Work);
		if (Work.Failure)
		{
			std::rethrow_exception(Work.Failure);
		}
		if (!Compiled->Code)
		{
			throw FSchemaRegexSyntax(std::string("Invalid Schema regex: ") + Error);
		}
		FSchemaSemantics::Check();
		Impl = std::move(Compiled);
	}

	bool FSchemaRegex::Search(std::string_view Text) const
	{
		FSchemaSemantics::Check(Text.size() + 1);
		if (Text.size() > static_cast<std::size_t>(INT_MAX / 2))
		{
			Fail(EError::Limit, "Schema regex input exceeds the backend range");
		}
		if (Text.size() > FSchemaSemantics::Budget().Limits.TokenBytes)
		{
			Fail(EError::Limit, "Schema regex input exceeds the string limit");
		}
		std::u16string Units;
		Units.reserve(Text.size());
		std::size_t NextCheck = 0;
		for (std::size_t I = 0; I < Text.size();)
		{
			if (I >= NextCheck)
			{
				FSchemaSemantics::Check();
				NextCheck = I + 4096;
			}
			std::uint32_t C = static_cast<unsigned char>(Text[I++]);
			if (C >= 0x80)
			{
				if (C < 0xc2 || C > 0xf4)
				{
					Fail(EError::InvalidUtf8, "Invalid regex input encoding");
				}
				unsigned Count = C < 0xe0 ? 1 : C < 0xf0 ? 2 : 3;
				const std::uint32_t Minimum = Count == 1 ? 0x80 : Count == 2 ? 0x800 : 0x10000;
				C &= Count == 1 ? 0x1f : Count == 2 ? 0xf : 7;
				if (Count > Text.size() - I)
				{
					Fail(EError::InvalidUtf8, "Truncated regex input encoding");
				}
				while (Count--)
				{
					const auto Byte = static_cast<unsigned char>(Text[I++]);
					if ((Byte & 0xc0) != 0x80)
					{
						Fail(EError::InvalidUtf8, "Invalid regex input continuation");
					}
					C = (C << 6) | (Byte & 63);
				}
				if (C < Minimum || C > 0x10ffff || (C >= 0xd800 && C <= 0xdfff))
				{
					Fail(EError::InvalidUtf8, "Invalid regex input code point");
				}
			}
			if (C > 0xffff)
			{
				C -= 0x10000;
				Units += static_cast<char16_t>(0xd800 + (C >> 10));
				Units += static_cast<char16_t>(0xdc00 + (C & 1023));
			}
			else
			{
				Units += static_cast<char16_t>(C);
			}
		}
		std::vector<std::uint8_t*> Captures(static_cast<std::size_t>(lre_get_alloc_count(Impl->Code)));
		FRegexWork Work;
		Work.StackBase = reinterpret_cast<std::uintptr_t>(&Work);
		const auto Result = lre_exec(Captures.data(),
			Impl->Code,
			reinterpret_cast<const std::uint8_t*>(Units.data()),
			0,
			static_cast<int>(Units.size()),
			1,
			&Work);
		if (Work.Failure)
		{
			std::rethrow_exception(Work.Failure);
		}
		if (Result == LRE_RET_MEMORY_ERROR)
		{
			throw std::bad_alloc();
		}
		if (Result < 0)
		{
			Fail(EError::Limit, "Schema regex evaluation stopped");
		}
		return Result != 0;
	}
} // namespace VeroScript::Json::Detail

extern "C" int lre_check_timeout(void* Opaque)
{
	return VeroScript::Json::Detail::CheckRegex(*static_cast<VeroScript::Json::Detail::FRegexWork*>(Opaque), 10000);
}

extern "C" bool lre_check_stack_overflow(void* Opaque, std::size_t Size)
{
	using namespace VeroScript::Json::Detail;
	auto& Work = *static_cast<FRegexWork*>(Opaque);
	char Marker;
	const auto Current = reinterpret_cast<std::uintptr_t>(&Marker);
	const auto Distance = Current > Work.StackBase ? Current - Work.StackBase : Work.StackBase - Current;
	if (Distance >= 64 * 1024 || Size > 64 * 1024 - Distance)
	{
		try
		{
			Fail(VeroScript::Json::EError::Limit, "Schema regex parser stack limit exceeded");
		}
		catch (...)
		{
			if (!Work.Failure)
			{
				Work.Failure = std::current_exception();
			}
		}
		return true;
	}
	return CheckRegex(Work, 1) != 0;
}

extern "C" void* lre_realloc(void* Opaque, void* Pointer, std::size_t Size)
{
	using namespace VeroScript::Json::Detail;
	auto* Previous = Pointer ? static_cast<FRegexAllocation*>(Pointer) - 1 : nullptr;
	auto* Work = static_cast<FRegexWork*>(Opaque);
	const auto OldSize = Previous ? Previous->Bytes : 0;
	if (!Size)
	{
		if (Work)
		{
			Work->Bytes -= OldSize;
		}
		std::free(Previous);
		return nullptr;
	}
	try
	{
		if (!Work || Size > 16 * 1024 * 1024 || Work->Bytes - OldSize > 16 * 1024 * 1024 - Size)
		{
			Fail(VeroScript::Json::EError::Limit, "Schema regex allocation limit exceeded");
		}
		FSchemaSemantics::Check();
		auto* Result = static_cast<FRegexAllocation*>(std::realloc(Previous, Size + sizeof(FRegexAllocation)));
		if (!Result)
		{
			throw std::bad_alloc();
		}
		Result->Bytes = Size;
		Work->Bytes = Work->Bytes - OldSize + Size;
		return Result + 1;
	}
	catch (...)
	{
		if (Work && !Work->Failure)
		{
			Work->Failure = std::current_exception();
		}
		return nullptr;
	}
}

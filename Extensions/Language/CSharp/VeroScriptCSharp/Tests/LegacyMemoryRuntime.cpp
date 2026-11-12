// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <cstdint>

// Omit memory, compile-capability and severity exports to model an earlier
// runtime with ABI 0x00000101 while retaining its basic diagnostic contract.
#if defined(_WIN32)
#define VS_LEGACY_EXPORT __declspec(dllexport)
#define VS_LEGACY_CALL __cdecl
#else
#define VS_LEGACY_EXPORT __attribute__((visibility("default")))
#define VS_LEGACY_CALL
#endif

struct FLegacyStringView
{
	const char* Data;
	std::uint64_t Size;
};
struct FLegacyDiagnostic
{
	std::uint32_t StructSize;
	FLegacyStringView Message, MessageId, FileLabel;
	std::uint32_t Line, Column;
};

extern "C"
{
	VS_LEGACY_EXPORT std::uint32_t VS_LEGACY_CALL vs_interop_get_abi_version()
	{
		return 0x00000101;
	}
	VS_LEGACY_EXPORT std::uint64_t VS_LEGACY_CALL vs_interop_get_capabilities()
	{
		return 0;
	}
	VS_LEGACY_EXPORT std::uint32_t VS_LEGACY_CALL vs_interop_session_create(const void*, void** Out)
	{
		static int Session;
		*Out = &Session;
		return 0;
	}
	VS_LEGACY_EXPORT std::uint32_t VS_LEGACY_CALL vs_interop_session_destroy(void*)
	{
		return 0;
	}
	VS_LEGACY_EXPORT std::uint64_t VS_LEGACY_CALL vs_interop_session_diagnostic_count(const void*)
	{
		return 1;
	}
	VS_LEGACY_EXPORT std::uint32_t VS_LEGACY_CALL vs_interop_session_get_diagnostic(
		const void*, std::uint64_t Index, FLegacyDiagnostic* Out)
	{
		if (!Out || Index != 0 || Out->StructSize < sizeof(FLegacyDiagnostic))
		{
			return 1;
		}
		Out->Message = {"legacy diagnostic", 17};
		Out->MessageId = {"LEGACY", 6};
		Out->FileLabel = {"legacy.vs", 9};
		Out->Line = 3;
		Out->Column = 2;
		return 0;
	}
	VS_LEGACY_EXPORT std::uint32_t VS_LEGACY_CALL vs_interop_value_create(void** Out)
	{
		static int Value;
		*Out = &Value;
		return 0;
	}
	VS_LEGACY_EXPORT void VS_LEGACY_CALL vs_interop_value_destroy(void*)
	{
	}
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStandardLibraryServices.h"
#include "VeroScriptNative.h"

namespace VeroScript::StandardLibrary
{
	FServiceResult NextRandomUInt(FDynamicArray& Engine, std::uint32_t& Output, FMemoryWork Work)
	{
		if (!NativeElementTypeNamesEqual(Engine.ElementTypeName, "unsigned int")) return {EServiceStatus::InvalidArgument, 0};
		if (Work.Check && !Work.Check(Work.UserData, 1)) return {EServiceStatus::Interrupted, 0};
		if (Engine.ElementCount() == 0)
		{
			FValue Seed = std::uint32_t{0x6d2b79f5};
			if (!Engine.TryPushScalar(Seed))
			{
				Engine.Elements.push_back(Seed);
			}
		}
		FValue Current;
		if (!Engine.TryGetScalar(0, Current))
		{
			Current = Engine.Elements.front();
		}
		const auto* Initial = std::get_if<std::uint32_t>(&Current);
		if (!Initial || !*Initial) return {EServiceStatus::InvalidArgument, 0};
		auto Value = *Initial;
		Value ^= Value << 13;
		Value ^= Value >> 17;
		Value ^= Value << 5;
		if (!Engine.TrySetScalar(0, FValue{Value}))
		{
			Engine.Elements.front() = Value;
		}
		Output = Value;
		return {};
	}
	FServiceResult RandomIndex(FDynamicArray& Engine, std::int64_t UpperExclusive, std::int64_t& Output, FMemoryWork Work)
	{
		if (UpperExclusive <= 0) return {EServiceStatus::InvalidArgument, 0};
		constexpr std::uint64_t Base = UINT64_C(4294967295);
		const auto Bound = static_cast<std::uint64_t>(UpperExclusive);
		const bool Wide = UpperExclusive > INT32_MAX;
		const auto Range = Wide ? Base * Base : Base;
		const auto Limit = Range - Range % Bound;
		for (;;)
		{
			std::uint32_t High = 0, Low = 0;
			if (auto R = NextRandomUInt(Engine, High, Work); !R) return R;
			std::uint64_t Sample = static_cast<std::uint64_t>(High) - 1;
			if (Wide)
			{
				if (auto R = NextRandomUInt(Engine, Low, Work); !R) return R;
				Sample = Sample * Base + static_cast<std::uint64_t>(Low) - 1;
			}
			if (Sample < Limit)
			{
				Output = static_cast<std::int64_t>(Sample % Bound);
				return {};
			}
		}
	}
}

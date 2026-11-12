// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNative.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptStandardLibrary.h"

#include <unordered_map>

namespace VeroScript
{
	bool BindBytecodeNativeReferences(const FBytecodeModule& Module,
	    const std::vector<FNativeFunction>& Available, std::vector<FNativeFunction>& Out, std::string& Error)
	{
		const auto StandardCount = StandardLibrary::GetBindingCatalog().GetNativeFunctions().size();
		if (Available.size() < StandardCount || Available.size() > 0xFFFF || Module.NativeReferences.size() > 0xFFFF)
		{
			Error = "invalid native function binding table size";
			return false;
		}
		const auto StandardBegin = Available.size() - StandardCount;
		std::unordered_map<std::string_view, std::vector<std::size_t>> ByName;
		for (std::size_t Index = 0; Index < StandardBegin; ++Index)
		{
			ByName[Available[Index].Name].push_back(Index);
		}
		std::vector<FNativeFunction> Bound;
		std::vector<bool> Used(StandardBegin, false);
		Bound.reserve(Available.size());
		for (const auto& Reference : Module.NativeReferences)
		{
			std::size_t Match = Available.size();
			bool bAmbiguous = false;
			const char* InvalidMetadata = nullptr;
			const auto Candidates = ByName.find(Reference.Name);
			if (Candidates != ByName.end())
			{
				for (const auto Index : Candidates->second)
				{
					const auto& Native = Available[Index];
					if (Native.Origin.ProviderId != Reference.ProviderId
					    || NativeRuntimeArgumentCount(Native) != Reference.ArgumentCount)
					{
						continue;
					}
					if (!NativeParamFlagsAreValid(Native))
					{
						InvalidMetadata = "invalid native parameter flags: ";
						continue;
					}
					if (!NativeFunctionContainerMetadataIsValid(Native))
					{
						InvalidMetadata = "invalid native container metadata: ";
						continue;
					}
					if (!Native.Callback)
					{
						InvalidMetadata = "null native callback: ";
						continue;
					}
					if (!(DescribeNativeFunction(Native) == Reference))
					{
						continue;
					}
					bAmbiguous |= Match != Available.size();
					Match = Index;
				}
			}
			if (Match == Available.size() || bAmbiguous)
			{
				Error = std::string(bAmbiguous ? "ambiguous native function binding: "
				    : InvalidMetadata ? InvalidMetadata : "missing or incompatible native function binding: ") + Reference.Name;
				return false;
			}
			Bound.push_back(Available[Match]);
			Used[Match] = true;
		}
		for (std::size_t Index = 0; Index < StandardBegin; ++Index)
		{
			if (!Used[Index])
			{
				Bound.push_back(Available[Index]);
			}
		}
		Bound.insert(Bound.end(), Available.begin() + StandardBegin, Available.end());
		if (Bound.size() > 0xFFFF)
		{
			Error = "native function binding table exceeds the runtime slot limit";
			return false;
		}
		Out.swap(Bound);
		Error.clear();
		return true;
	}
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptValue.h"
#include <unordered_map>

namespace VeroScript
{
	void FScriptObject::SetTypeName(const std::string& Name)
	{
		if (Name == GetTypeName()) return;
		const auto Commit = [this](std::shared_ptr<const std::string> Identity)
		{
			TypeNameIdentity = std::move(Identity);
			RuntimeTypeModule = nullptr;
			RuntimeTypeGeneration = 0;
			RuntimeTypeIndex = 0xFFFF;
		};
		if (Name.empty())
		{
			Commit({});
			return;
		}
		if (Name.size() > 4096)
		{
			Commit(std::make_shared<const std::string>(Name));
			return;
		}
		// Retain a bounded set of immutable names across object lifetimes. No
		// objects, program layouts or modules are retained by this cache.
		static thread_local bool CacheAlive = true;
		if (!CacheAlive)
		{
			Commit(std::make_shared<const std::string>(Name));
			return;
		}
		struct FNames
		{
			explicit FNames(bool& InAlive)
				: Alive(InAlive)
			{
			}
			~FNames()
			{
				Alive = false;
			}
			bool& Alive;
			std::unordered_map<std::string, std::shared_ptr<const std::string>> Entries;
			std::size_t Bytes = 0;
		};
		static thread_local FNames Names(CacheAlive);
		const auto Found = Names.Entries.find(Name);
		if (Found != Names.Entries.end())
		{
			Commit(Found->second);
			return;
		}
		auto Identity = std::make_shared<const std::string>(Name);
		if (Names.Entries.size() >= 1024 || Name.size() > 65536 - Names.Bytes)
		{
			Names.Entries.clear();
			Names.Bytes = 0;
		}
		Names.Entries.emplace(*Identity, Identity);
		Names.Bytes += Name.size();
		Commit(std::move(Identity));
	}

	const FScriptObjectDescriptorData& FScriptObject::GetDescriptorData() const noexcept
	{
		static const FScriptObjectDescriptorData Empty;
		return DescriptorData ? *DescriptorData : Empty;
	}

	void FScriptObject::SetHostBase(FHostObjectHandle Value)
	{
		if (Value)
		{
			EditSupportData().HostBase = std::move(Value);
		}
		else if (SupportData)
		{
			if (SupportData->CompletedCopyBases.empty())
			{
				SupportData.reset();
			}
			else
			{
				EditSupportData().HostBase.reset();
			}
		}
	}

	const std::vector<std::uint16_t>& FScriptObject::GetCompletedCopyBases() const noexcept
	{
		static const std::vector<std::uint16_t> Empty;
		return SupportData ? SupportData->CompletedCopyBases : Empty;
	}

	std::vector<std::uint16_t>& FScriptObject::EditCompletedCopyBases()
	{
		return EditSupportData().CompletedCopyBases;
	}

	void FScriptObject::ClearCompletedCopyBases()
	{
		if (!SupportData || SupportData->CompletedCopyBases.empty()) return;
		if (!SupportData->HostBase)
		{
			SupportData.reset();
		}
		else
		{
			EditSupportData().CompletedCopyBases.clear();
		}
	}

	FScriptObjectSupportData& FScriptObject::EditSupportData()
	{
		if (!SupportData)
		{
			SupportData = std::make_shared<FScriptObjectSupportData>();
		}
		else if (SupportData.use_count() != 1)
		{
			SupportData = std::make_shared<FScriptObjectSupportData>(*SupportData);
		}
		return *SupportData;
	}
}

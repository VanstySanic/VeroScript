// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "BoostUpgrade.h"
#include <utility>
namespace VeroScript::Network::Detail
{
	void FUpgradeLeaseState::Close() noexcept
	{
		if (Stream)
		{
			Stream->Close();
			Stream.reset();
		}
		if (ResourceId)
		{
			RemoveResource(Context, std::exchange(ResourceId, 0));
		}
		FBytes{}.swap(PreRead);
		Memory.reset();
	}
	FUpgradeLease::~FUpgradeLease()
	{
		if (State && State->ResourceId)
		{
			// The registered closer also owns State: a failed post cannot discard the only transport owner.
			OnDestroy(State->Context,
				[&]
				{
					Post(State->Context,
						[S = State]
						{
							S->Close();
						});
				});
		}
	}
}

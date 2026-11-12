// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkUpgrade.h"
#include "NetworkInternal.h"

namespace VeroScript::Network::Detail
{
	std::shared_ptr<FUpgradeSource> FUpgradeAccess::Make(
		const std::shared_ptr<FContext>& C, FUpgradeInfo Info, FUpgradeSourceState::FBegin Begin, FError& Error)
	{
		const auto Bytes = sizeof(FUpgradeSourceState) + sizeof(FUpgradeSource) + Info.Protocol.capacity() +
						   Info.BackendIdentity.capacity() + Info.RequestHead.capacity() + 256;
		auto Memory = ReserveBytes(C, Bytes, Error);
		if (!Memory)
		{
			return {};
		}
		auto S = std::make_shared<FUpgradeSourceState>();
		S->Context = C;
		S->Info = std::move(Info);
		S->Memory = std::move(Memory);
		S->Begin = std::move(Begin);
		return RetainObject(C, std::shared_ptr<FUpgradeSource>(new FUpgradeSource(std::move(S))));
	}
	void FUpgradeAccess::Begin(const std::shared_ptr<FUpgradeSource>& Source,
		std::shared_ptr<FUpgradeConsumer> Consumer, const std::shared_ptr<FOperation>& Op)
	{
		auto S = Source->Impl;
		if (S->Context != Op->Context.lock() || S->Info.ContractVersion != 1 ||
			S->Info.BackendIdentity != Consumer->BackendIdentity())
		{
			Fail(Op, EError::InvalidArgument, "Upgrade source context or backend is incompatible");
			return;
		}
		SetAbort(Op,
			[Consumer]
			{
				Consumer->Cancel();
			});
		S->Context->Post(
			[S, Consumer = std::move(Consumer), Op]
			{
				if (!Terminal(Op->Status))
				{
					S->Begin(S, Consumer, Op);
				}
			});
	}
} // namespace VeroScript::Network::Detail
namespace VeroScript::Network
{
	const FUpgradeInfo& FUpgradeSource::Info() const noexcept
	{
		return Impl->Info;
	}

	bool FUpgradeSource::BelongsTo(const FContext& C) const noexcept
	{
		if (std::string_view(C.GetFoundationIdentity()) != Backend::FoundationIdentity()) return false;
		return Impl->Context == Detail::FAccess::Context(const_cast<FContext&>(C));
	}

	bool FUpgradeSource::IsConsumed() const noexcept
	{
		return Impl->Consumed;
	}

	FUpgradeSource::FUpgradeSource(std::shared_ptr<Detail::FUpgradeSourceState> S) : Impl(std::move(S))
	{
	}
} // namespace VeroScript::Network

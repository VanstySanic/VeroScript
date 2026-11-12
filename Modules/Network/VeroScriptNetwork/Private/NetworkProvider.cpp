// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkInternal.h"
#include "NetworkDiagnostics.h"

namespace VeroScript::Network::Detail
{
	std::uint64_t ResourceCount(const std::shared_ptr<FContext>& Context)
	{
		std::lock_guard<std::mutex> Lock(Context->Mutex);
		return static_cast<std::uint64_t>(Context->Resources.size());
	}

	const std::shared_ptr<Backend::IRuntime>& Runtime(const std::shared_ptr<FContext>& Context) noexcept
	{
		return Context->Runtime;
	}
	const std::shared_ptr<Backend::IService>& Service(const std::shared_ptr<FContext>& Context) noexcept
	{
		return Context->Service;
	}
	const FContextOptions& Options(const std::shared_ptr<FContext>& Context) noexcept
	{
		return Context->Options;
	}
	bool IsClosed(const std::shared_ptr<FContext>& Context) noexcept
	{
		return Context->Closed.load();
	}
	bool RetainCodeOwner(const std::shared_ptr<FContext>& Context, std::shared_ptr<const void> Owner, FError& Error)
	{
		Error = {};
		std::lock_guard<std::mutex> Lock(Context->Mutex);
		if (Owner.use_count() == 0 || Context->Closed)
		{
			Error = {EError::Closed, "Missing code owner or context closed", 0};
			return false;
		}
		for (const auto& Existing : Context->CodeOwners)
		{
			if (!Existing.owner_before(Owner) && !Owner.owner_before(Existing))
			{
				return true;
			}
		}
		Context->CodeOwners.push_back(std::move(Owner));
		return true;
	}
	std::shared_ptr<const void> RetainObject(
		const std::shared_ptr<FContext>& Context, std::shared_ptr<const void> Object)
	{
		return Context->RetainObject(std::move(Object), Context);
	}
	std::shared_ptr<FOwnedResult> CreateOwnedResult(const std::shared_ptr<FContext>& Context,
		std::shared_ptr<const void> Retention, std::any Value)
	{
		return Context->CreateResult(Context, std::move(Retention), std::move(Value));
	}
	void Post(const std::shared_ptr<FContext>& Context, std::function<void()> Function)
	{
		Context->Post(std::move(Function));
	}
	std::uint64_t AddResource(const std::shared_ptr<FContext>& Context, std::function<void()> Close)
	{
		return Context->AddResource(std::move(Close));
	}
	bool ReplaceResource(const std::shared_ptr<FContext>& Context, std::uint64_t Id, std::function<void()> Close)
	{
		return Context->ReplaceResource(Id, std::move(Close));
	}
	void RemoveResource(const std::shared_ptr<FContext>& Context, std::uint64_t Id)
	{
		Context->RemoveResource(Id);
	}
	void Fault(const std::shared_ptr<FContext>& Context, EError Code) noexcept
	{
		Context->Fault(Code);
	}
	bool Finish(const std::shared_ptr<FContext>& Context, const std::shared_ptr<FOperation>& Operation,
		EOperationState State, FError Error, std::shared_ptr<const Network::Detail::FOwnedResult> Result)
	{
		return Context->Finish(Operation, State, std::move(Error), std::move(Result));
	}
	EOperationState State(const std::shared_ptr<FOperation>& Operation) noexcept
	{
		return Operation->Status.load();
	}
	std::uint64_t OperationId(const std::shared_ptr<FOperation>& Operation) noexcept
	{
		return Operation->Id;
	}
	std::shared_ptr<FContext> OperationContext(const std::shared_ptr<FOperation>& Operation)
	{
		return Operation->Context.lock();
	}
	FOperationErrorRead::FOperationErrorRead(std::shared_ptr<FOperation> Value)
		: Operation(std::move(Value)), Lock(Operation->Mutex)
	{
	}
	const FError& FOperationErrorRead::Get() const noexcept
	{
		return Operation->Failure;
	}
} // namespace VeroScript::Network::Detail

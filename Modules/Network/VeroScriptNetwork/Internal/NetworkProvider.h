// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"
#include "VeroScriptNetworkBackend.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace VeroScript::Network::Detail
{
	bool Terminal(EOperationState State) noexcept;

	struct FContext;
	struct FOperation;

	// Concrete context and operation layouts remain private to the foundation.
	const std::shared_ptr<Backend::IRuntime>& Runtime(const std::shared_ptr<FContext>& Context) noexcept;
	const std::shared_ptr<Backend::IService>& Service(const std::shared_ptr<FContext>& Context) noexcept;
	const FContextOptions& Options(const std::shared_ptr<FContext>& Context) noexcept;
	bool IsClosed(const std::shared_ptr<FContext>& Context) noexcept;
	bool RetainCodeOwner(const std::shared_ptr<FContext>& Context, std::shared_ptr<const void> Owner, FError& Error);
	// The context's creating image owns the outer control block. Provider objects
	// and their deleters finish before releasing the code/context lease.
	std::shared_ptr<const void> RetainObject(
		const std::shared_ptr<FContext>& Context, std::shared_ptr<const void> Object);
	template <class T>
	std::shared_ptr<T> RetainObject(const std::shared_ptr<FContext>& Context, std::shared_ptr<T> Object)
	{
		if (!Object) return Object;
		auto* Pointer = Object.get();
		auto Owner = RetainObject(Context, std::shared_ptr<const void>(std::move(Object)));
		return std::shared_ptr<T>(std::move(Owner), Pointer);
	}
	void Post(const std::shared_ptr<FContext>& Context, std::function<void()> Function);
	std::uint64_t AddResource(const std::shared_ptr<FContext>& Context, std::function<void()> Close);
	bool ReplaceResource(const std::shared_ptr<FContext>& Context, std::uint64_t Id, std::function<void()> Close);
	void RemoveResource(const std::shared_ptr<FContext>& Context, std::uint64_t Id);
	void Fault(const std::shared_ptr<FContext>& Context, EError Code) noexcept;
	struct FOwnedResult;
	bool Finish(const std::shared_ptr<FContext>& Context, const std::shared_ptr<FOperation>& Operation,
		EOperationState State, FError Error = {}, std::shared_ptr<const Network::Detail::FOwnedResult> Result = {});
	EOperationState State(const std::shared_ptr<FOperation>& Operation) noexcept;
	std::uint64_t OperationId(const std::shared_ptr<FOperation>& Operation) noexcept;
	std::shared_ptr<FContext> OperationContext(const std::shared_ptr<FOperation>& Operation);
	struct FOwnedResult
	{
		std::shared_ptr<FContext> Lifetime;
		std::shared_ptr<const void> Retention;
		std::any Value;
		const char* Type = nullptr;
		std::size_t Size = 0, Alignment = 0;
		void* Data = nullptr;
	};

	class FOperationErrorRead
	{
	public:
		explicit FOperationErrorRead(std::shared_ptr<FOperation> Operation);
		const FError& Get() const noexcept;

	private:
		// The operation outlives the lock, including destruction of a detached read view.
		std::shared_ptr<FOperation> Operation;
		std::unique_lock<std::mutex> Lock;
	};
	std::shared_ptr<FOwnedResult> CreateOwnedResult(const std::shared_ptr<FContext>& Context,
		std::shared_ptr<const void> Retention, std::any Value);
	template <class T>
	std::shared_ptr<FOwnedResult> CreateTypedResult(const std::shared_ptr<FContext>& Context,
		std::shared_ptr<const void> Retention, T Value)
	{
		auto Owned = CreateOwnedResult(Context, std::move(Retention), std::any(std::move(Value)));
		// Unbox in the producer image, where the std::any RTTI is canonical.
		Owned->Data = std::any_cast<T>(&Owned->Value);
		Owned->Type = typeid(T).name();
		Owned->Size = sizeof(T);
		Owned->Alignment = alignof(T);
		return Owned;
	}

	// A lease follows the actual allocation, including externally retained results.
	// Resize is called by the allocation's executor; accounting itself is atomic.
	class FByteLease
	{
	public:
		explicit FByteLease(std::shared_ptr<FContext> Context);
		FByteLease(const FByteLease&) = delete;
		FByteLease& operator=(const FByteLease&) = delete;
		~FByteLease();
		bool TryResize(std::uint64_t Bytes) noexcept;
		std::uint64_t Size() const noexcept
		{
			return Bytes;
		}
		const std::shared_ptr<FContext>& OwnerContext() const noexcept
		{
			return Context;
		}

	private:
		std::shared_ptr<FContext> Context;
		std::uint64_t Bytes = 0;
	};
	std::shared_ptr<FByteLease> ReserveBytes(
		const std::shared_ptr<FContext>& Context, std::uint64_t Bytes, FError& Error);
	// Owned diagnostic copies remain readable after I/O shutdown; quota still applies.
	std::shared_ptr<FByteLease> ReserveSnapshotBytes(
		const std::shared_ptr<FContext>& Context, std::uint64_t Bytes, FError& Error);
	std::shared_ptr<FByteLease> ReserveBytes(
		const std::shared_ptr<FContext>& Context, std::uint64_t Bytes, const std::shared_ptr<FOperation>& Operation);

	struct FAccess
	{
		static std::shared_ptr<FContext> Context(Network::FContext& Value);
		static Network::FOperation Wrap(std::shared_ptr<FOperation> Value);
		static std::shared_ptr<FOperation> Operation(const Network::FOperation& Value);
	};

	std::shared_ptr<FOperation> Begin(const std::shared_ptr<FContext>& Context, FOperationOptions Options);
	void SetAbort(const std::shared_ptr<FOperation>& Operation, std::function<void()> Function);
	void ObserveTerminal(
		const std::shared_ptr<FOperation>& Operation, std::function<void(EOperationState, const FError&)> Function);
	void Fail(const std::shared_ptr<FOperation>& Operation, EError Code, std::string Message, int SystemCode = 0);
	void Fail(const std::shared_ptr<FOperation>& Operation, EError Code, const char* Message, int SystemCode = 0);
	void Fail(const std::shared_ptr<FOperation>& Operation, EError Code, const std::error_code& Error);
	void Running(const std::shared_ptr<FOperation>& Operation);
	// Public handle destructors must not let an allocating post escape noexcept destruction.
	template <class F>
	void OnDestroy(const std::shared_ptr<FContext>& Context, F&& Function) noexcept
	{
		try
		{
			Function();
		}
		catch (const std::bad_alloc&)
		{
			Fault(Context, EError::Allocation);
		}
		catch (...)
		{
			Fault(Context, EError::Internal);
		}
	}
	template <class T>
	std::shared_ptr<const Network::Detail::FOwnedResult> OwnResult(
		const std::shared_ptr<FContext>& Context, T Result, std::shared_ptr<const void> Retention = {})
	{
		return CreateTypedResult(Context, std::move(Retention), std::move(Result));
	}
	template <class T>
	bool Complete(const std::shared_ptr<FOperation>& Operation, T Result, std::shared_ptr<const void> Retention = {})
	{
		if (auto Context = OperationContext(Operation))
		{
			auto Value = OwnResult(Context, std::move(Result), std::move(Retention));
			return Finish(Context, Operation, EOperationState::Succeeded, {}, std::move(Value));
		}
		return false;
	}
} // namespace VeroScript::Network::Detail

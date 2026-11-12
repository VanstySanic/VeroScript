// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"
#include <functional>
#include <thread>

namespace VeroScript
{
	struct FValueRootState;
	struct FValueRootLease;
	class FValueRootOwner;

	/** Explicit ownership of a value exported by a session. Destruction only retires
	 * the preallocated slot; language cleanup always runs at an owner safe point. */
	class FValueRoot final
	{
	public:
		FValueRoot() = default;
		const FValue* Read() const noexcept;
		bool IsValid() const noexcept
		{
			return Read() != nullptr;
		}
		bool IsRegistered() const noexcept
		{
			return static_cast<bool>(Lease);
		}
		std::shared_ptr<FValueRootOwner> Owner() const noexcept;
		void Reset() noexcept
		{
			Lease.reset();
		}

	private:
		friend class FValueRootOwner;
		std::shared_ptr<FValueRootLease> Lease;
	};

	/** Session-owned registry. Cleanup executes exclusively on the creator thread.
	 * Retain/read/drain/close require its creator thread. Retirement, prepared
	 * result reserve/commit and liveness snapshots are thread safe; none invokes
	 * language cleanup. */
	class FValueRootOwner final : public std::enable_shared_from_this<FValueRootOwner>
	{
	public:
		using FCleanup = std::function<bool(std::vector<FValue>&, const std::vector<FValue>&)>;
		FValueRootOwner();
		~FValueRootOwner();
		bool Retain(FValue Value, FValueRoot& Output, std::string& Error);
		// Generation roots are invalidated before replacing their script program.
		// They share the registry's cleanup/Keep set with ordinary exported roots.
		bool RetainForGeneration(FValue Value, FValueRoot& Output, std::string& Error);
		// Explicit opt-in: Session reload may migrate this root's graph. Read()
		// returns null while that reload is pending. Ordinary copied raw handles
		// must not be used during staging; access resumes after settlement.
		bool RetainForMigration(FValue Value, FValueRoot& Output, std::string& Error);
		bool InvalidateGeneration(std::string& Error);
		bool HasExternalRoots(bool IncludeMigratable = true) const noexcept;
		// Owner-thread transaction services. Leases pin the exact root set until
		// settlement, including roots concurrently retired by a host wrapper.
		bool PrepareMigration(std::vector<FValueRoot>& Roots, std::vector<FValue>& Values, std::string& Error);
		bool CompleteMigration(std::vector<FValueRoot>& Roots, std::vector<FValue>* Values, std::string& Error);
		// Reserve before an operation that may create script-owned temporaries.
		// Commit moves into that slot without allocation, including during Closing.
		bool Reserve(FValueRoot& Output, std::string& Error);
		bool CommitReserved(FValueRoot& Reservation, FValue& Prepared, std::string& Error);
		bool Drain(const FCleanup& Cleanup, std::string& Error);
		// Native safe points retire only generation leases. Ordinary exported
		// borrows remain registered until the host call's outer safe point.
		bool DrainGeneration(const FCleanup& Cleanup, std::string& Error);
		// Thread-safe liveness snapshot for selective graph cleanup. The copies
		// do not replace registered roots. Access to the returned graphs still
		// requires the host's normal ownership and synchronization rules.
		std::vector<FValue> SnapshotLiveValues() const;
		bool HasLiveRoots() const noexcept;
		bool IsOpen() const noexcept;
		bool IsOwnerThread() const noexcept;
		void BeginClose() noexcept;
		bool Close(const FCleanup& Cleanup, std::string& Error);

	private:
		friend class FValueRoot;
		bool DrainImpl(const FCleanup& Cleanup, std::string& Error, bool GenerationOnly);
		std::shared_ptr<FValueRootState> State;
	};

	/** Traverses shared language storage once, with bounded native recursion.
	 * Returning false stops the visit. Ref descriptors are never dereferenced. */
	bool VisitValueGraph(const FValue& Value,
	    const std::function<bool(const FValue&, const void*)>& Visitor);
	bool ValueRequiresOwner(const FValue& Value);
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRun.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace VeroScript
{
	inline constexpr std::uint64_t ExecutionInstructionCapability = 1ull << 0;
	inline constexpr std::uint64_t ExecutionPollingCapability = 1ull << 1;
	inline constexpr std::uint64_t ExecutionPermissionCapability = 1ull << 2;
	inline constexpr std::uint64_t ExecutionFrameCapability = 1ull << 3;
	// Observable field writes and method interception require Core call/value
	// barriers even when no instruction, time or permission budget is enabled.
	inline constexpr std::uint64_t ExecutionObservationCapability = 1ull << 4;
	inline constexpr std::uint64_t ControlledExecutionCapabilities =
	    ExecutionInstructionCapability | ExecutionPollingCapability |
	    ExecutionPermissionCapability | ExecutionFrameCapability;

	// Borrowed only for the synchronous preparation call; never retained by code.
	struct FExecutionCheck
	{
		bool (*Check)(void*) noexcept = nullptr;
		void* Context = nullptr;
		bool Poll() const noexcept
		{
			return !Check || Check(Context);
		}
	};

	// One owner thread and one top-level execution. All execution paths share it.
	struct FExecutionGovernanceState
	{
		std::uint64_t ExecutedInstructions = 0;
		std::chrono::steady_clock::time_point Deadline = std::chrono::steady_clock::time_point::max();
		void Begin(const FExecutionLimits& Limits, std::chrono::steady_clock::time_point ExternalDeadline);
		bool Charge(std::uint64_t Units, std::uint64_t Limit) noexcept
		{
			if (Units > std::numeric_limits<std::uint64_t>::max() - ExecutedInstructions || (Limit != 0 && (Units > Limit || ExecutedInstructions > Limit - Units)))
			{
				return false;
			}
			ExecutedInstructions += Units;
			return true;
		}
	};
	enum class EExecutionInterruption : std::uint8_t
	{
		Cancelled,
		Timeout
	};

	namespace ExecutionDetail
	{
		// The same poll is used by Core and bytecode dispatch. Error reporting and
		// bounded cleanup stay in Core; no polling point or atomic read is omitted.
		template <typename TInterrupted>
		bool PollCancellationAndDeadline(const FExecutionGovernanceState& State,
		    const std::shared_ptr<FExecutionControl>& Control,
		    std::chrono::steady_clock::time_point ExternalDeadline, TInterrupted&& Interrupted)
		{
			if (Control && Control->IsCancellationRequested()) return Interrupted(EExecutionInterruption::Cancelled);
			const auto Deadline = std::min(State.Deadline, ExternalDeadline);
			if (Deadline != std::chrono::steady_clock::time_point::max() && std::chrono::steady_clock::now() >= Deadline)
			{
				return Interrupted(EExecutionInterruption::Timeout);
			}
			return true;
		}
	}

	class IExecutionGovernanceServices
	{
	public:
		virtual ~IExecutionGovernanceServices() = default;
		// The active Core frame determines function identity and canonical cost.
		virtual bool CheckStep(std::size_t InstructionOffset) noexcept = 0;
		virtual bool Poll() noexcept = 0;
		virtual bool ChargeWork(std::uint64_t Units) noexcept = 0;
		virtual std::uint64_t GetRequiredCapabilities() const noexcept = 0;
	};
}

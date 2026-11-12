// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmJitDiagnostics.h"
#include <algorithm>
#include <chrono>
#include <unordered_map>
namespace VeroScript
{
	const char *JitExecutionKindName(const EJitExecutionKind Kind) noexcept
	{
		static constexpr const char *Names[]{"vm", "vm-scalar", "native-leaf", "native-cfg", "controlled-native", "linear", "bridge", "vm-trampoline", "aot", "unknown"};
		const auto Index = static_cast<std::size_t>(Kind);
		return Index < std::size(Names) ? Names[Index] : "unknown";
	}
	const char *JitFallbackReasonName(const EJitFallbackReason Reason) noexcept
	{
		static constexpr const char *Names[]{"none", "unsupported-opcode", "unsupported-type", "unsupported-call", "ownership", "reference-parameter",
			"closure", "exception-handling", "cost-model", "execution-governance", "observation", "debugger", "hotness", "compile-budget",
			"cache-capacity", "resource-budget", "invocation-depth", "entry-unavailable", "runtime-guard"};
		const auto Index = static_cast<std::size_t>(Reason);
		return Index < std::size(Names) ? Names[Index] : "unknown";
	}
	struct FJitDiagnosticState
	{
		struct FEntry
		{
			std::uint64_t Token, Start, Children;
			std::size_t Record;
			EJitExecutionKind Kind;
			bool bFailed = false;
			bool bControlTransfer = false;
		};
		const FBytecodeModule *Module = nullptr;
		FJitDiagnosticOptions Options;
		FJitDiagnostics Snapshot;
		std::unordered_map<std::uint16_t, std::size_t> Indices;
		std::vector<FEntry> Stack;
		std::uint64_t NextToken = 1;
		std::size_t Record(const FBytecodeModule &Program, const std::uint16_t Function)
		{
			const auto Found = Indices.find(Function);
			if (Found != Indices.end()) return Found->second;
			if (Snapshot.Functions.size() >= Options.MaxFunctions)
			{
				++Snapshot.DroppedRecords;
				return SIZE_MAX;
			}
			FJitFunctionDiagnostic Item;
			Item.FunctionIndex = Function;
			Item.Name = Program.Functions[Function].Name.substr(0, 4096);
			Item.ModulePath = Program.Functions[Function].ModulePath.substr(0, 4096);
			const auto Index = Snapshot.Functions.size();
			Snapshot.Functions.push_back(std::move(Item));
			try
			{
				Indices.emplace(Function, Index);
			}
			catch (...)
			{
				Snapshot.Functions.pop_back();
				throw;
			}
			return Index;
		}
		std::uint64_t Now() const noexcept
		{
			return Options.bMeasureTime ? static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
											  std::chrono::steady_clock::now().time_since_epoch())
												  .count())
										: 0;
		}
	};
	void MarkFunctionDiagnosticOutcome(FJitDiagnosticState *State, const std::uint64_t Token,
		const bool Failed, const bool ControlTransfer) noexcept
	{
		if (!State || !Token) return;
		const auto Slot = static_cast<std::size_t>(Token & 0xFFFFu);
		if (Slot == 0 || Slot > State->Stack.size()) return;
		auto &Entry = State->Stack[Slot - 1];
		if (Entry.Token == Token)
		{
			Entry.bFailed |= Failed;
			Entry.bControlTransfer |= ControlTransfer;
		}
	}
	std::uint64_t VeroScriptVm::BeginFunctionDiagnostic(const std::uint16_t FunctionIndex, const EJitExecutionKind Kind) noexcept
	{
		if (!JitPolicy.Diagnostics.MaxFunctions || !ActiveModule || FunctionIndex >= ActiveModule->Functions.size()) return 0;
		try
		{
			if (!JitDiagnostics || JitDiagnostics->Module != ActiveModule || JitDiagnostics->Snapshot.ProgramGeneration != ProgramGeneration)
			{
				auto State = std::make_shared<FJitDiagnosticState>();
				State->Module = ActiveModule;
				State->Options = JitPolicy.Diagnostics;
				State->Options.MaxFunctions = std::min<std::size_t>(State->Options.MaxFunctions, 4096);
				State->Options.MaxDepth = std::min<std::size_t>(State->Options.MaxDepth, 32768);
				State->Snapshot.ProgramGeneration = ProgramGeneration;
				State->Snapshot.bTimingEnabled = State->Options.bMeasureTime;
				JitDiagnostics = std::move(State);
			}
			auto &State = *JitDiagnostics;
			if (State.Stack.size() >= State.Options.MaxDepth)
			{
				++State.Snapshot.DroppedRecords;
				return 0;
			}
			const auto Index = State.Record(*ActiveModule, FunctionIndex);
			if (Index == SIZE_MAX) return 0;
			// The bounded stack fits in 16 bits; direct slot lookup keeps deep
			// exceptional unwinding linear. The generation guards reused slots.
			const auto Token = (State.NextToken++ << 16u) | static_cast<std::uint64_t>(State.Stack.size() + 1u);
			State.Stack.push_back({Token, State.Now(), 0, Index, Kind});
			return Token;
		}
		catch (...)
		{
			return 0;
		} // Diagnostics must never change execution outcomes.
	}
	void VeroScriptVm::EndFunctionDiagnostic(const std::uint64_t Token, const bool Entered) noexcept
	{
		if (!Token || !JitDiagnostics || JitDiagnostics->Stack.empty()) return;
		auto &State = *JitDiagnostics;
		if (State.Stack.back().Token != Token)
		{
			++State.Snapshot.DroppedRecords;
			return;
		}
		const auto Entry = State.Stack.back();
		State.Stack.pop_back();
		if (!Entered) return;
		auto &Item = State.Snapshot.Functions[Entry.Record];
		// Borrowed-program caches can be released before the host queries the
		// completed run. Preserve its final plan while the cache still exists.
		if (BackendCache && BackendCache->GetProgram().Module == State.Module && BackendCache->GetProgram().Identity.Generation == State.Snapshot.ProgramGeneration)
		{
			try
			{
				const auto Reason = BackendCache->GetFunctionReason(Item.FunctionIndex).substr(0, 4096);
				if (std::string_view(Item.PlanReason) != Reason)
				{
					Item.PlanReason = Reason;
				}
				if (const auto *Plan = BackendCache->FindBorrowed(Item.FunctionIndex))
				{
					Item.PlannedKind = Plan->ExecutionKind;
				}
			}
			catch (...)
			{
				++State.Snapshot.DroppedRecords;
			}
		}
		++Item.Calls;
		const auto Kind = static_cast<std::size_t>(Entry.Kind) < Item.Executions.size() ? Entry.Kind : EJitExecutionKind::Unknown;
		++Item.Executions[static_cast<std::size_t>(Kind)];
		if (bHasError || Entry.bFailed)
		{
			++Item.Failures;
		}
		else if (bHasPendingScriptException || bExitRequested || Entry.bControlTransfer)
		{
			++Item.ControlTransfers;
		}
		else
		{
			++Item.Returned;
		}
		const auto Elapsed = State.Now() - Entry.Start;
		Item.InclusiveNanoseconds += Elapsed;
		Item.SelfNanoseconds += Elapsed - std::min(Elapsed, Entry.Children);
		if (!State.Stack.empty())
		{
			State.Stack.back().Children += Elapsed;
		}
	}
	void VeroScriptVm::SetFunctionDiagnosticKind(const std::uint64_t Token, const EJitExecutionKind Kind) noexcept
	{
		if (Token && JitDiagnostics && !JitDiagnostics->Stack.empty() && JitDiagnostics->Stack.back().Token == Token)
		{
			JitDiagnostics->Stack.back().Kind = Kind;
		}
	}
	void VeroScriptVm::RecordFunctionFallbackEnabled(const std::uint16_t Function, const EJitFallbackReason Reason) noexcept
	{
		if (!JitPolicy.Diagnostics.MaxFunctions || !ActiveModule || Reason == EJitFallbackReason::None || static_cast<std::size_t>(Reason) >= static_cast<std::size_t>(EJitFallbackReason::Count))
		{
			return;
		}
		const auto Token = BeginFunctionDiagnostic(Function, EJitExecutionKind::Vm);
		if (!Token) return;
		auto &State = *JitDiagnostics;
		auto &Item = State.Snapshot.Functions[State.Stack.back().Record];
		++Item.Fallbacks[static_cast<std::size_t>(Reason)];
		EndFunctionDiagnostic(Token, false);
	}
	FJitDiagnostics VeroScriptVm::GetJitDiagnostics() const
	{
		auto Snapshot = JitDiagnostics ? JitDiagnostics->Snapshot : FJitDiagnostics{};
		// Resolve the current plan once per query. A warm-up/budget rejection can
		// precede successful compilation in the same execution; retaining its first
		// text would incorrectly describe the final plan and omit native plans.
		if (BackendCache && JitDiagnostics && BackendCache->GetProgram().Module == JitDiagnostics->Module && BackendCache->GetProgram().Identity.Generation == Snapshot.ProgramGeneration)
		{
			for (auto &Item : Snapshot.Functions)
			{
				Item.PlanReason = BackendCache->GetFunctionReason(Item.FunctionIndex).substr(0, 4096);
				if (const auto *Entry = BackendCache->FindBorrowed(Item.FunctionIndex))
				{
					Item.PlannedKind = Entry->ExecutionKind;
				}
			}
		}
		return Snapshot;
	}
} // namespace VeroScript

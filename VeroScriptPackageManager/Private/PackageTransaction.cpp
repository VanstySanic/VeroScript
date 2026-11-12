// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include <algorithm>
#include <random>

namespace VeroScript::Packages::Detail
{
	namespace
	{
		void ApplyJournal(const FEnvironmentLayout& L, const J& Journal, bool New, const FWork& W)
		{
			ValidateTransactionJournal(L, Journal, W);
			// Validate the complete journal and current records before the first write.
			// Recheck each destination immediately before replay so callbacks cannot
			// silently replace a later record between validation and application.
			for (const auto& R : Journal.at("writes"))
			{
				W.Check();
				(void)TransactionRecordMatches(L, R, New, W);
			}
			for (const auto& R : Journal.at("writes"))
			{
				W.Check();
				if (TransactionRecordMatches(L, R, New, W))
				{
					continue;
				}
				auto P = U8Path(R.at("path").get<std::string>());
				const auto& Data = R.at(New ? "new" : "old");
				if (Data.is_null())
				{
					if (fs::exists(P))
					{
						fs::remove(P);
					}
				}
				else
				{
					AtomicWrite(P, Data.get_ref<const std::string&>());
				}
			}
		}
	} // namespace
	FManagementSnapshot CaptureManagementSnapshot(const FEnvironmentLayout& L, const FWork& W)
	{
		FManagementSnapshot Snapshot;
		for (const auto& P : TransactionDestinations(L))
		{
			auto Limit = P == U8Path(L.Requirements) ? W.Options.Limits.MaxManifestBytes
													 : W.Options.Limits.MaxStateBytes;
			Snapshot[P] = fs::exists(P) ? HashFile(P, W, Limit) : std::string{};
		}
		return Snapshot;
	}
	void Recover(const FEnvironmentLayout& L, const FWork& W)
	{
		auto File = U8Path(L.Management) / "transaction.json";
		if (!fs::exists(File))
		{
			return;
		}
		auto Journal = ReadRecord(File, W);
		auto Phase = Journal.is_object() ? Journal.value("phase", J{}) : J{};
		if (Phase != "applying" && Phase != "rollback" && Phase != "committed")
		{
			Fail("recovery-invalid", "unknown transaction phase");
		}
		ApplyJournal(L, Journal, Phase == "committed", W);
		fs::remove(File);
	}
	void Commit(const FEnvironmentLayout& L, const std::vector<std::pair<fs::path, std::string>>& Writes,
				const FManagementSnapshot& Expected, const FWork& W)
	{
		W.Check();
		auto Allowed = TransactionDestinations(L);
		auto Next = Expected;
		// Include records this operation retains, so a changed lock or store owner
		// cannot invalidate a plan that only writes configuration or cleanup state.
		J Previous = J::object();
		for (const auto& P : Allowed)
		{
			auto Limit = P == U8Path(L.Requirements) ? W.Options.Limits.MaxManifestBytes
													 : W.Options.Limits.MaxStateBytes;
			auto Bytes = fs::exists(P) ? J(Read(P, Limit, &W)) : J{};
			auto I = Expected.find(P);
			if (I == Expected.end() ||
				I->second != (Bytes.is_null() ? std::string{} : Hash(Bytes.get_ref<const std::string&>())))
			{
				Fail("environment-changed",
					 "environment records changed during the operation; retry with the "
					 "current configuration",
					 NativePathText(P));
			}
			Previous[NativePathText(P)] = std::move(Bytes);
		}
		J Journal = {{"format", "veroscript-package-transaction"},
					 {"formatVersion", FormatVersion},
					 {"management", L.Management},
					 {"phase", "applying"},
					 {"writes", J::array()}};
		std::set<fs::path> Destinations;
		for (const auto& R : Writes)
		{
			if (!Allowed.count(R.first) || !Destinations.insert(R.first).second)
			{
				Fail("transaction-path", "unrecognized or duplicate management destination");
			}
			auto Limit = R.first == U8Path(L.Requirements) ? W.Options.Limits.MaxManifestBytes
														   : W.Options.Limits.MaxStateBytes;
			if (R.second.size() > Limit)
			{
				Fail("state-limit", "management record exceeds its budget");
			}
			Journal["writes"].push_back(
				{{"path", NativePathText(R.first)}, {"old", Previous.at(NativePathText(R.first))}, {"new", R.second}});
			Next[R.first] = Hash(R.second);
		}
		auto File = U8Path(L.Management) / "transaction.json";
		auto Bytes = Journal.dump();
		// The completed phase is one byte longer than applying/rollback. Reserve
		// it before the first write so every persisted phase remains readable.
		if (Bytes.size() >= W.Options.Limits.MaxJournalBytes)
		{
			Fail("journal-limit", "transaction journal exceeds its budget");
		}
		AtomicWrite(File, Bytes);
		try
		{
			ApplyJournal(L, Journal, true, W);
			W.Check();
			// Replay callbacks may edit retained records or an earlier write. Verify
			// the complete result without another host callback, keeping the deadline.
			FClientOptions VerificationOptions = W.Options;
			VerificationOptions.CheckCancelled = {};
			FWork VerificationWork{VerificationOptions};
			VerificationWork.Started = W.Started;
			for (const auto& P : Allowed)
			{
				VerificationWork.Check();
				const auto Limit = P == U8Path(L.Requirements) ? W.Options.Limits.MaxManifestBytes
																 : W.Options.Limits.MaxStateBytes;
				const auto Current = fs::exists(P) ? HashFile(P, VerificationWork, Limit) : std::string{};
				if (Current != Next.at(P))
				{
					Fail("environment-changed", "environment record changed during transaction replay",
						 NativePathText(P));
				}
			}
			Journal["phase"] = "committed";
			AtomicWrite(File, Journal.dump());
		}
		catch (...)
		{
			auto Original = std::current_exception();
			try
			{
				Journal["phase"] = "rollback";
				AtomicWrite(File, Journal.dump());
				FClientOptions RecoveryOptions = W.Options;
				RecoveryOptions.CheckCancelled = {};
				RecoveryOptions.Limits.TimeoutMilliseconds = 0;
				FWork RecoveryWork{RecoveryOptions};
				ApplyJournal(L, Journal, false, RecoveryWork);
				fs::remove(File);
			}
			catch (...)
			{
				Fail("recovery-required", "transaction failed and requires recovery "
										  "before the environment can be read");
			}
			std::rethrow_exception(Original);
		}
		// Committed state is usable even if removing a redundant journal fails;
		// the next writer can finish that cleanup without rerunning hooks.
		std::error_code Error;
		fs::remove(File, Error);
	}
}

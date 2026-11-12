// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"

namespace VeroScript::Packages::Detail
{
	std::set<fs::path> TransactionDestinations(const FEnvironmentLayout& L)
	{
		return {U8Path(L.Requirements), U8Path(L.Lockfile), U8Path(L.State),
				U8Path(L.Packages) / ".vero-owner.json"};
	}
	void ValidateTransactionJournal(const FEnvironmentLayout& L, const J& Journal, const FWork& W)
	{
		if (!Journal.is_object() || Journal.value("format", J{}) != "veroscript-package-transaction" ||
			Journal.value("formatVersion", J{}) != FormatVersion ||
			Journal.value("management", J{}) != L.Management || !Journal.contains("writes") ||
			!Journal.at("writes").is_array())
		{
			Fail("recovery-invalid", "invalid or foreign transaction journal");
		}
		const auto Allowed = TransactionDestinations(L);
		std::set<fs::path> Seen;
		if (Journal.at("writes").size() > Allowed.size())
		{
			Fail("recovery-invalid", "transaction contains too many destinations");
		}
		for (const auto& R : Journal.at("writes"))
		{
			W.Check();
			if (!R.is_object() || !R.contains("path") || !R.at("path").is_string() ||
				!R.contains("old") || !R.contains("new"))
			{
				Fail("recovery-invalid", "transaction record is incomplete");
			}
			const auto P = U8Path(R.at("path").get<std::string>());
			if (!Allowed.count(P))
			{
				Fail("recovery-layout", "recovery requires the original lockfile and package-root selection");
			}
			if (!Seen.insert(P).second)
			{
				Fail("recovery-invalid", "transaction contains a duplicate destination", NativePathText(P));
			}
			const auto Limit = P == U8Path(L.Requirements) ? W.Options.Limits.MaxManifestBytes
														 : W.Options.Limits.MaxStateBytes;
			for (const auto* Key : {"old", "new"})
			{
				const auto& Data = R.at(Key);
				if (!Data.is_null() &&
					(!Data.is_string() || Data.get_ref<const std::string&>().size() > Limit))
				{
					Fail("recovery-invalid", "transaction record has invalid data or exceeds its budget",
						 NativePathText(P));
				}
			}
		}
	}
	bool TransactionRecordMatches(const FEnvironmentLayout& L, const J& R, bool New, const FWork& W)
	{
		const auto P = U8Path(R.at("path").get<std::string>());
		NoLinks(P, true);
		const bool Present = fs::exists(P);
		const auto Limit = P == U8Path(L.Requirements) ? W.Options.Limits.MaxManifestBytes
													 : W.Options.Limits.MaxStateBytes;
		const auto Bytes = Present ? Read(P, Limit, &W) : std::string{};
		const auto Matches = [&](const J& Data)
		{
			return Present ? Data.is_string() && Bytes == Data.get_ref<const std::string&>()
						   : Data.is_null();
		};
		if (!Matches(R.at("old")) && !Matches(R.at("new")))
		{
			Fail("recovery-conflict", "management record changed outside the transaction; "
				 "preserve the edit and resolve the journal before retrying", NativePathText(P));
		}
		return Matches(R.at(New ? "new" : "old"));
	}
} // namespace VeroScript::Packages::Detail

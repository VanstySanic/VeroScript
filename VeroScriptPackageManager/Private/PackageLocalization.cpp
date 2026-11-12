// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInternal.h"
#include <algorithm>

namespace VeroScript::Packages::Detail
{
	namespace
	{
		using Messages = std::map<std::string, I18n::FI18nMessage>;
		class FDisplayResources final : public I18n::II18nResourceSnapshot
		{
		public:
			I18n::EI18nLookupResult Lookup(const I18n::FI18nLookupRequest& R, const I18n::FI18nWork& W,
										   I18n::FI18nMessage& Out, I18n::FI18nError& E) const override
			{
				if (W.Check && !W.Check(W.UserData, 1))
				{
					E.Status = I18n::EI18nStatus::Interrupted;
					E.Diagnostic.Message = "package display cancelled";
					return I18n::EI18nLookupResult::Failed;
				}
				if (!Authorized.count(std::string(R.Domain)))
				{
					E.Status = I18n::EI18nStatus::InvalidArgument;
					E.Diagnostic.Message = "package text domain is outside this display snapshot";
					return I18n::EI18nLookupResult::Failed;
				}
				auto D = Domains.find(std::string(R.Domain));
				if (D == Domains.end())
				{
					return I18n::EI18nLookupResult::NotFound;
				}
				auto L = D->second.find(std::string(R.Language));
				if (L == D->second.end())
				{
					return I18n::EI18nLookupResult::NotFound;
				}
				auto M = L->second.find(std::string(R.Key));
				if (M == L->second.end())
				{
					return I18n::EI18nLookupResult::NotFound;
				}
				Out = M->second;
				return I18n::EI18nLookupResult::Found;
			}

			std::set<std::string> Authorized;

			std::map<std::string, std::map<std::string, Messages>> Domains;
		};
		bool StandardKey(const std::string& S)
		{
			return S == "package.displayName" || S == "package.description" || S == "install.before" ||
				   S == "install.after" ||
				   (S.rfind("inputs.", 0) == 0 && S.size() > 19 && S.substr(S.size() - 12) == ".description");
		}
		void I18nFailure(const I18n::FI18nError& E)
		{
			FFailure F("package-text", E.Diagnostic.Message);
			F.Diagnostic.DetailsJson = J{
				{"status", static_cast<unsigned>(E.Status)},
				{"messageId", E.Diagnostic.MessageId},
				{"arguments", E.Diagnostic.MessageArgs},
				{"domain", E.Domain},
				{"key", E.Key},
				{"language", E.Language},
				{"argument",
				 E.ArgumentName}}.dump();
			throw F;
		}
		I18n::FI18nWork Work(const FWork& W)
		{
			return {[](void* P, std::uint64_t)
				{
					static_cast<const FWork*>(P)->Check();
					return true;
				},
				const_cast<FWork*>(&W), W.Options.Limits.MaxDisplayBytes};
		}
	} // namespace
	std::shared_ptr<I18n::FI18nServices> ScopedDisplayServices(
		std::shared_ptr<const I18n::II18nResourceSnapshot> Resources, std::set<std::string> Domains)
	{
		struct FScoped final : I18n::II18nResourceSnapshot
		{
			I18n::EI18nLookupResult Lookup(const I18n::FI18nLookupRequest& R, const I18n::FI18nWork& W,
										   I18n::FI18nMessage& Out, I18n::FI18nError& E) const override
			{
				if (!Allowed.count(std::string(R.Domain)))
				{
					E.Status = I18n::EI18nStatus::InvalidArgument;
					E.Diagnostic.Message = "package text domain is outside this display snapshot";
					return I18n::EI18nLookupResult::Failed;
				}
				return Base->Lookup(R, W, Out, E);
			}

			std::shared_ptr<const I18n::II18nResourceSnapshot> Base;

			std::set<std::string> Allowed;
		};
		auto View = std::make_shared<FScoped>();
		View->Base = std::move(Resources);
		View->Allowed = std::move(Domains);
		auto S = std::make_shared<I18n::FI18nServices>();
		I18n::FI18nError E;
		if (!S->PublishResources(View, E))
		{
			I18nFailure(E);
		}
		return S;
	}
	std::shared_ptr<I18n::FI18nServices> DisplayServices(const std::vector<FInstalledPackage>& Packages,
														 const FWork& W)
	{
		auto View = std::make_shared<FDisplayResources>();
		for (const auto& P : Packages)
		{
			W.Check();
			View->Authorized.insert(P.TextDomain);
			auto M = Manifest(Parse(P.ManifestJson, W.Options.Limits));
			if (!M.contains("localization"))
			{
				continue;
			}
			auto Root = U8Path(P.Path) / U8Path(M["localization"]["root"].get<std::string>());
			NoLinks(Root);
			if (!fs::is_directory(Root))
			{
				Fail("localization-root", "localization root is not a directory", P.Name);
			}
			auto& Languages = View->Domains[P.TextDomain];
			bool Baseline = false;
			for (const auto& Culture : fs::directory_iterator(Root))
			{
				W.Check();
				NoLinks(Culture.path());
				if (!Culture.is_directory())
				{
					Fail("localization-layout", "localization root may contain only language directories");
				}
				std::string Tag = NativePathText(Culture.path().filename()), Normalized;
				I18n::FI18nError E;
				if (!I18n::NormalizeLanguageTag(Tag, Normalized, E) || Normalized != Tag)
				{
					Fail("localization-tag", "language directories must use normalized BCP 47 tags", Tag);
				}
				auto& Items = Languages[Tag];
				bool HasJson = false;
				for (const auto& File : fs::recursive_directory_iterator(Culture.path()))
				{
					W.Check();
					NoLinks(File.path());
					if (File.is_directory())
					{
						continue;
					}
					if (!File.is_regular_file() || File.path().extension() != ".json")
					{
						Fail("localization-file", "localization trees may contain only JSON files");
					}
					HasJson = true;
					auto Bytes = Read(File.path(), W.Options.Limits.MaxManifestBytes, &W);
					W.ChargeMetadata(Bytes.size());
					auto Values = Parse(Bytes, W.Options.Limits);
					if (!Values.is_object())
					{
						Fail("localization-format", "localization file must be a message object");
					}
					for (auto I = Values.begin(); I != Values.end(); ++I)
					{
						if (I.key().empty() || I.key().find('\0') != std::string::npos)
						{
							Fail("localization-key", "invalid message key");
						}
						I18n::FI18nMessage Message;
						if (I.value().is_string())
						{
							Message = {I.value().get<std::string>(), "literal"};
						}
						else
						{
							Keys(I.value(), {"pattern", "formatId"}, "localization.message");
							String(I.value().at("pattern"), "message.pattern");
							String(I.value().at("formatId"), "message.formatId");
							Message = {I.value()["pattern"], I.value()["formatId"]};
							if (Message.FormatId != "literal" && Message.FormatId != "vero.named")
							{
								Fail("localization-format",
									 "package display uses only literal or vero.named");
							}
						}
						if (!Items.emplace(I.key(), std::move(Message)).second)
						{
							Fail("localization-duplicate", "duplicate key across localization files",
								 I.key());
						}
					}
				}
				if (Tag == "en-US" && HasJson)
				{
					Baseline = true;
				}
			}
			if (!Baseline)
			{
				Fail("localization-baseline", "localization requires at least one en-US JSON baseline file",
					 P.Name);
			}
			auto& Base = Languages.at("en-US");
			I18n::FI18nArguments Args;
			I18n::FI18nError E;
			Args.Set("package.name", I18n::FI18nArgument(P.Name), E);
			Args.Set("package.version", I18n::FI18nArgument(P.Version), E);
			for (const auto& Language : Languages)
			{
				for (const auto& Item : Language.second)
				{
					auto Original = Base.find(Item.first);
					if (Original == Base.end())
					{
						Fail("localization-extra",
							 "translation contains a key absent from the English baseline", Item.first);
					}
					if (Original->second.FormatId != Item.second.FormatId)
					{
						Fail("localization-grammar", "translation changes the baseline format ID",
							 Item.first);
					}
					if (StandardKey(Item.first) && Item.second.FormatId == "vero.named")
					{
						std::string Out;
						if (!I18n::TryFormatNamed(Item.second.Pattern, Args, Out, E, Work(W)))
						{
							I18nFailure(E);
						}
					}
					else if (Item.second.Pattern.size() > W.Options.Limits.MaxDisplayBytes)
					{
						Fail("localization-limit", "display message exceeds its output budget");
					}
				}
			}
		}
		auto Services = std::make_shared<I18n::FI18nServices>();
		I18n::FI18nError E;
		if (!Services->PublishResources(View, E))
		{
			I18nFailure(E);
		}
		return Services;
	}
	std::string Render(const FInstalledPackage& P, const std::string& Key,
					   const I18n::FI18nSnapshot& Snapshot, const FWork& W)
	{
		W.Check();
		I18n::FI18nArguments Args;
		I18n::FI18nError E;
		Args.Set("package.name", I18n::FI18nArgument(P.Name), E);
		Args.Set("package.version", I18n::FI18nArgument(P.Version), E);
		I18n::FI18nText Text;
		I18n::FI18nText::Message(P.TextDomain, Key, Args, Text, E);
		I18n::FI18nResolvedText Result;
		if (Snapshot.TryResolve(Text, Result, E, Work(W)))
		{
			return Result.Value;
		}
		if (E.Status != I18n::EI18nStatus::MissingMessage)
		{
			I18nFailure(E);
		}
		auto M = Parse(P.ManifestJson, W.Options.Limits);
		std::string Fallback;
		if (Key == "package.displayName")
		{
			Fallback = M.value("displayName", P.Name);
		}
		else if (Key == "package.description")
		{
			Fallback = M.value("description", "");
		}
		else if (Key == "install.before" || Key == "install.after")
		{
			Fallback =
				M.value("install", J::object()).value("messages", J::object()).value(Key.substr(8), "");
		}
		if (Key.rfind("inputs.", 0) == 0 && Key.size() > 19 && Key.substr(Key.size() - 12) == ".description")
		{
			Fallback = M.value("install", J::object())
						   .value("inputs", J::object())
						   .value(Key.substr(7, Key.size() - 19), J::object())
						   .value("description", std::string{});
		}
		if (Fallback.size() > W.Options.Limits.MaxDisplayBytes)
		{
			Fail("localization-limit", "fallback display exceeds its output budget");
		}
		W.Check();
		return Fallback;
	}
} // namespace VeroScript::Packages::Detail

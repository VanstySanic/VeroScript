// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiLocalization.h"

namespace VeroScript::Gui
{
	namespace
	{
		constexpr const char* LocalizationId = "veroscript.gui.localization";
		class FLocalization final : public IFrameService
		{
		public:
			explicit FLocalization(std::shared_ptr<I18n::FI18nContext> Value)
				: Context(std::move(Value)), Snapshot(Context->CaptureSnapshot())
			{
			}
			bool BeginFrame(double, std::string& Error) override
			{
				Error.clear();
				Snapshot = Context->CaptureSnapshot();
				return Snapshot.IsValid();
			}
			std::shared_ptr<I18n::FI18nContext> Context;
			I18n::FI18nSnapshot Snapshot;
		};

		std::shared_ptr<FLocalization> Find(const std::shared_ptr<FContext>& Context, std::string& Error)
		{
			Error.clear();
			if (!Context)
			{
				Error = "gui.context_closed";
				return {};
			}
			auto Service = Context->GetFrameService(LocalizationId, Error);
			auto Result = std::dynamic_pointer_cast<FLocalization>(Service);
			if (!Result && Error.empty())
			{
				Error = "gui.localization_unavailable";
			}
			return Result;
		}
	} // namespace

	bool SetLocalization(const std::shared_ptr<FContext>& Context, std::shared_ptr<I18n::FI18nContext> Localization,
						 std::string& Error)
	{
		if (!Context)
		{
			Error = "gui.context_closed";
			return false;
		}
		return Context->SetFrameService(
			LocalizationId, Localization ? std::make_shared<FLocalization>(std::move(Localization)) : nullptr, Error);
	}

	std::shared_ptr<I18n::FI18nContext> GetLocalization(const std::shared_ptr<FContext>& Context, std::string& Error)
	{
		auto State = Find(Context, Error);
		return State ? State->Context : nullptr;
	}

	I18n::FI18nSnapshot GetLocalizationSnapshot(const std::shared_ptr<FContext>& Context, std::string& Error)
	{
		auto State = Find(Context, Error);
		return State ? State->Snapshot : I18n::FI18nSnapshot{};
	}
} // namespace VeroScript::Gui

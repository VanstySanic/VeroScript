// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFontBackend.h"
#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiServices.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptPath.h"
#include "VeroScriptStb.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
} // namespace

#if defined(_WIN32)
int wmain(int Count, wchar_t** Args)
#else
int main(int Count, char** Args)
#endif
{
	using namespace VeroScript;
	try
	{
		Require(Count == 2, "provider module path is required");
		FNativeExtensionLoadOptions Load;
		Load.ExpectedId = "veroscript-rmlui";
		for (bool Deferred : {false, true})
		{
			std::string Error;
			auto Module = LoadNativeExtension(PathUtil::ToUtf8(std::filesystem::absolute(Args[1])), Error, Load);
			Require(Module != nullptr, Error);
			std::weak_ptr<const FLoadedNativeExtension> Loaded = Module;
			auto Registry = std::make_unique<FNativeExtensionRegistry>();
			FNativeExtensionRegistrationOptions Registration;
			Registration.Origin.ProviderId = "gui-provider-test";
			Require(Registry->Add(Module, Registration, Error), Error);
			auto Providers = Registry->GetProviders();
			Require(Providers.size() == 1 && Providers[0].ServiceId == Gui::ServiceId &&
			        Providers[0].BackendId == "rmlui" && Providers[0].ContractId == Gui::ContractId &&
			        Providers[0].ContractVersion == Gui::Version,
			    "GUI provider descriptor mismatch");
			Gui::FBackendOptions Resources;
			Resources.Images = Stb::CreateImageService();
			Resources.Fonts = FontBackend::CreateFontService();
			Resources.TextLayouts = FontBackend::CreateTextLayoutService();
			auto Backend = Gui::CreateRegisteredGuiBackend(Providers[0], std::move(Resources), {}, Error);
			Require(Backend != nullptr, Error);
			auto* BackendView = Backend.get();
			auto Context = Gui::FContext::Create(std::move(Backend), {100, 100, 1}, Error);
			auto Text = Gui::FWidget::Create();
			Gui::FControlOptions TextOptions;
			TextOptions.Kind = Gui::EControlKind::TextBox;
			TextOptions.Text = "abc";
			Require(Context && Text->SetControlOptions(TextOptions, Error) &&
			        Text->Set(Gui::EProperty::Height, Gui::FLength{Gui::ELengthUnit::Pixel, 40}, Error) &&
			        Text->Set(Gui::EProperty::Focusable, true, Error) && Context->SetRoot(Text, Error) &&
			        Context->Update(0, Error),
			    Error);
			Providers.clear();
			Registry.reset();
			Module.reset();
			Require(!Loaded.expired(), "GUI backend did not retain provider code");
			Require(Context->Update(1, Error), Error);
			bool InputEnabled = false;
			Gui::FTextInputServices Services;
			Services.SetInputArea = [&](bool Enabled, const auto&, auto, auto&)
			{
				InputEnabled = Enabled;
				return true;
			};
			Require(Context->SetTextInputServices(Services, Error) && Context->SetFocus(Text, Error) && InputEnabled,
			    "registered backend did not forward text focus: " + Error);
			Require(Text->SetTextSelection(1, 2, Error), Error);
			Platform::FEvent Input;
			Input.WindowId = 1;
			Input.Type = Platform::EEventType::TextInput;
			Input.Text = "z";
			Gui::FEventResult EventResult;
			Require(Context->ProcessEvent(Input, EventResult, Error), Error);
			Require(Text->GetControlOptions().Text == "azc",
			    "registered backend text edit result: " + Text->GetControlOptions().Text);
			Input.Type = Platform::EEventType::TextEditing;
			Input.Text = "xy";
			Require(Context->ProcessEvent(Input, EventResult, Error) &&
			        BackendView->IsTextComposing(Text->GetIdentity()) && Text->GetControlOptions().Text == "azc",
			    "registered backend did not forward IME state: " + Error);
			Require(Text->SetTextSelection(0, 0, Error) && !BackendView->IsTextComposing(Text->GetIdentity()),
			    "registered backend did not forward selection or cancel composition: " + Error);
			if (Deferred)
			{
				std::thread Worker([Owned = std::move(Context)]() mutable
				{
					Owned.reset();
				});
				Worker.join();
				Require(!Loaded.expired(), "retired GUI backend released provider code early");
				Require(Gui::FContext::CollectRetired(Error), Error);
			}
			else
			{
				Require(Context->Close(Error), Error);
				Context.reset();
			}
			Require(Loaded.expired(), "closed GUI backend retained the native module");
			Require(!InputEnabled, "provider shutdown left platform text input enabled");
		}
		std::cout << "RmlUi native provider text editing, IME, ownership and deferred cleanup passed\n";
		return 0;
	}
	catch (const std::exception& Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}

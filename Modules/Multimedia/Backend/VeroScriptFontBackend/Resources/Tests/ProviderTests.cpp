// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFont.h"
#include "VeroScriptFontLayout.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptPath.h"
#if defined(VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER)
#include "VeroScriptFontBackendModule.h"
#endif
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

namespace
{
	enum class ERetainedObject
	{
		Face,
		Analysis,
		Snapshot
	};
	void Require(bool Value, const std::string &Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
} // namespace

#if defined(_WIN32)
int wmain(int Count, wchar_t **Args)
#else
int main(int Count, char **Args)
#endif
{
	using namespace VeroScript;
	try
	{
		std::string Error;
		FNativeExtensionLoadOptions Load;
		Load.ExpectedId = "veroscript-font-backend";
		#if !defined(VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER)
		Require(Count == 2, "font provider path required");
		#endif
		for (const auto Retained : {ERetainedObject::Face, ERetainedObject::Analysis, ERetainedObject::Snapshot})
		{
			#if defined(VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER)
			auto Module = QueryNativeExtension(veroscript_font_backend_query, {}, Error, Load);
			#else
			auto Module = LoadNativeExtension(PathUtil::ToUtf8(std::filesystem::absolute(Args[1])), Error, Load);
			#endif
			Require(Module != nullptr, Error);
			std::weak_ptr<const FLoadedNativeExtension> Loaded = Module;
			auto Registry = std::make_unique<FNativeExtensionRegistry>();
			FNativeExtensionRegistrationOptions Registration;
			Registration.Origin.ProviderId = "font-provider-test";
			Require(Registry->Add(Module, Registration, Error), Error);
			auto Providers = Registry->GetProviders();
			Require(Providers.size() == 2, "expected independent font and text-layout providers");
			Require(!CreateNativeService(Providers, Font::TextLayoutServiceId, "missing-layout-backend",
				Font::TextLayoutContractId, Font::TextLayoutContractVersion, {}, Error) &&
				Error.find("missing-layout-backend") != std::string::npos,
				"an explicit unavailable layout backend must report its ID");
			auto Instance = CreateNativeService(Providers, Font::ServiceId, Font::DefaultBackendId,
				Font::ContractId, Font::ContractVersion, {}, Error);
			Require(Instance != nullptr, Error);
			auto *Service = static_cast<Font::IService *>(Instance->Get());
			auto Face = Service->Load(Font::GetDefaultFontData(), 0, Error);
			Require(Face != nullptr, Error);
			auto LayoutInstance = CreateNativeService(Providers, Font::TextLayoutServiceId, Font::DefaultBackendId,
				Font::TextLayoutContractId, Font::TextLayoutContractVersion, {}, Error);
			Require(LayoutInstance != nullptr, Error);
			auto *LayoutService = static_cast<Font::ITextLayoutService *>(LayoutInstance->Get());
			auto Analysis = LayoutService->Analyze("abc אבג", Font::EDirection::Auto, Error);
			Require(Analysis != nullptr, Error);
			auto Selected = std::shared_ptr<Font::ITextLayoutService>(LayoutInstance, LayoutService);
			auto Snapshot = Font::FTextLayout::Create(Selected, "abc אבג", Font::EDirection::Auto, Error);
			Require(Snapshot != nullptr, Error);
			Selected.reset();
			LayoutInstance.reset();
			// Retain only one kind of object, so another lease cannot mask a missing one.
			if (Retained != ERetainedObject::Face)
			{
				Face.reset();
			}
			if (Retained != ERetainedObject::Analysis)
			{
				Analysis.reset();
			}
			if (Retained != ERetainedObject::Snapshot)
			{
				Snapshot.reset();
			}
			Providers.clear();
			Registry.reset();
			Module.reset();
			Instance.reset();
			Require(!Loaded.expired(), "retained object released its provider code");
			std::vector<Font::FTextRun> Runs;
			if (Face)
			{
				Font::FShapedText Text;
				Font::FBitmap Bitmap;
				Require(Face->Shape("office", {}, Text, Error) && !Text.Glyphs.empty() &&
				        Face->Rasterize(Text.Glyphs.front().Glyph, 16, Bitmap, Error) && !Bitmap.Pixels.empty(),
				    Error);
			}
			if (Analysis)
			{
				Require(Analysis->GetVisualRuns(0, 10, Runs, Error) && !Runs.empty(),
				    "retained analysis lost its module lease");
			}
			if (Snapshot)
			{
				Require(Snapshot->GetVisualRuns(0, 10, Runs, Error) && !Runs.empty(),
				    "retained layout snapshot lost its provider instance");
			}
			Face.reset();
			Analysis.reset();
			Snapshot.reset();
			Require(Loaded.expired(), "released object retained the provider code");
		}
		std::cout << "Font/layout provider selection and module lifetimes passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}

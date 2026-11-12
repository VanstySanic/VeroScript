// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiTools.h"
#include "VeroScriptGuiScript.h"
#include "Compilation/VeroScriptGuiSource.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"
#include "VeroScriptGuiArtifact.h"
#include <filesystem>

namespace VeroScript::Gui
{
	namespace
	{
		FCompileResult Failure(std::string_view Label, std::string Message)
		{
			FCompileResult Result;
			Result.Error.MessageId = "gui.tool.configuration";
			Result.Error.Message = std::move(Message);
			Result.Error.Location.FileLabel = std::string(Label);
			Result.Diagnostics.push_back(Result.Error);
			return Result;
		}

		bool Configure(FCompileOptions& Options, const FGuiCompilationConfiguration& Configuration,
					   std::shared_ptr<const IHostBindingCatalog>& OwnedCatalog, std::string& Error)
		{
			const auto* Existing = Options.ModuleEnvironment
									   ? Options.ModuleEnvironment->GetDescription().Bindings.get()
									   : Options.Bindings;
			if (!Existing || !Existing->FindType("VS.Gui.__NativeWidget"))
			{
				const auto* Registry = dynamic_cast<const FHostBindingRegistry*>(Existing);
				if ((Existing && !Registry) || (!Existing && Options.Natives && !Options.Natives->empty()))
				{
					Error = "gui.tool.requires_gui_catalog: register GUI bindings in the supplied catalog";
					return false;
				}
				auto Candidate = Registry ? std::make_shared<FHostBindingRegistry>(*Registry)
										  : std::make_shared<FHostBindingRegistry>();
				if (!RegisterScriptBindings(*Candidate, Error))
				{
					return false;
				}
				OwnedCatalog = std::move(Candidate);
				Options.Bindings = OwnedCatalog.get();
				if (Options.ModuleEnvironment)
				{
					auto Description = Options.ModuleEnvironment->GetDescription();
					Description.Bindings = OwnedCatalog;
					Options.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
					if (!Options.ModuleEnvironment)
					{
						return false;
					}
				}
			}
			FEngineConfig Sources;
			Sources.ModuleEnvironment = Options.ModuleEnvironment;
			Sources.CompileProcessing = Options.CompileProcessing;
			Sources.ImplicitImportPaths = Options.ImplicitImportPaths;
			Sources.SourceOverlays = Options.SourceOverlays;
			if (!ConfigureGuiScriptEngine(Sources, Configuration, Error))
			{
				return false;
			}
			Options.ModuleEnvironment = std::move(Sources.ModuleEnvironment);
			Options.CompileProcessing = std::move(Sources.CompileProcessing);
			Options.ImplicitImportPaths = std::move(Sources.ImplicitImportPaths);
			Options.SourceOverlays = std::move(Sources.SourceOverlays);
			return true;
		}
	} // namespace

	FCompileResult CompileGuiSource(std::string_view Source, std::string_view FileLabel, const FCompileOptions& Options,
									const FGuiCompilationConfiguration& Configuration)
	{
		auto Core = Options;
		std::shared_ptr<const IHostBindingCatalog> Catalog;
		std::string Error;
		if (!Configure(Core, Configuration, Catalog, Error))
		{
			return Failure(FileLabel, std::move(Error));
		}
		auto Result = VeroScriptCompile::CompileSource(Source, FileLabel, Core);
		if (Result.Module && Catalog)
		{
			Result.Module->LifetimeAnchors.push_back(std::move(Catalog));
		}
		return Result;
	}

	FCompileResult CompileGuiFile(const std::string& Path, const FCompileOptions& Options,
								  const FGuiCompilationConfiguration& Configuration)
	{
		const auto Input = PathUtil::FromUtf8(Path);
		const auto Extension = PathUtil::ToUtf8(Input.extension());
		if (Extension == ".vswml")
		{
			return CompileGuiSource("import " + GuiQuote(PathUtil::ToUtf8(std::filesystem::absolute(Input))) +
										"; void main(){}",
									Path + ".gui.vs", Options, Configuration);
		}
		if (Extension != ".vs")
		{
			return Failure(Path, "gui.tool.entry_extension: expected .vs or .vswml");
		}
		auto Core = Options;
		std::shared_ptr<const IHostBindingCatalog> Catalog;
		std::string Error;
		if (!Configure(Core, Configuration, Catalog, Error))
		{
			return Failure(Path, std::move(Error));
		}
		auto Result = VeroScriptCompile::CompileFile(Path, Core);
		if (Result.Module && Catalog)
		{
			Result.Module->LifetimeAnchors.push_back(std::move(Catalog));
		}
		return Result;
	}

	bool BakeGuiApplication(const FCompileResult& Compilation, FPackageBuildOptions Options,
							std::vector<std::uint8_t>& Bytes, std::string& Error)
	{
		Error.clear();
		if (!Compilation.bSuccess || !Compilation.Module || Options.Kind != EPackageKind::Application)
		{
			Error = "gui.tool.invalid_application_compilation";
			return false;
		}
		return BuildGuiArtifact(Compilation, std::move(Options), Bytes, Error);
	}
	bool BakeGuiApplication(const FCompileResult& Compilation, FPackageBuildOptions Options,
							const FGuiAssetOptions& Assets, std::vector<std::uint8_t>& Bytes, std::string& Error)
	{
		Error.clear();
		if (!Compilation.bSuccess || !Compilation.Module || Options.Kind != EPackageKind::Application)
		{
			Error = "gui.tool.invalid_application_compilation";
			return false;
		}
		return BuildGuiArtifact(Compilation, std::move(Options), Bytes, Error, Assets);
	}
} // namespace VeroScript::Gui

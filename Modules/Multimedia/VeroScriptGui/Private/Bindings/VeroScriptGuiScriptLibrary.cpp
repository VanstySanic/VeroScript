// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiScriptLibrary.h"
#include "VeroScriptGuiEmbeddedLibrary.h"
#include "VeroScriptGuiResources.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"
#include <filesystem>
#include <algorithm>
#include <array>

namespace VeroScript::Gui
{
	namespace
	{
		struct FSource
		{
			const char* Name;
			const char* Source;
		};
		const std::array<FSource, 13> Sources{{{"framework.vs", EmbeddedGuiScriptLibrary},
		    {"Gui/Widget.vs", EmbeddedGuiWidgetLibrary}, {"Gui/Runtime.vs", EmbeddedGuiRuntimeLibrary},
		    {"Gui/Binding.vs", EmbeddedGuiBindingLibrary}, {"Gui/Resources.vs", EmbeddedGuiResourcesLibrary},
		    {"Gui/Controls.vs", EmbeddedGuiControlsLibrary},
		    {"Gui/CompositeSupport.vs", EmbeddedGuiCompositeLibrary},
		    {"Gui/Collections.vs", EmbeddedGuiCollectionsLibrary}, {"Gui/Table.vs", EmbeddedGuiTableLibrary},
		    {"Gui/Virtualization.vs", EmbeddedGuiVirtualizationLibrary},
		    {"Gui/Navigation.vs", EmbeddedGuiNavigationLibrary}, {"Gui/Menus.vs", EmbeddedGuiMenusLibrary},
		    {"Gui/Dialogs.vs", EmbeddedGuiDialogsLibrary}}};

		struct FDefaultDependencyReader
		{
			std::vector<FSourceOverlay> Overlays;
			bool operator()(std::string_view Name, std::string& Text) const
			{
				const auto Key = PathUtil::IdentityKey(std::string(Name));
				for (auto It = Overlays.rbegin(); It != Overlays.rend(); ++It)
				{
					if (PathUtil::IdentityKey(It->Path) == Key)
					{
						Text = It->Source;
						return true;
					}
				}
				std::vector<std::uint8_t> Bytes;
				std::string Error;
				if (!ReadGuiResourceFile(std::string(Name), Bytes, Error))
				{
					Text.clear();
					return false;
				}
				Text.assign(Bytes.begin(), Bytes.end());
				return true;
			}
		};
	} // namespace

	std::string_view GetGuiScriptLibrarySource()
	{
		return EmbeddedGuiScriptLibrary;
	}

	std::string_view GetGuiCompilationFingerprint()
	{
		return EmbeddedGuiCompilationFingerprint;
	}

	const std::string& GetGuiScriptLibraryPath()
	{
		// Embedded files do not exist on disk; resolve parent aliases before registering their identities.
		static const auto Path = PathUtil::ToUtf8(
		    std::filesystem::weakly_canonical(std::filesystem::absolute("__veroscript_gui__/framework.vs")));
		return Path;
	}

	bool IsGuiScriptLibraryPath(const std::string& Path)
	{
		const auto Parent = PathUtil::FromUtf8(GetGuiScriptLibraryPath()).parent_path();
		const auto Key = PathUtil::IdentityKey(Path);
		return std::any_of(Sources.begin(), Sources.end(),
		    [&](const auto& Source)
		{
			if (PathUtil::Equal(Path, PathUtil::ToUtf8(Parent / Source.Name))) return true;
			// Saved programs retain the compiler's virtual source paths when the
			// process later starts in a different working directory.
			const auto Suffix = PathUtil::IdentityKey(std::string("/__veroscript_gui__/") + Source.Name);
			return Key.size() >= Suffix.size() && Key.compare(Key.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
		});
	}

	bool ConfigureGuiLibrarySources(FEngineConfig& Config, std::string& Error)
	{
		auto Next = Config;
		auto Description = Next.ModuleEnvironment ? Next.ModuleEnvironment->GetDescription() : FModuleEnvironmentDesc{};
		if (Next.ModuleEnvironment)
		{
			const auto Scope = std::find_if(Description.Scopes.begin(), Description.Scopes.end(),
			    [](const auto& Item)
			{
				return Item.Id == "veroscript.gui.framework";
			});
			if (Scope == Description.Scopes.end())
			{
				Description.Scopes.push_back({"veroscript.gui.framework", {Description.EntryOwnerId}, true});
			}
		}
		const auto Parent = PathUtil::FromUtf8(GetGuiScriptLibraryPath()).parent_path();
		for (const auto& Source : Sources)
		{
			const auto Path = PathUtil::ToUtf8(Parent / Source.Name);
			const auto Existing = std::find_if(Next.SourceOverlays.begin(), Next.SourceOverlays.end(),
			    [&](const auto& Item)
			{
				return PathUtil::Equal(Item.Path, Path);
			});
			if (Existing == Next.SourceOverlays.end())
			{
				Next.SourceOverlays.push_back({Path, Source.Source});
			}
			else if (Existing->Source != Source.Source)
			{
				Error = "gui.framework_source_conflict: " + Path;
				return false;
			}
			if (Next.ModuleEnvironment)
			{
				const std::string Id =
				    std::string("veroscript.gui.framework") +
				    (Source.Name == std::string("framework.vs") ? "" : std::string("/") + Source.Name);
				const auto Found = std::find_if(Description.Modules.begin(), Description.Modules.end(),
				    [&](const auto& Module)
				{
					return Module.Id == Id;
				});
				if (Found == Description.Modules.end())
				{
					FResolvedModule Module;
					Module.Name = Source.Name;
					Module.Id = Id;
					Module.OwnerId = "veroscript.gui.framework";
					Module.Path = Path;
					Module.Source = Source.Source;
					Description.Modules.push_back(std::move(Module));
				}
				else if (!PathUtil::Equal(Found->Path, Path) || Found->Source != Source.Source ||
				    Found->OwnerId != "veroscript.gui.framework")
				{
					Error = "gui.framework_module_conflict: " + Id;
					return false;
				}
			}
		}
		if (Next.ModuleEnvironment)
		{
			Next.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
			if (!Next.ModuleEnvironment)
			{
				return false;
			}
		}
		const auto& Entry = GetGuiScriptLibraryPath();
		if (std::find(Next.ImplicitImportPaths.begin(), Next.ImplicitImportPaths.end(), Entry) ==
		    Next.ImplicitImportPaths.end())
		{
			Next.ImplicitImportPaths.push_back(Entry);
		}
		if (!Next.ModuleEnvironment && (!Next.CompileProcessing.ReadDependency || Next.CompileProcessing.ReadDependency.target<FDefaultDependencyReader>()))
		{
			Next.CompileProcessing.ReadDependency = FDefaultDependencyReader{Next.SourceOverlays};
		}
		Config = std::move(Next);
		Error.clear();
		return true;
	}
} // namespace VeroScript::Gui

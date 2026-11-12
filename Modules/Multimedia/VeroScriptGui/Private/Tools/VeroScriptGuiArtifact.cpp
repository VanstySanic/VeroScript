// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiArtifact.h"
#include "Bindings/VeroScriptGuiScriptLibrary.h"
#include "VeroScriptPath.h"
#include "VeroScriptResources.h"
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>

namespace VeroScript::Gui
{
	namespace
	{
		constexpr const char* ManifestKey = "veroscript.gui.resources";
		struct FModuleBase
		{
			std::string Path;
			bool bFramework = false;
		};
		struct FManifest
		{
			std::string Scope;
			std::map<std::string, FModuleBase> Modules;
			std::set<std::string> Resources;
			std::map<std::string, std::string> Assets;
		};

		void Write(std::vector<std::uint8_t>& Bytes, const std::string& Text)
		{
			if (Text.size() > UINT32_MAX)
			{
				throw std::runtime_error("gui.artifact.identity_limit");
			}
			const auto Length = static_cast<std::uint32_t>(Text.size());
			for (unsigned I = 0; I < 4; ++I)
			{
				Bytes.push_back(static_cast<std::uint8_t>(Length >> (I * 8)));
			}
			Bytes.insert(Bytes.end(), Text.begin(), Text.end());
		}

		bool Decode(const std::vector<std::uint8_t>& Bytes, FManifest& Manifest)
		{
			if (Bytes.size() > 16 * 1024 * 1024)
			{
				return false;
			}
			std::size_t Offset = 0;
			const auto Read = [&](std::string& Text)
			{
				if (Bytes.size() - Offset < 4)
				{
					return false;
				}
				std::uint32_t Length = 0;
				for (unsigned I = 0; I < 4; ++I)
				{
					Length |= std::uint32_t(Bytes[Offset++]) << (I * 8);
				}
				if (Length > Bytes.size() - Offset)
				{
					return false;
				}
				Text.assign(reinterpret_cast<const char*>(Bytes.data() + Offset), Length);
				Offset += Length;
				return true;
			};
			std::string Kind, Key, Value;
			if (!Read(Kind) || Kind != "veroscript.gui.resources:2" || !Read(Manifest.Scope) || Manifest.Scope.empty())
			{
				return false;
			}
			while (Offset < Bytes.size())
			{
				if (!Read(Kind) || !Read(Key) || Key.empty())
				{
					return false;
				}
				if (Kind == "resource")
				{
					if (!Manifest.Resources.insert(Key).second)
					{
						return false;
					}
				}
				else if (Kind == "asset")
				{
					if (!Read(Value) || Value.empty() || !Manifest.Assets.emplace(Key, Value).second)
					{
						return false;
					}
				}
				else if (Kind == "module" || Kind == "framework")
				{
					if (!Read(Value) || !Manifest.Modules.emplace(Key, FModuleBase{Value, Kind == "framework"}).second)
					{
						return false;
					}
				}
				else
				{
					return false;
				}
			}
			return true;
		}
	} // namespace

	bool BuildGuiArtifact(const FCompileResult& Compilation, FPackageBuildOptions Options,
						  std::vector<std::uint8_t>& Bytes, std::string& Error, const FGuiAssetOptions& Assets)
	try
	{
		auto Module = *Compilation.Module;
		const auto Root = std::filesystem::absolute(PathUtil::FromUtf8(Module.EntryModulePath)).parent_path();
		const auto Relative = [&](const std::string& Source)
		{
			const auto Path = PathUtil::FromUtf8(Source);
			if (Source.empty() || Source.front() == '<')
			{
				throw std::runtime_error("gui.artifact.requires_module_resource_bases");
			}
			const auto Result = std::filesystem::absolute(Path).lexically_normal().lexically_relative(Root);
			if (Result.empty() || Result.is_absolute())
			{
				throw std::runtime_error("gui.artifact.nonportable_resource_base");
			}
			return PathUtil::ToUtf8(Result);
		};
		FManifest Manifest;
		std::uint64_t AssetBytes = 0;
		for (const auto& Source : Assets.Resources)
		{
			const auto Key = Relative(Source);
			if (!Manifest.Resources.insert(Key).second)
			{
				Error = "gui.artifact.duplicate_asset: " + Key;
				return false;
			}
			std::vector<std::uint8_t> Data;
			if (!(Assets.ReadResource ? Assets.ReadResource(Source, Data, Error)
									  : ReadGuiResourceFile(Source, Data, Error)))
			{
				return false;
			}
			AssetBytes += Data.size();
			if (Data.empty() || Data.size() > 64 * 1024 * 1024 || AssetBytes > 128 * 1024 * 1024)
			{
				Error = "gui.artifact.asset_limit";
				return false;
			}
			const auto Token = std::to_string(Manifest.Assets.size());
			Manifest.Assets.emplace(Key, Token);
			Options.HostMetadata[std::string("veroscript.gui.asset.") + Token] = std::move(Data);
		}
		Manifest.Scope = Options.PackageName + '@' + std::to_string(Options.PackageVersion);
		for (auto& Text : Module.StringPool)
		{
			if (Text.rfind(GuiModuleMarker, 0) == 0)
			{
				const auto Resource = Relative(Text.substr(std::char_traits<char>::length(GuiModuleMarker)));
				Manifest.Resources.insert(Resource);
				Text = std::string(GuiBakedMarker) + Manifest.Scope + '/' + Resource;
			}
			else if (Text.rfind(GuiSourceMarker, 0) == 0)
			{
				const auto Source = Text.substr(std::char_traits<char>::length(GuiSourceMarker));
				if (IsResourceDependencyId(Source))
				{
					FResourceRequest Request;
					std::string Requester;
					if (!ParseResourceDependencyId(Source, Request, Requester))
					{
						throw std::runtime_error("gui.artifact.invalid_resource_identity");
					}
					Text = "resource:" + Request.ProviderId + '/' + Request.ModuleId + '/' + Request.Group + '/' +
						   Request.Path;
				}
				else
				{
					Text = Manifest.Scope + '/' + Relative(Source);
				}
			}
		}
		std::vector<std::uint8_t> Intermediate;
		if (!BuildPackage(Module, Options, Intermediate, Error))
		{
			return false;
		}
		FPackageData Portable;
		if (!ReadPackage({Intermediate.data(), Intermediate.size()}, Portable, Error))
		{
			return false;
		}
		if (Portable.Module->Functions.size() != Module.Functions.size() ||
			Portable.Module->GlobalMetadata.size() != Module.GlobalMetadata.size())
		{
			Error = "gui.artifact.module_mapping_mismatch";
			return false;
		}
		const auto FrameworkRoot = PathUtil::FromUtf8(GetGuiScriptLibraryPath()).parent_path();
		const auto AddModule = [&](const std::string& Original, const std::string& Packaged)
		{
			if (Original.empty() || Packaged.empty() || Manifest.Modules.count(Packaged))
			{
				return;
			}
			const auto Parent = PathUtil::FromUtf8(Original).parent_path();
			const bool Framework = Parent == FrameworkRoot || Parent == FrameworkRoot / "Gui" ||
								   Original == "veroscript.gui.framework" ||
								   Original.rfind("veroscript.gui.framework/", 0) == 0;
			if (Original.front() == '<' && !Framework)
			{
				return;
			}
			// Libraries on another volume need no GUI base unless they define a baked
			// GUI resource (validated above). Do not reject an unrelated stdlib SDK.
			const auto Base = Framework ? std::filesystem::path{}
										: std::filesystem::absolute(PathUtil::FromUtf8(Original))
											  .lexically_normal()
											  .lexically_relative(Root);
			if (!Framework && (Base.empty() || Base.is_absolute()))
			{
				return;
			}
			Manifest.Modules.emplace(Packaged, FModuleBase{PathUtil::ToUtf8(Base), Framework});
		};
		AddModule(Module.EntryModulePath, Portable.Module->EntryModulePath);
		for (std::size_t I = 0; I < Module.Functions.size(); ++I)
		{
			AddModule(Module.Functions[I].ModulePath, Portable.Module->Functions[I].ModulePath);
		}
		for (std::size_t I = 0; I < Module.GlobalMetadata.size(); ++I)
		{
			AddModule(Module.GlobalMetadata[I].ModulePath, Portable.Module->GlobalMetadata[I].ModulePath);
		}
		std::vector<std::uint8_t> Encoded;
		Write(Encoded, "veroscript.gui.resources:2");
		Write(Encoded, Manifest.Scope);
		for (const auto& Entry : Manifest.Modules)
		{
			Write(Encoded, Entry.second.bFramework ? "framework" : "module");
			Write(Encoded, Entry.first);
			Write(Encoded, Entry.second.Path);
		}
		for (const auto& Entry : Manifest.Resources)
		{
			Write(Encoded, "resource");
			Write(Encoded, Entry);
		}
		for (const auto& Entry : Manifest.Assets)
		{
			Write(Encoded, "asset");
			Write(Encoded, Entry.first);
			Write(Encoded, Entry.second);
		}
		Options.HostMetadata[ManifestKey] = std::move(Encoded);
		const std::string Identity = std::string(ContractId) + ":" + ContractVersion;
		Options.HostMetadata["veroscript.gui.contract"] = {Identity.begin(), Identity.end()};
		return BuildPackage(Module, Options, Bytes, Error);
	}
	catch (const std::exception& Exception)
	{
		Error = std::string("gui.artifact.failure: ") + Exception.what();
		return false;
	}

	bool ConfigureGuiArtifactServices(const FPackageData& Package, FScriptServices& Services, std::string& Error)
	{
		Error.clear();
		const auto Contract = Package.HostMetadata.find("veroscript.gui.contract");
		const std::string Expected = std::string(ContractId) + ":" + ContractVersion;
		if (Contract == Package.HostMetadata.end() ||
			std::string(Contract->second.begin(), Contract->second.end()) != Expected)
		{
			Error = "gui.artifact.incompatible_contract";
			return false;
		}
		const auto Found = Package.HostMetadata.find(ManifestKey);
		auto Manifest = std::make_shared<FManifest>();
		if (Found == Package.HostMetadata.end() || !Decode(Found->second, *Manifest))
		{
			Error = "gui.artifact.invalid_resource_manifest";
			return false;
		}
		auto Assets = std::make_shared<std::map<std::string, std::vector<std::uint8_t>>>();
		for (const auto& Asset : Manifest->Assets)
		{
			const auto Entry = Package.HostMetadata.find(std::string("veroscript.gui.asset.") + Asset.second);
			if (!Manifest->Resources.count(Asset.first) || Entry == Package.HostMetadata.end() ||
				Entry->second.empty() || Entry->second.size() > 64 * 1024 * 1024)
			{
				Error = "gui.artifact.invalid_asset";
				return false;
			}
			Assets->emplace(std::string(GuiBakedMarker) + Manifest->Scope + '/' + Asset.first, Entry->second);
		}
		auto PreviousReader = std::move(Services.ReadResource);
		Services.ReadResource = [Assets, PreviousReader](const auto& Identity, auto& Bytes, auto& Failure)
		{
			const auto Found = Assets->find(Identity);
			if (Found != Assets->end())
			{
				Bytes = Found->second;
				Failure.clear();
				return true;
			}
			if (PreviousReader)
			{
				return PreviousReader(Identity, Bytes, Failure);
			}
			Failure = "gui.artifact.asset_not_baked: " + Identity;
			return false;
		};
		auto PreviousFramework = std::move(Services.IsFrameworkModule);
		Services.IsFrameworkModule = [Manifest, PreviousFramework](const auto& Module)
		{
			const auto Found = Manifest->Modules.find(Module);
			return (Found != Manifest->Modules.end() && Found->second.bFramework) ||
				   (PreviousFramework && PreviousFramework(Module));
		};
		auto PreviousResolver = std::move(Services.ResolveResourceReference);
		Services.ResolveResourceReference =
			[Manifest, PreviousResolver](const auto& Requester, const auto& Reference, auto& Identity, auto& Failure)
		{
			Failure.clear();
			Identity.clear();
			if (IsResourceDependencyId(Reference) || Reference.rfind(GuiBakedMarker, 0) == 0 ||
				(Reference.find(':') != std::string::npos && Reference.find(':') != 1))
			{
				Identity = Reference;
				return true;
			}
			std::string Base;
			const auto Prefix = Manifest->Scope + '/';
			if (Requester.rfind(Prefix, 0) == 0)
			{
				Base = Requester.substr(Prefix.size());
			}
			const auto Found = Manifest->Modules.find(Requester);
			if (Found != Manifest->Modules.end() && !Found->second.bFramework)
			{
				Base = Found->second.Path;
			}
			if (!Base.empty())
			{
				const auto Path = PathUtil::FromUtf8(Reference);
				const auto Relative =
					PathUtil::ToUtf8((PathUtil::FromUtf8(Base).parent_path() / Path).lexically_normal());
				if (!Path.is_absolute() && Manifest->Resources.count(Relative))
				{
					Identity = std::string(GuiBakedMarker) + Manifest->Scope + '/' + Relative;
					return true;
				}
			}
			if (PreviousResolver)
			{
				return PreviousResolver(Requester, Reference, Identity, Failure);
			}
			Failure = "gui.artifact.resource_not_baked: " + Reference;
			return false;
		};
		return true;
	}

	bool RegisterGuiArtifactBindings(FHostBindingRegistry& Registry, const FPackageData& Package,
									 FScriptServices Services, std::string& Error)
	{
		return ConfigureGuiArtifactServices(Package, Services, Error) &&
			   RegisterScriptBindings(Registry, std::move(Services), Error);
	}
} // namespace VeroScript::Gui

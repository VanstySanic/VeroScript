// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAst.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptPackagePath.h"
#include "VeroScriptPath.h"
#include "VeroScriptScriptExtensionsInternal.h"
#include <deque>
#include <mutex>
#include <stdexcept>
#include <algorithm>

namespace VeroScript::ScriptExtensions
{
	namespace
	{
		struct FCacheEntry
		{
			std::string Key;
			FCompiledExtension Compiled;
			std::vector<FSourceOverlay> Sources;
			std::vector<FImportDecl> Imports;
			std::size_t Bytes = 0;
		};
		std::mutex CacheMutex;
		std::deque<std::shared_ptr<const FCacheEntry>> Cache;
		std::size_t CacheBytes = 0;
		constexpr std::size_t MaxCacheEntries = 8, MaxCacheBytes = 64 * 1024 * 1024;
		void Append(std::string& Key, std::string_view Value)
		{
			Key += std::to_string(Value.size()) + ":";
			Key.append(Value);
		}
	} // namespace
	FCompiledExtension CompileBootstrap(FTask& Task, const std::string& Path, const std::string& Source,
										const FCompileOptions& Options)
	{
		Task.Check();
		std::string Key, Catalog;
		if (!ExportBindingCatalogJson(Task.Bindings, Catalog))
		{
			throw std::runtime_error("cannot fingerprint compilation extension bindings");
		}

		if (Options.ModuleEnvironment)
		{
			Append(Key, Options.ModuleEnvironment->GetCompileFingerprint());
		}

		Append(Key, Path);
		Append(Key, Source);
		Append(Key, Catalog);
		Append(Key, GetScriptCompileServiceFingerprint());
		Append(Key, Task.Inputs.ScriptCompileExtensions.BindingConfiguration);
		Append(Key, std::to_string(Options.ImportPaths.size()));
		for (const auto Value : Options.ImportPaths)
		{
			Append(Key, Value);
		}

		Append(Key, "stdlib");
		Append(Key, std::to_string(Options.StdlibSearchPaths.size()));
		for (const auto Value : Options.StdlibSearchPaths)
		{
			Append(Key, Value);
		}

		Append(Key, "defines");
		Append(Key, std::to_string(Options.Defines.size()));
		for (const auto Value : Options.Defines)
		{
			Append(Key, Value);
		}

		Append(Key, std::to_string(Options.PredefinedMacros.size()));
		for (const auto& Macro : Options.PredefinedMacros)
		{
			Append(Key, Macro.Name);
			Append(Key, Macro.Body);
			Append(Key, Macro.bVariadic ? "1" : "0");
			Append(Key, Macro.SourceName);
			Append(Key, std::to_string(Macro.Parameters.size()));
			for (const auto& Param : Macro.Parameters)
			{
				Append(Key, Param);
			}
		}
		Append(Key, "overlays");
		Append(Key, std::to_string(Options.SourceOverlays.size()));
		for (const auto& Overlay : Options.SourceOverlays)
		{
			Append(Key, Overlay.Path);
			Append(Key, Overlay.Source);
		}
		Append(Key, Options.bAllowCircularImports ? "1" : "0");
		Append(Key, Options.bAllowProjectWideAccess ? "1" : "0");
		Append(Key, std::to_string(Options.CompileProcessing.MaxInputBytes));
		const bool CanCache = !Task.Inputs.ScriptCompileExtensions.Bindings ||
			(Task.Inputs.ScriptCompileExtensions.bBindingsDeterministic &&
				Task.Inputs.ScriptCompileExtensions.Bindings->GetGenericTypeTemplates().empty() &&
				std::none_of(Task.Inputs.ScriptCompileExtensions.Bindings->GetFunctions().begin(),
					Task.Inputs.ScriptCompileExtensions.Bindings->GetFunctions().end(),
					[](const auto& F)
					{
						return !F.TypeParams.empty();
					}));
		std::vector<std::shared_ptr<const FCacheEntry>> Candidates;
		if (CanCache)
		{
			std::lock_guard<std::mutex> Lock(CacheMutex);
			for (const auto& Entry : Cache)
			{
				if (Entry->Key == Key)
				{
					Candidates.push_back(Entry);
				}
			}
		}
		for (const auto& Entry : Candidates)
		{
			bool Matches = true;
			for (const auto& Input : Entry->Sources)
			{
				Task.Check();
				std::string Content;
				if (!Task.ReadSource(Input.Path, Content) || Content != Input.Source)
				{
					Matches = false;
					break;
				}
			}
			// Re-resolve imports as well as checking their contents: a newly created
			// nearer file may shadow an unchanged, previously resolved dependency.
			if (Matches)
			{
				for (const auto& Import : Entry->Imports)
				{
					if (Import.ResolvedPath.empty() || Import.ResolvedPath.front() == '<')
					{
						continue;
					}

					if (PathUtil::IdentityKey(Task.Resolve(Import.Path, Import.ImporterModulePath, false)) !=
						PathUtil::IdentityKey(Import.ResolvedPath))
					{
						Matches = false;
						break;
					}
				}
			}

			if (Matches)
			{
				for (const auto& Input : Entry->Sources)
				{
					Task.RecordContent(Input.Path, Input.Source);
				}

				return Entry->Compiled;
			}
		}
		auto Result = VeroScriptCompile::CompileSource(Source, Path, Options);
		if (!Result.bSuccess)
		{
			throw std::runtime_error("compiler extension " + Path + ":" +
				std::to_string(Result.Error.Location.Line) + ":" +
				std::to_string(Result.Error.Location.Column) + ": " + Result.Error.Message);
		}

		FCompiledExtension Compiled{std::move(Result.Module), std::move(Result.PreparedProgram)};
		if (CanCache)
		{
			auto Entry = std::make_shared<FCacheEntry>();
			Entry->Key = std::move(Key);
			Entry->Compiled = Compiled;
			Entry->Imports = Compiled.Prepared->GetProgram().Imports;
			Entry->Bytes = Entry->Key.size();
			const auto& Metadata = Compiled.Prepared->GetSourceMetadata();
			for (const auto& Document : Metadata.Documents)
			{
				if (Document.Id != Document.ModuleId + ":raw" || Document.ModuleId == Path)
				{
					continue;
				}

				const auto Actual = Task.Contents.find(PathUtil::IdentityKey(Document.ModuleId));
				if (Actual == Task.Contents.end())
				{
					continue;
				}

				const auto& Content = Actual->second;
				Entry->Sources.push_back({Document.ModuleId, Content});
				Entry->Bytes += Content.size();
			}
			// Binary helper modules do not have a raw source document.
			for (const auto& Import : Entry->Imports)
			{
				if (Detail::IsLibraryPackagePath(Import.ResolvedPath))
				{
					std::string Bytes;
					if (Task.ReadSource(Import.ResolvedPath, Bytes))
					{
						Entry->Sources.push_back({Import.ResolvedPath, Bytes});
						Entry->Bytes += Bytes.size();
					}
				}
			}

			if (Entry->Bytes <= MaxCacheBytes)
			{
				std::lock_guard<std::mutex> Lock(CacheMutex);
				while (!Cache.empty() && (Cache.size() >= MaxCacheEntries || CacheBytes > MaxCacheBytes - Entry->Bytes))
				{
					CacheBytes -= Cache.front()->Bytes;
					Cache.pop_front();
				}
				CacheBytes += Entry->Bytes;
				Cache.push_back(std::move(Entry));
			}
		}
		return Compiled;
	}
} // namespace VeroScript::ScriptExtensions

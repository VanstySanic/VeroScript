// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptModuleLoader.h"
#include "VeroScriptAnalysisControl.h"
#include "Processing/VeroScriptProcessingSession.h"
#include "Processing/VeroScriptScriptExtensionsInternal.h"
#include "Processing/VeroScriptSourceMetadataIO.h"
#include "VeroScriptImportSyntax.h"
#include "VeroScriptPlatform.h"
#include "VeroScriptModuleEnvironment.h"
#include "ModuleSupport/VeroScriptModuleSupport.h"

#include "Parser/VeroScriptParserDetail.h"

#include "VeroScriptAst.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptLexer.h"
#include "VeroScriptLocale.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPackagePath.h"
#include "VeroScriptParser.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptTypes.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VeroScript
{
	class FEnvironmentLoadError final : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};
	namespace Detail
	{
		// Bounds graph discovery recursion and the logical active import chain.
		// Breadth (including large implicit cohorts) does not consume this budget.
		constexpr std::size_t MaxImportNesting = 128;

		std::string NormalizePath(std::string Path)
		{
			return PathUtil::Normalize(std::move(Path));
		}

		std::vector<std::string> SplitPathList(const char* PathList)
		{
			std::vector<std::string> Paths;
			if (PathList == nullptr)
			{
				return Paths;
			}

			#ifdef _WIN32
			constexpr char PathSeparator = ';';
			#else
			constexpr char PathSeparator = ':';
			#endif

			std::string Current;
			for (const char* Cursor = PathList; *Cursor != '\0'; ++Cursor)
			{
				if (*Cursor == PathSeparator)
				{
					if (!Current.empty())
					{
						Paths.push_back(NormalizePath(Current));
						Current.clear();
					}
					continue;
				}
				Current.push_back(*Cursor);
			}
			if (!Current.empty())
			{
				Paths.push_back(NormalizePath(Current));
			}
			return Paths;
		}

		bool TryResolveAtPath(const std::string& Candidate, std::string& OutResolvedPath)
		{
			const auto NativePath = PathUtil::FromUtf8(Candidate);
			std::error_code Error;
			if (!std::filesystem::is_regular_file(NativePath, Error)) return false;
			std::ifstream TestStream(NativePath, std::ios::binary);
			if (!TestStream)
			{
				return false;
			}
			OutResolvedPath = Candidate;
			return true;
		}

		std::string DecodeImportPath(const FToken& Token)
		{
			std::string Path;
			std::string Error;
			return ParserDetail::DecodeModuleImportPath(Token.Lexeme, Path, Error) ? Path
			                                                                       : std::string{};
		}

		std::string GetEnvironmentVariable(const char* Name)
		{
			const auto Value = Environment::GetUtf8(Name);
			return Value ? *Value : std::string{};
		}

		std::uint64_t PackageTemplateFingerprint(const FBytecodePackageTemplateModule& Template)
		{
			std::uint64_t TemplateFingerprint = 14695981039346656037ull;
			TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, Template.Source);
			TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, Template.FacadePath);
			TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, Template.OwnerComponentId);
			TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, Template.SourceOwnerId);
			for (const auto& Op : Template.GeneratedOperations)
			{
				TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, Op.TargetId);
				TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, Op.Source);
				TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, Op.GenerationId);
				TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, Op.ProcessorId);
				const auto Kind = static_cast<std::uint8_t>(Op.Kind), Access = static_cast<std::uint8_t>(Op.Access);
				TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, &Kind, 1);
				TemplateFingerprint = Runtime::Fnv1a64(TemplateFingerprint, &Access, 1);
			}
			std::vector<std::uint8_t> ProcessingBytes;
			std::string ProcessingError;
			if (!EncodeCompileOperations(
			        Template.CompileGeneratedOperations, ProcessingBytes, ProcessingError))
			{
				throw std::runtime_error(ProcessingError);
			}

			TemplateFingerprint =
			    Runtime::Fnv1a64(TemplateFingerprint, ProcessingBytes.data(), ProcessingBytes.size());
			const FSourceMetadataSnapshot Empty;
			if (!EncodeSourceMetadata(
			        Template.InitialSourceMetadata ? *Template.InitialSourceMetadata : Empty, ProcessingBytes,
			        ProcessingError))
			{
				throw std::runtime_error(ProcessingError);
			}

			TemplateFingerprint =
			    Runtime::Fnv1a64(TemplateFingerprint, ProcessingBytes.data(), ProcessingBytes.size());
			const std::uint8_t TemplateFlags = (Template.bDeclarationOnly ? 0x01u : 0u) |
			    (Template.bFromPackage ? 0x02u : 0u) |
			    (Template.bPackageSurface ? 0x04u : 0u);
			TemplateFingerprint =
			    Runtime::Fnv1a64(TemplateFingerprint, &TemplateFlags, sizeof(TemplateFlags));
			for (const FBytecodePackageTemplateImport& Import : Template.Imports)
			{
				TemplateFingerprint =
				    Runtime::Fnv1a64(TemplateFingerprint, Import.ResolvedModulePath);
				const std::uint8_t ReExport = Import.bReExport ? 1u : 0u;
				TemplateFingerprint =
				    Runtime::Fnv1a64(TemplateFingerprint, &ReExport, sizeof(ReExport));
			}
			return TemplateFingerprint;
		}

		std::vector<FImportDecl> ExtractImportDecls(const std::vector<FToken>& Tokens)
		{
			std::vector<FImportDecl> Imports;
			FModuleImportHeadState State;
			FSourceLocation Location;
			for (const auto& Token : Tokens)
			{
				const bool ReExport = State.IsReExport();
				if (Token.Lexeme == "import" && !ReExport)
				{
					Location = Token.Location;
				}

				if (Token.Lexeme == "export" && !ReExport)
				{
					Location = Token.Location;
				}

				if (!State.Consume(Token.Lexeme, Token.Kind == ETokenKind::StringLiteral))
				{
					continue;
				}

				FImportDecl Import;
				Import.Path = DecodeImportPath(Token);
				Import.Location = Location;
				Import.bReExport = ReExport;
				Imports.push_back(std::move(Import));
			}
			return Imports;
		}
	} // namespace Detail

	struct VeroScriptModuleLoader::FPreparedModule
	{
		std::string ResolvedPath;
		std::string Source;
		std::string Preprocessed;
		std::vector<FToken> Tokens;
		std::vector<FImportDecl> Imports;
		std::unique_ptr<FPreprocessorSession> Preprocessor;
		std::vector<FPublicMacro> PublicMacros;
		FPreprocessLineMap LineMap;
		bool bReady = false;
	};

	VeroScriptModuleLoader::VeroScriptModuleLoader(const std::string_view InMainFilePath,
	    std::shared_ptr<const FModuleEnvironment> Environment)
	    : MainFilePath(Detail::NormalizePath(std::string(InMainFilePath))), ModuleEnvironment(std::move(Environment)), MainDirectory(GetDirectory(MainFilePath))
	{
		ResetLoadState();
		InitSearchPaths();
		if (ModuleEnvironment)
		{
			FCompileOptions O;
			std::string Message;
			if (!ModuleEnvironment->Apply(O, Message))
			{
				throw FEnvironmentLoadError(Message);
			}

			SetCompileProcessing(std::move(O.CompileProcessing));
			SetScriptCompileExtensions(std::move(O.ScriptCompileExtensions));
			for (const auto P : O.ImportPaths)
			{
				AddImportPath(std::string(P));
			}

			for (const auto P : O.StdlibSearchPaths)
			{
				AddStdlibSearchRoot(std::string(P));
			}

			for (const auto P : O.ImplicitImportPaths)
			{
				AddImplicitImportPath(std::string(P));
			}

			for (const auto& V : O.SourceOverlays)
			{
				SetSourceOverlay(V.Path, V.Source);
			}
		}
	}

	VeroScriptModuleLoader::~VeroScriptModuleLoader() = default;

	void VeroScriptModuleLoader::AddImportPath(std::string Path)
	{
		Path = Detail::NormalizePath(std::move(Path));
		if (std::find(CliImportPaths.begin(), CliImportPaths.end(), Path) == CliImportPaths.end())
		{
			CliImportPaths.push_back(std::move(Path));
		}
	}

	void VeroScriptModuleLoader::AddImplicitImportPath(std::string Path)
	{
		Path = Detail::NormalizePath(std::move(Path));
		if (!Path.empty() && std::find(ImplicitImportPaths.begin(), ImplicitImportPaths.end(), Path) == ImplicitImportPaths.end())
		{
			ImplicitImportPaths.push_back(std::move(Path));
		}
	}

	void VeroScriptModuleLoader::AddStdlibSearchRoot(std::string Path)
	{
		const std::string Normalized = Detail::NormalizePath(std::move(Path));
		if (Normalized.empty())
		{
			return;
		}
		for (const std::string& Existing : StdlibSearchRoots)
		{
			if (Existing == Normalized)
			{
				return;
			}
		}
		StdlibSearchRoots.insert(StdlibSearchRoots.begin(), Normalized);
		MergeSiblingLocalization(Normalized);
	}

	void VeroScriptModuleLoader::SetDefines(std::vector<std::string> Defines)
	{
		std::vector<std::string_view> Specs;
		Specs.reserve(Defines.size());
		for (const std::string& Define : Defines)
		{
			if (!Define.empty())
			{
				Specs.emplace_back(Define);
			}
		}
		PreprocessorDefines = NormalizeDefineList(Specs);
	}

	void VeroScriptModuleLoader::SetPredefinedMacros(std::vector<FPredefinedMacro> Macros)
	{
		PredefinedMacros = NormalizePredefinedMacros(Macros);
	}

	void VeroScriptModuleLoader::SetSourceOverlay(std::string Path, std::string Source)
	{
		if (ModuleEnvironment)
		{
			if (const auto* Existing = FindOverlayKey(Path))
			{
				SourceOverlays.erase(*Existing);
			}
		}

		SourceOverlays[Detail::NormalizePath(std::move(Path))] = std::move(Source);
	}

	void VeroScriptModuleLoader::ClearSourceOverlays()
	{
		SourceOverlays.clear();
	}

	const FPreprocessLineMap* VeroScriptModuleLoader::GetPreprocessLineMap(
	    std::string_view Path) const
	{
		const auto It = PreparedModules.find(PathUtil::IdentityKey(std::string(Path)));
		if (It == PreparedModules.end()) return nullptr;
		if (It->second->Preprocessor)
		{
			// Failed dependencies leave the emitted prefix available to tooling.
			It->second->LineMap = It->second->Preprocessor->LineMap();
			It->second->LineMap.PreprocessedSource = It->second->Preprocessor->Source();
		}
		return &It->second->LineMap;
	}

	void VeroScriptModuleLoader::SetHostTypeNames(std::unordered_set<std::string> TypeNames)
	{
		HostTypeNames = std::move(TypeNames);
	}

	void VeroScriptModuleLoader::ClearHostTypeNames()
	{
		HostTypeNames.clear();
	}

	bool VeroScriptModuleLoader::ResolveImportPath(
	    const std::string& ImportPath,
	    const std::string& FromDirectory,
	    std::string& OutResolvedPath, const std::string_view ImporterModulePath) const
	{
		FAnalysisStage Stage("imports");
		CheckAnalysisCancellation();
		const std::string FromOrigin = ImporterModulePath.empty() ? FromDirectory : std::string(ImporterModulePath);
		const bool FromIsDirectory = ImporterModulePath.empty();
		const auto RequestedPath = PathUtil::FromUtf8(ImportPath);
		const bool Explicit = RequestedPath.is_absolute() || ImportPath.rfind("./", 0) == 0 ||
		    ImportPath.rfind("../", 0) == 0 || ImportPath.rfind(".\\", 0) == 0 || ImportPath.rfind("..\\", 0) == 0;
		auto TryCandidate = [this, &OutResolvedPath, &FromOrigin, FromIsDirectory](const std::string& Candidate) -> bool
		{
			const std::string NormalizedCandidate = Detail::NormalizePath(Candidate);
			std::string Denial;
			if (ModuleEnvironment && !ModuleEnvironment->AuthorizeSource(NormalizedCandidate, FromOrigin, Denial, FromIsDirectory)) return false;
			if (ModuleEnvironment)
			{
				if (const auto* M = ModuleEnvironment->FindSource(NormalizedCandidate);
				    M && (M->Binary || (ModuleEnvironment->GetDescription().ArtifactReader && M->Path.rfind("veroscript-module:/", 0) == 0)))
				{
					OutResolvedPath = M->Path;
					return true;
				}
			}
			// Entry source supplied to this traversal is authoritative, including
			// back-references through mapped overlays and aliases of an unsaved path.
			if (MergedProgram && (!PreparingGraphIdentities.empty() || !LoadingIdentities.empty()))
			{
				const auto& Entry = MergedProgram->EntryModulePath;
				const auto Prepared = PreparedModules.find(PathUtil::IdentityKey(Entry));
				if (Prepared != PreparedModules.end())
				{
					bool SameEntry = PathUtil::Equal(NormalizedCandidate, Entry);
					if (!SameEntry && ModuleEnvironment)
					{
						const auto PhysicalCandidate = ModuleSupport::Physical(NormalizedCandidate);
						const auto PhysicalEntry = ModuleSupport::Physical(Entry);
						SameEntry = !PhysicalCandidate.empty() && !PhysicalEntry.empty() && PathUtil::Equal(PhysicalCandidate, PhysicalEntry);
					}
					if (SameEntry)
					{
						OutResolvedPath = Prepared->second->ResolvedPath;
						return true;
					}
				}
			}
			if (const std::string* OverlayKey = FindOverlayKey(NormalizedCandidate))
			{
				// Prefer the open-buffer key so FileLabels match Workspace document paths.
				OutResolvedPath = Detail::NormalizePath(*OverlayKey);
				return true;
			}
			if (Detail::TryResolveAtPath(NormalizedCandidate, OutResolvedPath))
			{
				OutResolvedPath = Detail::NormalizePath(OutResolvedPath);
				return true;
			}
			return false;
		};

		if (ModuleEnvironment && !Explicit)
		{
			if (const auto* M = ModuleEnvironment->FindModule(ImportPath))
			{
				std::string Denial;
				if (!ModuleEnvironment->AuthorizeSource(M->Path, FromOrigin, Denial, FromIsDirectory))
				{
					throw FEnvironmentLoadError(Denial);
				}

				const auto Local = Detail::NormalizePath(FromDirectory + "/" + ImportPath);
				// Mapped overlays may be unsaved, so compare the environment's canonical
				// module identity even when the local spelling uses a path alias.
				const auto* LocalModule = ModuleEnvironment->FindSource(Local);
				if ((!LocalModule || LocalModule->Id != M->Id) && ModuleEnvironment->AuthorizeSource(Local, FromOrigin, Denial, FromIsDirectory))
				{
					std::error_code Ec;
					if (FindOverlaySource(Local) ||
					    std::filesystem::is_regular_file(PathUtil::FromUtf8(Local), Ec))
					{
						throw FEnvironmentLoadError("ambiguous mapped/local import: " + ImportPath +
						    "; use an explicit relative import");
					}
				}
				if (TryCandidate(M->Path)) return true;
				throw FEnvironmentLoadError("mapped module is unavailable: " + ImportPath);
			}
		}
		if (ModuleEnvironment && Explicit)
		{
			std::string Denial;
			const auto Candidate =
			    RequestedPath.is_absolute() ? ImportPath : FromDirectory + "/" + ImportPath;
			if (!ModuleEnvironment->AuthorizeSource(Candidate, FromOrigin, Denial, FromIsDirectory))
			{
				throw FEnvironmentLoadError(Denial);
			}

			// An explicit path selects one source. A missing file must not become
			// a different package merely because a search root has the same suffix.
			return TryCandidate(Candidate);
		}
		// Absolute / root-relative import paths: try as-is before joining FromDirectory.
		{
			const std::filesystem::path ImportAsPath = PathUtil::FromUtf8(ImportPath);
			if (ImportAsPath.is_absolute())
			{
				if (TryCandidate(ImportPath))
				{
					return true;
				}
			}
		}

		const std::string LocalCandidate = FromDirectory + "/" + ImportPath;
		if (TryCandidate(LocalCandidate))
		{
			return true;
		}

		for (const std::string& SearchPath : CliImportPaths)
		{
			if (TryCandidate(SearchPath + "/" + ImportPath))
			{
				return true;
			}
		}

		for (const std::string& SearchPath : EnvImportPaths)
		{
			if (TryCandidate(SearchPath + "/" + ImportPath))
			{
				return true;
			}
		}

		for (const std::string& StdlibRoot : StdlibSearchRoots)
		{
			if (TryCandidate(StdlibRoot + "/" + ImportPath))
			{
				return true;
			}
		}

		return false;
	}

	std::unique_ptr<FProgram> VeroScriptModuleLoader::LoadMainModule()
	try
	{
		CheckAnalysisCancellation();
		ResetLoadState();
		FoldPreprocessorDefinesIntoFingerprint();
		const std::string& StableMainPath = InternPath(MainFilePath);
		std::string Denial;
		if (ModuleEnvironment && !ModuleEnvironment->AuthorizeSource(StableMainPath, StableMainPath, Denial))
		{
			throw FEnvironmentLoadError(Denial);
		}

		MergedProgram->EntryModulePath = StableMainPath;
		std::string EntrySource;
		const bool MainIsPackage = Detail::IsLibraryPackagePath(StableMainPath) && !FindOverlaySource(StableMainPath);
		if (!MainIsPackage)
		{
			if (const auto Overlay = FindOverlaySource(StableMainPath))
			{
				EntrySource = *Overlay;
			}

			else
			{
				RecordLoadedFileStamp(StableMainPath);
				const auto NativePath = PathUtil::FromUtf8(StableMainPath);
				std::error_code FileError;
				std::ifstream Input;
				if (std::filesystem::is_regular_file(NativePath, FileError))
				{
					Input.open(NativePath, std::ios::binary);
				}
				if (!Input.is_open())
				{
					bHasError = true;
					FillDiagnostic(
					    Error, "diag.cannot_open_script", {StableMainPath, 1, 1}, {StableMainPath});
					Diagnostics = {Error};
					return nullptr;
				}
				std::ostringstream Buffer;
				Buffer << Input.rdbuf();
				EntrySource = Buffer.str();
			}
		}
		if (!PrepareScriptExtensions(EntrySource, StableMainPath) || (!MainIsPackage && PrepareModuleSource(StableMainPath, EntrySource) == nullptr)) return nullptr;
		if (!PrepareImplicitModules())
		{
			return nullptr;
		}
		for (const std::string& ImplicitPath : ResolvedImplicitModulePaths)
		{
			if (!PrepareReachableModuleGraph(
			        ImplicitPath,
			        ImplicitPath,
			        false))
			{
				return nullptr;
			}
		}
		bImplicitMacroInterfacesReady = true;
		ProvenEmptyMacroModules.clear();
		if (!PrepareReachableModuleGraph(StableMainPath, StableMainPath, true))
		{
			return nullptr;
		}
		for (const auto& [Identity, Module] : PreparedModules)
		{
			(void)Identity;
			CollectImplicitForwardTypes(*Module);
		}
		bModuleGraphPrepared = true;
		const std::string MainIdentity = PathUtil::IdentityKey(StableMainPath);
		for (const std::string& ImplicitPath : ResolvedImplicitModulePaths)
		{
			if (PathUtil::IdentityKey(ImplicitPath) == MainIdentity)
			{
				continue;
			}
			if (!LoadModule(ImplicitPath, ImplicitPath, false))
			{
				return nullptr;
			}
		}
		if (!LoadModule(StableMainPath, StableMainPath, true))
		{
			return nullptr;
		}
		return std::move(MergedProgram);
	}
	catch (const FEnvironmentLoadError& Failure)
	{
		bHasError = true;
		Error = {};
		Error.Message = Failure.what();
		Diagnostics = {Error};
		return nullptr;
	}

	std::unique_ptr<FProgram> VeroScriptModuleLoader::LoadSourceModule(
	    const std::string_view Source,
	    const std::string_view FileLabel)
	try
	{
		FAnalysisStage Stage("load");
		CheckAnalysisCancellation();
		// Inputs may refer to paths or diagnostics owned by the previous load.
		// Snapshot both views before resetting those owners.
		const std::string EntrySource(Source);
		std::string EntryLabel = Detail::NormalizePath(std::string(FileLabel));
		ResetLoadState();
		FoldPreprocessorDefinesIntoFingerprint();
		const std::string& StableLabel = InternPath(std::move(EntryLabel));
		std::string Denial;
		if (ModuleEnvironment && !ModuleEnvironment->AuthorizeSource(StableLabel, StableLabel, Denial))
		{
			throw FEnvironmentLoadError(Denial);
		}

		MergedProgram->EntryModulePath = StableLabel;
		if (!PrepareScriptExtensions(EntrySource, StableLabel)) return nullptr;
		// An implicit module can reach the entry before graph discovery visits it.
		// Fix its contents first so an overlay or disk read cannot shadow Source.
		if (PrepareModuleSource(StableLabel, EntrySource) == nullptr)
		{
			return nullptr;
		}
		if (!PrepareImplicitModules(&EntrySource, &StableLabel))
		{
			return nullptr;
		}
		for (const std::string& ImplicitPath : ResolvedImplicitModulePaths)
		{
			if (!PrepareReachableModuleGraph(
			        ImplicitPath,
			        ImplicitPath,
			        false))
			{
				return nullptr;
			}
		}
		bImplicitMacroInterfacesReady = true;
		ProvenEmptyMacroModules.clear();
		if (!PrepareReachableModuleGraph(
		        StableLabel,
		        StableLabel,
		        true,
		        &EntrySource))
		{
			return nullptr;
		}
		for (const auto& [Identity, Module] : PreparedModules)
		{
			(void)Identity;
			CollectImplicitForwardTypes(*Module);
		}
		bModuleGraphPrepared = true;
		const std::string MainIdentity = PathUtil::IdentityKey(StableLabel);
		for (const std::string& ImplicitPath : ResolvedImplicitModulePaths)
		{
			if (PathUtil::IdentityKey(ImplicitPath) == MainIdentity)
			{
				continue;
			}
			if (!LoadModule(ImplicitPath, ImplicitPath, false))
			{
				return nullptr;
			}
		}
		if (!LoadModuleFromSource(StableLabel, StableLabel, EntrySource))
		{
			return nullptr;
		}
		return std::move(MergedProgram);
	}
	catch (const FEnvironmentLoadError& Failure)
	{
		bHasError = true;
		Error = {};
		Error.Message = Failure.what();
		Diagnostics = {Error};
		return nullptr;
	}

	std::vector<std::string> VeroScriptModuleLoader::ResolveLoadedModulePaths(
	    const std::string_view MainFilePath,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImports)
	{
		VeroScriptModuleLoader Loader(MainFilePath);
		for (const std::string_view ImportPath : ImportPaths)
		{
			Loader.AddImportPath(std::string(ImportPath));
		}
		for (const std::string_view ImplicitImport : ImplicitImports)
		{
			Loader.AddImplicitImportPath(std::string(ImplicitImport));
		}
		if (!Defines.empty())
		{
			std::vector<std::string> Specs;
			Specs.reserve(Defines.size());
			for (const std::string_view Define : Defines)
			{
				Specs.emplace_back(Define);
			}
			Loader.SetDefines(std::move(Specs));
		}
		Loader.SetPredefinedMacros(PredefinedMacros);
		Loader.LoadMainModule();
		return std::vector<std::string>(Loader.LoadedPaths.begin(), Loader.LoadedPaths.end());
	}

	std::uint64_t VeroScriptModuleLoader::GetModuleResolutionFingerprint() const
	{
		std::uint64_t Fingerprint = 14695981039346656037ull;
		auto HashPathList = [&Fingerprint](const std::vector<std::string>& Paths)
		{
			std::vector<std::string> SortedPaths = Paths;
			std::sort(SortedPaths.begin(), SortedPaths.end());
			for (const std::string& Path : SortedPaths)
			{
				Fingerprint = Runtime::Fnv1a64(Fingerprint, Path);
			}
		};
		HashPathList(CliImportPaths);
		HashPathList(ImplicitImportPaths);
		HashPathList(EnvImportPaths);
		HashPathList(StdlibSearchRoots);
		Fingerprint = Runtime::Fnv1a64(Fingerprint, "implicit-import:public-macros-v1");
		Fingerprint = Runtime::Fnv1a64(
		    Fingerprint,
		    bAllowCircularImports
		        ? std::string_view("circular-imports:on")
		        : std::string_view("circular-imports:off"));
		Fingerprint = Runtime::Fnv1a64(
		    Fingerprint,
		    bAllowProjectWideAccess
		        ? std::string_view("project-wide-access:on")
		        : std::string_view("project-wide-access:off"));
		return Fingerprint;
	}

	void VeroScriptModuleLoader::ResetLoadState()
	{
		PreprocessBudget = std::make_shared<FPreprocessBudget>();
		ProcessingOptions = ConfiguredProcessingOptions;
		// Programs anchor their own prepared sources and interned paths. Replacing
		// these owners cannot invalidate a program returned by an earlier load.
		MergedProgram = std::make_unique<FProgram>();
		MergedProgram->ModuleEnvironment = ModuleEnvironment;
		if (ModuleEnvironment)
		{
			MergedProgram->LifetimeAnchors.push_back(ModuleEnvironment);
			MergedProgram->RequiredExternalDependencies = ModuleEnvironment->GetDescription().RequiredDependencies;
		}
		InternedPaths = std::make_shared<std::unordered_set<std::string>>();
		MergedProgram->LifetimeAnchors.push_back(InternedPaths);
		ResolvedImplicitModulePaths.clear();
		LoadedPaths.clear();
		LoadedFileStamps.clear();
		LoadedIdentities.clear();
		LoadingIdentities.clear();
		IdentityToDisplayPath.clear();
		LoadedPackageFacades.clear();
		LoadedPackageTemplateFingerprints.clear();
		PreparedModules.clear();
		PreparedPackages.clear();
		ProvenEmptyMacroModules.clear();
		ImplicitModuleIdentities.clear();
		PreparingGraphIdentities.clear();
		PreparedGraphIdentities.clear();
		ImplicitForwardTypeNames.clear();
		ImplicitForwardInterfaceNames.clear();
		bDefinesFoldedIntoFingerprint = false;
		bModuleGraphPrepared = false;
		bImplicitMacroInterfacesReady = false;
		bHasError = false;
		Error = {};
		Diagnostics.clear();
		ProcessingSession = std::make_shared<FCompileProcessingSession>(ProcessingOptions);
		MergedProgram->CompileProcessingSession = ProcessingSession;
		MergedProgram->SourceMetadata = ProcessingSession->Metadata;
		SourceFingerprint = 14695981039346656037ull;
		if (ModuleEnvironment)
		{
			SourceFingerprint =
			    Runtime::Fnv1a64(SourceFingerprint, ModuleEnvironment->GetCompileFingerprint());
		}

		// Source text alone cannot distinguish bytecode cached before internal
		// builtin access became standard-library-only. Keep this policy marker in
		// the ordinary source fingerprint; it is not a bytecode format epoch.
		SourceFingerprint = Runtime::Fnv1a64(
		    SourceFingerprint,
		    std::string_view("builtin-visibility:stdlib-internal-v3"));
		// Recompile cached source so reflected writes include assignment coercions.
		SourceFingerprint = Runtime::Fnv1a64(
		    SourceFingerprint,
		    std::string_view("reflection:field-assignment-coercion-v1"));
		SourceFingerprint = Runtime::Fnv1a64(
		    SourceFingerprint, std::string_view("implicit-import:public-macros-v1"));
	}

	bool VeroScriptModuleLoader::PrepareScriptExtensions(std::string_view Source, std::string_view Label)
	{
		if (ProcessingOptions.PrepareSource) return true;
		if (ScriptExtensionOptions.Roots.empty() && !ScriptExtensions::HasCompilerImport(Source))
		{
			ProcessingSession->Options.PrepareSource = [](std::string_view, std::string& S)
			{
				if (ScriptExtensions::HasCompilerImport(S))
				{
					throw std::runtime_error(
					    "compiler extension must be loaded from the entry or project configuration");
				}
				return std::vector<FSourceRange>{};
			};
			return true;
		}
		FCompileOptions Options;
		Options.ModuleEnvironment = ModuleEnvironment;
		Options.CompileProcessing = ProcessingOptions;
		Options.ScriptCompileExtensions = ScriptExtensionOptions;
		for (const auto& P : CliImportPaths)
		{
			Options.ImportPaths.push_back(P);
		}

		for (const auto& P : StdlibSearchRoots)
		{
			Options.StdlibSearchPaths.push_back(P);
		}

		for (const auto& D : PreprocessorDefines)
		{
			Options.Defines.push_back(D);
		}

		Options.PredefinedMacros = PredefinedMacros;
		for (const auto& O : SourceOverlays)
		{
			Options.SourceOverlays.push_back({O.first, O.second});
		}

		auto Task = LoadScriptCompileExtensions(Source, Label, Options, Diagnostics);
		if (!Diagnostics.empty())
		{
			bHasError = true;
			Error = Diagnostics.front();
			return false;
		}
		if (Task)
		{
			ProcessingOptions = Task->GetProcessingOptions();
			MergedProgram->ScriptCompileExtensionTask = std::move(Task);
			for (const auto& D : ProcessingOptions.InitialDependencies)
			{
				if (D.bExists && !IsResourceDependencyId(D.Path))
				{
					LoadedPaths.insert(D.Path);
					if (!FindOverlaySource(D.Path))
					{
						RecordLoadedFileStamp(D.Path);
					}
				}
			}
		}
		else
		{
			ProcessingOptions.PrepareSource = [](std::string_view, std::string& S)
			{
				if (ScriptExtensions::HasCompilerImport(S))
				{
					throw std::runtime_error(
					    "compiler extension must be loaded from the entry or project configuration");
				}
				return std::vector<FSourceRange>{};
			};
		}

		ProcessingSession = std::make_shared<FCompileProcessingSession>(ProcessingOptions);
		MergedProgram->CompileProcessingSession = ProcessingSession;
		MergedProgram->SourceMetadata = ProcessingSession->Metadata;
		SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, ProcessingOptions.ImplementationFingerprint);
		return true;
	}

	void VeroScriptModuleLoader::RecordLoadedFileStamp(const std::string& Path)
	{
		FLoadedFileStamp Stamp;
		Stamp.Path = Path;
		std::error_code FileError;
		const auto File = PathUtil::FromUtf8(Path);
		Stamp.Modified = std::filesystem::last_write_time(File, FileError);
		if (!FileError)
		{
			Stamp.Size = std::filesystem::file_size(File, FileError);
			Stamp.bValid = !FileError;
		}
		LoadedFileStamps.push_back(std::move(Stamp));
	}

	void VeroScriptModuleLoader::InitSearchPaths()
	{
		// Mobile resources and writable directories belong to the embedding app.
		// Explicit roots/overlays remain available; desktop convenience discovery
		// must not probe a mobile process's cwd, environment, or source checkout.
		if (GetHostPlatformInfo().bMobile || (ModuleEnvironment && !ModuleEnvironment->GetDescription().bAllowAmbientSearch)) return;
		if (const std::string EnvPath = Detail::GetEnvironmentVariable("VERO_PATH"); !EnvPath.empty())
		{
			EnvImportPaths = Detail::SplitPathList(EnvPath.c_str());
		}
		if (const std::string EnvStdlib = Detail::GetEnvironmentVariable("VERO_STDLIB_PATH");
		    !EnvStdlib.empty())
		{
			for (const std::string& Root : Detail::SplitPathList(EnvStdlib.c_str()))
			{
				AddStdlibSearchRoot(Root);
			}
		}

		const std::string ExecutableDirectory = Runtime::GetExecutableDirectory();
		// Distinct from the member AddStdlibSearchRoot (which inserts at highest
		// priority): env roots above win, so exe/cwd roots are appended (lowest).
		auto AppendStdlibSearchRoot = [this](const std::string& Root)
		{
			const std::string Normalized = Detail::NormalizePath(Root);
			if (Normalized.empty())
			{
				return;
			}
			for (const std::string& Existing : StdlibSearchRoots)
			{
				if (Existing == Normalized)
				{
					return;
				}
			}
			StdlibSearchRoots.push_back(Normalized);
		};
		if (!ExecutableDirectory.empty())
		{
			AppendStdlibSearchRoot(ExecutableDirectory + "/stdlib");
			// Portable SDKs keep runtime data outside bin/ so libraries, tools and
			// assets remain distinct. Resolve that layout relative to the executable;
			// callers must not need a working-directory or environment override.
			AppendStdlibSearchRoot(ExecutableDirectory + "/../assets/stdlib");
		}
		// Cwd convenience for deployed assets and source checkouts.
		AppendStdlibSearchRoot("stdlib");
		const auto SourceRoot = PathUtil::FindSourceModuleRoot(".");
		if (!SourceRoot.empty())
		{
			AppendStdlibSearchRoot(PathUtil::ToUtf8(
			    SourceRoot / "VeroScript/Resources/Scripts/stdlib"));
		}
		AppendStdlibSearchRoot("Resources/Scripts/stdlib");

		// Per-module localization: a stdlib/library root may ship its own
		// `localization/` catalog tree.
		for (const std::string& Root : StdlibSearchRoots)
		{
			MergeSiblingLocalization(Root);
		}
	}

	void VeroScriptModuleLoader::MergeSiblingLocalization(const std::string& DirectoryOrFile)
	{
		if (ModuleEnvironment || DirectoryOrFile.empty())
		{
			return;
		}
		std::error_code Ec;
		std::filesystem::path Base = PathUtil::FromUtf8(DirectoryOrFile);
		// Accept either a directory (stdlib root) or a module file (use its parent).
		if (!std::filesystem::is_directory(Base, Ec))
		{
			Base = Base.parent_path();
		}
		if (Base.empty())
		{
			return;
		}
		const std::string LocalizationDirectory = PathUtil::ToUtf8(Base / "localization");
		const std::string LocalizationIdentity = PathUtil::IdentityKey(LocalizationDirectory);
		if (!MergedLocalizationDirectories.insert(LocalizationIdentity).second)
		{
			return;
		}
		// Best-effort: missing catalogs simply return false and are ignored.
		Locale::MergeCatalogDirectory(LocalizationDirectory);
	}

	void VeroScriptModuleLoader::FoldPreprocessorDefinesIntoFingerprint()
	{
		if (bDefinesFoldedIntoFingerprint)
		{
			return;
		}
		bDefinesFoldedIntoFingerprint = true;
		for (const std::string& Define : PreprocessorDefines)
		{
			SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, std::string_view("D:"));
			SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, Define);
		}
		for (const FPredefinedMacro& Macro : PredefinedMacros)
		{
			SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, std::string_view("M:"));
			SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, Macro.Name);
			for (const std::string& Parameter : Macro.Parameters)
			{
				SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, std::string_view("P:"));
				SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, Parameter);
			}
			SourceFingerprint = Runtime::Fnv1a64(
			    SourceFingerprint,
			    Macro.bVariadic ? std::string_view("V:1") : std::string_view("V:0"));
			SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, std::string_view("B:"));
			SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, Macro.Body);
		}
	}

	VeroScriptModuleLoader::FPreparedModule* VeroScriptModuleLoader::PrepareModuleSource(
	    const std::string& ResolvedPath,
	    std::string Source)
	{
		CheckAnalysisCancellation();
		const std::string& StableResolvedPath = InternPath(ResolvedPath);
		if (ModuleEnvironment)
		{
			if (const auto* M = ModuleEnvironment->FindSource(ResolvedPath))
			{
				std::string Failure;
				if (!MergeExternalDependencies(
				        MergedProgram->RequiredExternalDependencies, M->RequiredDependencies, Failure))
				{
					throw FEnvironmentLoadError(Failure);
				}
			}
		}

		const std::string Identity = PathUtil::IdentityKey(StableResolvedPath);
		if (const auto Existing = PreparedModules.find(Identity);
		    Existing != PreparedModules.end())
		{
			return Existing->second.get();
		}

		if (ModuleEnvironment)
		{
			MergedProgram->ModuleSourceOwners.emplace(
			    StableResolvedPath, ModuleEnvironment->GetSourceOwner(StableResolvedPath));
		}

		auto Module = std::make_shared<FPreparedModule>();
		Module->ResolvedPath = StableResolvedPath;
		Module->Source = std::move(Source);
		if (IsStdlibModulePath(StableResolvedPath))
		{
			ProcessingSession->ReadOnlyModules.insert(StableResolvedPath);
		}

		try
		{
			if (!ProcessingSession->ProcessSource(
			        ECompileProcessingPhase::RawSource, StableResolvedPath, Module->Source))
			{
				bHasError = true;
				Diagnostics = ProcessingSession->Diagnostics;
				Error = Diagnostics.front();
				return nullptr;
			}
		}
		catch (const FAnalysisCancelled&)
		{
			throw;
		}
		catch (const std::exception& E)
		{
			bHasError = true;
			FillFromEnglishMessage(Error, E.what(), {StableResolvedPath, 1, 1});
			Diagnostics = {Error};
			return nullptr;
		}

		SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, Identity);
		SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, Module->Source);

		std::unordered_map<std::string, std::string> InitialDefines;
		{
			std::vector<std::string_view> Specs;
			Specs.reserve(PreprocessorDefines.size());
			for (const std::string& Define : PreprocessorDefines)
			{
				Specs.emplace_back(Define);
			}
			std::string DefineError;
			if (!BuildDefineMap(Specs, InitialDefines, DefineError))
			{
				bHasError = true;
				FillFromEnglishMessage(
				    Error,
				    DefineError,
				    FSourceLocation{StableResolvedPath, 1, 1});
				Diagnostics = {Error};
				return nullptr;
			}
		}

		FPreprocessContext Context{StableResolvedPath};
		Context.Budget = PreprocessBudget;
		Context.Mode = EPreprocessMode::Module;
		Context.MapSourcePosition = [Session = std::weak_ptr<FCompileProcessingSession>(ProcessingSession),
		                                StableResolvedPath](FPreprocessSourcePos P)
		{
			auto S = Session.lock();
			return S ? S->MapSourcePosition(StableResolvedPath, P) : P;
		};
		Module->Preprocessor = std::make_unique<FPreprocessorSession>(Module->Source, InitialDefines,
		    PredefinedMacros, Context,
		    bBuildPreprocessLineMaps ||
		        (ProcessingOptions.Registry &&
		            (ProcessingOptions.Registry->HasPhase(ECompileProcessingPhase::RawSource) ||
		                ProcessingOptions.Registry->HasPhase(ECompileProcessingPhase::ActiveSource))) ||
		        !ProcessingSession->Metadata->FindDocument(StableResolvedPath + ":raw")->Comments.empty());
		FPreparedModule* Result = Module.get();
		MergedProgram->LifetimeAnchors.push_back(Module);
		PreparedModules.emplace(Identity, std::move(Module));
		return Result;
	}

	void VeroScriptModuleLoader::CollectImplicitForwardTypes(const FPreparedModule& Module)
	{
		const std::vector<FToken>& Tokens = Module.Tokens;
		std::vector<std::string> NamespaceStack;
		std::vector<int> NamespaceBraceDepths;
		std::string PendingNamespaceName;
		int BraceDepth = 0;
		bool bPendingExport = false;

		for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
		{
			const FToken& Token = Tokens[Index];
			if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == "{")
			{
				++BraceDepth;
				if (!PendingNamespaceName.empty())
				{
					NamespaceStack.push_back(std::move(PendingNamespaceName));
					NamespaceBraceDepths.push_back(BraceDepth);
					PendingNamespaceName.clear();
				}
				else
				{
					bPendingExport = false;
				}
				continue;
			}
			if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == "}")
			{
				while (!NamespaceBraceDepths.empty() && NamespaceBraceDepths.back() == BraceDepth)
				{
					NamespaceBraceDepths.pop_back();
					NamespaceStack.pop_back();
				}
				if (BraceDepth > 0)
				{
					--BraceDepth;
				}
				PendingNamespaceName.clear();
				bPendingExport = false;
				continue;
			}

			const bool bAtNamespaceLevel =
			    BraceDepth == static_cast<int>(NamespaceBraceDepths.size());
			if (!bAtNamespaceLevel)
			{
				continue;
			}
			if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == ";")
			{
				bPendingExport = false;
				continue;
			}
			if (Token.Kind == ETokenKind::Keyword && Token.Lexeme == "namespace" && Index + 1 < Tokens.size() && Tokens[Index + 1].Kind == ETokenKind::Identifier)
			{
				PendingNamespaceName.assign(Tokens[Index + 1].Lexeme);
				std::size_t NameIndex = Index + 2;
				while (NameIndex + 1 < Tokens.size() && Tokens[NameIndex].Kind == ETokenKind::Symbol && Tokens[NameIndex].Lexeme == "::" && Tokens[NameIndex + 1].Kind == ETokenKind::Identifier)
				{
					PendingNamespaceName += '.';
					PendingNamespaceName += Tokens[NameIndex + 1].Lexeme;
					NameIndex += 2;
				}
				Index = NameIndex - 1;
				continue;
			}
			if (Token.Kind == ETokenKind::Keyword && Token.Lexeme == "export")
			{
				bPendingExport = true;
				continue;
			}
			if (Token.Kind == ETokenKind::Keyword && Token.Lexeme == "import")
			{
				bPendingExport = false;
				continue;
			}
			const bool bProjectInternal =
			    bAllowProjectWideAccess && MergedProgram->ProjectVisibleModulePaths.find(Module.ResolvedPath) != MergedProgram->ProjectVisibleModulePaths.end();
			if ((!bPendingExport && !bProjectInternal) || Token.Kind != ETokenKind::Keyword || (Token.Lexeme != "struct" && Token.Lexeme != "class" && Token.Lexeme != "interface" && Token.Lexeme != "enum" && Token.Lexeme != "using" && Token.Lexeme != "typedef") || Index + 1 >= Tokens.size() || Tokens[Index + 1].Kind != ETokenKind::Identifier)
			{
				continue;
			}

			std::string TypeName(Tokens[Index + 1].Lexeme);
			if (!NamespaceStack.empty())
			{
				std::string Qualified;
				for (const std::string& Namespace : NamespaceStack)
				{
					if (!Qualified.empty())
					{
						Qualified += '.';
					}
					Qualified += Namespace;
				}
				Qualified += '.';
				Qualified += TypeName;
				TypeName = std::move(Qualified);
			}
			ImplicitForwardTypeNames.insert(TypeName);
			if (Token.Lexeme == "interface")
			{
				ImplicitForwardInterfaceNames.insert(std::move(TypeName));
			}
			bPendingExport = false;
		}
	}

	std::shared_ptr<FPackageData> VeroScriptModuleLoader::PreparePackageInterface(
	    const std::string& ResolvedPath, const FSourceLocation* Location)
	{
		const std::string& StablePath = InternPath(ResolvedPath);
		const auto Identity = PathUtil::IdentityKey(StablePath);
		if (const auto It = PreparedPackages.find(Identity); It != PreparedPackages.end())
		{
			return It->second;
		}

		LoadedPaths.insert(StablePath);
		RecordLoadedFileStamp(StablePath);
		FArtifactData Artifact;
		FArtifactRequest Request{StablePath, "library", {}};
		FArtifactWork Work;
		Work.MaxBytes = VEROSCRIPT_PACKAGE_MAX_BYTES;
		Work.CheckCancelled = []
		{
			CheckAnalysisCancellation();
		};
		const IArtifactReader* Reader = nullptr;
		const IArtifactDecoder* Decoder = nullptr;
		const FResolvedModule* Resolved = nullptr;
		if (ModuleEnvironment)
		{
			const auto& Description = ModuleEnvironment->GetDescription();
			Reader = Description.ArtifactReader.get();
			Decoder = Description.ArtifactDecoder.get();
			Resolved = ModuleEnvironment->FindSource(StablePath);
			Request.OwnerId = ModuleEnvironment->GetSourceOwner(StablePath);
		}
		std::string ArtifactError;
		bool Read = false;
		if (Resolved && Resolved->Binary)
		{
			Artifact.Bytes = *Resolved->Binary;
			Artifact.ContentIdentity = Resolved->ContentIdentity;
			Read = DecodeArtifact(Request, Work, Artifact, ArtifactError, Decoder);
		}
		else
		{
			Read = ReadArtifact(Request, Work, Artifact, ArtifactError, Reader, Decoder);
		}
		if (!Read)
		{
			bHasError = true;
			FillDiagnostic(Error, "diag.cannot_open_import",
			    Location != nullptr ? *Location : FSourceLocation{ResolvedPath, 1, 1},
			    {ResolvedPath});
			Error.Message += ": " + ArtifactError;
			Diagnostics = {Error};
			return nullptr;
		}
		const auto& Bytes = Artifact.Bytes;

		FPackageData Package;
		std::string PackageError;
		if (!ReadPackage(FByteView(Bytes), Package, PackageError) || Package.Kind != EPackageKind::Library || !Package.Module)
		{
			bHasError = true;
			FillDiagnostic(Error, "diag.cannot_open_import",
			    Location != nullptr ? *Location : FSourceLocation{ResolvedPath, 1, 1},
			    {ResolvedPath});
			Error.Message += ": ";
			Error.Message += PackageError.empty()
			    ? "imported package is not a library"
			    : PackageError;
			Diagnostics = {Error};
			return nullptr;
		}
		if (ModuleEnvironment)
		{
			std::string Failure;
			if (!CheckExternalDependencies(Package.Module->RequiredExternalDependencies,
			        ModuleEnvironment->GetDescription().AvailableDependencies, Failure) ||
			    !MergeExternalDependencies(MergedProgram->RequiredExternalDependencies,
			        Package.Module->RequiredExternalDependencies, Failure))
			{
				throw FEnvironmentLoadError(Failure);
			}

			if (const auto* M = ModuleEnvironment->FindSource(StablePath))
			{
				if (!MergeExternalDependencies(
				        MergedProgram->RequiredExternalDependencies, M->RequiredDependencies, Failure))
				{
					throw FEnvironmentLoadError(Failure);
				}
			}
		}
		SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, Identity);
		SourceFingerprint = Runtime::Fnv1a64(
		    SourceFingerprint,
		    std::string_view(reinterpret_cast<const char*>(Bytes.data()), Bytes.size()));
		auto Result = std::make_shared<FPackageData>(std::move(Package));
		PreparedPackages.emplace(Identity, Result);
		return Result;
	}

	bool VeroScriptModuleLoader::ProveEmptyMacroClosure(const std::string& Path)
	{
		if (ProvenEmptyMacroModules.count(PathUtil::IdentityKey(Path)))
		{
			return true;
		}

		std::vector<std::pair<std::string, std::size_t>> Work{{Path, 1}};
		std::unordered_set<std::string> Visited;
		std::vector<std::string_view> Specs;
		for (const auto& D : PreprocessorDefines)
		{
			Specs.push_back(D);
		}

		std::unordered_map<std::string, std::string> Defines;
		std::string Ignored;
		if (!BuildDefineMap(Specs, Defines, Ignored))
		{
			return false;
		}

		while (!Work.empty())
		{
			CheckAnalysisCancellation();
			auto Item = std::move(Work.back());
			Work.pop_back();
			const auto Identity = PathUtil::IdentityKey(Item.first);
			if (!Visited.insert(Identity).second)
			{
				continue;
			}

			if (Item.second > Detail::MaxImportNesting)
			{
				return false;
			}

			if (Detail::IsLibraryPackagePath(Item.first) && !FindOverlaySource(Item.first) &&
			    !PreparedModules.count(Identity))
			{
				auto P = PreparePackageInterface(Item.first);
				if (!P || !P->Module->PublicMacros.empty())
				{
					return false;
				}

				continue;
			}
			FPreparedModule* Module = nullptr;
			if (const auto It = PreparedModules.find(Identity); It != PreparedModules.end())
			{
				Module = It->second.get();
			}

			else
			{
				std::string Source;
				if (const auto Overlay = FindOverlaySource(Item.first))
				{
					Source = *Overlay;
				}

				else
				{
					LoadedPaths.insert(Item.first);
					RecordLoadedFileStamp(Item.first);
					std::ifstream In(PathUtil::FromUtf8(Item.first), std::ios::binary);
					if (!In)
					{
						return false;
					}

					std::ostringstream B;
					B << In.rdbuf();
					Source = B.str();
				}
				Module = PrepareModuleSource(Item.first, std::move(Source));
				if (!Module)
				{
					return false;
				}
			}
			FPreprocessContext C{Module->ResolvedPath};
			C.Budget = PreprocessBudget;
			C.MapSourcePosition = [Session = ProcessingSession, Path = Module->ResolvedPath](
			                          FPreprocessSourcePos P)
			{
				return Session->MapSourcePosition(Path, P);
			};
			FPreprocessorSession Probe(Module->Source, Defines, PredefinedMacros, C);
			if (!ApplyImplicitMacros(*Module, Probe) || Probe.Advance() != EPreprocessStep::Complete ||
			    Probe.Result().bContainsExportDirective)
			{
				return false;
			}

			for (const auto& Import : Probe.Result().Imports)
			{
				std::string Target;
				if (!ResolveImportPath(Import.Path, GetDirectory(Item.first), Target, Item.first))
				{
					return false;
				}

				Work.push_back({std::move(Target), Item.second + 1});
			}
		}
		ProvenEmptyMacroModules.insert(Visited.begin(), Visited.end());
		return true;
	}

	bool VeroScriptModuleLoader::ApplyImplicitMacros(FPreparedModule& Module, FPreprocessorSession& Preprocessor)
	{
		if (!bImplicitMacroInterfacesReady) return true;
		// Providers and their explicit dependencies establish interfaces once.
		// Consumer preprocessing and cycle proofs use the same completed tables.
		const auto Identity = PathUtil::IdentityKey(Module.ResolvedPath);
		for (const auto& ProviderPath : ResolvedImplicitModulePaths)
		{
			CheckAnalysisCancellation();
			const auto ProviderIdentity = PathUtil::IdentityKey(ProviderPath);
			if (ProviderIdentity == Identity)
			{
				continue;
			}
			const std::vector<FPublicMacro>* Macros = nullptr;
			std::shared_ptr<const void> Owner;
			if (const auto Package = PreparedPackages.find(ProviderIdentity); Package != PreparedPackages.end())
			{
				Macros = &Package->second->Module->PublicMacros;
				Owner = Package->second;
			}
			else if (const auto Source = PreparedModules.find(ProviderIdentity);
			    Source != PreparedModules.end() && Source->second->bReady)
			{
				Macros = &Source->second->PublicMacros;
				Owner = Source->second;
			}
			if (!Macros || (!Macros->empty() && !Preprocessor.ImportImplicitMacros(*Macros, std::move(Owner))))
			{
				bHasError = true;
				Error = Preprocessor.Result().Diagnostic;
				if (Error.Message.empty())
				{
					FillFromEnglishMessage(Error,
					    Preprocessor.Error().empty() ? "implicit macro interface is unavailable" : Preprocessor.Error(),
					    {Module.ResolvedPath, 1, 1});
				}
				Diagnostics = {Error};
				return false;
			}
		}
		return true;
	}

	bool VeroScriptModuleLoader::PrepareImplicitModules(
	    const std::string* const EntrySource,
	    const std::string* const EntryPath)
	{
		if (!ResolvedImplicitModulePaths.empty() || ImplicitImportPaths.empty())
		{
			return true;
		}

		std::vector<std::string> RequestedPaths = ImplicitImportPaths;
		std::sort(RequestedPaths.begin(), RequestedPaths.end());
		for (const std::string& RequestedPath : RequestedPaths)
		{
			std::string ResolvedPath;
			const std::string RequestedIdentity = PathUtil::IdentityKey(RequestedPath);
			const std::string LocalCandidate =
			    Detail::NormalizePath(MainDirectory + "/" + RequestedPath);
			const bool bMatchesEntry =
			    EntryPath != nullptr && (RequestedIdentity == PathUtil::IdentityKey(*EntryPath) || PathUtil::IdentityKey(LocalCandidate) == PathUtil::IdentityKey(*EntryPath));
			if (bMatchesEntry)
			{
				ResolvedPath = *EntryPath;
			}
			else if (!ResolveImportPath(RequestedPath, MainDirectory, ResolvedPath, MergedProgram->EntryModulePath))
			{
				bHasError = true;
				FillDiagnostic(
				    Error,
				    "diag.cannot_resolve_import",
				    FSourceLocation{
				        MergedProgram->EntryModulePath.empty()
				            ? MainFilePath
				            : MergedProgram->EntryModulePath,
				        1,
				        1},
				    {RequestedPath});
				Diagnostics = {Error};
				return false;
			}

			const std::string& StablePath = InternPath(ResolvedPath);
			const std::string Identity = PathUtil::IdentityKey(StablePath);
			if (!ImplicitModuleIdentities.insert(Identity).second)
			{
				continue;
			}
			ResolvedImplicitModulePaths.push_back(StablePath);
		}

		std::sort(
		    ResolvedImplicitModulePaths.begin(),
		    ResolvedImplicitModulePaths.end(),
		    [](const std::string& A, const std::string& B)
		{
			return PathUtil::IdentityKey(A) < PathUtil::IdentityKey(B);
		});

		if (bAllowProjectWideAccess)
		{
			if (!MergedProgram->EntryModulePath.empty())
			{
				MergedProgram->ProjectVisibleModulePaths.insert(
				    MergedProgram->EntryModulePath);
			}
			for (const std::string& ResolvedPath : ResolvedImplicitModulePaths)
			{
				MergedProgram->ProjectVisibleModulePaths.insert(ResolvedPath);
			}
		}

		for (const std::string& ResolvedPath : ResolvedImplicitModulePaths)
		{
			// Binary libraries expose their structured bytecode signatures during
			// LoadPackageModule; never feed canonical package bytes to the source
			// preprocessor while preparing an implicit-import cohort.
			if (Detail::IsLibraryPackagePath(ResolvedPath) && !FindOverlaySource(ResolvedPath) &&
			    PreparedModules.count(PathUtil::IdentityKey(ResolvedPath)) == 0)
			{
				continue;
			}
			const std::string Identity = PathUtil::IdentityKey(ResolvedPath);
			std::string Source;
			if (EntrySource != nullptr && EntryPath != nullptr && Identity == PathUtil::IdentityKey(*EntryPath))
			{
				Source = *EntrySource;
			}
			else if (const std::string* OverlaySource = FindOverlaySource(ResolvedPath))
			{
				Source = *OverlaySource;
			}
			else
			{
				RecordLoadedFileStamp(ResolvedPath);
				std::ifstream InputStream(PathUtil::FromUtf8(ResolvedPath), std::ios::binary);
				if (!InputStream)
				{
					bHasError = true;
					FillDiagnostic(
					    Error,
					    "diag.cannot_open_import",
					    FSourceLocation{ResolvedPath, 1, 1},
					    {ResolvedPath});
					Diagnostics = {Error};
					return false;
				}
				std::ostringstream Buffer;
				Buffer << InputStream.rdbuf();
				Source = Buffer.str();
			}

			FPreparedModule* Module = PrepareModuleSource(ResolvedPath, std::move(Source));
			if (Module == nullptr)
			{
				return false;
			}
			CollectImplicitForwardTypes(*Module);
		}
		return true;
	}

	bool VeroScriptModuleLoader::PrepareReachableModuleGraph(const std::string& ResolvedPath,
	    const std::string& RequestedPath,
	    const bool bIsMainModule,
	    const std::string* SourceOverride,
	    const FSourceLocation* ImportLocation)
	{
		struct FFrame
		{
			std::string Path, Identity;
			FPreparedModule* Module;
		};
		std::vector<FFrame> Frames;
		struct FGuard
		{
			~FGuard()
			{
				Active.clear();
			}

			std::unordered_set<std::string>& Active;
		} Guard{PreparingGraphIdentities};
		const auto PhysicalFailure = [&]()
		{
			Error.bPhysicalLocation = true;
			for (auto& Diagnostic : Diagnostics)
			{
				Diagnostic.bPhysicalLocation = true;
			}

			return false;
		};
		const auto Fail = [&](std::string Message, const FSourceLocation& Location,
		                      bool bPhysicalLocation = true)
		{
			bHasError = true;
			FillFromEnglishMessage(Error, Message, Location);
			Error.bPhysicalLocation = bPhysicalLocation;
			Diagnostics = {Error};
			return false;
		};
		const auto Push = [&](const std::string& Path, const std::string* Override,
		                      const FSourceLocation& Location)
		{
			const std::string Stable = InternPath(Path), Identity = PathUtil::IdentityKey(Stable);
			if (PreparingGraphIdentities.size() >= Detail::MaxImportNesting)
			{
				return Fail("import nesting limit exceeded", Location);
			}

			FPreparedModule* Module = nullptr;
			const auto Existing = PreparedModules.find(Identity);
			if (Existing != PreparedModules.end())
			{
				Module = Existing->second.get();
			}

			else
			{
				std::string Source;
				if (Override)
				{
					Source = *Override;
				}

				else if (const auto Overlay = FindOverlaySource(Stable))
				{
					Source = *Overlay;
				}

				else
				{
					LoadedPaths.insert(Stable);
					RecordLoadedFileStamp(Stable);
					std::ifstream Input(PathUtil::FromUtf8(Stable), std::ios::binary);
					if (!Input)
					{
						bHasError = true;
						FillDiagnostic(Error,
						    Frames.empty() && bIsMainModule ? "diag.cannot_open_script"
						                                    : "diag.cannot_open_import",
						    Location, {Frames.empty() ? RequestedPath : Stable});
						Diagnostics = {Error};
						return PhysicalFailure();
					}
					std::ostringstream Buffer;
					Buffer << Input.rdbuf();
					Source = Buffer.str();
				}
				Module = PrepareModuleSource(Stable, std::move(Source));
				if (!Module)
				{
					return false;
				}
			}
			if (!ApplyImplicitMacros(*Module, *Module->Preprocessor)) return false;
			LoadedPaths.insert(Stable);
			PreparingGraphIdentities.insert(Identity);
			Frames.push_back({Stable, Identity, Module});
			return true;
		};
		const std::string RootIdentity = PathUtil::IdentityKey(ResolvedPath);
		if (PreparedGraphIdentities.count(RootIdentity))
		{
			return true;
		}

		if (Detail::IsLibraryPackagePath(ResolvedPath) && !SourceOverride &&
		    !FindOverlaySource(ResolvedPath) && !PreparedModules.count(RootIdentity))
		{
			if (!PreparePackageInterface(ResolvedPath, ImportLocation))
			{
				return PhysicalFailure();
			}

			PreparedGraphIdentities.insert(RootIdentity);
			return true;
		}
		if (!Push(ResolvedPath, SourceOverride,
		        ImportLocation ? *ImportLocation : FSourceLocation{InternPath(ResolvedPath), 1, 1}))
		{
			return false;
		}

		while (!Frames.empty())
		{
			CheckAnalysisCancellation();
			auto& Frame = Frames.back();
			auto& Module = *Frame.Module;
			const auto Step = Module.Preprocessor->Advance();
			if (Step == EPreprocessStep::Error)
			{
				if (bBuildPreprocessLineMaps)
				{
					Module.LineMap = Module.Preprocessor->LineMap();
				}

				bHasError = true;
				Error = Module.Preprocessor->Result().Diagnostic;
				Diagnostics = {Error};
				return false;
			}
			if (Step == EPreprocessStep::NeedModule)
			{
				const auto Import = Module.Preprocessor->PendingImport();
				const FSourceLocation Location{InternPath(Frame.Path), Import.Position.Line,
				    Import.Position.Column};
				std::string Target;
				if (!ResolveImportPath(Import.Path, GetDirectory(Frame.Path), Target, Frame.Path))
				{
					const auto RecordCandidate = [&](const std::string& Candidate)
					{
						const auto Normalized = PathUtil::Normalize(Candidate);
						LoadedPaths.insert(Normalized);
						RecordLoadedFileStamp(Normalized);
					};
					if (PathUtil::FromUtf8(Import.Path).is_absolute())
					{
						RecordCandidate(Import.Path);
					}

					RecordCandidate(GetDirectory(Frame.Path) + "/" + Import.Path);
					for (const auto& Directory : CliImportPaths)
					{
						RecordCandidate(Directory + "/" + Import.Path);
					}

					for (const auto& Directory : EnvImportPaths)
					{
						RecordCandidate(Directory + "/" + Import.Path);
					}

					for (const auto& Directory : StdlibSearchRoots)
					{
						RecordCandidate(Directory + "/" + Import.Path);
					}

					bHasError = true;
					FillDiagnostic(Error, "diag.cannot_resolve_import", Location, {Import.Path});
					Diagnostics = {Error};
					return PhysicalFailure();
				}
				const auto Identity = PathUtil::IdentityKey(Target);
				const std::vector<FPublicMacro>* Public = nullptr;
				std::shared_ptr<const void> Owner;
				static const std::vector<FPublicMacro> Empty;
				if (Detail::IsLibraryPackagePath(Target) && !FindOverlaySource(Target) &&
				    !PreparedModules.count(Identity))
				{
					auto Package = PreparePackageInterface(Target, &Location);
					if (!Package)
					{
						return PhysicalFailure();
					}

					Public = &Package->Module->PublicMacros;
					Owner = std::move(Package);
				}
				else if (const auto Existing = PreparedModules.find(Identity);
				    Existing != PreparedModules.end() && Existing->second->bReady)
				{
					Public = &Existing->second->PublicMacros;
					Owner = Existing->second;
				}
				else if (PreparingGraphIdentities.count(Identity))
				{
					if (!bAllowCircularImports)
					{
						bHasError = true;
						FillDiagnostic(Error, "diag.circular_import", Location, {Import.Path});
						Diagnostics = {Error};
						return PhysicalFailure();
					}
					if (!ProveEmptyMacroClosure(Target))
					{
						std::string Chain;
						for (const auto& F : Frames)
						{
							if (!Chain.empty())
							{
								Chain += " -> ";
							}

							Chain += F.Path;
						}
						return Fail(
						    "public macro interface cannot be determined in circular imports: " +
						        Chain + " -> " + Target,
						    Location);
					}
					Public = &Empty;
				}
				else
				{
					if (!Push(Target, nullptr, Location))
					{
						return false;
					}

					continue;
				}
				if (!Module.Preprocessor->ResumeImport(Target, *Public, std::move(Owner)))
				{
					return Fail(Module.Preprocessor->Error(), Location);
				}

				continue;
			}
			Module.Preprocessed = Module.Preprocessor->Source();
			try
			{
				ProcessingSession->RecordPreprocessedSource(Module.ResolvedPath, Module.Preprocessed, Module.Preprocessor->LineMap());
				if (!ProcessingSession->ProcessSource(
				        ECompileProcessingPhase::ActiveSource, Module.ResolvedPath, Module.Preprocessed))
				{
					bHasError = true;
					Diagnostics = ProcessingSession->Diagnostics;
					Error = Diagnostics.front();
					return false;
				}
			}
			catch (const FAnalysisCancelled&)
			{
				throw;
			}
			catch (const std::exception& E)
			{
				return Fail(E.what(), {InternPath(Frame.Path), 1, 1});
			}
			Module.PublicMacros = Module.Preprocessor->Result().PublicMacros;
			if (bBuildPreprocessLineMaps)
			{
				Module.LineMap = Module.Preprocessor->LineMap();
				Module.LineMap.PreprocessedSource = Module.Preprocessed;
				MergedProgram->PreprocessLineMaps[Frame.Identity] = Module.LineMap;
			}
			VeroScriptLexer Lexer(Module.Preprocessed, Module.ResolvedPath);
			Module.Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				const auto Location = Module.Tokens.empty() ? FSourceLocation{InternPath(Frame.Path), 1, 1}
				                                            : Module.Tokens.back().Location;
				return Fail(Lexer.GetErrorMessage(), Location, false);
			}
			Module.Imports = Detail::ExtractImportDecls(Module.Tokens);
			const auto& Edges = Module.Preprocessor->Result().Imports;
			if (Edges.size() != Module.Imports.size())
			{
				return Fail(
				    "preprocessor and parser import boundaries disagree", {InternPath(Frame.Path), 1, 1});
			}

			for (std::size_t I = 0; I < Edges.size(); ++I)
			{
				if (Edges[I].Path != Module.Imports[I].Path ||
				    Edges[I].bReExport != Module.Imports[I].bReExport)
				{
					return Fail(
					    "preprocessor and parser import boundaries disagree", {InternPath(Frame.Path), 1, 1});
				}

				Module.Imports[I].ResolvedPath = Edges[I].ResolvedPath;
				Module.Imports[I].ImporterModulePath = Frame.Path;
			}
			Module.bReady = true;
			Module.Preprocessor.reset();
			PreparedGraphIdentities.insert(Frame.Identity);
			PreparingGraphIdentities.erase(Frame.Identity);
			if (Frame.Path == MergedProgram->EntryModulePath)
			{
				MergedProgram->PublicMacros = Module.PublicMacros;
			}

			Frames.pop_back();
		}
		return true;
	}

	bool VeroScriptModuleLoader::IsImplicitModule(const std::string& ResolvedPath) const
	{
		return ImplicitModuleIdentities.find(PathUtil::IdentityKey(ResolvedPath)) != ImplicitModuleIdentities.end();
	}

	bool VeroScriptModuleLoader::IsStdlibModulePath(
	    const std::string& ResolvedPath) const
	{
		const std::string ModuleIdentity = PathUtil::IdentityKey(ResolvedPath);
		for (const std::string& Root : StdlibSearchRoots)
		{
			const std::string RootIdentity = PathUtil::IdentityKey(Root);
			if (RootIdentity.empty() || ModuleIdentity.size() <= RootIdentity.size())
			{
				continue;
			}
			if (ModuleIdentity.compare(0, RootIdentity.size(), RootIdentity) == 0 && (RootIdentity.back() == '/' || ModuleIdentity[RootIdentity.size()] == '/'))
			{
				return true;
			}
		}
		return false;
	}

	void VeroScriptModuleLoader::AppendImplicitImports(
	    const std::string& ImporterPath,
	    std::vector<FImportDecl>& Imports) const
	{
		const std::string ImporterIdentity = PathUtil::IdentityKey(ImporterPath);
		for (const std::string& ImplicitPath : ResolvedImplicitModulePaths)
		{
			const std::string ImplicitIdentity = PathUtil::IdentityKey(ImplicitPath);
			if (ImplicitIdentity == ImporterIdentity)
			{
				continue;
			}
			const bool bAlreadyImported = std::any_of(
			    Imports.begin(),
			    Imports.end(),
			    [&](const FImportDecl& ImportDecl)
			{
				return !ImportDecl.ResolvedPath.empty() && PathUtil::IdentityKey(ImportDecl.ResolvedPath) == ImplicitIdentity;
			});
			if (bAlreadyImported)
			{
				continue;
			}
			FImportDecl ImportDecl;
			ImportDecl.Path = ImplicitPath;
			ImportDecl.ImporterModulePath = ImporterPath;
			ImportDecl.ResolvedPath = ImplicitPath;
			ImportDecl.bImplicit = true;
			ImportDecl.Location = FSourceLocation{ImporterPath, 1, 1};
			Imports.push_back(std::move(ImportDecl));
		}
	}

	const std::string& VeroScriptModuleLoader::InternPath(std::string Path)
	{
		std::string Display = PathUtil::Normalize(std::move(Path));
		const std::string Identity = PathUtil::IdentityKey(Display);
		const auto Existing = IdentityToDisplayPath.find(Identity);
		if (Existing != IdentityToDisplayPath.end())
		{
			return *InternedPaths->find(Existing->second);
		}
		const auto Inserted = InternedPaths->insert(Display);
		IdentityToDisplayPath.emplace(Identity, *Inserted.first);
		return *Inserted.first;
	}

	bool VeroScriptModuleLoader::LoadModule(
	    const std::string& ResolvedPath,
	    const std::string& RequestedPath,
	    const bool bIsMainModule,
	    const FSourceLocation* const ImportLocation)
	{
		CheckAnalysisCancellation();
		const std::string Identity = PathUtil::IdentityKey(ResolvedPath);
		if (LoadedIdentities.find(Identity) != LoadedIdentities.end())
		{
			return true;
		}
		if (LoadingIdentities.find(Identity) != LoadingIdentities.end())
		{
			if (bModuleGraphPrepared)
			{
				return true;
			}
			bHasError = true;
			const FSourceLocation Location = ImportLocation
			    ? *ImportLocation
			    : FSourceLocation{RequestedPath, 1, 1};
			FillDiagnostic(
			    Error,
			    "diag.circular_import",
			    Location,
			    {RequestedPath});
			Diagnostics = {Error};
			return false;
		}
		if (Detail::IsLibraryPackagePath(ResolvedPath) && PreparedModules.find(Identity) == PreparedModules.end())
		{
			return LoadPackageModule(ResolvedPath, RequestedPath, ImportLocation);
		}

		// Keep dependency traversal on the heap. A bounded import chain can still
		// exhaust the native stack when its deepest source needs deep syntax parsing.
		struct FLoadFrame
		{
			std::string Path;
			std::string RequestedPath;
			std::string Identity;
			const FPreparedModule* Prepared = nullptr;
			FSourceLocation ImportLocation;
			bool bHasImportLocation = false;
			std::size_t NextImport = 0;
		};
		std::vector<FLoadFrame> Frames;
		struct FTraversalGuard
		{
			~FTraversalGuard()
			{
				for (const FLoadFrame& Frame : Frames)
				{
					Loading.erase(Frame.Identity);
				}
			}
			std::unordered_set<std::string>& Loading;
			std::vector<FLoadFrame>& Frames;
		} TraversalGuard{LoadingIdentities, Frames};
		auto PushModule = [&](const std::string& Path, const std::string& Request,
		                      const bool bMain, const FSourceLocation* const Location) -> bool
		{
			FLoadFrame Frame;
			Frame.Path = InternPath(Path);
			Frame.RequestedPath = Request;
			Frame.Identity = PathUtil::IdentityKey(Frame.Path);
			Frame.bHasImportLocation = Location != nullptr;
			Frame.ImportLocation = Location ? *Location : FSourceLocation{};
			const auto Prepared = PreparedModules.find(Frame.Identity);
			if (Prepared != PreparedModules.end())
			{
				Frame.Prepared = Prepared->second.get();
				// Preserve the original catalog precedence: merge on entry, before
				// visiting dependencies, even though source parsing is postorder.
				MergeSiblingLocalization(Frame.Path);
			}
			else if (!Detail::IsLibraryPackagePath(Frame.Path))
			{
				// Every source was prepared by graph discovery before loading begins.
				bHasError = true;
				FillDiagnostic(Error,
				    bMain ? "diag.cannot_open_script" : "diag.cannot_open_import",
				    Location ? *Location : FSourceLocation{Frame.Path, 1, 1}, {Request});
				Diagnostics = {Error};
				return false;
			}
			Frames.push_back(std::move(Frame));
			LoadingIdentities.insert(Frames.back().Identity);
			if (LoadingIdentities.size() > Detail::MaxImportNesting)
			{
				bHasError = true;
				FillFromEnglishMessage(Error, "import nesting limit exceeded",
				    Location ? *Location : FSourceLocation{Frames.back().Path, 1, 1});
				Diagnostics = {Error};
				return false;
			}
			return true;
		};
		if (!PushModule(ResolvedPath, RequestedPath, bIsMainModule, ImportLocation))
		{
			return false;
		}
		while (!Frames.empty())
		{
			CheckAnalysisCancellation();
			FLoadFrame& Frame = Frames.back();
			if (Frame.Prepared && Frame.NextImport < Frame.Prepared->Imports.size())
			{
				const FImportDecl& Import = Frame.Prepared->Imports[Frame.NextImport++];
				const std::string ImportIdentity = PathUtil::IdentityKey(Import.ResolvedPath);
				// Cycles were checked during graph discovery. Active ancestors are
				// forward declarations; implicit cohort members keep their stable order.
				if (IsImplicitModule(Import.ResolvedPath) || LoadedIdentities.find(ImportIdentity) != LoadedIdentities.end() || LoadingIdentities.find(ImportIdentity) != LoadingIdentities.end())
				{
					continue;
				}
				if (!PushModule(Import.ResolvedPath, Import.Path, false, &Import.Location))
				{
					return false;
				}
				continue;
			}
			FLoadFrame Ready = std::move(Frame);
			Frames.pop_back();
			LoadingIdentities.erase(Ready.Identity);
			const FSourceLocation* const Location = Ready.bHasImportLocation
			    ? &Ready.ImportLocation
			    : nullptr;
			if (!Ready.Prepared)
			{
				if (!LoadPackageModule(Ready.Path, Ready.RequestedPath, Location))
				{
					return false;
				}
			}
			else
			{
				if (!LoadModuleFromSource(Ready.Path, Ready.RequestedPath,
				        Ready.Prepared->Source, Location))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool VeroScriptModuleLoader::LoadPackageModule(const std::string& ResolvedPath,
	    const std::string& RequestedPath,
	    const FSourceLocation* const ImportLocation)
	try
	{
		const std::string& StablePath = InternPath(ResolvedPath);
		const std::string Identity = PathUtil::IdentityKey(StablePath);
		if (LoadedIdentities.find(Identity) != LoadedIdentities.end())
		{
			return true;
		}

		auto PreparedPackage = PreparePackageInterface(StablePath, ImportLocation);
		if (!PreparedPackage)
		{
			return false;
		}

		const FPackageData& Package = *PreparedPackage;
		const std::string PackageFacade = "<vslib:" + Package.PackageName + ">";
		if (ModuleEnvironment)
		{
			MergedProgram->ModuleSourceOwners.emplace(
			    PackageFacade, ModuleEnvironment->GetSourceOwner(StablePath));
		}

		LoadedPackageFacades[Identity] = PackageFacade;
		bool bPackageFacadeLoadedFromTemplates = false;

		if (!Package.Module->PackageTemplateModules.empty())
		{
			std::vector<FImportDecl> TemplateImports;
			FImportDecl PhysicalFacadeImport;
			PhysicalFacadeImport.ImporterModulePath = StablePath;
			PhysicalFacadeImport.ResolvedPath = PackageFacade;
			PhysicalFacadeImport.Path = PackageFacade;
			PhysicalFacadeImport.bReExport = true;
			PhysicalFacadeImport.Location = FSourceLocation{StablePath, 1, 1};
			TemplateImports.push_back(std::move(PhysicalFacadeImport));

			struct FParsedTemplateInput
			{
				const FBytecodePackageTemplateModule* Module = nullptr;
				std::vector<FToken> Tokens;
			};
			std::vector<FParsedTemplateInput> Inputs;
			Inputs.reserve(Package.Module->PackageTemplateModules.size());
			for (const FBytecodePackageTemplateModule& Template :
			    Package.Module->PackageTemplateModules)
			{
				if (Template.bPackageSurface && Template.FacadePath == PackageFacade)
				{
					bPackageFacadeLoadedFromTemplates = true;
				}
				for (const FBytecodePackageTemplateImport& Import : Template.Imports)
				{
					FImportDecl Decl;
					Decl.Path = Import.ResolvedModulePath;
					Decl.ImporterModulePath = Template.ModulePath;
					Decl.ResolvedPath = Import.ResolvedModulePath;
					Decl.bReExport = Import.bReExport;
					Decl.Location = FSourceLocation{StablePath, 1, 1};
					TemplateImports.push_back(std::move(Decl));
				}
				if (Template.bPackageSurface && !Template.FacadePath.empty())
				{
					FImportDecl Surface;
					Surface.Path = Template.ModulePath;
					Surface.ImporterModulePath = Template.FacadePath;
					Surface.ResolvedPath = Template.ModulePath;
					Surface.bReExport = true;
					Surface.Location = FSourceLocation{StablePath, 1, 1};
					TemplateImports.push_back(std::move(Surface));
				}

				const auto TemplateFingerprint = Detail::PackageTemplateFingerprint(Template);
				const auto ExistingTemplate =
				    LoadedPackageTemplateFingerprints.find(Template.ModulePath);
				if (ExistingTemplate != LoadedPackageTemplateFingerprints.end())
				{
					if (ExistingTemplate->second != TemplateFingerprint)
					{
						bHasError = true;
						FillDiagnostic(
						    Error,
						    "diag.cannot_open_import",
						    ImportLocation != nullptr
						        ? *ImportLocation
						        : FSourceLocation{RequestedPath, 1, 1},
						    {RequestedPath});
						Error.Message += ": conflicting package template module: " + Template.ModulePath;
						Diagnostics = {Error};
						return false;
					}
					continue;
				}
				LoadedPackageTemplateFingerprints.emplace(
				    Template.ModulePath, TemplateFingerprint);

				FParsedTemplateInput TemplateInput;
				TemplateInput.Module = &Template;
				VeroScriptLexer Lexer(Template.Source, Template.ModulePath);
				TemplateInput.Tokens = Lexer.Tokenize();
				if (Lexer.HasError())
				{
					bHasError = true;
					FillFromEnglishMessage(
					    Error,
					    Lexer.GetErrorMessage(),
					    FSourceLocation{StablePath, 1, 1});
					Diagnostics = {Error};
					return false;
				}
				FPreparedModule ForwardModule;
				ForwardModule.ResolvedPath = Template.ModulePath;
				ForwardModule.Tokens = TemplateInput.Tokens;
				CollectImplicitForwardTypes(ForwardModule);
				Inputs.push_back(std::move(TemplateInput));
			}

			// Template AST merges discard their parsed imports, so this graph
			// stays fixed until the complete package batch has been parsed.
			const auto Visibility = Inputs.empty()
			    ? ModuleVisibility::FImportIndex(std::vector<FImportDecl>{})
			    : ModuleVisibility::FImportIndex(MergedProgram->Imports, TemplateImports);
			for (FParsedTemplateInput& TemplateInput : Inputs)
			{
				const FBytecodePackageTemplateModule& Template =
				    *TemplateInput.Module;
				VeroScriptParser Parser(
				    TemplateInput.Tokens,
				    Template.ModulePath,
				    MergedProgram.get(),
				    Visibility.CollectVisibleFrom(Template.ModulePath));
				// Package template source was accepted by a producer compiler. Keep
				// standard-library implementation bodies usable when an exported open
				// generic is instantiated by a downstream package consumer.
				Parser.SetAllowInternalBuiltins(true);
				Parser.SetCollectMultipleErrors(true);
				Parser.SetDeclarationOutline(!Template.CompileGeneratedOperations.empty(), Template.Source);
				if (!HostTypeNames.empty())
				{
					Parser.SetHostTypeNames(HostTypeNames);
				}
				Parser.SetAdditionalForwardTypeNames(
				    ImplicitForwardTypeNames,
				    ImplicitForwardInterfaceNames);
				std::unique_ptr<FProgram> TemplateProgram = Parser.ParseProgram();
				if (Parser.HasError() || !TemplateProgram)
				{
					bHasError = true;
					Diagnostics = Parser.GetDiagnostics();
					Error = !Diagnostics.empty()
					    ? Diagnostics.front()
					    : Parser.GetError();
					return false;
				}
				for (FTypeAliasDecl& Alias : TemplateProgram->TypeAliases)
				{
					Alias.bPrecompiledPackage = true;
				}
				for (FEnumDecl& Enum : TemplateProgram->Enums)
				{
					Enum.bPrecompiledPackage = true;
				}
				for (FStructDecl& Struct : TemplateProgram->Structs)
				{
					Struct.bPrecompiledPackage =
					    Template.bDeclarationOnly || Struct.TypeParams.empty();
				}
				for (FClassDecl& Class : TemplateProgram->Classes)
				{
					Class.bPrecompiledPackage =
					    Template.bDeclarationOnly || Class.TypeParams.empty();
				}
				for (FInterfaceDecl& Interface : TemplateProgram->Interfaces)
				{
					Interface.bPrecompiledPackage =
					    Template.bDeclarationOnly || Interface.TypeParams.empty();
				}
				for (FFunctionDecl& Function : TemplateProgram->Functions)
				{
					Function.bPrecompiledPackage =
					    Template.bDeclarationOnly || Function.TypeParams.empty();
				}
				for (auto It = TemplateProgram->TopLevelStatements.begin();
				    It != TemplateProgram->TopLevelStatements.end();)
				{
					if (*It && (*It)->Kind == EStmtKind::VarDecl)
					{
						static_cast<FVarDeclStmt&>(**It).bPrecompiledPackage = true;
						++It;
					}
					else
					{
						It = TemplateProgram->TopLevelStatements.erase(It);
					}
				}
				TemplateProgram->Imports.clear();
				MergeProgram(std::move(*TemplateProgram));

				FPackageSourceModule Carried;
				Carried.ModulePath = Template.ModulePath;
				Carried.Source = Template.Source;
				Carried.OwnerComponentId = Template.OwnerComponentId;
				Carried.SourceOwnerId = Template.SourceOwnerId.empty() && ModuleEnvironment ? ModuleEnvironment->GetSourceOwner(StablePath) : Template.SourceOwnerId;
				MergedProgram->ModuleSourceOwners.emplace(Template.ModulePath, Carried.SourceOwnerId);
				Carried.GeneratedOperations = Template.GeneratedOperations;
				Carried.CompileGeneratedOperations = Template.CompileGeneratedOperations;
				Carried.InitialSourceMetadata = Template.InitialSourceMetadata;
				Carried.bDeclarationOnly = Template.bDeclarationOnly;
				Carried.bFromPackage = true;
				Carried.bPackageSurface = Template.bPackageSurface;
				Carried.FacadePath = Template.FacadePath;
				for (const FBytecodePackageTemplateImport& Import : Template.Imports)
				{
					FImportDecl Decl;
					Decl.Path = Import.ResolvedModulePath;
					Decl.ImporterModulePath = Template.ModulePath;
					Decl.ResolvedPath = Import.ResolvedModulePath;
					Decl.bReExport = Import.bReExport;
					Carried.Imports.push_back(std::move(Decl));
				}
				MergedProgram->PackageSourceModules.push_back(std::move(Carried));
			}
			MergedProgram->Imports.insert(MergedProgram->Imports.end(),
			    std::make_move_iterator(TemplateImports.begin()),
			    std::make_move_iterator(TemplateImports.end()));
		}
		if (!Package.Module->PackageInterfaceSource.empty() && !bPackageFacadeLoadedFromTemplates)
		{
			FBytecodePackageTemplateModule FacadeTemplate;
			FacadeTemplate.ModulePath = PackageFacade;
			FacadeTemplate.Source = Package.Module->PackageInterfaceSource;
			FacadeTemplate.bDeclarationOnly = FacadeTemplate.bFromPackage =
			    FacadeTemplate.bPackageSurface = true;
			FacadeTemplate.FacadePath = PackageFacade;
			FacadeTemplate.OwnerComponentId = Package.PackageName + "@" + std::to_string(Package.PackageVersion);
			const auto Fingerprint = Detail::PackageTemplateFingerprint(FacadeTemplate);
			const auto Existing = LoadedPackageTemplateFingerprints.find(PackageFacade);
			if (Existing != LoadedPackageTemplateFingerprints.end() &&
			    Existing->second != Fingerprint)
			{
				bHasError = true;
				FillFromEnglishMessage(
				    Error, "conflicting package template module: " + PackageFacade,
				    ImportLocation ? *ImportLocation : FSourceLocation{StablePath, 1, 1});
				Diagnostics = {Error};
				return false;
			}
			if (Existing == LoadedPackageTemplateFingerprints.end())
			{
				VeroScriptLexer Lexer(Package.Module->PackageInterfaceSource, StablePath);
				const std::vector<FToken>& InterfaceTokens = Lexer.Tokenize();
				if (Lexer.HasError())
				{
					bHasError = true;
					FSourceLocation Location{StablePath, 1, 1};
					if (!InterfaceTokens.empty())
					{
						Location = InterfaceTokens.back().Location;
						Location.FileLabel = StablePath;
					}
					FillFromEnglishMessage(Error, Lexer.GetErrorMessage(), Location);
					Diagnostics = {Error};
					return false;
				}
				std::unordered_set<std::string> VisibleModulePaths;
				VisibleModulePaths.insert(StablePath);
				VeroScriptParser Parser(InterfaceTokens, StablePath, MergedProgram.get(),
				    std::move(VisibleModulePaths));
				Parser.SetAllowInternalBuiltins(true);
				Parser.SetCollectMultipleErrors(true);
				if (!HostTypeNames.empty())
				{
					Parser.SetHostTypeNames(HostTypeNames);
				}
				FPreparedModule InterfaceModule;
				InterfaceModule.ResolvedPath = StablePath;
				InterfaceModule.Tokens = InterfaceTokens;
				CollectImplicitForwardTypes(InterfaceModule);
				if (!ImplicitForwardTypeNames.empty())
				{
					Parser.SetAdditionalForwardTypeNames(ImplicitForwardTypeNames,
					    ImplicitForwardInterfaceNames);
				}
				std::unique_ptr<FProgram> InterfaceProgram = Parser.ParseProgram();
				if (Parser.HasError() || !InterfaceProgram)
				{
					bHasError = true;
					Diagnostics = Parser.GetDiagnostics();
					Error = !Diagnostics.empty() ? Diagnostics.front() : Parser.GetError();
					return false;
				}
				for (FTypeAliasDecl& Alias : InterfaceProgram->TypeAliases)
				{
					Alias.ModulePath = PackageFacade;
					Alias.bPrecompiledPackage = true;
				}
				for (FEnumDecl& Enum : InterfaceProgram->Enums)
				{
					Enum.ModulePath = PackageFacade;
					Enum.bPrecompiledPackage = true;
				}
				for (FStructDecl& Struct : InterfaceProgram->Structs)
				{
					Struct.ModulePath = PackageFacade;
					Struct.bPrecompiledPackage = true;
				}
				for (FClassDecl& Class : InterfaceProgram->Classes)
				{
					Class.ModulePath = PackageFacade;
					Class.bPrecompiledPackage = true;
				}
				for (FInterfaceDecl& Interface : InterfaceProgram->Interfaces)
				{
					Interface.ModulePath = PackageFacade;
					Interface.bPrecompiledPackage = true;
				}
				for (FFunctionDecl& Function : InterfaceProgram->Functions)
				{
					Function.ModulePath = PackageFacade;
					Function.bPrecompiledPackage = true;
				}
				for (const std::unique_ptr<FStmt>& Statement : InterfaceProgram->TopLevelStatements)
				{
					if (Statement && Statement->Kind == EStmtKind::VarDecl)
					{
						Statement->ModulePath = PackageFacade;
						static_cast<FVarDeclStmt&>(*Statement).ModulePath = PackageFacade;
						static_cast<FVarDeclStmt&>(*Statement).bPrecompiledPackage = true;
					}
				}
				InterfaceProgram->Imports.clear();
				MergeProgram(std::move(*InterfaceProgram));
				LoadedPackageTemplateFingerprints.emplace(PackageFacade, Fingerprint);
			}

			FImportDecl FacadeImport;
			FacadeImport.Path = PackageFacade;
			FacadeImport.ImporterModulePath = StablePath;
			FacadeImport.ResolvedPath = PackageFacade;
			FacadeImport.bReExport = true;
			FacadeImport.Location = FSourceLocation{StablePath, 1, 1};
			MergedProgram->Imports.push_back(std::move(FacadeImport));

			FPackageSourceModule Carried;
			Carried.ModulePath = PackageFacade;
			Carried.Source = Package.Module->PackageInterfaceSource;
			Carried.bDeclarationOnly = true;
			Carried.bFromPackage = true;
			Carried.bPackageSurface = true;
			Carried.FacadePath = PackageFacade;
			Carried.OwnerComponentId = Package.PackageName + "@" + std::to_string(Package.PackageVersion);
			MergedProgram->PackageSourceModules.push_back(std::move(Carried));
		}
		else if (Package.Module->PackageTemplateModules.empty())
		{
			for (const FBytecodeFunction& BytecodeFunction : Package.Module->Functions)
			{
				if (!BytecodeFunction.bPackageExported || BytecodeFunction.bIsMethod)
				{
					continue;
				}
				const std::size_t Marker = BytecodeFunction.Name.find(
				    "#fn#", FindLinkageIdentityStart(BytecodeFunction.Name));
				if (Marker == std::string::npos)
				{
					continue;
				}
				const std::string PublicName =
				    BytecodeFunction.Name.substr(0, Marker);
				if (PublicName.empty() || PublicName == "main" || PublicName.find('\x1f') != std::string::npos || PublicName.find("::") != std::string::npos)
				{
					continue;
				}
				std::vector<std::string> ParamTypes;
				std::string ReturnType;
				if (!Runtime::ParseFunctionType(
				        BytecodeFunction.ClosureTypeName,
				        ParamTypes,
				        ReturnType) ||
				    ParamTypes.size() != BytecodeFunction.ParamCount)
				{
					bHasError = true;
					FillDiagnostic(
					    Error,
					    "diag.cannot_open_import",
					    ImportLocation != nullptr
					        ? *ImportLocation
					        : FSourceLocation{RequestedPath, 1, 1},
					    {RequestedPath});
					Error.Message += ": exported function has incomplete signature metadata: " + BytecodeFunction.Name;
					Diagnostics = {Error};
					return false;
				}

				FFunctionDecl Function;
				Function.Name = PublicName;
				Function.ModulePath = PackageFacade;
				Function.ReturnType = std::move(ReturnType);
				Function.bExported = true;
				Function.bPrecompiledPackage = true;
				Function.Location = FSourceLocation{StablePath, 1, 1};
				Function.Body = std::make_unique<FBlockStmt>();
				Function.Parameters.reserve(ParamTypes.size());
				for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
				{
					FFunctionParam Param;
					Param.TypeName = std::move(ParamTypes[Index]);
					Param.Name = "$package_arg" + std::to_string(Index);
					if (Index < BytecodeFunction.ParamFlags.size())
					{
						const std::uint8_t Flags =
						    BytecodeFunction.ParamFlags[Index];
						Param.bIsRef =
						    (Flags & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0;
						Param.bIsConst =
						    (Flags & BytecodeParamFlagConstRef) != 0;
					}
					Function.Parameters.push_back(std::move(Param));
				}
				MergedProgram->Functions.push_back(std::move(Function));
			}
		}

		// The binary snapshot was already fingerprinted during interface preparation.
		if (StablePath == MergedProgram->EntryModulePath)
		{
			MergedProgram->PublicMacros = Package.Module->PublicMacros;
			// Recompiling a binary root is a facade over the original declarations.
			// Retain its export edge as a root template, just like a source facade,
			// so a repackaged library cannot flatten and duplicate those declarations.
			FPackageSourceModule RootFacade;
			RootFacade.ModulePath = StablePath;
			RootFacade.Source = "export import \"" + PackageFacade + "\";";
			RootFacade.bDeclarationOnly = true;
			RootFacade.bPackageSurface = true;
			RootFacade.OwnerComponentId = Package.PackageName + "@" + std::to_string(Package.PackageVersion);
			FImportDecl Export;
			Export.Path = Export.ResolvedPath = PackageFacade;
			Export.ImporterModulePath = StablePath;
			Export.bReExport = true;
			RootFacade.Imports.push_back(std::move(Export));
			MergedProgram->PackageSourceModules.push_back(std::move(RootFacade));
		}
		MergedProgram->PrecompiledPackageModules.push_back(
		    std::shared_ptr<const FBytecodeModule>(PreparedPackage, Package.Module.get()));
		LoadedIdentities.insert(Identity);
		LoadedPaths.insert(StablePath);
		return true;
	}
	catch (const std::bad_alloc&)
	{
		bHasError = true;
		FillDiagnostic(
		    Error,
		    "diag.cannot_open_import",
		    ImportLocation != nullptr
		        ? *ImportLocation
		        : FSourceLocation{RequestedPath, 1, 1},
		    {RequestedPath});
		Error.Message += ": out of memory while loading package";
		Diagnostics = {Error};
		return false;
	}
	catch (const std::length_error&)
	{
		bHasError = true;
		FillDiagnostic(
		    Error,
		    "diag.cannot_open_import",
		    ImportLocation != nullptr
		        ? *ImportLocation
		        : FSourceLocation{RequestedPath, 1, 1},
		    {RequestedPath});
		Error.Message += ": invalid package allocation size";
		Diagnostics = {Error};
		return false;
	}

	bool VeroScriptModuleLoader::LoadModuleFromSource(
	    const std::string& ResolvedPath,
	    const std::string& RequestedPath,
	    const std::string_view Source,
	    const FSourceLocation* const ImportLocation)
	{
		const std::string& StableResolvedPath = InternPath(ResolvedPath);
		if (ModuleEnvironment)
		{
			if (const auto* M = ModuleEnvironment->FindSource(ResolvedPath))
			{
				std::string Failure;
				if (!MergeExternalDependencies(
				        MergedProgram->RequiredExternalDependencies, M->RequiredDependencies, Failure))
				{
					throw FEnvironmentLoadError(Failure);
				}
			}
		}

		const std::string Identity = PathUtil::IdentityKey(StableResolvedPath);
		if (LoadedIdentities.find(Identity) != LoadedIdentities.end())
		{
			return true;
		}
		if (LoadingIdentities.find(Identity) != LoadingIdentities.end())
		{
			if (bModuleGraphPrepared)
			{
				return true;
			}
			bHasError = true;
			const FSourceLocation Location = ImportLocation
			    ? *ImportLocation
			    : FSourceLocation{RequestedPath, 1, 1};
			FillDiagnostic(
			    Error,
			    "diag.circular_import",
			    Location,
			    {RequestedPath});
			Diagnostics = {Error};
			return false;
		}

		FPreparedModule* Prepared = PrepareModuleSource(StableResolvedPath, std::string(Source));
		if (Prepared == nullptr)
		{
			return false;
		}
		const std::vector<FToken>& Tokens = Prepared->Tokens;

		LoadingIdentities.insert(Identity);
		struct FLoadingScopeGuard
		{
			~FLoadingScopeGuard()
			{
				LoadingIdentities.erase(Identity);
			}

			std::unordered_set<std::string>& LoadingIdentities;
			const std::string& Identity;

		} LoadingGuard{LoadingIdentities, Identity};
		if (LoadingIdentities.size() > Detail::MaxImportNesting)
		{
			bHasError = true;
			FillFromEnglishMessage(Error, "import nesting limit exceeded",
			    ImportLocation ? *ImportLocation : FSourceLocation{StableResolvedPath, 1, 1});
			Diagnostics = {Error};
			return false;
		}

		std::vector<FImportDecl> PendingImports = Prepared->Imports;
		for (FImportDecl& ImportDecl : PendingImports)
		{
			ImportDecl.ImporterModulePath = StableResolvedPath;
			// Resolve once during preparation. Re-resolving here can bind declarations
			// to a different file than the macro interface when resources change.
			const std::string& StableImportPath = InternPath(ImportDecl.ResolvedPath);
			ImportDecl.ResolvedPath = StableImportPath;
			// Implicit-cohort members are parsed in stable cohort order. Avoid
			// recursively entering them here; their exported type declarations were
			// already collected during the preparation phase.
			if (!IsImplicitModule(StableImportPath) && !LoadModule(StableImportPath, ImportDecl.Path, false, &ImportDecl.Location))
			{
				return false;
			}
		}
		AppendImplicitImports(StableResolvedPath, PendingImports);

		std::vector<FImportDecl> VisibilityImports = MergedProgram->Imports;
		VisibilityImports.insert(
		    VisibilityImports.end(),
		    PendingImports.begin(),
		    PendingImports.end());
		std::unordered_set<std::string> VisibleModulePaths;
		ModuleVisibility::CollectVisibleModulePaths(
		    VisibilityImports,
		    StableResolvedPath,
		    VisibleModulePaths);
		for (const std::string& ProjectPath : MergedProgram->ProjectVisibleModulePaths)
		{
			VisibleModulePaths.insert(ModuleVisibility::ProjectVisibilityKey(ProjectPath));
		}

		// Keep suspended import frames small enough for the supported combined
		// import/syntax depth, even as parser template environments grow.
		auto ParserStorage = std::make_unique<VeroScriptParser>(Tokens, StableResolvedPath,
		    MergedProgram.get(), std::move(VisibleModulePaths));
		auto& Parser = *ParserStorage;
		Parser.SetAllowInternalBuiltins(IsStdlibModulePath(StableResolvedPath));
		Parser.SetCollectMultipleErrors(true);
		if (!HostTypeNames.empty())
		{
			Parser.SetHostTypeNames(HostTypeNames);
		}
		if (!ImplicitForwardTypeNames.empty())
		{
			Parser.SetAdditionalForwardTypeNames(
			    ImplicitForwardTypeNames,
			    ImplicitForwardInterfaceNames);
		}
		Parser.SetSourceText(Prepared->Preprocessed);
		const bool PackageGeneratesTypes = std::any_of(MergedProgram->PackageSourceModules.begin(),
		    MergedProgram->PackageSourceModules.end(),
		    [](const auto& M)
		{
			return std::any_of(M.CompileGeneratedOperations.begin(), M.CompileGeneratedOperations.end(),
			    [](const auto& O)
			{
				return O.Phase == ECompileProcessingPhase::Types &&
				    (O.Kind == ECompileOperation::AddDeclaration ||
				        O.Kind == ECompileOperation::ReplaceDeclaration);
			});
		});
		// Package replay registers generated types during preparation too. Consumers
		// must defer their bodies until those types exist, without producer callbacks.
		Parser.SetDeclarationOutline(PackageGeneratesTypes || (ProcessingOptions.Registry && ProcessingOptions.Registry->HasPhase(ECompileProcessingPhase::Types)), Prepared->Preprocessed);
		std::unique_ptr<FProgram> Program = Parser.ParseProgram();
		if (Parser.HasError() || !Program)
		{
			bHasError = true;
			Diagnostics = Parser.GetDiagnostics();
			if (Diagnostics.empty() && Parser.HasError())
			{
				Diagnostics.push_back(Parser.GetError());
			}
			Error = !Diagnostics.empty() ? Diagnostics.front() : Parser.GetError();
			return false;
		}

		if (Program->Imports.size() != Prepared->Imports.size())
		{
			bHasError = true;
			FillFromEnglishMessage(Error, "preprocessor and parser import boundaries disagree",
			    {StableResolvedPath, 1, 1});
			Diagnostics = {Error};
			return false;
		}
		for (std::size_t I = 0; I < Program->Imports.size(); ++I)
		{
			auto& ImportDecl = Program->Imports[I];
			const auto& PreparedImport = Prepared->Imports[I];
			if (ImportDecl.Path != PreparedImport.Path ||
			    ImportDecl.bReExport != PreparedImport.bReExport)
			{
				bHasError = true;
				FillFromEnglishMessage(Error, "preprocessor and parser import boundaries disagree",
				    ImportDecl.Location);
				Diagnostics = {Error};
				return false;
			}
			ImportDecl.ImporterModulePath = StableResolvedPath;
			ImportDecl.ResolvedPath = InternPath(PreparedImport.ResolvedPath);
		}
		AppendImplicitImports(StableResolvedPath, Program->Imports);

		FPackageSourceModule PackageSource;
		if (ModuleEnvironment)
		{
			PackageSource.SourceOwnerId = ModuleEnvironment->GetSourceOwner(StableResolvedPath);
		}

		PackageSource.ModulePath = StableResolvedPath;
		PackageSource.Source = Prepared->Preprocessed;
		PackageSource.Imports = Program->Imports;
		for (FImportDecl& Import : PackageSource.Imports)
		{
			const auto PackageFacade = LoadedPackageFacades.find(
			    PathUtil::IdentityKey(Import.ResolvedPath));
			if (PackageFacade != LoadedPackageFacades.end())
			{
				Import.ResolvedPath = PackageFacade->second;
			}
		}
		Program->PackageSourceModules.push_back(std::move(PackageSource));

		LoadedIdentities.insert(Identity);
		LoadedPaths.insert(StableResolvedPath);
		MergeProgram(std::move(*Program));
		return true;
	}

	const std::string* VeroScriptModuleLoader::FindOverlayKey(const std::string& ResolvedPath) const
	{
		if (const auto Exact = SourceOverlays.find(ResolvedPath); Exact != SourceOverlays.end())
		{
			return &Exact->first;
		}
		const auto Identity = [this](const std::string& Path)
		{
			// Resolve existing parents as well as files: an unsaved overlay may
			// use a short Windows path or a symlink spelling of a search root.
			const auto Physical = ModuleEnvironment ? ModuleSupport::Physical(Path) : std::string{};
			return PathUtil::IdentityKey(Physical.empty() ? Path : Physical);
		};
		const std::string ResolvedIdentity = Identity(ResolvedPath);
		for (const auto& [Key, Source] : SourceOverlays)
		{
			(void)Source;
			if (Identity(Key) == ResolvedIdentity)
			{
				return &Key;
			}
		}

		if (ModuleEnvironment) return nullptr;

		auto PathEndsWith = [](const std::string& Path, const std::string& Suffix) -> bool
		{
			if (Suffix.empty() || Path.size() < Suffix.size())
			{
				return false;
			}
			if (Path.compare(Path.size() - Suffix.size(), Suffix.size(), Suffix) != 0)
			{
				return false;
			}
			return Path.size() == Suffix.size() || Path[Path.size() - Suffix.size() - 1] == '/';
		};

		const std::string* BestKey = nullptr;
		std::size_t BestKeySize = 0;
		const std::string NormalizedResolved = PathUtil::Normalize(ResolvedPath);
		for (const auto& [Key, Source] : SourceOverlays)
		{
			(void)Source;
			const std::string NormalizedKey = PathUtil::Normalize(Key);
			if (PathEndsWith(NormalizedResolved, NormalizedKey) || PathEndsWith(NormalizedKey, NormalizedResolved))
			{
				if (BestKey == nullptr || Key.size() > BestKeySize)
				{
					BestKey = &Key;
					BestKeySize = Key.size();
				}
			}
		}
		return BestKey;
	}

	const std::string* VeroScriptModuleLoader::FindOverlaySource(const std::string& ResolvedPath) const
	{
		if (const std::string* Key = FindOverlayKey(ResolvedPath))
		{
			return &SourceOverlays.find(*Key)->second;
		}
		return nullptr;
	}

	void VeroScriptModuleLoader::MergeProgram(FProgram&& ImportedProgram)
	{
		for (auto& Definition : ImportedProgram.SourceDefinitions)
		{
			MergedProgram->SourceDefinitions.push_back(std::move(Definition));
		}

		MergedProgram->ScopedTypeUses.insert(MergedProgram->ScopedTypeUses.end(),
		    ImportedProgram.ScopedTypeUses.begin(), ImportedProgram.ScopedTypeUses.end());
		MergedProgram->UsedCompilerBuiltinTypes.insert(
		    ImportedProgram.UsedCompilerBuiltinTypes.begin(), ImportedProgram.UsedCompilerBuiltinTypes.end());
		for (FImportDecl& ImportDecl : ImportedProgram.Imports)
		{
			MergedProgram->Imports.push_back(std::move(ImportDecl));
		}
		for (FNamespaceDecl& NamespaceDecl : ImportedProgram.Namespaces)
		{
			MergedProgram->Namespaces.push_back(std::move(NamespaceDecl));
		}
		for (FUsingNamespaceDecl& Directive : ImportedProgram.UsingNamespaces)
		{
			MergedProgram->UsingNamespaces.push_back(std::move(Directive));
		}
		for (std::unique_ptr<FStmt>& Statement : ImportedProgram.TopLevelStatements)
		{
			MergedProgram->TopLevelStatements.push_back(std::move(Statement));
		}
		for (FStructDecl& StructDecl : ImportedProgram.Structs)
		{
			MergedProgram->Structs.push_back(std::move(StructDecl));
		}
		for (FClassDecl& ClassDecl : ImportedProgram.Classes)
		{
			MergedProgram->Classes.push_back(std::move(ClassDecl));
		}
		for (FInterfaceDecl& InterfaceDecl : ImportedProgram.Interfaces)
		{
			MergedProgram->Interfaces.push_back(std::move(InterfaceDecl));
		}
		for (FEnumDecl& EnumDecl : ImportedProgram.Enums)
		{
			MergedProgram->Enums.push_back(std::move(EnumDecl));
		}
		for (FTypeAliasDecl& AliasDecl : ImportedProgram.TypeAliases)
		{
			MergedProgram->TypeAliases.push_back(std::move(AliasDecl));
		}
		for (FFunctionDecl& FunctionDecl : ImportedProgram.Functions)
		{
			MergedProgram->Functions.push_back(std::move(FunctionDecl));
		}
		for (FPackageSourceModule& SourceModule :
		    ImportedProgram.PackageSourceModules)
		{
			MergedProgram->PackageSourceModules.push_back(
			    std::move(SourceModule));
		}
	}

	std::string VeroScriptModuleLoader::GetDirectory(const std::string_view FilePath)
	{
		const std::string Normalized = Detail::NormalizePath(std::string(FilePath));
		const std::size_t SlashIndex = Normalized.find_last_of('/');
		if (SlashIndex == std::string::npos)
		{
			return ".";
		}
		return Normalized.substr(0, SlashIndex);
	}
}

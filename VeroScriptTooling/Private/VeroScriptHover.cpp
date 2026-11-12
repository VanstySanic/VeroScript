// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHover.h"

#include "Internal/VeroScriptOperatorLex.h"
#include "Internal/VeroScriptPreprocessorQuery.h"
#include "Internal/VeroScriptSourceDocumentation.h"
#include "Internal/VeroScriptStandardNamespaceMetadata.h"
#include "Internal/VeroScriptSymbolQuery.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptLocale.h"
#include "VeroScriptPath.h"
#include "VeroScriptTypeScope.h"
#include "VeroScriptSymbolKindMap.h"
#include "VeroScriptOverlayUtil.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling
{
	namespace
	{
		// Cross-file AST sites use the target module's preprocessed line space.
		// Resolve imports with the same configuration as the symbol query.
		std::string ExtractLeadingDocumentationRemapped(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FHoverOptions& Options,
		    const std::uint32_t PreprocessedDefinitionLine)
		{
			std::uint32_t PhysicalLine = PreprocessedDefinitionLine;
			VeroScript::FPreprocessLineMap LineMap;
			std::string PreprocessError;
			if (Detail::BuildModulePreprocessorLineMap(
			        Source,
			        FileLabel,
			        Options,
			        LineMap,
			        PreprocessError))
			{
				std::uint32_t Mapped = 0;
				if (Detail::MapPreprocessedLineToPhysicalLine(
				        LineMap,
				        PreprocessedDefinitionLine,
				        Mapped))
				{
					PhysicalLine = Mapped;
				}
			}
			return Detail::ExtractLeadingDocumentation(Source, PhysicalLine);
		}

		bool ReadSourceFile(const std::string_view Path, std::string& OutSource)
		{
			std::ifstream Input(PathUtil::FromUtf8(Path), std::ios::binary);
			if (!Input)
			{
				return false;
			}
			std::ostringstream Buffer;
			Buffer << Input.rdbuf();
			OutSource = Buffer.str();
			return true;
		}

		std::string DocumentationForHit(
		    const Detail::FSymbolQueryHit& Hit,
		    const std::string_view CurrentSource,
		    const std::string_view CurrentFileLabel,
		    const FHoverOptions& Options)
		{
			if (Hit.DefLine == 0 || Hit.FileLabel.empty())
			{
				return {};
			}

			if (Hit.FileLabel == CurrentFileLabel || PathUtil::Equal(Hit.FileLabel, std::string(CurrentFileLabel)))
			{
				return Detail::ExtractLeadingDocumentation(CurrentSource, Hit.DefLine);
			}
			const auto Extract = [&](const std::string_view Source)
			{
				// Macro definitions, including type-expanding macros, already
				// carry physical source coordinates from the preprocessor.
				if (Hit.bPhysicalLocation || Hit.Kind == Detail::EDocumentSymbolKind::Macro ||
				    Hit.Detail.rfind("#define", 0) == 0)
				{
					return Detail::ExtractLeadingDocumentation(Source, Hit.DefLine);
				}

				return ExtractLeadingDocumentationRemapped(Source, Hit.FileLabel, Options, Hit.DefLine);
			};

			for (auto I = Options.SourceOverlays.rbegin(); I != Options.SourceOverlays.rend(); ++I)
			{
				const auto& Overlay = *I;
				if (PathUtil::Equal(Hit.FileLabel, Overlay.Path))
				{
					return Extract(Overlay.Source);
				}
			}

			std::string FileSource;
			if (Hit.FileLabel.front() != '<' && ReadSourceFile(Hit.FileLabel, FileSource))
			{
				return Extract(FileSource);
			}
			return {};
		}

		const char* KindLabel(const EOutlineSymbolKind Kind, const std::string_view Detail = {})
		{
			switch (Kind)
			{
			case EOutlineSymbolKind::Import:
				return "import";
			case EOutlineSymbolKind::Namespace:
				return "namespace";
			case EOutlineSymbolKind::Function:
				return "function";
			case EOutlineSymbolKind::Method:
				return "method";
			case EOutlineSymbolKind::Constructor:
				return "constructor";
			case EOutlineSymbolKind::Destructor:
				return "destructor";
			case EOutlineSymbolKind::Struct:
				return "struct";
			case EOutlineSymbolKind::Class:
				return "class";
			case EOutlineSymbolKind::Interface:
				return "interface";
			case EOutlineSymbolKind::Enum:
				{
					const std::string_view Core = Detail::StripDeclMetaDisplay(Detail);
					return Core.rfind("enum class", 0) == 0 ? "enum class" : "enum";
				}
			case EOutlineSymbolKind::EnumMember:
				return "enum member";
			case EOutlineSymbolKind::Field:
				return "field";
			case EOutlineSymbolKind::Variable:
				return "variable";
			case EOutlineSymbolKind::Constant:
				return "constant";
			case EOutlineSymbolKind::TypeAlias:
				return "type alias";
			case EOutlineSymbolKind::Label:
				return "label";
			case EOutlineSymbolKind::Keyword:
				return "keyword";
			case EOutlineSymbolKind::Annotation:
				return "annotation";
			case EOutlineSymbolKind::AnnotationArgument:
				return "annotation argument";
			case EOutlineSymbolKind::Macro:
				return "macro";
			default:
				return "symbol";
			}
		}

		std::string DefinitionLocation(const FHoverHit& Hit)
		{
			if (Hit.FileLabel.empty() || Hit.FileLabel.front() == '<' || Hit.DefinitionRange.StartLine == 0)
			{
				return {};
			}

			const std::size_t Separator = Hit.FileLabel.find_last_of("/\\");
			const std::string_view FileName = Separator == std::string::npos
			    ? std::string_view(Hit.FileLabel)
			    : std::string_view(Hit.FileLabel).substr(Separator + 1);
			if (FileName.empty())
			{
				return {};
			}

			std::string Result;
			if (Hit.bExported)
			{
				Result = "exported | ";
			}
			Result += "defined at ";
			Result += FileName;
			Result.push_back(':');
			Result += std::to_string(Hit.DefinitionRange.StartLine);
			return Result;
		}

		std::string MarkdownDelimiter(const std::string_view Text, const std::size_t Minimum)
		{
			std::size_t LongestRun = 0;
			std::size_t Run = 0;
			for (const char Character : Text)
			{
				Run = Character == '`' ? Run + 1 : 0;
				LongestRun = std::max(LongestRun, Run);
			}
			return std::string(std::max(Minimum, LongestRun + 1), '`');
		}

		std::string FormatContents(
		    const std::vector<FHoverHit>& Definitions,
		    const bool bMarkdown = false)
		{
			if (Definitions.empty())
			{
				return {};
			}

			const auto IsSoftFormatVarargs = [](const FHoverHit& Hit)
			{
				if (!(Hit.FileLabel.empty() && Hit.DefinitionRange.StartLine == 0))
				{
					return false;
				}
				if (Hit.Detail.find("...") == std::string::npos)
				{
					return false;
				}
				return Hit.Name == "format" || Hit.Name == "formatf" || Hit.Name == "print" || Hit.Name == "printf";
			};

			bool bPreferSoftFormat = false;
			for (const FHoverHit& Hit : Definitions)
			{
				if (IsSoftFormatVarargs(Hit))
				{
					bPreferSoftFormat = true;
					break;
				}
			}

			std::string Contents;
			bool bWrote = false;
			for (const FHoverHit& Hit : Definitions)
			{
				if (bPreferSoftFormat && !IsSoftFormatVarargs(Hit))
				{
					continue;
				}
				if (bWrote)
				{
					Contents += bMarkdown ? "\n\n---\n\n" : "\n\n";
				}
				bWrote = true;
				const bool bBuiltinType = Hit.Detail == (bMarkdown ? Locale::TranslateId("tooling.builtin_type.detail") : "builtin type");
				std::string Title = KindLabel(Hit.Kind, Hit.Detail);
				Title.push_back(' ');
				if (Hit.Kind == EOutlineSymbolKind::Import)
				{
					// A module path is a filename, even when it starts with `std.`.
					Title += Hit.Name;
				}
				else if (Hit.Kind == EOutlineSymbolKind::Namespace)
				{
					Title += Detail::FormatNamespaceQualifiedName(Hit.Name);
				}
				else if (Hit.Name.rfind("std.", 0) == 0)
				{
					// Qualified standard-library aliases are stored in the symbol
					// index with its normalized dotted spelling.  Hover is user-facing,
					// so preserve the language spelling consistently with namespaces.
					Title += Detail::FormatNamespaceQualifiedName(Hit.Name);
				}
				else if (!Hit.ContainerName.empty())
				{
					Title += Detail::FormatStandardSourceSpelling(Hit.ContainerName);
					Title.push_back('.');
					Title += Detail::FormatOperatorDisplayName(Hit.Name);
				}
				else
				{
					Title += Detail::FormatOperatorDisplayName(Hit.Name);
				}
				if (bMarkdown)
				{
					const std::string Delimiter = MarkdownDelimiter(Title, 1);
					Contents += "**" + Delimiter + " " + Title + " " + Delimiter + "**";
				}
				else
				{
					Contents += Title;
				}
				if (!Hit.Detail.empty())
				{
					if (bMarkdown)
					{
						constexpr std::string_view SoftBuiltinPrefix = "soft builtin";
						if (Hit.Detail.rfind(SoftBuiltinPrefix, 0) == 0)
						{
							Contents += "\n\n" + Locale::TranslateId("tooling.hover.builtin");
							const std::size_t SummaryStart = Hit.Detail.find_first_not_of(
							    " \t", SoftBuiltinPrefix.size());
							if (SummaryStart != std::string::npos)
							{
								const std::string_view Summary = std::string_view(Hit.Detail).substr(SummaryStart);
								const std::string Delimiter = MarkdownDelimiter(Summary, 1);
								Contents += " " + Delimiter + " " + std::string(Summary) + " " + Delimiter;
							}
						}
						else if (bBuiltinType)
						{
							Contents += "\n\n" + Hit.Detail;
						}
						else
						{
							const std::string Fence = MarkdownDelimiter(Hit.Detail, 3);
							Contents += "\n\n" + Fence + "veroscript\n" + Hit.Detail + "\n" + Fence;
						}
					}
					else
					{
						Contents.push_back('\n');
						Contents += Hit.Detail;
					}
				}
				if (!Hit.Documentation.empty())
				{
					Contents += "\n\n";
					Contents += Detail::FormatDocumentationTags(Hit.Documentation);
				}
				const std::string Location = DefinitionLocation(Hit);
				if (!Location.empty())
				{
					Contents += "\n\n";
					if (bMarkdown)
					{
						const std::string Delimiter = MarkdownDelimiter(Location, 1);
						Contents += Delimiter + " " + Location + " " + Delimiter;
					}
					else
					{
						Contents += Location;
					}
				}
				if (Hit.FileLabel.empty() && Hit.DefinitionRange.StartLine == 0 && !bBuiltinType && Hit.Detail.find("soft builtin") == std::string::npos && Hit.Documentation.find("soft builtin") == std::string::npos && Hit.Kind != EOutlineSymbolKind::Namespace && Hit.Kind != EOutlineSymbolKind::Annotation && Hit.Kind != EOutlineSymbolKind::AnnotationArgument && Hit.Kind != EOutlineSymbolKind::Macro && Hit.Documentation.rfind("Predefined macro", 0) != 0)
				{
					Contents += bMarkdown
					    ? "\n\n*" + Locale::TranslateId("tooling.hover.builtin") + "*"
					    : "\n(soft builtin)";
				}
			}
			return Contents;
		}

		FHoverResult FromQueryResult(
		    const Detail::FSymbolQueryResult& In,
		    const std::string_view FallbackFileLabel,
		    const std::string_view CurrentSource,
		    const FHoverOptions& Options)
		{
			FHoverResult Result;
			Result.bSuccess = In.bSuccess;
			if (!In.bSuccess)
			{
				AppendToolingDiagnostics(
				    Result.Diagnostics,
				    In.Error,
				    In.Diagnostics,
				    FallbackFileLabel,
				    "hover query failed");
				return Result;
			}

			Result.bHasToken = In.bHasToken;
			Result.TokenText = In.TokenText;
			Result.TokenRange.StartLine = In.TokenLine;
			Result.TokenRange.StartColumn = In.TokenColumn;
			Result.TokenRange.EndLine = In.TokenLine;
			Result.TokenRange.EndColumn = In.TokenEndColumn;

			Result.Definitions.reserve(In.Definitions.size());
			for (const Detail::FSymbolQueryHit& Hit : In.Definitions)
			{
				FHoverHit Mapped;
				Mapped.Kind = MapDocumentSymbolKind(Hit.Kind);
				Mapped.Name = StripTypeScopeMarkers(Detail::FormatOperatorDisplayName(Hit.Name));
				if (Hit.bTypeDefinition)
				{
					const auto Dot = Mapped.Name.rfind('.', Mapped.Name.find('<'));
					if (Dot != std::string::npos)
					{
						Mapped.Name.erase(0, Dot + 1);
					}
				}
				Mapped.Detail = Detail::FormatStandardSourceSpelling(Hit.Detail);
				Mapped.Documentation = Hit.Documentation.empty() && !Hit.bHasSourceDocumentation &&
				        Options.CompileProcessing.CommentRetention != ECommentRetention::NoComments
				    ? DocumentationForHit(Hit, CurrentSource, FallbackFileLabel, Options)
				    : Hit.Documentation;
				Mapped.ContainerName = StripTypeScopeMarkers(Hit.ContainerName);
				if (Hit.Name.find("$arity") != std::string::npos && !Mapped.ContainerName.empty() &&
				    Mapped.Name.rfind(Mapped.ContainerName + ".", 0) == 0)
				{
					Mapped.Name.erase(0, Mapped.ContainerName.size() + 1);
				}
				Mapped.FileLabel = Hit.FileLabel;
				Mapped.DefinitionRange.StartLine = Hit.DefLine;
				Mapped.DefinitionRange.StartColumn = Hit.DefColumn;
				Mapped.DefinitionRange.EndLine = Hit.DefLine;
				Mapped.DefinitionRange.EndColumn = Hit.DefColumn;
				Mapped.bExported = Hit.bExported;
				Result.Definitions.push_back(std::move(Mapped));
			}

			if (Options.Bindings && !Options.ModuleEnvironment && Result.bHasToken && !Result.TokenText.empty())
			{
				bool bAlreadyHas = false;
				for (const FHoverHit& Hit : Result.Definitions)
				{
					// Annotation definitions retain canonical spelling under relaxed name matching.
					if (Hit.Name == Result.TokenText || Hit.Kind == EOutlineSymbolKind::Annotation)
					{
						bAlreadyHas = true;
						break;
					}
				}
				if (!bAlreadyHas)
				{
					const auto& HostFunctions = Options.Bindings->GetFunctions();
					if (std::any_of(HostFunctions.begin(), HostFunctions.end(), [&](const auto& F)
					{
						return F.Name == Result.TokenText;
					}))
					{
						for (const auto& Function : HostFunctions)
						{
							if (Function.Name != Result.TokenText)
							{
								continue;
							}
							FHoverHit Hit;
							Hit.Kind = EOutlineSymbolKind::Function;
							Hit.Name = Function.Name;
							Hit.Detail = VeroScript::FormatHostAnnotations(Function.Annotations);
							if (!Hit.Detail.empty())
							{
								Hit.Detail.push_back(' ');
							}
							Hit.Detail += VeroScript::FormatHostFunctionSignature(Function);
							Hit.Documentation = VeroScript::FormatHostDocumentation(
							    Function.Documentation, Function.ParamNames,
							    Function.ParamDocumentation, Function.ReturnDocumentation,
							    Function.Annotations, Function.ParamAnnotations);
							Hit.FileLabel = FallbackFileLabel;
							Result.Definitions.push_back(std::move(Hit));
						}
					}
					else if (const VeroScript::FHostTypeDesc* const Type =
					             Options.Bindings->FindType(Result.TokenText))
					{
						FHoverHit Hit;
						switch (Type->Kind)
						{
						case VeroScript::EHostTypeKind::Enum:
							{
								Hit.Kind = EOutlineSymbolKind::Enum;
								break;
							}
						case VeroScript::EHostTypeKind::Struct:
							{
								Hit.Kind = EOutlineSymbolKind::Struct;
								break;
							}
						default:
							{
								Hit.Kind = EOutlineSymbolKind::Class;
								break;
							}
						}
						Hit.Name = Type->Name;
						Hit.Detail = VeroScript::FormatHostAnnotations(Type->Annotations);
						if (!Hit.Detail.empty())
						{
							Hit.Detail.push_back(' ');
						}
						Hit.Detail += VeroScript::HostTypeKindName(Type->Kind);
						Hit.Documentation = VeroScript::FormatHostDocumentation(
						    Type->Documentation, {}, {}, {}, Type->Annotations, {});
						Hit.FileLabel = FallbackFileLabel;
						Result.Definitions.push_back(std::move(Hit));
					}
					else
					{
						for (const VeroScript::FHostAnnotationDesc& Annotation :
						    Options.Bindings->GetAnnotations())
						{
							if (!VeroScript::AnnotationNamesEqual(Annotation.Name, Result.TokenText, Annotation.CaseSensitive))
							{
								continue;
							}
							FHoverHit Hit;
							Hit.Kind = EOutlineSymbolKind::Annotation;
							Hit.Name = Annotation.Name;
							Hit.Detail = Annotation.Detail.empty()
							    ? std::string("annotation")
							    : Annotation.Detail;
							Hit.Documentation = Annotation.Documentation;
							Hit.FileLabel = FallbackFileLabel;
							Result.Definitions.push_back(std::move(Hit));
							break;
						}
					}
				}
			}

			Result.Contents = FormatContents(Result.Definitions);
			return Result;
		}

		Detail::FSymbolQueryOptions ToQueryOptions(const FHoverOptions& Options)
		{
			Detail::FSymbolQueryOptions Out;
			Out.ImportPaths = Options.ImportPaths;
			Out.StdlibSearchPaths = Options.StdlibSearchPaths;
			Out.ImplicitImportPaths = Options.ImplicitImportPaths;
			Out.bAllowCircularImports = Options.bAllowCircularImports;
			Out.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			Out.Defines = Options.Defines;
			Out.PredefinedMacros = Options.PredefinedMacros;
			Out.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
			Out.Bindings = Options.Bindings;
			Out.AnnotationProcessing = Options.AnnotationProcessing;
			Out.CompileProcessing = Options.CompileProcessing;
			Out.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			Out.ModuleEnvironment = Options.ModuleEnvironment;
			return Out;
		}

		Detail::FTextPosition ToEnginePosition(const FTextPosition Position)
		{
			return Detail::FTextPosition{Position.Line, Position.Column};
		}
	}

	std::string VeroScriptHover::FormatMarkdown(const FHoverResult& Result)
	{
		Locale::InitializeModuleFromEnvironment("VeroScriptTooling");
		return FormatContents(Result.Definitions, true);
	}

	FHoverResult VeroScriptHover::HoverAtSource(
	    const std::string_view Source,
	    const FTextPosition Position,
	    const std::string_view FileLabel,
	    const FHoverOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FHoverResult>(Failure);
			}

			return HoverAtSource(Source, Position, FileLabel, Effective);
		}

		return FromQueryResult(
		    Detail::VeroScriptSymbolQuery::QueryAtSource(
		        Source,
		        ToEnginePosition(Position),
		        FileLabel,
		        ToQueryOptions(Options)),
		    FileLabel,
		    Source,
		    Options);
	}

	FHoverResult VeroScriptHover::HoverAtFile(
	    const std::string_view FilePath,
	    const FTextPosition Position,
	    const FHoverOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FHoverResult>(Failure);
			}

			return HoverAtFile(FilePath, Position, Effective);
		}

		std::string Source;
		ReadSourceFile(FilePath, Source);
		return FromQueryResult(
		    Detail::VeroScriptSymbolQuery::QueryAtFile(
		        FilePath,
		        ToEnginePosition(Position),
		        ToQueryOptions(Options)),
		    FilePath,
		    Source,
		    Options);
	}
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiSourceImports.h"
#include "VeroScriptGuiSource.h"
#include "VeroScriptGuiMarkupGeneration.h"
#include "Bindings/VeroScriptGuiScriptLibrary.h"
#include "VeroScriptGuiVersion.h"
#include "VeroScriptLexer.h"
#include "VeroScriptPackage.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"
#include <algorithm>
#include <filesystem>

namespace VeroScript::Gui
{
	namespace
	{
		constexpr const char* ProcessorId = "veroscript.gui.source-imports";
		std::string ConfigurationKey()
		{
			return "source-imports-7:" + GetGuiScriptLibraryPath() + ':' + std::string(GetGuiCompilationFingerprint());
		}
		bool DirectiveToken(const std::vector<FToken>& Tokens, std::size_t Index)
		{
			return Tokens[Index].Lexeme == "#" && Index + 1 < Tokens.size() &&
			    (!Index || Tokens[Index - 1].Location.Line != Tokens[Index].Location.Line) &&
			    Tokens[Index + 1].Location.Line == Tokens[Index].Location.Line;
		}
		bool GuiToken(const std::vector<FToken>& Tokens, std::size_t Index, bool MacroBody = false)
		{
			const auto& Token = Tokens[Index];
			if (Token.Kind == ETokenKind::Symbol && (Token.Lexeme == "#" || Token.Lexeme == "##"))
			{
				if (!DirectiveToken(Tokens, Index)) return true;
				const auto Name = Tokens[Index + 1].Lexeme;
				return Name != "include" && Name != "define" && Name != "define_export" && Name != "undef" &&
				    Name != "if" && Name != "ifdef" && Name != "ifndef" && Name != "elif" &&
				    Name != "else" && Name != "endif" && Name != "error" && Name != "line";
			}
			if (MacroBody && (Token.Lexeme == "@" || Token.Lexeme == "import")) return true;
			if (Token.Kind != ETokenKind::Identifier) return false;
			// An ordinary variable, field or parameter named Gui needs no framework.
			if (Token.Lexeme == "Gui")
			{
				const auto Next = Index + 1 < Tokens.size() ? Tokens[Index + 1].Lexeme : std::string_view{};
				return MacroBody || Next == "::" || Next == "." ||
				    (Index && (Tokens[Index - 1].Lexeme == "::" || Tokens[Index - 1].Lexeme == "namespace")) ||
				    (Index >= 2 && Tokens[Index - 1].Lexeme == "." && Tokens[Index - 2].Lexeme == "VS");
			}
			if (Index && Tokens[Index - 1].Lexeme == "@")
			{
				for (const auto* Name : {"GuiWidget", "GuiProperty", "GuiMethod", "GuiEvent"})
				{
					if (AnnotationNamesEqual(Token.Lexeme, Name, false)) return true;
				}
			}
			return false;
		}
		bool MacroDefinitionNeedsGui(const std::vector<FToken>& Tokens, std::size_t Index)
		{
			std::size_t Begin = Index + 3, End = Begin;
			while (End < Tokens.size() && Tokens[End].Location.Line == Tokens[Index].Location.Line &&
			    !Tokens[End].Lexeme.empty())
			{
				++End;
			}
			if (Begin < End && Tokens[Begin].Lexeme == "(" &&
			    Tokens[Begin].Location.Column == Tokens[Begin - 1].Location.Column + Tokens[Begin - 1].Lexeme.size())
			{
				++Begin;
				if (Begin < End && Tokens[Begin].Lexeme != ")") return true;
				if (Begin < End)
				{
					++Begin;
				}
			}
			// Names and macro parameters can forward a namespace in any token
			// position, not only as a standalone replacement (for example {} x).
			for (std::size_t Body = Begin; Body < End; ++Body)
			{
				if (Tokens[Body].Kind == ETokenKind::Identifier || Tokens[Body].Kind == ETokenKind::Keyword) return true;
			}
			return false;
		}
		bool MacroNeedsGui(std::string_view Body, bool ConservativeNames = false)
		{
			if (HasScriptCompileExtensionImport(Body)) return true;
			VeroScriptLexer Lexer(Body);
			const auto& Tokens = Lexer.Tokenize();
			if (Lexer.HasError()) return true;
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const auto Text = Tokens[Index].Lexeme;
				if (GuiToken(Tokens, Index, true) || Text == "#" || Text == "##" ||
				    (ConservativeNames && (Tokens[Index].Kind == ETokenKind::Identifier || Tokens[Index].Kind == ETokenKind::Keyword || Text == "::" || Text == ".")))
				{
					return true;
				}
			}
			return false;
		}
		bool BinaryImportNeedsGui(const FCompileProcessingContext& Context, std::string_view Literal)
		{
			// Leave opaque, escaped and large imports on the conservative path.
			if (!Context.ReadDependency || Literal.size() < 8 || Literal.front() != '"' ||
			    Literal.substr(Literal.size() - 7) != ".vslib\"" || Literal.find('\\') != std::string_view::npos)
			{
				return true;
			}
			const std::string Path(Literal.substr(1, Literal.size() - 2));
			std::error_code Failure;
			const auto Size = std::filesystem::file_size(
			    PathUtil::FromUtf8(Context.ModuleId).parent_path() / PathUtil::FromUtf8(Path), Failure);
			if (Failure || Size > 1024 * 1024) return true;
			std::string Bytes, Error;
			FPackageData Package;
			if (!Context.ReadDependency(Path, Bytes) ||
			    !ReadPackage({reinterpret_cast<const std::uint8_t*>(Bytes.data()), Bytes.size()}, Package, Error) ||
			    Package.Kind != EPackageKind::Library || !Package.Module)
			{
				return true;
			}
			// The loader already imports the library's declarations. Only its public
			// macros can introduce hidden GUI syntax in the caller before parsing.
			for (const auto& Macro : Package.Module->PublicMacros)
			{
				if (!Macro.Parameters.empty() || Macro.bVariadic || MacroNeedsGui(Macro.Body, true)) return true;
			}
			return false;
		}

		class FSourceImports final : public ICompileProcessor
		{
		public:
			void Process(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output) override
			{
				if (!Context.Source || IsGuiScriptLibraryPath(Context.ModuleId) ||
				    PathUtil::FromUtf8(Context.ModuleId).extension() == ".vswml")
				{
					return; // ML's generated source imports its framework itself.
				}
				if (Context.CheckCancelled)
				{
					Context.CheckCancelled();
				}
				VeroScriptLexer Lexer(Context.Source->Text, Context.ModuleId);
				const auto& Tokens = Lexer.Tokenize();
				// Inspect macro bodies as well as ordinary syntax. Identifier pasting,
				// stringification and unknown directives keep the conservative fallback.
				// Compiler imports have already been masked by the source bootstrap.
				// Inspect its retained input so source-enabled generators keep the same fallback.
				bool Needed = Lexer.HasError() || std::any_of(Context.SourceMetadata.Documents.begin(), Context.SourceMetadata.Documents.end(), [&](const auto& Document)
				{
					return Document.ModuleId == Context.ModuleId && HasScriptCompileExtensionImport(Document.Text);
				});
				std::uint32_t MacroLine = 0;
				for (std::size_t Index = 0; Index < Tokens.size() && !Needed; ++Index)
				{
					const auto& Token = Tokens[Index];
					if (DirectiveToken(Tokens, Index) &&
					    (Tokens[Index + 1].Lexeme == "define" || Tokens[Index + 1].Lexeme == "define_export"))
					{
						MacroLine = Token.Location.Line;
						Needed = MacroDefinitionNeedsGui(Tokens, Index);
					}
					const bool MacroBody = Token.Location.Line == MacroLine;
					Needed = Needed || GuiToken(Tokens, Index, MacroBody) ||
					    (MacroBody && (Token.Lexeme == "namespace" || Token.Lexeme == "::" || Token.Lexeme == "."));
					const bool Include = DirectiveToken(Tokens, Index) && Tokens[Index + 1].Lexeme == "include";
					if ((Token.Lexeme == "import" && Token.Kind == ETokenKind::Keyword) || Include)
					{
						// #include is the export-import spelling alias. Binary/logical
						// imports can publish macros without visiting a source module.
						const auto PathIndex = Index + (Include ? 2 : 1);
						const auto Path = PathIndex < Tokens.size() ? Tokens[PathIndex].Lexeme : std::string_view{};
						Needed = Needed || ((Path.size() < 4 || Path.substr(Path.size() - 4) != ".vs\"") && BinaryImportNeedsGui(Context, Path));
					}
				}
				if (Needed)
				{
					FSourceRange Range;
					Range.SourceId = Context.ModuleId;
					Range.Begin = Range.End = Context.Source->Text.size();
					// Re-export also covers consumers of a provider's GUI-valued public macros.
					// Append before preprocessing, preserving the original bytes and diagnostic positions.
					Output.EditSource(Range, "\n\nexport import " + GuiQuote(GetGuiScriptLibraryPath()) + ";\n");
				}
			}
		};
	} // namespace

	bool RemoveGuiSourceImports(FCompileProcessingOptions& Options, std::string& Error)
	{
		if (!Options.Registry) return true;
		const auto& Processors = Options.Registry->GetProcessors();
		const auto Found = std::find_if(Processors.begin(), Processors.end(),
		    [](const auto& Processor)
		{
			return Processor.ProcessorId == ProcessorId;
		});
		if (Found == Processors.end()) return true;
		if (!Options.Registry->IsFrozen() || Found->Version != Version || Found->ConfigurationKey != ConfigurationKey())
		{
			Error = "gui.source_import_configuration_conflict";
			return false;
		}
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		for (const auto& Processor : Processors)
		{
			if (Processor.ProcessorId == ProcessorId)
			{
				continue;
			}
			if (Processor.ProcessorId == "veroscript.gui.markup")
			{
				const std::string Suffix = ":source-imports";
				if (Processor.Version != Version || Processor.ConfigurationKey.size() < Suffix.size() ||
				    Processor.ConfigurationKey.substr(Processor.ConfigurationKey.size() - Suffix.size()) != Suffix)
				{
					Error = "gui.source_import_configuration_conflict";
					return false;
				}
				continue; // ConfigureGuiCompilation restores the default ML processor.
			}
			if (!Registry->Register(Processor, Error)) return false;
		}
		Registry->Freeze();
		Options.Registry = Registry->GetProcessors().empty() ? nullptr : std::move(Registry);
		return true;
	}

	bool ConfigureGuiSourceImports(FEngineConfig& Config, const FGuiCompilationConfiguration& Compilation,
	    std::string& Error)
	{
		if ((!Config.Defines.empty() || !Config.PredefinedMacros.empty()) &&
		    (std::any_of(Config.ImplicitImportPaths.begin(), Config.ImplicitImportPaths.end(),
		         [](const auto Path)
		{
			return !PathUtil::Equal(std::string(Path), GetGuiScriptLibraryPath());
		}) ||
		        (Config.ModuleEnvironment && !Config.ModuleEnvironment->GetDescription().ImplicitImports.empty())))
		{
			// Opaque implicit providers may supply aliases used by host macros.
			return true;
		}
		// Unknown generators may introduce GUI after the source callback. Host
		// macros with GUI syntax or token construction also keep the full import.
		if (std::any_of(Config.Defines.begin(), Config.Defines.end(), [](std::string_view Define)
		{
			const auto Equal = Define.find('=');
			return MacroNeedsGui(Define) ||
			    (Equal != std::string_view::npos && MacroNeedsGui(Define.substr(Equal + 1), true));
		}) ||
		    std::any_of(Config.PredefinedMacros.begin(), Config.PredefinedMacros.end(), [](const auto& Macro)
		{
			return Macro.Name == "Gui" || !Macro.Parameters.empty() ||
			    Macro.bVariadic || MacroNeedsGui(Macro.Body, true);
		}) ||
		    Config.AnnotationProcessing.Registry || !Config.ScriptCompileExtensions.Roots.empty())
		{
			return true;
		}
		const auto Compatible = [](const FCompileProcessingOptions& Processing)
		{
			if (!Processing.Registry) return true;
			return std::all_of(Processing.Registry->GetProcessors().begin(), Processing.Registry->GetProcessors().end(),
			    [](const auto& Processor)
			{
				return Processor.ProcessorId == "veroscript.gui.declarations" ||
				    Processor.ProcessorId == "veroscript.gui.markup";
			});
		};
		if (!Compatible(Config.CompileProcessing)) return true;
		if (Config.ModuleEnvironment)
		{
			const auto& Description = Config.ModuleEnvironment->GetDescription();
			if (!Compatible(Description.CompileProcessing) || !Description.ScriptCompileExtensions.Roots.empty()) return true;
		}
		auto Next = Config;
		auto Description = Next.ModuleEnvironment ? Next.ModuleEnvironment->GetDescription() : FModuleEnvironmentDesc{};
		auto& Processing = Next.ModuleEnvironment ? Description.CompileProcessing : Next.CompileProcessing;
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		if (Processing.Registry)
		{
			for (auto Processor : Processing.Registry->GetProcessors())
			{
				if (Processor.ProcessorId == "veroscript.gui.markup")
				{
					Processor = MakeGuiMarkupProcessor(std::make_shared<FGuiCompilationConfiguration>(Compilation),
					    Processor.ConfigurationKey, true);
				}
				if (!Registry->Register(Processor, Error)) return false;
			}
		}
		FCompileProcessorDesc Processor;
		Processor.ProcessorId = ProcessorId;
		Processor.Version = Version;
		Processor.ConfigurationKey = ConfigurationKey();
		Processor.Phases = CompilePhaseMask(ECompileProcessingPhase::RawSource);
		Processor.Operations = CompileOperationMask(ECompileOperation::EditSource);
		Processor.Subscriptions.push_back({ECompileProcessingEvent::Module});
		Processor.Factory = []
		{
			return std::make_unique<FSourceImports>();
		};
		if (!Registry->Register(std::move(Processor), Error)) return false;
		Registry->Freeze();
		Processing.Registry = std::move(Registry);
		if (Next.ModuleEnvironment)
		{
			Next.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
			if (!Next.ModuleEnvironment) return false;
		}
		auto& Imports = Next.ImplicitImportPaths;
		Imports.erase(std::remove_if(Imports.begin(), Imports.end(),
		                  [](const auto Path)
		{
			return PathUtil::Equal(std::string(Path), GetGuiScriptLibraryPath());
		}),
		    Imports.end());
		Config = std::move(Next);
		Error.clear();
		return true;
	}
} // namespace VeroScript::Gui

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiMarkup.h"
#include "Compilation/VeroScriptGuiMarkupInternal.h"
#include <exception>
#include <filesystem>
#include <fstream>

namespace VeroScript::Gui
{
	namespace
	{
		std::filesystem::path Utf8Path(const std::string& Value)
		{
			#if defined(__cpp_lib_char8_t)
			return std::filesystem::path(std::u8string(Value.begin(), Value.end()));
			#else
			return std::filesystem::u8path(Value);
			#endif
		}
	} // namespace

	std::string FMarkupDiagnostic::Format() const
	{
		if (Message.empty())
		{
			return {};
		}
		return Location.Source + ":" + std::to_string(Location.Line) + ":" + std::to_string(Location.Column) + ": " +
		    Message;
	}

	std::shared_ptr<const FStyleSheet> FStyleSheet::Compile(const FMarkupSource& Source, FMarkupDiagnostic& Diagnostic,
	    FResourceResolver ResolveResource)
	{
		Diagnostic = {};
		try
		{
			auto Result = std::make_shared<FStyleSheetData>();
			if (!ParseStyleSheet(Source, *Result, Diagnostic))
			{
				return {};
			}
			for (auto& Rule : Result->Rules)
			{
				for (auto& Declaration : Rule.Declarations)
				{
					Declaration.ResolveResource = ResolveResource;
				}
			}
			return std::shared_ptr<const FStyleSheet>(new FStyleSheet(std::move(Result)));
		}
		catch (const std::exception& Exception)
		{
			MarkupFail(Diagnostic, MarkupLocation(Source, 0), std::string("gui.style_exception: ") + Exception.what());
		}
		catch (...)
		{
			MarkupFail(Diagnostic, MarkupLocation(Source, 0), "gui.style_exception");
		}
		return {};
	}

	std::shared_ptr<const FStyleSheet> FStyleSheet::CompileTyped(const FMarkupSource& Source,
	    FMarkupDiagnostic& Diagnostic,
	    FResourceResolver ResolveResource)
	{
		Diagnostic = {};
		try
		{
			auto Data = std::make_shared<FStyleSheetData>();
			if (!ParseStyleSheet(Source, *Data, Diagnostic, false))
			{
				return {};
			}
			for (auto& Rule : Data->Rules)
			{
				for (auto& Declaration : Rule.Declarations)
				{
					Declaration.ResolveResource = ResolveResource;
				}
			}
			return std::shared_ptr<const FStyleSheet>(new FStyleSheet(std::move(Data)));
		}
		catch (const std::exception& Exception)
		{
			MarkupFail(Diagnostic, MarkupLocation(Source, 0), std::string("gui.style_exception: ") + Exception.what());
		}
		catch (...)
		{
			MarkupFail(Diagnostic, MarkupLocation(Source, 0), "gui.style_exception");
		}
		return {};
	}

	FStyleSheet::FStyleSheet(std::shared_ptr<const FStyleSheetData> InData) : Data(std::move(InData))
	{
	}

	std::shared_ptr<const FMarkupProgram> FMarkupProgram::Compile(const FMarkupSource& Source,
	    const FMarkupCompileOptions& Options,
	    FMarkupDiagnostic& Diagnostic)
	{
		Diagnostic = {};
		try
		{
			if (!Options.MaxDepth || Options.MaxDepth > 256 || !Options.MaxNodes || Options.MaxNodes > 100000 ||
			    !Options.MaxSourceBytes || !Options.MaxDependencies)
			{
				MarkupFail(Diagnostic, MarkupLocation(Source, 0), "gui.invalid_compile_limits");
				return {};
			}
			auto Result = std::make_shared<FMarkupData>();
			Result->Entry = Source.Identity;
			Result->Options = Options;
			Result->Dependencies.push_back(Source);
			Result->Types.emplace("Widget", FMarkupType{"Widget", "", "", []
			{
				return FWidget::Create();
			}});
			for (const auto& Type : Options.Types)
			{
				if (!Type.Create || !IsMarkupName(Type.Name) || (!Type.Mark.empty() && !IsMarkupName(Type.Mark)))
				{
					MarkupFail(Diagnostic, MarkupLocation(Source, 0), "gui.invalid_widget_registration");
					return {};
				}
				std::set<std::string> RegisteredNames;
				for (const auto& Name : std::vector<std::string>{Type.Name, Type.Mark})
				{
					if (Name.empty() || !RegisteredNames.insert(Name).second)
					{
						continue;
					}
					if (Name == "component" || Name == "script" || Name == "style" || Name == "template" ||
					    Name == "import" || !Result->Types.emplace(Name, Type).second)
					{
						MarkupFail(Diagnostic, MarkupLocation(Source, 0), "gui.widget_name_conflict: " + Name);
						return {};
					}
				}
			}
			std::set<std::string> Visiting;
			if (!CompileMarkupUnit(*Result, Source, Visiting, Diagnostic) || !ValidateMarkupGraph(*Result, Diagnostic))
			{
				return {};
			}
			// Resource/cancellation callbacks belong to this compile, not to future instances.
			Result->Options.Read = {};
			Result->Options.CheckCancelled = {};
			return std::shared_ptr<const FMarkupProgram>(new FMarkupProgram(std::move(Result)));
		}
		catch (const std::exception& Exception)
		{
			MarkupFail(Diagnostic, MarkupLocation(Source, 0),
			    std::string("gui.compile_exception: ") + Exception.what());
		}
		catch (...)
		{
			MarkupFail(Diagnostic, MarkupLocation(Source, 0), "gui.compile_exception");
		}
		return {};
	}

	const std::vector<FMarkupSource>& FMarkupProgram::GetDependencies() const
	{
		return Data->Dependencies;
	}

	std::shared_ptr<FWidget> FMarkupProgram::Create(const FMarkupScriptFactory& Scripts, FMarkupDiagnostic& Diagnostic,
	    std::shared_ptr<IMarkupScriptContext> BindingContext) const
	{
		Diagnostic = {};
		try
		{
			return CreateMarkup(Data, Scripts, std::move(BindingContext), Diagnostic);
		}
		catch (const std::exception& Exception)
		{
			MarkupFail(Diagnostic, {Data->Entry}, std::string("gui.create_exception: ") + Exception.what());
		}
		catch (...)
		{
			MarkupFail(Diagnostic, {Data->Entry}, "gui.create_exception");
		}
		return {};
	}

	std::shared_ptr<FWidget> FMarkupProgram::Find(const std::shared_ptr<FWidget>& ScopeRoot, const std::string& Id)
	{
		return ScopeRoot && !ScopeRoot->IsDisposed() && ScopeRoot->Markup ? ScopeRoot->Markup->Find(ScopeRoot, Id)
		                                                                  : nullptr;
	}

	bool FMarkupProgram::SetTheme(const std::shared_ptr<FWidget>& Root, std::shared_ptr<const FStyleSheet> Theme,
	    std::string& Error)
	{
		Error.clear();
		if (!Root || !Root->Check(Error, true) || !Root->Markup)
		{
			if (Error.empty())
			{
				Error = "gui.markup_root_required";
			}
			return false;
		}
		Root->Markup->Theme = Theme ? Theme->Data : nullptr;
		Root->Markup->LastContext.reset();
		Root->MarkDirty();
		return true;
	}

	FMarkupProgram::FMarkupProgram(std::shared_ptr<const FMarkupData> InData) : Data(std::move(InData))
	{
	}

	bool ReadMarkupFile(const std::string& Requester, const std::string& Reference, FMarkupSource& Source,
	    std::string& Error)
	{
		Error.clear();
		Source = {};
		try
		{
			auto Path = Utf8Path(Reference);
			if (Path.is_relative() && !Requester.empty())
			{
				Path = Utf8Path(Requester).parent_path() / Path;
			}
			Path = std::filesystem::absolute(Path).lexically_normal();
			const auto EncodedPath = Path.generic_u8string();
			const std::string Identity(EncodedPath.begin(), EncodedPath.end());
			const auto Size = std::filesystem::file_size(Path);
			if (Size > 16 * 1024 * 1024)
			{
				Error = "gui.source_limit: " + Identity;
				return false;
			}
			std::ifstream Stream(Path, std::ios::binary);
			std::string Text(static_cast<std::size_t>(Size), '\0');
			if (!Stream || !Stream.read(Text.data(), static_cast<std::streamsize>(Text.size())))
			{
				Error = "gui.resource_read_failed: " + Identity;
				return false;
			}
			Source = {Identity, std::move(Text)};
			return true;
		}
		catch (const std::exception& Exception)
		{
			Error = std::string("gui.resource_read_failed: ") + Exception.what();
			return false;
		}
	}
} // namespace VeroScript::Gui

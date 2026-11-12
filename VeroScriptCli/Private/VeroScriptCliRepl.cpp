// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCliRepl.h"

#include "VeroScriptEngine.h"
#include "VeroScriptHost.h"
#include "VeroScriptVersion.h"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace VeroScript::Cli
{
	namespace
	{
		bool IsReplCommand(const std::string_view Line)
		{
			return Line == ":quit" || Line == ":exit" || Line == ":q";
		}

		int CountBraceDelta(const std::string_view Source)
		{
			int Depth = 0;
			bool bInString = false;
			bool bInLineComment = false;
			bool bInBlockComment = false;

			for (std::size_t Index = 0; Index < Source.size(); ++Index)
			{
				const char Character = Source[Index];
				const char Next = Index + 1 < Source.size() ? Source[Index + 1] : '\0';

				if (bInLineComment)
				{
					if (Character == '\n')
					{
						bInLineComment = false;
					}
					continue;
				}

				if (bInBlockComment)
				{
					if (Character == '*' && Next == '/')
					{
						bInBlockComment = false;
						++Index;
					}
					continue;
				}

				if (bInString)
				{
					if (Character == '\\' && Next != '\0')
					{
						++Index;
						continue;
					}
					if (Character == '"')
					{
						bInString = false;
					}
					continue;
				}

				if (Character == '/' && Next == '/')
				{
					bInLineComment = true;
					++Index;
					continue;
				}
				if (Character == '/' && Next == '*')
				{
					bInBlockComment = true;
					++Index;
					continue;
				}
				if (Character == '"')
				{
					bInString = true;
					continue;
				}
				if (Character == '{')
				{
					++Depth;
				}
				else if (Character == '}')
				{
					--Depth;
				}
			}

			return Depth;
		}

		bool IsBlockComplete(const std::string_view Source)
		{
			return CountBraceDelta(Source) <= 0;
		}

		std::string TrimCopy(std::string Value)
		{
			while (!Value.empty() && (Value.front() == ' ' || Value.front() == '\t'))
			{
				Value.erase(Value.begin());
			}
			while (!Value.empty() && (Value.back() == ' ' || Value.back() == '\t'))
			{
				Value.pop_back();
			}
			return Value;
		}
	}

	int RunRepl(const std::vector<std::string_view>& ImportPaths, const bool bStrictExit,
				const std::vector<std::string_view>& Defines,
				const std::vector<std::string_view>& StdlibSearchPaths,
				const EExecutionBackend ExecutionBackend,
				const std::vector<FPredefinedMacro>& PredefinedMacros,
				const std::vector<std::string_view>& ImplicitImportPaths, const bool bAllowCircularImports,
				const bool bAllowProjectWideAccess,
				std::shared_ptr<const IJitBackendFactory> JitBackendFactory,
				const FScriptCompileExtensionsOptions& ScriptExtensions,
				std::shared_ptr<const FModuleEnvironment> ModuleEnvironment, std::shared_ptr<const IVmBackendFactory> VmBackendFactory, std::string VmBackendPath)
	{
		std::cout
			<< "VeroScript REPL (" << GetVersionString() << ")\n"
			<< "  :quit / :exit / :q  exit\n"
			<< "  Multi-line blocks: close all '{' before run\n"
			<< "  Redeclare globals with int/name twice is an error; use assignment instead\n\n";

		std::string Session;
		std::string PendingBlock;
		int LastExitCode = 0;
		bool bLastRunFailed = false;

		while (true)
		{
			std::cout << (PendingBlock.empty() ? "> " : "... ");
			std::string Line;
			if (!std::getline(std::cin, Line))
			{
				break;
			}

			const std::string TrimmedLine = TrimCopy(std::move(Line));
			if (TrimmedLine.empty())
			{
				continue;
			}

			if (PendingBlock.empty() && IsReplCommand(TrimmedLine))
			{
				break;
			}

			PendingBlock += TrimmedLine;
			PendingBlock += '\n';

			if (!IsBlockComplete(PendingBlock))
			{
				continue;
			}

			const std::size_t SessionSizeBefore = Session.size();
			Session += PendingBlock;
			PendingBlock.clear();

			std::ostringstream Output;
			std::ostringstream Errors;
			FEngineConfig Config;
			Config.ImportPaths = ImportPaths;
			Config.Defines = Defines;
			Config.PredefinedMacros = PredefinedMacros;
			Config.StdlibSearchPaths = StdlibSearchPaths;
			Config.ImplicitImportPaths = ImplicitImportPaths;
			Config.bAllowCircularImports = bAllowCircularImports;
			Config.bAllowProjectWideAccess = bAllowProjectWideAccess;
				Config.ScriptCompileExtensions=ScriptExtensions;
				Config.ModuleEnvironment = ModuleEnvironment;
				Config.ExecutionBackend = ExecutionBackend;
				Config.JitBackendFactory = JitBackendFactory;
				Config.VmBackendFactory = VmBackendFactory;
				Config.VmBackendPath = VmBackendFactory ? std::string{} : VmBackendPath;
				FStreamEngineHost Host(Output, Errors);
				const FRunResult Result =
					VeroScriptEngine::RunSource(Session, "<repl>", Host, ERunMode::Eval, Config);

				if (!Errors.str().empty())
				{
					std::cerr << Errors.str();
				}
			if (!Output.str().empty())
			{
				std::cout << Output.str();
				if (Output.str().back() != '\n')
				{
					std::cout << '\n';
				}
			}

			if (!Result.bSuccess)
			{
				Session.resize(SessionSizeBefore);
				LastExitCode = Result.ExitCode;
				bLastRunFailed = true;
			}
			else if (Result.bExitRequested)
			{
				// Intentional script exit(n) — leave the REPL (including exit(0)).
				return Result.ExitCode;
			}
			else
			{
				bLastRunFailed = false;
				LastExitCode = 0;
			}
		}

		if (bStrictExit && bLastRunFailed)
		{
			return LastExitCode;
		}

		return 0;
	}
}

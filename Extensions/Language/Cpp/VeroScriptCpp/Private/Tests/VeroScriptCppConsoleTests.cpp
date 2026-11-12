// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestEngine.h"
#include "VeroScriptEngine.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptSession.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{
	using namespace VeroScript;
	class FConsoleHost final : public IEngineHost
	{
	public:
		//~ Begin IEngineHost Interface
		void OnPrintLine(std::string_view Line) override
		{
			Text.append(Line);
			Text += '\n';
			Lines.emplace_back(Line);
		}
		void OnDiagnosticLine(std::string_view Line) override
		{
			Diagnostics.append(Line);
			Diagnostics += '\n';
		}
		Console::FInfo QueryConsole() override
		{
			if (bThrowQuery)
			{
				throw std::runtime_error("console query failure");
			}

			return {true, true, true, true, 80, 25};
		}
		bool ExecuteConsole(Console::EOperation Operation, std::int32_t First, std::int32_t Second) override
		{
			Commands.emplace_back(Operation, First, Second);
			if (bThrowReset && Operation == Console::EOperation::ResetStyle)
			{
				throw std::runtime_error("console reset failure");
			}

			return true;
		}
		bool HasConsoleTextOutput() const override
		{
			return true;
		}
		bool WriteConsoleText(std::string_view Value) override
		{
			if (bThrowText)
			{
				throw std::runtime_error("console text failure");
			}

			Text.append(Value);
			return !bFailText;
		}
		bool FlushConsoleText() override
		{
			return true;
		}
		//~ End IEngineHost Interface
		std::string Text, Diagnostics;
		std::vector<std::string> Lines;
		std::vector<std::tuple<Console::EOperation, int, int>> Commands;
		bool bThrowQuery = false, bThrowReset = false, bFailText = false, bThrowText = false;
	};
	int Failures = 0;
	void Expect(bool Condition, const char* Label)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Label << '\n';
			++Failures;
		}
	}
	bool Restored(const FConsoleHost& Host)
	{
		return Host.Commands.size() >= 4
			&& std::get<0>(Host.Commands[Host.Commands.size()-2]) == Console::EOperation::ResetStyle
			&& Host.Commands.back() == std::make_tuple(Console::EOperation::SetCursorVisible, 1, 0);
	}
	void TestBackend(EExecutionBackend Backend, const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.ExecutionBackend = Backend;
		Config.JitBackendFactory = Factory;
		Config.bUseBytecodeCache = false;
		FConsoleHost Host;
		Host.bThrowQuery = true;
		const auto QueryFailure = VeroScript::Test::FEngine::RunSource(
			"void main(){assert(!std::console::supports_color());assert(std::console::width()==0);}",
			"console-query.vs", Host, ERunMode::Script, Config);
		Expect(QueryFailure.bSuccess && Host.Commands.empty(),
			"console query callback failures safely report unavailable capabilities");
		Host.bThrowQuery = false;
		Host.Diagnostics.clear();
		const auto Successful = VeroScript::Test::FEngine::RunSource(R"VS(void main(){
            assert(std::console::supports_color() && std::console::supports_cursor());
            assert(std::console::supports_clear() && std::console::is_output_terminal());
            assert(std::console::width()==80 && std::console::height()==25);
            assert(std::console::set_color("bright_red","default"));
            assert(std::console::set_cursor_visible(false));
            assert(std::console::move_cursor(5,3));
            assert(!std::console::move_cursor(80,25));
            assert(std::console::clear_line("all") && std::console::clear());
            std::console::write("A"); print("B"); std::console::write("C"); std::console::flush();
        })VS", "console-host.vs", Host, ERunMode::Script, Config);
		Expect(Successful.bSuccess && Host.Text == "AB\nC" && Host.Lines == std::vector<std::string>{"B"},
			"exact console text preserves print callback grouping and output order");
		Expect(Host.Commands.size() == 7 && Restored(Host), "normal execution restores host color and cursor");
		Host.bThrowText = Host.bThrowReset = true;
		Host.Commands.clear();
		Expect(!VeroScript::Test::FEngine::RunSource("void main(){std::console::set_color(\"red\",\"default\");"
			"std::console::set_cursor_visible(false);std::console::write(\"x\");}", "console-callback-failure.vs", Host,
			ERunMode::Script, Config).bSuccess && Host.Diagnostics.find("std::console::write")!=std::string::npos
			&& Host.Commands.size()==4 && Restored(Host), "text callback exceptions preserve independent cursor cleanup");
		Host.bThrowText = Host.bThrowReset = false;
		Config.Capabilities.bAllowConsoleOutput = false;
		Host.Commands.clear();
		Expect(!VeroScript::Test::FEngine::RunSource("void main(){std::console::set_color(\"red\",\"default\");}",
			"console-denied.vs", Host, ERunMode::Script, Config).bSuccess && Host.Commands.empty(),
			"console permissions reject control before host side effects");
		Config.Capabilities.bAllowConsoleOutput = true;
		Host.bThrowReset = true;
		Host.Commands.clear();
		Expect(!VeroScript::Test::FEngine::RunSource("void main(){std::console::set_color(\"red\",\"default\");"
			"std::console::set_cursor_visible(false);assert(false);}", "console-failure.vs", Host,
			ERunMode::Script, Config).bSuccess && Restored(Host), "failure cleanup survives host reset exceptions");
		Host.bThrowReset = false;
		Host.Commands.clear();
		Config.ExecutionLimits.MaxInstructions = 60;
		Expect(!VeroScript::Test::FEngine::RunSource("void main(){std::console::set_color(\"red\",\"default\");"
			"std::console::set_cursor_visible(false);while(true){}}", "console-limit.vs", Host,
			ERunMode::Script, Config).bSuccess && Restored(Host), "instruction exhaustion restores host terminal state");
		Config.ExecutionLimits = {};
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.ApplyConfig(Config);
		Expect(Session.LoadSource("int control(){std::console::set_color(\"red\",\"default\");"
			"std::console::set_cursor_visible(false);return std::console::width();}", "console-session.vs").bSuccess,
			"load console Session entry");
		for (int Index = 0; Index < 2; ++Index)
		{
			Host.Commands.clear();
			FValue Return;
			const auto Call = Session.CallFunction("control", {}, &Return);
			Expect(Call.bSuccess && std::get_if<std::int32_t>(&Return) && std::get<std::int32_t>(Return)==80
				&& Host.Commands.size()==4 && Restored(Host), "reused Session calls restore independently");
		}
		Host.bFailText = true;
		Expect(!VeroScript::Test::FEngine::RunSource("void main(){std::console::write(\"x\");}", "console-text-failure.vs",
			Host, ERunMode::Script, Config).bSuccess, "failed exact text delivery is reported");
		std::ostringstream Output, Errors;
		FStreamEngineHost Plain(Output, Errors);
		Expect(VeroScript::Test::FEngine::RunSource(R"VS(void main(){
            assert(!std::console::is_output_terminal() && !std::console::supports_color());
            assert(!std::console::supports_cursor() && !std::console::supports_clear());
            assert(std::console::width()==0 && std::console::height()==0);
            assert(!std::console::clear() && !std::console::set_color("red","default"));
            assert(!std::console::move_cursor(0,0) && !std::console::set_cursor_visible(false));
            std::console::write("plain"); print("text");
        })VS", "console-plain.vs", Plain, ERunMode::Script, Config).bSuccess && Output.str()=="plaintext\n",
			"plain stream hosts preserve text without terminal escape codes");
	}
}
int main(int Argc, char** Argv)
{
	std::shared_ptr<const VeroScript::IJitBackendFactory> Factory;
	if (Argc > 1)
	{
		std::string Error;
		Factory = VeroScript::LoadJitBackend(Argv[1], Error);
		if (!Factory)
		{
			std::cerr << Error << '\n';
			return 1;
		}
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed,
			 VeroScript::EExecutionBackend::Jit})
	{
		if (Backend != VeroScript::EExecutionBackend::Jit || Factory)
		{
			TestBackend(Backend, Factory);
		}
	}

	return Failures ? 1 : 0;
}

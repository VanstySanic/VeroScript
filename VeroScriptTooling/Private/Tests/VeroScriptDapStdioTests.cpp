// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptProtocolTestStreams.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	class FTestLiveDomain final : public VeroScript::Tooling::IVeroScriptLiveDebugDomain
	{
	public:
		//~ Begin VeroScript::Tooling::IVeroScriptLiveDebugDomain Interface
		VeroScript::Tooling::FLiveDebugDomainInfo Info() const override
		{
			VeroScript::Tooling::FLiveDebugDomainInfo Result;
			Result.HostId = "test-host";
			Result.DomainId = "test-domain";
			Result.Name = "test target";
			Result.SourceRevision = "source-2";
			Result.BindingRevision = "bindings-3";
			Result.Sources.push_back({"live.vs", "v2"});
			Result.bCanPause = true;
			return Result;
		}
		bool Connect(FEventHandler Handler, std::string&) override
		{
			if (bConnected) return false;
			Callback = std::move(Handler);
			bConnected = true;
			++ConnectCount;
			return true;
		}
		bool Activate(std::string&) override
		{
			++ActivateCount;
			Stop();
			return true;
		}
		void Disconnect() noexcept override
		{
			++DisconnectCount;
			bStopped = false;
			bContextRunning = false;
			bConnected = false;
			Callback = {};
		}
		VeroScript::Tooling::FDapSetBreakpointsResult SetBreakpoints(
		    std::string_view Path,
		    const std::vector<VeroScript::Tooling::FLiveDebugBreakpoint>& Breakpoints) override
		{
			++BreakpointUpdates;
			ReceivedBreakpointPaths.emplace_back(Path);
			VeroScript::Tooling::FDapSetBreakpointsResult Result;
			for (const auto& Breakpoint : Breakpoints)
			{
				ReceivedBreakpointIds.push_back(Breakpoint.Id);
				Result.Breakpoints.push_back({1, true, Breakpoint.Line, std::string(Path), {}});
			}
			if (bStopDuringBinding)
			{
				Stop();
			}

			return Result;
		}
		VeroScript::Tooling::FDapContinueResult Resume(
		    VeroScript::Tooling::EDebugRunMode Mode) override
		{
			if (bRejectResume || Mode != VeroScript::Tooling::EDebugRunMode::Continue)
			{
				return {false, false};
			}
			bStopped = false;
			++ResumeCount;
			if (ResumeCount == 1)
			{
				Stop();
			}
			else
			{
				bContextRunning = false;
			}
			return {};
		}
		VeroScript::Tooling::FDapContinueResult Pause() override
		{
			return {};
		}
		void RequestCancel() override
		{
			++CancelCount;
		}
		VeroScript::Tooling::FDapStackTraceResult StackTrace() const override
		{
			VeroScript::Tooling::FDapStackTraceResult Result;
			if (bStopped)
			{
				Result.StackFrames.push_back({0, "host call", "live.vs", 2, 1});
			}
			return Result;
		}
		VeroScript::Tooling::FDapScopesResult Scopes(int) const override
		{
			return {};
		}
		VeroScript::Tooling::FDapVariablesResult Variables(int) const override
		{
			return {};
		}
		VeroScript::Tooling::FDapEvaluateResult Evaluate(
		    std::string_view, int, std::string_view) const override
		{
			return {};
		}
		VeroScript::Tooling::FDapSetVariableResult SetVariable(
		    int, std::string_view, std::string_view) override
		{
			return {};
		}
		bool IsStopped() const override
		{
			return bStopped;
		}
		bool IsContextRunning() const override
		{
			return bContextRunning;
		}
		//~ End VeroScript::Tooling::IVeroScriptLiveDebugDomain Interface
		void Stop()
		{
			bStopped = true;
			bContextRunning = true;
			if (Callback)
			{
				VeroScript::Tooling::FDapEvent Event;
				Event.Kind = VeroScript::Tooling::EDapEventKind::Stopped;
				Callback(Event);
			}
		}
		int ConnectCount = 0;
		int ActivateCount = 0;
		int BreakpointUpdates = 0;
		int ResumeCount = 0;
		int DisconnectCount = 0;
		int CancelCount = 0;
		std::vector<int> ReceivedBreakpointIds;
		std::vector<std::string> ReceivedBreakpointPaths;
		bool bConnected = false;
		bool bStopped = false;
		bool bContextRunning = false;
		bool bRejectResume = false;
		bool bStopDuringBinding = false;
		FEventHandler Callback;
	};

	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptDapStdioTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	std::string Frame(const std::string& Body)
	{
		return "Content-Length: " + std::to_string(Body.size()) + "\r\n\r\n" + Body;
	}

	std::string Request(const int Seq, const std::string& Command, const std::string& ArgsJson = "{}")
	{
		return Frame(
		    std::string("{\"seq\":") + std::to_string(Seq) + ",\"type\":\"request\",\"command\":\"" + Command + "\",\"arguments\":" + ArgsJson + "}");
	}

	bool Contains(const std::string& Haystack, const std::string_view Needle)
	{
		return Haystack.find(Needle) != std::string::npos;
	}

	std::string JsonString(const std::string_view Text)
	{
		std::string Result = "\"";
		for (const char Character : Text)
		{
			switch (Character)
			{
			case '\\':
				{
					Result += "\\\\";
					break;
				}
			case '"':
				{
					Result += "\\\"";
					break;
				}
			case '\n':
				{
					Result += "\\n";
					break;
				}
			case '\r':
				{
					Result += "\\r";
					break;
				}
			case '\t':
				{
					Result += "\\t";
					break;
				}
			default:
				{
					Result.push_back(Character);
					break;
				}
			}
		}
		Result.push_back('"');
		return Result;
	}

	// Extract framed DAP message bodies in order.
	std::vector<std::string> ExtractBodies(const std::string& Stream)
	{
		std::vector<std::string> Bodies;
		std::size_t Index = 0;
		while (Index < Stream.size())
		{
			const std::size_t Header = Stream.find("Content-Length:", Index);
			if (Header == std::string::npos)
			{
				break;
			}
			const std::size_t Colon = Header + std::string("Content-Length:").size();
			const std::size_t LineEnd = Stream.find("\r\n", Colon);
			if (LineEnd == std::string::npos)
			{
				break;
			}
			const std::size_t Length = static_cast<std::size_t>(
			    std::stoul(Stream.substr(Colon, LineEnd - Colon)));
			const std::size_t BodyStart = Stream.find("\r\n\r\n", Header);
			if (BodyStart == std::string::npos)
			{
				break;
			}
			const std::size_t Start = BodyStart + 4;
			if (Start + Length > Stream.size())
			{
				break;
			}
			Bodies.push_back(Stream.substr(Start, Length));
			Index = Start + Length;
		}
		return Bodies;
	}

	class FBlockingInputBuffer final : public std::streambuf
	{
	public:
		void Append(const std::string& Text)
		{
			{
				const std::lock_guard<std::mutex> Lock(Mutex);
				Buffer.insert(Buffer.end(), Text.begin(), Text.end());
			}
			Condition.notify_all();
		}

		void Close()
		{
			{
				const std::lock_guard<std::mutex> Lock(Mutex);
				bClosed = true;
			}
			Condition.notify_all();
		}

	protected:
		//~ Begin std::streambuf Interface
		int_type underflow() override
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			Condition.wait(Lock, [this]()
			{
				return !Buffer.empty() || bClosed;
			});
			return Buffer.empty()
			    ? traits_type::eof()
			    : traits_type::to_int_type(Buffer.front());
		}

		int_type uflow() override
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			Condition.wait(Lock, [this]()
			{
				return !Buffer.empty() || bClosed;
			});
			if (Buffer.empty())
			{
				return traits_type::eof();
			}
			const char Value = Buffer.front();
			Buffer.pop_front();
			return traits_type::to_int_type(Value);
		}
		//~ End std::streambuf Interface

	private:
		std::mutex Mutex;
		std::condition_variable Condition;
		std::deque<char> Buffer;
		bool bClosed = false;
	};

	class FBlockingOutputBuffer final : public std::streambuf
	{
	public:
		bool WaitContains(const std::string_view Text, const std::chrono::milliseconds Timeout)
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			return Condition.wait_for(
			    Lock,
			    Timeout,
			    [this, Text]()
			{
				return Output.find(Text) != std::string::npos;
			});
		}

		std::string Str() const
		{
			const std::lock_guard<std::mutex> Lock(Mutex);
			return Output;
		}

	protected:
		//~ Begin std::streambuf Interface
		int_type overflow(const int_type Character) override
		{
			if (traits_type::eq_int_type(Character, traits_type::eof()))
			{
				return traits_type::not_eof(Character);
			}
			{
				const std::lock_guard<std::mutex> Lock(Mutex);
				Output.push_back(traits_type::to_char_type(Character));
			}
			Condition.notify_all();
			return Character;
		}

		std::streamsize xsputn(const char* Text, const std::streamsize Count) override
		{
			{
				const std::lock_guard<std::mutex> Lock(Mutex);
				Output.append(Text, static_cast<std::size_t>(Count));
			}
			Condition.notify_all();
			return Count;
		}
		//~ End std::streambuf Interface

	private:
		mutable std::mutex Mutex;
		std::condition_variable Condition;
		std::string Output;
	};
}

int main()
{
	using namespace VeroScript::Tooling;
	{
		Tests::FInputTieCheckingBuffer Buffer(Request(1, "initialize") + Request(2, "disconnect"));
		std::istream TiedInput(&Buffer);
		std::ostringstream TiedOutput;
		Buffer.Attach(TiedInput);
		TiedInput.tie(&TiedOutput);
		const auto Exit = VeroScript::Test::FDapStdio::Run(TiedInput, TiedOutput);
		if (!Expect(Exit == 0 && !Buffer.ObservedTieDuringRead() && TiedInput.tie() == &TiedOutput,
		        "DAP protocol reads cannot implicitly flush output and restore the caller's stream tie"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		std::istringstream FailedLaunchInput(Request(1, "initialize") + Request(2, "launch", "{\"source\":\"void main( {\",\"fileLabel\":\"invalid-target.vs\"}") + Request(3, "disconnect"));
		std::ostringstream FailedLaunchOutput;
		const auto Exit = VeroScript::Test::FDapStdio::Run(FailedLaunchInput, FailedLaunchOutput);
		bool bLaunchRejected = false;
		for (const auto& Body : ExtractBodies(FailedLaunchOutput.str()))
		{
			bLaunchRejected |=
			    Contains(Body, "\"command\":\"launch\"") && Contains(Body, "\"success\":false");
		}

		if (!Expect(Exit == VeroScript::Test::FDapStdio::ExitOk && bLaunchRejected,
		        "default disconnect after a failed launch cannot request host termination"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* Source =
	    "void main() {\\n"
	    "    int x = 1;\\n"
	    "    int y = 2;\\n"
	    "    print(x + y);\\n"
	    "}\\n";

	FBlockingInputBuffer InputBuffer;
	FBlockingOutputBuffer OutputBuffer;
	std::istream In(&InputBuffer);
	std::ostream Out(&OutputBuffer);
	InputBuffer.Append(Request(1, "initialize"));
	InputBuffer.Append(Request(
	    2,
	    "launch",
	    // Debugging must remain usable without a loaded JIT module, even when
	    // the client requests Jit. Exercise breakpoints and paused-state access.
	    std::string("{\"source\":\"") + Source + "\",\"fileLabel\":\"debug.vs\",\"backend\":\"jit\",\"stopOnEntry\":false}"));
	InputBuffer.Append(Request(
	    3,
	    "setBreakpoints",
	    "{\"source\":{\"path\":\"debug.vs\"},\"breakpoints\":[{\"line\":4,\"condition\":\"x == 1\"}]}"));
	InputBuffer.Append(Request(4, "configurationDone"));
	int ExitCode = -1;
	std::thread Host([&]()
	{
		ExitCode = VeroScript::Test::FDapStdio::Run(In, Out);
	});
	if (!Expect(
	        OutputBuffer.WaitContains("\"event\":\"stopped\"", std::chrono::seconds(5)),
	        "stopped event arrived while protocol thread remained responsive"))
	{
		InputBuffer.Close();
		Host.join();
		return EXIT_FAILURE;
	}
	// While stopped the host will read these:
	InputBuffer.Append(Request(5, "threads"));
	InputBuffer.Append(Request(6, "stackTrace", "{\"threadId\":1}"));
	InputBuffer.Append(Request(7, "scopes", "{\"frameId\":0}"));
	InputBuffer.Append(Request(8, "variables", "{\"variablesReference\":1}"));
	InputBuffer.Append(Request(9, "evaluate", "{\"expression\":\"x + 1\",\"frameId\":0}"));
	InputBuffer.Append(Request(
	    10,
	    "setVariable",
	    "{\"variablesReference\":1,\"name\":\"x\",\"value\":\"5\"}"));
	InputBuffer.Append(Request(11, "continue", "{\"threadId\":1}"));
	if (!Expect(
	        OutputBuffer.WaitContains("\"event\":\"terminated\"", std::chrono::seconds(5)),
	        "continued debuggee terminated"))
	{
		InputBuffer.Close();
		Host.join();
		return EXIT_FAILURE;
	}
	// Keep the in-process test host alive: launch sessions otherwise default
	// terminateDebuggee to true and intentionally return ExitTerminateHost.
	InputBuffer.Append(Request(12, "disconnect", "{\"terminateDebuggee\":false}"));
	InputBuffer.Close();
	Host.join();
	if (!Expect(ExitCode == 0, "stdio host should exit 0"))
	{
		return EXIT_FAILURE;
	}

	const std::string Output = OutputBuffer.Str();
	const std::vector<std::string> Bodies = ExtractBodies(Output);
	if (!Expect(!Bodies.empty(), "should emit framed messages"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(Contains(Output, "\"event\":\"initialized\""), "initialized event"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Contains(Output, "\"event\":\"stopped\""), "stopped event"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Contains(Output, "\"reason\":\"breakpoint\""), "breakpoint reason"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        Contains(Output, "\"supportsEvaluateRequest\":true") && Contains(Output, "\"supportsConditionalBreakpoints\":true") && Contains(Output, "\"supportsSetVariable\":true") && Contains(Output, "\"name\":\"veroscript-dap\"") && Contains(Output, "\"platform\":\"") && Contains(Output, "\"jitCapability\":\"backend-services\""),
	        "initialize evaluate/setVariable/conditional caps + platform + core JIT service contract"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Contains(Output, "\"name\":\"x\"") && Contains(Output, "\"value\":\"1\""), "local x"))
	{
		std::cerr << Output << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Contains(Output, "\"command\":\"evaluate\"") && Contains(Output, "\"result\":\"2\""), "evaluate"))
	{
		std::cerr << Output << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Contains(Output, "\"command\":\"setVariable\"") && Contains(Output, "\"value\":\"5\""), "setVariable"))
	{
		std::cerr << Output << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Contains(Output, "\"event\":\"output\"") && Contains(Output, "7"), "stdout after setVariable"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Contains(Output, "\"event\":\"exited\""), "exited event"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Contains(Output, "\"event\":\"terminated\""), "terminated event"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        Contains(Bodies.front(), "\"type\":\"response\"") && Contains(Bodies.front(), "\"command\":\"initialize\"") && Contains(Bodies.front(), "\"veroscriptSupportsDomainAttach\":false"),
	        "initialize response first; no domain bridge means no domain attach support"))
	{
		return EXIT_FAILURE;
	}

	{
		// Launch context is part of the debug contract: argv, environment, cwd and
		// script stdin must reach the VM without consuming the DAP protocol stream.
		const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path Root = std::filesystem::temp_directory_path() / ("veroscript-dap-context-" + std::to_string(Unique));
		std::error_code FileError;
		std::filesystem::create_directories(Root, FileError);
		const std::optional<std::string> PreviousEnvironment =
		    VeroScript::Environment::GetUtf8("VERO_DAP_CONTEXT_TEST");
		const std::filesystem::path PreviousDirectory = std::filesystem::current_path();
		const std::string ContextSource =
		    "import \"io.vs\";\n"
		    "int main(string[] args) {\n"
		    "    print(args[0]);\n"
		    "    print(std::environment::getenv(\"VERO_DAP_CONTEXT_TEST\"));\n"
		    "    print(std::filesystem::cwd());\n"
		    "    print(std::io::read_line());\n"
		    "    return 0;\n"
		    "}\n";
		std::ostringstream ContextInput;
		ContextInput << Request(1, "initialize");
		ContextInput << Request(
		    2,
		    "launch",
		    std::string("{\"source\":") + JsonString(ContextSource) + ",\"fileLabel\":\"context.vs\",\"args\":[\"argument-ok\"]," + "\"env\":[{\"name\":\"VERO_DAP_CONTEXT_TEST\",\"value\":\"environment-ok\"}]," + "\"cwd\":" + JsonString(Root.generic_string()) + "," + "\"input\":\"input-ok\\n\",\"stopOnEntry\":false}");
		ContextInput << Request(3, "configurationDone");
		ContextInput << Request(4, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream ContextIn(ContextInput.str());
		std::ostringstream ContextOut;
		const int ContextExit = VeroScript::Test::FDapStdio::Run(ContextIn, ContextOut);
		const std::string ContextOutput = ContextOut.str();
		const std::optional<std::string> RestoredEnvironment =
		    VeroScript::Environment::GetUtf8("VERO_DAP_CONTEXT_TEST");
		std::filesystem::remove_all(Root, FileError);
		if (!Expect(ContextExit == 0, "launch context DAP session shuts down") || !Expect(Contains(ContextOutput, "argument-ok"), "DAP passes main arguments") || !Expect(Contains(ContextOutput, "environment-ok"), "DAP applies launch environment") || !Expect(Contains(ContextOutput, Root.generic_string()), "DAP applies launch cwd") || !Expect(Contains(ContextOutput, "input-ok"), "DAP uses independent script input") || !Expect(RestoredEnvironment == PreviousEnvironment, "DAP restores the inherited process environment") || !Expect(std::filesystem::current_path() == PreviousDirectory, "DAP restores the inherited process working directory"))
		{
			std::cerr << ContextOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// The protocol thread must stay responsive while the VM is executing.
		FBlockingInputBuffer PauseInputBuffer;
		FBlockingOutputBuffer PauseOutputBuffer;
		std::istream PauseIn(&PauseInputBuffer);
		std::ostream PauseOut(&PauseOutputBuffer);
		PauseInputBuffer.Append(Request(1, "initialize"));
		PauseInputBuffer.Append(Request(
		    2,
		    "launch",
		    "{\"source\":\"void main() { while (true) {} }\\n\","
		    "\"fileLabel\":\"pause.vs\",\"stopOnEntry\":false}"));
		PauseInputBuffer.Append(Request(3, "configurationDone"));
		PauseInputBuffer.Append(Request(4, "pause", "{\"threadId\":1}"));
		int PauseExit = -1;
		std::thread PauseHost(
		    [&]()
		{
			PauseExit = VeroScript::Test::FDapStdio::Run(PauseIn, PauseOut);
		});
		if (!Expect(
		        PauseOutputBuffer.WaitContains("\"reason\":\"pause\"", std::chrono::seconds(5)),
		        "pause interrupts a running VM"))
		{
			PauseInputBuffer.Close();
			PauseHost.join();
			return EXIT_FAILURE;
		}
		PauseInputBuffer.Append(Request(5, "terminate"));
		PauseInputBuffer.Close();
		PauseHost.join();
		const std::string PauseOutput = PauseOutputBuffer.Str();
		if (!Expect(PauseExit == VeroScript::Test::FDapStdio::ExitTerminateHost, "terminate exits DAP host") || !Expect(Contains(PauseOutput, "\"command\":\"pause\"") && Contains(PauseOutput, "\"success\":true"), "pause request succeeds") || !Expect(Contains(PauseOutput, "\"event\":\"terminated\""), "terminated event emitted"))
		{
			std::cerr << PauseOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Relative standard-library roots are resolved from the workspace/cwd and
		// remain distinct from ordinary imports in the compiler module loader.
		const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path Root = std::filesystem::temp_directory_path() / ("veroscript-dap-stdlib-" + std::to_string(Unique));
		const std::filesystem::path Stdlib = Root / "stdlib";
		std::error_code FileError;
		std::filesystem::create_directories(Stdlib, FileError);
		{
			std::ofstream Module(Stdlib / "custom.vs", std::ios::binary | std::ios::trunc);
			Module << "export int custom_value() { return 91; }\n";
		}
		const std::string SourceText =
		    "import \"custom.vs\";\nvoid main() { print(custom_value()); }\n";
		std::ostringstream StdlibInput;
		StdlibInput << Request(1, "initialize");
		StdlibInput << Request(
		    2,
		    "launch",
		    std::string("{\"source\":") + JsonString(SourceText) + ",\"fileLabel\":\"stdlib-main.vs\",\"workspace\":" + JsonString(Root.generic_string()) + ",\"stdlibPath\":\"stdlib\",\"stopOnEntry\":false}");
		StdlibInput << Request(3, "configurationDone");
		StdlibInput << Request(4, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream StdlibIn(StdlibInput.str());
		std::ostringstream StdlibOut;
		const int StdlibExit = VeroScript::Test::FDapStdio::Run(StdlibIn, StdlibOut);
		const std::string StdlibOutput = StdlibOut.str();
		std::filesystem::remove_all(Root, FileError);
		if (!Expect(StdlibExit == 0, "custom stdlib DAP session shuts down") || !Expect(Contains(StdlibOutput, "91"), "DAP loads workspace-relative stdlib root"))
		{
			std::cerr << StdlibOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// A catalog containing only aliases/templates is still a meaningful compile
		// input. The DAP launch path must not discard it as an empty registry.
		const auto Unique =
		    std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path CatalogPath =
		    std::filesystem::temp_directory_path() / ("veroscript-dap-type-metadata-" + std::to_string(Unique) + ".json");
		{
			std::ofstream Catalog(CatalogPath, std::ios::binary | std::ios::trunc);
			Catalog
			    << "{\"version\":1,\"functions\":[],\"types\":[],"
			       "\"genericTypeTemplates\":[{\"name\":\"HostOnly\",\"arity\":1}],"
			       "\"typeAliases\":[{\"name\":\"HostAlias\",\"typeParams\":[],"
			       "\"targetTypeName\":\"int\"}]}";
		}
		std::ostringstream MetadataInput;
		MetadataInput << Request(1, "initialize");
		MetadataInput << Request(
		    2,
		    "launch",
		    std::string("{\"source\":\"void main() { HostAlias value = 42; print(value); }\\n\",") + "\"fileLabel\":\"metadata.vs\",\"bindingCatalogPaths\":[\"" + CatalogPath.generic_string() + "\"],\"stopOnEntry\":false}");
		MetadataInput << Request(3, "configurationDone");
		MetadataInput << Request(4, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream MetadataIn(MetadataInput.str());
		std::ostringstream MetadataOut;
		const int MetadataExit = VeroScript::Test::FDapStdio::Run(MetadataIn, MetadataOut);
		const std::string MetadataOutput = MetadataOut.str();
		FDapStdioRunOptions DefaultOptions;
		DefaultOptions.DefaultBindingCatalogPaths = {"", CatalogPath.filename().generic_string(),
		    CatalogPath.generic_string(), CatalogPath.parent_path().generic_string() + "/./" + CatalogPath.filename().generic_string()};
		std::ostringstream DefaultInput;
		DefaultInput << Request(1, "initialize");
		DefaultInput << Request(2, "launch",
		    std::string("{\"source\":\"void main(){HostAlias value=42;print(value);}\",") + "\"workspace\":\"" + CatalogPath.parent_path().generic_string() + "\"}");
		DefaultInput << Request(3, "configurationDone");
		DefaultInput << Request(4, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream DefaultIn(DefaultInput.str());
		std::ostringstream DefaultOut;
		const int DefaultExit = VeroScript::Test::FDapStdio::Run(DefaultIn, DefaultOut, DefaultOptions);
		std::error_code RemoveError;
		std::filesystem::remove(CatalogPath, RemoveError);
		if (!Expect(MetadataExit == 0, "type-metadata-only DAP session shuts down") || !Expect(Contains(MetadataOutput, "\"event\":\"output\"") && Contains(MetadataOutput, "42"), "DAP applies alias-only binding catalogs during compilation") || !Expect(DefaultExit == 0 && Contains(DefaultOut.str(), "\"output\":\"42\\n\""), "DAP deduplicates default relative, absolute and dotted catalog paths and skips empty paths"))
		{
			std::cerr << MetadataOutput << '\n'
			          << DefaultOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		const auto Unique =
		    std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path Root =
		    std::filesystem::temp_directory_path() / ("veroscript-dap-implicit-" + std::to_string(Unique));
		std::error_code FileError;
		std::filesystem::create_directories(Root, FileError);
		const std::filesystem::path SharedPath = Root / "shared.vs";
		const std::filesystem::path MainPath = Root / "main.vs";
		{
			std::ofstream Shared(SharedPath);
			Shared << "int project_value() { return 42; }\n";
		}
		std::ostringstream ProjectInput;
		ProjectInput << Request(1, "initialize");
		ProjectInput << Request(
		    2,
		    "launch",
		    std::string("{\"source\":\"void main() { print(project_value()); }\\n\",") + "\"fileLabel\":\"" + MainPath.generic_string() + "\"," + "\"implicitImportPaths\":[\"" + SharedPath.generic_string() + "\"]," + "\"allowCircularImports\":false," + "\"allowProjectWideAccess\":true,\"stopOnEntry\":false}");
		ProjectInput << Request(3, "configurationDone");
		ProjectInput << Request(4, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream ProjectIn(ProjectInput.str());
		std::ostringstream ProjectOut;
		const int ProjectExit = VeroScript::Test::FDapStdio::Run(ProjectIn, ProjectOut);
		const std::string ProjectOutput = ProjectOut.str();
		std::filesystem::remove_all(Root, FileError);
		if (!Expect(ProjectExit == 0, "implicit-import DAP session shuts down") || !Expect(Contains(ProjectOutput, "\"event\":\"output\"") && Contains(ProjectOutput, "42"), "DAP applies implicit imports and project visibility"))
		{
			std::cerr << ProjectOutput << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream InvalidDepthInput;
		// Keep this assertion deterministic after the preceding zh-CN protocol
		// session changed the process-wide tooling locale.
		InvalidDepthInput << Request(1, "initialize", "{\"locale\":\"en-US\"}");
		InvalidDepthInput << Request(
		    2,
		    "launch",
		    "{\"source\":\"void main() {}\\n\",\"fileLabel\":\"depth.vs\","
		    "\"maxCallDepth\":-1}");
		InvalidDepthInput << Request(
		    3,
		    "disconnect",
		    "{\"terminateDebuggee\":false}");
		std::istringstream InvalidDepthIn(InvalidDepthInput.str());
		std::ostringstream InvalidDepthOut;
		if (!Expect(
		        VeroScript::Test::FDapStdio::Run(InvalidDepthIn, InvalidDepthOut) == 0,
		        "invalid maxCallDepth request keeps DAP host alive") ||
		    !Expect(
		        Contains(InvalidDepthOut.str(), "\"success\":false") && Contains(InvalidDepthOut.str(), "maxCallDepth expects a non-negative platform-sized integer"),
		        "negative maxCallDepth is rejected"))
		{
			std::cerr << InvalidDepthOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream SurrogateInput;
		SurrogateInput << Request(1, "initialize");
		SurrogateInput << Request(
		    2,
		    "launch",
		    "{\"source\":\"void main() { print(\\\"\\uD83D\\uDE00\\\"); }\\n\","
		    "\"fileLabel\":\"surrogate.vs\",\"stopOnEntry\":false}");
		SurrogateInput << Request(3, "configurationDone");
		SurrogateInput << Request(4, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream SurrogateIn(SurrogateInput.str());
		std::ostringstream SurrogateOut;
		if (!Expect(
		        VeroScript::Test::FDapStdio::Run(SurrogateIn, SurrogateOut) == 0,
		        "launch JSON with UTF-16 surrogate pair"))
		{
			std::cerr << SurrogateOut.str() << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Contains(SurrogateOut.str(), "\"event\":\"exited\""), "surrogate launch exited"))
		{
			std::cerr << SurrogateOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Inspect the wire response, not only the adapter's internal result:
		// the client must see relocated executable lines and explicit failures.
		const std::string BindingSource =
		    "int main()\n{\n\n    print(1);\n    return 0;\n}\n";
		std::ostringstream BindingInput;
		BindingInput << Request(1, "initialize");
		BindingInput << Request(2, "launch",
		    "{\"source\":" + JsonString(BindingSource) + ",\"fileLabel\":\"dap-binding.vs\"}");
		BindingInput << Request(3, "setBreakpoints",
		    "{\"source\":{\"path\":\"dap-binding.vs\"},"
		    "\"breakpoints\":[{\"line\":3},{\"line\":20}]}");
		BindingInput << Request(4, "disconnect",
		    "{\"terminateDebuggee\":false}");
		std::istringstream BindingIn(BindingInput.str());
		std::ostringstream BindingOut;
		if (!Expect(VeroScript::Test::FDapStdio::Run(BindingIn, BindingOut) == 0 && Contains(BindingOut.str(), "\"line\":4,\"verified\":true") && Contains(BindingOut.str(), "\"line\":20,\"message\":") && Contains(BindingOut.str(), "\"verified\":false"),
		        "DAP setBreakpoints reports executable and unbound lines"))
		{
			std::cerr << BindingOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream LocalizedInput;
		LocalizedInput << Request(1, "initialize", "{\"locale\":\"zh-CN\"}");
		LocalizedInput << Request(2, "launch",
		    "{\"source\":\"int main() { return 0; }\\n\","
		    "\"fileLabel\":\"localized-breakpoint.vs\"}");
		LocalizedInput << Request(3, "setBreakpoints",
		    "{\"source\":{\"path\":\"localized-breakpoint.vs\"},"
		    "\"breakpoints\":[{\"line\":20}]}");
		LocalizedInput << Request(4, "disconnect",
		    "{\"terminateDebuggee\":false}");
		std::istringstream LocalizedIn(LocalizedInput.str());
		std::ostringstream LocalizedOut;
		if (!Expect(VeroScript::Test::FDapStdio::Run(LocalizedIn, LocalizedOut) == 0 && Contains(LocalizedOut.str(), "该行超出已编译源码文件的范围"),
		        "unbound breakpoint reason follows client locale"))
		{
			std::cerr << LocalizedOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// A pre-launch response is pending; launch must publish the resolved
		// state under the same DAP ID so clients can update its icon.
		std::ostringstream PreLaunchInput;
		PreLaunchInput << Request(1, "initialize");
		PreLaunchInput << Request(2, "setBreakpoints",
		    "{\"source\":{\"path\":\"prelaunch-event.vs\"},"
		    "\"breakpoints\":[{\"line\":3}]}");
		PreLaunchInput << Request(3, "launch",
		    "{\"source\":\"int main()\\n{\\n    print(1);\\n}\\n\","
		    "\"fileLabel\":\"prelaunch-event.vs\"}");
		PreLaunchInput << Request(4, "disconnect",
		    "{\"terminateDebuggee\":false}");
		std::istringstream PreLaunchIn(PreLaunchInput.str());
		std::ostringstream PreLaunchOut;
		const bool bRan = VeroScript::Test::FDapStdio::Run(PreLaunchIn, PreLaunchOut) == 0;
		const std::vector<std::string> PreLaunchBodies =
		    ExtractBodies(PreLaunchOut.str());
		bool bSawPending = false;
		bool bSawBoundEvent = false;
		for (const std::string& Body : PreLaunchBodies)
		{
			bSawPending |= Contains(Body, "\"command\":\"setBreakpoints\"") && Contains(Body, "\"id\":1") && Contains(Body, "\"verified\":false");
			bSawBoundEvent |= Contains(Body, "\"event\":\"breakpoint\"") && Contains(Body, "\"reason\":\"changed\"") && Contains(Body, "\"id\":1") && Contains(Body, "\"line\":3") && Contains(Body, "\"verified\":true");
		}
		if (!Expect(bRan && bSawPending && bSawBoundEvent,
		        "pre-launch breakpoint must publish a stable-ID changed event"))
		{
			std::cerr << PreLaunchOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// DAP clients may use zero-based source positions. The adapter keeps
		// one-based engine positions internally and must map both directions.
		std::ostringstream ZeroBasedInput;
		ZeroBasedInput << Request(1, "initialize",
		    "{\"linesStartAt1\":false,\"columnsStartAt1\":false}");
		ZeroBasedInput << Request(2, "setBreakpoints",
		    "{\"source\":{\"path\":\"zero-based.vs\"},"
		    "\"breakpoints\":[{\"line\":2}]}");
		ZeroBasedInput << Request(3, "launch",
		    "{\"source\":\"int main()\\n{\\n    print(1);\\n}\\n\","
		    "\"fileLabel\":\"zero-based.vs\"}");
		ZeroBasedInput << Request(4, "disconnect",
		    "{\"terminateDebuggee\":false}");
		std::istringstream ZeroBasedIn(ZeroBasedInput.str());
		std::ostringstream ZeroBasedOut;
		const bool bRan = VeroScript::Test::FDapStdio::Run(ZeroBasedIn, ZeroBasedOut) == 0;
		bool bSawPending = false;
		bool bSawBoundEvent = false;
		for (const std::string& Body : ExtractBodies(ZeroBasedOut.str()))
		{
			bSawPending |= Contains(Body, "\"command\":\"setBreakpoints\"") && Contains(Body, "\"line\":2") && Contains(Body, "\"verified\":false");
			bSawBoundEvent |= Contains(Body, "\"event\":\"breakpoint\"") && Contains(Body, "\"line\":2") && Contains(Body, "\"verified\":true");
		}
		if (!Expect(bRan && bSawPending && bSawBoundEvent,
		        "zero-based DAP breakpoints retain the client's line convention"))
		{
			std::cerr << ZeroBasedOut.str() << '\n';
			return EXIT_FAILURE;
		}

		auto ReadStoppedFrame = [](const bool bZeroBasedLines,
		                            const bool bZeroBasedColumns,
		                            int& OutLine, int& OutColumn) -> bool
		{
			FBlockingInputBuffer FrameInputBuffer;
			FBlockingOutputBuffer FrameOutputBuffer;
			std::istream FrameIn(&FrameInputBuffer);
			std::ostream FrameOut(&FrameOutputBuffer);
			const std::string InitializeArgs =
			    std::string("{\"linesStartAt1\":") + (bZeroBasedLines ? "false" : "true") + ",\"columnsStartAt1\":" + (bZeroBasedColumns ? "false" : "true") + '}';
			FrameInputBuffer.Append(Request(1, "initialize", InitializeArgs));
			FrameInputBuffer.Append(Request(2, "launch",
			    "{\"source\":\"void main() {\\n    print(1);\\n}\\n\","
			    "\"fileLabel\":\"frame-position.vs\",\"stopOnEntry\":true}"));
			FrameInputBuffer.Append(Request(3, "configurationDone"));
			int FrameExit = -1;
			std::thread Host([&]()
			{
				FrameExit = VeroScript::Test::FDapStdio::Run(FrameIn, FrameOut);
			});
			const bool bStopped = FrameOutputBuffer.WaitContains(
			    "\"event\":\"stopped\"", std::chrono::seconds(5));
			if (bStopped)
			{
				FrameInputBuffer.Append(Request(4, "stackTrace", "{\"threadId\":1}"));
			}
			const bool bHasStack = bStopped && FrameOutputBuffer.WaitContains("\"command\":\"stackTrace\"", std::chrono::seconds(5));
			FrameInputBuffer.Append(Request(5, "disconnect",
			    "{\"terminateDebuggee\":false}"));
			FrameInputBuffer.Close();
			Host.join();
			if (!bHasStack || FrameExit != 0)
			{
				return false;
			}
			for (const std::string& Body : ExtractBodies(FrameOutputBuffer.Str()))
			{
				if (!Contains(Body, "\"command\":\"stackTrace\""))
				{
					continue;
				}
				const std::size_t Frame = Body.find("\"stackFrames\":[{");
				if (Frame == std::string::npos)
				{
					return false;
				}
				const std::size_t Column = Body.find("\"column\":", Frame);
				const std::size_t Line = Body.find("\"line\":", Frame);
				if (Column == std::string::npos || Line == std::string::npos)
				{
					return false;
				}
				OutColumn = std::stoi(Body.substr(Column + 9));
				OutLine = std::stoi(Body.substr(Line + 7));
				return true;
			}
			return false;
		};
		int OneBasedLine = 0;
		int OneBasedColumn = 0;
		int ZeroBasedLine = 0;
		int ZeroBasedColumn = 0;
		int LineOnlyZeroBased = 0;
		int LineOnlyColumn = 0;
		int ColumnOnlyLine = 0;
		int ColumnOnlyZeroBased = 0;
		if (!Expect(ReadStoppedFrame(false, false, OneBasedLine, OneBasedColumn) && ReadStoppedFrame(true, true, ZeroBasedLine, ZeroBasedColumn) && ReadStoppedFrame(true, false, LineOnlyZeroBased, LineOnlyColumn) && ReadStoppedFrame(false, true, ColumnOnlyLine, ColumnOnlyZeroBased) && OneBasedLine == ZeroBasedLine + 1 && OneBasedColumn == ZeroBasedColumn + 1 && LineOnlyZeroBased == ZeroBasedLine && LineOnlyColumn == OneBasedColumn && ColumnOnlyLine == OneBasedLine && ColumnOnlyZeroBased == ZeroBasedColumn,
		        "stack frame line and column follow independent client base conventions"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// DAP sequence and line fields are integers. Fractions and out-of-range
		// numbers must use safe defaults rather than C++ floating-to-int UB.
		std::ostringstream NumericInput;
		NumericInput << Frame(
		    "{\"seq\":2147483648,\"type\":\"request\","
		    "\"command\":\"initialize\",\"arguments\":{}}");
		NumericInput << Frame(
		    "{\"seq\":2,\"type\":\"request\",\"command\":\"setBreakpoints\","
		    "\"arguments\":{\"source\":{\"path\":\"numeric.vs\"},"
		    "\"breakpoints\":[{\"line\":-1.5}]}}");
		NumericInput << Request(3, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream NumericIn(NumericInput.str());
		std::ostringstream NumericOut;
		if (!Expect(
		        VeroScript::Test::FDapStdio::Run(NumericIn, NumericOut) == 0,
		        "invalid numeric DAP fields keep the host alive") ||
		    !Expect(
		        Contains(NumericOut.str(), "\"request_seq\":0"),
		        "out-of-range DAP sequence uses the safe default") ||
		    !Expect(
		        Contains(NumericOut.str(), "\"line\":1"),
		        "invalid DAP breakpoint line is clamped safely"))
		{
			std::cerr << NumericOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream UnsupportedBreakpointInput;
		UnsupportedBreakpointInput << Request(1, "initialize", "{\"locale\":\"zh-CN\"}");
		UnsupportedBreakpointInput << Request(
		    2,
		    "setExceptionBreakpoints",
		    "{\"filters\":[]}");
		UnsupportedBreakpointInput << Request(
		    3,
		    "setExceptionBreakpoints",
		    "{\"filters\":[\"all\"]}");
		UnsupportedBreakpointInput << Request(4, "setFunctionBreakpoints");
		UnsupportedBreakpointInput << Request(5, "unknownDebugCommand");
		UnsupportedBreakpointInput << Request(6, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream UnsupportedBreakpointIn(UnsupportedBreakpointInput.str());
		std::ostringstream UnsupportedBreakpointOut;
		if (!Expect(
		        VeroScript::Test::FDapStdio::Run(UnsupportedBreakpointIn, UnsupportedBreakpointOut) == 0,
		        "unsupported breakpoint request keeps DAP host alive") ||
		    !Expect(
		        Contains(
		            UnsupportedBreakpointOut.str(),
		            "\"command\":\"setExceptionBreakpoints\",\"request_seq\":2,\"seq\":3,\"success\":true") &&
		            Contains(UnsupportedBreakpointOut.str(), "\"success\":false") && Contains(UnsupportedBreakpointOut.str(), "不支持的断点类型"),
		        "empty exception filters are cleared, while requested unsupported breakpoint classes are rejected") ||
		    !Expect(
		        Contains(UnsupportedBreakpointOut.str(), "不支持的调试命令"),
		        "unknown DAP command is rejected with localized text"))
		{
			std::cerr << UnsupportedBreakpointOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::istringstream InvalidLengthIn(
		    "Content-Length: 12junk\r\n\r\n{}");
		std::ostringstream InvalidLengthOut;
		if (!Expect(
		        VeroScript::Test::FDapStdio::Run(InvalidLengthIn, InvalidLengthOut) == VeroScript::Test::FDapStdio::ExitError,
		        "DAP rejects a partially parsed Content-Length"))
		{
			return EXIT_FAILURE;
		}

		std::istringstream OversizedIn(
		    "Content-Length: 67108865\r\n\r\n");
		std::ostringstream OversizedOut;
		if (!Expect(
		        VeroScript::Test::FDapStdio::Run(OversizedIn, OversizedOut) == VeroScript::Test::FDapStdio::ExitError,
		        "DAP rejects an oversized message before allocation"))
		{
			return EXIT_FAILURE;
		}

		std::string DeepJson(300, '[');
		DeepJson += "null";
		DeepJson.append(300, ']');
		std::istringstream DeepJsonIn(Frame(DeepJson));
		std::ostringstream DeepJsonOut;
		if (!Expect(
		        VeroScript::Test::FDapStdio::Run(DeepJsonIn, DeepJsonOut) == VeroScript::Test::FDapStdio::ExitError,
		        "DAP rejects pathological JSON nesting") ||
		    !Expect(
		        Contains(DeepJsonOut.str(), "JSON nested too deeply"),
		        "DAP reports the JSON nesting limit"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream EarlyInput;
		EarlyInput << Request(1, "initialize", "{\"locale\":\"en-US\"}");
		EarlyInput << Request(2, "threads");
		EarlyInput << Request(3, "configurationDone");
		EarlyInput << Request(4, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream EarlyIn(EarlyInput.str());
		std::ostringstream EarlyOut;
		if (!Expect(VeroScript::Test::FDapStdio::Run(EarlyIn, EarlyOut) == VeroScript::Test::FDapStdio::ExitOk && Contains(EarlyOut.str(), "\"threads\":[]") && Contains(EarlyOut.str(), "ConfigurationDone requires a successful Launch or Attach"),
		        "configurationDone before target selection must not start execution"))
		{
			std::cerr << EarlyOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		// Even a client bypassing capability negotiation must never fall back
		// from a domain request to loading the supplied program.
		for (const char* Fields : {
		         "\"attachMode\":\"domain\"",
		         "\"attachMode\":true",
		         "\"attachMode\":null",
		         "\"attachMode\":\"\"",
		         "\"attachMode\":\"unknown\"",
		         "\"domainId\":\"test-domain\"",
		         "\"attachMode\":\"program\",\"domainId\":\"test-domain\""})
		{
			std::ostringstream UnsafeInput;
			UnsafeInput << Request(1, "initialize");
			UnsafeInput << Request(2, "attach", std::string("{") + Fields + ",\"source\":" + JsonString("void main() { print(\"UNEXPECTED_DOMAIN_LAUNCH\"); }") + "}");
			UnsafeInput << Request(3, "configurationDone");
			UnsafeInput << Request(4, "disconnect");
			std::istringstream UnsafeIn(UnsafeInput.str());
			std::ostringstream UnsafeOut;
			const int Exit = VeroScript::Test::FDapStdio::Run(UnsafeIn, UnsafeOut);
			const std::vector<std::string> Responses = ExtractBodies(UnsafeOut.str());
			bool bAttachRejected = false;
			bool bConfigurationRejected = false;
			for (const std::string& Body : Responses)
			{
				if (Contains(Body, "\"success\":false"))
				{
					bAttachRejected |= Contains(Body, "\"command\":\"attach\"");
					bConfigurationRejected |= Contains(Body, "\"command\":\"configurationDone\"");
				}
			}
			if (!Expect(Exit == VeroScript::Test::FDapStdio::ExitOk && !Responses.empty() && Contains(Responses.front(), "\"veroscriptSupportsDomainAttach\":false") && bAttachRejected && bConfigurationRejected && !Contains(UnsafeOut.str(), "UNEXPECTED_DOMAIN_LAUNCH") && !Contains(UnsafeOut.str(), "\"event\":\"exited\""),
			        "unavailable or malformed domain attach cannot load or execute supplied source"))
			{
				std::cerr << Fields << '\n'
				          << UnsafeOut.str() << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	{
		for (const char* InvalidId : {"null", "false", "17", "[]", "{}"})
		{
			FTestLiveDomain Domain;
			std::ostringstream InvalidInput;
			InvalidInput << Request(1, "initialize");
			InvalidInput << Request(2, "attach",
			    std::string("{\"attachMode\":\"domain\",\"domainId\":") + InvalidId + "}");
			InvalidInput << Request(3, "configurationDone");
			InvalidInput << Request(4, "disconnect");
			std::istringstream InvalidIn(InvalidInput.str());
			std::ostringstream InvalidOut;
			if (!Expect(VeroScript::Test::FDapStdio::Run(InvalidIn, InvalidOut, {}, &Domain) == VeroScript::Test::FDapStdio::ExitOk && Domain.ConnectCount == 0 && Domain.ActivateCount == 0 && Contains(InvalidOut.str(), "domainId must be a string"),
			        "invalid domain identity cannot silently attach to the available domain"))
			{
				std::cerr << InvalidId << '\n'
				          << InvalidOut.str() << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	{
		FTestLiveDomain Domain;
		std::ostringstream Input;
		Input << Request(1, "initialize");
		Input << Request(2, "setBreakpoints", "{\"source\":{\"path\":\"live.vs\"},\"breakpoints\":[{\"line\":2}]}");
		Input << Request(3, "attach", "{\"attachMode\":\"domain\",\"domainId\":\"test-domain\"}");
		Input << Request(4, "setBreakpoints", "{\"source\":{\"path\":\"other.vs\"},\"breakpoints\":[{\"line\":3}]}");
		Input << Request(5, "configurationDone");
		Input << Request(6, "stackTrace", "{\"threadId\":1}");
		Input << Request(7, "continue", "{\"threadId\":1}");
		Input << Request(8, "stackTrace", "{\"threadId\":1}");
		Input << Request(9, "continue", "{\"threadId\":1}");
		Input << Request(10, "threads");
		Input << Request(11, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream LiveIn(Input.str());
		std::ostringstream LiveOut;
		FDapStdioRunOptions Options;
		const int Exit = VeroScript::Test::FDapStdio::Run(LiveIn, LiveOut, Options, &Domain);
		const std::string LiveOutput = LiveOut.str();
		const std::vector<std::string> LiveBodies = ExtractBodies(LiveOutput);
		if (!Expect(Exit == VeroScript::Test::FDapStdio::ExitOk,
		        "live attach disconnect keeps the host running") ||
		    !Expect(!LiveBodies.empty() && Contains(LiveBodies.front(), "\"command\":\"initialize\"") && Contains(LiveBodies.front(), "\"veroscriptSupportsDomainAttach\":true"),
		        "host-prepared domain advertises support in initialize before attach") ||
		    !Expect(Domain.ConnectCount == 1 && Domain.ActivateCount == 1 && Domain.BreakpointUpdates == 2 && Domain.ResumeCount == 2 && Domain.DisconnectCount == 1 && Domain.CancelCount == 0,
		        "live domain handles setup, two calls, and nonterminating detach") ||
		    !Expect(Domain.ReceivedBreakpointIds.size() == 2 && Domain.ReceivedBreakpointIds[0] > 0 && Domain.ReceivedBreakpointIds[1] > 0 && Domain.ReceivedBreakpointIds[0] != Domain.ReceivedBreakpointIds[1],
		        "breakpoint identities survive pre-attach and post-attach binding") ||
		    !Expect(Contains(LiveOutput, "\"veroscriptHostId\":\"test-host\"") && Contains(LiveOutput, "\"veroscriptSourceRevision\":\"source-2\"") && Contains(LiveOutput, "\"veroscriptBindingRevision\":\"bindings-3\"") && Contains(LiveOutput, "\"veroscriptCanPause\":true") && Contains(LiveOutput, "\"veroscriptCanStep\":false") && Contains(LiveOutput, "\"veroscriptCanEvaluate\":false") && Contains(LiveOutput, "\"version\":\"v2\""),
		        "live attach returns target and source identity") ||
		    !Expect(Contains(LiveOutput, "\"name\":\"host call\"") && Contains(LiveOutput, "\"event\":\"breakpoint\"") && Contains(LiveOutput, "\"threads\":[]") && !Contains(LiveOutput, "\"event\":\"exited\"") && !Contains(LiveOutput, "\"event\":\"terminated\""),
		        "successive host calls remain in one debug connection"))
		{
			std::cerr << LiveOutput << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		FTestLiveDomain PausedDomain;
		std::ostringstream PausedInput;
		PausedInput << Request(1, "initialize");
		PausedInput << Request(2, "attach", "{\"attachMode\":\"domain\"}");
		PausedInput << Request(3, "configurationDone");
		PausedInput << Request(4, "setBreakpoints",
		    "{\"source\":{\"path\":\"live.vs\"},\"breakpoints\":["
		    "{\"line\":2,\"condition\":\"ready\"}]}");
		PausedInput << Request(5, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream PausedIn(PausedInput.str());
		std::ostringstream PausedOut;
		FDapStdioRunOptions PausedOptions;
		if (!Expect(VeroScript::Test::FDapStdio::Run(PausedIn, PausedOut, PausedOptions, &PausedDomain) == VeroScript::Test::FDapStdio::ExitOk && PausedDomain.DisconnectCount == 1 && !PausedDomain.bStopped && PausedDomain.CancelCount == 0 && PausedDomain.BreakpointUpdates == 0 && Contains(PausedOut.str(), "host debug domain does not support conditional breakpoints"),
		        "disconnect releases a paused host call without cancellation"))
		{
			std::cerr << PausedOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		FTestLiveDomain RejectingDomain;
		RejectingDomain.bRejectResume = true;
		std::ostringstream RejectingInput;
		RejectingInput << Request(1, "initialize");
		RejectingInput << Request(2, "attach", "{\"attachMode\":\"domain\"}");
		RejectingInput << Request(3, "configurationDone");
		RejectingInput << Request(4, "continue", "{\"threadId\":1}");
		RejectingInput << Request(5, "next", "{\"threadId\":1}");
		RejectingInput << Request(6, "terminate");
		RejectingInput << Request(7, "disconnect");
		std::istringstream RejectingIn(RejectingInput.str());
		std::ostringstream RejectingOut;
		FDapStdioRunOptions RejectingOptions;
		const int Exit = VeroScript::Test::FDapStdio::Run(
		    RejectingIn, RejectingOut, RejectingOptions, &RejectingDomain);
		bool bRejectedContinue = false;
		bool bRejectedStep = false;
		bool bRejectedTerminate = false;
		for (const std::string& Body : ExtractBodies(RejectingOut.str()))
		{
			if (Contains(Body, "\"command\":\"continue\"") && Contains(Body, "\"success\":false"))
			{
				bRejectedContinue = true;
			}
			if (Contains(Body, "\"command\":\"next\"") && Contains(Body, "\"success\":false"))
			{
				bRejectedStep = true;
			}
			if (Contains(Body, "\"command\":\"terminate\"") && Contains(Body, "\"success\":false"))
			{
				bRejectedTerminate = true;
			}
		}
		if (!Expect(Exit == VeroScript::Test::FDapStdio::ExitOk && RejectingDomain.ResumeCount == 0 && RejectingDomain.CancelCount == 0 && bRejectedContinue && bRejectedStep && bRejectedTerminate,
		        "unsupported or rejected host commands return failed DAP responses"))
		{
			std::cerr << RejectingOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}
	for (const bool Retry : {false, true})
	{
		FTestLiveDomain Domain;
		std::ostringstream Input;
		Input << Request(1, "initialize");
		Input << Request(2, "setBreakpoints",
		    "{\"source\":{\"path\":\"live.vs\"},\"breakpoints\":[{\"line\":2}]}");
		Input << Request(3, "setBreakpoints",
		    "{\"source\":{\"path\":\"blocked.vs\"},\"breakpoints\":["
		    "{\"line\":3,\"condition\":\"ready\"}]}");
		Input << Request(4, "attach", "{\"attachMode\":\"domain\"}");
		if (Retry)
		{
			Input << Request(5, "setBreakpoints",
			    "{\"source\":{\"path\":\"blocked.vs\"},\"breakpoints\":[]}");
			Input << Request(6, "attach", "{\"attachMode\":\"domain\"}");
		}
		Input << Request(7, "disconnect");
		std::istringstream In(Input.str());
		std::ostringstream Out;
		const int Exit = VeroScript::Test::FDapStdio::Run(In, Out, {}, &Domain);
		bool Rejected = false, Accepted = false, PrematureEvent = false;
		int BreakpointEvents = 0;
		for (const auto& Body : ExtractBodies(Out.str()))
		{
			if (Contains(Body, "\"event\":\"breakpoint\""))
			{
				++BreakpointEvents;
				PrematureEvent |= !Rejected;
			}
			if (Contains(Body, "\"command\":\"attach\""))
			{
				Rejected |= Contains(Body, "\"request_seq\":4") && Contains(Body, "\"success\":false");
				Accepted |= Contains(Body, "\"request_seq\":6") && Contains(Body, "\"success\":true");
			}
		}
		// Verbose CTest retains the exact wire requests and responses for both
		// rejection and retry, including notification ordering.
		std::cout << "domain attach breakpoint transaction: retry=" << Retry
		          << "\nrequests:\n"
		          << Input.str() << "\nresponses:\n"
		          << Out.str() << '\n';
		if (!Expect(Exit == VeroScript::Test::FDapStdio::ExitOk && Rejected && Accepted == Retry && !PrematureEvent && BreakpointEvents == (Retry ? 1 : 0) && Domain.ConnectCount == (Retry ? 2 : 1) && Domain.DisconnectCount == (Retry ? 2 : 1) && Domain.BreakpointUpdates == (Retry ? 2 : 1) && !Domain.bConnected && !Domain.Callback,
		        "failed multi-source attachment emits no verified breakpoint notifications; retry emits one"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		FTestLiveDomain Domain;
		std::vector<FDapBreakpoint> Changes;
		bool AllSourcesReady = true;
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		const auto First = Adapter.SetBreakpoints("live.vs", {{2, {}}});
		const auto Second = Adapter.SetBreakpoints("other.vs", {{3, {}}});
		Adapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Breakpoint) return;
			AllSourcesReady &= Domain.BreakpointUpdates == 2;
			Changes.push_back(Event.Breakpoint);
		});
		if (!Expect(Adapter.AttachDomain(Domain, "test-domain").bSuccess && AllSourcesReady &&
		            Changes.size() == 2 && First.Breakpoints.size() == 1 && Second.Breakpoints.size() == 1 &&
		            Changes[0].Id == First.Breakpoints[0].Id && Changes[1].Id == Second.Breakpoints[0].Id &&
		            Changes[0].Path == "live.vs" && Changes[1].Path == "other.vs" && Changes[0].Line == 2 &&
		            Changes[1].Line == 3 && Changes[0].bVerified && Changes[1].bVerified,
		        "successful multi-source attach emits each original breakpoint after all sources bind"))
		{
			return EXIT_FAILURE;
		}

		Adapter.Disconnect();
	}
	{
		FTestLiveDomain Domain;
		Domain.bStopDuringBinding = true;
		std::ostringstream Input;
		Input << Request(1, "initialize");
		Input << Request(2, "setBreakpoints",
		    "{\"source\":{\"path\":\"live.vs\"},\"breakpoints\":[{\"line\":2}]}");
		Input << Request(3, "attach", "{\"attachMode\":\"domain\"}");
		Input << Request(4, "disconnect");
		std::cout << "domain stop during attachment preparation\nrequests:\n"
		          << Input.str() << std::endl;
		std::istringstream In(Input.str());
		std::ostringstream Out;
		const int Exit = VeroScript::Test::FDapStdio::Run(In, Out, {}, &Domain);
		std::cout << "responses:\n"
		          << Out.str() << '\n';
		bool Attached = false;
		for (const auto& Body : ExtractBodies(Out.str()))
		{
			Attached |= Contains(Body, "\"command\":\"attach\"") && Contains(Body, "\"success\":true");
		}

		if (!Expect(Exit == VeroScript::Test::FDapStdio::ExitOk && Attached &&
		            Contains(Out.str(), "\"event\":\"stopped\"") && Domain.ActivateCount == 0 &&
		            Domain.ConnectCount == 1 && Domain.DisconnectCount == 1 && !Domain.bStopped &&
		            !Domain.bConnected && !Domain.Callback,
		        "a host stop during breakpoint preparation does not enter the program execution stop pump"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		FTestLiveDomain ConditionalDomain;
		std::ostringstream ConditionalInput;
		ConditionalInput << Request(1, "initialize");
		ConditionalInput << Request(2, "setBreakpoints",
		    "{\"source\":{\"path\":\"live.vs\"},\"breakpoints\":["
		    "{\"line\":2,\"condition\":\"ready\"}]}");
		ConditionalInput << Request(3, "attach", "{\"attachMode\":\"domain\"}");
		ConditionalInput << Request(4, "disconnect");
		std::istringstream ConditionalIn(ConditionalInput.str());
		std::ostringstream ConditionalOut;
		FDapStdioRunOptions ConditionalOptions;
		if (!Expect(VeroScript::Test::FDapStdio::Run(
		                ConditionalIn, ConditionalOut, ConditionalOptions, &ConditionalDomain) == VeroScript::Test::FDapStdio::ExitOk &&
		            ConditionalDomain.ConnectCount == 1 && ConditionalDomain.DisconnectCount == 1 && ConditionalDomain.BreakpointUpdates == 0 && Contains(ConditionalOut.str(), "host debug domain does not support conditional breakpoints"),
		        "pending conditional breakpoints cannot attach to an unsupported host"))
		{
			std::cerr << ConditionalOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		FTestLiveDomain Domain;
		VeroScript::Tooling::VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		Adapter.SetBreakpoints("live.vs", {{2, {}}});
		Adapter.SetBreakpoints("./live.vs", {{3, {}}});
		if (!Expect(Adapter.AttachDomain(Domain, "test-domain").bSuccess && Domain.BreakpointUpdates == 1 &&
		            Domain.ReceivedBreakpointIds.size() == 1,
		        "domain attach binds only the replacement of a pending source alias"))
		{
			return EXIT_FAILURE;
		}

		const auto Cleared = Adapter.SetBreakpoints("./live.vs", {});
		if (!Expect(Cleared.bSuccess && Cleared.Breakpoints.empty() && Domain.BreakpointUpdates == 2 &&
		            Domain.ReceivedBreakpointPaths.back() == "live.vs",
		        "domain clearing uses the same source identity previously sent to the host"))
		{
			return EXIT_FAILURE;
		}

		Adapter.Disconnect();
		if (!Expect(Adapter.AttachDomain(Domain, "test-domain").bSuccess && Domain.BreakpointUpdates == 2,
		        "cleared source breakpoints cannot return when a host domain is reattached"))
		{
			return EXIT_FAILURE;
		}

		Adapter.Disconnect();
	}

	{
		FTestLiveDomain WrongDomain;
		std::ostringstream WrongInput;
		WrongInput << Request(1, "initialize");
		WrongInput << Request(2, "attach",
		    "{\"attachMode\":\"domain\",\"domainId\":\"another-domain\"}");
		WrongInput << Request(3, "disconnect");
		std::istringstream WrongIn(WrongInput.str());
		std::ostringstream WrongOut;
		FDapStdioRunOptions WrongOptions;
		if (!Expect(VeroScript::Test::FDapStdio::Run(WrongIn, WrongOut, WrongOptions, &WrongDomain) == VeroScript::Test::FDapStdio::ExitOk && WrongDomain.ConnectCount == 0 && Contains(WrongOut.str(), "identity is missing or does not match"),
		        "an unmatched domain identity is rejected before host connection"))
		{
			std::cerr << WrongOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	for (const bool Cancellation : {true, false})
	{
		std::ostringstream CancelInput;
		CancelInput << Request(1, "initialize");
		CancelInput << Request(2, "launch", "{\"source\":\"void main(){}\",\"fileLabel\":\"cancel-launch.vs\"}");
		CancelInput << Request(3, "threads");
		CancelInput << Request(4, "launch", "{\"source\":\"void main(){}\",\"fileLabel\":\"recovered-launch.vs\"}");
		CancelInput << Request(5, "disconnect", "{\"terminateDebuggee\":false}");
		std::istringstream CancelIn(CancelInput.str());
		std::ostringstream CancelOut;
		int Checks = 0, Exit = -1;
		bool Escaped = false;
		{
			VeroScript::FAnalysisControlScope Control(
			    [&]
			{
				if (Checks++ != 0) return false;
				if (!Cancellation)
				{
					throw std::runtime_error("test load failure");
				}

				return true;
			});
			try
			{
				Exit = VeroScript::Test::FDapStdio::Run(CancelIn, CancelOut);
			}
			catch (const VeroScript::FAnalysisCancelled&)
			{
				Escaped = true;
			}
			catch (const std::exception&)
			{
				Escaped = true;
			}
		}
		bool FailedLoad = false, EmptyThreads = false, RecoveredLoad = false;
		for (const auto& Body : ExtractBodies(CancelOut.str()))
		{
			if (Contains(Body, "\"request_seq\":2,"))
			{
				FailedLoad =
				    Contains(Body,
				        Cancellation ? "\"message\":\"cancelled\"" : "\"message\":\"test load failure\"") &&
				    Contains(Body, "\"success\":false");
			}

			if (Contains(Body, "\"request_seq\":3,"))
			{
				EmptyThreads = Contains(Body, "\"threads\":[]") && Contains(Body, "\"success\":true");
			}

			if (Contains(Body, "\"request_seq\":4,"))
			{
				RecoveredLoad = Contains(Body, "\"success\":true");
			}
		}
		if (!Expect(!Escaped && Exit == VeroScript::Test::FDapStdio::ExitOk && FailedLoad && EmptyThreads && RecoveredLoad,
		        "cancelled and exceptional program loads respond without ending the DAP connection"))
		{
			std::cerr << "escaped cancellation=" << Escaped << '\n'
			          << CancelOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}
	for (const bool StopOnEntry : {true, false})
	{
		// EOF is asynchronous shutdown, not a stopped-thread resume request.
		// Cancellation must be published before the stop pump is released.
		FBlockingInputBuffer EofInputBuffer;
		FBlockingOutputBuffer EofOutputBuffer;
		std::istream EofIn(&EofInputBuffer);
		std::ostream EofOut(&EofOutputBuffer);
		EofInputBuffer.Append(Request(1, "initialize"));
		EofInputBuffer.Append(Request(2, "launch",
		    std::string("{\"source\":\"void main(){print(\\\"before_loop\\\");while(true){}}\","
		                "\"fileLabel\":\"eof-cancel.vs\",\"stopOnEntry\":") +
		        (StopOnEntry ? "true}" : "false}")));
		EofInputBuffer.Append(Request(3, "configurationDone"));
		int EofExit = -1;
		std::thread EofHost([&]
		{
			EofExit = VeroScript::Test::FDapStdio::Run(EofIn, EofOut);
		});
		const bool Ready = EofOutputBuffer.WaitContains(
		    StopOnEntry ? "\"event\":\"stopped\"" : "before_loop", std::chrono::seconds(5));
		EofInputBuffer.Close();
		EofHost.join();
		const std::string EofOutput = EofOutputBuffer.Str();
		if (!Expect(Ready && EofExit == VeroScript::Test::FDapStdio::ExitOk && Contains(EofOutput, "\"event\":\"exited\"") && Contains(EofOutput, "\"event\":\"terminated\"") && (!StopOnEntry || !Contains(EofOutput, "before_loop")),
		        "EOF cancels both paused and running programs and joins their execution thread"))
		{
			std::cerr << EofOutput << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		FTestLiveDomain EofDomain;
		std::istringstream EofInput(Request(1, "initialize") + Request(2, "attach", "{\"attachMode\":\"domain\",\"domainId\":\"test-domain\"}") + Request(3, "configurationDone"));
		std::ostringstream EofOutput;
		const auto Exit = VeroScript::Test::FDapStdio::Run(EofInput, EofOutput, {}, &EofDomain);
		if (!Expect(Exit == VeroScript::Test::FDapStdio::ExitOk && EofDomain.ActivateCount == 1 && EofDomain.DisconnectCount == 1 && EofDomain.CancelCount == 0 && !EofDomain.bStopped && !EofDomain.bConnected,
		        "EOF detaches and restores a paused host domain without cancelling its execution"))
		{
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

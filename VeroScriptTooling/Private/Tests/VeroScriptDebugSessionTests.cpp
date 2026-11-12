// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptPath.h"
#include "VeroScriptEnvironment.h"

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptDebugSessionTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}
}

int main()
{
	{
		using namespace VeroScript;
		using namespace VeroScript::Tooling;
		VeroScriptDebugSession Session;
		VeroScript::Test::Configure(Session);
		const std::string Source = "int pick(int n){return n+1;}\nvoid main(){\nprint(pick(2));\n}\n";
		if (!Expect(Session.GetLastJitDiagnostics().Functions.empty() && Session.LoadSource(Source, "debug-diagnostics.vs").bSuccess && Session.Run().bSuccess && Session.GetLastJitDiagnostics().Functions.empty(),
				"debug diagnostics are disabled by default"))
		{
			return EXIT_FAILURE;
		}
		FJitPolicy Policy;
		Policy.Diagnostics.MaxFunctions = 8;
		Policy.Diagnostics.MaxDepth = 16;
		Session.SetJitPolicy(Policy);
		Session.SetExecutionBackend(EExecutionBackend::Jit);
		Session.SetBreakpoint("debug-diagnostics.vs", 3);
		int Stops = 0;
		Session.SetStopHandler([&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			++Stops;
			Active.ClearBreakpoints();
			Active.SetJitPolicy({});
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const auto Run = Session.Run();
		const auto& Diagnostics = Session.GetLastJitDiagnostics();
		if (!Expect(Run.bSuccess && Run.Output == "3\n" && Stops == 1
		        && Diagnostics.Functions.size() == 2 && !Diagnostics.bTimingEnabled && Diagnostics.DroppedRecords == 0
		        && std::all_of(Diagnostics.Functions.begin(), Diagnostics.Functions.end(), [](const auto& Item)
		        {
			        return !Item.Name.empty() && Item.Calls == 1 && Item.Returned == 1 && Item.Failures == 0
			            && Item.InclusiveNanoseconds == 0 && Item.SelfNanoseconds == 0
			            && Item.Executions[static_cast<std::size_t>(EJitExecutionKind::Vm)] == 1;
		        })
		        && Session.GetLastJitStats().CompileAttempts == 0 && Session.GetLastJitStats().Dispatches == 0
		        && Session.GetJitPolicy().Diagnostics.MaxFunctions == 0,
		        "debug run retains its VM diagnostic snapshot despite a next-run policy change"))
		{
			std::cerr << "success=" << Run.bSuccess << " output=" << Run.Output << " stops=" << Stops
			          << " records=" << Diagnostics.Functions.size()
			          << " timing=" << Diagnostics.bTimingEnabled << " dropped=" << Diagnostics.DroppedRecords
			          << " next-limit=" << Session.GetJitPolicy().Diagnostics.MaxFunctions
			          << " compile=" << Session.GetLastJitStats().CompileAttempts
			          << " dispatch=" << Session.GetLastJitStats().Dispatches << '\n';
			for (const auto& Item : Diagnostics.Functions)
			{
				std::cerr << Item.Name << ": calls=" << Item.Calls << " returned=" << Item.Returned
				          << " failures=" << Item.Failures << " control=" << Item.ControlTransfers
				          << " inclusive=" << Item.InclusiveNanoseconds << " self=" << Item.SelfNanoseconds << '\n';
			}
			return EXIT_FAILURE;
		}
		if (!Expect(Session.Run().bSuccess && Session.GetLastJitDiagnostics().Functions.empty(),
				"a run with diagnostics disabled clears the preceding snapshot"))
		{
			return EXIT_FAILURE;
		}
		Session.SetStopHandler({});
		Session.SetJitPolicy(Policy);
		if (!Expect(Session.Run().bSuccess && !Session.GetLastJitDiagnostics().Functions.empty(),
				"debug diagnostics can be enabled again"))
		{
			return EXIT_FAILURE;
		}
		Session.Clear();
		if (!Expect(Session.GetLastJitDiagnostics().Functions.empty() && Session.GetJitPolicy().Diagnostics.MaxFunctions == 8,
				"Clear removes the snapshot while retaining the requested policy"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript::Tooling;
		constexpr const char* Source =
		    "class Counter{public:static int value=9;};\n"
		    "class First<T>{public:static int value=Second<T>::read();};\n"
		    "class Second<T>{\n"
		    "public:\n"
		    "static int value=Counter::value;\n"
		    "static int read(){return Second<T>::value;}\n"
		    "};\n"
		    "class State{public:First<int> first=First<int>();Second<int> second=Second<int>();};\n"
		    "void main(){print(First<int>::value);print(Second<int>::value);}\n";
		VeroScriptDebugSession Session;
		VeroScript::Test::Configure(Session);
		if (!Expect(Session.LoadSource(Source, "deferred-static-debug.vs").bSuccess,
				"load generic static dependency debugging"))
		{
			return EXIT_FAILURE;
		}
		const auto Binding = Session.BindBreakpoint("deferred-static-debug.vs", 5);
		if (!Expect(Binding.Id != 0 && Binding.Line == 5,
				"bind breakpoint in a deferred generic static initializer"))
		{
			return EXIT_FAILURE;
		}
		int Stops = 0;
		bool bLocationValid = true, bEvaluationValid = true;
		std::string EvaluationError;
		Session.SetStopHandler([&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			++Stops;
			const auto Stack = Active.GetStackTrace();
			bLocationValid = bLocationValid && !Stack.empty() && Stack.front().Line == 5
			    && Stack.front().FileLabel == "deferred-static-debug.vs";
			const auto Value = Active.Evaluate("1 + 2", 0);
			bEvaluationValid = bEvaluationValid && Value.bOk && Value.Value == "3";
			if (!Value.bOk)
			{
				EvaluationError = Value.Error;
			}
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const auto Run = Session.Run();
		if (!Expect(Run.bSuccess && Run.Output == "9\n9\n" && Stops == 1 && bLocationValid && bEvaluationValid,
		        "deferred initializer preserves breakpoint location, stack and expression evaluation"))
		{
			std::cerr << "stops=" << Stops << " location=" << bLocationValid << " evaluation=" << bEvaluationValid
			          << " error=" << EvaluationError << " output=" << Run.Output << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		using namespace VeroScript::Tooling;
		VeroScriptDebugSession Session;
		VeroScript::Test::Configure(Session);
		const auto Loaded = Session.LoadSource(
		    "void main(){\nprint(1);\nprint(2);\n}\n", "replace-stop-handler.vs");
		if (!Expect(Loaded.bSuccess, "load stop handler replacement regression")) return EXIT_FAILURE;
		Session.SetBreakpoint("replace-stop-handler.vs", 2);
		Session.SetBreakpoint("replace-stop-handler.vs", 3);
		auto FirstOwner = std::make_shared<int>(1);
		auto SecondOwner = std::make_shared<int>(2);
		const std::weak_ptr<int> FirstLifetime = FirstOwner;
		const std::weak_ptr<int> SecondLifetime = SecondOwner;
		bool bFirstAlive = false, bSecondAlive = false;
		int FirstCalls = 0, SecondCalls = 0;
		Session.SetStopHandler([FirstOwner, SecondOwner, &FirstLifetime, &SecondLifetime,
		                           &bFirstAlive, &bSecondAlive, &FirstCalls, &SecondCalls](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			(void)FirstOwner; // The capture deliberately owns its lifetime guard.
			++FirstCalls;
			// Save external addresses before replacing the callback; the old
			// implementation destroys its captures during SetStopHandler itself.
			const auto* FirstWeak = &FirstLifetime;
			auto* FirstAlive = &bFirstAlive;
			Active.SetStopHandler([SecondOwner, &SecondLifetime, &bSecondAlive, &SecondCalls](VeroScriptDebugSession& Next, EDebugStopReason)
			{
				(void)SecondOwner;
				++SecondCalls;
				const auto* SecondWeak = &SecondLifetime;
				auto* SecondAlive = &bSecondAlive;
				Next.SetStopHandler({});
				*SecondAlive = !SecondWeak->expired();
				Next.SetRunMode(EDebugRunMode::Continue);
			});
			*FirstAlive = !FirstWeak->expired();
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		FirstOwner.reset();
		SecondOwner.reset();
		const auto Result = Session.Run();
		if (!Expect(Result.bSuccess && Result.Output == "1\n2\n" && FirstCalls == 1 && SecondCalls == 1 && bFirstAlive && bSecondAlive && FirstLifetime.expired() && SecondLifetime.expired(),
		        "replacing or clearing a stop handler retains its active captures until it returns"))
		{
			return EXIT_FAILURE;
		}

		std::vector<int> Visits;
		Session.SetStopHandler([Count = 0, &Visits](VeroScriptDebugSession& Active, EDebugStopReason) mutable
		{
			Visits.push_back(++Count);
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const auto Repeated = Session.Run();
		if (!Expect(Repeated.bSuccess && Repeated.Output == "1\n2\n" && Visits == std::vector<int>{1, 2},
		        "stop handler mutable captures keep the same identity across stops"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		using namespace VeroScript::Tooling;
		VeroScriptDebugSession Session;
		VeroScript::Test::Configure(Session);
		const auto Loaded = Session.LoadSource(
		    "void main(){\nint value=7;\nprint(value);\n}\n", "active-session.vs");
		if (!Expect(Loaded.bSuccess, "load session lifecycle regression")) return EXIT_FAILURE;
		Session.SetBreakpoint("active-session.vs", 3);
		int Stops = 0;
		bool bCorrect = true;
		Session.SetStopHandler([&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			++Stops;
			const auto Frames = Active.GetStackTrace();
			const auto SourceLoad = Active.LoadSource("void main(){print(99);}", "replacement.vs");
			const auto FileLoad = Active.LoadFile("missing-active-session.vs");
			const auto NestedRun = Active.Run();
			const auto Rejected = [](const auto& Result)
			{
				return !Result.bSuccess && Result.Diagnostics.size() == 1 && Result.Diagnostics.front().Code == "dap.already_running";
			};
			bCorrect &= Rejected(SourceLoad) && Rejected(FileLoad) && Rejected(NestedRun);
			Active.Clear();
			const auto PreservedFrames = Active.GetStackTrace();
			const auto Value = Active.Evaluate("value");
			bCorrect &= Active.IsLoaded() && !Frames.empty() && !PreservedFrames.empty() && Frames.front().FileLabel == PreservedFrames.front().FileLabel && Frames.front().Line == PreservedFrames.front().Line && Value.bOk && Value.Value == "7";
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const auto FirstRun = Session.Run();
		if (!Expect(FirstRun.bSuccess && FirstRun.Output == "7\n" && Stops == 1 && bCorrect,
		        "active session rejects lifecycle reentry and preserves the paused VM"))
		{
			return EXIT_FAILURE;
		}
		Session.Clear();
		if (!Expect(!Session.IsLoaded(), "completed Run releases lifecycle guard")) return EXIT_FAILURE;
		const auto Reloaded = Session.LoadSource("void main(){print(99);}", "replacement.vs");
		if (!Expect(Reloaded.bSuccess, "reload after completed Run")) return EXIT_FAILURE;
		const auto SecondRun = Session.Run();
		if (!Expect(SecondRun.bSuccess && SecondRun.Output == "99\n" && Stops == 1,
		        "replacement runs after original execution completes"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		using namespace VeroScript::Tooling;
		VeroScriptDebugSession Session;
		VeroScript::Test::Configure(Session);
		const auto Loaded = Session.LoadSource("void main(){\nprint(7);\n}\n", "throwing-stop.vs");
		if (!Expect(Loaded.bSuccess, "load throwing stop regression")) return EXIT_FAILURE;
		Session.SetBreakpoint("throwing-stop.vs", 2);
		Session.SetStopHandler([](VeroScriptDebugSession&, EDebugStopReason)
		{
			throw std::runtime_error("stop callback failed");
		});
		bool bThrew = false;
		try
		{
			Session.Run();
		}
		catch (const std::runtime_error&)
		{
			bThrew = true;
		}
		if (!Expect(bThrew, "stop callback exception propagates")) return EXIT_FAILURE;
		Session.SetStopHandler({});
		Session.Clear();
		if (!Expect(!Session.IsLoaded(), "exceptional Run releases lifecycle guard")) return EXIT_FAILURE;
		const auto Reloaded = Session.LoadSource("void main(){print(99);}", "after-throw.vs");
		if (!Expect(Reloaded.bSuccess, "reload after exceptional Run")) return EXIT_FAILURE;
		const auto Result = Session.Run();
		if (!Expect(Result.bSuccess && Result.Output == "99\n",
		        "session remains usable after callback exception"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		using namespace VeroScript::Tooling;
		VeroScriptDebugSession Session;
		VeroScript::Test::Configure(Session);
		const auto Loaded = Session.LoadSource(
		    "int marker=7;\nvoid main(string[] args){print(args[0]);}\n", "argument-snapshot.vs");
		if (!Expect(Loaded.bSuccess, "load run argument snapshot regression")) return EXIT_FAILURE;
		Session.SetScriptArguments({"original argument retained during initialization"});
		Session.SetBreakpoint("argument-snapshot.vs", 1);
		bool bStoppedInInit = false;
		Session.SetStopHandler([&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			const auto Frames = Active.GetStackTrace();
			bStoppedInInit = !Frames.empty() && Frames.front().Line == 1;
			Active.SetScriptArguments({"replacement configured for the next execution"});
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const auto FirstRun = Session.Run();
		if (!Expect(FirstRun.bSuccess && bStoppedInInit && FirstRun.Output == "original argument retained during initialization\n",
		        "current Run owns argument strings across initialization callbacks"))
		{
			return EXIT_FAILURE;
		}
		Session.ClearBreakpoints();
		Session.SetStopHandler({});
		const auto SecondRun = Session.Run();
		if (!Expect(SecondRun.bSuccess && SecondRun.Output == "replacement configured for the next execution\n",
		        "callback argument changes apply to the following Run"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		using namespace VeroScript::Tooling;
		VeroScriptDebugSession Session;
		VeroScript::Test::Configure(Session);
		const auto Loaded = Session.LoadSource(
		    "struct Item{int value=7;};\nvoid main(){\nvariant box=Item();\nprint(0);\n}\n",
		    "readonly-variant.vs");
		if (!Expect(Loaded.bSuccess, "load immutable variant inspection")) return EXIT_FAILURE;
		Session.SetBreakpoint("readonly-variant.vs", 4);
		bool bChecked = false;
		bool bCorrect = true;
		Session.SetStopHandler([&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			const auto Locals = Active.GetLocals();
			const auto Box = std::find_if(Locals.begin(), Locals.end(),
			    [](const FDebugVariable& Variable)
			{
				return Variable.Name == "box";
			});
			if (Box == Locals.end())
			{
				bCorrect = false;
				Active.SetRunMode(EDebugRunMode::Continue);
				return;
			}
			const auto Children = Active.GetChildren(*Box);
			bCorrect &= Children.size() == 3;
			for (const auto& Child : Children)
			{
				bCorrect &= Child.bReadOnly;
			}
			const auto Payload = std::find_if(Children.begin(), Children.end(),
			    [](const FDebugVariable& Variable)
			{
				return Variable.Name == "$value";
			});
			if (Payload != Children.end())
			{
				const auto Fields = Active.GetChildren(*Payload);
				bCorrect &= Fields.size() == 1 && Fields.front().bReadOnly && !Active.SetChild(Fields.front(), "9").bOk;
				// Raw-value callers must retain readonly without an engine descriptor.
				auto RawPayload = *Payload;
				RawPayload.EngineVariable.reset();
				const auto RawFields = Active.GetChildren(RawPayload);
				bCorrect &= RawFields.size() == 1 && RawFields.front().bReadOnly;
			}
			else
			{
				bCorrect = false;
			}
			bChecked = true;
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		if (!Expect(Session.Run().bSuccess && bChecked && bCorrect,
		        "readonly survives debugger mapping and nested/raw variant inspection"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		using namespace VeroScript;
		using namespace VeroScript::Tooling;
		struct FGetter final : IAnnotationProcessor
		{
			void Process(const FAnnotationContext& Context, FAnnotationOutput& Output) override
			{
				Output.SubmitMethod(Context.Owner->Id, EMemberAccess::Public, "int Read(){print(\"中文😀\");return value;}");
			}
		};
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		std::string Error;
		if (!Expect(Registry->Register({"@Getter", "debug.test.getter", "1", "",
		                                   AnnotationTargetMask(EAnnotationTarget::Field), 1, 0,
		                                   []
		{
			return std::make_unique<FGetter>();
		}},
		                Error),
		        "register debug getter processor"))
		{
			return EXIT_FAILURE;
		}

		Registry->Freeze();
		FDebugLoadOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.AnnotationProcessing.Registry = Registry;
		VeroScriptDebugSession Session;
		VeroScript::Test::Configure(Session);
		if (!Expect(Session
		                .LoadSource(
		                    "struct A {@Getter int value=4;};\nvoid main(){\nA a=A();\nprint(a.Read());\n}",
		                    "generated-debug.vs", Options)
		                .bSuccess,
		        "load generated debug method"))
		{
			return EXIT_FAILURE;
		}

		Session.SetBreakpoint("generated-debug.vs", 4);
		int Stops = 0;
		std::string Label, Captured;
		Session.SetStopHandler(
		    [&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			if (++Stops == 1)
			{
				Active.SetRunMode(EDebugRunMode::StepIn);
				return;
			}
			const auto Frames = Active.GetStackTrace();
			if (!Frames.empty() && Frames.front().FileLabel.rfind("veroscript-generated:", 0) == 0)
			{
				Label = Frames.front().FileLabel;
				Active.TryGetGeneratedSource(Label, Captured);
			}
			Active.SetRunMode(
			    !Label.empty() || Stops >= 16 ? EDebugRunMode::Continue : EDebugRunMode::StepIn);
		});
		if (!Expect(Session.Run().bSuccess && !Label.empty() && Captured.find("Read") != std::string::npos &&
		            Captured.find("return value") != std::string::npos,
		        "step into generated code exposes its captured immutable source"))
		{
			return EXIT_FAILURE;
		}

		const auto UnicodeStart = Captured.find("中文😀");
		if (!Expect(UnicodeStart != std::string::npos, "generated source retains unicode")) return EXIT_FAILURE;
		const auto UnicodeEnd = UnicodeStart + std::string("中文😀").size();
		const auto LineStart = Captured.rfind('\n', UnicodeStart);
		const auto GeneratedLine = static_cast<std::uint32_t>(std::count(Captured.begin(),
		                               Captured.begin() + UnicodeStart, '\n')) +
		    1;
		const auto GeneratedColumn = static_cast<std::uint32_t>(UnicodeEnd - (LineStart == std::string::npos ? 0 : LineStart + 1)) + 1;
		if (!Expect(Session.GetUtf16Column(Label, GeneratedLine, GeneratedColumn) == GeneratedColumn - 6,
		        "generated debug source uses the same UTF-16 column conversion"))
		{
			return EXIT_FAILURE;
		}

		Session.Clear();
		if (!Expect(!Session.TryGetGeneratedSource(Label, Captured) && Captured.empty(),
		        "clearing a debug session releases generated source"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		using namespace VeroScript::Tooling;
		struct FWarning final : IAnnotationProcessor
		{
			void Process(const FAnnotationContext&, FAnnotationOutput& Output) override
			{
				FScriptError Diagnostic;
				Diagnostic.Message = "annotation warning";
				Diagnostic.Severity = ECompileDiagnosticSeverity::Warning;
				Output.ReportDiagnostic(std::move(Diagnostic));
			}
		};
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		std::string Error;
		if (!Expect(Registry->Register({"@Warning", "debug.test.warning", "1", "",
		                                   AnnotationTargetMask(EAnnotationTarget::Field), 1, 0,
		                                   []
		{
			return std::make_unique<FWarning>();
		}},
		                Error),
		        "register debug warning processor"))
		{
			return EXIT_FAILURE;
		}

		Registry->Freeze();
		FDebugLoadOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.AnnotationProcessing.Registry = Registry;
		VeroScriptDebugSession Session;
		VeroScript::Test::Configure(Session);
		const auto Load = Session.LoadSource("#define INTRO \\\nvoid unused(){}\nINTRO\nstruct A {@Warning int value=1;};void main(){}", "annotation-warning.vs", Options);
		if (!Expect(Load.bSuccess && Load.Diagnostics.size() == 1 &&
		            Load.Diagnostics.front().Severity == EDiagnosticSeverity::Warning &&
		            Load.Diagnostics.front().Range.StartLine == 4 &&
		            Load.Diagnostics.front().bPhysicalLocation && Session.Run().bSuccess,
		        "successful debug load retains processor warnings without reporting an error"))
		{
			return EXIT_FAILURE;
		}
	}
	using namespace VeroScript::Tooling;

	constexpr const char* Source =
	    "void main() {\n"
	    "    int x = 1;\n"
	    "    int y = 2;\n"
	    "    print(x + y);\n"
	    "}\n";

	VeroScriptDebugSession Session;
	VeroScript::Test::Configure(Session);
	const FDebugLoadResult Load = Session.LoadSource(Source, "debug.vs");
	if (!Expect(Load.bSuccess && Session.IsLoaded(), "load source for debug"))
	{
		return EXIT_FAILURE;
	}

	int StopCount = 0;
	std::uint32_t StoppedLine = 0;
	std::string LocalX;
	std::string EvalPlus;
	std::string EvalCast;
	Session.SetBreakpoint("debug.vs", 4);
	Session.SetStopHandler(
	    [&](VeroScriptDebugSession& Active, EDebugStopReason)
	{
		++StopCount;
		const auto Stack = Active.GetStackTrace();
		if (!Stack.empty())
		{
			StoppedLine = Stack.front().Line;
		}
		for (const FDebugVariable& Local : Active.GetLocals(0))
		{
			if (Local.Name == "x")
			{
				LocalX = Local.Value;
			}
		}
		const FDebugEvaluateResult Plus = Active.Evaluate("x + 1", 0);
		if (Plus.bOk)
		{
			EvalPlus = Plus.Value;
		}
		const FDebugEvaluateResult Cast = Active.Evaluate("(int)(x + 0.75)", 0);
		if (Cast.bOk)
		{
			EvalCast = Cast.Value;
		}
		(void)Active.SetLocal("x", "5", 0);
		Active.SetRunMode(EDebugRunMode::Continue);
	});

	const FDebugRunResult Run = Session.Run();
	if (!Expect(Run.bSuccess, "debug run should succeed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(StopCount == 1, "breakpoint on a single-pass line should stop exactly once"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(StoppedLine == 4, "should stop on breakpoint line"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(LocalX == "1", "local x should be visible while stopped"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(EvalPlus == "2", "session Evaluate x+1"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(EvalCast == "1", "session Evaluate C-style cast sugar"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Run.Output.find('7') != std::string::npos, "program should print 7 after SetLocal"))
	{
		return EXIT_FAILURE;
	}

	{
		// std::console::write is deliberately newline-free.  Its explicit flush must
		// still reach adapters before Run returns, while stderr remains distinct.
		constexpr const char* ConsoleSource =
		    "void main() {\n"
		    "    std::console::write(\"out\");\n"
		    "    std::console::flush();\n"
		    "    std::console::error(\"err\");\n"
		    "}\n";
		VeroScriptDebugSession ConsoleSession;
		VeroScript::Test::Configure(ConsoleSession);
		if (!Expect(
		        ConsoleSession.LoadSource(ConsoleSource, "debug-console.vs").bSuccess,
		        "load console debug session"))
		{
			return EXIT_FAILURE;
		}
		std::vector<std::string> OutputChunks;
		std::vector<std::string> ErrorChunks;
		ConsoleSession.SetOutputHandler(
		    [&](const std::string_view Text, const bool bIsError)
		{
			(bIsError ? ErrorChunks : OutputChunks).emplace_back(Text);
		});
		const FDebugRunResult ConsoleRun = ConsoleSession.Run();
		if (!Expect(
		        ConsoleRun.bSuccess && ConsoleRun.Output == "out" && ConsoleRun.ErrorOutput == "err" && OutputChunks.size() == 1 && OutputChunks.front() == "out" && ErrorChunks.size() == 1 && ErrorChunks.front() == "err",
		        "debug session streams flushed stdout and distinct stderr"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ExceptionSource =
		    "int main()\n"
		    "{\n"
		    "    try\n"
		    "    {\n"
		    "        throw 3;\n"
		    "    }\n"
		    "    catch (const int& value)\n"
		    "    {\n"
		    "        print(value);\n"
		    "    }\n"
		    "    finally\n"
		    "    {\n"
		    "        print(4);\n"
		    "    }\n"
		    "    return 0;\n"
		    "}\n";
		VeroScriptDebugSession ExceptionSession;
		VeroScript::Test::Configure(ExceptionSession);
		if (!Expect(
		        ExceptionSession.LoadSource(
		                            ExceptionSource, "debug-exception.vs")
		            .bSuccess,
		        "load exception debug session"))
		{
			return EXIT_FAILURE;
		}
		int ExceptionStops = 0;
		std::string CaughtValue;
		ExceptionSession.SetBreakpoint("debug-exception.vs", 9);
		ExceptionSession.SetBreakpoint("debug-exception.vs", 13);
		ExceptionSession.SetStopHandler(
		    [&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			++ExceptionStops;
			for (const FDebugVariable& Local : Active.GetLocals(0))
			{
				if (Local.Name == "value")
				{
					CaughtValue = Local.Value;
				}
			}
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const FDebugRunResult ExceptionRun = ExceptionSession.Run();
		if (!Expect(
		        ExceptionRun.bSuccess && ExceptionRun.ExitCode == 0 && ExceptionRun.Output == "3\n4\n" && ExceptionStops == 2 && CaughtValue == "3",
		        "debugger must stop in catch/finally and expose the catch local"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDebugSession Cond;
		VeroScript::Test::Configure(Cond);
		if (!Expect(Cond.LoadSource(Source, "cond.vs").bSuccess, "load conditional session"))
		{
			return EXIT_FAILURE;
		}
		int CondStops = 0;
		Cond.SetBreakpoint("cond.vs", 4, "x == 1");
		Cond.SetStopHandler(
		    [&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			++CondStops;
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const FDebugRunResult CondRun = Cond.Run();
		if (!Expect(CondRun.bSuccess && CondStops == 1, "session conditional x==1 stops"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FDebugLoadOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.PredefinedMacros = {
		    {"HOST_ID", {"value"}, false, "value", "debug-host"},
		    {"HOST_ADD", {"left", "right"}, false, "left + right", "debug-host"},
		    {"HOST_EQ_ONE", {"value"}, false, "value == 1", "debug-host"}};
		VeroScriptDebugSession HostMacroSession;
		VeroScript::Test::Configure(HostMacroSession);
		if (!Expect(
		        HostMacroSession.LoadSource(
		                            "void main() {\n"
		                            "    int x = HOST_ID(1);\n"
		                            "    print(x);\n"
		                            "}\n",
		                            "host_macro_debug.vs",
		                            Options)
		            .bSuccess,
		        "load debug source with predefined macro"))
		{
			return EXIT_FAILURE;
		}
		int Stops = 0;
		std::string Evaluated;
		HostMacroSession.SetBreakpoint(
		    "host_macro_debug.vs",
		    3,
		    "HOST_EQ_ONE(x)");
		HostMacroSession.SetStopHandler(
		    [&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			++Stops;
			const FDebugEvaluateResult Value = Active.Evaluate("HOST_ADD(x, 4)", 0);
			if (Value.bOk)
			{
				Evaluated = Value.Value;
			}
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const FDebugRunResult MacroRun = HostMacroSession.Run();
		if (!Expect(
		        MacroRun.bSuccess && Stops == 1 && Evaluated == "5",
		        "predefined macros apply to breakpoint conditions and debug Evaluate"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MacroSource =
		    "#define STEP print(x);\n"
		    "void main() {\n"
		    "    int x = 1;\n"
		    "    STEP\n"
		    "}\n";
		VeroScriptDebugSession MacroSession;
		VeroScript::Test::Configure(MacroSession);
		if (!Expect(MacroSession.LoadSource(MacroSource, "macro.vs").bSuccess, "load macro debug"))
		{
			return EXIT_FAILURE;
		}
		int MacroStops = 0;
		std::uint32_t MacroStoppedLine = 0;
		MacroSession.SetBreakpoint("macro.vs", 4);
		MacroSession.SetStopHandler(
		    [&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			++MacroStops;
			const auto Stack = Active.GetStackTrace();
			if (!Stack.empty())
			{
				MacroStoppedLine = Stack.front().Line;
			}
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const FDebugRunResult MacroRun = MacroSession.Run();
		if (!Expect(MacroRun.bSuccess && MacroStops == 1, "macro breakpoint should stop once"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(MacroStoppedLine == 4, "stopped line should be physical STEP line"))
		{
			std::cerr << "line=" << MacroStoppedLine << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// One source line expands to many IPs; unmapped opcodes must not re-arm the latch.
		constexpr const char* DenseSource =
		    "void main() {\n"
		    "    int a = 1;\n"
		    "    int b = 2;\n"
		    "    int c = a + b * (a + b) - a;\n"
		    "    print(c);\n"
		    "}\n";
		VeroScriptDebugSession Dense;
		VeroScript::Test::Configure(Dense);
		if (!Expect(Dense.LoadSource(DenseSource, "dense.vs").bSuccess, "load dense line session"))
		{
			return EXIT_FAILURE;
		}
		int DenseStops = 0;
		Dense.SetBreakpoint("dense.vs", 4);
		Dense.SetStopHandler(
		    [&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			++DenseStops;
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		const FDebugRunResult DenseRun = Dense.Run();
		if (!Expect(DenseRun.bSuccess && DenseStops == 1,
		        "dense expression line must not re-hit the same breakpoint"))
		{
			std::cerr << "denseStops=" << DenseStops << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Requested Mixed/Jit must not break debugging: debugger attach forces Vm.
		constexpr const char* Backends[] = {"mixed", "jit"};
		for (const char* BackendName : Backends)
		{
			VeroScript::EExecutionBackend Backend = VeroScript::EExecutionBackend::Vm;
			if (!Expect(
			        VeroScript::TryParseExecutionBackend(BackendName, Backend),
			        "parse execution backend"))
			{
				return EXIT_FAILURE;
			}
			VeroScriptDebugSession BackendSession;
			VeroScript::Test::Configure(BackendSession);
			BackendSession.SetExecutionBackend(Backend);
			if (!Expect(
			        BackendSession.GetExecutionBackend() == Backend,
			        "session stores requested execution backend"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(
			        BackendSession.LoadSource(Source, "backend.vs").bSuccess,
			        "load with non-vm backend preference"))
			{
				return EXIT_FAILURE;
			}
			int BackendStops = 0;
			BackendSession.SetBreakpoint("backend.vs", 4);
			BackendSession.SetStopHandler(
			    [&](VeroScriptDebugSession& Active, EDebugStopReason)
			{
				++BackendStops;
				Active.SetRunMode(EDebugRunMode::Continue);
			});
			const FDebugRunResult BackendRun = BackendSession.Run();
			if (!Expect(
			        BackendRun.bSuccess && BackendStops == 1,
			        "debug run with mixed/jit preference still hits breakpoint under Vm"))
			{
				std::cerr << "backend=" << BackendName
				          << " stops=" << BackendStops << '\n';
				return EXIT_FAILURE;
			}
			// Debug execution is interpreter-only even when a JIT mode is requested.
			if (!Expect(
			        BackendSession.GetLastJitStats().CompileAttempts == 0 && BackendSession.GetLastJitStats().Dispatches == 0,
			        "debug Run should avoid backend compilation and execution"))
			{
				std::cerr << "backend=" << BackendName
				          << " invalidations="
				          << BackendSession.GetLastJitStats().CacheInvalidations << '\n';
				return EXIT_FAILURE;
			}
		}

		{
			VeroScriptDebugSession PolicySession;
			VeroScript::Test::Configure(PolicySession);
			VeroScript::FJitPolicy Policy;
			Policy.MixedHotCallThreshold = 8;
			Policy.EagerCompileBudget = 16;
			PolicySession.SetJitPolicy(Policy);
			if (!Expect(
			        PolicySession.GetJitPolicy().MixedHotCallThreshold == 8 && PolicySession.GetJitPolicy().EagerCompileBudget == 16,
			        "debug session stores JIT policy"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(
			        PolicySession.LoadSource(Source, "policy.vs").bSuccess,
			        "load with custom JIT policy"))
			{
				return EXIT_FAILURE;
			}
			int PolicyStops = 0;
			PolicySession.SetBreakpoint("policy.vs", 4);
			PolicySession.SetStopHandler(
			    [&](VeroScriptDebugSession& Active, EDebugStopReason)
			{
				++PolicyStops;
				Active.SetRunMode(EDebugRunMode::Continue);
			});
			const FDebugRunResult PolicyRun = PolicySession.Run();
			if (!Expect(
			        PolicyRun.bSuccess && PolicyStops == 1 && PolicySession.GetLastJitStats().CompileAttempts == 0 && PolicySession.GetLastJitStats().Dispatches == 0,
			        "custom JIT policy still debugs under forced Vm without backend execution"))
			{
				std::cerr << "invalidations="
				          << PolicySession.GetLastJitStats().CacheInvalidations << '\n';
				return EXIT_FAILURE;
			}

			PolicySession.Clear();
			if (!Expect(
			        PolicySession.GetLastJitStats().CacheInvalidations == 0 && PolicySession.GetLastJitStats().CompileAttempts == 0,
			        "Clear resets last JIT stats"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(
			        PolicySession.GetJitPolicy().MixedHotCallThreshold == 8,
			        "Clear keeps requested JIT policy preference"))
			{
				return EXIT_FAILURE;
			}
		}

		{
			constexpr const char* RecursiveSource =
			    "int dive(int n) {\n"
			    "    if (n <= 0) { return 0; }\n"
			    "    return 1 + dive(n - 1);\n"
			    "}\n"
			    "void main() { print(dive(8)); }\n";
			VeroScriptDebugSession DepthSession;
			VeroScript::Test::Configure(DepthSession);
			DepthSession.SetMaxCallDepth(4);
			if (!Expect(
			        DepthSession.GetMaxCallDepth() == 4,
			        "debug session stores max call depth") ||
			    !Expect(
			        DepthSession.LoadSource(
			                        RecursiveSource,
			                        "recursion-depth.vs")
			            .bSuccess,
			        "load recursion depth source"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(
			        !DepthSession.Run().bSuccess,
			        "debug session enforces custom max call depth"))
			{
				return EXIT_FAILURE;
			}
			DepthSession.SetMaxCallDepth(32);
			if (!Expect(
			        DepthSession.Run().bSuccess,
			        "debug session applies updated max call depth"))
			{
				return EXIT_FAILURE;
			}
			DepthSession.Clear();
			if (!Expect(
			        DepthSession.GetMaxCallDepth() == 32,
			        "Clear keeps max call depth preference"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		FDebugLoadOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.SourceOverlays = {{"macro-debug/library.vs",
		    "#define_export VALUE 42\n#define_export SAY(x) \\\nprint(x)\n"}};
		VeroScriptDebugSession MacroSession;
		VeroScript::Test::Configure(MacroSession);
		if (!Expect(MacroSession
		                .LoadSource(
		                    "import \"library.vs\";\nvoid main(){\nint result = VALUE;\nSAY(result);\n}\n",
		                    "macro-debug/main.vs", Options)
		                .bSuccess,
		        "debug imported macro using compile overlay snapshot"))
		{
			return EXIT_FAILURE;
		}

		int Stops = 0;
		MacroSession.SetBreakpoint("macro-debug/main.vs", 4);
		MacroSession.SetStopHandler(
		    [&](VeroScriptDebugSession& Active, EDebugStopReason)
		{
			const auto Stack = Active.GetStackTrace();
			if (!Stack.empty() && Stack.front().Line == 4)
			{
				++Stops;
			}

			Active.SetRunMode(EDebugRunMode::Continue);
		});
		if (!Expect(MacroSession.Run().bSuccess && Stops == 1,
		        "imported continued macro breakpoint maps to caller physical line"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(!MacroSession
		                .LoadSource("import \"missing.vs\";void main(){}", "macro-debug/failed.vs", Options)
		                .bSuccess &&
		            !MacroSession.IsLoaded() && !MacroSession.Run().bSuccess &&
		            MacroSession.BindBreakpoint("macro-debug/main.vs", 4).Id == 0,
		        "failed source reload discards old bytecode and incompatible macro maps"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(MacroSession.LoadSource("void main(){print(1);}", "macro-debug/reloaded.vs").bSuccess &&
		            MacroSession.Run().bSuccess && Stops == 1,
		        "valid reload after failure has no stale breakpoints"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(!MacroSession.LoadFile("macro-debug/missing-file.vs").bSuccess &&
		            !MacroSession.IsLoaded() && !MacroSession.Run().bSuccess,
		        "failed file reload also discards the previous program"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		VeroScriptDebugSession Columns;
		VeroScript::Test::Configure(Columns);
		const std::string Unicode = "void main(){print(\"中文😀\");}\r\n";
		const std::uint32_t ByteColumn = static_cast<std::uint32_t>(Unicode.find(");")) + 1;
		if (!Expect(Columns.LoadSource(Unicode, "unicode.vs").bSuccess &&
		            Columns.GetUtf16Column("unicode.vs", 1, ByteColumn) == ByteColumn - 6 &&
		            Columns.GetUtf16Column("unicode.vs", 2, 1) == 1,
		        "loaded source columns count UTF-16 units under CRLF"))
		{
			return EXIT_FAILURE;
		}

		std::string Captured;
		if (!Expect(Columns.TryGetSource("unicode.vs", Captured) && Captured == Unicode,
		        "inline main source retains its compiled text without a module environment"))
		{
			return EXIT_FAILURE;
		}

		Columns.Clear();
		if (!Expect(!Columns.TryGetSource("unicode.vs", Captured) && Captured.empty(),
		        "clear releases the inline main source snapshot"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(Columns.GetUtf16Column("unicode.vs", 1, ByteColumn) == ByteColumn,
		        "clear releases the previous source coordinate snapshot"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(Columns.LoadSource(std::string("\xef\xbb\xbf") + Unicode, "unicode.vs").bSuccess &&
		            Columns.GetUtf16Column("unicode.vs", 1, ByteColumn) == ByteColumn - 6,
		        "a leading encoding BOM does not contribute to compiler columns"))
		{
			return EXIT_FAILURE;
		}

		FDebugLoadOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		const std::string Imported = "export int Value(){print(\"中文😀\");return 1;}";
		Options.SourceOverlays = {{"unicode/library.vs", Imported}};
		if (!Expect(
		        Columns.LoadSource("import \"library.vs\";void main(){Value();}", "unicode/main.vs", Options)
		                .bSuccess &&
		            Columns.GetUtf16Column(
		                "unicode/library.vs", 1, static_cast<std::uint32_t>(Imported.find("return")) + 1) ==
		                static_cast<std::uint32_t>(Imported.find("return")) + 1 - 6,
		        "imported overlay coordinates use the compiled source snapshot"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(!Columns.TryGetSource("unicode.vs", Captured) && Captured.empty() &&
		            Columns.TryGetSource("unicode/main.vs", Captured) &&
		            Captured == "import \"library.vs\";void main(){Value();}",
		        "reload replaces the main source snapshot and discards the previous label"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		VeroScriptDebugSession CancelledLoad;
		VeroScript::Test::Configure(CancelledLoad);
		if (!Expect(CancelledLoad.LoadSource("void main(){print(1);}", "cancel-old.vs").bSuccess,
		        "load before a cancelled replacement"))
		{
			return EXIT_FAILURE;
		}

		bool Cancelled = false;
		{
			VeroScript::FAnalysisControlScope Control(
			    []
			{
				return true;
			});
			try
			{
				(void)CancelledLoad.LoadSource("void main(){print(2);}", "cancel-new.vs");
			}
			catch (const VeroScript::FAnalysisCancelled&)
			{
				Cancelled = true;
			}
		}
		if (!Expect(Cancelled && !CancelledLoad.IsLoaded() && !CancelledLoad.Run().bSuccess,
		        "a cancelled reload discards previous bytecode and source state"))
		{
			return EXIT_FAILURE;
		}

		FDebugLoadOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.SourceOverlays = {{"cancel-file.vs", "void main(){print(3);}"}};
		if (!Expect(CancelledLoad.LoadFile("cancel-file.vs", Options).bSuccess,
		        "a valid file overlay loads after source cancellation"))
		{
			return EXIT_FAILURE;
		}

		CancelledLoad.SetBreakpoint("cancel-file.vs", 1);
		Cancelled = false;
		{
			VeroScript::FAnalysisControlScope Control(
			    []
			{
				return true;
			});
			try
			{
				(void)CancelledLoad.LoadFile("cancel-file.vs", Options);
			}
			catch (const VeroScript::FAnalysisCancelled&)
			{
				Cancelled = true;
			}
		}
		if (!Expect(Cancelled && !CancelledLoad.IsLoaded() && !CancelledLoad.Run().bSuccess &&
		            CancelledLoad.BindBreakpoint("cancel-file.vs", 1).Id == 0,
		        "file cancellation releases bytecode, maps and breakpoints"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(CancelledLoad.LoadSource("void main(){}", "after-cancel.vs").bSuccess &&
		            CancelledLoad.Run().bSuccess,
		        "a debug session remains usable after cancellation"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		using namespace VeroScript::Tooling;
		bool Passed = true;
		const std::string OldSource = "void main(){print(11);}";
		for (const bool FileLoad : {false, true})
		{
			for (const bool Executed : {false, true})
			{
				for (const bool ConflictingSelection : {false, true})
				{
					VeroScriptDebugSession Session;
					Test::Configure(Session);
					const auto Domain = Session.GetVmExecutionDomain();
					const auto Factory = Domain->GetFactory();
					FDebugLoadOptions Good;
					Good.SourceOverlays = {{"vm-load-old.vs", OldSource}};
					const auto Load = [&](const FDebugLoadOptions& Options)
					{
						return FileLoad ? Session.LoadFile("vm-load-old.vs", Options)
						                : Session.LoadSource(OldSource, "vm-load-old.vs", Options);
					};
					if (!Expect(Load(Good).bSuccess && (!Executed || Session.Run().bSuccess),
					        "prepare VM selection failure regression"))
					{
						return EXIT_FAILURE;
					}
					Session.SetBreakpoint("vm-load-old.vs", 1);
					FDebugLoadOptions Bad = Good;
					Bad.VmBackendPath = "__tooling_missing_vm_backend__.dll";
					if (ConflictingSelection)
					{
						Bad.VmBackendFactory = Factory;
					}
					const auto Rejected = Load(Bad);
					std::string Captured;
					const std::string Case = std::string(FileLoad ? "file" : "source") + (Executed ? "/frozen" : "/idle") + (ConflictingSelection ? "/conflict" : "/invalid-path");
					Passed = Expect(!Rejected.bSuccess && !Rejected.Diagnostics.empty() && !Session.IsLoaded() && !Session.TryGetSource("vm-load-old.vs", Captured) && Session.BindBreakpoint("vm-load-old.vs", 1).Id == 0,
					             ("failed VM selection clears old program/source/breakpoints: " + Case).c_str()) &&
					    Passed;
					Passed = Expect(Domain == Session.GetVmExecutionDomain() && Domain->GetFactory() == Factory && Domain->IsFrozen() == Executed, "failed selection preserves the VM domain") && Passed;
					Passed = Expect(!Session.Run().bSuccess, ("failed VM load cannot run old program: " + Case).c_str()) && Passed;
					Passed = Expect(Load(Good).bSuccess && Session.Run().bSuccess,
					             "VM selection failure leaves the session reloadable") &&
					    Passed;
				}
			}
		}
		const auto BackendOverride = Environment::GetUtf8("VEROSCRIPT_TEST_VM_BACKEND");
		std::string BackendPath = BackendOverride.value_or("");
		#ifdef VEROSCRIPT_TEST_VM_LIBRARY
		if (!BackendOverride)
		{
			BackendPath = VEROSCRIPT_TEST_VM_LIBRARY;
		}
		#endif
		std::ostringstream Output;
		VeroScriptVm Probe(Output);
		std::string Error;
		if (!Expect(!BackendPath.empty() && Probe.SetVmBackendPath(BackendPath, Error), "resolve test VM library path")) return EXIT_FAILURE;
		const std::string Library = Probe.GetVmExecutionDomain()->GetLibraryPath();
		const std::string Directory = PathUtil::ToUtf8(PathUtil::FromUtf8(Library).parent_path());
		for (const bool FileLoad : {false, true})
		{
			VeroScriptDebugSession Session;
			FDebugLoadOptions Options;
			Options.VmBackendPath = Directory;
			Options.SourceOverlays = {{"vm-path.vs", "void main(){print(7);}"}};
			const auto Load = [&]()
			{
				return FileLoad ? Session.LoadFile("vm-path.vs", Options)
				                : Session.LoadSource("void main(){print(7);}", "vm-path.vs", Options);
			};
			if (!Expect(Load().bSuccess && Session.Run().bSuccess, "load and execute with a VM directory")) return EXIT_FAILURE;
			const auto Domain = Session.GetVmExecutionDomain();
			const auto Factory = Domain->GetFactory();
			for (const std::string& Path : {Directory, Directory + "/.", Library})
			{
				Session.Clear();
				Options.VmBackendPath = Path;
				const auto Reloaded = Load();
				const auto ReloadRun = Session.Run();
				Passed = Expect(Reloaded.bSuccess && ReloadRun.bSuccess && ReloadRun.Output == "7\n" && Session.GetVmExecutionDomain() == Domain && Domain->GetFactory() == Factory,
				             "same VM path/directory aliases reload without changing a frozen domain") &&
				    Passed;
			}
		}
		VeroScriptDebugSession AliasedError;
		Error = Library;
		Passed = Expect(AliasedError.SetVmBackendPath(Error, Error) && Error.empty() && AliasedError.GetVmExecutionDomain()->GetFactory(), "VM path can borrow the error buffer") && Passed;
		VeroScriptDebugSession ExternallySelected;
		FDebugLoadOptions Options;
		Options.VmBackendPath = Directory;
		if (!Expect(ExternallySelected.LoadSource("void main(){}", "vm-owner.vs", Options).bSuccess,
		        "prepare shared-domain selection regression"))
		{
			return EXIT_FAILURE;
		}
		VeroScriptVm ExternalOwner(Output);
		if (!Expect(ExternalOwner.SetVmExecutionDomain(ExternallySelected.GetVmExecutionDomain(), Error) && ExternalOwner.SetVmBackendFactory(Test::VmFactory(), Error), "select a VM through the shared idle domain"))
		{
			return EXIT_FAILURE;
		}
		Passed = Expect(ExternallySelected.LoadSource("void main(){}", "vm-owner.vs", Options).bSuccess && PathUtil::IdentityKey(ExternallySelected.GetVmExecutionDomain()->GetLibraryPath()) == PathUtil::IdentityKey(Library),
		             "a previous path selection cannot hide a factory change in the shared domain") &&
		    Passed;
		if (!Passed) return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

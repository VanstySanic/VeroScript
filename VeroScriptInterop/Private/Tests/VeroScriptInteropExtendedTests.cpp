// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	vs_interop_string_view View(const char* Text)
	{
		return {Text, static_cast<std::uint64_t>(std::strlen(Text))};
	}

	vs_interop_string_view View(const std::string& Text)
	{
		return {Text.data(), static_cast<std::uint64_t>(Text.size())};
	}

	std::string Utf8Path(const std::filesystem::path& Path)
	{
		const auto Bytes = Path.generic_u8string();
		return std::string(Bytes.begin(), Bytes.end());
	}

	bool Expect(const bool Condition, const char* Message)
	{
		if (Condition)
		{
			return true;
		}
		std::cerr << "FAILED: " << Message << '\n';
		return false;
	}

	bool TestAnnotationNamePolicies()
	{
		const char* Backend = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
		for (const bool UseJit : {false, true})
		{
			if (UseJit && (!Backend || !*Backend))
			{
				continue;
			}

			struct FSessionOwner
			{
				~FSessionOwner()
				{
					if (Handle)
					{
						(void)vs_interop_session_destroy(Handle);
					}
				}

				vs_interop_session* Handle = nullptr;
			} Session;
			if (!Expect(vs_interop_session_create(nullptr, &Session.Handle) == VS_INTEROP_STATUS_OK,
			        "annotation policy session"))
			{
				return false;
			}

			vs_interop_config Config{sizeof(Config)};
			Config.execution_backend = UseJit ? VS_INTEROP_BACKEND_JIT : VS_INTEROP_BACKEND_VM;
			if ((UseJit &&
			        vs_interop_session_set_jit_backend(Session.Handle, View(Backend)) !=
			            VS_INTEROP_STATUS_OK) ||
			    vs_interop_session_apply_config(Session.Handle, &Config) != VS_INTEROP_STATUS_OK)
			{
				return Expect(false, "annotation policy execution backend");
			}

			vs_interop_annotation_desc Definition{};
			Definition.struct_size = sizeof(Definition);
			Definition.name = View("@Strict");
			for (const auto Size : {std::uint32_t{0},
			         static_cast<std::uint32_t>(offsetof(vs_interop_annotation_desc, name_flags)),
			         static_cast<std::uint32_t>(sizeof(Definition) - 1)})
			{
				Definition.struct_size = Size;
				if (!Expect(vs_interop_session_register_annotation(Session.Handle, &Definition) ==
				            VS_INTEROP_STATUS_INVALID_ARGUMENT,
				        "incomplete annotation descriptors are rejected"))
				{
					return false;
				}
			}
			Definition.struct_size = sizeof(Definition);
			Definition.name_flags = UINT32_C(0x80000000);
			if (!Expect(vs_interop_session_register_annotation(Session.Handle, &Definition) ==
			            VS_INTEROP_STATUS_INVALID_ARGUMENT,
			        "annotation flags validate before registration"))
			{
				return false;
			}

			Definition.name_flags = 0;
			if (!Expect(vs_interop_session_register_annotation(Session.Handle, &Definition) ==
			            VS_INTEROP_STATUS_OK,
			        "annotation names default to exact matching"))
			{
				return false;
			}

			Definition.name = View("@Flexible");
			Definition.name_flags = VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE;
			if (!Expect(vs_interop_session_register_annotation(Session.Handle, &Definition) ==
			            VS_INTEROP_STATUS_OK,
			        "annotation case folding is explicit"))
			{
				return false;
			}

			Definition.name = View("@fLEXIBLE");
			Definition.name_flags = 0;
			if (!Expect(vs_interop_session_register_annotation(Session.Handle, &Definition) ==
			            VS_INTEROP_STATUS_ENGINE_ERROR,
			        "exact registration cannot overlap a folded name"))
			{
				return false;
			}

			Definition.name = View("@sTRICT");
			Definition.name_flags = VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE;
			if (!Expect(vs_interop_session_register_annotation(Session.Handle, &Definition) ==
			            VS_INTEROP_STATUS_ENGINE_ERROR,
			        "folded registration cannot overlap an exact name"))
			{
				return false;
			}

			Definition.name = View("@sTRICT");
			Definition.name_flags = 0;
			if (!Expect(vs_interop_session_register_annotation(Session.Handle, &Definition) ==
			            VS_INTEROP_STATUS_OK,
			        "distinct exact spellings can coexist after a rejected registration"))
			{
				return false;
			}

			const char* Source = R"vs(
	@Strict @fLeXiBlE(Name=3) @STRICT @Unknown class Tagged {};
	void main() {
	    type<Tagged> t=type<Tagged>();
	    assert(t.annotation_count()==4);
	    assert(t.has_annotation("Strict") && !t.has_annotation("strict"));
	    assert(t.has_annotation("FLEXIBLE") && t.has_annotation("@flexible"));
	    assert(t.has_annotation("Unknown") && !t.has_annotation("unknown"));
	    assert(t.annotation(0).definition().case_sensitive());
	    annotation_info a=t.annotation(1);
	    assert(a.name()=="@fLeXiBlE" && a.matches_name("@FLEXIBLE"));
	    assert(a.definition().name()=="@Flexible" && !a.definition().case_sensitive());
	    assert(len(a.find_arguments("Name"))==1 && len(a.find_arguments("name"))==0);
	    assert(!t.annotation(2).definition().is_valid());
	    assert(t.metadata("Name")=="3" && !t.has_metadata("name"));
	}
	)vs";
			vs_interop_run_result Result{sizeof(Result)};
			if (!Expect(vs_interop_session_load_source(Session.Handle, View(Source),
			                View("annotation-policy.vs"), &Result) == VS_INTEROP_STATUS_OK &&
			            Result.success &&
			            vs_interop_session_run(Session.Handle, VS_INTEROP_RUN_SCRIPT, &Result) ==
			                VS_INTEROP_STATUS_OK &&
			            Result.success,
			        "C annotation policies reach script reflection without changing source spelling"))
			{
				return false;
			}

			if (UseJit)
			{
				vs_interop_jit_stats Stats{sizeof(Stats)};
				if (!Expect(vs_interop_session_get_last_jit_stats(Session.Handle, &Stats) ==
				                VS_INTEROP_STATUS_OK &&
				            Stats.dispatches != 0,
				        "annotation reflection executes through the selected external JIT"))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool TestConsole()
	{
		struct FState
		{
			std::vector<uint32_t> Commands;
			std::string Text;
		} State;
		vs_interop_session_options Options{};
		Options.struct_size = sizeof(Options);
		Options.user_data = &State;
		Options.console_query_callback = [](vs_interop_console_info* Info, void*) -> uint8_t
		{
			Info->flags = 15;
			Info->width = 80;
			Info->height = 25;
			return 1;
		};
		Options.console_control_callback = [](uint32_t Op, int32_t, int32_t, void* Data) -> uint8_t
		{
			static_cast<FState*>(Data)->Commands.push_back(Op);
			return 1;
		};
		Options.console_write_callback = [](vs_interop_string_view Text, void* Data) -> uint8_t
		{
			static_cast<FState*>(Data)->Text.append(Text.data, static_cast<size_t>(Text.size));
			return 1;
		};
		vs_interop_session* Session = nullptr;
		if (!Expect(vs_interop_session_create(&Options, &Session) == VS_INTEROP_STATUS_OK, "console session")) return false;
		vs_interop_run_result Result{sizeof(Result)};
		const auto Loaded = vs_interop_session_load_source(Session, View("void main() { assert(std::console::width() == 80); assert(std::console::supports_color()); "
		                                                                 "std::console::write(\"A\"); std::console::write(\"B\"); std::console::set_color(\"red\", \"default\"); "
		                                                                 "std::console::set_cursor_visible(false); std::console::clear(); }"),
		    View("console.vs"), &Result);
		const auto Ran = Loaded == VS_INTEROP_STATUS_OK ? vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) : Loaded;
		vs_interop_session_destroy(Session);
		return Expect(Ran == VS_INTEROP_STATUS_OK && Result.success, "C ABI terminal callbacks execute") && Expect(State.Text == "AB", "C ABI raw output adds no newline") && Expect(State.Commands == std::vector<uint32_t>{2, 5, 0, 3, 5}, "C ABI terminal cleanup before releasing host");
	}

	bool TestConsoleFailureRecovery()
	{
		struct FState
		{
			std::vector<uint32_t> Commands;
			bool FailWrite = true;
			bool FailQuery = false;
			bool ThrowWrite = false;
		};
		for (const bool ThrowWrite : {false, true})
		{
			FState State;
			State.ThrowWrite = ThrowWrite;
			vs_interop_session_options Options{};
			Options.struct_size = sizeof(Options);
			Options.user_data = &State;
			Options.console_query_callback = [](vs_interop_console_info* Info, void* Data) -> uint8_t
			{
				if (static_cast<FState*>(Data)->FailQuery)
				{
					throw std::runtime_error("query failed");
				}

				Info->flags = VS_INTEROP_CONSOLE_COLOR | VS_INTEROP_CONSOLE_CURSOR;
				Info->width = 80;
				return 1;
			};
			Options.console_control_callback = [](uint32_t Op, int32_t, int32_t, void* Data) -> uint8_t
			{
				auto& State = *static_cast<FState*>(Data);
				State.Commands.push_back(Op);
				if (State.FailQuery && Op == VS_INTEROP_CONSOLE_OP_RESET_STYLE)
				{
					throw std::runtime_error("reset failed");
				}

				return 1;
			};
			Options.console_write_callback = [](vs_interop_string_view, void* Data) -> uint8_t
			{
				auto& State = *static_cast<FState*>(Data);
				if (!State.FailWrite) return 1;
				State.FailQuery = true;
				if (State.ThrowWrite)
				{
					throw std::runtime_error("write failed");
				}

				return 0;
			};
			vs_interop_session* Session = nullptr;
			if (!Expect(vs_interop_session_create(&Options, &Session) == VS_INTEROP_STATUS_OK,
			        "console failure session"))
			{
				return false;
			}

			vs_interop_run_result Result{sizeof(Result)};
			const auto Loaded = vs_interop_session_load_source(Session, View("void main() { assert(std::console::set_color(\"red\", \"default\")); "
			                                                                 "assert(std::console::set_cursor_visible(false)); std::console::write(\"text\"); }"),
			    View("console-failure.vs"), &Result);
			bool Passed = Loaded == VS_INTEROP_STATUS_OK && vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && Result.success == 0 && Result.diagnostic_count != 0;
			vs_interop_diagnostic Diagnostic{sizeof(Diagnostic)};
			Passed &= vs_interop_session_get_diagnostic(Session, 0, &Diagnostic) == VS_INTEROP_STATUS_OK && Diagnostic.message.data && std::string(Diagnostic.message.data, static_cast<std::size_t>(Diagnostic.message.size)).find("failed to write output") != std::string::npos;
			Passed &= State.Commands == std::vector<uint32_t>{2, 5, 3, 5};
			// A query exception is contained, and cleanup must not depend on another query.
			Passed &= vs_interop_session_load_source(Session, View("void main() { assert(std::console::width() == 0); assert(!std::console::supports_color()); }"),
			              View("console-query-failure.vs"), &Result) == VS_INTEROP_STATUS_OK &&
			    vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success != 0 && Result.diagnostic_count == 0;
			State.FailWrite = State.FailQuery = false;
			Passed &= vs_interop_session_load_source(Session, View("void main() { assert(std::console::width() == 80); std::console::write(\"retry\"); }"),
			              View("console-retry.vs"), &Result) == VS_INTEROP_STATUS_OK &&
			    vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success != 0 && Result.diagnostic_count == 0;
			Passed &= vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK;
			if (!Expect(Passed, "console failure diagnostics, independent cleanup and session reuse")) return false;
		}
		return true;
	}

	bool TestConsoleOptionsPrefix()
	{
		// The last byte before each field's end must not enable that callback.
		const std::size_t QueryEnd = offsetof(vs_interop_session_options, console_query_callback) + sizeof(vs_interop_console_query_callback);
		const std::size_t ControlEnd = offsetof(vs_interop_session_options, console_control_callback) + sizeof(vs_interop_console_control_callback);
		const std::size_t WriteEnd = offsetof(vs_interop_session_options, console_write_callback) + sizeof(vs_interop_console_write_callback);
		for (const std::size_t Size : {offsetof(vs_interop_session_options, console_query_callback),
		         QueryEnd - 1, QueryEnd, ControlEnd - 1, ControlEnd, WriteEnd - 1, WriteEnd})
		{
			struct FState
			{
				std::string Printed, Written;
			} State;
			vs_interop_session_options Options{};
			Options.struct_size = static_cast<uint32_t>(Size);
			Options.user_data = &State;
			Options.print_callback = [](vs_interop_string_view Text, void* Data)
			{
				static_cast<FState*>(Data)->Printed.append(Text.data, static_cast<std::size_t>(Text.size));
			};
			Options.console_query_callback = [](vs_interop_console_info* Info, void*) -> uint8_t
			{
				Info->flags = VS_INTEROP_CONSOLE_COLOR;
				Info->width = 80;
				return 1;
			};
			Options.console_control_callback = [](uint32_t, int32_t, int32_t, void*) -> uint8_t
			{
				return 1;
			};
			Options.console_write_callback = [](vs_interop_string_view Text, void* Data) -> uint8_t
			{
				static_cast<FState*>(Data)->Written.append(Text.data, static_cast<std::size_t>(Text.size));
				return 1;
			};
			vs_interop_session* Session = nullptr;
			if (!Expect(vs_interop_session_create(&Options, &Session) == VS_INTEROP_STATUS_OK,
			        "console options prefix session"))
			{
				return false;
			}

			const std::string Source =
			    "void main() { assert(std::console::width() == " + std::to_string(Size >= QueryEnd ? 80 : 0) +
			    "); assert(std::console::supports_color() == " + (Size >= ControlEnd ? "true" : "false") +
			    "); std::console::write(\"AB\"); }";
			vs_interop_run_result Result{sizeof(Result)};
			bool Passed = vs_interop_session_load_source(Session, View(Source), View("console-prefix.vs"), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success;
			Passed &= vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK;
			Passed &= Size >= WriteEnd ? State.Written == "AB" && State.Printed.empty()
			                           : State.Printed == "AB" && State.Written.empty();
			if (!Expect(Passed, "console options read only complete trailing fields")) return false;
		}
		return true;
	}

	bool HasTestJitBackend()
	{
		const char* Path = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
		return Path && *Path;
	}

	vs_interop_session* CreateSession()
	{
		vs_interop_session_options Options{};
		Options.struct_size = sizeof(Options);
		vs_interop_session* Session = nullptr;
		if (vs_interop_session_create(&Options, &Session) != VS_INTEROP_STATUS_OK) return nullptr;
		const char* Path = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
		if (Path && *Path && vs_interop_session_set_jit_backend(Session, View(Path)) != VS_INTEROP_STATUS_OK)
		{
			vs_interop_session_destroy(Session);
			return nullptr;
		}
		return Session;
	}

	struct FTeardownProbe
	{
		vs_interop_session* Session = nullptr;
		bool ClosedHandleRejected = true;
		int NativeCalls = 0;
		int Interceptions = 0;
		int Mutations = 0;
	};

	vs_interop_callback_result VS_INTEROP_CALL ObserveTeardown(
	    vs_interop_call_context*, vs_interop_value*, void* UserData)
	{
		auto& Probe = *static_cast<FTeardownProbe*>(UserData);
		++Probe.NativeCalls;
		if (Probe.Session)
		{
			Probe.ClosedHandleRejected &= vs_interop_session_clear(Probe.Session) == VS_INTEROP_STATUS_INVALID_ARGUMENT;
			Probe.ClosedHandleRejected &= vs_interop_session_destroy(Probe.Session) == VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_intercept_result VS_INTEROP_CALL InterceptTeardown(
	    vs_interop_intercept_context*, vs_interop_value*, void* UserData)
	{
		++static_cast<FTeardownProbe*>(UserData)->Interceptions;
		return VS_INTEROP_INTERCEPT_UNHANDLED;
	}

	void VS_INTEROP_CALL ObserveTeardownMutation(
	    const vs_interop_mutation_event* Event, void* UserData)
	{
		int32_t Value = 0;
		if (Event && Event->kind == VS_INTEROP_MUTATION_FIELD_WRITE && Event->value && vs_interop_value_get_int32(Event->value, &Value) == VS_INTEROP_STATUS_OK && Value == 9)
		{
			++static_cast<FTeardownProbe*>(UserData)->Mutations;
		}
	}

	bool TestSessionTeardownCallbacks()
	{
		FTeardownProbe Probe;
		vs_interop_session* Session = CreateSession();
		vs_interop_native_type_desc Void{};
		Void.struct_size = sizeof(Void);
		Void.type = VS_INTEROP_NATIVE_VOID;
		vs_interop_run_result Result{};
		Result.struct_size = sizeof(Result);
		const bool bSetup = Session && vs_interop_session_set_hot_reload_enabled(Session, 1) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(Session, View("observe_teardown"), &Void, nullptr, 0, &ObserveTeardown, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_set_instance_call_interceptor(Session, &InterceptTeardown, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_set_mutation_callback(Session, &ObserveTeardownMutation, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Session, View("class Trace {public: int value; void Touch(){value=9;}"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           "~Trace(){Touch();observe_teardown();}};"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           "Trace root=Trace(); void main(){}"),
		                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  View("interop-teardown.vs"), &Result) == VS_INTEROP_STATUS_OK &&
		    vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK;
		Probe = {};
		Probe.Session = Session;
		const bool bDestroyed = vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK;
		return Expect(bSetup && bDestroyed && Probe.NativeCalls == 1 && Probe.Interceptions > 0 && Probe.Mutations == 1 && Probe.ClosedHandleRejected,
		    "session teardown keeps native and hook state alive through script destructors");
	}

	bool TestProcessDirectoryCapability()
	{
		struct FCase
		{
			std::uint32_t Allowed;
			bool bPolicyEnabled;
			const char* RequiredCapability;
		};
		const FCase Cases[] = {
		    {0, true, "environment write"},
		    {VS_INTEROP_SCRIPT_CAP_ENVIRONMENT_WRITE, true, "process directory change"},
		    {VS_INTEROP_SCRIPT_CAP_PROCESS_DIRECTORY_CHANGE, true, "environment write"},
		    {VS_INTEROP_SCRIPT_CAP_ENVIRONMENT_WRITE | VS_INTEROP_SCRIPT_CAP_PROCESS_DIRECTORY_CHANGE, true, nullptr},
		    {0, false, nullptr}};
		for (const FCase& Case : Cases)
		{
			vs_interop_session* Session = CreateSession();
			if (!Expect(Session != nullptr, "directory capability session creation"))
			{
				return false;
			}
			vs_interop_config Config{};
			Config.struct_size = sizeof(Config);
			Config.execution_backend = HasTestJitBackend() ? VS_INTEROP_BACKEND_JIT : VS_INTEROP_BACKEND_MIXED;
			Config.capability_policy_enabled = Case.bPolicyEnabled ? 1u : 0u;
			Config.allowed_script_capabilities = Case.Allowed;
			vs_interop_run_result Result{sizeof(Result)};
			const std::string Source =
			    "import \"io.vs\"; void main() { std::filesystem::chdir(\".\"); }";
			const bool bLoaded = vs_interop_session_apply_config(Session, &Config) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Session, View(Source), View("directory-capability.vs"), &Result) == VS_INTEROP_STATUS_OK;
			const vs_interop_status RunStatus = bLoaded
			    ? vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result)
			    : VS_INTEROP_STATUS_INVALID_ARGUMENT;
			bool bDiagnosticMatches = true;
			if (bLoaded && Case.RequiredCapability)
			{
				vs_interop_diagnostic Diagnostic{sizeof(Diagnostic)};
				bDiagnosticMatches = Result.diagnostic_count != 0 && vs_interop_session_get_diagnostic(Session, 0, &Diagnostic) == VS_INTEROP_STATUS_OK && Diagnostic.message.data != nullptr && std::string(Diagnostic.message.data, static_cast<std::size_t>(Diagnostic.message.size)).find(Case.RequiredCapability) != std::string::npos;
			}
			const bool bPassed = bLoaded && bDiagnosticMatches && (Case.RequiredCapability ? RunStatus == VS_INTEROP_STATUS_ENGINE_ERROR && Result.success == 0 : RunStatus == VS_INTEROP_STATUS_OK && Result.success != 0 && Result.diagnostic_count == 0);
			const bool bDestroyed = vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK;
			if (!Expect(bPassed, "independent process directory capability"))
			{
				return false;
			}
			if (!Expect(bDestroyed, "directory capability session destruction"))
			{
				return false;
			}
		}
		vs_interop_session* Session = CreateSession();
		if (!Expect(Session != nullptr, "directory capability validation session"))
		{
			return false;
		}
		vs_interop_config Config{};
		Config.struct_size = sizeof(Config);
		Config.capability_policy_enabled = 1;
		Config.allowed_script_capabilities = VS_INTEROP_SCRIPT_CAP_ALL | UINT32_C(0x80000000);
		const bool bRejected = vs_interop_session_apply_config(Session, &Config) == VS_INTEROP_STATUS_INVALID_ARGUMENT;
		const bool bDestroyed = vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK;
		if (!Expect(bRejected, "unknown script capability bit rejection"))
		{
			return false;
		}
		if (!Expect(bDestroyed, "capability validation session destruction"))
		{
			return false;
		}
		return true;
	}

	std::vector<std::uint8_t> CopyBuffer(const vs_interop_buffer* Buffer)
	{
		std::uint64_t Size = 0;
		const vs_interop_status QueryStatus =
		    vs_interop_buffer_copy(Buffer, nullptr, 0, &Size);
		if (QueryStatus != (Size == 0 ? VS_INTEROP_STATUS_OK : VS_INTEROP_STATUS_BUFFER_TOO_SMALL))
		{
			return {};
		}
		std::vector<std::uint8_t> Result(static_cast<std::size_t>(Size));
		if (vs_interop_buffer_copy(Buffer, Result.data(), Size, &Size) != VS_INTEROP_STATUS_OK)
		{
			return {};
		}
		return Result;
	}

	struct FValue
	{
		FValue()
		{
			(void)vs_interop_value_create(&Handle);
		}

		~FValue()
		{
			vs_interop_value_destroy(Handle);
		}

		vs_interop_value* Handle = nullptr;
	};

	bool TestProtocolCallbackLifetime(const bool Generic)
	{
		struct FProbe
		{
			int Releases = 0, Clones = 0;
		} Probe;
		struct FConstructor
		{
			uint32_t TypeId = 0;
			const char* Name;
		} Constructors[2]{{0, "EarlyKey"}, {0, Generic ? "LateKey<int>" : "LateKey"}};
		struct FFactory
		{
			vs_interop_host_type_desc Type{};
			vs_interop_host_member_desc Member{};
			vs_interop_host_type_protocol Protocol{};
		} Factory;
		vs_interop_session* Session = CreateSession();
		if (!Expect(Session != nullptr, "protocol lifetime session")) return false;
		FValue Value;
		FValue Returned;
		bool Passed = Value.Handle != nullptr && Returned.Handle != nullptr;
		const auto Check = [&](vs_interop_status Status, const char* Stage)
		{
			if (Status == VS_INTEROP_STATUS_OK) return true;
			char Error[1024]{};
			uint64_t Required = 0;
			vs_interop_session_copy_last_error(Session, Error, sizeof(Error), &Required);
			std::cerr << "Protocol lifetime " << Stage << ": status=" << Status << " " << Error << '\n';
			return false;
		};
		for (auto& Constructor : Constructors)
		{
			vs_interop_host_type_desc Type{};
			Type.struct_size = sizeof(Type);
			Type.name = View(Constructor.Name);
			Type.kind = VS_INTEROP_HOST_STRUCT;
			vs_interop_host_member_desc Member{};
			Member.struct_size = sizeof(Member);
			Member.kind = VS_INTEROP_HOST_CONSTRUCTOR;
			Member.return_type.struct_size = sizeof(Member.return_type);
			Member.return_type.type = VS_INTEROP_NATIVE_HOST_OBJECT;
			Member.return_type.name = Type.name;
			Member.user_data = &Constructor;
			Member.callback = [](vs_interop_call_context*, vs_interop_value* Out, void* Data) -> vs_interop_callback_result
			{
				const auto& Constructor = *static_cast<FConstructor*>(Data);
				return vs_interop_value_set_host_object(Out, Constructor.TypeId, View(Constructor.Name), Data, nullptr) == VS_INTEROP_STATUS_OK ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
			};
			vs_interop_host_type_protocol Protocol{};
			Protocol.struct_size = sizeof(Protocol);
			Protocol.clone_user_data = &Probe;
			Protocol.clone_callback = [](vs_interop_call_context* Context, vs_interop_value* Out, void* Data) -> vs_interop_callback_result
			{
				auto& Probe = *static_cast<FProbe*>(Data);
				if (Probe.Releases != 0) return VS_INTEROP_CALLBACK_ERROR;
				++Probe.Clones;
				const vs_interop_value* Source = nullptr;
				uint32_t Id = 0;
				vs_interop_string_view Name{};
				void* Payload = nullptr;
				return vs_interop_call_context_get_argument(Context, 0, &Source) == VS_INTEROP_STATUS_OK && vs_interop_value_get_host_object(Source, &Id, &Name, &Payload) == VS_INTEROP_STATUS_OK && vs_interop_value_set_host_object(Out, Id, Name, Payload, nullptr) == VS_INTEROP_STATUS_OK
				    ? VS_INTEROP_CALLBACK_OK
				    : VS_INTEROP_CALLBACK_ERROR;
			};
			if (Generic && &Constructor != Constructors)
			{
				Factory.Type = Type;
				Factory.Member = Member;
				Factory.Protocol = Protocol;
				uint32_t TemplateId = 0;
				const auto Build = +[](vs_interop_generic_type_context* Context, void* Data) -> vs_interop_callback_result
				{
					const auto& Factory = *static_cast<FFactory*>(Data);
					return vs_interop_generic_type_context_set_type(Context, &Factory.Type) == VS_INTEROP_STATUS_OK && vs_interop_generic_type_context_add_member(Context, &Factory.Member) == VS_INTEROP_STATUS_OK && vs_interop_generic_type_context_set_protocol(Context, &Factory.Protocol) == VS_INTEROP_STATUS_OK
					    ? VS_INTEROP_CALLBACK_OK
					    : VS_INTEROP_CALLBACK_ERROR;
				};
				const vs_interop_string_view Argument = View("int");
				Passed &= Check(vs_interop_session_register_generic_host_type_template(Session, View("LateKey"), 1,
				                    {}, Build, &Factory, &TemplateId),
				              "register generic") &&
				    Check(vs_interop_session_instantiate_generic_host_type(Session, TemplateId, {&Argument, 1},
				              &Constructor.TypeId),
				        "instantiate generic");
			}
			else
			{
				Passed &= Check(vs_interop_session_register_host_type(Session, &Type, &Constructor.TypeId), "register type") && Check(vs_interop_session_register_host_member(Session, Constructor.TypeId, &Member), "register constructor") && Check(vs_interop_session_register_host_protocol(Session, Constructor.TypeId, &Protocol), "register clone");
			}
			if (&Constructor == Constructors)
			{
				const auto Release = +[](void* Data)
				{
					++static_cast<FProbe*>(Data)->Releases;
				};
				Passed &= vs_interop_session_retain_callback_user_data(Session, &Probe, Release) ==
				        VS_INTEROP_STATUS_OK &&
				    vs_interop_session_retain_callback_user_data(Session, &Probe, Release) ==
				        VS_INTEROP_STATUS_INVALID_ARGUMENT &&
				    Probe.Releases == 0;
			}
		}
		vs_interop_run_result Result{sizeof(Result)};
		const std::string KeyType = Constructors[1].Name;
		const std::string Source = KeyType + " make_key() { return " + KeyType + "(); } " + KeyType + " copy_key(" + KeyType + " source) { return source; } void main() {}";
		Passed &= Check(vs_interop_session_load_source(Session,
		                    View(Source),
		                    View("protocol-lifetime.vs"), &Result),
		              "load") &&
		    Check(vs_interop_session_call_function(Session, View("make_key"), nullptr, 0, Value.Handle, &Result), "construct") && Result.success;
		vs_interop_value* Arguments[] = {Value.Handle};
		Passed &= Check(vs_interop_session_call_function(Session, View("copy_key"), Arguments, 1, Returned.Handle, &Result), "call copy") && Result.success && Probe.Clones > 0 && vs_interop_value_assign(Value.Handle, Returned.Handle) == VS_INTEROP_STATUS_OK;
		vs_interop_value_reset(Returned.Handle);
		const bool SetupPassed = Passed;
		Passed &= vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK;
		const bool HeldAfterSession = Probe.Releases == 0;
		Passed &= HeldAfterSession;
		vs_interop_value* Copy = nullptr;
		Passed &= vs_interop_value_clone(Value.Handle, &Copy) == VS_INTEROP_STATUS_OK && Probe.Clones > 0;
		vs_interop_value_reset(Value.Handle);
		Passed &= Probe.Releases == 0;
		std::thread Finalizer(
		    [&]
		{
			vs_interop_value_destroy(Copy);
		});
		Finalizer.join();
		Passed &= Probe.Releases == 1;
		if (!Passed)
		{
			std::cerr << "Protocol lifetime: setup=" << SetupPassed << " held=" << HeldAfterSession
			          << " clones=" << Probe.Clones << " releases=" << Probe.Releases << '\n';
		}

		return Expect(Passed,
		    "protocol user data survives later registrations, session teardown and escaped value cloning");
	}

	bool TestExecutableValueOwnership()
	{
		struct FSessionOwner
		{
			~FSessionOwner()
			{
				if (Handle)
				{
					(void)vs_interop_session_destroy(Handle);
				}
			}

			vs_interop_session* Handle = CreateSession();
		};
		FSessionOwner First;
		FSessionOwner Second;
		if (!Expect(First.Handle && Second.Handle,
		        "executable value sessions"))
		{
			return false;
		}

		const std::string Source =
		    "import \"concurrent.vs\";\n"
		    "class Counter { public: int value = 7; }\n"
		    "Counter counter = Counter();\n"
		    "field_info<Counter> selected = "
		    "field_info<Counter>(type<Counter>().qualified_name, 0);\n"
		    "int double_value(int value) { return value * 2; }\n"
		    "fn(int)->int work = double_value;\n"
		    "int inspect(field_info<Counter> item) { return item.get<int>(counter); }\n"
		    "int execute(fn(int)->int target) { "
		    "task<int> job = std::concurrent::spawn_task<int>(target, 21); "
		    "return job.await(); }\n"
		    "void main() {}\n";
		vs_interop_run_result Result{sizeof(Result)};
		for (vs_interop_session* Session : {First.Handle, Second.Handle})
		{
			if (!Expect(vs_interop_session_set_hot_reload_enabled(Session, 1) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Session, View(Source), View("interop-executable-values.vs"), &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_session_run(Session, VS_INTEROP_RUN_EVAL, &Result) == VS_INTEROP_STATUS_OK && Result.success,
			        "executable value source initialization"))
			{
				return false;
			}
		}

		FValue Field;
		FValue Function;
		FValue FieldCopy;
		FValue FunctionCopy;
		struct FClonedValue
		{
			~FClonedValue()
			{
				vs_interop_value_destroy(Handle);
			}

			vs_interop_value* Handle = nullptr;
		};
		FClonedValue FieldClone;
		FClonedValue FunctionClone;
		FValue Return;
		if (!Expect(vs_interop_session_get_global(First.Handle, View("selected"),
		                Field.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_get_global(First.Handle, View("work"),
		                Function.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_kind(Function.Handle) == VS_INTEROP_VALUE_FUNCTION && vs_interop_value_assign(FieldCopy.Handle, Field.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_assign(FunctionCopy.Handle, Function.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_clone(FieldCopy.Handle, &FieldClone.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_clone(FunctionCopy.Handle, &FunctionClone.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(Field.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(Function.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(FieldCopy.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(FunctionCopy.Handle) == VS_INTEROP_STATUS_OK,
		        "C values retain executable identities"))
		{
			return false;
		}
		vs_interop_value* FieldArgs[] = {FieldClone.Handle};
		vs_interop_value* FunctionArgs[] = {FunctionClone.Handle};
		std::int32_t Number = 0;
		if (!Expect(vs_interop_session_call_function(First.Handle, View("inspect"),
		                FieldArgs, 1, Return.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_int32(Return.Handle, &Number) == VS_INTEROP_STATUS_OK && Number == 7 && vs_interop_session_call_function(First.Handle, View("execute"), FunctionArgs, 1, Return.Handle, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(Return.Handle, &Number) == VS_INTEROP_STATUS_OK && Number == 42,
		        "C values execute reflection and callable tasks in their owner session"))
		{
			return false;
		}

		auto ForeignCallDenied = [&](const char* FunctionName,
		                             vs_interop_value* const* Arguments)
		{
			if (vs_interop_session_call_function(Second.Handle, View(FunctionName),
			        Arguments, 1, Return.Handle, &Result) != VS_INTEROP_STATUS_ENGINE_ERROR ||
			    Result.success || Result.diagnostic_count == 0)
			{
				return false;
			}
			vs_interop_diagnostic Diagnostic{sizeof(Diagnostic)};
			return vs_interop_session_get_diagnostic(Second.Handle, 0, &Diagnostic) == VS_INTEROP_STATUS_OK && Diagnostic.message.data && std::string(Diagnostic.message.data, static_cast<std::size_t>(Diagnostic.message.size)).find("foreign") != std::string::npos;
		};
		return Expect(ForeignCallDenied("inspect", FieldArgs) && ForeignCallDenied("execute", FunctionArgs),
		    "C values reject foreign reflection and callable execution");
	}

	struct FHookState
	{
		int Intercepts = 0;
		int Mutations = 0;
	};

	vs_interop_intercept_result VS_INTEROP_CALL InterceptAdd(
	    vs_interop_intercept_context* Context,
	    vs_interop_value* OutReturn,
	    void* UserData)
	{
		auto& State = *static_cast<FHookState*>(UserData);
		vs_interop_resolved_method Target{};
		Target.struct_size = sizeof(Target);
		if (vs_interop_intercept_context_get_target(Context, &Target) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_INTERCEPT_FAILED;
		}
		const std::string Name(Target.method_name.data,
		    static_cast<std::size_t>(Target.method_name.size));
		if (Name != "Add")
		{
			return VS_INTEROP_INTERCEPT_UNHANDLED;
		}
		++State.Intercepts;
		if (vs_interop_intercept_context_proceed(Context, OutReturn) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_INTERCEPT_FAILED;
		}
		std::int32_t Value = 0;
		if (vs_interop_value_get_int32(OutReturn, &Value) != VS_INTEROP_STATUS_OK || vs_interop_value_set_int32(OutReturn, Value + 100) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_INTERCEPT_FAILED;
		}
		return VS_INTEROP_INTERCEPT_HANDLED;
	}

	enum class EArrayInterception
	{
		Forward,
		Copy,
		Fail
	};

	struct FArrayInterception
	{
		vs_interop_value* Copy = nullptr;
		EArrayInterception Mode = EArrayInterception::Forward;
	};

	vs_interop_intercept_result VS_INTEROP_CALL InterceptArrayReturn(
	    vs_interop_intercept_context* Context, vs_interop_value* Return, void* UserData)
	{
		const auto& State = *static_cast<FArrayInterception*>(UserData);
		if (vs_interop_intercept_context_proceed(Context, Return) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_INTERCEPT_FAILED;
		}
		if (State.Mode == EArrayInterception::Fail)
		{
			vs_interop_intercept_context_set_error(Context, View("fixed array interceptor failed"));
			return VS_INTEROP_INTERCEPT_FAILED;
		}
		if (State.Mode == EArrayInterception::Copy &&
		    (vs_interop_value_assign(State.Copy, Return) != VS_INTEROP_STATUS_OK ||
		        vs_interop_value_assign(Return, State.Copy) != VS_INTEROP_STATUS_OK ||
		        vs_interop_value_reset(State.Copy) != VS_INTEROP_STATUS_OK))
		{
			return VS_INTEROP_INTERCEPT_FAILED;
		}
		return VS_INTEROP_INTERCEPT_HANDLED;
	}

	bool TestInterceptedArrayForwarding()
	{
		const char* Jit = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
		for (const auto Backend : {VS_INTEROP_BACKEND_VM, VS_INTEROP_BACKEND_MIXED, VS_INTEROP_BACKEND_JIT})
		{
			if (Backend != VS_INTEROP_BACKEND_VM && (!Jit || !*Jit))
			{
				continue;
			}
			FValue Return, Copy;
			FArrayInterception State{Copy.Handle};
			const std::unique_ptr<vs_interop_session, decltype(&vs_interop_session_destroy)> Session(
			    CreateSession(), vs_interop_session_destroy);
			vs_interop_config Config{sizeof(Config)};
			Config.execution_backend = Backend;
			vs_interop_run_result Result{sizeof(Result)};
			constexpr char Source[] =
			    "int copies=0;int destroyed=0;"
			    "struct S{int value=7;S()=default;S(const S& other){value=other.value;copies++;}~S(){destroyed++;}};"
			    "using Values=S[2];class Maker{public:Values values;Values Read(){return values;}};Maker maker=Maker();"
			    "int take(){copies=0;destroyed=0;Values result=maker.Read();result[0].value=9;"
			    "return copies*100+maker.values[0].value;}"
			    "int disposed(){return destroyed;}void main(){}";
			if (!Expect(Session && Return.Handle && Copy.Handle &&
			            (Backend == VS_INTEROP_BACKEND_VM ||
			                vs_interop_session_set_jit_backend(Session.get(), View(Jit)) == VS_INTEROP_STATUS_OK) &&
			            vs_interop_session_apply_config(Session.get(), &Config) == VS_INTEROP_STATUS_OK &&
			            vs_interop_session_set_hot_reload_enabled(Session.get(), 1) == VS_INTEROP_STATUS_OK &&
			            vs_interop_session_load_source(Session.get(), View(Source),
			                View("interop-fixed-array-return.vs"), &Result) == VS_INTEROP_STATUS_OK &&
			            vs_interop_session_run(Session.get(), VS_INTEROP_RUN_EVAL, &Result) == VS_INTEROP_STATUS_OK &&
			            vs_interop_session_set_instance_call_interceptor(
			                Session.get(), InterceptArrayReturn, &State) == VS_INTEROP_STATUS_OK,
			        "initialize fixed array interceptor fixture"))
			{
				return false;
			}
			const auto Call = [&](const char* Name, const vs_interop_status Expected, const int32_t Value)
			{
				int32_t Output = 0;
				return vs_interop_session_call_function(Session.get(), View(Name), nullptr, 0,
				           Return.Handle, &Result) == Expected &&
				    (Result.success != 0) == (Expected == VS_INTEROP_STATUS_OK) &&
				    vs_interop_value_get_int32(Return.Handle, &Output) == VS_INTEROP_STATUS_OK && Output == Value;
			};
			if (!Expect(Call("take", VS_INTEROP_STATUS_OK, 207) &&
			            Call("disposed", VS_INTEROP_STATUS_OK, 2),
			        "direct C Proceed preserves the prepared array and destroys each result element once"))
			{
				return false;
			}
			State.Mode = EArrayInterception::Copy;
			if (!Expect(Call("take", VS_INTEROP_STATUS_OK, 407) &&
			            Call("disposed", VS_INTEROP_STATUS_OK, 4),
			        "reassigning a copied C array keeps the replacement copy boundary"))
			{
				return false;
			}
			State.Mode = EArrayInterception::Fail;
			if (!Expect(vs_interop_value_set_int32(Return.Handle, 19) == VS_INTEROP_STATUS_OK &&
			            Call("take", VS_INTEROP_STATUS_ENGINE_ERROR, 19) &&
			            Call("disposed", VS_INTEROP_STATUS_OK, 2),
			        "failed C interception preserves output and destroys the original array"))
			{
				return false;
			}
		}
		return true;
	}

	struct FProceedCleanupState
	{
		~FProceedCleanupState()
		{
			if (Producer)
			{
				(void)vs_interop_session_destroy(Producer);
			}
			if (Foreign)
			{
				(void)vs_interop_session_destroy(Foreign);
			}
		}
		vs_interop_session* Producer = nullptr;
		vs_interop_session* Foreign = nullptr;
		vs_interop_value* RejectedOutput = nullptr;
		vs_interop_status ProceedStatus = VS_INTEROP_STATUS_OK;
		bool BorrowedScalarRejected = false, BorrowedResetRejected = false;
		int Intercepts = 0, Destroyed = 0;
	};

	vs_interop_intercept_result VS_INTEROP_CALL RejectProceedPublication(
	    vs_interop_intercept_context* Context, vs_interop_value* OutReturn, void* UserData)
	{
		auto& State = *static_cast<FProceedCleanupState*>(UserData);
		vs_interop_resolved_method Target{sizeof(Target)};
		if (vs_interop_intercept_context_get_target(Context, &Target) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_INTERCEPT_FAILED;
		}
		if (std::string_view(Target.method_name.data, static_cast<std::size_t>(Target.method_name.size)) != "produce")
		{
			return VS_INTEROP_INTERCEPT_UNHANDLED;
		}
		++State.Intercepts;
		State.ProceedStatus = vs_interop_intercept_context_proceed(Context, State.RejectedOutput);
		int32_t Unchanged = 0;
		State.BorrowedScalarRejected = vs_interop_value_set_int32(OutReturn, 17) == VS_INTEROP_STATUS_OK && vs_interop_value_assign(OutReturn, State.RejectedOutput) == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_value_get_int32(OutReturn, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 17;
		State.BorrowedResetRejected = vs_interop_value_reset(OutReturn) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_value_assign(OutReturn, State.RejectedOutput) == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_value_get_int32(OutReturn, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 17;
		return VS_INTEROP_INTERCEPT_FAILED;
	}

	vs_interop_callback_result VS_INTEROP_CALL ObserveProceedCleanup(
	    vs_interop_call_context*, vs_interop_value*, void* UserData)
	{
		++static_cast<FProceedCleanupState*>(UserData)->Destroyed;
		return VS_INTEROP_CALLBACK_OK;
	}

	bool TestProceedPublicationFailureCleanup()
	{
		FProceedCleanupState State;
		FValue Receiver, ForeignOutput, Return, Field;
		State.Producer = CreateSession();
		State.Foreign = CreateSession();
		State.RejectedOutput = ForeignOutput.Handle;
		vs_interop_native_type_desc Void{sizeof(Void)};
		Void.type = VS_INTEROP_NATIVE_VOID;
		vs_interop_run_result Result{sizeof(Result)};
		if (!Expect(State.Producer && State.Foreign && Receiver.Handle && ForeignOutput.Handle && Return.Handle && Field.Handle && vs_interop_session_register_native(State.Producer, View("observe_proceed_cleanup"), &Void, nullptr, 0, ObserveProceedCleanup, &State) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(State.Producer, View("class Item {public:int value=42;~Item(){observe_proceed_cleanup();}};"
		                                                                                                                                                                                                                                                                                                                                                "class Producer {public:Item produce(){return Item();}};"
		                                                                                                                                                                                                                                                                                                                                                "Producer make(){return Producer();}"),
		                                                                                                                                                                                                                                                                                                View("proceed-cleanup.vs"), &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_call_function(State.Producer, View("make"), nullptr, 0, Receiver.Handle,
		                &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_load_source(State.Foreign, View("class Foreign {public:int value=99;};Foreign make(){return Foreign();}"),
		                View("foreign-proceed-output.vs"), &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_call_function(State.Foreign, View("make"), nullptr, 0, ForeignOutput.Handle,
		                &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_set_instance_call_interceptor(State.Producer, RejectProceedPublication, &State) == VS_INTEROP_STATUS_OK,
		        "prepare foreign-root proceed publication failure"))
		{
			return false;
		}
		uint64_t Before = 0, After = 0;
		if (!Expect(vs_interop_memory_object_id(State.Foreign, nullptr, ForeignOutput.Handle, &Before) == VS_INTEROP_STATUS_OK && Before != 0 && vs_interop_session_call_method(State.Producer, Receiver.Handle, View("produce"), nullptr, 0, Return.Handle, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && State.ProceedStatus == VS_INTEROP_STATUS_ENGINE_ERROR && State.Intercepts == 1 && State.Destroyed == 1 && State.BorrowedScalarRejected && State.BorrowedResetRejected,
		        "failed proceed output publication still runs returned Item cleanup once"))
		{
			return false;
		}
		int32_t Value = 0;
		return Expect(vs_interop_memory_object_id(State.Foreign, nullptr, ForeignOutput.Handle, &After) == VS_INTEROP_STATUS_OK && After == Before && vs_interop_session_get_field(State.Foreign, ForeignOutput.Handle, View("value"), Field.Handle, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(Field.Handle, &Value) == VS_INTEROP_STATUS_OK && Value == 99 && State.Destroyed == 1,
		    "rejected proceed publication preserves the foreign output identity and field");
	}

	struct FPendingInitializationCleanupState
	{
		~FPendingInitializationCleanupState()
		{
			vs_interop_value_destroy(Output);
			Output = nullptr;
			if (Session)
			{
				(void)vs_interop_session_destroy(Session);
			}
			std::error_code Error;
			std::filesystem::remove(Path, Error);
		}

		vs_interop_session* Session = nullptr;
		vs_interop_value* Output = nullptr;
		std::filesystem::path Path;
		vs_interop_status PublicationStatus = VS_INTEROP_STATUS_OK;
		bool ForwardReturn = false;
		int NestedCalls = 0, Intercepts = 0, Destroyed = 0;
	};

	vs_interop_callback_result VS_INTEROP_CALL ObservePendingInitializationCleanup(
	    vs_interop_call_context*, vs_interop_value*, void* UserData)
	{
		++static_cast<FPendingInitializationCleanupState*>(UserData)->Destroyed;
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL ProbePendingInitializationCleanup(
	    vs_interop_call_context* Context, vs_interop_value* OutReturn, void* UserData)
	{
		auto& State = *static_cast<FPendingInitializationCleanupState*>(UserData);
		++State.NestedCalls;
		State.PublicationStatus = vs_interop_call_context_invoke_function(
		    Context, View("produce_item"), nullptr, 0, State.Output);
		// The initializer fails after the nested call has returned, so rollback
		// must retire its rejected Item while replacement metadata is still live.
		return vs_interop_value_set_int32(OutReturn, 0) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}

	vs_interop_intercept_result VS_INTEROP_CALL InterceptPendingInitializationCleanup(
	    vs_interop_intercept_context* Context, vs_interop_value* OutReturn, void* UserData)
	{
		vs_interop_resolved_method Target{sizeof(Target)};
		if (vs_interop_intercept_context_get_target(Context, &Target) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_INTERCEPT_FAILED;
		}
		if (!Target.method_name.data || std::string_view(Target.method_name.data, static_cast<std::size_t>(Target.method_name.size)) != "produce")
		{
			return VS_INTEROP_INTERCEPT_UNHANDLED;
		}
		auto& State = *static_cast<FPendingInitializationCleanupState*>(UserData);
		++State.Intercepts;
		State.PublicationStatus = vs_interop_intercept_context_proceed(
		    Context, State.ForwardReturn ? OutReturn : State.Output);
		return State.ForwardReturn && State.PublicationStatus == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_INTERCEPT_HANDLED
		    : VS_INTEROP_INTERCEPT_FAILED;
	}

	bool TestPendingInitializationResultCleanup()
	{
		for (const int Mode : {0, 1, 2, 3, 4, 5})
		{
			const bool EmptyContainer = Mode >= 3;
			const bool UseProceed = Mode % 3 != 0;
			FPendingInitializationCleanupState State;
			State.ForwardReturn = Mode % 3 == 2;
			const int ExpectedDestroyed = EmptyContainer ? 0 : 1;
			State.Path = std::filesystem::temp_directory_path() / ("veroscript-interop-pending-cleanup-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(Mode) + ".vs");
			const auto WriteSource = [&State](const char* Source)
			{
				std::ofstream Out(State.Path, std::ios::binary | std::ios::trunc);
				Out << Source;
				Out.close();
				return Out.good();
			};
			State.Session = CreateSession();
			vs_interop_native_type_desc Void{sizeof(Void)}, Int{sizeof(Int)};
			Void.type = VS_INTEROP_NATIVE_VOID;
			Int.type = VS_INTEROP_NATIVE_INT32;
			vs_interop_config Config{sizeof(Config)};
			Config.execution_backend = VS_INTEROP_BACKEND_VM;
			vs_interop_run_result Result{sizeof(Result)};
			const std::string Path = Utf8Path(State.Path);
			if (!Expect(State.Session && vs_interop_value_create(&State.Output) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int32(State.Output, 17) == VS_INTEROP_STATUS_OK && vs_interop_session_apply_config(State.Session, &Config) == VS_INTEROP_STATUS_OK && vs_interop_session_set_hot_reload_enabled(State.Session, 1) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(State.Session, View("notify_pending_cleanup"), &Void, nullptr, 0, ObservePendingInitializationCleanup, &State) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(State.Session, View("probe_pending_cleanup"), &Int, nullptr, 0, ProbePendingInitializationCleanup, &State) == VS_INTEROP_STATUS_OK && (!UseProceed || vs_interop_session_set_instance_call_interceptor(State.Session, InterceptPendingInitializationCleanup, &State) == VS_INTEROP_STATUS_OK) && WriteSource("class Held{public:int value=9;};Held hold(){return Held();}int version(){return 1;}void main(){}") && vs_interop_session_load_file(State.Session, View(Path), &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_session_run(State.Session, VS_INTEROP_RUN_EVAL, &Result) == VS_INTEROP_STATUS_OK && Result.success,
			        "initialize file-backed pending-result cleanup fixture"))
			{
				return false;
			}

			vs_interop_reload_status Status{sizeof(Status) - 1};
			Status.operation_id = UINT64_MAX;
			if (!Expect((vs_interop_get_capabilities() & VS_INTEROP_CAP_RELOAD_STATUS) != 0 &&
			            vs_interop_session_get_reload_status(State.Session, &Status) == VS_INTEROP_STATUS_INVALID_ARGUMENT &&
			            Status.struct_size == sizeof(Status) - 1 && Status.operation_id == UINT64_MAX,
			        "reload status capability and undersized output preservation"))
			{
				return false;
			}
			Status.struct_size = sizeof(Status);
			if (!Expect(vs_interop_session_get_reload_status(State.Session, &Status) == VS_INTEROP_STATUS_OK &&
			            Status.state == VS_INTEROP_RELOAD_NONE && Status.operation_id == 0 &&
			            Status.current_generation != 0 && Status.committed_generation == Status.current_generation,
			        "initial generation is committed with no reload operation"))
			{
				return false;
			}
			const uint64_t OriginalGeneration = Status.current_generation;
			const char* Replacement = EmptyContainer && State.ForwardReturn
			    ? "class Item{public:int value=42;~Item(){notify_pending_cleanup();}};"
			      "class Producer{public:array<Item> produce(){return array<Item>();}};"
			      "int initialize(){Producer p=Producer();array<Item> x=p.produce();return 42;}"
			      "int initialized=initialize();int version(){return 2;}void main(){}"
			    : EmptyContainer && UseProceed
			    ? "class Item{public:int value=42;~Item(){notify_pending_cleanup();}};"
			      "class Producer{public:array<Item> produce(){return array<Item>();}};"
			      "int initialize(){Producer p=Producer();p.produce();return 0;}"
			      "int pending=initialize();int version(){return 2;}void main(){}"
			    : EmptyContainer
			    ? "class Item{public:int value=42;~Item(){notify_pending_cleanup();}};"
			      "array<Item> produce_item(){return array<Item>();}"
			      "int pending=1/probe_pending_cleanup();int version(){return 2;}void main(){}"
			    : State.ForwardReturn
			    ? "class Item{public:int value=42;~Item(){notify_pending_cleanup();}};"
			      "class Producer{public:Item produce(){return Item();}};"
			      "int initialize(){Producer p=Producer();Item x=p.produce();return x.value;}"
			      "int initialized=initialize();int version(){return 2;}void main(){}"
			    : UseProceed
			    ? "class Item{public:int value=42;~Item(){notify_pending_cleanup();}};"
			      "class Producer{public:Item produce(){return Item();}};"
			      "int initialize(){Producer p=Producer();p.produce();return 0;}"
			      "int pending=initialize();int version(){return 2;}void main(){}"
			    : "class Item{public:int value=42;~Item(){notify_pending_cleanup();}};"
			      "Item produce_item(){return Item();}"
			      "int pending=1/probe_pending_cleanup();int version(){return 2;}void main(){}";
			if (Mode == 0)
			{
				FValue Held;
				char Error[256]{};
				uint64_t Required = 0;
				if (!Expect(Held.Handle &&
				            vs_interop_session_call_function(State.Session, View("hold"), nullptr, 0, Held.Handle, &Result) == VS_INTEROP_STATUS_OK &&
				            WriteSource(Replacement) && vs_interop_session_reload(State.Session, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR &&
				            !Result.success &&
				            vs_interop_session_copy_last_error(State.Session, Error, sizeof(Error), &Required) == VS_INTEROP_STATUS_OK &&
				            std::strstr(Error, "release external value roots") != nullptr &&
				            vs_interop_session_get_reload_status(State.Session, &Status) == VS_INTEROP_STATUS_OK &&
				            Status.state == VS_INTEROP_RELOAD_NONE && Status.operation_id == 0 &&
				            Status.committed_generation == OriginalGeneration &&
				            vs_interop_value_reset(Held.Handle) == VS_INTEROP_STATUS_OK,
				        "live value roots block reload with their own diagnostic before staging"))
				{
					return false;
				}
			}
			if (!Expect(WriteSource(Replacement) && vs_interop_session_reload(State.Session, &Result) == VS_INTEROP_STATUS_OK && Result.success && State.NestedCalls == 0 && State.Intercepts == 0 && State.Destroyed == 0,
			        "reload stages replacement without running pending initializer"))
			{
				return false;
			}
			if (!Expect(vs_interop_session_get_reload_status(State.Session, &Status) == VS_INTEROP_STATUS_OK &&
			            Status.state == VS_INTEROP_RELOAD_PENDING && Status.operation_id != 0 &&
			            Status.previous_generation == OriginalGeneration &&
			            Status.candidate_generation != OriginalGeneration &&
			            Status.current_generation == Status.candidate_generation &&
			            Status.committed_generation == OriginalGeneration,
			        "reload status distinguishes the staged candidate from the committed generation"))
			{
				return false;
			}
			const uint64_t Operation = Status.operation_id;
			const uint64_t CandidateGeneration = Status.candidate_generation;
			int32_t Unchanged = 0;
			const auto RunStatus = vs_interop_session_run(State.Session, VS_INTEROP_RUN_EVAL, &Result);
			if (!Expect(RunStatus == (State.ForwardReturn ? VS_INTEROP_STATUS_OK : VS_INTEROP_STATUS_ENGINE_ERROR) && static_cast<bool>(Result.success) == State.ForwardReturn && State.PublicationStatus == (State.ForwardReturn ? VS_INTEROP_STATUS_OK : VS_INTEROP_STATUS_ENGINE_ERROR) && State.NestedCalls == (UseProceed ? 0 : 1) && State.Intercepts == (UseProceed ? 1 : 0) && vs_interop_value_get_int32(State.Output, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 17 && State.Destroyed == ExpectedDestroyed,
			        "pending initialization rejects owned Item/container exports and permits borrowed returns"))
			{
				return false;
			}
			const uint64_t SettledGeneration = State.ForwardReturn ? CandidateGeneration : OriginalGeneration;
			const auto SettledState = State.ForwardReturn ? VS_INTEROP_RELOAD_APPLIED : VS_INTEROP_RELOAD_ROLLED_BACK;
			if (!Expect(vs_interop_session_get_reload_status(State.Session, &Status) == VS_INTEROP_STATUS_OK &&
			            Status.operation_id == Operation && Status.state == SettledState &&
			            Status.current_generation == SettledGeneration && Status.committed_generation == SettledGeneration,
			        "reload status reports commit or rollback for the same operation"))
			{
				return false;
			}
			if (State.ForwardReturn && !Expect(vs_interop_session_get_global(State.Session, View("initialized"), State.Output, &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_value_get_int32(State.Output, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 42, "borrowed proceed return completes replacement initialization"))
			{
				return false;
			}
			if (!Expect(vs_interop_session_call_function(State.Session, View("version"), nullptr, 0,
			                State.Output, &Result) == VS_INTEROP_STATUS_OK &&
			            Result.success && vs_interop_value_get_int32(State.Output, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == (State.ForwardReturn ? 2 : 1) && State.Destroyed == ExpectedDestroyed && State.NestedCalls == (UseProceed ? 0 : 1) && State.Intercepts == (UseProceed ? 1 : 0),
			        "initializer commits or restores its generation without repeated cleanup"))
			{
				return false;
			}
			if (Mode == 2)
			{
				std::string BusinessError(Replacement);
				BusinessError.replace(BusinessError.rfind("void main(){}"), std::string::npos,
				    "int divide(int n){return 10/n;}void main(){divide(0);}");
				if (!Expect(WriteSource(BusinessError.c_str()) &&
				            vs_interop_session_reload(State.Session, &Result) == VS_INTEROP_STATUS_OK &&
				            vs_interop_session_run(State.Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR &&
				            vs_interop_session_get_reload_status(State.Session, &Status) == VS_INTEROP_STATUS_OK &&
				            Status.state == VS_INTEROP_RELOAD_APPLIED_WITH_ERRORS && Status.operation_id != Operation &&
				            Status.current_generation == Status.candidate_generation &&
				            Status.committed_generation == Status.candidate_generation,
				        "business error after initialization reports the committed replacement"))
				{
					return false;
				}
				vs_interop_status QueryStatus = VS_INTEROP_STATUS_OK;
				const auto Committed = Status.committed_generation;
				std::thread Worker([&]()
				{
					QueryStatus = vs_interop_session_get_reload_status(State.Session, &Status);
				});
				Worker.join();
				if (!Expect(QueryStatus == VS_INTEROP_STATUS_WRONG_THREAD &&
				            Status.state == VS_INTEROP_RELOAD_APPLIED_WITH_ERRORS && Status.committed_generation == Committed,
				        "foreign thread reload status query preserves output"))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool TestHotReloadResetConfiguration()
	{
		FPendingInitializationCleanupState State;
		State.Path = std::filesystem::temp_directory_path() / ("veroscript-interop-reset-configuration-" +
		    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".vs");
		const auto WriteSource = [&State](const std::string& Source)
		{
			std::ofstream Out(State.Path, std::ios::binary | std::ios::trunc);
			Out << Source;
			Out.close();
			return Out.good();
		};
		State.Session = CreateSession();
		vs_interop_config Config{sizeof(Config)};
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_reload_status Status{sizeof(Status)};
		const std::string Source =
		    "class Item{public:int id;Item(){id=0;}Item(int n){id=n;}"
		    "~Item(){if(id==2){try{for(;;){}}finally{for(;;){}}}}};"
		    "class Owner{public:Item item=Item(1);};Owner root=Owner();void main(){}";
		const std::string Path = Utf8Path(State.Path);
		if (!Expect(State.Session &&
		            vs_interop_session_apply_config(State.Session, &Config) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_set_hot_reload_enabled(State.Session, 1) == VS_INTEROP_STATUS_OK &&
		            WriteSource(Source) &&
		            vs_interop_session_load_file(State.Session, View(Path), &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_run(State.Session, VS_INTEROP_RUN_EVAL, &Result) == VS_INTEROP_STATUS_OK &&
		            WriteSource(Source + "int fail(){root.item=Item(2);throw 7;}int trigger=fail();") &&
		            vs_interop_session_reload(State.Session, &Result) == VS_INTEROP_STATUS_OK,
		        "stage replacement for bounded rollback configuration rejection"))
		{
			return false;
		}
		Config.max_instructions = 80;
		if (!Expect(vs_interop_session_apply_config(State.Session, &Config) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_run(State.Session, VS_INTEROP_RUN_EVAL, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR &&
		            vs_interop_session_get_reload_status(State.Session, &Status) == VS_INTEROP_STATUS_OK &&
		            Status.state == VS_INTEROP_RELOAD_RESET_REQUIRED,
		        "bounded rollback leaves a reset-required replacement"))
		{
			return false;
		}
		const auto Operation = Status.operation_id;
		char Error[256]{};
		uint64_t Required = 0;
		return Expect(vs_interop_session_set_hot_reload_enabled(State.Session, 0) == VS_INTEROP_STATUS_ENGINE_ERROR &&
		                  vs_interop_session_is_hot_reload_enabled(State.Session) &&
		                  vs_interop_session_copy_last_error(State.Session, Error, sizeof(Error), &Required) == VS_INTEROP_STATUS_OK &&
		                  std::strstr(Error, "Destroy and recreate") != nullptr &&
		                  vs_interop_session_get_reload_status(State.Session, &Status) == VS_INTEROP_STATUS_OK &&
		                  Status.state == VS_INTEROP_RELOAD_RESET_REQUIRED && Status.operation_id == Operation,
		    "rejected hot-reload setting reports failure and retains reset-required status");
	}

	struct FContainerPromotionState
	{
		~FContainerPromotionState()
		{
			if (Session)
			{
				(void)vs_interop_session_destroy(Session);
			}
		}

		vs_interop_session* Session = nullptr;
		int Destroyed = 0;
	};

	vs_interop_callback_result VS_INTEROP_CALL ObserveContainerPromotionCleanup(
	    vs_interop_call_context*, vs_interop_value*, void* UserData)
	{
		++static_cast<FContainerPromotionState*>(UserData)->Destroyed;
		return VS_INTEROP_CALLBACK_OK;
	}

	bool InitializeContainerPromotion(FContainerPromotionState& State, const bool Persistent = false)
	{
		State.Session = CreateSession();
		vs_interop_config Config{sizeof(Config)};
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		vs_interop_native_type_desc Void{sizeof(Void)};
		Void.type = VS_INTEROP_NATIVE_VOID;
		vs_interop_run_result Result{sizeof(Result)};
		return Expect(State.Session && vs_interop_session_apply_config(State.Session, &Config) == VS_INTEROP_STATUS_OK && (!Persistent || vs_interop_session_set_hot_reload_enabled(State.Session, 1) == VS_INTEROP_STATUS_OK) && vs_interop_session_register_native(State.Session, View("notify_promotion_cleanup"), &Void, nullptr, 0, ObserveContainerPromotionCleanup, &State) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(State.Session, View("export class Item{public:int value=7;~Item(){notify_promotion_cleanup();}};"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                         "array<Item> empty(){return array<Item>();}"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                         "int fill(array<Item> items,ref int marker){items.push(Item());marker=99;return 42;}"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                         "int fill_fail(array<Item> items,ref int marker){items.push(Item());marker=99;assert(false);return 42;}"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                         "int fill_exit(array<Item> items,ref int marker){items.push(Item());marker=99;exit(23);return 42;}"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                         "array<Item> stash=array<Item>();"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                         "void fill_global(){stash.push(Item());}"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                         "void fill_nested(array<array<Item>> items){items[0].push(Item());}"
		                                                                                                                                                                                                                                                                                                                                                                                                                                                         "void main(){}"),
		                                                                                                                                                                                                                                                                                                                                                                                                          View("container-promotion.vs"), &Result) == VS_INTEROP_STATUS_OK &&
		        Result.success && vs_interop_session_run(State.Session, VS_INTEROP_RUN_EVAL, &Result) == VS_INTEROP_STATUS_OK && Result.success && State.Destroyed == 0,
		    "initialize independent-container promotion fixture in VM");
	}

	bool ClonePromotionAlias(const FValue& Source, FValue& Alias)
	{
		vs_interop_value_destroy(Alias.Handle);
		Alias.Handle = nullptr;
		return vs_interop_value_clone(Source.Handle, &Alias.Handle) == VS_INTEROP_STATUS_OK;
	}

	bool ReadPromotedItem(FContainerPromotionState& State, const vs_interop_value* Array)
	{
		// Release this temporary element root before callers check final cleanup.
		FValue Item, Field;
		std::uint64_t Count = 0;
		std::int32_t Number = 0;
		vs_interop_run_result Result{sizeof(Result)};
		return Expect(Item.Handle && Field.Handle && vs_interop_value_sequence_count(Array, &Count) == VS_INTEROP_STATUS_OK && Count == 1 && vs_interop_value_sequence_get(Array, 0, Item.Handle) == VS_INTEROP_STATUS_OK && vs_interop_session_get_field(State.Session, Item.Handle, View("value"), Field.Handle, &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_value_get_int32(Field.Handle, &Number) == VS_INTEROP_STATUS_OK && Number == 7,
		    "promoted container retains its Item and script field");
	}

	bool ReadPromotedItem(FContainerPromotionState& State, const FValue& Array)
	{
		return ReadPromotedItem(State, Array.Handle);
	}

	struct FEnumerationPromotionProbe
	{
		FContainerPromotionState* State;
		vs_interop_value* Child;
		int Calls = 0;
		bool Passed = false;
	};

	vs_interop_callback_result VS_INTEROP_CALL PromoteEnumeratedContainer(
	    const vs_interop_value* Key, const vs_interop_value* Mapped, void* UserData)
	{
		auto& Probe = *static_cast<FEnumerationPromotionProbe*>(UserData);
		++Probe.Calls;
		FValue Marker, Returned, Clone;
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_value* Arguments[] = {Probe.Child, Marker.Handle};
		std::int32_t KeyNumber = 0;
		if (!Marker.Handle || !Returned.Handle || !Clone.Handle || vs_interop_value_get_int32(Key, &KeyNumber) != VS_INTEROP_STATUS_OK || KeyNumber != 1 || vs_interop_value_set_int32(Marker.Handle, 5) != VS_INTEROP_STATUS_OK || vs_interop_session_call_function(Probe.State->Session, View("fill"), Arguments, 2, Returned.Handle, &Result) != VS_INTEROP_STATUS_OK || !Result.success || !ReadPromotedItem(*Probe.State, Mapped))
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		vs_interop_value_destroy(Clone.Handle);
		Clone.Handle = nullptr;
		Probe.Passed = vs_interop_value_clone(Mapped, &Clone.Handle) == VS_INTEROP_STATUS_OK && ReadPromotedItem(*Probe.State, Clone) && Probe.State->Destroyed == 0;
		return Probe.Passed ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
	}

	bool TestIndependentContainerPromotion()
	{
		const char* Functions[] = {"fill", "fill_fail", "fill_exit"};
		for (const bool ScriptCreated : {false, true})
		{
			for (const int Mode : {0, 1, 2})
			{
				// Declare State first so callback data outlives every value and the session.
				FContainerPromotionState State;
				FValue Array, Alias, Marker, Returned;
				vs_interop_run_result Result{sizeof(Result)};
				if (!InitializeContainerPromotion(State) || !Expect(Array.Handle && Alias.Handle && Marker.Handle && Returned.Handle && (ScriptCreated ? vs_interop_session_call_function(State.Session, View("empty"), nullptr, 0, Array.Handle, &Result) == VS_INTEROP_STATUS_OK && Result.success : vs_interop_value_create_array(Array.Handle, View("Item"), nullptr, 0) == VS_INTEROP_STATUS_OK) && ClonePromotionAlias(Array, Alias) && vs_interop_value_set_int32(Marker.Handle, 5) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int32(Returned.Handle, 77) == VS_INTEROP_STATUS_OK, "clone empty container before it acquires a script graph"))
				{
					return false;
				}
				vs_interop_value* Arguments[] = {Array.Handle, Marker.Handle};
				const auto Status = vs_interop_session_call_function(State.Session, View(Functions[Mode]),
				    Arguments, 2, Returned.Handle, &Result);
				std::int32_t MarkerNumber = 0, ReturnNumber = 0;
				if (!Expect(Status == (Mode == 1 ? VS_INTEROP_STATUS_ENGINE_ERROR : VS_INTEROP_STATUS_OK) && static_cast<bool>(Result.success) == (Mode != 1) && static_cast<bool>(Result.exit_requested) == (Mode == 2) && (Mode != 2 || Result.exit_code == 23) && vs_interop_value_get_int32(Marker.Handle, &MarkerNumber) == VS_INTEROP_STATUS_OK && MarkerNumber == (Mode == 0 ? 99 : 5) && vs_interop_value_get_int32(Returned.Handle, &ReturnNumber) == VS_INTEROP_STATUS_OK && ReturnNumber == (Mode == 0 ? 42 : 77),
				        "container mutation preserves failure and exit ref/return publication rules") ||
				    !ReadPromotedItem(State, Array) || !ReadPromotedItem(State, Alias) || !Expect(State.Destroyed == 0 && vs_interop_value_reset(Array.Handle) == VS_INTEROP_STATUS_OK && State.Destroyed == 0, "resetting the passed alias preserves the preexisting clone") || !ReadPromotedItem(State, Alias) || !Expect(State.Destroyed == 0 && vs_interop_value_reset(Alias.Handle) == VS_INTEROP_STATUS_OK && State.Destroyed == 1, "last promoted-container alias runs Item cleanup exactly once"))
				{
					std::cerr << "Container promotion source=" << (ScriptCreated ? "script" : "C")
					          << " function=" << Functions[Mode] << '\n';
					return false;
				}
			}
		}

		{
			FContainerPromotionState Source, Target;
			FValue Array, Marker, Returned;
			vs_interop_run_result Result{sizeof(Result)};
			if (!InitializeContainerPromotion(Source) || !InitializeContainerPromotion(Target) || !Expect(Array.Handle && Marker.Handle && Returned.Handle && vs_interop_session_call_function(Source.Session, View("empty"), nullptr, 0, Array.Handle, &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_value_set_int32(Marker.Handle, 5) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int32(Returned.Handle, 77) == VS_INTEROP_STATUS_OK, "export an empty module container before testing its session identity"))
			{
				return false;
			}
			vs_interop_value* Arguments[] = {Array.Handle, Marker.Handle};
			const auto Status = vs_interop_session_call_function(Target.Session, View("fill"), Arguments, 2,
			    Returned.Handle, &Result);
			vs_interop_diagnostic Diagnostic{sizeof(Diagnostic)};
			std::uint64_t Count = 1;
			std::int32_t MarkerNumber = 0, ReturnNumber = 0;
			if (!Expect(Status == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && !Result.exit_requested && Result.diagnostic_count != 0 && vs_interop_session_get_diagnostic(Target.Session, 0, &Diagnostic) == VS_INTEROP_STATUS_OK && Diagnostic.message.data && std::string_view(Diagnostic.message.data, static_cast<std::size_t>(Diagnostic.message.size)).find("foreign session") != std::string_view::npos && vs_interop_value_sequence_count(Array.Handle, &Count) == VS_INTEROP_STATUS_OK && Count == 0 && vs_interop_value_get_int32(Marker.Handle, &MarkerNumber) == VS_INTEROP_STATUS_OK && MarkerNumber == 5 && vs_interop_value_get_int32(Returned.Handle, &ReturnNumber) == VS_INTEROP_STATUS_OK && ReturnNumber == 77 && Source.Destroyed == 0 && Target.Destroyed == 0,
			        "empty module container rejects foreign mutation with diagnostics and preserves outputs"))
			{
				return false;
			}
		}

		{
			FContainerPromotionState State;
			FValue Array, Alias, Returned;
			vs_interop_run_result Result{sizeof(Result)};
			if (!InitializeContainerPromotion(State, true) || !Expect(Array.Handle && Alias.Handle && Returned.Handle && vs_interop_value_create_array(Array.Handle, View("Item"), nullptr, 0) == VS_INTEROP_STATUS_OK && ClonePromotionAlias(Array, Alias) && vs_interop_session_set_global(State.Session, View("stash"), Array.Handle, &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_session_call_function(State.Session, View("fill_global"), nullptr, 0, Returned.Handle, &Result) == VS_INTEROP_STATUS_OK && Result.success, "global mutates the original independent C container without C arguments") || !ReadPromotedItem(State, Array) || !ReadPromotedItem(State, Alias) || !Expect(State.Destroyed == 0 && vs_interop_value_reset(Array.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(Alias.Handle) == VS_INTEROP_STATUS_OK && State.Destroyed == 0, "global keeps the promoted Item alive after all C aliases are reset"))
			{
				return false;
			}
			auto* Session = State.Session;
			State.Session = nullptr;
			if (!Expect(vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK && State.Destroyed == 1,
			        "session teardown releases the global Item exactly once"))
			{
				return false;
			}
		}

		{
			FContainerPromotionState State;
			FValue Map, Key, Child;
			const vs_interop_value* Keys[] = {Key.Handle};
			const vs_interop_value* Values[] = {Child.Handle};
			FEnumerationPromotionProbe Probe{&State, Child.Handle};
			if (!InitializeContainerPromotion(State) || !Expect(Map.Handle && Key.Handle && Child.Handle && vs_interop_value_set_int32(Key.Handle, 1) == VS_INTEROP_STATUS_OK && vs_interop_value_create_array(Child.Handle, View("Item"), nullptr, 0) == VS_INTEROP_STATUS_OK && vs_interop_value_create_map(Map.Handle, View("int"), View("array<Item>"), 0, Keys, Values, 1) == VS_INTEROP_STATUS_OK && vs_interop_value_map_enumerate(Map.Handle, PromoteEnumeratedContainer, &Probe) == VS_INTEROP_STATUS_OK && Probe.Calls == 1 && Probe.Passed, "map snapshot view observes a child promoted during its visitor") || !ReadPromotedItem(State, Child) || !Expect(State.Destroyed == 0 && vs_interop_value_reset(Map.Handle) == VS_INTEROP_STATUS_OK && State.Destroyed == 0 && vs_interop_value_reset(Child.Handle) == VS_INTEROP_STATUS_OK && State.Destroyed == 1, "promoted visitor snapshots retire without retaining or destroying child aliases"))
			{
				return false;
			}
		}

		{
			FContainerPromotionState State;
			FValue Child, ChildAlias, Outer, ExtractedChild, Returned;
			vs_interop_run_result Result{sizeof(Result)};
			const vs_interop_value* Elements[] = {Child.Handle};
			if (!InitializeContainerPromotion(State) || !Expect(Child.Handle && ChildAlias.Handle && Outer.Handle && ExtractedChild.Handle && Returned.Handle && vs_interop_value_create_array(Child.Handle, View("Item"), nullptr, 0) == VS_INTEROP_STATUS_OK && ClonePromotionAlias(Child, ChildAlias) && vs_interop_value_create_array(Outer.Handle, View("array<Item>"), Elements, 1) == VS_INTEROP_STATUS_OK && vs_interop_value_sequence_get(Outer.Handle, 0, ExtractedChild.Handle) == VS_INTEROP_STATUS_OK, "create nested aggregate and getter aliases before script-graph promotion"))
			{
				return false;
			}
			vs_interop_value* Arguments[] = {Outer.Handle};
			if (!Expect(vs_interop_session_call_function(State.Session, View("fill_nested"), Arguments, 1,
			                Returned.Handle, &Result) == VS_INTEROP_STATUS_OK &&
			            Result.success,
			        "script mutates a nested independent container through its outer aggregate") ||
			    !ReadPromotedItem(State, Child) || !ReadPromotedItem(State, ChildAlias) || !ReadPromotedItem(State, ExtractedChild) || !Expect(State.Destroyed == 0 && vs_interop_value_reset(Outer.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(Child.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(ChildAlias.Handle) == VS_INTEROP_STATUS_OK && State.Destroyed == 0, "nested getter alias survives the outer aggregate and original child aliases") || !ReadPromotedItem(State, ExtractedChild) || !Expect(State.Destroyed == 0 && vs_interop_value_reset(ExtractedChild.Handle) == VS_INTEROP_STATUS_OK && State.Destroyed == 1, "last nested getter alias releases Item exactly once"))
			{
				return false;
			}
		}
		return true;
	}

	void VS_INTEROP_CALL OnMutation(
	    const vs_interop_mutation_event* Event,
	    void* UserData)
	{
		if (!Event || Event->struct_size < sizeof(vs_interop_mutation_event))
		{
			return;
		}
		auto& State = *static_cast<FHookState*>(UserData);
		if (Event->kind == VS_INTEROP_MUTATION_FIELD_WRITE && Event->object && Event->field_name.size != 0)
		{
			++State.Mutations;
		}
	}

	struct FGenericFactoryState
	{
		vs_interop_session* Session = nullptr;
		vs_interop_status ReentrantClearStatus = VS_INTEROP_STATUS_OK;
	};

	vs_interop_callback_result VS_INTEROP_CALL BuildGenericBox(
	    vs_interop_generic_type_context* Context,
	    void* UserData)
	{
		if (UserData)
		{
			auto& State = *static_cast<FGenericFactoryState*>(UserData);
			State.ReentrantClearStatus = vs_interop_session_clear(State.Session);
		}
		vs_interop_string_view Argument{};
		if (vs_interop_generic_type_context_argument_count(Context) != 1 || vs_interop_generic_type_context_get_argument(Context, 0, &Argument) != VS_INTEROP_STATUS_OK || Argument.size == 0)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		vs_interop_host_type_desc Type{};
		Type.struct_size = sizeof(Type);
		Type.name = View("Box");
		Type.kind = VS_INTEROP_HOST_STRUCT;
		Type.documentation = View("Interop generic box");
		if (vs_interop_generic_type_context_set_type(Context, &Type) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL SerializeCounter(
	    vs_interop_external_serialize_context* Context,
	    void*)
	{
		if (vs_interop_external_serialize_context_field_count(Context) != 1)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		vs_interop_string_view Name{};
		const vs_interop_value* Value = nullptr;
		if (vs_interop_external_serialize_context_get_field(
		        Context, 0, &Name, nullptr, nullptr, nullptr, &Value) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		std::int32_t Number = 0;
		if (std::string(Name.data, static_cast<std::size_t>(Name.size)) != "value" || vs_interop_value_get_int32(Value, &Number) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		const std::uint8_t Bytes[] = {
		    static_cast<std::uint8_t>(Number),
		    static_cast<std::uint8_t>(Number >> 8),
		    static_cast<std::uint8_t>(Number >> 16),
		    static_cast<std::uint8_t>(Number >> 24)};
		return vs_interop_external_serialize_context_set_bytes(Context, {Bytes, 4}) == VS_INTEROP_STATUS_OK ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
	}

	vs_interop_callback_result VS_INTEROP_CALL DeserializeCounter(
	    vs_interop_external_deserialize_context* Context,
	    void*)
	{
		const vs_interop_byte_view Bytes =
		    vs_interop_external_deserialize_context_bytes(Context);
		if (Bytes.size != 4 || vs_interop_external_deserialize_context_encoded_version(Context) != 3)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		const std::uint32_t Raw = static_cast<std::uint32_t>(Bytes.data[0]) | (static_cast<std::uint32_t>(Bytes.data[1]) << 8) | (static_cast<std::uint32_t>(Bytes.data[2]) << 16) | (static_cast<std::uint32_t>(Bytes.data[3]) << 24);
		FValue Value;
		if (vs_interop_value_set_int32(Value.Handle, static_cast<std::int32_t>(Raw)) != VS_INTEROP_STATUS_OK || vs_interop_external_deserialize_context_add_field(Context, View("value"), Value.Handle) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL CountReference(
	    vs_interop_external_reference_kind,
	    const vs_interop_value*,
	    void* UserData)
	{
		++*static_cast<int*>(UserData);
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL StructuredHostFunction(
	    vs_interop_call_context*,
	    vs_interop_value* OutReturn,
	    void*)
	{
		return vs_interop_value_set_int32(OutReturn, 42) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}

	struct FMapEnumerationState
	{
		std::vector<std::string> Keys;
		std::vector<std::int32_t> Values;
		vs_interop_value* ClonedKey = nullptr;
		bool bFail = false;
	};

	vs_interop_callback_result VS_INTEROP_CALL VisitMapEntry(
	    const vs_interop_value* Key,
	    const vs_interop_value* Mapped,
	    void* UserData)
	{
		auto& State = *static_cast<FMapEnumerationState*>(UserData);
		vs_interop_string_view KeyText{};
		std::int32_t Number = 0;
		if (vs_interop_value_get_string(Key, &KeyText) != VS_INTEROP_STATUS_OK || vs_interop_value_get_int32(Mapped, &Number) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		State.Keys.emplace_back(KeyText.data, static_cast<std::size_t>(KeyText.size));
		State.Values.push_back(Number);
		if (!State.ClonedKey && vs_interop_value_clone(Key, &State.ClonedKey) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return State.bFail ? VS_INTEROP_CALLBACK_ERROR : VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL VisitSetElement(
	    const vs_interop_value* Element,
	    void* UserData)
	{
		std::int32_t Number = 0;
		if (vs_interop_value_get_int32(Element, &Number) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		static_cast<std::vector<std::int32_t>*>(UserData)->push_back(Number);
		return VS_INTEROP_CALLBACK_OK;
	}

	struct FRetiredEnumerationState
	{
		~FRetiredEnumerationState()
		{
			vs_interop_value_destroy(Source);
			for (auto* Copy : Copies)
			{
				vs_interop_value_destroy(Copy);
			}
			if (Session)
			{
				(void)vs_interop_session_destroy(Session);
			}
		}
		vs_interop_session* Session = nullptr;
		vs_interop_value* Source = nullptr;
		vs_interop_value* Copies[2]{};
		unsigned Visits = 0, Seen = 0;
		int Destroyed[2]{};
		bool InvalidNotification = false;
	};

	vs_interop_callback_result VS_INTEROP_CALL ObserveEnumeratedItem(
	    vs_interop_call_context* Context, vs_interop_value*, void* UserData)
	{
		auto& State = *static_cast<FRetiredEnumerationState*>(UserData);
		const vs_interop_value* Argument = nullptr;
		int32_t Id = 0;
		if (vs_interop_call_context_get_argument(Context, 0, &Argument) != VS_INTEROP_STATUS_OK || vs_interop_value_get_int32(Argument, &Id) != VS_INTEROP_STATUS_OK || Id < 1 || Id > 2)
		{
			State.InvalidNotification = true;
			return VS_INTEROP_CALLBACK_ERROR;
		}
		++State.Destroyed[Id - 1];
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL RetireMapDuringEnumeration(
	    const vs_interop_value* Key, const vs_interop_value* Mapped, void* UserData)
	{
		auto& State = *static_cast<FRetiredEnumerationState*>(UserData);
		int32_t Id = 0;
		if (vs_interop_value_get_int32(Key, &Id) != VS_INTEROP_STATUS_OK || Id < 1 || Id > 2 || (State.Seen & (1u << (Id - 1))))
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		if (State.Source)
		{
			auto* Source = State.Source;
			State.Source = nullptr;
			vs_interop_value_destroy(Source);
		}
		// Clone after source retirement: raw FValue snapshots alone must not let
		// language cleanup destroy either Item before the visitor can retain it.
		if (State.Destroyed[0] || State.Destroyed[1] || State.InvalidNotification || vs_interop_value_clone(Mapped, &State.Copies[Id - 1]) != VS_INTEROP_STATUS_OK || vs_interop_value_get_kind(State.Copies[Id - 1]) != VS_INTEROP_VALUE_OBJECT)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		State.Seen |= 1u << (Id - 1);
		++State.Visits;
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL RetireSetDuringEnumeration(
	    const vs_interop_value* Element, void* UserData)
	{
		auto& State = *static_cast<FRetiredEnumerationState*>(UserData);
		if (State.Source)
		{
			auto* Source = State.Source;
			State.Source = nullptr;
			vs_interop_value_destroy(Source);
		}
		int32_t Number = 0;
		if (vs_interop_value_get_int32(Element, &Number) != VS_INTEROP_STATUS_OK || Number < 1 || Number > 2 || (State.Seen & (1u << (Number - 1))) || vs_interop_value_clone(Element, &State.Copies[Number - 1]) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		State.Seen |= 1u << (Number - 1);
		++State.Visits;
		return VS_INTEROP_CALLBACK_OK;
	}

	bool TestEnumerationSourceRetirement()
	{
		FRetiredEnumerationState Map;
		Map.Session = CreateSession();
		vs_interop_native_type_desc Void{sizeof(Void)};
		Void.type = VS_INTEROP_NATIVE_VOID;
		vs_interop_native_param_desc Parameter{sizeof(Parameter)};
		Parameter.type.struct_size = sizeof(Parameter.type);
		Parameter.type.type = VS_INTEROP_NATIVE_INT32;
		vs_interop_run_result Result{sizeof(Result)};
		const char* Source =
		    "class Item {public:int id; Item(int value){id=value;}"
		    "~Item(){observe_enumerated_item(id);}};"
		    "map<int,Item> make_map(){map<int,Item> result=map<int,Item>();"
		    "result[1]=Item(1);result[2]=Item(2);return result;}";
		if (!Expect(Map.Session && vs_interop_value_create(&Map.Source) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(Map.Session, View("observe_enumerated_item"), &Void, &Parameter, 1, ObserveEnumeratedItem, &Map) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Map.Session, View(Source), View("enumeration-retirement.vs"), &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_session_call_function(Map.Session, View("make_map"), nullptr, 0, Map.Source, &Result) == VS_INTEROP_STATUS_OK && Result.success && Map.Destroyed[0] == 0 && Map.Destroyed[1] == 0,
		        "prepare rooted map enumeration retirement"))
		{
			return false;
		}
		if (!Expect(vs_interop_value_map_enumerate(Map.Source, RetireMapDuringEnumeration, &Map) == VS_INTEROP_STATUS_OK && !Map.Source && Map.Visits == 2 && Map.Seen == 3 && Map.Destroyed[0] == 0 && Map.Destroyed[1] == 0 && !Map.InvalidNotification,
		        "map visitors retain snapshots after destroying the source handle"))
		{
			return false;
		}
		FValue Field;
		for (int Id = 1; Id <= 2; ++Id)
		{
			int32_t Value = 0;
			if (!Expect(Field.Handle && vs_interop_session_get_field(Map.Session, Map.Copies[Id - 1], View("id"), Field.Handle, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(Field.Handle, &Value) == VS_INTEROP_STATUS_OK && Value == Id,
			        "mapped snapshot clone remains usable after enumeration"))
			{
				return false;
			}
		}
		vs_interop_value_destroy(Map.Copies[0]);
		Map.Copies[0] = nullptr;
		if (!Expect(Map.Destroyed[0] == 1 && Map.Destroyed[1] == 0,
		        "first mapped clone retires only its own Item"))
		{
			return false;
		}
		vs_interop_value_destroy(Map.Copies[1]);
		Map.Copies[1] = nullptr;
		if (!Expect(Map.Destroyed[0] == 1 && Map.Destroyed[1] == 1 && !Map.InvalidNotification,
		        "mapped Items run language cleanup exactly once after final clone release"))
		{
			return false;
		}

		FRetiredEnumerationState Set;
		FValue One, Two;
		const vs_interop_value* Elements[] = {One.Handle, Two.Handle};
		if (!Expect(One.Handle && Two.Handle && vs_interop_value_create(&Set.Source) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int32(One.Handle, 1) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int32(Two.Handle, 2) == VS_INTEROP_STATUS_OK && vs_interop_value_create_set(Set.Source, View("int"), Elements, 2) == VS_INTEROP_STATUS_OK && vs_interop_value_set_enumerate(Set.Source, RetireSetDuringEnumeration, &Set) == VS_INTEROP_STATUS_OK && !Set.Source && Set.Visits == 2 && Set.Seen == 3,
		        "set visits both snapshot elements after destroying its source handle"))
		{
			return false;
		}
		for (int Id = 1; Id <= 2; ++Id)
		{
			int32_t Value = 0;
			if (!Expect(vs_interop_value_get_int32(Set.Copies[Id - 1], &Value) == VS_INTEROP_STATUS_OK && Value == Id,
			        "set element clone remains valid after enumeration"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestContainers()
	{
		FValue Map;
		FValue Key1;
		FValue Key2;
		FValue Value1;
		FValue Value2;
		FValue Out;
		(void)vs_interop_value_set_string(Key1.Handle, View("one"));
		(void)vs_interop_value_set_string(Key2.Handle, View("two"));
		(void)vs_interop_value_set_int32(Value1.Handle, 1);
		(void)vs_interop_value_set_int32(Value2.Handle, 2);
		const vs_interop_value* Keys[] = {Key1.Handle, Key2.Handle};
		const vs_interop_value* Values[] = {Value1.Handle, Value2.Handle};
		std::uint64_t Count = 0;
		std::uint8_t Found = 0;
		std::int32_t Number = 0;
		if (!Expect(vs_interop_value_create_map(Map.Handle, View("string"), View("int"),
		                1, Keys, Values, 2) == VS_INTEROP_STATUS_OK,
		        "ordered map construction"))
		{
			return false;
		}
		if (!Expect(vs_interop_value_map_count(Map.Handle, &Count) == VS_INTEROP_STATUS_OK && Count == 2 && vs_interop_value_map_get(Map.Handle, Key2.Handle, Out.Handle, &Found) == VS_INTEROP_STATUS_OK && Found && vs_interop_value_get_int32(Out.Handle, &Number) == VS_INTEROP_STATUS_OK && Number == 2, "map inspection"))
		{
			return false;
		}
		FValue MapCopy;
		if (!Expect(vs_interop_value_assign(MapCopy.Handle, Map.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_map_count(MapCopy.Handle, &Count) == VS_INTEROP_STATUS_OK && Count == 2,
		        "complete value assignment"))
		{
			return false;
		}
		FMapEnumerationState MapEntries;
		if (!Expect(vs_interop_value_map_enumerate(
		                Map.Handle, VisitMapEntry, &MapEntries) == VS_INTEROP_STATUS_OK &&
		            MapEntries.Keys == std::vector<std::string>{"one", "two"} && MapEntries.Values == std::vector<std::int32_t>{1, 2} && MapEntries.ClonedKey,
		        "ordered map enumeration"))
		{
			vs_interop_value_destroy(MapEntries.ClonedKey);
			return false;
		}
		vs_interop_string_view ClonedKeyText{};
		const bool bCloneSurvived = vs_interop_value_get_string(
		                                MapEntries.ClonedKey, &ClonedKeyText) == VS_INTEROP_STATUS_OK &&
		    std::string(ClonedKeyText.data,
		        static_cast<std::size_t>(ClonedKeyText.size)) == "one";
		vs_interop_value_destroy(MapEntries.ClonedKey);
		if (!Expect(bCloneSurvived,
		        "map enumeration borrowed value cloning"))
		{
			return false;
		}
		FMapEnumerationState FailedVisit;
		FailedVisit.bFail = true;
		if (!Expect(vs_interop_value_map_enumerate(
		                Map.Handle, VisitMapEntry, &FailedVisit) == VS_INTEROP_STATUS_CALLBACK_ERROR &&
		            FailedVisit.Keys.size() == 1 && vs_interop_value_map_enumerate(Map.Handle, nullptr, nullptr) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
		        "map enumeration cancellation"))
		{
			vs_interop_value_destroy(FailedVisit.ClonedKey);
			return false;
		}
		vs_interop_value_destroy(FailedVisit.ClonedKey);
		(void)vs_interop_value_set_string(Key2.Handle, View("missing"));
		if (!Expect(vs_interop_value_map_get(Map.Handle, Key2.Handle, Out.Handle, &Found) == VS_INTEROP_STATUS_OK && !Found, "map missing key"))
		{
			return false;
		}

		FValue MultiMap;
		const vs_interop_value* MultiKeys[] = {Key1.Handle, Key1.Handle, Key2.Handle};
		const vs_interop_value* MultiValues[] = {Value1.Handle, Value2.Handle, Value1.Handle};
		vs_interop_string_view MultiMapType{};
		if (!Expect(vs_interop_value_create_map_kind(
		                MultiMap.Handle, VS_INTEROP_VALUE_MULTI_MAP, View("string"), View("int"),
		                MultiKeys, MultiValues, 3) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_kind(MultiMap.Handle) == VS_INTEROP_VALUE_MULTI_MAP && vs_interop_value_get_type_name(MultiMap.Handle, &MultiMapType) == VS_INTEROP_STATUS_OK && std::string(MultiMapType.data, static_cast<std::size_t>(MultiMapType.size)) == "multimap<string,int>",
		        "multimap construction"))
		{
			return false;
		}

		FValue Set;
		const vs_interop_value* Elements[] = {Value1.Handle, Value2.Handle, Value2.Handle};
		std::uint8_t Contains = 0;
		if (!Expect(vs_interop_value_create_set(Set.Handle, View("int32"), Elements, 3) == VS_INTEROP_STATUS_OK && vs_interop_value_set_count(Set.Handle, &Count) == VS_INTEROP_STATUS_OK && Count == 2 && vs_interop_value_set_contains(Set.Handle, Value2.Handle, &Contains) == VS_INTEROP_STATUS_OK && Contains,
		        "set construction and inspection"))
		{
			return false;
		}
		std::vector<std::int32_t> SetElements;
		if (!Expect(vs_interop_value_set_enumerate(
		                Set.Handle, VisitSetElement, &SetElements) == VS_INTEROP_STATUS_OK,
		        "set enumeration"))
		{
			return false;
		}
		std::sort(SetElements.begin(), SetElements.end());
		if (!Expect(SetElements == std::vector<std::int32_t>{1, 2} && vs_interop_value_set_enumerate(Set.Handle, nullptr, nullptr) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
		        "set enumeration values"))
		{
			return false;
		}
		FValue MultiSet;
		if (!Expect(vs_interop_value_create_set_kind(
		                MultiSet.Handle, VS_INTEROP_VALUE_ORDERED_MULTI_SET, View("int"), Elements, 3) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_kind(MultiSet.Handle) == VS_INTEROP_VALUE_ORDERED_MULTI_SET,
		        "ordered multiset construction"))
		{
			return false;
		}
		if (!Expect(vs_interop_value_set_count(MultiSet.Handle, &Count) == VS_INTEROP_STATUS_OK && Count == 3 && vs_interop_value_set_contains(MultiSet.Handle, Value2.Handle, &Contains) == VS_INTEROP_STATUS_OK && Contains,
		        "ordered multiset inspection"))
		{
			return false;
		}
		FValue Queue;
		if (!Expect(vs_interop_value_create_sequence(Queue.Handle, VS_INTEROP_VALUE_QUEUE,
		                View("int"), Elements, 3) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_sequence_count(Queue.Handle, &Count) == VS_INTEROP_STATUS_OK && Count == 3 && vs_interop_value_sequence_get(Queue.Handle, 0, Out.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(Out.Handle, &Number) == VS_INTEROP_STATUS_OK && Number == 1,
		        "queue construction and inspection"))
		{
			return false;
		}
		FValue PriorityQueue;
		if (!Expect(vs_interop_value_create_sequence(
		                PriorityQueue.Handle, VS_INTEROP_VALUE_PRIORITY_QUEUE, View("int"), Elements, 3) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_kind(PriorityQueue.Handle) == VS_INTEROP_VALUE_PRIORITY_QUEUE,
		        "priority queue construction"))
		{
			return false;
		}
		FValue Channel;
		vs_interop_buffer* ChannelError = nullptr;
		std::uint64_t ChannelCount = 0;
		std::uint64_t ChannelCapacity = 0;
		std::uint8_t ChannelFlag = 0;
		if (!Expect(vs_interop_value_create_bounded_channel(
		                Channel.Handle, View("int32"), 1) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_channel_capacity(Channel.Handle, &ChannelCapacity) == VS_INTEROP_STATUS_OK && ChannelCapacity == 1 && vs_interop_value_channel_count(Channel.Handle, &ChannelCount) == VS_INTEROP_STATUS_OK && ChannelCount == 0 && vs_interop_value_channel_is_closed(Channel.Handle, &ChannelFlag) == VS_INTEROP_STATUS_OK && !ChannelFlag,
		        "bounded channel construction and queries"))
		{
			return false;
		}
		if (!Expect(vs_interop_value_channel_try_send(
		                Channel.Handle, Value1.Handle, &ChannelFlag) == VS_INTEROP_STATUS_OK &&
		            ChannelFlag && vs_interop_value_channel_try_send(Channel.Handle, Value2.Handle, &ChannelFlag) == VS_INTEROP_STATUS_OK && !ChannelFlag && vs_interop_value_channel_count(Channel.Handle, &ChannelCount) == VS_INTEROP_STATUS_OK && ChannelCount == 1,
		        "bounded channel non-blocking send"))
		{
			return false;
		}
		if (!Expect(vs_interop_value_channel_try_receive(
		                Channel.Handle, Out.Handle, &ChannelFlag) == VS_INTEROP_STATUS_OK &&
		            ChannelFlag && vs_interop_value_get_int32(Out.Handle, &Number) == VS_INTEROP_STATUS_OK && Number == 1 && vs_interop_value_channel_receive_for(Channel.Handle, 0, Out.Handle, &ChannelFlag) == VS_INTEROP_STATUS_OK && !ChannelFlag,
		        "channel non-blocking and timed receive"))
		{
			return false;
		}
		if (!Expect(vs_interop_value_channel_send_for(
		                Channel.Handle, Value2.Handle, 0, &ChannelFlag) == VS_INTEROP_STATUS_OK &&
		            ChannelFlag && vs_interop_value_channel_receive(Channel.Handle, Out.Handle, &ChannelError) == VS_INTEROP_STATUS_OK && !ChannelError && vs_interop_value_get_int32(Out.Handle, &Number) == VS_INTEROP_STATUS_OK && Number == 2,
		        "channel timed send and blocking receive"))
		{
			return false;
		}
		FValue InnerChannel;
		FValue NestedChannel;
		std::string TooDeepElement = "int";
		for (std::size_t Depth = 0; Depth < 65; ++Depth)
		{
			TooDeepElement = "channel<" + TooDeepElement + ">";
		}
		if (!Expect(vs_interop_value_create_bounded_channel(
		                InnerChannel.Handle, View("int"), 1) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_create_bounded_channel(
		                NestedChannel.Handle, View("channel<int>"), 1) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_create_bounded_channel(
		                Out.Handle, View(TooDeepElement), 1) == VS_INTEROP_STATUS_INVALID_ARGUMENT &&
		            vs_interop_value_channel_try_send(
		                NestedChannel.Handle, InnerChannel.Handle, &ChannelFlag) == VS_INTEROP_STATUS_OK &&
		            ChannelFlag && vs_interop_value_channel_try_send(NestedChannel.Handle, Value1.Handle, &ChannelFlag) == VS_INTEROP_STATUS_TYPE_MISMATCH && vs_interop_value_channel_try_receive(NestedChannel.Handle, Out.Handle, &ChannelFlag) == VS_INTEROP_STATUS_OK && ChannelFlag && vs_interop_value_channel_capacity(Out.Handle, &ChannelCapacity) == VS_INTEROP_STATUS_OK && ChannelCapacity == 1,
		        "nested channel capability transfer"))
		{
			return false;
		}
		if (!Expect(vs_interop_value_channel_close(Channel.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_channel_is_closed(Channel.Handle, &ChannelFlag) == VS_INTEROP_STATUS_OK && ChannelFlag && vs_interop_value_channel_try_send(Channel.Handle, Value1.Handle, &ChannelFlag) == VS_INTEROP_STATUS_OK && !ChannelFlag && vs_interop_value_channel_receive(Channel.Handle, Out.Handle, &ChannelError) == VS_INTEROP_STATUS_ENGINE_ERROR && ChannelError,
		        "closed channel receive"))
		{
			return false;
		}
		vs_interop_buffer_destroy(ChannelError);
		return true;
	}

	bool TestSessionJitCache()
	{
		if (!HasTestJitBackend())
		{
			std::cout << "SKIP: JIT cache requires an explicitly selected test backend\n";
			return true;
		}
		vs_interop_session* Session = CreateSession();
		if (!Expect(Session != nullptr, "JIT cache session creation"))
		{
			return false;
		}
		vs_interop_config Config{};
		Config.struct_size = sizeof(Config);
		Config.execution_backend = VS_INTEROP_BACKEND_JIT;
		if (!Expect(vs_interop_session_apply_config(Session, &Config) == VS_INTEROP_STATUS_OK, "JIT cache backend configuration"))
		{
			return false;
		}

		// A shorter descriptor omits fields after eager_compile_budget.
		// A legacy caller cannot spell the appended switch, so it must receive the
		// optimized default instead of the zero in this test's backing storage.
		vs_interop_jit_policy Policy{};
		Policy.struct_size = static_cast<std::uint32_t>(
		    offsetof(vs_interop_jit_policy, reuse_session_code));
		Policy.mixed_hot_call_threshold = 1;
		Policy.max_compile_attempts = 128;
		Policy.max_cached_entries = 128;
		Policy.eager_compile_budget = 128;
		if (!Expect(vs_interop_session_set_jit_policy(Session, &Policy) == VS_INTEROP_STATUS_OK, "legacy-sized JIT policy compatibility"))
		{
			return false;
		}

		vs_interop_jit_cache_limits Limits{};
		Limits.struct_size = sizeof(Limits);
		if (!Expect(vs_interop_session_get_jit_cache_limits(Session, &Limits) == VS_INTEROP_STATUS_OK && Limits.max_native_bytes == 8u * 1024u * 1024u && Limits.max_retained_payload_bytes == 32u * 1024u * 1024u,
		        "default session JIT cache limits"))
		{
			return false;
		}
		Limits.max_native_bytes = 4u * 1024u * 1024u;
		Limits.max_retained_payload_bytes = 16u * 1024u * 1024u;
		if (!Expect(vs_interop_session_set_jit_cache_limits(Session, &Limits) == VS_INTEROP_STATUS_OK,
		        "set session JIT cache limits"))
		{
			return false;
		}
		Limits = {};
		if (!Expect(vs_interop_session_get_jit_cache_limits(Session, &Limits) == VS_INTEROP_STATUS_OK && Limits.struct_size == sizeof(Limits) && Limits.max_native_bytes == 4u * 1024u * 1024u && Limits.max_retained_payload_bytes == 16u * 1024u * 1024u,
		        "get session JIT cache limits with zero-sized output"))
		{
			return false;
		}

		const std::string Source =
		    "int state=7; int next(){state++;return state;} "
		    "int add(int a,int b){return a+b;} void main(){}";
		vs_interop_run_result Result{sizeof(Result)};
		if (!Expect(vs_interop_session_load_source(Session, View(Source),
		                View("interop-session-cache.vs"), &Result) == VS_INTEROP_STATUS_OK,
		        "JIT cache source load"))
		{
			return false;
		}
		FValue Return;
		std::int32_t Value = 0;
		if (!Expect(vs_interop_session_call_function(Session, View("next"), nullptr, 0,
		                Return.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_int32(Return.Handle, &Value) == VS_INTEROP_STATUS_OK && Value == 8,
		        "cold cached function call"))
		{
			return false;
		}
		vs_interop_jit_stats Last{};
		Last.struct_size = sizeof(Last);
		vs_interop_jit_cache_stats Cache{};
		Cache.struct_size = sizeof(Cache);
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &Last) == VS_INTEROP_STATUS_OK && Last.compile_attempts > 0 && vs_interop_session_get_jit_cache_stats(Session, &Cache) == VS_INTEROP_STATUS_OK && Cache.compile_attempts > 0 && Cache.retained_payload_bytes > 0,
		        "cold call retains compiled session state"))
		{
			return false;
		}
		const std::uint64_t ColdAttempts = Cache.compile_attempts;
		if (!Expect(vs_interop_session_call_function(Session, View("next"), nullptr, 0,
		                Return.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_int32(Return.Handle, &Value) == VS_INTEROP_STATUS_OK && Value == 8,
		        "warm cached function call keeps fresh VM state"))
		{
			return false;
		}
		Last = {};
		Last.struct_size = sizeof(Last);
		Cache = {};
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &Last) == VS_INTEROP_STATUS_OK && Last.compile_attempts == 0 && vs_interop_session_get_jit_cache_stats(Session, &Cache) == VS_INTEROP_STATUS_OK && Cache.struct_size == sizeof(Cache) && Cache.compile_attempts == ColdAttempts && Cache.retained_payload_bytes > 0,
		        "legacy policy enables warm session-code reuse"))
		{
			return false;
		}

		Policy.struct_size = sizeof(Policy);
		Policy.reuse_session_code = 0;
		if (!Expect(vs_interop_session_set_jit_policy(Session, &Policy) == VS_INTEROP_STATUS_OK, "disable session-code reuse"))
		{
			return false;
		}
		for (int Index = 0; Index < 2; ++Index)
		{
			if (!Expect(vs_interop_session_call_function(Session, View("next"), nullptr, 0,
			                Return.Handle, &Result) == VS_INTEROP_STATUS_OK &&
			            vs_interop_value_get_int32(Return.Handle, &Value) == VS_INTEROP_STATUS_OK && Value == 8,
			        "uncached function call"))
			{
				return false;
			}
			Last = {};
			Last.struct_size = sizeof(Last);
			if (!Expect(vs_interop_session_get_last_jit_stats(Session, &Last) == VS_INTEROP_STATUS_OK && Last.compile_attempts > 0,
			        "explicit session-code reuse opt-out"))
			{
				return false;
			}
		}
		Cache = {};
		Cache.struct_size = sizeof(Cache);
		if (!Expect(vs_interop_session_get_jit_cache_stats(Session, &Cache) == VS_INTEROP_STATUS_OK && Cache.compile_attempts == 0 && Cache.retained_payload_bytes == 0 && Cache.native_bytes == 0,
		        "disabled cache retains no resources"))
		{
			return false;
		}

		Policy.reuse_session_code = 1;
		Limits = {};
		Limits.struct_size = sizeof(Limits);
		Limits.max_native_bytes = 4u * 1024u * 1024u;
		Limits.max_retained_payload_bytes = 1;
		if (!Expect(vs_interop_session_set_jit_policy(Session, &Policy) == VS_INTEROP_STATUS_OK && vs_interop_session_set_jit_cache_limits(Session, &Limits) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Session, View("next"), nullptr, 0, Return.Handle, &Result) == VS_INTEROP_STATUS_OK,
		        "resource-limited function call falls back safely"))
		{
			return false;
		}
		Cache = {};
		Cache.struct_size = sizeof(Cache);
		if (!Expect(vs_interop_session_get_jit_cache_stats(Session, &Cache) == VS_INTEROP_STATUS_OK && Cache.resource_limit_reached && Cache.retained_payload_bytes == 0 && Cache.native_bytes == 0,
		        "session JIT cache resource-limit diagnostics"))
		{
			return false;
		}

		return Expect(vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK,
		    "JIT cache session destruction");
	}

	bool TestJitExecutionStatistics()
	{
		if (!HasTestJitBackend()) return true;
		std::unique_ptr<vs_interop_session, decltype(&vs_interop_session_destroy)>
		    Session(CreateSession(), &vs_interop_session_destroy);
		if (!Expect(Session != nullptr, "execution statistics session")) return false;
		vs_interop_jit_backend_info Info{sizeof(Info)};
		if (!Expect(vs_interop_session_get_jit_backend_info(Session.get(), &Info) ==
						VS_INTEROP_STATUS_OK,
				"execution statistics backend metadata"))
		{
			return false;
		}
		vs_interop_jit_policy Policy{sizeof(Policy)};
		Policy.mixed_hot_call_threshold = 1;
		Policy.max_compile_attempts = Policy.max_cached_entries = Policy.eager_compile_budget = 128;
		Policy.reuse_session_code = 1;
		FValue Return;
		if (!Expect(Return.Handle != nullptr, "execution statistics return slot")) return false;
		const auto OrdinaryEntries = [](const vs_interop_jit_stats& Stats)
		{
			return Stats.native_leaf_executions + Stats.native_cfg_executions +
			    Stats.bridge_executions + Stats.linear_executions + Stats.vm_trampoline_executions;
		};
		for (const auto Backend : {VS_INTEROP_BACKEND_VM, VS_INTEROP_BACKEND_JIT,
		         VS_INTEROP_BACKEND_MIXED})
		{
			vs_interop_config Config{sizeof(Config)};
			Config.execution_backend = Backend;
			vs_interop_run_result Result{sizeof(Result)};
			if (!Expect(vs_interop_session_apply_config(Session.get(), &Config) == VS_INTEROP_STATUS_OK &&
							vs_interop_session_set_jit_policy(Session.get(), &Policy) == VS_INTEROP_STATUS_OK &&
							vs_interop_session_load_source(Session.get(), View("int Answer(){return 7;}"
																			   "int Sum(int n){int total=0;for(int i=0;i<n;i++)total+=i;return total;}"
																			   "int Protected(){try{return 9;}finally{int ignored=1;}}"
																			   "int Divide(int a,int b){return a/b;}void main(){}"),
								View("interop-execution-statistics.vs"), &Result) == VS_INTEROP_STATUS_OK,
					"execution statistics configuration and load"))
			{
				return false;
			}
			for (int Call = 0; Call < 2; ++Call)
			{
				vs_interop_jit_stats Stats{sizeof(Stats)};
				std::int32_t Value = 0;
				if (!Expect(vs_interop_session_call_function(Session.get(), View("Answer"), nullptr, 0,
								Return.Handle, &Result) == VS_INTEROP_STATUS_OK &&
								Result.success &&
								vs_interop_value_get_int32(Return.Handle, &Value) == VS_INTEROP_STATUS_OK && Value == 7 &&
								vs_interop_session_get_last_jit_stats(Session.get(), &Stats) == VS_INTEROP_STATUS_OK &&
								Stats.struct_size == sizeof(Stats) && Stats.scalar_native_fast_path_calls == 0 &&
								(Backend == VS_INTEROP_BACKEND_VM ? OrdinaryEntries(Stats) == 0 : OrdinaryEntries(Stats) == 1 && (Info.native_code ? Stats.native_leaf_executions + Stats.native_cfg_executions == 1 : Stats.native_leaf_executions + Stats.native_cfg_executions == 0)) &&
								(Call == 0 || Stats.compile_attempts == 0),
						"cold and warm calls report per-call entries separately from compilation and host calls"))
				{
					return false;
				}
			}
			FValue Argument, Zero;
			vs_interop_value* const Arguments[] = {Argument.Handle, Zero.Handle};
			if (!Expect(Argument.Handle && Zero.Handle &&
							vs_interop_value_set_int32(Argument.Handle, 16) == VS_INTEROP_STATUS_OK &&
							vs_interop_value_set_int32(Zero.Handle, 0) == VS_INTEROP_STATUS_OK &&
							vs_interop_session_call_function(Session.get(), View("Sum"), Arguments, 1,
								Return.Handle, &Result) == VS_INTEROP_STATUS_OK,
					"CFG execution statistics fixture"))
			{
				return false;
			}
			vs_interop_jit_stats Stats{sizeof(Stats)};
			std::int32_t Value = 0;
			if (!Expect(vs_interop_value_get_int32(Return.Handle, &Value) == VS_INTEROP_STATUS_OK && Value == 120 &&
							vs_interop_session_get_last_jit_stats(Session.get(), &Stats) == VS_INTEROP_STATUS_OK &&
							(Backend == VS_INTEROP_BACKEND_VM || !Info.native_code ||
								sizeof(void *) != 8 || Stats.native_cfg_executions == 1),
					"native CFG actual entries are copied through the C ABI"))
			{
				return false;
			}
			if (!Expect(vs_interop_session_call_function(Session.get(), View("Protected"), nullptr, 0,
							Return.Handle, &Result) == VS_INTEROP_STATUS_OK &&
							vs_interop_value_get_int32(Return.Handle, &Value) == VS_INTEROP_STATUS_OK && Value == 9 &&
							vs_interop_session_get_last_jit_stats(Session.get(), &Stats) == VS_INTEROP_STATUS_OK &&
							OrdinaryEntries(Stats) == Stats.vm_trampoline_executions &&
							Stats.vm_trampoline_executions == Stats.dispatches,
					"canonical VM fallback reports only trampoline entries actually dispatched"))
			{
				return false;
			}
			if (!Expect(vs_interop_value_set_int32(Return.Handle, 99) == VS_INTEROP_STATUS_OK &&
							vs_interop_session_call_function(Session.get(), View("Divide"), Arguments, 2,
								Return.Handle, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR &&
							!Result.success &&
							vs_interop_value_get_int32(Return.Handle, &Value) == VS_INTEROP_STATUS_OK && Value == 99 &&
							vs_interop_session_get_last_jit_stats(Session.get(), &Stats) == VS_INTEROP_STATUS_OK &&
							(Backend == VS_INTEROP_BACKEND_VM ? OrdinaryEntries(Stats) == 0 : OrdinaryEntries(Stats) == 1),
					"failed entries count once and preserve the output value"))
			{
				return false;
			}
			vs_interop_jit_stats Prefix;
			std::memset(&Prefix, 0xA5, sizeof(Prefix));
			Prefix.struct_size = static_cast<std::uint32_t>(
			    offsetof(vs_interop_jit_stats, native_leaf_executions));
			const auto Before = Prefix;
			if (!Expect(vs_interop_session_get_last_jit_stats(Session.get(), &Prefix) == VS_INTEROP_STATUS_OK &&
							Prefix.struct_size == sizeof(Prefix) &&
							std::memcmp(reinterpret_cast<const char *>(&Prefix) + Before.struct_size,
								reinterpret_cast<const char *>(&Before) + Before.struct_size,
								sizeof(Prefix) - Before.struct_size) == 0,
					"bounded statistics output preserves all omitted execution counters"))
			{
				return false;
			}
			const auto Preserved = Stats;
			if (!Expect(vs_interop_session_get_last_jit_stats(nullptr, &Stats) == VS_INTEROP_STATUS_INVALID_ARGUMENT &&
							std::memcmp(&Stats, &Preserved, sizeof(Stats)) == 0 &&
							vs_interop_session_clear(Session.get()) == VS_INTEROP_STATUS_OK &&
							vs_interop_session_get_last_jit_stats(Session.get(), &Stats) == VS_INTEROP_STATUS_OK &&
							OrdinaryEntries(Stats) == 0,
					"failed statistics queries preserve output and session clear resets actual entries"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestBindingsAndHooks()
	{
		vs_interop_session* Session = CreateSession();
		if (!Expect(Session != nullptr, "binding session creation"))
		{
			return false;
		}
		vs_interop_config Config{};
		Config.struct_size = sizeof(Config);
		Config.execution_backend = HasTestJitBackend() ? VS_INTEROP_BACKEND_JIT : VS_INTEROP_BACKEND_MIXED;
		if (!Expect(vs_interop_session_apply_config(Session, &Config) == VS_INTEROP_STATUS_OK, "Interop JIT backend configuration"))
		{
			return false;
		}
		vs_interop_jit_policy JitPolicy{};
		JitPolicy.struct_size = sizeof(JitPolicy);
		JitPolicy.mixed_hot_call_threshold = 2;
		JitPolicy.max_compile_attempts = 128;
		JitPolicy.max_cached_entries = 256;
		JitPolicy.eager_compile_budget = 64;
		if (!Expect(vs_interop_session_set_jit_policy(Session, &JitPolicy) == VS_INTEROP_STATUS_OK, "JIT policy configuration"))
		{
			return false;
		}
		const vs_interop_annotation_argument AnnotationArgument{
		    View("Category"), View("Interop"), 1, {0}};
		vs_interop_annotation Annotation{};
		Annotation.struct_size = sizeof(Annotation);
		Annotation.name = View("host_visible");
		Annotation.arguments = &AnnotationArgument;
		Annotation.argument_count = 1;
		vs_interop_annotation_argument_desc SupportedArgument{};
		SupportedArgument.struct_size = sizeof(SupportedArgument);
		SupportedArgument.name = View("Category");
		SupportedArgument.detail = View("string");
		SupportedArgument.documentation = View("Tooling category");
		vs_interop_annotation_desc SupportedAnnotation{};
		SupportedAnnotation.struct_size = sizeof(SupportedAnnotation);
		SupportedAnnotation.name = View("host_visible");
		SupportedAnnotation.detail = View("annotation (Category)");
		SupportedAnnotation.documentation = View("Marks a declaration visible to the host");
		SupportedAnnotation.arguments = &SupportedArgument;
		SupportedAnnotation.argument_count = 1;
		SupportedAnnotation.name_flags = 2u;
		if (!Expect(vs_interop_session_register_annotation(Session, &SupportedAnnotation) ==
		            VS_INTEROP_STATUS_INVALID_ARGUMENT,
		        "unknown annotation name flags are rejected"))
		{
			return false;
		}

		SupportedAnnotation.name_flags = VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE;
		if (!Expect(vs_interop_session_register_annotation(Session, &SupportedAnnotation) == VS_INTEROP_STATUS_OK, "annotation vocabulary registration"))
		{
			return false;
		}

		vs_interop_native_type_desc IntType{};
		IntType.struct_size = sizeof(IntType);
		IntType.type = VS_INTEROP_NATIVE_INT32;
		vs_interop_native_param_desc IntParameter{};
		IntParameter.struct_size = sizeof(IntParameter);
		IntParameter.type = IntType;
		const vs_interop_string_view ParameterNames[] = {View("seed")};
		const vs_interop_string_view ParameterDocs[] = {View("Ignored test seed")};
		const vs_interop_annotation_list ParameterAnnotations[] = {{&Annotation, 1}};
		vs_interop_host_function_desc HostFunction{};
		HostFunction.struct_size = sizeof(HostFunction);
		HostFunction.name = View("structured_host");
		HostFunction.return_type = IntType;
		HostFunction.parameters = &IntParameter;
		HostFunction.parameter_count = 1;
		HostFunction.callback = &StructuredHostFunction;
		HostFunction.documentation = View("Structured free host function");
		HostFunction.parameter_names = {ParameterNames, 1};
		HostFunction.parameter_documentation = {ParameterDocs, 1};
		HostFunction.return_documentation = View("The answer");
		HostFunction.annotations = {&Annotation, 1};
		HostFunction.parameter_annotations = ParameterAnnotations;
		HostFunction.parameter_annotation_count = 1;
		auto BuiltinConflict = HostFunction;
		BuiltinConflict.name = View("print");
		if (!Expect(vs_interop_session_register_host_function(Session, &BuiltinConflict) == VS_INTEROP_STATUS_ENGINE_ERROR,
		        "structured host function rejects builtin name conflicts"))
		{
			return false;
		}
		if (!Expect(vs_interop_session_register_host_function(Session, &HostFunction) == VS_INTEROP_STATUS_OK, "structured host function registration"))
		{
			return false;
		}
		auto StringParameter = IntParameter;
		StringParameter.type.type = VS_INTEROP_NATIVE_STRING;
		auto StringFunction = HostFunction;
		StringFunction.parameters = &StringParameter;
		if (!Expect(vs_interop_session_register_host_function(Session, &StringFunction) == VS_INTEROP_STATUS_OK, "structured host function overload registration"))
		{
			return false;
		}

		vs_interop_host_type_desc Type{};
		Type.struct_size = sizeof(Type);
		Type.name = View("HostVisible");
		Type.kind = VS_INTEROP_HOST_CLASS;
		Type.documentation = View("Visible host class");
		Type.annotations = {&Annotation, 1};
		std::uint32_t TypeId = 0;
		if (!Expect(vs_interop_session_register_host_type(Session, &Type, &TypeId) == VS_INTEROP_STATUS_OK && TypeId != 0, "annotated host type registration"))
		{
			return false;
		}
		vs_interop_host_type_desc LegacyType{};
		LegacyType.struct_size = static_cast<std::uint32_t>(
		    offsetof(vs_interop_host_type_desc, enum_underlying_type_name));
		LegacyType.name = View("LegacySizedHostValue");
		LegacyType.kind = VS_INTEROP_HOST_STRUCT;
		std::uint32_t LegacyTypeId = 0;
		if (!Expect(vs_interop_session_register_host_type(
		                Session, &LegacyType, &LegacyTypeId) == VS_INTEROP_STATUS_OK &&
		            LegacyTypeId != 0,
		        "appended host type fields preserve struct_size compatibility"))
		{
			return false;
		}
		vs_interop_host_type_desc EnumType{};
		EnumType.struct_size = sizeof(EnumType);
		EnumType.name = View("HostMode");
		EnumType.kind = VS_INTEROP_HOST_ENUM;
		EnumType.scoped_enum = 1;
		EnumType.enum_underlying_type_name = View("int64");
		std::uint32_t EnumTypeId = 0;
		if (!Expect(vs_interop_session_register_host_type(
		                Session, &EnumType, &EnumTypeId) == VS_INTEROP_STATUS_OK &&
		            EnumTypeId != 0,
		        "interop host enum underlying type registration"))
		{
			return false;
		}
		auto InvalidEnumType = EnumType;
		InvalidEnumType.name = View("InvalidWideMode");
		InvalidEnumType.enum_underlying_type_name = View("uint64");
		std::uint32_t InvalidEnumTypeId = 0;
		if (!Expect(vs_interop_session_register_host_type(
		                Session, &InvalidEnumType, &InvalidEnumTypeId) == VS_INTEROP_STATUS_ENGINE_ERROR,
		        "interop must reject a lossy uint64 host enum ABI"))
		{
			return false;
		}

		FGenericFactoryState GenericFactoryState{Session};
		std::uint32_t TemplateId = 0;
		if (!Expect(vs_interop_session_register_generic_host_type_template(
		                Session, View("Box"), 1, View("Generic box"), &BuildGenericBox,
		                &GenericFactoryState,
		                &TemplateId) == VS_INTEROP_STATUS_OK &&
		            TemplateId != 0,
		        "generic host template registration"))
		{
			return false;
		}
		const vs_interop_string_view TypeArgs[] = {View("int32")};
		std::uint32_t ConcreteTypeId = 0;
		if (!Expect(vs_interop_session_instantiate_generic_host_type(Session, TemplateId,
		                {TypeArgs, 1}, &ConcreteTypeId) == VS_INTEROP_STATUS_OK &&
		            ConcreteTypeId != 0 && GenericFactoryState.ReentrantClearStatus == VS_INTEROP_STATUS_ENGINE_ERROR,
		        "generic host type instantiation and reentrancy guard"))
		{
			return false;
		}
		if (!Expect(vs_interop_session_register_external_serializer(Session, View("Counter"),
		                3, &SerializeCounter, &DeserializeCounter, nullptr) == VS_INTEROP_STATUS_OK,
		        "external serializer registration"))
		{
			return false;
		}

		vs_interop_buffer* Bindings = nullptr;
		if (!Expect(vs_interop_session_export_bindings_json(Session, &Bindings) == VS_INTEROP_STATUS_OK && Bindings, "binding JSON export"))
		{
			return false;
		}
		const auto BindingBytes = CopyBuffer(Bindings);
		vs_interop_buffer_destroy(Bindings);
		const std::string BindingJson(BindingBytes.begin(), BindingBytes.end());
		const bool bBindingJsonComplete = BindingJson.find("HostVisible") != std::string::npos && BindingJson.find("\"caseSensitive\": false") != std::string::npos && BindingJson.find("HostMode") != std::string::npos && BindingJson.find("\"enumUnderlyingType\": \"long\"") != std::string::npos && BindingJson.find("InvalidWideMode") == std::string::npos && BindingJson.find("host_visible") != std::string::npos && BindingJson.find("structured_host") != std::string::npos && BindingJson.find("Structured free host function") != std::string::npos && BindingJson.find("Ignored test seed") != std::string::npos && BindingJson.find("\"paramAnnotations\": [[") != std::string::npos && BindingJson.find("Tooling category") != std::string::npos && BindingJson.find("\"genericTemplateName\": \"Box\"") != std::string::npos && BindingJson.find("\"genericTypeArguments\": [\"int\"]") != std::string::npos;
		if (!Expect(bBindingJsonComplete,
		        "binding JSON preserves declarations and generic identity"))
		{
			std::cerr << BindingJson << '\n';
			return false;
		}

		const std::string Source =
		    "import \"io.vs\"; "
		    "export int SharedValue = 9; "
		    "public static class StaticUtility { int answer() { return 42; } }; "
		    "class Counter { public: mutable int value; Counter(int v) { value = v; } "
		    "int Add(int delta) { value += delta; return value; } }; "
		    "interface IDispatch { virtual int add(int value) = 0; }; "
		    "class AddOne : IDispatch { int add(int value) { return value + 1; } }; "
		    "class AddTwo : IDispatch { int add(int value) { return value + 2; } }; "
		    "int RunDispatchStats() { IDispatch a=AddOne(); IDispatch b=AddTwo(); "
		    "IDispatch selected=a; int total=0; for(int i=0; i<16; i++) { "
		    "int choice=i&1; if(choice==0) selected=a; else selected=b; "
		    "total=selected.add(total); } return total; } "
		    "int DirectLeaf(int x) { for (int i=0; i<4; i++) { "
		    "x=(x*3+1)%10007; } return x; } "
		    "int RunDirectStats(int x) { int total=0; for (int i=0; i<3; i++) { "
		    "total += DirectLeaf(x+i); } return total; } "
		    "array<int> MakeCapturedArray() { array<int> values = {1}; "
		    "int& element = values[0]; function()->int reader = [&element]() -> int { "
		    "return element; }; element = 5; return values; } "
		    "string ReadInput() { return std::io::read_line(); } "
		    "int CallStructuredHost() { assert(structured_host(\"overload\")==42); return structured_host(0); } "
		    "int ReadReadonly() { readonly<int> value = 41; return value.get(); } "
		    "readonly<int> MakeReadonly(int value) { return value; } "
		    "array<readonly<int>> WrapReadonly(int value) { "
		    "array<readonly<int>> result = {readonly<int>(value)}; return result; } "
		    "void main() {}";
		vs_interop_run_result Result{sizeof(Result)};
		if (!Expect(vs_interop_session_load_source(
		                Session, View(Source), View("extended.vs"), &Result) == VS_INTEROP_STATUS_OK,
		        "hook source load"))
		{
			std::uint64_t ErrorSize = 0;
			(void)vs_interop_session_copy_last_error(Session, nullptr, 0, &ErrorSize);
			std::string ErrorText(static_cast<std::size_t>(ErrorSize), '\0');
			(void)vs_interop_session_copy_last_error(
			    Session, ErrorText.data(), ErrorSize, &ErrorSize);
			std::cerr << ErrorText << '\n';
			return false;
		}
		FValue DispatchReturn;
		std::int32_t DispatchValue = 0;
		if (!Expect(vs_interop_session_call_function(Session, View("RunDispatchStats"),
		                nullptr, 0, DispatchReturn.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_int32(DispatchReturn.Handle, &DispatchValue) == VS_INTEROP_STATUS_OK && DispatchValue == 24,
		        "polymorphic dispatch through the Interop JIT backend"))
		{
			return false;
		}
		vs_interop_jit_stats DispatchStats{};
		DispatchStats.struct_size = sizeof(DispatchStats);
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &DispatchStats) == VS_INTEROP_STATUS_OK && DispatchStats.dispatch_attempts >= DispatchStats.dispatches && DispatchStats.dispatches >= DispatchStats.scalar_method_fast_path_i32,
		        "Interop forwards coherent JIT dispatch statistics"))
		{
			return false;
		}
		const vs_interop_string_view HostPlatform = vs_interop_get_host_platform();
		const std::string HostPlatformText = HostPlatform.data
		    ? std::string(HostPlatform.data, static_cast<std::size_t>(HostPlatform.size))
		    : std::string{};
		const auto HasPlatformSuffix = [&HostPlatformText](const std::string_view Suffix)
		{
			return HostPlatformText.size() >= Suffix.size() && HostPlatformText.compare(HostPlatformText.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
		};
		const bool bHasNativeScalarCfg =
		    (vs_interop_get_capabilities() & VS_INTEROP_CAP_NATIVE_JIT) != 0 && (HasPlatformSuffix("-x64") || HasPlatformSuffix("-arm64"));
		if (!Expect(!bHasNativeScalarCfg || DispatchStats.scalar_method_fast_path_i32 >= 16,
		        "Interop observes the selected-receiver scalar JIT fast path"))
		{
			return false;
		}
		FValue DirectReturn;
		FValue DirectArgument;
		std::int32_t DirectValue = 0;
		if (!Expect(vs_interop_value_set_int32(DirectArgument.Handle, 2) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Session, View("RunDirectStats"), &DirectArgument.Handle, 1, DirectReturn.Handle, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(DirectReturn.Handle, &DirectValue) == VS_INTEROP_STATUS_OK && DirectValue == 849,
		        "generation-scoped direct scalar call through Interop"))
		{
			return false;
		}
		DispatchStats = {};
		DispatchStats.struct_size = sizeof(DispatchStats);
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &DispatchStats) == VS_INTEROP_STATUS_OK && (!bHasNativeScalarCfg || DispatchStats.direct_native_calls >= 3),
		        "Interop forwards direct native-call statistics"))
		{
			return false;
		}
		std::uint8_t HasMethod = 0;
		std::uint8_t HasFunction = 0;
		FValue StructuredReturn;
		std::int32_t StructuredValue = 0;
		if (!Expect(vs_interop_session_call_function(Session, View("CallStructuredHost"),
		                nullptr, 0, StructuredReturn.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_int32(StructuredReturn.Handle, &StructuredValue) == VS_INTEROP_STATUS_OK && StructuredValue == 42,
		        "structured host function dispatch"))
		{
			return false;
		}
		FValue ReadonlyReturn;
		std::int32_t ReadonlyValue = 0;
		if (!Expect(vs_interop_session_call_function(Session, View("ReadReadonly"),
		                nullptr, 0, ReadonlyReturn.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_int32(ReadonlyReturn.Handle, &ReadonlyValue) == VS_INTEROP_STATUS_OK && ReadonlyValue == 41,
		        "readonly values should cross an Interop function call"))
		{
			return false;
		}

		vs_interop_buffer* Reflection = nullptr;
		if (!Expect(vs_interop_session_export_reflection_json(Session, &Reflection) == VS_INTEROP_STATUS_OK && Reflection, "reflection JSON export"))
		{
			return false;
		}
		const auto ReflectionBytes = CopyBuffer(Reflection);
		vs_interop_buffer_destroy(Reflection);
		const std::string ReflectionJson(ReflectionBytes.begin(), ReflectionBytes.end());
		if (!Expect(ReflectionJson.find("Counter") != std::string::npos && ReflectionJson.find("StaticUtility") != std::string::npos && ReflectionJson.find("ReadInput") != std::string::npos && ReflectionJson.find("ReadReadonly") != std::string::npos && ReflectionJson.find("MakeReadonly") != std::string::npos && ReflectionJson.find("readonly<int>") != std::string::npos && ReflectionJson.find("array<readonly<int>>") != std::string::npos && ReflectionJson.find("\"globals\":[") != std::string::npos && ReflectionJson.find("SharedValue") != std::string::npos && ReflectionJson.find("\"modulePath\":") != std::string::npos && ReflectionJson.find("\"returnsRef\":") != std::string::npos && ReflectionJson.find("\"mutable\":true") != std::string::npos && ReflectionJson.find("\"static\":true") != std::string::npos && ReflectionJson.find("linkageName") != std::string::npos,
		        "reflection JSON completeness"))
		{
			std::cerr << ReflectionJson << '\n';
			return false;
		}

		FValue CapturedArray;
		FValue CapturedElement;
		std::int32_t CapturedValue = 0;
		if (!Expect(vs_interop_session_call_function(Session, View("MakeCapturedArray"),
		                nullptr, 0, CapturedArray.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_sequence_get(CapturedArray.Handle, 0, CapturedElement.Handle) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(CapturedElement.Handle, &CapturedValue) == VS_INTEROP_STATUS_OK && CapturedValue == 5,
		        "closure reference cells remain private across the Interop value ABI"))
		{
			return false;
		}

		FHookState HookState;
		if (!Expect(vs_interop_session_set_instance_call_interceptor(
		                Session, &InterceptAdd, &HookState) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_set_mutation_callback(Session, &OnMutation, &HookState) == VS_INTEROP_STATUS_OK,
		        "session-local hook registration"))
		{
			return false;
		}
		FValue Argument;
		FValue Object;
		FValue Return;
		(void)vs_interop_value_set_int32(Argument.Handle, 5);
		vs_interop_value* Args[] = {Argument.Handle};
		if (!Expect(vs_interop_session_construct_object(Session, View("Counter"), Args, 1,
		                Object.Handle, &Result) == VS_INTEROP_STATUS_OK,
		        "hook object construction"))
		{
			return false;
		}
		if (!Expect(vs_interop_session_has_method(
		                Session, Object.Handle, View("Add"), 1, &HasMethod) == VS_INTEROP_STATUS_OK &&
		            HasMethod && vs_interop_session_has_function(Session, View("ReadInput"), 0, &HasFunction) == VS_INTEROP_STATUS_OK && HasFunction,
		        "function and method discovery"))
		{
			return false;
		}
		(void)vs_interop_value_set_int32(Argument.Handle, 2);
		std::int32_t Returned = 0;
		if (!Expect(vs_interop_session_call_method(Session, Object.Handle, View("Add"), Args, 1,
		                Return.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_int32(Return.Handle, &Returned) == VS_INTEROP_STATUS_OK && Returned == 107 && HookState.Intercepts == 1 && HookState.Mutations != 0,
		        "interceptor proceed and mutation callback"))
		{
			return false;
		}
		if (!Expect(vs_interop_session_call_method_bypassing_interceptor(
		                Session, Object.Handle, View("Add"), Args, 1, Return.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_int32(Return.Handle, &Returned) == VS_INTEROP_STATUS_OK && Returned == 9 && HookState.Intercepts == 1,
		        "explicit interceptor bypass"))
		{
			return false;
		}
		vs_interop_buffer* Serialized = nullptr;
		std::uint32_t EncodedVersion = 0;
		if (!Expect(vs_interop_session_serialize_object(
		                Session, Object.Handle, &Serialized, &EncodedVersion) == VS_INTEROP_STATUS_OK &&
		            Serialized && EncodedVersion == 3,
		        "external object serialization"))
		{
			return false;
		}
		const auto SerializedBytes = CopyBuffer(Serialized);
		vs_interop_buffer_destroy(Serialized);
		if (!Expect(SerializedBytes.size() == 4 && SerializedBytes[0] == 9,
		        "external serialized field bytes"))
		{
			return false;
		}
		const std::uint8_t Replacement[] = {9, 0, 0, 0};
		if (!Expect(vs_interop_session_deserialize_object(
		                Session, Object.Handle, {Replacement, 4}, 3) == VS_INTEROP_STATUS_OK,
		        "external object deserialization"))
		{
			return false;
		}
		FValue Field;
		if (!Expect(vs_interop_session_get_field(
		                Session, Object.Handle, View("value"), Field.Handle, &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_get_int32(Field.Handle, &Returned) == VS_INTEROP_STATUS_OK && Returned == 9,
		        "deserialized field assignment"))
		{
			return false;
		}
		int ReferenceCount = 0;
		if (!Expect(vs_interop_session_enumerate_object_references(
		                Session, Object.Handle, &CountReference, &ReferenceCount) == VS_INTEROP_STATUS_OK &&
		            ReferenceCount == 0,
		        "external reference enumeration"))
		{
			return false;
		}
		if (!Expect(vs_interop_session_apply_class_defaults(Session, Object.Handle, &Result) == VS_INTEROP_STATUS_OK, "class defaults invocation"))
		{
			return false;
		}

		if (!Expect(vs_interop_session_set_input(Session, View("hello\n")) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Session, View("ReadInput"), nullptr, 0, Return.Handle, &Result) == VS_INTEROP_STATUS_OK,
		        "session input"))
		{
			return false;
		}
		vs_interop_string_view Input{};
		if (!Expect(vs_interop_value_get_string(Return.Handle, &Input) == VS_INTEROP_STATUS_OK && std::string(Input.data, static_cast<std::size_t>(Input.size)) == "hello",
		        "input value"))
		{
			return false;
		}
		vs_interop_jit_stats JitStats{};
		JitStats.struct_size = sizeof(JitStats);
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &JitStats) == VS_INTEROP_STATUS_OK && JitStats.struct_size == sizeof(JitStats),
		        "JIT statistics query"))
		{
			return false;
		}
		constexpr std::uint32_t LegacyJitStatsSize = static_cast<std::uint32_t>(
		    offsetof(vs_interop_jit_stats, scalar_method_fast_path_mixed));
		constexpr std::uint64_t MethodCanary = UINT64_C(0x13579BDF2468ACE0);
		constexpr std::uint64_t FunctionCanary = UINT64_C(0x0FEDCBA987654321);
		constexpr std::uint64_t DirectCanary = UINT64_C(0x55AA55AA33CC33CC);
		constexpr std::uint64_t CostModelCanary = UINT64_C(0x1122334455667788);
		constexpr std::uint64_t TypedLoopCanary = UINT64_C(0x8877665544332211);
		vs_interop_jit_stats ZeroSizeJitStats{};
		ZeroSizeJitStats.scalar_method_fast_path_mixed = MethodCanary;
		ZeroSizeJitStats.scalar_function_fast_path_mixed = FunctionCanary;
		ZeroSizeJitStats.direct_native_calls = DirectCanary;
		ZeroSizeJitStats.cost_model_vm_fallbacks = CostModelCanary;
		ZeroSizeJitStats.native_typed_loop_executions = TypedLoopCanary;
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &ZeroSizeJitStats) == VS_INTEROP_STATUS_OK && ZeroSizeJitStats.struct_size == sizeof(vs_interop_jit_stats) && ZeroSizeJitStats.scalar_method_fast_path_mixed == MethodCanary && ZeroSizeJitStats.scalar_function_fast_path_mixed == FunctionCanary && ZeroSizeJitStats.direct_native_calls == DirectCanary && ZeroSizeJitStats.cost_model_vm_fallbacks == CostModelCanary && ZeroSizeJitStats.native_typed_loop_executions == TypedLoopCanary,
		        "zero-size legacy JIT statistics query preserves appended storage"))
		{
			return false;
		}
		vs_interop_jit_stats LegacyJitStats{};
		LegacyJitStats.struct_size = LegacyJitStatsSize;
		LegacyJitStats.scalar_method_fast_path_mixed = MethodCanary;
		LegacyJitStats.scalar_function_fast_path_mixed = FunctionCanary;
		LegacyJitStats.direct_native_calls = DirectCanary;
		LegacyJitStats.cost_model_vm_fallbacks = CostModelCanary;
		LegacyJitStats.native_typed_loop_executions = TypedLoopCanary;
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &LegacyJitStats) == VS_INTEROP_STATUS_OK && LegacyJitStats.struct_size == sizeof(vs_interop_jit_stats) && LegacyJitStats.scalar_method_fast_path_mixed == MethodCanary && LegacyJitStats.scalar_function_fast_path_mixed == FunctionCanary && LegacyJitStats.direct_native_calls == DirectCanary && LegacyJitStats.cost_model_vm_fallbacks == CostModelCanary && LegacyJitStats.native_typed_loop_executions == TypedLoopCanary,
		        "legacy JIT statistics query preserves appended storage"))
		{
			return false;
		}
		constexpr std::uint32_t PreviousJitStatsSize = static_cast<std::uint32_t>(
		    offsetof(vs_interop_jit_stats, cost_model_vm_fallbacks));
		vs_interop_jit_stats PreviousJitStats{};
		PreviousJitStats.struct_size = PreviousJitStatsSize;
		PreviousJitStats.cost_model_vm_fallbacks = CostModelCanary;
		PreviousJitStats.native_typed_loop_executions = TypedLoopCanary;
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &PreviousJitStats) == VS_INTEROP_STATUS_OK && PreviousJitStats.struct_size == sizeof(vs_interop_jit_stats) && PreviousJitStats.cost_model_vm_fallbacks == CostModelCanary && PreviousJitStats.native_typed_loop_executions == TypedLoopCanary,
		        "previous JIT statistics query preserves newly appended storage"))
		{
			return false;
		}
		vs_interop_jit_stats TooSmallJitStats{};
		TooSmallJitStats.struct_size = LegacyJitStatsSize - 1;
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &TooSmallJitStats) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
		        "undersized JIT statistics query is rejected"))
		{
			return false;
		}
		return Expect(vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK,
		    "binding session destruction");
	}

	bool TestBytecodeCacheConfigurationOwnership()
	{
		const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path Root = std::filesystem::temp_directory_path() / ("veroscript-interop-config-" + std::to_string(Stamp));
		if (!Expect(std::filesystem::create_directory(Root), "cache fixture directory"))
		{
			return false;
		}
		struct FCleanup
		{
			~FCleanup()
			{
				if (Session)
				{
					(void)vs_interop_session_destroy(Session);
				}
				std::error_code Error;
				std::filesystem::remove_all(Root, Error);
			}

			std::filesystem::path Root;
			vs_interop_session* Session = nullptr;

		} Cleanup{Root, CreateSession()};
		if (!Expect(Cleanup.Session != nullptr, "cache configuration session"))
		{
			return false;
		}

		const auto ToUtf8 = [](const std::filesystem::path& Path)
		{
			const auto Bytes = Path.generic_u8string();
			return std::string(Bytes.begin(), Bytes.end());
		};
		const std::filesystem::path SourcePath = Root / "main.vs";
		const std::filesystem::path CacheDirectory = Root / ("bytecode cache " + std::string(64, 'x'));
		{
			std::ofstream Out(SourcePath, std::ios::binary | std::ios::trunc);
			Out << "int main() { return 42; }\n";
			if (!Expect(static_cast<bool>(Out), "cache fixture source"))
			{
				return false;
			}
		}
		std::string CacheDirectoryUtf8 = ToUtf8(CacheDirectory);
		std::string OverlayPathUtf8 = CacheDirectoryUtf8;
		OverlayPathUtf8.replace(OverlayPathUtf8.size() - 3, 3, "alt");
		// The equally sized overlay path also exercises allocations made while
		// converting the cache directory before the Session copies its configuration.
		const vs_interop_source_overlay Overlay{
		    View(OverlayPathUtf8), View("int unused() { return 1; }")};
		vs_interop_config Config{};
		Config.struct_size = sizeof(Config);
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		Config.use_bytecode_cache = 1;
		Config.bytecode_cache_directory = View(CacheDirectoryUtf8);
		Config.source_overlays = &Overlay;
		Config.source_overlay_count = 1;
		if (!Expect(vs_interop_session_apply_config(Cleanup.Session, &Config) == VS_INTEROP_STATUS_OK, "custom cache configuration"))
		{
			return false;
		}
		CacheDirectoryUtf8.assign(CacheDirectoryUtf8.size(), 'z');
		OverlayPathUtf8.assign(OverlayPathUtf8.size(), 'z');
		const std::string SourcePathUtf8 = ToUtf8(SourcePath);
		vs_interop_run_result Result{};
		Result.struct_size = sizeof(Result);
		if (!Expect(vs_interop_session_load_file(Cleanup.Session, View(SourcePathUtf8), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Cleanup.Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.exit_code == 42,
		        "custom cache script execution"))
		{
			return false;
		}
		if (!Expect(std::filesystem::is_directory(CacheDirectory),
		        "configured cache directory retained"))
		{
			return false;
		}
		for (const auto& Entry : std::filesystem::recursive_directory_iterator(CacheDirectory))
		{
			if (Entry.is_regular_file() && Entry.path().extension() == ".vsbc")
			{
				return true;
			}
		}
		return Expect(false, "bytecode written to configured cache directory");
	}

	bool TestMacroPackages()
	{
		const auto Root = std::filesystem::temp_directory_path() / ("veroscript-interop-macros-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		struct FCleanup
		{
			~FCleanup()
			{
				vs_interop_package_destroy(Package);
				vs_interop_buffer_destroy(Error);
				vs_interop_buffer_destroy(Bytes);
				vs_interop_session_destroy(Consumer);
				vs_interop_session_destroy(Builder);
				std::error_code ErrorCode;
				std::filesystem::remove(Root / "unstripped.vslib", ErrorCode);
				std::filesystem::remove(Root / "stripped.vslib", ErrorCode);
				std::filesystem::remove(Root, ErrorCode);
			}

			std::filesystem::path Root;

			vs_interop_session* Builder = nullptr;

			vs_interop_session* Consumer = nullptr;

			vs_interop_buffer* Bytes = nullptr;

			vs_interop_buffer* Error = nullptr;

			vs_interop_package* Package = nullptr;
		} Cleanup{Root};
		std::filesystem::create_directory(Root);
		const std::string LibraryPath = Utf8Path(Root / "library.vs");
		const std::string FacadePath = Utf8Path(Root / "facade.vs");
		const std::string ConsumerPath = Utf8Path(Root / "consumer.vs");
		const vs_interop_source_overlay Overlays[] = {
		    {View(LibraryPath), View("#define PRIVATE_HELPER 99\n"
		                             "#if BUILD_LEVEL == 21\n#define_export LEVEL 21\n"
		                             "#else\n#define_export LEVEL 99\n#endif\n"
		                             "#define_export ADD(x, ...) ((x) + LOCAL __VA_OPT__(+ __VA_ARGS__))\n"
		                             "export int compiled_level(){return LEVEL;}\n")},
		    {View(FacadePath), View("export import \"library.vs\";\n")}};
		const vs_interop_string_view BuildDefines[] = {View("BUILD_LEVEL=21")};
		const vs_interop_string_view ConsumerDefines[] = {View("BUILD_LEVEL=99")};
		vs_interop_config Config{};
		Config.struct_size = sizeof(Config);
		Config.execution_backend = VS_INTEROP_BACKEND_MIXED;
		Config.source_overlays = Overlays;
		Config.source_overlay_count = 2;
		Config.defines = {BuildDefines, 1};
		Cleanup.Builder = CreateSession();
		Cleanup.Consumer = CreateSession();
		if (!Expect(Cleanup.Builder && Cleanup.Consumer &&
		            vs_interop_session_apply_config(Cleanup.Builder, &Config) == VS_INTEROP_STATUS_OK,
		        "C ABI macro library builder configuration"))
		{
			return false;
		}

		Config.source_overlays = nullptr;
		Config.source_overlay_count = 0;
		Config.defines = {ConsumerDefines, 1};
		if (!Expect(vs_interop_session_apply_config(Cleanup.Consumer, &Config) == VS_INTEROP_STATUS_OK,
		        "C ABI macro library consumer configuration"))
		{
			return false;
		}

		vs_interop_package_build_options Options{};
		Options.struct_size = sizeof(Options);
		Options.kind = VS_INTEROP_PACKAGE_LIBRARY;
		Options.package_name = View("InteropMacros");
		Options.package_version = 1;
		vs_interop_run_result Result{};
		Result.struct_size = sizeof(Result);
		for (const uint8_t Strip : {uint8_t{0}, uint8_t{1}})
		{
			Options.strip_debug_info = Strip;
			if (!Expect(vs_interop_session_build_package_file(Cleanup.Builder, View(FacadePath), &Options,
			                &Cleanup.Bytes, &Result) == VS_INTEROP_STATUS_OK &&
			            Result.success,
			        "C ABI packages re-exported public macros"))
			{
				return false;
			}

			const auto Bytes = vs_interop_buffer_view(Cleanup.Bytes);
			if (!Expect(vs_interop_package_read(Bytes, &Cleanup.Package, &Cleanup.Error) ==
			                VS_INTEROP_STATUS_OK &&
			            Cleanup.Package && !Cleanup.Error &&
			            vs_interop_package_get_kind(Cleanup.Package) == VS_INTEROP_PACKAGE_LIBRARY &&
			            vs_interop_package_get_canonical_signed_bytes(Cleanup.Package).size > 0,
			        "C ABI validates macro-bearing library bytes"))
			{
				return false;
			}

			const auto Name = Strip ? "stripped.vslib" : "unstripped.vslib";
			{
				std::ofstream Out(Root / Name, std::ios::binary | std::ios::trunc);
				Out.write(reinterpret_cast<const char*>(Bytes.data), static_cast<std::streamsize>(Bytes.size));
				if (!Expect(Out.good(), "write C ABI macro library for import")) return false;
			}
			vs_interop_package_destroy(Cleanup.Package);
			Cleanup.Package = nullptr;
			vs_interop_buffer_destroy(Cleanup.Bytes);
			Cleanup.Bytes = nullptr;
			const std::string Source = std::string("import \"") + Name + "\";\n"
			                                                             "#ifdef PRIVATE_HELPER\n#error private macro leaked\n#endif\n"
			                                                             "#define LOCAL 1\nint main(){return ADD(LEVEL,20)+compiled_level();}\n";
			if (!Expect(vs_interop_session_load_source(Cleanup.Consumer, View(Source), View(ConsumerPath),
			                &Result) == VS_INTEROP_STATUS_OK &&
			            Result.success &&
			            vs_interop_session_run(Cleanup.Consumer, VS_INTEROP_RUN_SCRIPT, &Result) ==
			                VS_INTEROP_STATUS_OK &&
			            Result.success && Result.exit_code == 63,
			        "C ABI library imports retain frozen public macros and expand in the caller"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestPackages()
	{
		const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path SourcePath = std::filesystem::temp_directory_path() / ("veroscript-interop-" + std::to_string(Stamp) + ".vs");
		{
			std::ofstream Out(SourcePath, std::ios::binary | std::ios::trunc);
			Out
			    << "int main() { "
			    << "double widened = cast<double>(7); "
			    << "auto restored = try_cast<int>(widened); "
			    << "if (!restored.has_value()) return -1; "
			    << "return restored.get(); }\n";
		}
		struct FCleanup
		{
			~FCleanup()
			{
				std::error_code Error;
				std::filesystem::remove(Path, Error);
			}

			std::filesystem::path Path;

		} Cleanup{SourcePath};

		vs_interop_session* Builder = CreateSession();
		if (!Expect(Builder != nullptr, "package builder session"))
		{
			return false;
		}
		const std::string PathUtf8 = Utf8Path(SourcePath);
		const std::uint8_t MetadataBytes[] = {0, 1, 2, 255};
		const vs_interop_package_metadata Metadata[] = {
		    {View("empty.v1"), {nullptr, 0}},
		    {View("test.v1"), {MetadataBytes, sizeof(MetadataBytes)}}};
		vs_interop_package_build_options Options{};
		Options.struct_size = sizeof(Options);
		Options.kind = VS_INTEROP_PACKAGE_APPLICATION;
		Options.package_name = View("InteropApplication");
		Options.package_version = 42;
		Options.host_metadata = Metadata;
		Options.host_metadata_count = 2;
		vs_interop_buffer* PackageBytes = nullptr;
		vs_interop_run_result Result{sizeof(Result)};
		if (!Expect(vs_interop_session_build_package_file(Builder, View(PathUtf8), &Options,
		                &PackageBytes, &Result) == VS_INTEROP_STATUS_OK &&
		            PackageBytes,
		        "in-memory package build"))
		{
			return false;
		}
		const auto Bytes = CopyBuffer(PackageBytes);
		vs_interop_buffer_destroy(PackageBytes);
		if (!Expect(!Bytes.empty(), "package byte ownership"))
		{
			return false;
		}

		vs_interop_package* Package = nullptr;
		vs_interop_buffer* Error = nullptr;
		if (!Expect(vs_interop_package_read({Bytes.data(), Bytes.size()}, &Package, &Error) == VS_INTEROP_STATUS_OK && Package && !Error,
		        "package parse"))
		{
			return false;
		}
		if (!Expect(vs_interop_package_get_kind(Package) == VS_INTEROP_PACKAGE_APPLICATION && vs_interop_package_get_version(Package) == 42 && vs_interop_package_metadata_count(Package) == 2 && vs_interop_package_get_canonical_signed_bytes(Package).size != 0,
		        "package metadata and canonical signed range"))
		{
			return false;
		}
		vs_interop_package_metadata EmptyMetadata{};
		if (!Expect(vs_interop_package_get_metadata(Package, 0, &EmptyMetadata) == VS_INTEROP_STATUS_OK && EmptyMetadata.value.size == 0,
		        "empty package metadata"))
		{
			return false;
		}
		const std::uint8_t CurrentBytecodeMagic[] = {'V', 'S', 'B', 'C'};
		const auto Magic = std::search(Bytes.begin(), Bytes.end(),
		    CurrentBytecodeMagic, CurrentBytecodeMagic + sizeof(CurrentBytecodeMagic));
		if (!Expect(Magic != Bytes.end(), "package contains current bytecode magic"))
		{
			return false;
		}
		// All former VRS development identifiers remain incompatible at 0.1.1.
		for (const std::uint8_t Layout : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
		{
			std::vector<std::uint8_t> OldLayoutBytes = Bytes;
			const auto MagicOffset = static_cast<std::size_t>(Magic - Bytes.begin());
			OldLayoutBytes[MagicOffset] = 'V';
			OldLayoutBytes[MagicOffset + 1] = 'R';
			OldLayoutBytes[MagicOffset + 2] = 'S';
			OldLayoutBytes[MagicOffset + 3] = Layout;
			vs_interop_package* OldLayoutPackage = nullptr;
			vs_interop_buffer* OldLayoutError = nullptr;
			const vs_interop_status OldLayoutStatus = vs_interop_package_read(
			    {OldLayoutBytes.data(), OldLayoutBytes.size()},
			    &OldLayoutPackage, &OldLayoutError);
			const vs_interop_byte_view OldLayoutErrorText =
			    vs_interop_buffer_view(OldLayoutError);
			// Archive integrity may reject the changed bytes before the embedded
			// bytecode magic is read. Both layers must fail with a diagnostic.
			const bool bRejectedOldLayout = OldLayoutStatus == VS_INTEROP_STATUS_ENGINE_ERROR && OldLayoutPackage == nullptr && OldLayoutErrorText.data && OldLayoutErrorText.size != 0;
			vs_interop_buffer_destroy(OldLayoutError);
			if (!Expect(bRejectedOldLayout, "old bytecode layout rejected through C package API"))
			{
				return false;
			}
		}

		vs_interop_session* Runtime = CreateSession();
		if (!Expect(Runtime != nullptr && vs_interop_session_set_hot_reload_enabled(Runtime, 1) == VS_INTEROP_STATUS_OK && vs_interop_session_is_hot_reload_enabled(Runtime),
		        "hot reload configuration"))
		{
			return false;
		}
		if (!Expect(vs_interop_session_load_package(Runtime, Package, View("memory.vsapp"),
		                &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_run(Runtime, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.exit_code == 7,
		        "package load and run checked-cast bytecode"))
		{
			return false;
		}
		vs_interop_package_destroy(Package);

		vs_interop_package* ReloadPackage = nullptr;
		if (!Expect(vs_interop_package_read({Bytes.data(), Bytes.size()}, &ReloadPackage, &Error) == VS_INTEROP_STATUS_OK && vs_interop_session_reload_package(Runtime, ReloadPackage, View("memory.vsapp"), &Result) == VS_INTEROP_STATUS_OK,
		        "compiled package transaction reload"))
		{
			return false;
		}
		vs_interop_package_destroy(ReloadPackage);

		const std::vector<std::uint8_t> Truncated(Bytes.begin(), Bytes.begin() + Bytes.size() / 2);
		vs_interop_package* Invalid = nullptr;
		if (!Expect(vs_interop_package_read({Truncated.data(), Truncated.size()}, &Invalid, &Error) == VS_INTEROP_STATUS_ENGINE_ERROR && !Invalid && Error && vs_interop_buffer_view(Error).size != 0,
		        "truncated package rejection"))
		{
			return false;
		}
		vs_interop_buffer_destroy(Error);
		return Expect(vs_interop_session_destroy(Runtime) == VS_INTEROP_STATUS_OK && vs_interop_session_destroy(Builder) == VS_INTEROP_STATUS_OK,
		    "package session destruction");
	}
	bool TestHostProtocolUserDataRetention()
	{
		int Releases = 0;
		int UnmatchedReleases = 0;
		vs_interop_session* Session = CreateSession();
		if (!Expect(Session != nullptr, "protocol ownership session")) return false;
		vs_interop_host_type_desc Type{};
		Type.struct_size = sizeof(Type);
		Type.name = View("OwnedPoint");
		Type.kind = VS_INTEROP_HOST_STRUCT;
		std::uint32_t TypeId = 0;
		vs_interop_host_type_protocol Protocol{};
		Protocol.struct_size = sizeof(Protocol);
		Protocol.equals_callback = [](vs_interop_call_context*, vs_interop_value* Out, void*)
		    -> vs_interop_callback_result
		{
			return vs_interop_value_set_bool(Out, 1) == VS_INTEROP_STATUS_OK
			    ? VS_INTEROP_CALLBACK_OK
			    : VS_INTEROP_CALLBACK_ERROR;
		};
		Protocol.hash_callback = [](vs_interop_call_context*, vs_interop_value* Out, void*)
		    -> vs_interop_callback_result
		{
			return vs_interop_value_set_uint64(Out, 42) == VS_INTEROP_STATUS_OK
			    ? VS_INTEROP_CALLBACK_OK
			    : VS_INTEROP_CALLBACK_ERROR;
		};
		Protocol.equals_user_data = &Releases;
		Protocol.hash_user_data = &Releases;
		const auto Release = +[](void* Data)
		{
			++*static_cast<int*>(Data);
		};
		const bool Registered =
		    vs_interop_session_register_host_type(Session, &Type, &TypeId) == VS_INTEROP_STATUS_OK &&
		    vs_interop_session_register_host_protocol(Session, TypeId, &Protocol) == VS_INTEROP_STATUS_OK;
		const bool Rejected = Registered && vs_interop_session_retain_callback_user_data(nullptr, &Releases, Release) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_retain_callback_user_data(Session, &Releases, nullptr) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_retain_callback_user_data(Session, &UnmatchedReleases, Release) == VS_INTEROP_STATUS_INVALID_ARGUMENT && Releases == 0 && UnmatchedReleases == 0;
		const bool Retained = Rejected && vs_interop_session_retain_callback_user_data(Session, &Releases, Release) == VS_INTEROP_STATUS_OK && vs_interop_session_retain_callback_user_data(Session, &Releases, Release) == VS_INTEROP_STATUS_INVALID_ARGUMENT && Releases == 0;
		const bool Destroyed = vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK;
		return Expect(Registered && Rejected && Retained && Destroyed && Releases == 1 && UnmatchedReleases == 0,
		    "protocol user data transfers only on success and releases once across shared bridges");
	}
}

int main()
{
	const std::uint64_t RequiredCapabilities = VS_INTEROP_CAP_PACKAGES | VS_INTEROP_CAP_HOT_RELOAD | VS_INTEROP_CAP_HOST_BINDINGS | VS_INTEROP_CAP_BINDING_JSON | VS_INTEROP_CAP_REFLECTION_JSON | VS_INTEROP_CAP_INSTANCE_INTERCEPT | VS_INTEROP_CAP_MUTATION_CALLBACK | VS_INTEROP_CAP_CONTAINER_VALUES | VS_INTEROP_CAP_RETAINED_CALLABLES | VS_INTEROP_CAP_GENERIC_HOST_TYPES | VS_INTEROP_CAP_EXTERNAL_SERIALIZATION | VS_INTEROP_CAP_JIT_DIAGNOSTICS | VS_INTEROP_CAP_CONTAINER_ENUMERATION | VS_INTEROP_CAP_SESSION_JIT_CACHE;
	if (!Expect((vs_interop_get_capabilities() & RequiredCapabilities) == RequiredCapabilities, "capability discovery"))
	{
		return 1;
	}
	const std::uint64_t Capabilities = vs_interop_get_capabilities();
	if (!Expect((Capabilities & VS_INTEROP_CAP_NATIVE_TYPED_LOOPS) == 0 || (Capabilities & VS_INTEROP_CAP_NATIVE_JIT) != 0,
	        "native typed-loop capability implies native JIT"))
	{
		return 1;
	}
	if (!TestAnnotationNamePolicies() || !TestConsole() || !TestConsoleFailureRecovery() || !TestConsoleOptionsPrefix() || !TestProcessDirectoryCapability() || !TestProtocolCallbackLifetime(false) || !TestProtocolCallbackLifetime(true) || !TestExecutableValueOwnership() || !TestContainers() || !TestIndependentContainerPromotion() || !TestEnumerationSourceRetirement() || !TestSessionJitCache() || !TestJitExecutionStatistics() || !TestBindingsAndHooks() || !TestInterceptedArrayForwarding() || !TestProceedPublicationFailureCleanup() || !TestPendingInitializationResultCleanup() || !TestHotReloadResetConfiguration() || !TestBytecodeCacheConfigurationOwnership() || !TestPackages() || !TestMacroPackages() || !TestSessionTeardownCallbacks() || !TestHostProtocolUserDataRetention())
	{
		return 1;
	}
	std::cout << "VeroScriptInterop extended tests passed\n";
	return 0;
}

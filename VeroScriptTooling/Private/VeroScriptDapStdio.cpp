// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptDapStdio.h"
#include "Internal/VeroScriptToolingJson.h"
#include "VeroScriptAnalysisControl.h"

#include "VeroScriptDapAdapter.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptLocale.h"
#include "VeroScriptPath.h"
#include "Internal/VeroScriptMessageFraming.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace VeroScript::Tooling
{
	namespace
	{
		using Detail::FJson;
		using Detail::ParseJson;
		using Detail::ToJsonString;

		const char* StopReasonName(const EDapStopReason Reason)
		{
			switch (Reason)
			{
			case EDapStopReason::Step:
				return "step";
			case EDapStopReason::Entry:
				return "entry";
			case EDapStopReason::Pause:
				return "pause";
			case EDapStopReason::Breakpoint:
			default:
				return "breakpoint";
			}
		}

		std::string ResolveLaunchPath(
			const std::string_view BaseDirectory,
			const std::string_view Path)
		{
			if (Path.empty())
			{
				return {};
			}
			if (Path.rfind("veroscript-module:/", 0) == 0) return PathUtil::Normalize(std::string(Path));
			try
			{
				std::filesystem::path Native = PathUtil::FromUtf8(Path);
				if (Native.is_relative())
				{
					Native = BaseDirectory.empty()
						? std::filesystem::absolute(Native)
						: PathUtil::FromUtf8(BaseDirectory) / Native;
				}
				return PathUtil::ToUtf8(Native.lexically_normal());
			}
			catch (...)
			{
				return std::string(Path);
			}
		}

		class FDapStdioSession
		{
		public:
			FDapStdioSession(
				std::istream& In,
				std::ostream& Out,
				FDapStdioRunOptions Options,
				IVeroScriptLiveDebugDomain* LiveDomain)
				: In(In)
				, Out(Out)
				, Options(std::move(Options))
				, LiveDomain(LiveDomain)
			{
				Adapter.SetEventHandler(
					[this](const FDapEvent& Event)
					{
						OnAdapterEvent(Event);
					});
			}

			int Run(const std::atomic<bool>* StopRequested)
			{
				const auto Stopping = [StopRequested]()
				{
					return StopRequested && StopRequested->load();
				};
				int ExitCode = VeroScriptDapStdio::ExitOk;
				while (!bExit.load() && !Stopping())
				{
					std::optional<std::string> Body = ReadMessage();
					if (Stopping())
					{
						break;
					}
					if (!Body)
					{
						bCancelExecutionOnShutdown = true;
						ExitCode = bFramingError
							? VeroScriptDapStdio::ExitError
							: (bTerminateHost
								? VeroScriptDapStdio::ExitTerminateHost
								: VeroScriptDapStdio::ExitOk);
						break;
					}
					if (!DispatchMessage(*Body, /*bFromStopPump=*/false))
					{
						bCancelExecutionOnShutdown = true;
						ExitCode = VeroScriptDapStdio::ExitError;
						break;
					}
				}
				bCancelExecutionOnShutdown |= Stopping();
				StopExecutionThread(StopRequested);
				if (Adapter.IsDomainAttached())
				{
					Adapter.Disconnect();
				}
				if (ExitCode == VeroScriptDapStdio::ExitOk && bTerminateHost)
				{
					return VeroScriptDapStdio::ExitTerminateHost;
				}
				return ExitCode;
			}

		private:
			enum class EResumeKind : std::uint8_t
			{
				None,
				Continue,
				Next,
				StepIn,
				StepOut,
				Disconnect
			};

			std::uint32_t InternalLine(const int ClientLine) const
			{
				return static_cast<std::uint32_t>(std::max(
					bClientLinesStartAt1 ? 1 : 0, ClientLine))
					+
					(bClientLinesStartAt1 ? 0u : 1u);
			}

			std::uint32_t ClientLine(const std::uint32_t Internal) const
			{
				return bClientLinesStartAt1 ? Internal : (Internal > 0 ? Internal - 1 : 0);
			}

			std::uint32_t ClientColumn(const std::uint32_t Internal) const
			{
				return bClientColumnsStartAt1 ? Internal : (Internal > 0 ? Internal - 1 : 0);
			}

			void WriteFramed(const std::string& Body)
			{
				Out << "Content-Length: " << Body.size() << "\r\n\r\n"
					<< Body;
				Out.flush();
			}

			void WriteJsonMessage(FJson::FObject Message)
			{
				const std::lock_guard<std::mutex> Lock(OutputMutex);
				Message["seq"] = FJson::Number(static_cast<double>(NextSeq++));
				WriteFramed(ToJsonString(FJson::Object(std::move(Message))));
			}

			void WriteEvent(const std::string& EventName, FJson Body = FJson::Object())
			{
				FJson::FObject Message;
				Message["type"] = FJson::String("event");
				Message["event"] = FJson::String(EventName);
				Message["body"] = std::move(Body);
				WriteJsonMessage(std::move(Message));
			}

			void WriteResponse(
				const int RequestSeq,
				const std::string& Command,
				const bool bSuccess,
				FJson Body = FJson::Object(),
				const std::string& Message = {})
			{
				FJson::FObject Response;
				Response["type"] = FJson::String("response");
				Response["request_seq"] = FJson::Number(static_cast<double>(RequestSeq));
				Response["success"] = FJson::Bool(bSuccess);
				Response["command"] = FJson::String(Command);
				if (!bSuccess && !Message.empty())
				{
					Response["message"] = FJson::String(Message);
				}
				Response["body"] = std::move(Body);
				WriteJsonMessage(std::move(Response));
			}

			std::optional<std::string> ReadMessage()
			{
				Internal::FMessageReadResult Result =
					Internal::ReadContentLengthMessage(In);
				bFramingError =
					Result.Status == Internal::EMessageReadStatus::InvalidFrame;
				if (Result.Status != Internal::EMessageReadStatus::Message)
				{
					return std::nullopt;
				}
				return std::move(Result.Body);
			}

			void OnAdapterEvent(const FDapEvent& Event)
			{
				switch (Event.Kind)
				{
				case EDapEventKind::Initialized:
					{
						// Wire hosts emit `initialized` after the initialize response.
						break;
					}
				case EDapEventKind::Stopped:
					{
						// Read preparation first: once it clears, the committed domain
						// is already visible. Host callbacks must never enter the Run pump.
						const bool bLiveDomain = bDomainAttachInProgress.load()
							|| Adapter.IsDomainAttached();
						if (!bLiveDomain)
						{
							const std::lock_guard<std::mutex> Lock(StopMutex);
							bResumeRequested = false;
							bStopPumpActive = true;
						}
						FJson::FObject Body;
						Body["reason"] = FJson::String(StopReasonName(Event.StopReason));
						Body["threadId"] = FJson::Number(static_cast<double>(Event.ThreadId));
						Body["allThreadsStopped"] = FJson::Bool(true);
						WriteEvent("stopped", FJson::Object(std::move(Body)));
						// A host domain owns its paused execution. Blocking its event
						// callback would prevent it from processing a resume or detach.
						if (bLiveDomain)
						{
							break;
						}
						std::unique_lock<std::mutex> Lock(StopMutex);
						while (!bResumeRequested && !bExit.load())
						{
							StopCondition.wait(Lock, [this]()
							{
								return PendingStopRequest.has_value() || bResumeRequested || bExit.load();
							});
							if (PendingStopRequest)
							{
								std::string Request = std::move(*PendingStopRequest);
								PendingStopRequest.reset();
								Lock.unlock();
								const bool bResult = DispatchMessage(Request, /*bFromStopPump=*/true);
								Lock.lock();
								bStopRequestResult = bResult;
								bStopRequestCompleted = true;
								StopCondition.notify_all();
							}
						}
						bStopPumpActive = false;
						Lock.unlock();
						break;
					}
				case EDapEventKind::Output:
					{
						FJson::FObject Body;
						Body["category"] = FJson::String(
							Event.Category.empty() ? "stdout" : Event.Category);
						Body["output"] = FJson::String(Event.Output);
						WriteEvent("output", FJson::Object(std::move(Body)));
						break;
					}
				case EDapEventKind::Exited:
					{
						FJson::FObject Body;
						Body["exitCode"] = FJson::Number(static_cast<double>(Event.ExitCode));
						WriteEvent("exited", FJson::Object(std::move(Body)));
						break;
					}
				case EDapEventKind::Terminated:
					{
						WriteEvent("terminated");
						break;
					}
				case EDapEventKind::Breakpoint:
					{
						FJson::FObject Breakpoint;
						Breakpoint["id"] = FJson::Number(
							static_cast<double>(Event.Breakpoint.Id));
						Breakpoint["verified"] = FJson::Bool(Event.Breakpoint.bVerified);
						Breakpoint["line"] = FJson::Number(
							static_cast<double>(ClientLine(Event.Breakpoint.Line)));
						Breakpoint["source"] = FJson::Object({{"path", FJson::String(Event.Breakpoint.Path)}});
						if (!Event.Breakpoint.Message.empty())
						{
							Breakpoint["message"] = FJson::String(Event.Breakpoint.Message);
						}
						WriteEvent("breakpoint", FJson::Object({{"reason", FJson::String("changed")}, {"breakpoint", FJson::Object(std::move(Breakpoint))}}));
						break;
					}
				default:
					{
						break;
					}
				}
			}

			FDapContinueResult RequestResume(const EResumeKind Resume)
			{
				FDapContinueResult Result;
				switch (Resume)
				{
				case EResumeKind::Next:
					{
						Result = Adapter.Next();
						break;
					}
				case EResumeKind::StepIn:
					{
						Result = Adapter.StepIn();
						break;
					}
				case EResumeKind::StepOut:
					{
						Result = Adapter.StepOut();
						break;
					}
				case EResumeKind::Disconnect:
					{
						Adapter.Disconnect();
						break;
					}
				case EResumeKind::Continue:
				case EResumeKind::None:
				default:
					{
						Result = Adapter.Continue();
						break;
					}
				}
				if (Result.bSuccess || Resume == EResumeKind::Disconnect)
				{
					{
						const std::lock_guard<std::mutex> Lock(StopMutex);
						bResumeRequested = true;
					}
					StopCondition.notify_all();
				}
				return Result;
			}

			void StartExecutionThread()
			{
				if (ExecutionThread.joinable())
				{
					ExecutionThread.join();
				}
				bExecutionActive.store(true);
				ExecutionThread = std::thread(
					[this]()
					{
						(void)Adapter.ConfigurationDone();
						{
							const std::lock_guard<std::mutex> Lock(StopMutex);
							bExecutionActive.store(false);
						}
						StopCondition.notify_all();
					});
				// Do not let an immediately queued disconnect/terminate race the adapter's
				// transition into Run(). This is only a thread-start barrier; execution
				// itself remains asynchronous and all requests stay serviceable.
				while (bExecutionActive.load() && !Adapter.IsRunning())
				{
					std::this_thread::yield();
				}
			}

			void StopExecutionThread(const std::atomic<bool>* StopRequested)
			{
				// Request cancellation before releasing a paused execution thread.
				// Once the stop pump wakes, only that thread may change debugger state.
				if (ExecutionThread.joinable() && bExecutionActive.load() && bCancelExecutionOnShutdown)
				{
					Adapter.Terminate();
				}
				bExit.store(true);
				StopCondition.notify_all();
				if (ExecutionThread.joinable())
				{
					if (StopRequested)
					{
						// A non-terminating disconnect may already be waiting for its
						// owned program to finish when the host requests endpoint stop.
						// The socket wake cannot interrupt join(), so retain the host
						// signal during this wait. Completion wakes immediately; the
						// bounded condition wait covers the atomic stop signal, which
						// does not notify this session's condition variable.
						std::unique_lock<std::mutex> Lock(StopMutex);
						while (bExecutionActive.load() && !StopRequested->load())
						{
							StopCondition.wait_for(Lock, std::chrono::milliseconds(250));
						}
						const bool bCancel = bExecutionActive.load() && StopRequested->load();
						Lock.unlock();
						if (bCancel)
						{
							Adapter.Terminate();
						}
					}
					ExecutionThread.join();
				}
			}

			bool DispatchMessage(const std::string& Body, const bool bFromStopPump)
			{
				FJson Root;
				try
				{
					Root = ParseJson(Body);
				}
				catch (const std::exception& Exception)
				{
					WriteEvent("output", FJson::Object({
											 {"category", FJson::String("stderr")},
											 {"output", FJson::String(
							Locale::TranslateId("dap.invalid_json", {Exception.what()}) + "\n")},
										 }));
					return false;
				}

				const std::string Type = Root.GetString("type");
				if (Type != "request")
				{
					return true;
				}

				const int RequestSeq = Root.GetInt("seq");
				const std::string Command = Root.GetString("command");
				const FJson* Arguments = Root.Find("arguments");
				const FJson EmptyArgs = FJson::Object();
				const FJson& Args = Arguments ? *Arguments : EmptyArgs;
				if ((Command == "stackTrace" || Command == "continue" || Command == "next"
					|| Command == "stepIn" || Command == "stepOut" || Command == "pause")
					&& Args.GetInt("threadId", Args.Find("threadId") ? -1 : VeroScriptDapAdapter::MainThreadId)
						!= VeroScriptDapAdapter::MainThreadId)
				{
					WriteResponse(RequestSeq, Command, false, FJson::Object(), "Unknown thread.");
					return true;
				}

				// The reader stays on the protocol thread. At a program stop the
				// execution thread services requests, so inspecting memory regions
				// and entering host getters obey their execution-thread contract.
				// Live domains already own and dispatch their paused operations.
				if (!bFromStopPump && !Adapter.IsDomainAttached())
				{
					std::unique_lock<std::mutex> Lock(StopMutex);
					if (bStopPumpActive)
					{
						PendingStopRequest = Body;
						bStopRequestCompleted = false;
						StopCondition.notify_all();
						StopCondition.wait(Lock,
										   [this]()
										   {
											   return bStopRequestCompleted;
										   });
						return bStopRequestResult;
					}
				}

				if (Command == "initialize")
				{
					bClientLinesStartAt1 = Args.GetBool("linesStartAt1", true);
					bClientColumnsStartAt1 = Args.GetBool("columnsStartAt1", true);
					const std::string RequestedLocale = Args.GetString("locale");
					if (!RequestedLocale.empty())
					{
						// DAP initialize.locale is a BCP 47 preference. Keep the
						// environment-selected language when that catalog is unavailable.
						(void)Locale::TrySetLanguageFromTag(RequestedLocale);
					}
					const FDapInitializeResult Result = Adapter.Initialize();
					FJson::FObject Caps;
					Caps["supportsConfigurationDoneRequest"] =
						FJson::Bool(Result.Capabilities.bSupportsConfigurationDoneRequest);
					Caps["supportsStepInRequest"] =
						FJson::Bool(Result.Capabilities.bSupportsStepInRequest);
					Caps["supportsStepOutRequest"] =
						FJson::Bool(Result.Capabilities.bSupportsStepOutRequest);
					Caps["supportsStepBackRequest"] =
						FJson::Bool(Result.Capabilities.bSupportsStepBackRequest);
					Caps["supportsConditionalBreakpoints"] =
						FJson::Bool(Result.Capabilities.bSupportsConditionalBreakpoints);
					Caps["supportsEvaluateRequest"] =
						FJson::Bool(Result.Capabilities.bSupportsEvaluateRequest);
					Caps["supportsSetVariable"] =
						FJson::Bool(Result.Capabilities.bSupportsSetVariable);
					Caps["supportsTerminateRequest"] =
						FJson::Bool(Result.Capabilities.bSupportsTerminateRequest);
					// Clients must confirm this before sending attachMode="domain";
					// an older host may otherwise interpret attach as a program load.
					Caps["veroscriptSupportsDomainAttach"] = FJson::Bool(LiveDomain != nullptr);
					// DAP body is capabilities; attach host metadata for multi-platform
					// diagnostics (mirrors CLI --version platform/jit tags).
					Caps["name"] = FJson::String("veroscript-dap");
					Caps["version"] = FJson::String(Result.Version);
					Caps["platform"] = FJson::String(Result.Platform);
					Caps["jitCapability"] = FJson::String(Result.JitCapability);
					WriteResponse(RequestSeq, Command, Result.bSuccess, FJson::Object(std::move(Caps)));
					WriteEvent("initialized");
					return true;
				}

				if (Command == "launch" || Command == "attach")
				{
					const bool bAttach = Command == "attach";
					if (bExecutionActive.load())
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Locale::TranslateId("dap.already_running"));
						return true;
					}
					if (bAttach && !Adapter.IsLoaded())
					{
						bAttachedSession = true;
						GeneratedSources.clear();
						GeneratedSourceBytes = 0;
					}
					const std::string AttachMode = Args.GetString("attachMode");
					const FJson* AttachModeField = Args.Find("attachMode");
					if (AttachModeField && (!bAttach
						|| !std::holds_alternative<std::string>(AttachModeField->Value)
						|| (AttachMode != "domain" && AttachMode != "program")))
					{
						WriteResponse(RequestSeq, Command, false, FJson::Object(),
							"unsupported attach mode");
						return true;
					}
					const FJson* DomainIdField = Args.Find("domainId");
					if (DomainIdField && (!bAttach || AttachMode != "domain"
						|| !std::holds_alternative<std::string>(DomainIdField->Value)))
					{
						WriteResponse(RequestSeq, Command, false, FJson::Object(),
							"domainId must be a string used with domain attach");
						return true;
					}
					if (bAttach && AttachMode == "domain")
					{
						if (!LiveDomain || !Args.GetString("program").empty()
							|| !Args.GetString("source").empty())
						{
							WriteResponse(RequestSeq, Command, false, FJson::Object(),
								"live attach requires a host domain and cannot load a program");
							return true;
						}
						struct FAttachScope
						{
							~FAttachScope()
							{
								InProgress.store(false);
							}

							std::atomic<bool>& InProgress;
						} AttachScope{bDomainAttachInProgress};
						bDomainAttachInProgress.store(true);
						const FDapLaunchResult Result = Adapter.AttachDomain(
							*LiveDomain, Args.GetString("domainId"));
						if (!Result.bSuccess)
						{
							WriteResponse(RequestSeq, Command, false, FJson::Object(),
								Result.Diagnostics.empty() ? "live domain attach failed"
									: Result.Diagnostics.front().Message);
							return true;
						}
						bAttachedSession = true;
						const FLiveDebugDomainInfo Info = Adapter.AttachedDomainInfo();
						FJson::FArray Sources;
						for (const FLiveDebugSourceVersion& Source : Info.Sources)
						{
							Sources.push_back(FJson::Object({
								{"path", FJson::String(Source.Path)},
								{"version", FJson::String(Source.Version)}}));
						}
						WriteResponse(RequestSeq, Command, true, FJson::Object({
							{"veroscriptHostId", FJson::String(Info.HostId)},
							{"veroscriptDomainId", FJson::String(Info.DomainId)},
							{"veroscriptTargetName", FJson::String(Info.Name)},
							{"veroscriptSourceRevision", FJson::String(Info.SourceRevision)},
							{"veroscriptBindingRevision", FJson::String(Info.BindingRevision)},
							{"veroscriptSources", FJson::Array(std::move(Sources))},
							{"veroscriptCanPause", FJson::Bool(Info.bCanPause)},
							{"veroscriptCanStep", FJson::Bool(Info.bCanStep)},
							{"veroscriptCanEvaluate", FJson::Bool(Info.bCanEvaluate)},
							{"veroscriptCanUseConditionalBreakpoints",
								FJson::Bool(Info.bCanUseConditionalBreakpoints)},
							{"veroscriptCanSetVariable", FJson::Bool(Info.bCanSetVariable)},
							{"veroscriptCanTerminate", FJson::Bool(Info.bCanTerminate)}}));
						return true;
					}
					FDapLaunchArgs LaunchArgs;
					LaunchArgs.AnnotationProcessing = Options.AnnotationProcessing;
					LaunchArgs.CompileProcessing = Options.CompileProcessing;
					LaunchArgs.ScriptCompileExtensions = Options.ScriptCompileExtensions;
					LaunchArgs.VmBackendFactory = Options.VmBackendFactory;
					LaunchArgs.VmBackendPath = Options.VmBackendPath;
					LaunchArgs.ModuleEnvironment = Options.ModuleEnvironment;
					LaunchArgs.Path = Args.GetString("program");
					if (LaunchArgs.Path.empty())
					{
						LaunchArgs.Path = Options.DefaultProgram;
					}
					LaunchArgs.Source = Args.GetString("source");
					const bool bHasExplicitFileLabel = !Args.GetString("fileLabel").empty();
					LaunchArgs.FileLabel = Args.GetString("fileLabel", LaunchArgs.Path);
					if (LaunchArgs.FileLabel.empty())
					{
						LaunchArgs.FileLabel = "<source>";
					}
					LaunchArgs.bStopOnEntry = Args.GetBool("stopOnEntry", false);
					LaunchArgs.ExecutionBackend = Options.DefaultExecutionBackend;
					LaunchArgs.MaxCallDepth = Options.DefaultMaxCallDepth;
					LaunchArgs.ScriptArgs = Options.DefaultScriptArgs;
					if (const FJson* ScriptArgs = Args.Find("args"))
					{
						if (const FJson::FArray* Array = ScriptArgs->ArrayPtr())
						{
							LaunchArgs.ScriptArgs.clear();
							for (const FJson& Entry : *Array)
							{
								if (const std::string* Value = std::get_if<std::string>(&Entry.Value))
								{
									LaunchArgs.ScriptArgs.push_back(*Value);
								}
							}
						}
					}
					LaunchArgs.StdlibSearchPaths = Options.DefaultStdlibSearchPaths;
					if (const FJson* StdlibList = Args.Find("stdlibSearchPaths"))
					{
						if (const FJson::FArray* Array = StdlibList->ArrayPtr())
						{
							LaunchArgs.StdlibSearchPaths.clear();
							for (const FJson& Entry : *Array)
							{
								if (const std::string* Path = std::get_if<std::string>(&Entry.Value);
									Path && !Path->empty())
								{
									LaunchArgs.StdlibSearchPaths.push_back(*Path);
								}
							}
						}
					}
					else if (const std::string StdlibPath = Args.GetString("stdlibPath");
						!StdlibPath.empty())
					{
						LaunchArgs.StdlibSearchPaths = {StdlibPath};
					}
					LaunchArgs.Environment = Options.DefaultEnvironment;
					const FJson* Environment = Args.Find("env");
					if (!Environment)
					{
						Environment = Args.Find("environment");
					}
					if (Environment)
					{
						LaunchArgs.Environment.clear();
						if (const FJson::FObject* Object = Environment->ObjectPtr())
						{
							for (const auto& [Name, Entry] : *Object)
							{
								if (const std::string* Value = std::get_if<std::string>(&Entry.Value))
								{
									LaunchArgs.Environment.emplace_back(Name, *Value);
								}
							}
						}
						else if (const FJson::FArray* Array = Environment->ArrayPtr())
						{
							for (const FJson& Entry : *Array)
							{
								const std::string Name = Entry.GetString("name");
								if (!Name.empty())
								{
									LaunchArgs.Environment.emplace_back(Name, Entry.GetString("value"));
								}
							}
						}
					}
					LaunchArgs.WorkingDirectory = Args.GetString(
						"cwd",
						Args.GetString("workingDirectory", Options.DefaultWorkingDirectory));
					LaunchArgs.Input = Args.GetString("input", Options.DefaultInput);
					const std::string WorkspaceDirectory = ResolveLaunchPath(
						{},
						Args.GetString("workspace"));
					if (LaunchArgs.WorkingDirectory.empty() && !WorkspaceDirectory.empty())
					{
						LaunchArgs.WorkingDirectory = WorkspaceDirectory;
					}
					else if (!LaunchArgs.WorkingDirectory.empty())
					{
						LaunchArgs.WorkingDirectory = ResolveLaunchPath(
							WorkspaceDirectory,
							LaunchArgs.WorkingDirectory);
					}
					const std::string LoadingBase = !LaunchArgs.WorkingDirectory.empty()
						? LaunchArgs.WorkingDirectory
						: WorkspaceDirectory;
					LaunchArgs.Path = ResolveLaunchPath(LoadingBase, LaunchArgs.Path);
					if (!bHasExplicitFileLabel && !LaunchArgs.Path.empty())
					{
						LaunchArgs.FileLabel = LaunchArgs.Path;
					}
					{
						const FJson* const MaxCallDepthField =
							Args.Find("maxCallDepth");
						const double* const RawMaxCallDepth = MaxCallDepthField
							? std::get_if<double>(&MaxCallDepthField->Value)
							: nullptr;
						if (MaxCallDepthField
							&& (!RawMaxCallDepth
								|| !std::isfinite(*RawMaxCallDepth)
								|| *RawMaxCallDepth < 0.0
								|| std::trunc(*RawMaxCallDepth) != *RawMaxCallDepth
								|| *RawMaxCallDepth
									>= static_cast<double>(
										std::numeric_limits<std::size_t>::max())))
						{
							WriteResponse(
								RequestSeq,
								Command,
								false,
								FJson::Object(),
								Locale::TranslateId(
									"dap.invalid_max_call_depth",
									{bAttach ? "attach" : "launch"}));
							return true;
						}
						if (RawMaxCallDepth)
						{
							LaunchArgs.MaxCallDepth =
								static_cast<std::size_t>(*RawMaxCallDepth);
						}
					}
					{
						const std::string BackendText = Args.GetString("backend");
						if (!BackendText.empty())
						{
							EExecutionBackend ParsedBackend = EExecutionBackend::Vm;
							if (!TryParseExecutionBackend(BackendText, ParsedBackend))
							{
								WriteResponse(
									RequestSeq,
									Command,
									false,
									FJson::Object(),
									Locale::TranslateId(
										"dap.invalid_backend",
										{bAttach ? "attach" : "launch"}));
								return true;
							}
							LaunchArgs.ExecutionBackend = ParsedBackend;
						}
					}
					if (const FJson* Imports = Args.Find("importPaths"))
					{
						if (const FJson::FArray* Array = Imports->ArrayPtr())
						{
							for (const FJson& Entry : *Array)
							{
								if (const std::string* Path = std::get_if<std::string>(&Entry.Value))
								{
									LaunchArgs.ImportPaths.push_back(*Path);
								}
							}
						}
					}
					if (LaunchArgs.ImportPaths.empty())
					{
						LaunchArgs.ImportPaths = Options.DefaultImportPaths;
					}
					for (std::string& Path : LaunchArgs.ImportPaths)
					{
						Path = ResolveLaunchPath(LoadingBase, Path);
					}
					if (const FJson* Imports = Args.Find("implicitImportPaths"))
					{
						if (const FJson::FArray* Array = Imports->ArrayPtr())
						{
							for (const FJson& Entry : *Array)
							{
								if (const std::string* Path =
									std::get_if<std::string>(&Entry.Value))
								{
									LaunchArgs.ImplicitImportPaths.push_back(*Path);
								}
							}
						}
					}
					if (LaunchArgs.ImplicitImportPaths.empty())
					{
						LaunchArgs.ImplicitImportPaths =
							Options.DefaultImplicitImportPaths;
					}
					for (std::string& Path : LaunchArgs.ImplicitImportPaths)
					{
						Path = ResolveLaunchPath(LoadingBase, Path);
					}
					for (std::string& Path : LaunchArgs.StdlibSearchPaths)
					{
						Path = ResolveLaunchPath(LoadingBase, Path);
					}
					LaunchArgs.bAllowCircularImports = Args.GetBool(
						"allowCircularImports",
						Options.bDefaultAllowCircularImports);
					LaunchArgs.bAllowProjectWideAccess = Args.GetBool(
						"allowProjectWideAccess",
						Options.bDefaultAllowProjectWideAccess);
					if (const FJson* DefineList = Args.Find("defines"))
					{
						if (const FJson::FArray* Array = DefineList->ArrayPtr())
						{
							for (const FJson& Entry : *Array)
							{
								if (const std::string* Name = std::get_if<std::string>(&Entry.Value))
								{
									LaunchArgs.Defines.push_back(*Name);
								}
							}
						}
					}
					if (LaunchArgs.Defines.empty())
					{
						LaunchArgs.Defines = Options.DefaultDefines;
					}
					if (const FJson* MacroList = Args.Find("predefinedMacros"))
					{
						if (const FJson::FArray* Array = MacroList->ArrayPtr())
						{
							for (const FJson& Entry : *Array)
							{
								if (!Entry.ObjectPtr())
								{
									continue;
								}
								VeroScript::FPredefinedMacro Macro;
								Macro.Name = Entry.GetString("name");
								Macro.Body = Entry.GetString("body");
								Macro.SourceName = Entry.GetString("sourceName", "<host>");
								Macro.bVariadic = Entry.GetBool("variadic");
								if (const FJson* Parameters = Entry.Find("parameters"))
								{
									if (const FJson::FArray* ParamArray = Parameters->ArrayPtr())
									{
										for (const FJson& Parameter : *ParamArray)
										{
											if (const std::string* Name =
													std::get_if<std::string>(&Parameter.Value))
											{
												Macro.Parameters.push_back(*Name);
											}
										}
									}
								}
								LaunchArgs.PredefinedMacros.push_back(std::move(Macro));
							}
						}
					}
					if (LaunchArgs.PredefinedMacros.empty())
					{
						LaunchArgs.PredefinedMacros = Options.DefaultPredefinedMacros;
					}
					if (const FJson* OverlayList = Args.Find("sourceOverlays"))
					{
						if (const FJson::FArray* Array = OverlayList->ArrayPtr())
						{
							for (const FJson& Entry : *Array)
							{
								FDocumentOverlay Overlay;
								Overlay.Path = Entry.GetString("path");
								Overlay.Source = Entry.GetString("source");
								if (!Overlay.Path.empty())
								{
									Overlay.Path = ResolveLaunchPath(LoadingBase, Overlay.Path);
									LaunchArgs.SourceOverlays.push_back(std::move(Overlay));
								}
							}
						}
					}
					VeroScript::FHostBindingRegistry PendingBindingCatalog;
					const auto* EffectiveLiveBindings = Options.LiveBindings ? Options.LiveBindings
						: Options.ModuleEnvironment ? Options.ModuleEnvironment->GetDescription().Bindings.get() : nullptr;
					{
						// Prefer in-process live registry from the embed host.
						if (EffectiveLiveBindings)
						{
							LaunchArgs.Bindings = EffectiveLiveBindings;
							LaunchArgs.bLiveBindings = true;
						}
						else
						{
							std::vector<std::string> CatalogPaths;
							if (const FJson* CatalogList = Args.Find("bindingCatalogPaths"))
							{
								if (const FJson::FArray* Array = CatalogList->ArrayPtr())
								{
									for (const FJson& Entry : *Array)
									{
										if (const std::string* Path =
												std::get_if<std::string>(&Entry.Value);
											Path != nullptr && !Path->empty())
										{
											CatalogPaths.push_back(*Path);
										}
									}
								}
							}
							if (CatalogPaths.empty())
							{
								CatalogPaths = Options.DefaultBindingCatalogPaths;
							}
							std::vector<std::string> UniquePaths;
							std::unordered_set<std::string> Identities;
							for (const std::string& Path : CatalogPaths)
							{
								if (Path.empty())
								{
									continue;
								}

								std::string Resolved = ResolveLaunchPath(LoadingBase, Path);
								if (Identities.insert(PathUtil::IdentityKey(Resolved)).second)
								{
									UniquePaths.push_back(std::move(Resolved));
								}
							}
							CatalogPaths = std::move(UniquePaths);
							for (std::size_t Index = 0; Index < CatalogPaths.size(); ++Index)
							{
								std::string Error;
								if (!VeroScript::LoadBindingCatalogFile(
										CatalogPaths[Index],
										PendingBindingCatalog,
										Error,
										Index == 0))
								{
									WriteResponse(
										RequestSeq,
										Command,
										false,
										FJson::Object(),
										Locale::TranslateId(
											"dap.binding_catalog_failed",
											{CatalogPaths[Index], Error}));
									return true;
								}
							}
							const bool bHasCatalogContent =
								!PendingBindingCatalog.GetFunctions().empty()
								|| !PendingBindingCatalog.GetTypes().empty()
								|| !PendingBindingCatalog.GetTypeAliases().empty()
								|| !PendingBindingCatalog.GetGenericTypeTemplates().empty()
								|| !PendingBindingCatalog.GetAnnotations().empty();
							LaunchArgs.Bindings = bHasCatalogContent ? &BindingCatalog : nullptr;
							LaunchArgs.bLiveBindings = false;
						}
					}
					constexpr std::size_t MaxLaunchArguments = 4096;
					constexpr std::size_t MaxLaunchPaths = 2048;
					constexpr std::size_t MaxEnvironmentEntries = 1024;
					if (LaunchArgs.ScriptArgs.size() > MaxLaunchArguments
						|| LaunchArgs.ImportPaths.size() > MaxLaunchPaths
						|| LaunchArgs.ImplicitImportPaths.size() > MaxLaunchPaths
						|| LaunchArgs.StdlibSearchPaths.size() > MaxLaunchPaths
						|| LaunchArgs.Environment.size() > MaxEnvironmentEntries)
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Locale::TranslateId("dap.launch_resource_limits"));
						return true;
					}
					for (const auto& [Name, Value] : LaunchArgs.Environment)
					{
						if (Name.empty() || Name.find('=') != std::string::npos
							|| Name.find('\0') != std::string::npos
							|| Value.find('\0') != std::string::npos)
						{
							WriteResponse(
								RequestSeq,
								Command,
								false,
								FJson::Object(),
								Locale::TranslateId("dap.invalid_environment_variable"));
							return true;
						}
					}
					if (!LaunchArgs.WorkingDirectory.empty())
					{
						std::error_code DirectoryError;
						bool bDirectory = false;
						try
						{
							bDirectory = std::filesystem::is_directory(
								PathUtil::FromUtf8(LaunchArgs.WorkingDirectory),
								DirectoryError);
						}
						catch (...)
						{
							bDirectory = false;
						}
						if (!bDirectory)
						{
							WriteResponse(
								RequestSeq,
								Command,
								false,
								FJson::Object(),
								Locale::TranslateId("dap.working_directory_missing"));
							return true;
						}
					}
					// A rejected request must leave the loaded program's native indices
					// and binding metadata intact. Commit only after validation, directly
					// before the adapter clears and replaces the previous program.
					if (!EffectiveLiveBindings)
					{
						BindingCatalog.Swap(PendingBindingCatalog);
					}

					GeneratedSources.clear();
					GeneratedSourceBytes = 0;
					FDapLaunchResult Result;
					try
					{
						Result = bAttach ? Adapter.Attach(LaunchArgs) : Adapter.Launch(LaunchArgs);
					}
					catch (const FAnalysisCancelled&)
					{
						WriteResponse(RequestSeq, Command, false, FJson::Object(), "cancelled");
						return true;
					}
					catch (const std::exception& Error)
					{
						WriteResponse(RequestSeq, Command, false, FJson::Object(), Error.what());
						return true;
					}
					if (!Result.bSuccess)
					{
						std::string Message = Locale::TranslateId(
							bAttach ? "dap.attach_failed" : "dap.launch_failed");
						if (!Result.Diagnostics.empty())
						{
							const auto Error =
								std::find_if(Result.Diagnostics.begin(), Result.Diagnostics.end(),
									[](const auto& Diagnostic)
									{
										return Diagnostic.Severity == EDiagnosticSeverity::Error;
									});
							Message =
								(Error == Result.Diagnostics.end() ? Result.Diagnostics.front() : *Error)
									.Message;
						}
						WriteResponse(RequestSeq, Command, false, FJson::Object(), Message);
						return true;
					}
					bAttachedSession = bAttach;
					if (LaunchArgs.Bindings)
					{
						if (LaunchArgs.bLiveBindings)
						{
							WriteEvent(
								"output",
								FJson::Object({
									{"category", FJson::String("console")},
									{"output",
										FJson::String(
											Locale::TranslateId(
												bAttach
													? "dap.live_bindings_attached"
													: "dap.live_bindings_linked")
											+
											"\n")},
								}));
						}
						else
						{
							WriteEvent(
								"output",
								FJson::Object({
									{"category", FJson::String("console")},
									{"output",
										FJson::String(
											Locale::TranslateId("dap.binding_catalog_stub_warning")
											+ "\n")},
								}));
						}
					}
					WriteResponse(RequestSeq, Command, true);
					return true;
				}

				if (Command == "setBreakpoints")
				{
					if (bExecutionActive.load() && !Adapter.IsStopped())
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Locale::TranslateId("dap.breakpoints_change_requires_stop"));
						return true;
					}
					std::string Path;
					if (const FJson* Source = Args.Find("source"))
					{
						Path = Source->GetString("path");
					}
					std::vector<FDapSourceBreakpoint> Breakpoints;
					if (const FJson* BreakpointList = Args.Find("breakpoints"))
					{
						if (const FJson::FArray* Array = BreakpointList->ArrayPtr())
						{
							for (const FJson& Entry : *Array)
							{
								FDapSourceBreakpoint Item;
								Item.Line = InternalLine(Entry.GetInt(
									"line", bClientLinesStartAt1 ? 1 : 0));
								Item.Condition = Entry.GetString("condition");
								Breakpoints.push_back(std::move(Item));
							}
						}
					}
					if (Adapter.IsDomainAttached()
						&& !Adapter.AttachedDomainInfo().bCanUseConditionalBreakpoints
						&& std::any_of(Breakpoints.begin(), Breakpoints.end(),
							[](const FDapSourceBreakpoint &Entry)
							{
								return !Entry.Condition.empty();
							}))
					{
						WriteResponse(RequestSeq, Command, false, FJson::Object(),
							"host debug domain does not support conditional breakpoints");
						return true;
					}
					const FDapSetBreakpointsResult Result =
						Adapter.SetBreakpoints(Path, Breakpoints);
					FJson::FArray OutBreakpoints;
					for (const FDapBreakpoint& Breakpoint : Result.Breakpoints)
					{
						FJson::FObject Item;
						Item["id"] = FJson::Number(static_cast<double>(Breakpoint.Id));
						Item["verified"] = FJson::Bool(Breakpoint.bVerified);
						Item["line"] = FJson::Number(
							static_cast<double>(ClientLine(Breakpoint.Line)));
						if (!Breakpoint.Message.empty())
						{
							Item["message"] = FJson::String(Breakpoint.Message);
						}
						OutBreakpoints.push_back(FJson::Object(std::move(Item)));
					}
					WriteResponse(
						RequestSeq,
						Command,
						Result.bSuccess,
						FJson::Object({{"breakpoints", FJson::Array(std::move(OutBreakpoints))}}));
					return true;
				}

				if (Command == "configurationDone")
				{
					if (Adapter.IsDomainAttached())
					{
						const FDebugRunResult Result = Adapter.ConfigurationDone();
						WriteResponse(RequestSeq, Command, Result.bSuccess, FJson::Object(),
							Result.Diagnostics.empty() ? std::string{}
								: Result.Diagnostics.front().Message);
						return true;
					}
					if (!Adapter.IsLoaded())
					{
						WriteResponse(RequestSeq, Command, false, FJson::Object(),
							Locale::TranslateId(
								"diag.configuration_done_requires_a_successful_launch"));
						return true;
					}
					if (bFromStopPump)
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Locale::TranslateId("dap.already_running"));
						return true;
					}
					if (bExecutionActive.load())
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Locale::TranslateId("dap.already_running"));
						return true;
					}
					WriteResponse(RequestSeq, Command, true);
					StartExecutionThread();
					return true;
				}

				if (Command == "threads")
				{
					FJson::FArray Threads;
					if (Adapter.IsLoaded() && (!Adapter.IsDomainAttached()
						|| Adapter.IsRunning() || Adapter.IsStopped()))
					{
						Threads.push_back(FJson::Object({
							{"id", FJson::Number(static_cast<double>(VeroScriptDapAdapter::MainThreadId))},
							{"name", FJson::String("main")},
						}));
					}
					WriteResponse(
						RequestSeq,
						Command,
						true,
						FJson::Object({{"threads", FJson::Array(std::move(Threads))}}));
					return true;
				}

				if (Command == "stackTrace")
				{
					const auto ReadCount = [&Args](const std::string& Key) -> std::optional<std::uint32_t>
					{
						const FJson* Field = Args.Find(Key);
						if (!Field) return 0;
						const double* Number = std::get_if<double>(&Field->Value);
						if (!Number || !std::isfinite(*Number) || std::trunc(*Number) != *Number ||
							*Number < 0 ||
							*Number > static_cast<double>((std::numeric_limits<std::uint32_t>::max)()))
						{
							return std::nullopt;
						}

						return static_cast<std::uint32_t>(*Number);
					};
					const auto StartFrame = ReadCount("startFrame");
					const auto Levels = ReadCount("levels");
					if (!StartFrame || !Levels)
					{
						WriteResponse(RequestSeq, Command, false, FJson::Object(),
							"Stack frame offset and count must be non-negative 32-bit integers.");
						return true;
					}
					const FDapStackTraceResult Result = Adapter.StackTrace();
					const std::size_t Begin = (std::min)(static_cast<std::size_t>(*StartFrame),
						Result.StackFrames.size());
					const std::size_t Count = *Levels == 0 ? Result.StackFrames.size() - Begin
						: (std::min)(static_cast<std::size_t>(*Levels), Result.StackFrames.size() - Begin);
					FJson::FArray Frames;
					std::map<std::string, FJson> SourcesInStack;
					for (std::size_t Index = Begin; Index < Begin + Count; ++Index)
					{
						const FDapStackFrame& Frame = Result.StackFrames[Index];
						FJson::FObject Item;
						Item["id"] = FJson::Number(static_cast<double>(Frame.Id));
						Item["name"] = FJson::String(Frame.Name);
						Item["line"] = FJson::Number(
							static_cast<double>(ClientLine(Frame.Line)));
						Item["column"] = FJson::Number(
							static_cast<double>(ClientColumn(Frame.Column)));
						const auto CachedSource = SourcesInStack.find(Frame.Path);
						if (CachedSource != SourcesInStack.end())
						{
							Item["source"] = CachedSource->second;
						}

						else
						{
							FJson::FObject Source{{"path", FJson::String(Frame.Path)}};
							std::string Generated;
							if (Adapter.TryGetSource(Frame.Path, Generated))
							{
								// Capture code once while stopped. sourceReference then reads
								// immutable text without VM access, even after execution resumes.
								int Reference = 0;
								for (const auto& Entry : GeneratedSources)
								{
									if (Entry.second.first == Frame.Path && Entry.second.second == Generated)
									{
										Reference = Entry.first;
										break;
									}
								}

								if (Reference == 0 && GeneratedSources.size() < 4096 &&
									Generated.size() <= 16u * 1024u * 1024u - GeneratedSourceBytes &&
									NextGeneratedSourceReference < (std::numeric_limits<int>::max)())
								{
									Reference = NextGeneratedSourceReference++;
									GeneratedSourceBytes += Generated.size();
									GeneratedSources.emplace(Reference, std::make_pair(Frame.Path, std::move(Generated)));
								}
								Source["sourceReference"] = FJson::Number(Reference);
								Source["presentationHint"] = FJson::String("deemphasize");
							}
							Item["source"] = FJson::Object(std::move(Source));
							SourcesInStack.emplace(Frame.Path, Item["source"]);
						}
						Frames.push_back(FJson::Object(std::move(Item)));
					}
					WriteResponse(
						RequestSeq,
						Command,
						Result.bSuccess,
						FJson::Object({
							{"stackFrames", FJson::Array(std::move(Frames))},
							{"totalFrames", FJson::Number(static_cast<double>(Result.StackFrames.size()))},
						}),
						Result.bSuccess
							? std::string{}
							: Locale::TranslateId("dap.not_stopped"));
					return true;
				}

				if (Command == "scopes")
				{
					const int FrameId = Args.GetInt("frameId", 0);
					const FDapScopesResult Result = Adapter.Scopes(FrameId);
					FJson::FArray Scopes;
					for (const FDapScope& Scope : Result.Scopes)
					{
						FJson::FObject Item;
						Item["name"] = FJson::String(Scope.Name);
						Item["variablesReference"] =
							FJson::Number(static_cast<double>(Scope.VariablesReference));
						Item["expensive"] = FJson::Bool(Scope.bExpensive);
						Scopes.push_back(FJson::Object(std::move(Item)));
					}
					WriteResponse(
						RequestSeq,
						Command,
						Result.bSuccess,
						FJson::Object({{"scopes", FJson::Array(std::move(Scopes))}}),
						Result.bSuccess
							? std::string{}
							: Locale::TranslateId("dap.not_stopped"));
					return true;
				}

				if (Command == "source")
				{
					const int Reference = Args.GetInt("sourceReference", 0);
					const auto Source = GeneratedSources.find(Reference);
					const auto* Requested = Args.Find("source");
					const std::string Path = Requested ? Requested->GetString("path") : std::string{};
					const bool Found = Source != GeneratedSources.end() &&
						(Path.empty() || Path == Source->second.first);
					WriteResponse(RequestSeq, Command, Found, Found ? FJson::Object({
						{"content", FJson::String(Source->second.second)},
						{"mimeType", FJson::String("text/x-veroscript")}}) : FJson::Object(),
						Found ? std::string{} : "unknown generated source reference");
					return true;
				}

				if (Command == "variables")
				{
					const int VariablesReference = Args.GetInt("variablesReference", 0);
					const FDapVariablesResult Result = Adapter.Variables(VariablesReference);
					FJson::FArray Variables;
					for (const FDapVariable& Variable : Result.Variables)
					{
						FJson::FObject Item;
						Item["name"] = FJson::String(Variable.Name);
						Item["value"] = FJson::String(Variable.Value);
						if (Variable.bReadOnly)
						{
							Item["presentationHint"] = FJson::Object({
								{"attributes", FJson::Array({FJson::String("readOnly")})}});
						}
						if (Variable.Memory.StorageAddress)
						{
							Item["memoryReference"] =
								FJson::String(std::to_string(*Variable.Memory.StorageAddress));
						}
						FJson::FObject MemoryInfo;
						if (Variable.Memory.ObjectId)
						{
							MemoryInfo["objectId"] = FJson::String(std::to_string(*Variable.Memory.ObjectId));
						}
						if (Variable.Memory.ObjectAddress)
						{
							MemoryInfo["objectAddress"] =
								FJson::String(std::to_string(*Variable.Memory.ObjectAddress));
						}
						if (Variable.Memory.DataAddress)
						{
							MemoryInfo["dataAddress"] =
								FJson::String(std::to_string(*Variable.Memory.DataAddress));
						}
						if (Variable.Memory.ByteLength)
						{
							MemoryInfo["byteLength"] =
								FJson::String(std::to_string(*Variable.Memory.ByteLength));
						}
						if (!MemoryInfo.empty())
						{
							Item["veroMemory"] = FJson::Object(std::move(MemoryInfo));
						}
						Item["variablesReference"] =
							FJson::Number(static_cast<double>(Variable.VariablesReference));
						Variables.push_back(FJson::Object(std::move(Item)));
					}
					WriteResponse(
						RequestSeq,
						Command,
						Result.bSuccess,
						FJson::Object({{"variables", FJson::Array(std::move(Variables))}}),
						Result.bSuccess
							? std::string{}
							: Locale::TranslateId("dap.not_stopped"));
					return true;
				}

				if (Command == "evaluate")
				{
					const std::string Expression = Args.GetString("expression");
					int FrameId = Args.GetInt("frameId", -1);
					if (!Args.Find("frameId"))
					{
						const auto Stack = Adapter.StackTrace();
						if (Stack.bSuccess && !Stack.StackFrames.empty())
						{
							FrameId = Stack.StackFrames.front().Id;
						}
					}
					const std::string Context = Args.GetString("context");
					const FDapEvaluateResult Result =
						Adapter.Evaluate(Expression, FrameId, Context);
					if (!Result.bSuccess)
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Result.Error.empty()
								? Locale::TranslateId("dap.evaluate_failed")
								: Result.Error);
						return true;
					}
					WriteResponse(
						RequestSeq,
						Command,
						true,
						FJson::Object({
							{"result", FJson::String(Result.Result)},
							{"variablesReference",
								FJson::Number(static_cast<double>(Result.VariablesReference))},
						}));
					return true;
				}

				if (Command == "setVariable")
				{
					const int VariablesReference = Args.GetInt("variablesReference", 0);
					const std::string Name = Args.GetString("name");
					const std::string Value = Args.GetString("value");
					const FDapSetVariableResult Result =
						Adapter.SetVariable(VariablesReference, Name, Value);
					if (!Result.bSuccess)
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Result.Error.empty()
								? Locale::TranslateId("dap.set_variable_failed")
								: Result.Error);
						return true;
					}
					WriteResponse(
						RequestSeq,
						Command,
						true,
						FJson::Object({
							{"value", FJson::String(Result.Value)},
							{"variablesReference",
								FJson::Number(static_cast<double>(Result.VariablesReference))},
						}));
					return true;
				}

				const bool bStoppedRequest = bFromStopPump || Adapter.IsStopped();
				if (Command == "continue")
				{
					if (!bStoppedRequest)
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Locale::TranslateId("dap.not_stopped"));
						return true;
					}
					if (!Adapter.IsDomainAttached())
					{
						WriteResponse(RequestSeq, Command, true,
							FJson::Object({{"allThreadsContinued", FJson::Bool(true)}}));
						(void)RequestResume(EResumeKind::Continue);
						return true;
					}
					const FDapContinueResult Result = RequestResume(EResumeKind::Continue);
					WriteResponse(
						RequestSeq,
						Command,
						Result.bSuccess,
						FJson::Object({{"allThreadsContinued", FJson::Bool(Result.bAllThreadsContinued)}}),
						Result.bSuccess ? std::string{} : Locale::TranslateId("dap.not_stopped"));
					return true;
				}

				if (Command == "next")
				{
					if (!bStoppedRequest)
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Locale::TranslateId("dap.not_stopped"));
						return true;
					}
					if (!Adapter.IsDomainAttached())
					{
						WriteResponse(RequestSeq, Command, true);
						(void)RequestResume(EResumeKind::Next);
						return true;
					}
					const FDapContinueResult Result = RequestResume(EResumeKind::Next);
					WriteResponse(RequestSeq, Command, Result.bSuccess, FJson::Object(),
						Result.bSuccess ? std::string{} : Locale::TranslateId("dap.not_stopped"));
					return true;
				}

				if (Command == "stepIn")
				{
					if (!bStoppedRequest)
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Locale::TranslateId("dap.not_stopped"));
						return true;
					}
					if (!Adapter.IsDomainAttached())
					{
						WriteResponse(RequestSeq, Command, true);
						(void)RequestResume(EResumeKind::StepIn);
						return true;
					}
					const FDapContinueResult Result = RequestResume(EResumeKind::StepIn);
					WriteResponse(RequestSeq, Command, Result.bSuccess, FJson::Object(),
						Result.bSuccess ? std::string{} : Locale::TranslateId("dap.not_stopped"));
					return true;
				}

				if (Command == "stepOut")
				{
					if (!bStoppedRequest)
					{
						WriteResponse(
							RequestSeq,
							Command,
							false,
							FJson::Object(),
							Locale::TranslateId("dap.not_stopped"));
						return true;
					}
					if (!Adapter.IsDomainAttached())
					{
						WriteResponse(RequestSeq, Command, true);
						(void)RequestResume(EResumeKind::StepOut);
						return true;
					}
					const FDapContinueResult Result = RequestResume(EResumeKind::StepOut);
					WriteResponse(RequestSeq, Command, Result.bSuccess, FJson::Object(),
						Result.bSuccess ? std::string{} : Locale::TranslateId("dap.not_stopped"));
					return true;
				}

				if (Command == "pause")
				{
					const FDapContinueResult Result = Adapter.Pause();
					WriteResponse(
						RequestSeq,
						Command,
						Result.bSuccess,
						FJson::Object(),
						Result.bSuccess
							? std::string{}
							: Locale::TranslateId("dap.not_running"));
					return true;
				}

				if (Command == "disconnect" || Command == "terminate")
				{
					// Only an actually loaded launch target defaults to termination.
					// Cancelling setup or a failed launch must leave the host alive.
					const bool bTerminate = Command == "terminate"
						? true
						: Args.GetBool("terminateDebuggee", !bAttachedSession && Adapter.IsLoaded());
					if (bTerminate && Adapter.IsDomainAttached()
						&& !Adapter.AttachedDomainInfo().bCanTerminate)
					{
						WriteResponse(RequestSeq, Command, false, FJson::Object(),
							"host debug domain does not support termination");
						return true;
					}
					bTerminateHost = bTerminate;
					bCancelExecutionOnShutdown = bTerminate;
					WriteResponse(RequestSeq, Command, true);
					if (bTerminate)
					{
						Adapter.Terminate();
					}
					if (bStoppedRequest)
					{
						RequestResume(EResumeKind::Disconnect);
					}
					else
					{
						Adapter.Disconnect();
					}
					bExit.store(true);
					return true;
				}

				if (Command == "setExceptionBreakpoints")
				{
					// DAP clients commonly send an empty setExceptionBreakpoints request
					// during their mandatory launch handshake even when the adapter did not
					// advertise exception filters. Accept that request as the operation that
					// clears an already-empty policy, while still rejecting any real filter
					// request so clients never believe unsupported exception handling is active.
					bool bRequestsUnsupportedFilter = false;
					for (const char* FieldName : {"filters", "filterOptions", "exceptionOptions"})
					{
						if (const FJson* Field = Args.Find(FieldName))
						{
							const FJson::FArray* Array = Field->ArrayPtr();
							bRequestsUnsupportedFilter = !Array || !Array->empty();
							if (bRequestsUnsupportedFilter)
							{
								break;
							}
						}
					}
					WriteResponse(
						RequestSeq,
						Command,
						!bRequestsUnsupportedFilter,
						FJson::Object(),
						bRequestsUnsupportedFilter
							? Locale::TranslateId("dap.unsupported_breakpoint_type")
							: std::string{});
					return true;
				}

				// Function breakpoints are not implemented. Do not acknowledge a
				// non-empty policy as success: clients would otherwise show it as active.
				if (Command == "setFunctionBreakpoints")
				{
					WriteResponse(
						RequestSeq,
						Command,
						false,
						FJson::Object(),
						Locale::TranslateId("dap.unsupported_breakpoint_type"));
					return true;
				}

				WriteResponse(
					RequestSeq,
					Command,
					false,
					FJson::Object(),
					Locale::TranslateId("dap.unsupported_command"));
				return true;
			}

			std::istream& In;
			std::ostream& Out;
			FDapStdioRunOptions Options;
			std::map<int, std::pair<std::string, std::string>> GeneratedSources;
			std::size_t GeneratedSourceBytes = 0;
			int NextGeneratedSourceReference = 1;
			IVeroScriptLiveDebugDomain* LiveDomain = nullptr;
			std::atomic<bool> bDomainAttachInProgress{false};
			VeroScript::FHostBindingRegistry BindingCatalog;
			VeroScriptDapAdapter Adapter;
			int NextSeq = 1;
			std::atomic<bool> bExit{false};
			bool bFramingError = false;
			std::mutex OutputMutex;
			std::thread ExecutionThread;
			std::atomic<bool> bExecutionActive{false};
			bool bCancelExecutionOnShutdown = false;
			std::mutex StopMutex;
			std::condition_variable StopCondition;
			bool bStopPumpActive = false;
			std::optional<std::string> PendingStopRequest;
			bool bStopRequestCompleted = false;
			bool bStopRequestResult = true;
			// Filled by successful DAP attach (affects disconnect terminateDebuggee default).
			bool bAttachedSession = false;
			// Client asked to stop the host DAP server (terminateDebuggee=true).
			bool bTerminateHost = false;
			bool bResumeRequested = false;
			bool bClientLinesStartAt1 = true;
			bool bClientColumnsStartAt1 = true;

		};
	}

	int VeroScriptDapStdio::Run(
		std::istream& In,
		std::ostream& Out,
		const FDapStdioRunOptions& Options)
	{
		return Run(In, Out, Options, nullptr);
	}

	int VeroScriptDapStdio::Run(
		std::istream& In,
		std::ostream& Out,
		const FDapStdioRunOptions& Options,
		IVeroScriptLiveDebugDomain* LiveDomain)
	{
		return Run(In, Out, Options, LiveDomain, nullptr);
	}

	int VeroScriptDapStdio::Run(
		std::istream& In,
		std::ostream& Out,
		const FDapStdioRunOptions& Options,
		IVeroScriptLiveDebugDomain* LiveDomain,
		const std::atomic<bool>* StopRequested)
	{
		const Internal::FScopedProtocolStreamTies StreamTies(In, Out);
		return FDapStdioSession(In, Out, Options, LiveDomain).Run(StopRequested);
	}
}

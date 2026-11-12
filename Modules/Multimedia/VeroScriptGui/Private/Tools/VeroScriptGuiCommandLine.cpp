// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "Compilation/VeroScriptGuiSource.h"
#include "Compilation/VeroScriptGuiSourceImports.h"
#include "Bindings/VeroScriptGuiScriptLibrary.h"
#include "VeroScriptArtifact.h"
#include "VeroScriptGuiServices.h"
#include "VeroScriptGuiTools.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"
#if defined(VEROSCRIPT_WITH_APPLICATION)
#include "VeroScriptApplication.h"
#endif
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace VeroScript::Gui
{
	namespace
	{
		enum class ETask
		{
			Check,
			Compile,
			Preview
		};
		struct FState;
		struct FCommand
		{
			FState *State;
			ETask Task;
		};
		struct FState : std::enable_shared_from_this<FState>
		{
			explicit FState(FToolOptions Input)
			    : Options(std::move(Input)),
			      Commands{{{this, ETask::Check}, {this, ETask::Compile}, {this, ETask::Preview}}}
			{
			}
			FToolOptions Options;
			std::array<FCommand, 3> Commands;
			std::map<std::string, std::string> Backends;
		};

		struct FArguments
		{
			std::string Input, Output, Name = "gui-app";
			std::int64_t Frames = 0;
			std::vector<std::string> Script, Assets;
		};

		FScriptServices RuntimeServices(const std::shared_ptr<FState> &State)
		{
			auto Runtime = State->Options.Runtime;
			const auto Weak = std::weak_ptr<FState>(State);
			Runtime.DefaultBackend = [Weak](std::string_view Role)
			{
				const auto State = Weak.lock();
				if (!State)
				{
					return std::string{};
				}
				const auto Found = State->Backends.find(std::string(Role) + "-backend");
				if (Found != State->Backends.end())
				{
					return Found->second;
				}
				return State->Options.Runtime.DefaultBackend ? State->Options.Runtime.DefaultBackend(Role)
				                                             : std::string{};
			};
			Runtime.ResolveResourceReference =
			    [Weak](const auto &Requester, const auto &Reference, auto &Identity, auto &Error)
			{
				const auto State = Weak.lock();
				if (!State)
				{
					Error = "gui.tool.invocation_expired";
					return false;
				}
				const auto &Resolve = State->Options.Runtime.ResolveResourceReference;
				return Resolve ? Resolve(Requester, Reference, Identity, Error)
				               : ResolveGuiResourceReference(Requester, Reference, Identity, Error);
			};
			Runtime.IsFrameworkModule = [Weak](const auto &Module)
			{
				const auto State = Weak.lock();
				return State && State->Options.Runtime.IsFrameworkModule &&
				    State->Options.Runtime.IsFrameworkModule(Module);
			};
			Runtime.ReadResource = [Weak](const auto &Identity, auto &Bytes, auto &Error)
			{
				const auto State = Weak.lock();
				if (!State)
				{
					Error = "gui.tool.invocation_expired";
					return false;
				}
				const auto &Read = State->Options.Runtime.ReadResource;
				return Read ? Read(Identity, Bytes, Error)
				            : MakeGuiResourceReader(State->Options.Engine.ModuleEnvironment)(Identity, Bytes, Error);
			};
			// Factories are read at creation, after the package host has collected providers.
			Runtime.ResolveApplicationFactories = [Weak](FApplicationFactories &Factories, std::string &Error)
			{
				const auto State = Weak.lock();
				if (!State)
				{
					Error = "gui.tool.invocation_expired";
					return false;
				}
				Factories = State->Options.Runtime.Application;
				const auto &Resolve = State->Options.Runtime.ResolveApplicationFactories;
				return !Resolve || Resolve(Factories, Error);
			};
			return Runtime;
		}

		bool PrepareEngine(FEngineConfig &Engine, const std::shared_ptr<FState> &State, std::string &Error,
		    const FHostBindingRegistry *Base = nullptr, bool bImplicitFramework = false)
		{
			auto Next = Engine;
			const auto IsFrameworkImport = [&](const auto &Import)
			{
				const std::string Name(Import);
				if (PathUtil::Equal(Name, GetGuiScriptLibraryPath())) return true;
				const auto *Module = Next.ModuleEnvironment ? Next.ModuleEnvironment->FindModule(Name) : nullptr;
				return Module && PathUtil::Equal(Module->Path, GetGuiScriptLibraryPath());
			};
			bool ExplicitFramework = std::any_of(Next.ImplicitImportPaths.begin(), Next.ImplicitImportPaths.end(),
			    IsFrameworkImport);
			if (Next.ModuleEnvironment)
			{
				const auto &Imports = Next.ModuleEnvironment->GetDescription().ImplicitImports;
				ExplicitFramework = ExplicitFramework || std::any_of(Imports.begin(), Imports.end(), IsFrameworkImport);
			}
			if (Next.ModuleEnvironment || Base)
			{
				auto Description =
				    Next.ModuleEnvironment ? Next.ModuleEnvironment->GetDescription() : FModuleEnvironmentDesc{};
				if (!Next.ModuleEnvironment)
				{
					Description.bStrictSources = false;
					Description.bAllowAmbientSearch = true;
					Description.EntryOwnerId = "veroscript.gui.cli.entry";
					Description.Scopes.push_back({Description.EntryOwnerId, {}, false});
					if (Base)
					{
						Description.Bindings = std::make_shared<FHostBindingRegistry>(*Base);
					}
				}
				if (!Description.Bindings || !Description.Bindings->FindType("VS.Gui.__NativeWidget"))
				{
					const auto *Existing = dynamic_cast<const FHostBindingRegistry *>(Description.Bindings.get());
					if (Description.Bindings && !Existing)
					{
						Error = "gui.tool.requires_gui_catalog";
						return false;
					}
					auto Catalog = Existing ? std::make_shared<FHostBindingRegistry>(*Existing)
					                        : std::make_shared<FHostBindingRegistry>();
					if (!RegisterScriptBindings(*Catalog, RuntimeServices(State), Error))
					{
						return false;
					}
					Description.Bindings = std::move(Catalog);
				}
				Next.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
				if (!Next.ModuleEnvironment)
				{
					return false;
				}
			}
			if (!ConfigureGuiScriptEngine(Next, State->Options.Compilation, Error) ||
			    (!ExplicitFramework && !bImplicitFramework &&
			        !ConfigureGuiSourceImports(Next, State->Options.Compilation, Error)))
			{
				return false;
			}
			Engine = std::move(Next);
			return true;
		}

		bool Parse(const std::vector<std::string> &Input, ETask Task, FArguments &Result, std::string &Error)
		{
			std::set<std::string> Seen;
			for (std::size_t I = 0; I < Input.size(); ++I)
			{
				const auto &Item = Input[I];
				if (Item == "--" && Task == ETask::Preview)
				{
					Result.Script.assign(Input.begin() + I + 1, Input.end());
					break;
				}
				if (Item.rfind("--", 0) == 0)
				{
					const auto Equal = Item.find('=');
					const auto Name = Item.substr(0, Equal);
					if ((Name != "--asset" && !Seen.insert(Name).second) ||
					    (((Name != "--output" && Name != "--name" && Name != "--asset") || Task != ETask::Compile) &&
					        (Name != "--frames" || Task != ETask::Preview)))
					{
						Error = "unknown or repeated GUI task option: " + Name;
						return false;
					}
					std::string Value;
					if (Equal != std::string::npos)
					{
						Value = Item.substr(Equal + 1);
					}
					else if (I + 1 < Input.size())
					{
						Value = Input[++I];
					}
					if (Value.empty())
					{
						Error = "missing GUI task option value: " + Name;
						return false;
					}
					if (Name == "--output")
					{
						Result.Output = std::move(Value);
					}
					else if (Name == "--asset")
					{
						Result.Assets.push_back(std::move(Value));
					}
					else if (Name == "--name")
					{
						Result.Name = std::move(Value);
					}
					else
					{
						const auto Parsed = std::from_chars(Value.data(), Value.data() + Value.size(), Result.Frames);
						if (Parsed.ec != std::errc{} || Parsed.ptr != Value.data() + Value.size() || Result.Frames < 1)
						{
							Error = "--frames requires a positive int64";
							return false;
						}
					}
				}
				else if (Result.Input.empty())
				{
					Result.Input = Item;
				}
				else
				{
					Error = "unexpected GUI task argument: " + Item;
					return false;
				}
			}
			if (Result.Input.empty() || (Task == ETask::Compile && Result.Output.empty()))
			{
				Error = "GUI task requires an input and gui-compile also requires --output";
				return false;
			}
			return true;
		}

		FCompileOptions CompilerOptions(const FEngineConfig &Engine)
		{
			FCompileOptions Result;
			Result.ImportPaths = Engine.ImportPaths;
			Result.Defines = Engine.Defines;
			Result.PredefinedMacros = Engine.PredefinedMacros;
			Result.SourceOverlays = Engine.SourceOverlays;
			Result.ImplicitImportPaths = Engine.ImplicitImportPaths;
			Result.StdlibSearchPaths = Engine.StdlibSearchPaths;
			Result.bAllowCircularImports = Engine.bAllowCircularImports;
			Result.bAllowProjectWideAccess = Engine.bAllowProjectWideAccess;
			Result.AnnotationProcessing = Engine.AnnotationProcessing;
			Result.CompileProcessing = Engine.CompileProcessing;
			Result.ScriptCompileExtensions = Engine.ScriptCompileExtensions;
			if (!Result.ScriptCompileExtensions.VmBackendFactory && Result.ScriptCompileExtensions.VmBackendPath.empty())
			{
				Result.ScriptCompileExtensions.VmBackendFactory = Engine.VmBackendFactory;
				Result.ScriptCompileExtensions.VmBackendPath = Engine.VmBackendFactory ? std::string{} : Engine.VmBackendPath;
				if (!Result.ScriptCompileExtensions.VmBackendFactory && Result.ScriptCompileExtensions.VmBackendPath.empty() && !Engine.bClearVmBackend)
				{
					Result.ScriptCompileExtensions.VmBackendFactory = Engine.FallbackVmBackendFactory;
					Result.ScriptCompileExtensions.VmBackendPath = Engine.FallbackVmBackendPath;
				}
			}
			if (!Result.ScriptCompileExtensions.ExecutionControl)
			{
				Result.ScriptCompileExtensions.ExecutionControl = Engine.ExecutionControl;
			}
			Result.ModuleEnvironment = Engine.ModuleEnvironment;
			return Result;
		}

		std::string PreviewSource(const std::string &Path, const FArguments &Arguments, const FState &State)
		{
			std::string Result = "import " + GuiQuote(Path) +
			    ";void main(){VS::Gui::ApplicationOptions options=VS::Gui::ApplicationOptions();";
			for (const auto &Entry : State.Backends)
			{
				const auto Property = Entry.first == "platform-backend" ? "PlatformBackend"
				    : Entry.first == "graphics-backend"                 ? "GraphicsBackend"
				    : Entry.first == "image-backend"                    ? "ImageBackend"
				    : Entry.first == "font-backend"                     ? "FontBackend"
				    : Entry.first == "text-layout-backend"              ? "TextLayoutBackend"
				                                                        : "GuiBackend";
				Result += "options." + std::string(Property) + '=' + GuiQuote(Entry.second) + ';';
			}
			Result += "auto app=VS::Gui::Application(options);try{auto root=VS::Gui::LoadFactory<VS::Gui::Widget>(" +
			    GuiQuote(PathUtil::ToUtf8(PathUtil::FromUtf8(Path).filename())) + ");app.SetRoot(root.Create());";
			Result += Arguments.Frames > 0 ? "for(int64 frame=0;frame<" + std::to_string(Arguments.Frames) +
			        "i64 && app.IsOpen();frame++){app.Tick();}"
			                               : "app.Run();";
			return Result + "}finally{app.Close();}}";
		}

		class FHost final : public IEngineHost
		{
		public:
			explicit FHost(const FCommandLineContext &Input) : Context(Input)
			{
			}
			void OnPrintLine(std::string_view Text) override
			{
				if (Context.Output)
				{
					*Context.Output << Text << '\n';
				}
			}
			void OnDiagnosticLine(std::string_view Text) override
			{
				if (Context.Errors)
				{
					*Context.Errors << Text << '\n';
				}
			}

		private:
			const FCommandLineContext &Context;
		};

		int Run(const FCommandLineContext &Context, const std::vector<std::string> &Input, void *Data)
		{
			const auto &Command = *static_cast<FCommand *>(Data);
			const auto &State = *Command.State;
			std::string Error;
			const auto Fail = [&](int Code, const std::string &Message)
			{
				if (Context.Errors)
				{
					*Context.Errors << Message << '\n';
				}
				return Code;
			};
			try
			{
				FArguments Arguments;
				if (!Parse(Input, Command.Task, Arguments, Error))
				{
					return Fail(2, Error);
				}
				const auto Resolve = [&](const std::string &Name)
				{
					auto Path = PathUtil::FromUtf8(Name);
					if (!Path.is_absolute())
					{
						Path = PathUtil::FromUtf8(Context.WorkingDirectory) / Path;
					}
					return PathUtil::ToUtf8(std::filesystem::absolute(Path).lexically_normal());
				};
				const auto Path = Resolve(Arguments.Input);
				const auto Extension = PathUtil::ToUtf8(PathUtil::FromUtf8(Path).extension());
				const bool Markup = Extension == ".vswml";
				const bool Artifact = Extension == ".vsapp";
				if (!Markup && !Artifact && Extension != ".vs")
				{
					return Fail(2, "expected .vs, .vswml or .vsapp input");
				}
				if (Command.Task == ETask::Compile &&
				    (Artifact || PathUtil::FromUtf8(Arguments.Output).extension() != ".vsapp"))
				{
					return Fail(2, "gui-compile requires source input and a .vsapp output");
				}
				if (!Markup && Arguments.Frames > 0)
				{
					return Fail(2, "--frames is available for ML preview entries only");
				}
				auto Engine = State.Options.Engine;
				if (Context.Environment)
				{
					Engine.ModuleEnvironment = Context.Environment;
				}
				Engine.InputStream = Context.Input;
				Engine.ScriptArgs.clear();
				for (const auto &Arg : Arguments.Script)
				{
					Engine.ScriptArgs.push_back(Arg);
				}
				const auto Source = Markup ? PreviewSource(Path, Arguments, State) : std::string{};
				#if defined(VEROSCRIPT_WITH_APPLICATION)
				std::shared_ptr<const Application::FApplication> ApplicationInput;
				#else
				if (Artifact || Command.Task == ETask::Compile)
				{
					return Fail(2, "GUI application containers require Application support");
				}
				#endif
				if (Artifact)
				{
					#if defined(VEROSCRIPT_WITH_APPLICATION)
					if (!PrepareEngine(Engine, Command.State->shared_from_this(), Error, nullptr, true))
					{
						return Fail(2, Error);
					}
					auto Services = Application::CreateApplicationServices();
					Services.Environment = Engine.ModuleEnvironment;
					for (const auto Root : Engine.StdlibSearchPaths)
					{
						Services.StandardLibraryPaths.emplace_back(Root);
					}
					Services.PreparePackage = [State = Command.State->shared_from_this()](const auto &Package, auto &Failure)
					{
						return ConfigureGuiArtifactServices(Package, State->Options.Runtime, Failure);
					};
					Application::FDiagnostic Diagnostic;
					ApplicationInput = Application::FApplication::Open(Path, Services, Diagnostic);
					if (!ApplicationInput || ApplicationInput->IsSourceEntry())
					{
						return Fail(1, ApplicationInput ? "expected a compiled GUI application" : Diagnostic.Code + ": " + Diagnostic.Message);
					}
					if (Command.Task == ETask::Check)
					{
						FPackageData Package;
						if (!ApplicationInput->ReadEntryPackage(Package, Diagnostic) ||
						    !ConfigureGuiArtifactServices(Package, Command.State->Options.Runtime, Error))
						{
							return Fail(1, Error.empty() ? Diagnostic.Code + ": " + Diagnostic.Message : Error);
						}
						return 0;
					}
					#endif
				}
				if (!Artifact && Command.Task != ETask::Preview)
				{
					const auto Core = CompilerOptions(Engine);
					auto Compiled = Markup && Command.Task == ETask::Compile
					    ? CompileGuiSource(Source, Path + ".gui.vs", Core, State.Options.Compilation)
					    : CompileGuiFile(Path, Core, State.Options.Compilation);
					if (!Compiled.bSuccess)
					{
						return Fail(1, Compiled.Error.Location.FileLabel + ':' + std::to_string(Compiled.Error.Location.Line) + ':' + std::to_string(Compiled.Error.Location.Column) + ": " + Compiled.Error.Message);
					}
					if (Command.Task == ETask::Check)
					{
						if (Context.Output)
						{
							*Context.Output << "Checked " << Path << '\n';
						}
						return 0;
					}
					FPackageBuildOptions Package;
					Package.PackageName = Arguments.Name;
					Package.bStripDebugInfo = true;
					std::vector<std::uint8_t> Bytes;
					FGuiAssetOptions Assets;
					Assets.ReadResource = State.Options.Runtime.ReadResource;
					for (const auto &Reference : Arguments.Assets)
					{
						Assets.Resources.push_back(Resolve(Reference));
					}
					if (!BakeGuiApplication(Compiled, Package, Assets, Bytes, Error))
					{
						return Fail(1, Error);
					}
					#if defined(VEROSCRIPT_WITH_APPLICATION)
					std::vector<Application::FContentFile> Files;
					Application::FDiagnostic Diagnostic;
					if (!Application::BuildApplication(std::move(Bytes), Version, {}, Files, Diagnostic) ||
					    !Application::WriteArchive(Files, Bytes, Diagnostic))
					{
						return Fail(1, Diagnostic.Code + ": " + Diagnostic.Message);
					}
					#endif
					const auto Output = PathUtil::FromUtf8(Resolve(Arguments.Output));
					std::filesystem::create_directories(Output.parent_path());
					const auto Temporary = PathUtil::MakeTemporarySiblingPath(Output);
					bool Written = false;
					{
						std::ofstream Stream(Temporary, std::ios::binary | std::ios::trunc);
						Stream.write(reinterpret_cast<const char *>(Bytes.data()),
						    static_cast<std::streamsize>(Bytes.size()));
						Stream.close();
						Written = static_cast<bool>(Stream);
					}
					std::error_code IoError;
					if (!Written || !PathUtil::ReplaceFileAtomically(Temporary, Output, IoError))
					{
						std::error_code Ignored;
						std::filesystem::remove(Temporary, Ignored);
						return Fail(1, "gui.tool.output_write_failed: " + PathUtil::ToUtf8(Output) + ": " + IoError.message());
					}
					if (Context.Output)
					{
						*Context.Output << PathUtil::ToUtf8(Output) << '\n';
					}
					return 0;
				}
				FHost Host(Context);
				VeroScriptSession Session(Host);
				if ((!Engine.ModuleEnvironment &&
				        !RegisterScriptBindings(Session.GetBindingRegistry(),
				            RuntimeServices(Command.State->shared_from_this()), Error)) ||
				    (!Artifact && !PrepareEngine(Engine, Command.State->shared_from_this(), Error, nullptr, true)))
				{
					return Fail(2, Error);
				}
				if (!Session.ApplyConfig(Engine, &Error))
				{
					return Fail(2, Error);
				}
				auto Loaded =
				#if defined(VEROSCRIPT_WITH_APPLICATION)
				    Artifact ? Session.LoadApplication(std::move(ApplicationInput)) :
				#endif
				    Markup ? Session.LoadSource(Source, Path + ".gui.vs")
				           : Session.LoadFile(Path);
				if (!Loaded.bSuccess)
				{
					return Fail(1, Loaded.Error.Message);
				}
				const auto Executed = Session.Run();
				return Executed.bSuccess ? Executed.ExitCode : Fail(1, Executed.Error.Message);
			}
			catch (const std::exception &Exception)
			{
				return Fail(1, std::string("gui.tool.exception: ") + Exception.what());
			}
		}

		class FService final : public ICommandLineService
		{
		public:
			explicit FService(FToolOptions Input) : Options(std::move(Input))
			{
			}
			bool HasHostRegistration() const noexcept override
			{
				return Options.bHostOwnsCommandRegistration;
			}
			FCommandLineRegistration CreateRegistration() const override
			{
				auto State = std::make_shared<FState>(Options);
				FCommandLineRegistration Result;
				Result.Owner = State;
				Result.SetEngineOptions = [State](const auto &Engine)
				{
					State->Options.Engine = Engine;
				};
				Result.SetRuntimeServices = [State](auto Services)
				{
					State->Options.Runtime = std::move(Services);
				};
				Result.RegisterBindings = [State](auto &Registry, auto &Error)
				{
					return RegisterScriptBindings(Registry, RuntimeServices(State), Error);
				};
				Result.ConfigureEngine = [State](auto &Engine, const auto *Base, auto &Error)
				{
					return PrepareEngine(Engine, State, Error, Base);
				};
				Result.UseArtifact = [State](const auto &Package, auto &Error)
				{
					return ConfigureGuiArtifactServices(Package, State->Options.Runtime, Error);
				};
				auto &Module = Result.Module;
				Module.Id = "veroscript-gui-tools";
				Module.Description = "GUI compilation and preview";
				for (const auto *Name :
				    {"platform-backend", "graphics-backend", "gui-backend", "image-backend", "font-backend", "text-layout-backend"})
				{
					Module.Options.push_back({Name, "Select the GUI application service backend", "id", true, false});
				}
				Module.UserData = State.get();
				Module.Configure =
				    [](const FCommandLineContext &, const FCommandLineValues &Values, void *Data, std::string &Error)
				{
					auto &State = *static_cast<FState *>(Data);
					std::map<std::string, std::string> Next;
					for (const auto &Value : Values)
					{
						if (Value.second.size() != 1 || Value.second.front().empty())
						{
							Error = "invalid GUI backend option: " + Value.first;
							return false;
						}
						Next.emplace(Value.first, Value.second.front());
					}
					State.Backends = std::move(Next);
					return true;
				};
				Module.Commands.push_back({"gui-check", "Check GUI sources without running scripts", "gui-check INPUT",
				    Run, &State->Commands[0]});
				Module.Commands.push_back({"gui-compile", "Bake a GUI application",
				    "gui-compile INPUT --output FILE [--name NAME] [--asset FILE]...", Run,
				    &State->Commands[1]});
				Module.Commands.push_back({"gui-preview", "Run a GUI application",
				    "gui-preview INPUT [--frames N] [-- SCRIPT_ARGS]", Run,
				    &State->Commands[2]});
				return Result;
			}

		private:
			FToolOptions Options;
		};
	} // namespace

	std::shared_ptr<const ICommandLineService> CreateCommandLineService(FToolOptions Options)
	{
		return std::make_shared<FService>(std::move(Options));
	}
} // namespace VeroScript::Gui

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiRuntimeBindings.h"
#include "VeroScriptBackendSelection.h"
#include "VeroScriptGuiLocalization.h"
#include "VeroScriptGuiMarkup.h"

namespace VeroScript::Gui::ScriptBindings
{
	bool RegisterRuntimeBindings(FHostBindingRegistry &Registry, const std::shared_ptr<FBridge> &Bridge,
	    std::string &Error)
	{
		const auto Void = FNativeTypeDesc::Scalar(ENativeType::Void);
		const auto Bool = FNativeTypeDesc::Scalar(ENativeType::Bool);
		const auto String = FNativeTypeDesc::Scalar(ENativeType::String);
		const auto Double = FNativeTypeDesc::Scalar(ENativeType::Double);
		const auto Int64 = FNativeTypeDesc::Scalar(ENativeType::Int64);
		const auto NativeWidget = FNativeTypeDesc::Host(TypeNames[0]);
		const auto NativeContext = FNativeTypeDesc::Host(TypeNames[6]);
		const auto NativeApplication = FNativeTypeDesc::Host(TypeNames[7]);
		if (!Member(Registry, Bridge->Types[0], "GetMountGeneration", EHostMemberKind::Method, Int64, {},
		        [](const auto &Call, auto &Out, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			Out = static_cast<std::int64_t>(Widget->GetMountGeneration());
			return true;
		}, Error, true))
		{
			return false;
		}
		if (!Member(Registry, Bridge->Types[0], "GetContext", EHostMemberKind::Method, NativeContext, {},
		        [Bridge](const auto &Call, auto &Out, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			auto Context = Widget->GetContext();
			Out = Context ? Make(Bridge, 6, FContextView{std::move(Context), {}}) : FValue(FHostObjectHandle{});
			return true;
		}, Error))
		{
			return false;
		}
		for (const bool Input : {false, true})
		{
			if (!Member(Registry, Bridge->Types[0], Input ? "HasActiveInput" : "HasLayout", EHostMemberKind::Method,
			        Bool, {}, [Input](const auto &Call, auto &Out, auto &Failure)
			{
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				if (!Widget)
				{
					Failure = "gui.widget_expired";
					return false;
				}
				Out = Input ? Widget->HasActiveInput() : Widget->HasLayout();
				return true;
			}, Error, true))
			{
				return false;
			}
		}
		if (!Member(
		        Registry, Bridge->Types[6], "LoadFont", EHostMemberKind::Method, Void,
		        {String, String, FNativeTypeDesc::Scalar(ENativeType::Int), Bool, Bool,
		            FNativeTypeDesc::Scalar(ENativeType::Int)},
		        [](const auto &Call, auto &, auto &Failure)
		{
			FFontFace Font;
			Font.Source = std::get<std::string>(Call.Args[1]);
			Font.Family = std::get<std::string>(Call.Args[2]);
			Font.Weight = std::get<std::int32_t>(Call.Args[3]);
			Font.bItalic = std::get<bool>(Call.Args[4]);
			Font.bFallback = std::get<bool>(Call.Args[5]);
			Font.FaceIndex = std::get<std::int32_t>(Call.Args[6]);
			return static_cast<const FContextView *>(Call.HostThis)->Context->LoadFont(Font, Failure);
		},
		        Error))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[6], "SetLocalization", EHostMemberKind::Method, Void,
		        {FNativeTypeDesc::Function("fn()->std.i18n.context")},
		        [](const auto &Call, auto &, auto &Error)
		{
			if (!Call.ScriptInvoker)
			{
				Error = "gui.script_invoker_missing";
				return false;
			}
			std::vector<FValue> Arguments;
			FValue Supplied;
			if (!Call.ScriptInvoker->InvokeCallable(std::get<FFunctionHandle>(Call.Args[1]), Arguments,
			        Supplied, Error))
			{
				return false;
			}
			std::shared_ptr<I18n::FI18nContext> Value;
			I18n::FI18nError Failure;
			if (!I18n::FromScriptValue(Supplied, Value, Failure))
			{
				Error = Failure.Diagnostic.Message;
				return false;
			}
			return SetLocalization(static_cast<const FContextView *>(Call.HostThis)->Context, std::move(Value),
			    Error);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[6], "ReadLocalizationSnapshot", EHostMemberKind::Method, Void,
		        {FNativeTypeDesc::Function("fn(std.i18n.snapshot)->void")},
		        [](const auto &Call, auto &, auto &Error)
		{
			if (!Call.ScriptInvoker)
			{
				Error = "gui.script_invoker_missing";
				return false;
			}
			auto Snapshot =
			    GetLocalizationSnapshot(static_cast<const FContextView *>(Call.HostThis)->Context, Error);
			if (!Snapshot.IsValid())
			{
				return false;
			}
			I18n::FI18nError Failure;
			FValue Value;
			if (!I18n::ToScriptValue(Snapshot, Value, Failure))
			{
				Error = Failure.Diagnostic.Message;
				return false;
			}
			std::vector<FValue> Arguments{std::move(Value)};
			FValue Ignored;
			return Call.ScriptInvoker->InvokeCallable(std::get<FFunctionHandle>(Call.Args[1]), Arguments,
			    Ignored, Error);
		},
		        Error, true))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[6], "SetTheme", EHostMemberKind::Method, Void, {String, String},
		        [Bridge](const auto &Call, auto &, auto &Error)
		{
			FMarkupDiagnostic Diagnostic;
			auto Sheet = FStyleSheet::CompileTyped(
			    {std::get<std::string>(Call.Args[1]), std::get<std::string>(Call.Args[2])}, Diagnostic,
			    Bridge->Services.ResolveResourceReference);
			if (!Sheet)
			{
				Error = Diagnostic.Format();
				return false;
			}
			return static_cast<const FContextView *>(Call.HostThis)->Context->SetTheme(std::move(Sheet), Error);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[6], "ClearTheme", EHostMemberKind::Method, Void, {},
		        [](const auto &Call, auto &, auto &Error)
		{
			return static_cast<const FContextView *>(Call.HostThis)->Context->SetTheme({}, Error);
		}, Error))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[6], "__NativeContext", EHostMemberKind::Constructor, NativeContext,
		        {String, Int64, Int64, Double},
		        [Bridge](const auto &Call, auto &Out, auto &Error)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			auto BackendName = std::get<std::string>(Call.Args[0]);
			if (BackendName.empty())
			{
				BackendName = Bridge->Services.DefaultBackend ? Bridge->Services.DefaultBackend("gui") : "rmlui";
			}
			FViewport Viewport{std::get<std::int64_t>(Call.Args[1]), std::get<std::int64_t>(Call.Args[2]),
			    std::get<double>(Call.Args[3])};
			if (!ValidateViewport(Viewport, Error))
			{
				return false;
			}
			std::shared_ptr<FContext> Context;
			if (Bridge->Services.CreateContext)
			{
				Context = Bridge->Services.CreateContext(BackendName, Viewport, Error);
			}
			else
			{
				auto Factories = Bridge->Services.Application;
				if (Bridge->Services.ResolveApplicationFactories &&
				    !Bridge->Services.ResolveApplicationFactories(Factories, Error))
				{
					return false;
				}
				if (!Factories.Gui)
				{
					Error = "gui.context_factory_unavailable: " + BackendName;
					return false;
				}
				if (std::get<std::string>(Call.Args[0]).empty() &&
				    (!Bridge->Services.DefaultBackend || BackendName.empty()) && Factories.DefaultBackend)
				{
					BackendName = Factories.DefaultBackend("gui");
				}
				if (BackendName.empty())
				{
					BackendName = "rmlui";
				}
				FBackendOptions Options;
				const auto Resolve = [&](std::string_view Role, const char *Default, const auto &Factory, auto &Service)
				{
					auto Explicit = Bridge->Services.DefaultBackend ? Bridge->Services.DefaultBackend(Role) : std::string{};
					if (Explicit.empty() && Factories.DefaultBackend)
					{
						Explicit = Factories.DefaultBackend(Role);
					}
					const auto Name = Explicit.empty() ? std::string(Default) : Explicit;
					if (Name == DisabledBackend)
					{
						return true;
					}
					if (Factory)
					{
						Service = Factory(Name, Error);
						if (Service) return true;
					}
					else if (Explicit.empty() || Name == AutomaticBackend)
					{
						return true;
					}
					if (Error.empty())
					{
						Error = "gui.provider_unavailable: " + std::string(Role) + '/' + Name;
					}
					return false;
				};
				if (!Resolve("image", "stb", Factories.Image, Options.Images) ||
				    !Resolve("font", Font::DefaultBackendId, Factories.Font, Options.Fonts) ||
				    !Resolve("text-layout", Font::DefaultBackendId, Factories.TextLayout, Options.TextLayouts))
				{
					return false;
				}
				auto Backend = Factories.Gui(BackendName, Options, Error);
				if (Backend)
				{
					Context = FContext::Create(std::move(Backend), Viewport, Error);
				}
			}
			if (!Context)
			{
				if (Error.empty())
				{
					Error = "gui.context_factory_unavailable: " + BackendName;
				}
				return false;
			}
			if (Bridge->Services.ReadResource &&
			    !Context->SetResourceReader(Bridge->Services.ReadResource, Error))
			{
				return false;
			}
			if (!AttachBindingUpdates(Bridge, Context, Error)) return false;
			Out = Make(Bridge, 6, FContextView{std::move(Context), {}});
			return true;
		},
		        Error))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[6], "SetRoot", EHostMemberKind::Method, Void, {NativeWidget},
		        [Bridge](const auto &Call, auto &, auto &Error)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			const auto &Context = static_cast<const FContextView *>(Call.HostThis)->Context;
			const auto &Handle = std::get<FHostObjectHandle>(Call.Args[1]);
			return Context->SetRoot(Handle ? Payload<FWidgetView>(Call.Args[1]).lock() : nullptr, Error);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[6], "GetRoot", EHostMemberKind::Method, NativeWidget, {},
		        [Bridge](const auto &Call, auto &Out, auto &)
		{
			const auto Root = static_cast<const FContextView *>(Call.HostThis)->Context->GetRoot();
			Out = Root ? Make(Bridge, 0, std::weak_ptr<FWidget>(Root)) : FValue(FHostObjectHandle{});
			return true;
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[6], "Update", EHostMemberKind::Method, Void, {Double},
		        [Bridge](const auto &Call, auto &, auto &Error)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			const auto &Context = static_cast<const FContextView *>(Call.HostThis)->Context;
			return AttachBindingUpdates(Bridge, Context, Error) &&
			    Context->Update(std::get<double>(Call.Args[1]), Error);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[6], "SetViewport", EHostMemberKind::Method, Void, {Int64, Int64, Double},
		        [](const auto &Call, auto &, auto &Error)
		{
			return static_cast<const FContextView *>(Call.HostThis)
			    ->Context->SetViewport({std::get<std::int64_t>(Call.Args[1]),
			                               std::get<std::int64_t>(Call.Args[2]), std::get<double>(Call.Args[3])},
			        Error);
		},
		        Error))
		{
			return false;
		}
		for (const auto *Name : {"Render", "Close", "IsOpen"})
		{
			if (!Member(
			        Registry, Bridge->Types[6], Name, EHostMemberKind::Method, Bool, {},
			        [Bridge, Name = std::string(Name)](const auto &Call, auto &Out, auto &Error)
			{
				FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
				const auto &Context = static_cast<const FContextView *>(Call.HostThis)->Context;
				bool Value = false;
				if (Name == "Render")
				{
					if (!Context->Render(Value, Error))
					{
						return false;
					}
				}
				else if (Name == "Close")
				{
					if (!Context->Close(Error))
					{
						return false;
					}
					Value = true;
				}
				else
				{
					Value = Context->IsOpen();
				}
				Out = Value;
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		if (!Member(
		        Registry, Bridge->Types[6], "ProcessEvent", EHostMemberKind::Method, Bool,
		        {FNativeTypeDesc::Host(TypeNames[5])},
		        [Bridge](const auto &Call, auto &Out, auto &Error)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			FEventResult Result;
			const auto &Snapshot = Payload<std::shared_ptr<FScriptEvent>>(Call.Args[1]);
			if (!static_cast<const FContextView *>(Call.HostThis)
			        ->Context->ProcessEvent(Snapshot->Input, Result, Error))
			{
				return false;
			}
			Out = Result.bHandled;
			return true;
		},
		        Error))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[6], "SetFocus", EHostMemberKind::Method, Void, {NativeWidget},
		        [Bridge](const auto &Call, auto &, auto &Error)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			const auto &Handle = std::get<FHostObjectHandle>(Call.Args[1]);
			return static_cast<const FContextView *>(Call.HostThis)
			    ->Context->SetFocus(Handle ? Payload<FWidgetView>(Call.Args[1]).lock() : nullptr, Error);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[6], "GetFocus", EHostMemberKind::Method, NativeWidget, {},
		        [Bridge](const auto &Call, auto &Out, auto &)
		{
			const auto Value = static_cast<const FContextView *>(Call.HostThis)->Context->GetFocus();
			Out = Value ? Make(Bridge, 0, std::weak_ptr<FWidget>(Value)) : FValue(FHostObjectHandle{});
			return true;
		},
		        Error))
		{
			return false;
		}
		for (const bool Capture : {false, true})
		{
			std::vector<FNativeTypeDesc> Parameters{FNativeTypeDesc::Host(TypeNames[5])};
			if (Capture)
			{
				Parameters.push_back(NativeWidget);
			}
			if (!Member(
			        Registry, Bridge->Types[6], Capture ? "CapturePointer" : "ReleasePointer", EHostMemberKind::Method,
			        Void, std::move(Parameters),
			        [Capture](const auto &Call, auto &, auto &Error)
			{
				const auto &Context = static_cast<const FContextView *>(Call.HostThis)->Context;
				const auto &Input = Payload<std::shared_ptr<FScriptEvent>>(Call.Args[1])->Input;
				const auto Pointer = FPointerIdentity::FromEvent(Input);
				return Capture
				    ? Context->CapturePointer(Pointer, Payload<FWidgetView>(Call.Args[2]).lock(), Error)
				    : Context->ReleasePointer(Pointer, Error);
			},
			        Error))
			{
				return false;
			}
		}
		if (!Member(
		        Registry, Bridge->Types[6], "Post", EHostMemberKind::Method, Void,
		        {FNativeTypeDesc::Function("fn()->void")},
		        [Bridge](const auto &Call, auto &, auto &Error)
		{
			if (!Call.ScriptInvoker)
			{
				Error = "gui.script_invoker_missing";
				return false;
			}
			const std::weak_ptr<const void> Program = Call.ScriptInvoker->GetProgramOwner();
			const auto Generation = Call.ScriptInvoker->GetProgramGeneration();
			FValueRoot Callback;
			if (!Call.ScriptInvoker->RetainForGeneration(Call.Args[1], Callback, Error))
			{
				return false;
			}
			return static_cast<const FContextView *>(Call.HostThis)
			    ->Context->Post(
			        [Owner = std::weak_ptr<FBridge>(Bridge), Program, Generation,
			            Callback = std::move(Callback)](auto &Failure)
			{
				const auto Bridge = Owner.lock();
				if (!Bridge)
				{
					Failure = "gui.script_context_closed";
					return false;
				}
				if (!CheckScriptProgram(*Bridge, Program, Generation, Failure))
				{
					return false;
				}
				const auto *Value = Callback.Read();
				if (!Value)
				{
					Failure = "gui.stale_script_callback";
					return false;
				}
				FValue Ignored;
				if (Bridge->Invoker)
				{
					std::vector<FValue> Args;
					return Bridge->Invoker->InvokeCallable(std::get<FFunctionHandle>(*Value), Args,
					    Ignored, Failure);
				}
				if (Bridge->Services.Invoke)
				{
					std::vector<FValue> Args{*Value};
					return Bridge->Services.Invoke("VS.Gui.Internal.DispatchWork", Args, Ignored, Failure);
				}
				Failure = "gui.script_execution_bridge_unavailable";
				return false;
			},
			        Error);
		},
		        Error))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[7], "__NativeApplication", EHostMemberKind::Constructor, NativeApplication,
		        {String, Int64, Int64, String, String, String, Bool, Int64, String, String, String},
		        [Bridge](const auto &Call, auto &Out, auto &Error)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			FApplicationOptions Options;
			Options.Window.Title = std::get<std::string>(Call.Args[0]);
			Options.Window.Width = std::get<std::int64_t>(Call.Args[1]);
			Options.Window.Height = std::get<std::int64_t>(Call.Args[2]);
			const auto Select = [&](std::size_t Index, std::string_view Role, const std::string &Fallback, bool *Configured = nullptr)
			{
				const auto &Explicit = std::get<std::string>(Call.Args[Index]);
				if (!Explicit.empty())
				{
					if (Configured)
					{
						*Configured = Explicit != AutomaticBackend && Explicit != DisabledBackend;
					}
					return Explicit;
				}
				const auto Default =
				    Bridge->Services.DefaultBackend ? Bridge->Services.DefaultBackend(Role) : std::string{};
				if (Configured)
				{
					*Configured = !Default.empty() && Default != AutomaticBackend && Default != DisabledBackend;
				}
				return Default.empty() ? Fallback : Default;
			};
			Options.PlatformBackend = Select(3, "platform", Options.PlatformBackend);
			Options.GraphicsBackend = Select(4, "graphics", Options.GraphicsBackend);
			Options.GuiBackend = Select(5, "gui", Options.GuiBackend);
			Options.ImageBackend = Select(8, "image", Options.ImageBackend);
			bool FontConfigured = false, LayoutConfigured = false;
			Options.FontBackend = Select(9, "font", Options.FontBackend, &FontConfigured);
			Options.TextLayoutBackend = Select(10, "text-layout", Options.TextLayoutBackend, &LayoutConfigured);
			auto Factories = Bridge->Services.Application;
			if (Bridge->Services.ResolveApplicationFactories &&
			    !Bridge->Services.ResolveApplicationFactories(Factories, Error))
			{
				return false;
			}
			if ((!Factories.Font && FontConfigured) || (!Factories.TextLayout && LayoutConfigured))
			{
				Error = !Factories.Font && FontConfigured
				    ? "gui.provider_unavailable: font/" + Options.FontBackend
				    : "gui.provider_unavailable: text-layout/" + Options.TextLayoutBackend;
				return false;
			}
			Options.bRendering = std::get<bool>(Call.Args[6]);
			Options.WaitMilliseconds = std::get<std::int64_t>(Call.Args[7]);
			auto Application = FApplication::Create(Options, Factories, Error);
			if (!Application)
			{
				return false;
			}
			if (Bridge->Services.ReadResource &&
			    !Application->GetContext()->SetResourceReader(Bridge->Services.ReadResource, Error))
			{
				return false;
			}
			Out = Make(Bridge, 7, std::move(Application));
			return true;
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[7], "GetContext", EHostMemberKind::Method, NativeContext, {},
		        [Bridge](const auto &Call, auto &Out, auto &Error)
		{
			const auto &Application = *static_cast<const std::shared_ptr<FApplication> *>(Call.HostThis);
			auto Context = Application->GetContext();
			if (!Context)
			{
				Error = "gui.application_closed";
				return false;
			}
			if (!AttachBindingUpdates(Bridge, Context, Error)) return false;
			Out = Make(Bridge, 6, FContextView{std::move(Context), Application});
			return true;
		},
		        Error))
		{
			return false;
		}
		for (const auto *Name : {"Tick", "Run"})
		{
			if (!Member(
			        Registry, Bridge->Types[7], Name, EHostMemberKind::Method, Void,
			        {FNativeTypeDesc::Function("fn()->void")},
			        [Bridge, bRun = std::string(Name) == "Run"](const auto &Call, auto &, auto &Error)
			{
				FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
				if (!Call.ScriptInvoker)
				{
					Error = "gui.script_invoker_missing";
					return false;
				}
				const auto &Application = *static_cast<const std::shared_ptr<FApplication> *>(Call.HostThis);
				const auto &Callback = std::get<FFunctionHandle>(Call.Args[1]);
				const FApplication::FBeforeUpdate Synchronize = [&](std::string &Failure)
				{
					std::vector<FValue> Arguments;
					FValue Ignored;
					return Call.ScriptInvoker->InvokeCallable(Callback, Arguments, Ignored, Failure);
				};
				return bRun ? Application->Run(Error, Synchronize) : Application->Tick(Error, Synchronize);
			},
			        Error))
			{
				return false;
			}
		}
		for (const auto *Name : {"RequestStop", "Close", "IsOpen"})
		{
			if (!Member(
			        Registry, Bridge->Types[7], Name, EHostMemberKind::Method, Bool, {},
			        [Bridge, Name = std::string(Name)](const auto &Call, auto &Out, auto &Error)
			{
				FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
				const auto &Application = *static_cast<const std::shared_ptr<FApplication> *>(Call.HostThis);
				if (Name == "Close" && !Application->Close(Error))
				{
					return false;
				}
				if (Name == "RequestStop")
				{
					Application->RequestStop();
				}
				Out = Name == "IsOpen" ? Application->IsOpen() : true;
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::Gui::ScriptBindings

namespace VeroScript::Gui
{
	std::shared_ptr<FContext> GetScriptContext(const FValue &Handle, std::string &Error)
	{
		Error.clear();
		try
		{
			return ScriptBindings::Payload<ScriptBindings::FContextView>(Handle).Context;
		}
		catch (const std::exception &Exception)
		{
			Error = Exception.what();
			return {};
		}
	}

	FValue MakeScriptContext(const IHostBindingCatalog &Catalog, std::shared_ptr<FContext> Context, std::string &Error)
	{
		Error.clear();
		const auto *Type = Catalog.FindType(ScriptBindings::TypeNames[6]);
		if (!Type || !Context)
		{
			Error = "gui.invalid_context_or_catalog";
			return {};
		}
		auto Bridge = std::make_shared<ScriptBindings::FBridge>();
		Bridge->Types[6] = Type->Id;
		return ScriptBindings::Make(Bridge, 6, ScriptBindings::FContextView{std::move(Context), {}});
	}

	FValue MakeScriptInput(const IHostBindingCatalog &Catalog, const Platform::FEvent &Input, std::string &Error)
	{
		Error.clear();
		const auto *Type = Catalog.FindType(ScriptBindings::TypeNames[5]);
		if (!Type)
		{
			Error = "gui.invalid_script_catalog";
			return {};
		}
		auto Bridge = std::make_shared<ScriptBindings::FBridge>();
		Bridge->Types[5] = Type->Id;
		auto Event = std::make_shared<ScriptBindings::FScriptEvent>();
		Event->Input = Input;
		return ScriptBindings::Make(Bridge, 5, std::move(Event));
	}
} // namespace VeroScript::Gui

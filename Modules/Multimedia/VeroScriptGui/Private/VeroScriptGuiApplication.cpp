// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiApplication.h"
#include <exception>
#include <mutex>
#include <set>
#include <vector>

namespace VeroScript::Gui
{
	namespace
	{
		struct FFlag
		{
			explicit FFlag(bool &InValue) : Value(InValue)
			{
				Value = true;
			}
			~FFlag()
			{
				Value = false;
			}
			bool &Value;
		};

		struct FRetiredApplications
		{
			std::mutex Mutex;
			std::vector<std::unique_ptr<FApplication>> Items;
			std::set<Platform::IService *> Pumps;
		};

		FRetiredApplications &Retired()
		{
			static auto *Queue = new FRetiredApplications;
			return *Queue;
		}
	} // namespace

	FApplication::~FApplication()
	{
		std::string Ignored;
		CloseOwned(Ignored);
	}

	std::shared_ptr<FApplication> FApplication::Create(const FApplicationOptions &Input,
	    const FApplicationFactories &Factories, std::string &Error)
	{
		Error.clear();
		auto Options = Input;
		const auto Inherit = [&](const char *Role, std::string &Backend, const char *HostDefault)
		{
			if (!Backend.empty()) return true;
			const auto Selected = Factories.DefaultBackend ? Factories.DefaultBackend(Role) : std::string{};
			Backend = Selected.empty() ? std::string(HostDefault) : Selected;
			return !Selected.empty();
		};
		Inherit("platform", Options.PlatformBackend, "sdl3");
		Inherit("graphics", Options.GraphicsBackend, "sdl3");
		Inherit("gui", Options.GuiBackend, "rmlui");
		const bool bImageConfigured = Inherit("image", Options.ImageBackend, "stb");
		const bool bFontConfigured = Inherit("font", Options.FontBackend, Font::DefaultBackendId);
		const bool bTextLayoutConfigured = Inherit("text-layout", Options.TextLayoutBackend, Font::DefaultBackendId);
		if (Options.PlatformBackend == "none" || Options.GuiBackend == "none" || (Options.bRendering && Options.GraphicsBackend == "none"))
		{
			const auto *Service = Options.PlatformBackend == "none" ? "platform"
			    : Options.GuiBackend == "none"                      ? "gui"
			                                                        : "graphics";
			Error = "gui.provider_unavailable: " + std::string(Service) + "/none";
			return {};
		}
		const auto MissingResource = [](const bool HasFactory, const bool Configured, const std::string &Selected)
		{
			return !HasFactory && Configured && Selected != "auto" && Selected != "none";
		};
		const char *MissingRole = nullptr;
		const std::string *MissingBackend = nullptr;
		if (MissingResource(bool(Factories.Image), bImageConfigured, Options.ImageBackend))
		{
			MissingRole = "image";
			MissingBackend = &Options.ImageBackend;
		}
		else if (MissingResource(bool(Factories.Font), bFontConfigured, Options.FontBackend))
		{
			MissingRole = "font";
			MissingBackend = &Options.FontBackend;
		}
		else if (MissingResource(bool(Factories.TextLayout), bTextLayoutConfigured, Options.TextLayoutBackend))
		{
			MissingRole = "text-layout";
			MissingBackend = &Options.TextLayoutBackend;
		}
		if (MissingBackend)
		{
			Error = "gui.provider_unavailable: " + std::string(MissingRole) + '/' + *MissingBackend;
			return {};
		}
		if (!Platform::ValidateWindowOptions(Options.Window, Error) ||
		    !Platform::ValidateWait(Options.WaitMilliseconds, Error))
		{
			return {};
		}
		if (!Factories.Platform || !Factories.Gui || (Options.bRendering && !Factories.Graphics) ||
		    Options.PlatformBackend.empty() || Options.GuiBackend.empty() ||
		    (Options.bRendering && Options.GraphicsBackend.empty()) || !Options.MaxEventsPerFrame ||
		    Options.MaxEventsPerFrame > 65536)
		{
			Error = "gui.invalid_application_options";
			return {};
		}
		auto Result = std::shared_ptr<FApplication>(new FApplication(Options),
		    [](FApplication *Value)
		{
			std::unique_ptr<FApplication> Owned(Value);
			std::string Ignored;
			if (Value->Thread == std::this_thread::get_id() &&
			    Value->CloseOwned(Ignored))
			{
				return;
			}
			auto &Queue = Retired();
			std::lock_guard<std::mutex> Guard(Queue.Mutex);
			Queue.Items.push_back(std::move(Owned));
		});
		const auto Fail = [&]() -> std::shared_ptr<FApplication>
		{
			if (Error.empty())
			{
				Error = "gui.application_creation_failed";
			}
			return {};
		};
		Result->PlatformService = Factories.Platform(Options.PlatformBackend, Error);
		if (!Result->PlatformService)
		{
			return Fail();
		}
		if (!Result->PlatformService->GetCapabilities().bOwnsEventPump)
		{
			Error = "gui.application_requires_own_event_pump";
			return Fail();
		}
		{
			auto &Queue = Retired();
			std::lock_guard<std::mutex> Guard(Queue.Mutex);
			if (!Queue.Pumps.insert(Result->PlatformService.get()).second)
			{
				Error = "gui.event_pump_already_owned";
				return Fail();
			}
			Result->bOwnsPumpLease = true;
		}
		Result->Window = Result->PlatformService->CreateWindow(Options.Window, Error);
		if (!Result->Window)
		{
			return Fail();
		}
		if (Options.bRendering)
		{
			Result->GraphicsService = Factories.Graphics(Options.GraphicsBackend, Error);
			if (!Result->GraphicsService)
			{
				return Fail();
			}
			Result->Device = Result->GraphicsService->CreateDevice(Options.Device, Error);
			if (!Result->Device)
			{
				return Fail();
			}
			Result->Surface = Result->Device->CreateSurface(Result->Window, Error);
			if (!Result->Surface)
			{
				return Fail();
			}
		}
		FBackendOptions BackendOptions;
		BackendOptions.Device = Result->Device;
		BackendOptions.Surface = Result->Surface;
		if (Factories.Image && Options.ImageBackend != "none")
		{
			BackendOptions.Images = Factories.Image(Options.ImageBackend, Error);
			if (!BackendOptions.Images && (!Error.empty() || (bImageConfigured && Options.ImageBackend != "auto")))
			{
				if (Error.empty())
				{
					Error = "gui.provider_unavailable: image/" + Options.ImageBackend;
				}
				return Fail();
			}
		}
		if (Factories.Font && Options.FontBackend != "none")
		{
			BackendOptions.Fonts = Factories.Font(Options.FontBackend, Error);
			if (!BackendOptions.Fonts)
			{
				if (Error.empty())
				{
					Error = "gui.provider_unavailable: font/" + Options.FontBackend;
				}
				return Fail();
			}
		}
		else if (Options.FontBackend != Font::DefaultBackendId && Options.FontBackend != "auto" && Options.FontBackend != "none")
		{
			Error = "gui.provider_unavailable: font/" + Options.FontBackend;
			return Fail();
		}
		if (Factories.TextLayout && Options.TextLayoutBackend != "none")
		{
			BackendOptions.TextLayouts = Factories.TextLayout(Options.TextLayoutBackend, Error);
			if (!BackendOptions.TextLayouts)
			{
				if (Error.empty())
				{
					Error = "gui.provider_unavailable: text-layout/" + Options.TextLayoutBackend;
				}
				return Fail();
			}
		}
		else if (Options.TextLayoutBackend != Font::DefaultBackendId && Options.TextLayoutBackend != "auto" && Options.TextLayoutBackend != "none")
		{
			Error = "gui.provider_unavailable: text-layout/" + Options.TextLayoutBackend;
			return Fail();
		}
		auto Backend = Factories.Gui(Options.GuiBackend, BackendOptions, Error);
		if (!Backend || (Options.bRendering && !Backend->GetCapabilities().bRendering))
		{
			return Fail();
		}
		// A minimized initial window keeps a valid provisional viewport until it has drawable metrics.
		const FViewport Viewport{Options.Window.Width, Options.Window.Height, 1};
		Result->Context = FContext::Create(std::move(Backend), Viewport, Error);
		if (!Result->Context || !Result->RefreshMetrics(Error))
		{
			return Fail();
		}
		FTextInputServices TextServices;
		if (Result->PlatformService->GetCapabilities().bClipboard)
		{
			const auto Weak = std::weak_ptr<Platform::IService>(Result->PlatformService);
			TextServices.ReadClipboard = [Weak](std::string &Text, std::string &Failure)
			{
				const auto Service = Weak.lock();
				if (!Service)
				{
					Failure = "gui.platform_expired";
					return false;
				}
				return Service->GetClipboardText(Text, Failure);
			};
			TextServices.WriteClipboard = [Weak](const std::string &Text, std::string &Failure)
			{
				const auto Service = Weak.lock();
				if (!Service)
				{
					Failure = "gui.platform_expired";
					return false;
				}
				return Service->SetClipboardText(Text, Failure);
			};
		}
		if (Result->PlatformService->GetCapabilities().bTextInput)
		{
			const auto Weak = std::weak_ptr<Platform::IWindow>(Result->Window);
			TextServices.SetInputArea =
			    [Weak](bool Enabled, const FRect &Rect, std::int64_t Cursor, std::string &Failure)
			{
				const auto Window = Weak.lock();
				if (!Window || !Window->IsOpen())
				{
					if (!Enabled)
					{
						Failure.clear();
						return true;
					}
					Failure = "gui.window_expired";
					return false;
				}
				Platform::FWindowMetrics Metrics;
				if (!Window->SetTextInputEnabled(Enabled, Failure))
				{
					if (Failure.empty())
					{
						Failure = "gui.text_input_activation_failed";
					}
					return false;
				}
				if (!Enabled)
				{
					return true;
				}
				if (!Window->GetMetrics(Metrics, Failure))
				{
					return false;
				}
				const auto X = Metrics.DisplayScale / Metrics.ScaleX, Y = Metrics.DisplayScale / Metrics.ScaleY;
				return Window->SetTextInputArea(Rect.X * X, Rect.Y * Y, Rect.Width * X, Rect.Height * Y,
				    static_cast<std::int64_t>(Cursor * X), Failure);
			};
		}
		if (!Result->Context->SetTextInputServices(std::move(TextServices), Error))
		{
			return Fail();
		}
		FPointerServices PointerServices;
		const auto WeakWindow = std::weak_ptr<Platform::IWindow>(Result->Window);
		if (Result->PlatformService->GetCapabilities().bSystemCursor)
		{
			PointerServices.SetCursor = [WeakWindow](Platform::ECursor Shape, bool bVisible, std::string &Failure)
			{
				const auto Window = WeakWindow.lock();
				if (!Window || !Window->IsOpen())
				{
					if (Shape == Platform::ECursor::Default && bVisible)
					{
						Failure.clear();
						return true;
					}
					Failure = "gui.window_expired";
					return false;
				}
				return Window->SetCursor(Shape, bVisible, Failure);
			};
		}
		if (Result->PlatformService->GetCapabilities().bMouseCapture)
		{
			PointerServices.SetMouseCapture = [WeakWindow](bool bEnabled, std::string &Failure)
			{
				const auto Window = WeakWindow.lock();
				if (!Window || !Window->IsOpen())
				{
					if (!bEnabled)
					{
						Failure.clear();
						return true;
					}
					Failure = "gui.window_expired";
					return false;
				}
				return Window->SetMouseCapture(bEnabled, Failure);
			};
		}
		if (!Result->Context->SetPointerServices(std::move(PointerServices), Error))
		{
			return Fail();
		}
		return Result;
	}

	bool FApplication::CollectRetired(std::string &Error)
	{
		const bool bContextsCollected = FContext::CollectRetired(Error);
		std::vector<std::unique_ptr<FApplication>> Items;
		auto &Queue = Retired();
		{
			std::lock_guard<std::mutex> Guard(Queue.Mutex);
			for (auto Item = Queue.Items.begin(); Item != Queue.Items.end();)
			{
				if ((*Item)->Thread == std::this_thread::get_id())
				{
					Items.push_back(std::move(*Item));
					Item = Queue.Items.erase(Item);
				}
				else
				{
					++Item;
				}
			}
		}
		bool bSuccess = bContextsCollected;
		for (auto &Item : Items)
		{
			std::string Failure;
			if (!Item->CloseOwned(Failure))
			{
				if (Error.empty())
				{
					Error = std::move(Failure);
				}
				bSuccess = false;
				std::lock_guard<std::mutex> Guard(Queue.Mutex);
				Queue.Items.push_back(std::move(Item));
			}
		}
		return bSuccess;
	}

	bool FApplication::SetRoot(std::shared_ptr<FWidget> Root, std::string &Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		return Check(Error) && Context->SetRoot(std::move(Root), Error);
	}

	std::shared_ptr<FContext> FApplication::GetContext() const
	{
		return Thread == std::this_thread::get_id() ? Context : nullptr;
	}

	std::shared_ptr<Platform::IWindow> FApplication::GetWindow() const
	{
		return Thread == std::this_thread::get_id() ? Window : nullptr;
	}

	bool FApplication::Tick(std::string &Error, const FBeforeUpdate &BeforeUpdate)
	{
		if (!Check(Error))
		{
			return false;
		}
		if (bRunning)
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		return TickFrame(nullptr, Error, BeforeUpdate);
	}

	bool FApplication::Run(std::string &Error, const FBeforeUpdate &BeforeUpdate)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (bRunning || bInFrame)
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		if (!PlatformService->GetCapabilities().bOwnsEventPump)
		{
			Error = "gui.application_requires_own_event_pump";
			return false;
		}
		FFlag Guard(bRunning);
		using FClock = std::chrono::steady_clock;
		auto FrameStarted = FClock::now();
		while (IsOpen() && !bStopRequested.load(std::memory_order_acquire))
		{
			// Frame work already consumes the event-wait budget; do not add a full delay after rendering.
			const auto Spent =
			    std::chrono::duration_cast<std::chrono::milliseconds>(FClock::now() - FrameStarted).count();
			const auto Wait = Spent >= Options.WaitMilliseconds ? 0 : Options.WaitMilliseconds - Spent;
			Platform::FEvent Event;
			bool bAvailable = false;
			if (!PlatformService->WaitEvent(Wait, Event, bAvailable, Error))
			{
				return false;
			}
			FrameStarted = FClock::now();
			if (!TickFrame(bAvailable ? &Event : nullptr, Error, BeforeUpdate))
			{
				return false;
			}
		}
		return true;
	}

	void FApplication::RequestStop() noexcept
	{
		bStopRequested.store(true, std::memory_order_release);
	}

	bool FApplication::Close(std::string &Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (Thread != std::this_thread::get_id())
		{
			Error = "gui.wrong_thread";
			return false;
		}
		if (bInFrame)
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		return CloseOwned(Error);
	}

	bool FApplication::IsOpen() const noexcept
	{
		return Thread == std::this_thread::get_id() && Window && Window->IsOpen() && Context && Context->IsOpen();
	}

	FApplication::FApplication(FApplicationOptions InOptions)
	    : Thread(std::this_thread::get_id()), Options(std::move(InOptions)), Started(std::chrono::steady_clock::now())
	{
		bSuspended = !Options.Window.bVisible;
	}

	bool FApplication::Check(std::string &Error) const
	{
		Error.clear();
		if (Thread != std::this_thread::get_id())
		{
			Error = "gui.wrong_thread";
		}
		else if (!IsOpen())
		{
			Error = "gui.application_closed";
		}
		return Error.empty();
	}

	bool FApplication::RefreshMetrics(std::string &Error)
	{
		Platform::FWindowMetrics Metrics;
		if (!Window->GetMetrics(Metrics, Error))
		{
			return false;
		}
		bDrawable =
		    !bSuspended && Metrics.Width > 0 && Metrics.Height > 0 && Metrics.PixelWidth > 0 && Metrics.PixelHeight > 0;
		return !bDrawable || Context->SetWindowMetrics(Window->GetId(), Metrics, Error);
	}

	bool FApplication::Route(const Platform::FEvent &Event, std::string &Error)
	{
		if (Event.WindowId && Event.WindowId != Window->GetId())
		{
			return true;
		}
		using Platform::EEventType;
		if (Event.Type == EEventType::Hidden || Event.Type == EEventType::Minimized ||
		    Event.Type == EEventType::Background)
		{
			bSuspended = true;
			bDrawable = false;
		}
		else if (Event.Type == EEventType::Shown || Event.Type == EEventType::Restored ||
		    Event.Type == EEventType::Foreground)
		{
			bSuspended = false;
		}
		const bool bInput = Event.Type == EEventType::PointerMove || Event.Type == EEventType::PointerDown ||
		    Event.Type == EEventType::PointerUp || Event.Type == EEventType::PointerCancelled ||
		    Event.Type == EEventType::Wheel || Event.Type == EEventType::KeyDown ||
		    Event.Type == EEventType::KeyUp || Event.Type == EEventType::TextInput ||
		    Event.Type == EEventType::TextEditing || Event.Type == EEventType::MouseEntered ||
		    Event.Type == EEventType::MouseLeft;
		if (bInput && !bDrawable)
		{
			return true;
		}
		FEventResult Result;
		if (!Context->ProcessEvent(Event, Result, Error))
		{
			return false;
		}
		if (!Result.bDefaultPrevented &&
		    (Event.Type == Platform::EEventType::CloseRequested || Event.Type == Platform::EEventType::Quit))
		{
			return CloseOwned(Error);
		}
		return true;
	}

	bool FApplication::TickFrame(const Platform::FEvent *First, std::string &Error, const FBeforeUpdate &BeforeUpdate)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error) || bInFrame)
		{
			if (Error.empty())
			{
				Error = "gui.reentrant_operation";
			}
			return false;
		}
		FFlag Guard(bInFrame);
		if (!RefreshMetrics(Error))
		{
			return false;
		}
		if (const auto Root = Context->GetRoot(); Root && bDrawable)
		{
			FLayoutBox Layout;
			std::string Unused;
			if (!Root->GetLayout(Layout, Unused) &&
			    ((BeforeUpdate && !BeforeUpdate(Error)) || !Context->Update(Elapsed(), Error)))
			{
				return false;
			}
		}
		if (First && !Route(*First, Error))
		{
			return false;
		}
		for (std::size_t Index = First ? 1 : 0; IsOpen() && Index < Options.MaxEventsPerFrame; ++Index)
		{
			Platform::FEvent Event;
			bool bAvailable = false;
			if (!PlatformService->PollEvent(Event, bAvailable, Error))
			{
				return false;
			}
			if (!bAvailable)
			{
				break;
			}
			if (!Route(Event, Error))
			{
				return false;
			}
		}
		if (!IsOpen())
		{
			return true;
		}
		if (!RefreshMetrics(Error) || !PlatformService->CollectRetired(Error))
		{
			return false;
		}
		if (!bDrawable)
		{
			return true;
		}
		if ((BeforeUpdate && !BeforeUpdate(Error)) || !Context->Update(Elapsed(), Error))
		{
			return false;
		}
		bool bPresented = false;
		return !Options.bRendering || Context->Render(bPresented, Error);
	}

	bool FApplication::CloseOwned(std::string &Error)
	try
	{
		Error.clear();
		if (Thread != std::this_thread::get_id())
		{
			Error = "gui.wrong_thread";
			return false;
		}
		if (Context)
		{
			if (!Context->Close(Error))
			{
				return false;
			}
			Context.reset();
		}
		if (Device && !Device->WaitIdle(Error))
		{
			return false;
		}
		if (Surface)
		{
			if (!Surface->Close(Error))
			{
				return false;
			}
			Surface.reset();
		}
		if (Device)
		{
			if (!Device->Close(Error))
			{
				return false;
			}
			Device.reset();
		}
		if (Window)
		{
			if (!Window->Close(Error))
			{
				return false;
			}
			Window.reset();
		}
		GraphicsService.reset();
		if (PlatformService && !PlatformService->CollectRetired(Error))
		{
			return false;
		}
		if (bOwnsPumpLease)
		{
			auto &Queue = Retired();
			std::lock_guard<std::mutex> Guard(Queue.Mutex);
			Queue.Pumps.erase(PlatformService.get());
			bOwnsPumpLease = false;
		}
		PlatformService.reset();
		return true;
	}
	catch (const std::exception &Exception)
	{
		Error = std::string("gui.application_close_exception: ") + Exception.what();
		return false;
	}
	catch (...)
	{
		Error = "gui.application_close_exception";
		return false;
	}

	double FApplication::Elapsed() const
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count();
	}
} // namespace VeroScript::Gui

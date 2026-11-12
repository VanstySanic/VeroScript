// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptGuiApplication.h"
#include <deque>
#include <iostream>
#include <stdexcept>
#include <thread>
#if VEROSCRIPT_GUI_SCRIPTING_TESTS
#include "VeroScriptGuiScript.h"
#include "VeroScriptGuiServices.h"
#include "VeroScriptGuiTools.h"
#include "VeroScriptSession.h"
#endif

namespace
{
	using namespace VeroScript;
	using namespace Gui;

	void Require(bool Value, const std::string &Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	struct FState
	{
		Platform::FWindowMetrics Metrics{100, 100, 100, 100, 1, 1, 1};
		std::deque<Platform::FEvent> Events;
		std::vector<std::int64_t> Waits;
		std::vector<std::string> Closed;
		std::size_t Frames = 0, Rendered = 0;
		bool bWindow = false, bBackend = false, bSurface = false, bDevice = false, bFailBackend = false;
		Platform::ECursor Cursor = Platform::ECursor::Default;
		bool bPointerSupport = false, bCursorVisible = true, bMouseCaptured = false;
		std::string ThrowOnCleanup;
		bool bUnknownException = false;
		void CheckCleanup(const std::string &Stage) const
		{
			if (ThrowOnCleanup == Stage)
			{
				if (bUnknownException)
				{
					throw 1;
				}
				throw std::runtime_error("test cleanup failure: " + Stage);
			}
		}
	};

	class FWindow final : public Platform::IWindow
	{
	public:
		explicit FWindow(std::shared_ptr<FState> InState) : State(std::move(InState))
		{
			State->bWindow = true;
		}
		std::uint64_t GetId() const noexcept override
		{
			return 1;
		}
		bool IsOpen() const noexcept override
		{
			return State->bWindow;
		}
		bool GetMetrics(Platform::FWindowMetrics &Result, std::string &) const override
		{
			Result = State->Metrics;
			return true;
		}
		bool SetTitle(const std::string &, std::string &) override
		{
			return true;
		}
		bool SetSize(std::int64_t, std::int64_t, std::string &) override
		{
			return true;
		}
		bool SetVisible(bool, std::string &) override
		{
			return true;
		}
		bool SetTextInputEnabled(bool, std::string &) override
		{
			return true;
		}
		bool SetTextInputArea(double, double, double, double, std::int64_t, std::string &) override
		{
			return true;
		}
		bool SetCursor(Platform::ECursor Shape, bool bVisible, std::string &Error) override
		{
			if (!State->bWindow)
			{
				Error = "test.window_closed";
				return false;
			}
			State->Cursor = Shape;
			State->bCursorVisible = bVisible;
			return true;
		}
		bool SetMouseCapture(bool bEnabled, std::string &Error) override
		{
			if (!State->bWindow)
			{
				Error = "test.window_closed";
				return false;
			}
			State->bMouseCaptured = bEnabled;
			return true;
		}
		bool Close(std::string &) override
		{
			Require(!State->bSurface && !State->bBackend, "window was closed before its GUI or surface");
			State->CheckCleanup("window");
			if (State->bWindow)
			{
				State->bWindow = false;
				State->Closed.push_back("window");
			}
			return true;
		}
		std::shared_ptr<FState> State;
	};

	class FPlatform final : public Platform::IService
	{
	public:
		explicit FPlatform(std::shared_ptr<FState> InState) : State(std::move(InState))
		{
		}
		Platform::FCapabilities GetCapabilities() const noexcept override
		{
			Platform::FCapabilities Value;
			Value.bWindows = true;
			Value.bOwnsEventPump = true;
			Value.bSystemCursor = State->bPointerSupport;
			Value.bMouseCapture = State->bPointerSupport;
			return Value;
		}
		std::shared_ptr<Platform::IWindow> CreateWindow(const Platform::FWindowOptions &, std::string &) override
		{
			return std::make_shared<FWindow>(State);
		}
		bool PollEvent(Platform::FEvent &Event, bool &bAvailable, std::string &) override
		{
			bAvailable = !State->Events.empty();
			if (bAvailable)
			{
				Event = std::move(State->Events.front());
				State->Events.pop_front();
			}
			return true;
		}
		bool WaitEvent(std::int64_t Milliseconds, Platform::FEvent &Event, bool &bAvailable, std::string &Error) override
		{
			State->Waits.push_back(Milliseconds);
			return PollEvent(Event, bAvailable, Error);
		}
		bool SubmitEvent(const Platform::FEvent &Event, std::string &) override
		{
			State->Events.push_back(Event);
			return true;
		}
		bool SetClipboardText(const std::string &, std::string &) override
		{
			return true;
		}
		bool GetClipboardText(std::string &, std::string &) override
		{
			return true;
		}
		bool CollectRetired(std::string &) override
		{
			State->CheckCleanup("platform");
			return true;
		}
		std::shared_ptr<FState> State;
	};

	class FSurface final : public Graphics::ISurface
	{
	public:
		explicit FSurface(std::shared_ptr<FState> InState) : State(std::move(InState))
		{
			State->bSurface = true;
		}
		bool IsOpen() const noexcept override
		{
			return State->bSurface;
		}
		std::uint64_t GetWindowId() const noexcept override
		{
			return 1;
		}
		bool Close(std::string &) override
		{
			Require(!State->bBackend, "surface was closed before its GUI renderer");
			State->CheckCleanup("surface");
			if (State->bSurface)
			{
				State->bSurface = false;
				State->Closed.push_back("surface");
			}
			return true;
		}
		std::shared_ptr<FState> State;
	};

	class FDevice final : public Graphics::IDevice
	{
	public:
		explicit FDevice(std::shared_ptr<FState> InState) : State(std::move(InState))
		{
			State->bDevice = true;
		}
		bool IsOpen() const noexcept override
		{
			return State->bDevice;
		}
		std::string GetDriver() const override
		{
			return "test";
		}
		Graphics::FCapabilities GetCapabilities() const noexcept override
		{
			return {};
		}
		std::shared_ptr<Graphics::ISurface> CreateSurface(std::shared_ptr<Platform::IWindow>, std::string &) override
		{
			return std::make_shared<FSurface>(State);
		}
		bool WaitIdle(std::string &) override
		{
			State->CheckCleanup("wait-idle");
			return true;
		}
		bool Close(std::string &) override
		{
			Require(!State->bSurface && !State->bBackend, "device closed before its resources");
			State->CheckCleanup("device");
			if (State->bDevice)
			{
				State->bDevice = false;
				State->Closed.push_back("device");
			}
			return true;
		}
		std::shared_ptr<Graphics::ITexture> CreateTexture(const Graphics::FTextureOptions &, std::string &) override
		{
			return {};
		}
		bool ClearTexture(const std::shared_ptr<Graphics::ITexture> &, const Graphics::FColor &, std::string &) override
		{
			return false;
		}
		bool Present(const std::shared_ptr<Graphics::ISurface> &, const Graphics::FColor &, bool &,
		    std::string &) override
		{
			return false;
		}
		bool ReadTexture(const std::shared_ptr<Graphics::ITexture> &, std::vector<std::uint8_t> &,
		    std::string &) override
		{
			return false;
		}
		std::shared_ptr<Graphics::ISampler> CreateSampler(const Graphics::FSamplerOptions &, std::string &) override
		{
			return {};
		}
		bool UpdateTexture(const std::shared_ptr<Graphics::ITexture> &, std::int64_t, std::int64_t, std::int64_t,
		    std::int64_t, const std::vector<std::uint8_t> &, std::string &) override
		{
			return false;
		}
		std::shared_ptr<Graphics::IRenderer2D> CreateRenderer2D(std::string &) override
		{
			return {};
		}
		std::shared_ptr<FState> State;
	};

	class FGraphics final : public Graphics::IService
	{
	public:
		explicit FGraphics(std::shared_ptr<FState> InState) : State(std::move(InState))
		{
		}
		std::shared_ptr<Graphics::IDevice> CreateDevice(const Graphics::FDeviceOptions &, std::string &) override
		{
			return std::make_shared<FDevice>(State);
		}
		std::shared_ptr<FState> State;
	};

	class FBackend final : public IBackendContext
	{
	public:
		explicit FBackend(std::shared_ptr<FState> InState) : State(std::move(InState))
		{
			State->bBackend = true;
		}
		FCapabilities GetCapabilities() const noexcept override
		{
			return {true, true};
		}
		bool Synchronize(const FViewport &, const std::vector<FBackendNode> &InNodes, std::string &) override
		{
			Nodes = InNodes;
			return true;
		}
		bool Update(double, std::vector<FLayoutBox> &Boxes, std::string &) override
		{
			++State->Frames;
			for (const auto &Node : Nodes)
			{
				Boxes.push_back({Node.Id, {0, 0, 100, 100}, {0, 0, 100, 100}});
			}
			return true;
		}
		bool Render(bool &bPresented, std::string &) override
		{
			++State->Rendered;
			bPresented = true;
			return true;
		}
		bool Close(std::string &) override
		{
			Require(State->bSurface && State->bDevice, "GUI backend lost its target before closing");
			if (State->bBackend)
			{
				State->bBackend = false;
				State->Closed.push_back("gui");
			}
			return true;
		}
		std::vector<FBackendNode> Nodes;
		std::shared_ptr<FState> State;
	};

	FApplicationFactories Factories(const std::shared_ptr<FState> &State, const std::shared_ptr<FPlatform> &Platform)
	{
		FApplicationFactories Value;
		Value.Platform = [Platform](const auto &Name, auto &)
		{
			Require(Name == "window-provider", "platform backend selection");
			return Platform;
		};
		Value.Graphics = [State](const auto &Name, auto &)
		{
			Require(Name == "drawing-provider", "graphics backend selection");
			return std::make_shared<FGraphics>(State);
		};
		Value.Gui = [State](const auto &Name, const FBackendOptions &Input,
		                auto &Error) -> std::unique_ptr<IBackendContext>
		{
			Require(Name == "layout-provider" && Input.Device && Input.Surface,
			    "GUI backend selection or target wiring");
			if (State->bFailBackend)
			{
				Error = "test backend failure";
				return {};
			}
			return std::make_unique<FBackend>(State);
		};
		return Value;
	}

	FApplicationOptions Options()
	{
		FApplicationOptions Value;
		Value.PlatformBackend = "window-provider";
		Value.GraphicsBackend = "drawing-provider";
		Value.GuiBackend = "layout-provider";
		return Value;
	}

	class FTestLayouts final : public Font::ITextLayoutService
	{
	public:
		const char *GetBackendId() const noexcept override
		{
			return "test-layout";
		}
		std::shared_ptr<const Font::ITextLayoutAnalysis> Analyze(std::string, Font::EDirection, std::string &Error) const override
		{
			Error = "test.unused_layout_analysis";
			return {};
		}
	};

	void CheckTextLayoutSelection()
	{
		auto State = std::make_shared<FState>();
		auto Platform = std::make_shared<FPlatform>(State);
		auto Layouts = std::make_shared<FTestLayouts>();
		auto Sources = Factories(State, Platform);
		Sources.TextLayout = [Layouts](const auto &Name, auto &)
		{
			Require(Name == "test-layout", "text-layout selection was coupled to the font backend");
			return Layouts;
		};
		const auto CreateGui = Sources.Gui;
		Sources.Gui = [CreateGui, Layouts](const auto &Name, const FBackendOptions &Input, auto &Error)
		{
			Require(Input.TextLayouts == Layouts && !Input.Fonts, "selected text-layout service was not independently injected");
			return CreateGui(Name, Input, Error);
		};
		auto Settings = Options();
		Settings.TextLayoutBackend = "test-layout";
		std::string Error;
		auto Application = FApplication::Create(Settings, Sources, Error);
		Require(Application && Application->Close(Error), Error);
		Sources.TextLayout = {};
		Require(!FApplication::Create(Settings, Sources, Error) && Error == "gui.provider_unavailable: text-layout/test-layout",
		    "explicit missing text-layout factory was silently ignored");
		Sources.TextLayout = [](const auto &, auto &) -> std::shared_ptr<Font::ITextLayoutService>
		{
			return {};
		};
		Require(!FApplication::Create(Settings, Sources, Error) && Error == "gui.provider_unavailable: text-layout/test-layout",
		    "configured text-layout factory silently returned an optional null service");
	}

	void CheckBackendDefaults()
	{
		auto State = std::make_shared<FState>();
		auto Platform = std::make_shared<FPlatform>(State);
		auto Sources = Factories(State, Platform);
		Sources.DefaultBackend = [](const std::string_view Service) -> std::string
		{
			if (Service == "platform") return "window-provider";
			if (Service == "graphics") return "drawing-provider";
			if (Service == "gui") return "layout-provider";
			return "none";
		};
		Sources.Image = [](const auto &, auto &) -> std::shared_ptr<Image::IService>
		{
			throw std::runtime_error("disabled image factory was called");
		};
		Sources.Font = [](const auto &, auto &) -> std::shared_ptr<Font::IService>
		{
			throw std::runtime_error("disabled font factory was called");
		};
		Sources.TextLayout = [](const auto &, auto &) -> std::shared_ptr<Font::ITextLayoutService>
		{
			throw std::runtime_error("disabled text-layout factory was called");
		};
		std::string Error;
		auto Application = FApplication::Create({}, Sources, Error);
		Require(Application && Application->Close(Error), "omitted choices inherit the registered service policy: " + Error);
		State->Closed.clear();
		for (const char *Role : {"platform", "graphics", "gui"})
		{
			auto Settings = Options();
			if (std::string_view(Role) == "platform")
			{
				Settings.PlatformBackend = "none";
			}
			else if (std::string_view(Role) == "graphics")
			{
				Settings.GraphicsBackend = "none";
			}
			else
			{
				Settings.GuiBackend = "none";
			}
			Require(!FApplication::Create(Settings, Sources, Error) && !State->bWindow && State->Closed.empty() &&
			        Error == "gui.provider_unavailable: " + std::string(Role) + "/none",
			    std::string("disabled required backend was accepted: ") + Role);
		}
		Sources.Image = {};
		Sources.Font = {};
		Sources.TextLayout = {};
		auto Settings = Options();
		Settings.ImageBackend = "missing-image";
		Require(!FApplication::Create(Settings, Sources, Error) && Error == "gui.provider_unavailable: image/missing-image" && !State->bWindow, "explicit unavailable image provider must not be silently ignored");
		Settings = Options();
		Settings.FontBackend = Font::DefaultBackendId;
		Require(!FApplication::Create(Settings, Sources, Error) && !State->bWindow,
		    "explicit optional resource IDs require a factory even when matching the legacy host default");
		Sources.DefaultBackend = [](std::string_view Role)
		{
			return std::string(Role == "gui" ? "layout-provider" : "");
		};
		Settings = Options();
		Settings.GuiBackend.clear();
		Application = FApplication::Create(Settings, Sources, Error);
		Require(Application && Application->Close(Error), "partial backend defaults changed native host fallbacks: " + Error);
		Sources.Image = [](const auto &, auto &) -> std::shared_ptr<Image::IService>
		{
			return {};
		};
		Settings.ImageBackend = "missing-image";
		Require(!FApplication::Create(Settings, Sources, Error) &&
		        Error == "gui.provider_unavailable: image/missing-image" && !State->bWindow,
		    "explicit image factory silently returned no service");
	}

	void CheckApplication()
	{
		auto State = std::make_shared<FState>();
		auto Platform = std::make_shared<FPlatform>(State);
		std::string Error;
		auto Application = FApplication::Create(Options(), Factories(State, Platform), Error);
		Require(Application != nullptr, Error);
		Require(!FApplication::Create(Options(), Factories(State, Platform), Error),
		    "two applications claimed the same event pump");
		auto Root = FWidget::Create();
		Require(Root->Set(EProperty::Focusable, true, Error) && Application->SetRoot(Root, Error) &&
		        Application->Tick(Error),
		    Error);
		Require(State->Rendered == 1 && Application->GetContext()->SetFocus(Root, Error), Error);
		auto Cancel = Root->Subscribe(
		    Platform::EEventType::CloseRequested, false, [](auto &Event)
		{
			Event.PreventDefault();
		}, Error);
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::CloseRequested;
		Event.WindowId = 1;
		State->Events.push_back(Event);
		Require(Application->Tick(Error) && Application->IsOpen() && Application->GetContext()->GetFocus() == Root,
		    "cancelled close changed ownership or focus: " + Error);
		Require(Cancel.Disconnect(Error), Error);
		auto Stop = Root->Subscribe(
		    Platform::EEventType::CloseRequested, false, [](auto &Input)
		{
			Input.StopPropagation();
		}, Error);
		State->Events.push_back(Event);
		Require(Application->Tick(Error) && !Application->IsOpen() &&
		        State->Closed == std::vector<std::string>{"gui", "surface", "device", "window"},
		    "stopping propagation cancelled default close or resources closed out of order: " + Error);
		Require(Application->Close(Error), Error);
	}

	void CheckPointerAssembly()
	{
		auto State = std::make_shared<FState>();
		State->bPointerSupport = true;
		auto Platform = std::make_shared<FPlatform>(State);
		std::string Error;
		auto Application = FApplication::Create(Options(), Factories(State, Platform), Error);
		Require(Application != nullptr, Error);
		auto Button = FWidget::Create();
		FControlOptions Control;
		Control.Kind = EControlKind::Button;
		Require(Button->SetControlOptions(Control, Error) && Button->Set(EProperty::Focusable, true, Error) &&
		        Application->SetRoot(Button, Error) && Application->Tick(Error),
		    Error);
		Platform::FEvent Event;
		Event.WindowId = 1;
		Event.DeviceId = 4;
		Event.Button = 1;
		Event.X = 20;
		Event.Y = 20;
		Event.Type = Platform::EEventType::PointerMove;
		State->Events.push_back(Event);
		Require(Application->Tick(Error) && State->Cursor == Platform::ECursor::Pointer,
		    "window cursor bridge: " + Error);
		Event.Type = Platform::EEventType::PointerDown;
		State->Events.push_back(Event);
		Require(Application->Tick(Error) && State->bMouseCaptured, "window capture bridge: " + Error);
		Event = {};
		Event.WindowId = 1;
		Event.Type = Platform::EEventType::FocusLost;
		State->Events.push_back(Event);
		Require(Application->Tick(Error) && !State->bMouseCaptured && State->Cursor == Platform::ECursor::Default,
		    "window focus-loss cleanup: " + Error);
		Event.Type = Platform::EEventType::PointerMove;
		Event.DeviceId = 4;
		Event.Button = 1;
		Event.X = Event.Y = 20;
		State->Events.push_back(Event);
		Require(Application->Tick(Error), Error);
		Event.Type = Platform::EEventType::PointerDown;
		State->Events.push_back(Event);
		Require(Application->Tick(Error) && State->bMouseCaptured, "capture before native window destruction");
		// Native destruction releases platform-owned input state before GUI cleanup begins.
		State->bWindow = false;
		State->bMouseCaptured = false;
		State->Cursor = Platform::ECursor::Default;
		State->bCursorVisible = true;
		Require(Application->Close(Error) && !Application->GetContext(),
		    "GUI cleanup after native window destruction: " + Error);
	}

	void CheckFailuresAndRetirement()
	{
		auto State = std::make_shared<FState>();
		auto Platform = std::make_shared<FPlatform>(State);
		State->bFailBackend = true;
		std::string Error;
		Require(!FApplication::Create(Options(), Factories(State, Platform), Error) &&
		        Error == "test backend failure" &&
		        State->Closed == std::vector<std::string>{"surface", "device", "window"},
		    "failed creation did not release owned resources");
		State->bFailBackend = false;
		State->Closed.clear();
		auto Application = FApplication::Create(Options(), Factories(State, Platform), Error);
		Require(Application != nullptr, Error);
		Platform::FEvent Hidden;
		Hidden.Type = Platform::EEventType::Minimized;
		Hidden.WindowId = 1;
		State->Events.push_back(Hidden);
		Require(Application->Tick(Error) && State->Rendered == 0, "minimized application continued drawing");
		Hidden.Type = Platform::EEventType::Restored;
		State->Events.push_back(Hidden);
		Require(Application->Tick(Error) && State->Rendered == 1, Error);
		std::thread Worker([Owned = std::move(Application)]() mutable
		{
			Owned.reset();
		});
		Worker.join();
		Require(State->Closed.empty(), "application resources were released on a foreign thread");
		Require(FApplication::CollectRetired(Error) &&
		        State->Closed == std::vector<std::string>{"gui", "surface", "device", "window"},
		    Error);
	}

	void CheckCleanupExceptions()
	{
		for (const auto *Stage : {"wait-idle", "surface", "device", "window", "platform"})
		{
			for (const bool FinalRelease : {false, true})
			{
				auto State = std::make_shared<FState>();
				auto Platform = std::make_shared<FPlatform>(State);
				std::string Error;
				auto Application = FApplication::Create(Options(), Factories(State, Platform), Error);
				Require(Application != nullptr, Error);
				State->ThrowOnCleanup = Stage;
				State->bUnknownException = FinalRelease;
				if (FinalRelease)
				{
					Application.reset();
					Require(!FApplication::CollectRetired(Error) && Error == "gui.application_close_exception",
					    "final release lost an unfinished resource or propagated an exception");
				}
				else
				{
					Require(!Application->Close(Error) &&
					        Error == "gui.application_close_exception: test cleanup failure: " + std::string(Stage),
					    "explicit close propagated a provider exception or lost its diagnostic");
				}
				Require(!FApplication::Create(Options(), Factories(State, Platform), Error),
				    "failed cleanup prematurely released the event pump lease");
				State->ThrowOnCleanup.clear();
				Require(FinalRelease ? FApplication::CollectRetired(Error) : Application->Close(Error), Error);
				Require(State->Closed == std::vector<std::string>{"gui", "surface", "device", "window"},
				    "cleanup retry lost resources, closed them twice or closed them out of order");
				auto Next = FApplication::Create(Options(), Factories(State, Platform), Error);
				Require(Next != nullptr && Next->Close(Error), "cleanup retry did not release the event pump lease");
			}
		}
		auto State = std::make_shared<FState>();
		auto Platform = std::make_shared<FPlatform>(State);
		State->bFailBackend = true;
		State->ThrowOnCleanup = "wait-idle";
		std::string Error;
		Require(!FApplication::Create(Options(), Factories(State, Platform), Error) &&
		        Error == "test backend failure" && State->Closed.empty(),
		    "failed initialization lost its error or cleanup resources");
		State->ThrowOnCleanup.clear();
		Require(FApplication::CollectRetired(Error) &&
		        State->Closed == std::vector<std::string>{"surface", "device", "window"},
		    "failed initialization cleanup could not be retried");
	}

	void CheckFramePacing()
	{
		auto State = std::make_shared<FState>();
		auto Platform = std::make_shared<FPlatform>(State);
		auto Settings = Options();
		Settings.WaitMilliseconds = 4;
		std::string Error;
		auto Application = FApplication::Create(Settings, Factories(State, Platform), Error);
		Require(Application != nullptr, Error);
		int Frames = 0;
		Require(Application->Run(Error,
		            [&](std::string &)
		{
			if (++Frames == 1)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(12));
			}
			else
			{
				Application->RequestStop();
			}
			return true;
		}),
		    Error);
		Require(State->Waits.size() == 2 && State->Waits.front() <= Settings.WaitMilliseconds &&
		        State->Waits.back() == 0,
		    "a slow frame must not incur an additional event-wait interval");
		Require(Application->Close(Error), Error);
	}

	void CheckRun()
	{
		auto State = std::make_shared<FState>();
		auto Platform = std::make_shared<FPlatform>(State);
		std::string Error;
		auto Application = FApplication::Create(Options(), Factories(State, Platform), Error);
		Require(Application != nullptr, Error);
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::Quit;
		State->Events.push_back(Event);
		Require(Application->Run(Error) && !Application->IsOpen(), Error);
	}

	#if VEROSCRIPT_GUI_SCRIPTING_TESTS
	void CheckOptionalRegisteredResources()
	{
		auto Empty = MakeRegisteredServiceFactories({});
		Require(!Empty.Image && !Empty.Font && !Empty.TextLayout, "absent optional resource providers became required factories");
		FNativeServiceProviderDesc Provider;
		Provider.ServiceId = Image::ServiceId;
		Provider.BackendId = "test-image";
		Provider.ContractId = Image::ContractId;
		Provider.ContractVersion = Image::ContractVersion;
		auto Selected = MakeRegisteredServiceFactories({Provider});
		Require(Selected.Image && !Selected.Font, "resource factories did not follow registered services");
		std::string Error;
		Require(!Selected.Image("other-backend", Error) && !Error.empty(),
		    "invalid configured backend silently became optional");
		Provider.ContractVersion = "invalid";
		Selected = MakeRegisteredServiceFactories({Provider});
		Require(!Selected.Image("test-image", Error) && Error.find("contract_mismatch") != std::string::npos,
		    "mismatched image provider contract accepted");
		int Created = 0;
		Provider.ServiceId = Font::TextLayoutServiceId;
		Provider.BackendId = "test-layout";
		Provider.ContractId = Font::TextLayoutContractId;
		Provider.ContractVersion = Font::TextLayoutContractVersion;
		Provider.Owner = std::make_shared<int>(0);
		Provider.UserData = &Created;
		Provider.Create = [](const FNativeServiceCreateContext &, void *Data, std::string &) -> void *
		{
			++*static_cast<int *>(Data);
			return static_cast<Font::ITextLayoutService *>(new FTestLayouts);
		};
		Provider.Destroy = [](void *Value, void *)
		{
			delete static_cast<Font::ITextLayoutService *>(Value);
		};
		int FailedCreations = 0;
		auto Unavailable = Provider;
		Unavailable.BackendId = "a-unavailable";
		Unavailable.UserData = &FailedCreations;
		Unavailable.Create = [](const auto &, void *Data, std::string &Failure) -> void *
		{
			++*static_cast<int *>(Data);
			Failure = "test backend unavailable";
			return nullptr;
		};
		Selected = MakeRegisteredServiceFactories({Provider, Unavailable});
		auto First = Selected.TextLayout("test-layout", Error);
		auto Second = Selected.TextLayout("test-layout", Error);
		Require(First && Second == First && Created == 1, "registered layout factory did not share a live immutable service instance");
		auto Automatic = Selected.TextLayout("auto", Error);
		Require(Automatic == First && Created == 1 && FailedCreations == 1,
		    "automatic selection must skip unavailable candidates and share the named live service");
		Require(!Selected.TextLayout("a-unavailable", Error) && Created == 1 && FailedCreations == 2,
		    "explicit unavailable backend must not fall through to the live alternative");
		Require(!Selected.TextLayout("none", Error) && Created == 1, "disabled optional service must not invoke a factory");
		Require(!Selected.TextLayout("other-layout", Error) && Error.find("text-layout/other-layout") != std::string::npos,
		    "unknown layout backend did not identify the missing service");
		First.reset();
		Second.reset();
		Automatic.reset();
		Require(Selected.TextLayout("test-layout", Error) && Created == 2, "layout factory cache retained an unused service");
		Provider.ContractVersion = "invalid";
		Selected = MakeRegisteredServiceFactories({Provider});
		Require(!Selected.TextLayout("test-layout", Error) && Error.find("contract_mismatch") != std::string::npos && Created == 2,
		    "invalid layout contract reached the provider factory");
	}
	void CheckScriptApplication()
	{
		struct FHost final : IEngineHost
		{
			void OnPrintLine(std::string_view) override
			{
			}
			void OnDiagnosticLine(std::string_view Text) override
			{
				std::cerr << Text << '\n';
			}
		} Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		auto State = std::make_shared<FState>();
		auto Platform = std::make_shared<FPlatform>(State);
		class FFonts final : public Font::IService
		{
		public:
			const char *GetBackendId() const noexcept override
			{
				return "test-font";
			}
			std::shared_ptr<Font::IFace> Load(const std::vector<std::uint8_t> &, std::int32_t, std::string &Failure,
			    std::int32_t) const override
			{
				Failure = "test.unused_font_load";
				return {};
			}
			bool GetVariations(const std::vector<std::uint8_t> &, std::int32_t, std::vector<Font::FVariation> &,
			    std::string &Failure) const override
			{
				Failure = "test.unused_font_variations";
				return false;
			}
		};
		int FontCreations = 0;
		FScriptServices Services;
		Services.Application = Factories(State, Platform);
		Services.Application.Font = [&FontCreations](const auto &Name, auto &)
		{
			Require(Name == Font::DefaultBackendId || Name == "test-font", "unexpected font backend selection");
			++FontCreations;
			return std::make_shared<FFonts>();
		};
		Services.Application.TextLayout = [](const auto &Name, auto &)
		{
			Require(Name == "test-layout", "script text-layout selection did not override the CLI configuration");
			return std::make_shared<FTestLayouts>();
		};
		std::string Error;
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		FToolOptions ToolOptions;
		auto Tools = CreateCommandLineService(ToolOptions);
		auto Registration = Tools->CreateRegistration();
		FCommandLineContext CommandContext;
		Require(Registration.Module.Configure(CommandContext,
		            {{"platform-backend", {"ignored-by-explicit-script"}},
		                {"graphics-backend", {"drawing-provider"}},
		                {"gui-backend", {"layout-provider"}},
		                {"text-layout-backend", {"ignored-by-explicit-script"}}},
		            Registration.Module.UserData, Error),
		    Error);
		Require(Registration.RegisterBindings(Session.GetBindingRegistry(), Error), Error);
		// The package host discovers providers after it has registered GUI bindings.
		Registration.SetRuntimeServices(std::move(Services));
		Require(Registration.ConfigureEngine(Config, nullptr, Error), Error);
		Session.ApplyConfig(Config);
		auto Loaded = Session.LoadSource(R"vs(
void main()
{
    VS::Gui::ApplicationOptions options = VS::Gui::ApplicationOptions();
    options.PlatformBackend = "window-provider";
    options.TextLayoutBackend = "test-layout";
    VS::Gui::Application application = VS::Gui::Application(options);
    int count = 2;
    std::attribute<double> amount = std::attribute<double>();
    amount.bind([&count]()->double { return count; });
    VS::Gui::Widget root = VS::Gui::Widget::Create().Gap(amount);
    root.Subscribe("pointer-down", [&count, application](VS::Gui::Widget sender, VS::Gui::Event event) mutable ->void
    {
        assert(sender == event.Target, "application routed the wrong target");
        count = 6;
        application.RequestStop();
    });
    application.SetRoot(root);
    application.Run();
    assert(application.IsOpen() && root.GetGap() == 6, "Run did not synchronize after the callback");
    application.Close();
    assert(!application.IsOpen(), "application remained open");
    VS::Font::Service fonts = VS::Font::Service::Create("test-font");
    VS::Gui::Application animated = VS::Gui::Application(options);
    try
    {
        int frames = 0;
        std::attribute<double> frameGap = std::attribute<double>();
        frameGap.bind([&frames]()->double { return frames; });
        VS::Gui::Widget animatedRoot = VS::Gui::Widget::Create().Gap(frameGap);
        animated.SetRoot(animatedRoot);
        animated.Tick([&frames]()->void { frames++; });
        assert(frames == 2 && animatedRoot.GetGap() == 2,
               "Initial Tick must invoke the hook for bootstrap and event-phase synchronization");
        animated.Tick([&frames]()->void { frames++; });
        assert(frames == 3 && animatedRoot.GetGap() == 3,
               "Tick with an existing layout invoked the hook more than once");
        animated.Run([&frames, animated]() mutable ->void
        {
            frames++;
            if (frames == 5) animated.RequestStop();
        });
        assert(frames == 5 && animated.IsOpen() && animatedRoot.GetGap() == 5,
               "Run frame hook lost pacing, stop or synchronization semantics");
    }
    finally { animated.Close(); }
}
)vs",
		    "script-application.vs");
		Require(Loaded.bSuccess, Loaded.Error.Message);
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::PointerDown;
		Event.WindowId = 1;
		Event.Button = 1;
		State->Events.push_back(Event);
		const auto Result = Session.Run();
		Require(Result.bSuccess, Result.Error.Message);
		Require(FontCreations == 3, "late provider discovery did not reach applications and standalone Font bindings");
		Require(State->Closed == std::vector<std::string>{"gui", "surface", "device", "window", "gui", "surface", "device", "window"},
		    "script application did not release native resources");
		Require(State->Waits.size() >= 3, "script Run frame hook bypassed the event-wait loop");
	}
	#endif
} // namespace

int main()
{
	try
	{
		CheckApplication();
		CheckBackendDefaults();
		CheckTextLayoutSelection();
		CheckPointerAssembly();
		CheckFailuresAndRetirement();
		CheckCleanupExceptions();
		CheckRun();
		CheckFramePacing();
		#if VEROSCRIPT_GUI_SCRIPTING_TESTS
		CheckOptionalRegisteredResources();
		CheckScriptApplication();
		#endif
		std::cout << "GUI application assembly, close routing and ownership passed\n";
		return 0;
	}
	catch (const std::exception &Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}

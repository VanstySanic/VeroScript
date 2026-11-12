// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "Widget.vs";
export import "Resources.vs";

namespace VS::Gui
{
	export class Context
	{
	public:
		Context(string backend, int64 width, int64 height, double scale)
		{
			handle = __NativeContext(backend, width, height, scale);
		}

		Context(string backend, int64 width, int64 height)
		{
			handle = __NativeContext(backend, width, height, 1);
		}

		Context(__NativeContext value)
		{
			handle = value;
		}

		void SetRoot(Widget value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (value != nullptr)
			{
				value.Initialize();
			}
			try
			{
				if (value == nullptr)
				{
					handle.SetRoot(nullptr);
				}
				else
				{
					handle.SetRoot(value.__GetNativeHandle());
				}
			}
			finally
			{
				VsguiRefreshRoot();
			}
		}

		Widget GetRoot() const
		{
			return VS::Gui::Widget::VsguiWrap(handle.GetRoot());
		}
		Widget LoadWidget(string reference)
		{
			if (!IsOpen())
			{
				throw "GUI context is closed";
			}
			auto factory = VS::Gui::LoadFactory<Widget>(reference);
			return factory.Create();
		}
		__NativeContext __GetNativeHandle() const
		{
			return handle;
		}
		bool IsOpen() const
		{
			return handle.IsOpen();
		}
		void SetViewport(int64 width, int64 height, double scale)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.SetViewport(width, height, scale);
		}

		void SetViewport(int64 width, int64 height)
		{
			SetViewport(width, height, 1);
		}

		void SetTheme(string styles)
		{
			SetTheme("<gui-theme>", styles);
		}
		void SetTheme(string identity, string styles)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.SetTheme(identity, styles);
		}
		void ClearTheme()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.ClearTheme();
		}

		void SetLocalization(std::i18n::context value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.SetLocalization([value]() -> std::i18n::context
			{
				return value;
			});
		}
		void LoadFont(string source, string family, int weight, bool italic, bool fallback, int faceIndex)
		{
			handle.LoadFont(VS::Gui::__NativeResources::Resolve(source), family, weight, italic, fallback, faceIndex);
		}
		void LoadFont(string source, string family)
		{
			LoadFont(source, family, 400, false, false, 0);
		}
		std::i18n::snapshot GetLocalizationSnapshot() const
		{
			optional<std::i18n::snapshot> result = optional<std::i18n::snapshot>();
			handle.ReadLocalizationSnapshot([&result](std::i18n::snapshot value) -> void
			{
				result = optional<std::i18n::snapshot>(value);
			});
			return result.get();
		}

		void Update(double elapsedSeconds)
		{
			VS::Gui::BindingUpdates::VsguiRequireBoundary();
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI update is not allowed in a getter");
			try
			{
				handle.Update(elapsedSeconds);
			}
			finally
			{
				VsguiRefreshRoot();
			}
		}

		bool ProcessEvent(__NativeEvent value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI input is not allowed in a getter");
			return handle.ProcessEvent(value);
		}
		bool Render()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI rendering is not allowed in a getter");
			return handle.Render();
		}

		void SetFocus(Widget value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (value == nullptr)
			{
				handle.SetFocus(nullptr);
			}
			else
			{
				handle.SetFocus(value.__GetNativeHandle());
			}
		}
		Widget GetFocus() const
		{
			return VS::Gui::Widget::VsguiWrap(handle.GetFocus());
		}
		void CapturePointer(Event input, Widget value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			assert(value != nullptr, "Pointer capture requires a widget");
			handle.CapturePointer(input, value.__GetNativeHandle());
		}
		void ReleasePointer(Event input)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.ReleasePointer(input);
		}
		void Post(fn() -> void work)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.Post(work);
		}

		void Close()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			try
			{
				handle.Close();
			}

			finally
			{
				VsguiRefreshRoot();
			}
		}

		void VsguiRefreshRoot()
		{
			root = VS::Gui::Widget::VsguiWrap(handle.GetRoot());
		}

	private:
		__NativeContext handle;
		Widget root;
	};

	export struct ApplicationOptions
	{
		string Title = "VeroScript";
		int64 Width = 960;
		int64 Height = 640;
		string PlatformBackend = "";
		string GraphicsBackend = "";
		string GuiBackend = "";
		string ImageBackend = "";
		string FontBackend = "";
		string TextLayoutBackend = "";
		bool Rendering = true;
		int64 WaitMilliseconds = 16;
	};

	export class Application
	{
	public:
		Application(ApplicationOptions options)
		{
			handle = __NativeApplication(options.Title, options.Width, options.Height, options.PlatformBackend, options.GraphicsBackend, options.GuiBackend, options.Rendering, options.WaitMilliseconds, options.ImageBackend, options.FontBackend, options.TextLayoutBackend);
			context = Context(handle.GetContext());
		}

		Context GetContext() const
		{
			return context;
		}
		void SetRoot(Widget value)
		{
			context.SetRoot(value);
		}
		bool IsOpen() const
		{
			return handle.IsOpen();
		}

		void Tick()
		{
			Tick([]() -> void
			{
			});
		}

		void Tick(fn() -> void beforeUpdate)
		{
			VS::Gui::BindingUpdates::VsguiRequireBoundary();
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI update is not allowed in a getter");
			try
			{
				handle.Tick(beforeUpdate);
			}
			finally
			{
				context.VsguiRefreshRoot();
			}
		}

		void Run()
		{
			Run([]() -> void
			{
			});
		}

		void Run(fn() -> void beforeUpdate)
		{
			VS::Gui::BindingUpdates::VsguiRequireBoundary();
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI execution is not allowed in a getter");
			try
			{
				handle.Run(beforeUpdate);
			}
			finally
			{
				context.VsguiRefreshRoot();
			}
		}

		void RequestStop()
		{
			handle.RequestStop();
		}
		void Close()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			try
			{
				handle.Close();
			}
			finally
			{
				context.VsguiRefreshRoot();
			}
		}

	private:
		__NativeApplication handle;
		Context context;
	};
} // namespace VS::Gui

namespace VS::Gui
{
	namespace Internal
	{
		export void DispatchWork(fn() -> void work)
		{
			work();
		}
	}
} // namespace VS::Gui

namespace VS::Gui
{
	export Widget LoadWidget<TContext>(Context gui, string reference, TContext context)
	{
		if (!gui.IsOpen())
		{
			throw "GUI context is closed";
		}
		auto factory = VS::Gui::LoadFactory<Widget>(reference);
		return VS::Gui::Create(factory, context);
	}
} // namespace VS::Gui

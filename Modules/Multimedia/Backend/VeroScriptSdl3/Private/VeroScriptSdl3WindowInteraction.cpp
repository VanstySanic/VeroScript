// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3Platform.h"

namespace VeroScript::Sdl3
{
	SDL_Window* CreateNativeWindow(const Platform::FWindowOptions& Options, SDL_WindowFlags Flags, std::string& Error)
	{
		const auto& Api = Options.GraphicsApi;
		if (!Api.empty() && Api != "opengl" && Api != "opengles" && Api != "direct3d11" && Api != "direct3d11-warp" &&
			Api != "direct3d12" && Api != "vulkan" && Api != "metal" && Api != "webgpu")
		{
			Error = "platform.unsupported_graphics_api: unknown window graphics compatibility hint";
			return nullptr;
		}
		const auto Create = [&](SDL_WindowFlags NativeFlags)
		{
			auto* Window = SDL_CreateWindow(Options.Title.c_str(), static_cast<int>(Options.Width),
										static_cast<int>(Options.Height), NativeFlags);
			if (!Window)
			{
				SdlError("create window", Error);
			}
			else
			{
				Error.clear();
			}
			return Window;
		};
		if (Api != "opengl" && Api != "opengles")
		{
			return Create(Flags);
		}
		// GL context attributes are process-wide. Restore the host's configuration after window creation.
		struct FAttributes
		{
			~FAttributes()
			{
				if (!bSaved)
				{
					return;
				}
				for (unsigned I = 0; I < std::size(Keys); ++I)
				{
					SDL_GL_SetAttribute(Keys[I], Values[I]);
				}
			}
			SDL_GLAttr Keys[6]{SDL_GL_CONTEXT_MAJOR_VERSION,	  SDL_GL_CONTEXT_MINOR_VERSION,
							   SDL_GL_CONTEXT_PROFILE_MASK,		  SDL_GL_CONTEXT_FLAGS,
							   SDL_GL_SHARE_WITH_CURRENT_CONTEXT, SDL_GL_FRAMEBUFFER_SRGB_CAPABLE};
			int Values[6]{};
			bool bSaved = false;
		} Saved;
		for (unsigned I = 0; I < std::size(Saved.Keys); ++I)
		{
			if (!SDL_GL_GetAttribute(Saved.Keys[I], &Saved.Values[I]))
			{
				SdlError("read OpenGL window configuration", Error);
				return nullptr;
			}
		}
		Saved.bSaved = true;
		const int Values[]{3,
						   Api == "opengles" ? 0 : 3,
						   Api == "opengles" ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE,
						   0,
						   0,
						   0};
		for (unsigned I = 0; I < std::size(Saved.Keys); ++I)
		{
			if (!SDL_GL_SetAttribute(Saved.Keys[I], Values[I]))
			{
				SdlError("configure OpenGL window", Error);
				return nullptr;
			}
		}
		return Create(Flags | SDL_WINDOW_OPENGL);
	}

	bool FWindowState::ApplyCursor(std::string& Error) const
	{
		if (!bCursorConfigured || (SDL_GetMouseFocus() != Retirement->Native && !bCaptured.load()))
		{
			Error.clear();
			return true;
		}
		if (!SDL_SetCursor(Retirement->Cursor) || !(bCursorVisible ? SDL_ShowCursor() : SDL_HideCursor()))
		{
			return SdlError("apply window cursor", Error);
		}
		Error.clear();
		return true;
	}

	bool FWindowState::ReleaseCapture() noexcept
	{
		if (!bCaptured.exchange(false))
		{
			return true;
		}
		Retirement->bCaptureRequested = false;
		Runtime->CaptureOwner.reset();
		const bool bReleased = SDL_CaptureMouse(false);
		if (SDL_GetMouseFocus() != Retirement->Native && SDL_GetCursor() == Retirement->Cursor)
		{
			SDL_SetCursor(SDL_GetDefaultCursor());
			SDL_ShowCursor();
		}
		return bReleased;
	}

	bool FWindow::IsBorrowed() const noexcept
	{
		return State->Retirement->bBorrowed;
	}

	bool FWindow::SetCursor(Platform::ECursor Shape, bool bVisible, std::string& Error)
	{
		if (!State->Check(Error))
		{
			return false;
		}
		static constexpr SDL_SystemCursor Shapes[]{
			SDL_SYSTEM_CURSOR_DEFAULT,	   SDL_SYSTEM_CURSOR_TEXT,		  SDL_SYSTEM_CURSOR_WAIT,
			SDL_SYSTEM_CURSOR_CROSSHAIR,   SDL_SYSTEM_CURSOR_PROGRESS,	  SDL_SYSTEM_CURSOR_NWSE_RESIZE,
			SDL_SYSTEM_CURSOR_NESW_RESIZE, SDL_SYSTEM_CURSOR_EW_RESIZE,	  SDL_SYSTEM_CURSOR_NS_RESIZE,
			SDL_SYSTEM_CURSOR_MOVE,		   SDL_SYSTEM_CURSOR_NOT_ALLOWED, SDL_SYSTEM_CURSOR_POINTER};
		const auto Index = static_cast<unsigned>(Shape);
		if (Index >= std::size(Shapes))
		{
			Error = "platform.invalid_cursor: unknown system cursor";
			return false;
		}
		auto& Native = State->Retirement->Cursor;
		std::unique_ptr<SDL_Cursor, decltype(&SDL_DestroyCursor)> Created(nullptr, SDL_DestroyCursor);
		SDL_Cursor* Candidate = Native;
		if (!Native || State->CursorShape != Shape)
		{
			Created.reset(SDL_CreateSystemCursor(Shapes[Index]));
			Candidate = Created.get();
			if (!Candidate)
			{
				return SdlError("create system cursor", Error);
			}
		}
		if (SDL_GetMouseFocus() == State->Retirement->Native || State->bCaptured.load())
		{
			auto* Previous = SDL_GetCursor();
			const bool bWasVisible = SDL_CursorVisible();
			if (!SDL_SetCursor(Candidate) || !(bVisible ? SDL_ShowCursor() : SDL_HideCursor()))
			{
				SdlError("set system cursor", Error);
				SDL_SetCursor(Previous ? Previous : SDL_GetDefaultCursor());
				bWasVisible ? SDL_ShowCursor() : SDL_HideCursor();
				return false;
			}
		}
		if (Native && Native != Candidate)
		{
			SDL_DestroyCursor(Native);
		}
		Native = Candidate;
		Created.release();
		State->CursorShape = Shape;
		State->bCursorVisible = bVisible;
		State->bCursorConfigured = true;
		Error.clear();
		return true;
	}

	bool FWindow::SetMouseCapture(bool bEnabled, std::string& Error)
	{
		if (!State->Check(Error))
		{
			return false;
		}
		if (!bEnabled)
		{
			if (!State->ReleaseCapture())
			{
				return SdlError("release mouse capture", Error);
			}
			Error.clear();
			return true;
		}
		if (SDL_GetMouseFocus() != State->Retirement->Native || SDL_GetKeyboardFocus() != State->Retirement->Native)
		{
			Error = "platform.capture_focus_required: the window must own native mouse and keyboard focus";
			return false;
		}
		const auto Previous = State->Runtime->CaptureOwner.lock();
		if (!SDL_CaptureMouse(true))
		{
			SdlError("capture mouse", Error);
			SDL_CaptureMouse(Previous && Previous->bCaptured.load());
			return false;
		}
		if (Previous && Previous != State)
		{
			Previous->bCaptured.store(false);
			Previous->Retirement->bCaptureRequested = false;
		}
		State->Runtime->CaptureOwner = State;
		State->Retirement->bCaptureRequested = true;
		State->bCaptured.store(true);
		Error.clear();
		return true;
	}

	bool FWindow::HasMouseCapture() const noexcept
	{
		return State->bCaptured.load();
	}

	bool SDLCALL WatchWindowLifecycle(void* UserData, SDL_Event* Event) noexcept
	{
		auto& Runtime = *static_cast<FRuntime*>(UserData);
		// SDL may invoke event watches on a submitting worker. Native window destruction is owner-thread only.
		if (std::this_thread::get_id() != Runtime.OwnerThread)
		{
			return true;
		}
		if (Event->type == SDL_EVENT_WILL_ENTER_BACKGROUND || Event->type == SDL_EVENT_DID_ENTER_BACKGROUND ||
			Event->type == SDL_EVENT_QUIT)
		{
			if (const auto Window = Runtime.CaptureOwner.lock())
			{
				Window->ReleaseCapture();
			}
			return true;
		}
		if (Event->type < SDL_EVENT_WINDOW_FIRST || Event->type > SDL_EVENT_WINDOW_LAST)
		{
			return true;
		}
		const auto Found = Runtime.Windows.find(Event->window.windowID);
		if (Found == Runtime.Windows.end())
		{
			return true;
		}
		const auto Window = Found->second.lock();
		if (!Window)
		{
			return true;
		}
		if (Event->type == SDL_EVENT_WINDOW_FOCUS_LOST || Event->type == SDL_EVENT_WINDOW_HIDDEN ||
			Event->type == SDL_EVENT_WINDOW_MINIMIZED || Event->type == SDL_EVENT_WINDOW_DESTROYED)
		{
			Window->ReleaseCapture();
		}
		if (Event->type == SDL_EVENT_WINDOW_DESTROYED)
		{
			Window->bOpen.store(false);
			if (Window->Claim)
			{
				// SDL keeps claimed GPU windows until explicitly released. Unclaim while the native
				// window is still valid, before SDL destroys its properties and platform handle.
				Window->Claim->Release();
			}
			Window->Retirement->Native = nullptr;
			Window->Retirement->Release();
		}
		else if (Event->type == SDL_EVENT_WINDOW_MOUSE_ENTER)
		{
			try
			{
				std::string Error;
				Window->ApplyCursor(Error);
			}
			catch (...)
			{
				// No script or allocation failure may unwind through SDL's native callback.
			}
		}
		else if (Event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE && Window->bCursorConfigured &&
				 !Window->bCaptured.load() && SDL_GetCursor() == Window->Retirement->Cursor)
		{
			SDL_SetCursor(SDL_GetDefaultCursor());
			SDL_ShowCursor();
		}
		return true;
	}
}

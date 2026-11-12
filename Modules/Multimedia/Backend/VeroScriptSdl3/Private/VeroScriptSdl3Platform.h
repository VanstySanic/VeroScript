// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptSdl3Runtime.h"

namespace VeroScript::Sdl3
{
	SDL_Window* CreateNativeWindow(const Platform::FWindowOptions&, SDL_WindowFlags, std::string&);
	struct FWindowRetirement final : FRetirement
	{
		void Release() noexcept override;
		SDL_Window* Native = nullptr;
		std::uint64_t WindowId = 0;
		SDL_WindowID NativeId = 0;
		SDL_Cursor* Cursor = nullptr;
		std::shared_ptr<const void> HostOwner;
		bool bBorrowed = false;
		bool bCaptureRequested = false;
	};

	class FWindowState final
	{
	public:
		FWindowState(std::shared_ptr<FRuntime> Runtime, std::uint64_t Id);
		~FWindowState();
		bool Check(std::string& Error) const;
		bool Close(std::string& Error);
		bool ApplyCursor(std::string& Error) const;
		bool ReleaseCapture() noexcept;

		std::shared_ptr<FRuntime> Runtime;
		std::unique_ptr<FWindowRetirement> Retirement;
		std::uint64_t Id;
		std::uint64_t CreatedNanoseconds = 0;
		std::uint32_t Claims = 0;
		std::uint32_t OpenGlContextClaims = 0;
		// Non-owning, owner-thread registration. The claim retains this window until Release unregisters it.
		FRetirement* Claim = nullptr;
		std::atomic<bool> bOpen{false};
		std::atomic<bool> bCaptured{false};
		bool bCursorVisible = true, bCursorConfigured = false;
		Platform::ECursor CursorShape = Platform::ECursor::Default;
	};

	class FWindow final : public Platform::IWindow
	{
	public:
		//~ Begin Platform::IWindow Interface
		std::uint64_t GetId() const noexcept override;
		bool IsOpen() const noexcept override;
		bool GetMetrics(Platform::FWindowMetrics& Result, std::string& Error) const override;
		bool SetTitle(const std::string& Title, std::string& Error) override;
		bool SetSize(std::int64_t Width, std::int64_t Height, std::string& Error) override;
		bool SetVisible(bool bVisible, std::string& Error) override;
		bool SetTextInputEnabled(bool bEnabled, std::string& Error) override;
		bool SetTextInputArea(double X, double Y, double Width, double Height, std::int64_t Cursor,
							  std::string& Error) override;
		bool Close(std::string& Error) override;
		bool IsBorrowed() const noexcept override;
		bool SetCursor(Platform::ECursor Cursor, bool bVisible, std::string& Error) override;
		bool SetMouseCapture(bool bEnabled, std::string& Error) override;
		bool HasMouseCapture() const noexcept override;
		//~ End Platform::IWindow Interface

		explicit FWindow(std::shared_ptr<FWindowState> State);
		std::shared_ptr<FWindowState> GetState() const;

	private:
		std::shared_ptr<FWindowState> State;
	};
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiWidget.h"
#include "VeroScriptGuiPointer.h"
#include <map>
#include <atomic>
#include <mutex>

namespace VeroScript::Gui
{
	class IFrameService
	{
	public:
		virtual ~IFrameService() = default;
		/** Capture immutable views once, before any bindings in the frame are evaluated. */
		virtual bool BeginFrame(double ElapsedSeconds, std::string& Error) = 0;
	};

	/** Optional second phase; all frame snapshots are captured before bindings run.
	 * Existing IFrameService implementations keep their original interface. */
	class IFrameBindingService : public IFrameService
	{
	public:
		virtual bool FlushBindings(std::string& Error) = 0;
	};

	struct FPointerIdentity
	{
		bool operator<(const FPointerIdentity& Other) const noexcept;
		bool operator==(const FPointerIdentity& Other) const noexcept;
		static FPointerIdentity FromEvent(const Platform::FEvent& Event) noexcept;
		Platform::EInputDeviceKind Kind = Platform::EInputDeviceKind::Mouse;
		std::uint64_t DeviceId = 0, PointerId = 0;
	};
	/** Host-driven GUI context. It borrows platform/window/device ownership from its caller. */
	class FContext : public std::enable_shared_from_this<FContext>
	{
		friend class FWidget;
		friend struct FMarkupRuntime;

	public:
		~FContext();
		FContext(const FContext&) = delete;
		FContext& operator=(const FContext&) = delete;
		FContext(FContext&&) = delete;
		FContext& operator=(FContext&&) = delete;
		static std::shared_ptr<FContext> Create(std::unique_ptr<IBackendContext> Backend, const FViewport& Viewport,
												std::string& Error);
		/** Reclaims contexts retired by final release on another thread. Call on the owner before shutdown. */
		static bool CollectRetired(std::string& Error);
		bool SetRoot(std::shared_ptr<FWidget> Value, std::string& Error);
		std::shared_ptr<FWidget> GetRoot() const;
		bool SetViewport(const FViewport& Value, std::string& Error);
		/** Lowest style layer, evaluated independently inside each declaration scope. */
		bool SetTheme(std::shared_ptr<const FStyleSheet> Value, std::string& Error);
		bool SetResourceReader(FResourceReader Reader, std::string& Error);
		bool LoadFont(const FFontFace& Font, std::string& Error);
		bool SetFrameService(std::string Id, std::shared_ptr<IFrameService> Service, std::string& Error);
		std::shared_ptr<IFrameService> GetFrameService(const std::string& Id, std::string& Error) const;
		bool SetWindowMetrics(std::uint64_t WindowId, const Platform::FWindowMetrics& Metrics, std::string& Error);
		bool Update(double ElapsedSeconds, std::string& Error);
		bool Render(bool& bPresented, std::string& Error);
		bool ProcessEvent(const Platform::FEvent& Input, bool& bHandled, std::string& Error);
		bool ProcessEvent(const Platform::FEvent& Input, FEventResult& Result, std::string& Error);
		/** Thread-safe enqueue; work and accepted-work cleanup execute on the owner thread. */
		bool Post(std::function<bool(std::string&)> Work, std::string& Error);
		/** Reveals a laid-out target in ancestor ScrollViews through
		 * their ordinary Scrolled callbacks. */
		bool SetFocus(std::shared_ptr<FWidget> Value, std::string &Error);
		std::shared_ptr<FWidget> GetFocus() const;
		bool CapturePointer(std::uint64_t PointerId, std::shared_ptr<FWidget> Value, std::string& Error);
		bool ReleasePointer(std::uint64_t PointerId, std::string& Error);
		bool CapturePointer(const FPointerIdentity& Pointer, std::shared_ptr<FWidget> Value, std::string& Error);
		bool ReleasePointer(const FPointerIdentity& Pointer, std::string& Error);
		bool IsEffectivelyEnabled(const std::shared_ptr<FWidget>& Value) const;
		bool SetPointerServices(FPointerServices Services, std::string &Error);
		/** Host text-input callbacks may query GUI state; defer mutations until the callback returns. */
		bool SetTextInputServices(FTextInputServices Services, std::string &Error);
		/** Stops new work and detaches the tree. Retry Close if backend cleanup fails. */
		bool Close(std::string& Error);
		/** False once closing begins, including while backend cleanup awaits a retry. */
		bool IsOpen() const noexcept;
		FCapabilities GetCapabilities() const noexcept;

	private:
		struct FFloatingState
		{
			std::weak_ptr<FWidget> Widget;
			std::uint64_t PreviousPortal = 0;
			bool bModal = false;
			std::weak_ptr<FWidget> PreviousFocus;
			double HoverSince = 0;
			bool bHovered = false, bOpen = false;
			std::uint64_t ModalOrder = 0;
		};
		struct FControlPress
		{
			std::weak_ptr<FWidget> Widget;
			double GrabOffset = 0;
		};
		struct FScrollGesture
		{
			std::weak_ptr<FWidget> Widget;
			double StartX = 0, StartY = 0, LastX = 0, LastY = 0;
			bool bPanning = false;
		};

		bool PrepareAdditionalControls(const std::vector<std::shared_ptr<FWidget>> &Nodes, double Time, bool bFloating,
									   std::string &Error);
		bool PerformAdditionalControlAction(FEvent &Event, bool &bHandled, std::string &Error);
		void RememberFloatingFocus(FFloatingState& State);
		void RetireFloatingFocus(std::uint64_t Id, const FFloatingState& State);
		bool CommitNumber(const std::shared_ptr<FWidget> &Widget, bool bSubmit, std::string &Error);
		bool SelectRadio(const std::shared_ptr<FWidget> &Widget, std::string &Error);
		FContext(std::unique_ptr<IBackendContext> Backend, const FViewport& Viewport);
		bool Check(std::string& Error) const;
		bool Gather(std::vector<std::shared_ptr<FWidget>>& Nodes, std::string& Error) const;
		bool Eligible(const std::shared_ptr<FWidget>& Value, bool bPointer, bool bFocus) const;
		std::shared_ptr<FWidget> GetModalRoot() const;
		bool RefreshModalFocus(std::string& Error);
		std::shared_ptr<FWidget> HitTest(double X, double Y) const;
		void CleanInputState();
		void RefreshHover();
		bool Dispatch(FEvent& Event, std::string& Error);
		bool PerformControlAction(FEvent &Event, bool &bHandled, std::string &Error);
		bool NotifyControl(const std::shared_ptr<FWidget> &Widget, FControlChange Change, std::string &Error);
		bool SetFocusTarget(std::shared_ptr<FWidget> Value, bool bReveal, std::string &Error);
		bool RevealFocus(const std::shared_ptr<FWidget> &Widget, std::string &Error);
		bool RefreshPointerServices(std::string &Error);
		bool ReleasePointerServices(std::string &Error);
		bool RefreshInputServices(std::string &Error);
		bool FocusBackendTextControl(std::uint64_t Id, const FTextInputServices &Services, std::string &Error);
		bool RefreshTextFocus(std::string &Error);
		void CancelControlPress() noexcept;

		std::thread::id Thread;
		std::unique_ptr<IBackendContext> Backend;
		std::shared_ptr<FWidget> Root;
		FViewport Viewport;
		std::shared_ptr<const FStyleSheet> Theme;
		std::shared_ptr<FMarkupRuntime> SceneStyles;
		std::map<std::string, std::shared_ptr<IFrameService>> FrameServices;
		std::weak_ptr<FWidget> Focus;
		std::map<FPointerIdentity, std::weak_ptr<FWidget>> Hovered;
		std::map<FPointerIdentity, std::pair<double, double>> MousePositions;
		std::map<std::pair<FPointerIdentity, std::int32_t>, std::weak_ptr<FWidget>> Pressed;
		std::map<FPointerIdentity, std::weak_ptr<FWidget>> Captures;
		std::vector<std::function<bool(std::string&)>> Pending;
		std::mutex PendingMutex;
		std::atomic<bool> bAcceptingWork{true};
		FContext* RetiredNext = nullptr;
		std::vector<std::weak_ptr<FWidget>> LayoutNodes;
		std::uint64_t Round = 0, Frame = 0, WindowId = 0;
		double WindowScaleX = 1, WindowScaleY = 1, LastTime = 0;
		bool bUpdating = false, bDraining = false, bDispatching = false, bRendering = false;
		bool bSynchronizingScripts = false;
		bool bDirty = true, bReady = false;
		bool bNotifyingLayout = false, bNextFrameDirty = false, bFinalizing = false;
		std::map<std::uint64_t, FFloatingState> FloatingStates;
		std::map<FPointerIdentity, FControlPress> ControlPresses;
		std::weak_ptr<FWidget> ActiveControl;
		Platform::EKey ControlKey = Platform::EKey::Unknown;
		std::map<FPointerIdentity, FScrollGesture> ScrollGestures;
		std::uint64_t ControlKeyboardDevice = 0;
		FPointerServices PointerServices;
		std::optional<FPointerIdentity> LastMouse;
		std::weak_ptr<FWidget> MouseHit;
		Platform::ECursor AppliedCursor = Platform::ECursor::Default;
		bool bAppliedCursorVisible = true, bCursorApplied = false, bMouseCaptured = false;
		bool bRefreshingPointerServices = false, bMouseInside = false;
		FTextInputServices TextInputServices;
		bool bRefreshingTextServices = false;
		bool bInputActive = true;
		bool bHasModalControls = false;
		std::uint64_t NextModalOrder = 0;
		std::uint64_t TextFocus = 0;
	};
} // namespace VeroScript::Gui

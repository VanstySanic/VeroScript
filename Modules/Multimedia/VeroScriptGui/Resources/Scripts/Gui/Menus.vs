// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "CompositeSupport.vs";
export import "Controls.vs";
export import "Widget.vs";
export import "Binding.vs";
import "delegate.vs";

namespace VS::Gui
{
	@ GuiWidget(Mark = "menu") export class Menu : public VS::Gui::Stack
	{
	public:
		Menu()
		{
			SetLayout(Layout::Horizontal);
			Subscribe("PointerDown", [](Widget sender, Event input) -> void
			{
				if (input.DefaultPrevented || input.Button != 1) return; MenuItem target = VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(input.Target); if (target == nullptr || VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(target.GetParent()) != nullptr) return; for (Widget child : sender.GetContentChildren())
				{
					optional<MenuItem> item = try_cast<MenuItem>(child); if (item.has_value() && item.get() != target)
					{
						item.get().CloseMenu();
					}
				}
			}, true);
		}
		@ GuiMethod void CloseMenus()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			for (Widget child : GetContentChildren())
			{
				optional<MenuItem> item = try_cast<MenuItem>(child);
				if (item.has_value())
				{
					item.get().CloseMenu();
				}
			}
		}
		void VsguiInvoke(string command)
		{
			Invoked.broadcast(this, command);
		}
	private:
		@ GuiEvent(Mark = "invoked") std::multicast_delegate<VS::Gui::Menu, string> Invoked = std::multicast_delegate<VS::Gui::Menu, string>();
	};

	@ GuiWidget(Mark = "menu-item") export class MenuItem : public VS::Gui::Button
	{
	public:
		MenuItem()
		{
			popup = Popup::Create().Width(Length(200)).Background(Color(0.08, 0.09, 0.12, 1));
			AddInternalChild(popup);
			VsguiSetContentHost(popup);
			popup.AddOpenChanged([](Popup sender, bool value) -> void
			{
				if (value || sender.GetDismissReason() != "outside") return; MenuItem item = VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(sender.GetParent()); if (item == nullptr) return; MenuItem parent = VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(item.GetParent());
				while (parent != nullptr)
				{
					item = parent; parent = VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(item.GetParent());
				}
				item.CloseMenu();
			});
			cast<Button>(this).AddClicked([](Button sender) -> void
			{
				MenuItem item = cast<MenuItem>(sender); if (item.HasSubmenu())
				{
					item.OpenMenu();
				}
				else
				{
					item.Invoke();
				}
			});
			Subscribe("KeyDown", [](Widget sender, Event input) -> void
			{
				cast<MenuItem>(sender).VsguiHandleKey(input);
			});
		}
		@ GuiMethod bool HasSubmenu() const
		{
			return len(popup.GetChildren()) > 0;
		}
		@ GuiMethod bool IsMenuOpen() const
		{
			return popup.IsActuallyOpen();
		}
		@ GuiMethod bool IsActuallyChecked() const
		{
			return checkedReady ? actualChecked : GetChecked();
		}
		@ GuiMethod void OpenMenu()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (IsDisposed() || !HasSubmenu() || !VS::Gui::Internal::IsInteractive(this)) return;
			MenuItem parent = VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(GetParent());
			popup.SetPlacement(parent != nullptr ? "right" : "below");
			for (Widget sibling : VsguiSiblings())
			{
				optional<MenuItem> item = try_cast<MenuItem>(sibling);
				if (item.has_value() && item.get() != this)
				{
					item.get().CloseMenu();
				}
			}
			if (IsDisposed() || !VS::Gui::Internal::IsInteractive(this)) return;
			int64 focusBefore = VsguiFocusedIdentity();
			popup.RequestOpen(true);
			if (IsDisposed() || !popup.IsActuallyOpen() || VsguiFocusedIdentity() != focusBefore) return;
			for (Widget child : popup.GetChildren())
			{
				optional<MenuItem> item = try_cast<MenuItem>(child);
				if (item.has_value() && VS::Gui::Internal::IsInteractive(child))
				{
					if (child.IsMounted())
					{
						item.get().Focus();
					}
					break;
				}
			}
		}
		@ GuiMethod void CloseMenu()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (IsDisposed()) return;
			for (Widget child : popup.GetChildren())
			{
				optional<MenuItem> item = try_cast<MenuItem>(child);
				if (item.has_value())
				{
					item.get().CloseMenu();
				}
			}
			if (!IsDisposed())
			{
				popup.RequestOpen(false);
			}
		}
		@ GuiMethod void Invoke()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (!VS::Gui::Internal::IsInteractive(this)) return;
			if (HasSubmenu())
			{
				OpenMenu();
				return;
			}
			if (GetCheckable())
			{
				bool candidate = !IsActuallyChecked();
				if (!IsCheckedBound())
				{
					SetChecked(candidate);
				}
				actualChecked = candidate;
				checkedReady = true;
				pendingChecked = true;
				VsguiApplyChecked();
				CheckedChanged.broadcast(this, candidate);
				if (IsDisposed()) return;
			}
			string command = GetCommand();
			if (command == "")
			{
				command = GetId();
			}
			if (command == "")
			{
				command = GetText();
			}
			Menu root = VS::Gui::Internal::FindAncestorOrSelf<Menu>(GetParent());
			MenuItem parent = VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(GetParent());
			// Close the chain before callbacks, so a callback can intentionally reopen it.
			while (parent != nullptr)
			{
				parent.popup.RequestOpen(false);
				if (IsDisposed()) return;
				parent = VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(parent.GetParent());
			}
			Invoked.broadcast(this, command);
			if (root != nullptr && !root.IsDisposed())
			{
				root.VsguiInvoke(command);
			}
		}
		void VsguiSynchronizeControl() override
		{
			if (!pendingChecked && !VsguiNeedsControlConfiguration()) return;
			actualChecked = GetChecked();
			checkedReady = true;
			VsguiApplyChecked();
			pendingChecked = false;
			VsguiCommitControlConfiguration();
		}
	private:
		int64 VsguiFocusedIdentity()
		{
			if (!IsMounted()) return 0i64;
			VS::Gui::__NativeWidget focused = this.__GetNativeHandle().GetContext().GetFocus();
			if (focused == nullptr) return 0i64;
			return focused.GetIdentity();
		}
		void VsguiApplyChecked()
		{
			bool checked = GetCheckable() && actualChecked;
			SetClass("gui-checked", checked);
			this.__GetNativeHandle().ConfigureItem(checked, false, true, GetSelectionColor(), false);
		}
		bool VsguiMovePeer(int direction, bool open)
		{
			array<Widget> siblings = VsguiSiblings();
			int64 position = -1;
			for (int64 index = 0; index < len(siblings); index++)
			{
				if (siblings[index] == this)
				{
					position = index;
				}
			}
			for (int64 step = 1; position >= 0 && step < len(siblings); step++)
			{
				int64 index = (position + direction * step + len(siblings)) % len(siblings);
				optional<MenuItem> item = try_cast<MenuItem>(siblings[index]);
				if (!item.has_value() || !VS::Gui::Internal::IsInteractive(item.get()))
				{
					continue;
				}
				item.get().Focus();
				if (open && !item.get().IsDisposed() && item.get().VsguiFocusedIdentity() == item.get().__GetNativeHandle().GetIdentity())
				{
					item.get().OpenMenu();
				}
				return true;
			}
			return false;
		}
		array<Widget> VsguiSiblings()
		{
			Widget parent = GetParent();
			return parent != nullptr ? parent.GetChildren() : array<Widget>();
		}
		void VsguiHandleKey(Event input)
		{
			if (input.DefaultPrevented || VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(input.Target) != this) return;
			MenuItem parent = VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(GetParent());
			bool vertical = parent != nullptr || (GetParent() != nullptr && GetParent().GetLayout() != Layout::Horizontal);
			Menu root = nullptr;
			if (parent != nullptr)
			{
				root = VS::Gui::Internal::FindAncestorOrSelf<Menu>(parent.GetParent());
			}
			if (root != nullptr && root.GetLayout() == Layout::Horizontal && VS::Gui::Internal::FindAncestorOrSelf<MenuItem>(parent.GetParent()) == nullptr && (input.Key == cast<int>(Key::Left) || (input.Key == cast<int>(Key::Right) && !HasSubmenu())))
			{
				if (parent.VsguiMovePeer(input.Key == cast<int>(Key::Left) ? - 1 : 1, true))
				{
					input.PreventDefault();
					input.StopPropagation();
					return;
				}
			}
			if (input.Key == cast<int>(Key::Escape) || (vertical && input.Key == cast<int>(Key::Left)))
			{
				if (parent == nullptr) return;
				parent.CloseMenu();
				if (!parent.IsDisposed() && parent.IsMounted())
				{
					parent.Focus();
				}
			}
			else if ((HasSubmenu() && input.Key == cast<int>(vertical ? Key::Right : Key::Down)))
			{
				OpenMenu();
			}
			else
			{
				int direction = 0;
				if (input.Key == cast<int>(vertical ? Key::Down : Key::Right))
				{
					direction = 1;
				}
				else if (input.Key == cast<int>(vertical ? Key::Up : Key::Left))
				{
					direction = -1;
				}
				if (direction != 0)
				{
					VsguiMovePeer(direction, !vertical && IsMenuOpen());
					input.PreventDefault();
					input.StopPropagation();
					return;
				}
				array<Widget> siblings = VsguiSiblings();
				int64 position = -1;
				for (int64 index = 0; index < len(siblings); index++)
				{
					if (siblings[index] == this)
					{
						position = index;
					}
				}
				if (input.Key == cast<int>(Key::Home))
				{
					position = -1;
					direction = 1;
				}
				if (input.Key == cast<int>(Key::End))
				{
					position = len(siblings);
					direction = -1;
				}
				if (direction == 0) return;
				for (int64 index = position + direction; index >= 0 && index < len(siblings); index += direction)
				{
					optional<MenuItem> item = try_cast<MenuItem>(siblings[index]);
					if (!item.has_value() || !VS::Gui::Internal::IsInteractive(item.get()))
					{
						continue;
					}
					item.get().Focus();
					break;
				}
			}
			input.PreventDefault();
			input.StopPropagation();
		}
		@ GuiProperty(Mark = "command") std::attribute<string> Command = std::attribute<string>("");
		@ GuiProperty(Mark = "checkable") std::attribute<bool> Checkable = std::attribute<bool>(false);
		@ GuiProperty(Mark = "checked") std::attribute<bool> Checked = std::attribute<bool>(false);
		@ GuiProperty(Style, Mark = "selection-color") std::attribute<Color> SelectionColor = std::attribute<Color>(Color(0.08, 0.22, 0.45, 0.85));
		@ GuiEvent(Mark = "invoked") std::multicast_delegate<VS::Gui::MenuItem, string> Invoked = std::multicast_delegate<VS::Gui::MenuItem, string>();
		@ GuiEvent(Mark = "checked-changed") std::multicast_delegate<VS::Gui::MenuItem, bool> CheckedChanged = std::multicast_delegate<VS::Gui::MenuItem, bool>();
		Popup popup;
		bool actualChecked = false;
		bool checkedReady = false;
		bool pendingChecked = false;
	};
}

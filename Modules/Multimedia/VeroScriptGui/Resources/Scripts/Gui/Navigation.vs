// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "Collections.vs";
export import "Controls.vs";
export import "Widget.vs";
export import "Binding.vs";
import "delegate.vs";

namespace VS::Gui
{
	@ GuiWidget(Mark = "combo-box") export class ComboBox : public VS::Gui::Button
	{
	public:
		ComboBox()
		{
			choices = ListView::Create().Height(Length(160));
			popup = Popup::Create().MatchAnchorWidth(true).Background(Color(0.08, 0.09, 0.12, 1));
			popup.AddChild(choices);
			AddInternalChild(popup);
			VsguiSetContentHost(choices);
			cast<Button>(this).AddClicked([](Button sender) -> void
			{
				ComboBox box = cast<ComboBox>(sender); box.RequestOpen(!box.IsActuallyOpen());
			});
			choices.AddSelectionChanged([](ListView sender, string key) -> void
			{
				ComboBox box = VS::Gui::Internal::FindAncestorOrSelf<ComboBox>(sender.GetParent()); if (box != nullptr)
				{
					box.VsguiSelect(key);
				}
			});
			choices.AddItemActivated([](ListView sender, string key) -> void
			{
				ComboBox box = VS::Gui::Internal::FindAncestorOrSelf<ComboBox>(sender.GetParent()); if (box != nullptr)
				{
					box.RequestOpen(false);
				}
			});
			popup.AddOpenChanged([](Popup sender, bool value) -> void
			{
				ComboBox box = VS::Gui::Internal::FindAncestorOrSelf<ComboBox>(sender.GetParent()); if (box != nullptr)
				{
					box.VsguiOpenChanged(value);
				}
			});
			Subscribe("KeyDown", [](Widget sender, Event input) -> void
			{
				ComboBox box = cast<ComboBox>(sender); if (input.DefaultPrevented || input.Target != box) return; if (input.Key == cast<int>(Key::Down) || input.Key == cast<int>(Key::Up))
				{
					box.RequestOpen(true); box.choices.FocusSelected(); if (box.choices.GetActualSelectedKey() == "")
					{
						for (CollectionItem item : box.choices.GetItems())
						{
							if (!VS::Gui::Internal::IsInteractive(item))
							{
								continue;
							}
							item.Focus(); break;
						}
					}
					input.PreventDefault(); input.StopPropagation();
				}
			});
		}
		@ GuiMethod void SetItems(array<string> labels)
		{
			choices.SetItems(labels);
			VsguiRefreshLabel();
		}
		@ GuiMethod ListView GetList() const
		{
			return choices;
		}
		@ GuiMethod bool IsActuallyOpen() const
		{
			return popup.IsActuallyOpen();
		}
		@ GuiMethod string GetActualSelectedKey() const
		{
			return choices.GetActualSelectedKey();
		}
		@ GuiMethod void Select(string key)
		{
			choices.Select(key);
		}
		@ GuiMethod void RequestOpen(bool value)
		{
			popup.RequestOpen(value);
		}
		void VsguiSynchronizeControl() override
		{
			// Restore bound models after native input candidates, including unchanged getters.
			if (popup.GetOpen() != GetOpen())
			{
				popup.SetOpen(GetOpen());
			}
			if (choices.IsSelectedKeyBound() || choices.GetSelectedKey() != GetSelectedKey())
			{
				choices.VsguiSetSelectionModel(GetSelectedKey());
			}
			VsguiRefreshLabel();
		}
	protected:
		void OnInitialized() override
		{
			VsguiRefreshLabel();
		}
		void OnSelectedKeyChanged()
		{
			if (choices != nullptr)
			{
				choices.VsguiSetSelectionModel(GetSelectedKey());
				VsguiRefreshLabel();
			}
		}
		void OnOpenChanged()
		{
			if (popup != nullptr)
			{
				popup.SetOpen(GetOpen());
			}
		}
		void OnPlaceholderChanged()
		{
			if (choices != nullptr)
			{
				VsguiRefreshLabel();
			}
		}
		void OnDropDownHeightChanged()
		{
			double value = GetDropDownHeight();
			assert(value > 0 && value <= 16777216, "Invalid combo drop-down height");
			if (choices != nullptr)
			{
				choices.SetHeight(Length(value));
			}
		}
	private:
		void VsguiSelect(string key)
		{
			if (!IsSelectedKeyBound())
			{
				SetSelectedKey(key);
			}
			VsguiRefreshLabel();
			SelectionChanged.broadcast(this, key);
		}
		void VsguiRefreshLabel()
		{
			string value = "";
			if (!choices.VsguiTryGetItemText(choices.GetActualSelectedKey(), ref value))
			{
				value = GetPlaceholder();
			}
			if (GetText() != value)
			{
				SetText(value);
			}
		}
		void VsguiOpenChanged(bool value)
		{
			if (!IsOpenBound())
			{
				SetOpen(value);
			}
			OpenChanged.broadcast(this, value);
			// The portal restores its previous eligible focus before this callback.
			// A handler may move focus or open another modal; preserve that decision.
		}
		@ GuiProperty(Mark = "selected-key") std::attribute<string> SelectedKey = std::attribute<string>("");
		@ GuiProperty(Mark = "open") std::attribute<bool> Open = std::attribute<bool>(false);
		@ GuiProperty(Mark = "placeholder") std::attribute<string> Placeholder = std::attribute<string>("");
		@ GuiProperty(Style, Mark = "drop-down-height") std::attribute<double> DropDownHeight = std::attribute<double>(160);
		@ GuiEvent(Mark = "selection-changed") std::multicast_delegate<VS::Gui::ComboBox, string> SelectionChanged = std::multicast_delegate<VS::Gui::ComboBox, string>();
		@ GuiEvent(Mark = "open-changed") std::multicast_delegate<VS::Gui::ComboBox, bool> OpenChanged = std::multicast_delegate<VS::Gui::ComboBox, bool>();
		ListView choices;
		Popup popup;
	};

	@ GuiWidget(Mark = "tab") export class TabItem : public VS::Gui::Widget
	{
	public:
		void VsguiSetTabs(TabView value)
		{
			owner = weak<TabView>(value);
		}
		void VsguiReleaseTabs(TabView value)
		{
			if (owner.lock() == value)
			{
				owner = weak<TabView>();
			}
		}
		void VsguiSynchronizeControl() override
		{
			// A detached page can be reused outside a TabView. Never write to a disposing page.
			if (owner.expired())
			{
				this.__GetNativeHandle().SetContentVisible(true);
			}
		}
	protected:
		void OnTitleChanged()
		{
			TabView tabs = owner.lock();
			if (tabs != nullptr)
			{
				tabs.VsguiUpdateHeaders();
			}
		}
		void OnKeyChanged()
		{
			TabView tabs = owner.lock();
			if (tabs != nullptr)
			{
				tabs.VsguiRefreshKeys();
			}
		}
	private:
		@ GuiProperty(Mark = "key") std::attribute<string> Key = std::attribute<string>("");
		@ GuiProperty(Mark = "title") std::attribute<string> Title = std::attribute<string>("");
		weak<TabView> owner;
	};

	@ GuiWidget(Mark = "tabs") export class TabView : public VS::Gui::Widget
	{
	public:
		TabView()
		{
			headers = Stack::Create().Layout(Layout::Horizontal);
			pages = VS::Gui::Internal::ContentHost::Create();
			AddInternalChild(headers);
			AddInternalChild(pages);
			VsguiSetContentHost(pages);
			pages.VsguiObserve([](VS::Gui::Internal::ContentHost sender) -> void
			{
				TabView tabs = VS::Gui::Internal::FindAncestorOrSelf<TabView>(sender.GetParent()); if (tabs != nullptr && tabs.IsInitialized())
				{
					tabs.RefreshTabs();
				}
			});
			headers.Subscribe("KeyDown", [](Widget sender, Event input) -> void
			{
				TabView tabs = VS::Gui::Internal::FindAncestorOrSelf<TabView>(sender.GetParent()); if (tabs != nullptr)
				{
					tabs.VsguiHandleKey(input);
				}
			});
		}
		@ GuiMethod void RefreshTabs()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (refreshing || IsDisposed()) return;
			tabsDirty = true;
			try
			{
				refreshing = true;
				map<string, weak<TabItem>> next = map<string, weak<TabItem>>();
				array<Widget> buttons = array<Widget>();
				array<string> order = array<string>();
				for (Widget child : pages.GetChildren())
				{
					optional<TabItem> entry = try_cast<TabItem>(child);
					assert(entry.has_value(), "TabView children must be TabItem");
					TabItem page = entry.get();
					string key = page.GetKey();
					if (key == "")
					{
						key = page.GetId();
					}
					if (key == "")
					{
						key = to_string(len(order));
					}
					assert(!next.has(key), "Tab keys must be unique: " + key);
					next[key] = weak<TabItem>(page);
					order.push(key);
					Button button = Button::Create().Text(page.GetTitle());
					button.AddClicked([](Button sender) -> void
					{
						TabView tabs = VS::Gui::Internal::FindAncestorOrSelf<TabView>(sender.GetParent()); if (tabs != nullptr)
						{
							tabs.VsguiSelectHeader(sender);
						}
					});
					buttons.push(button);
				}
				for (string key : keys)
				{
					TabItem old = entries[key].lock();
					if (old != nullptr && !old.IsDisposed())
					{
						old.VsguiReleaseTabs(this);
					}
				}
				entries = next;
				keys = order;
				selectionReady = false;
				headers.SetChildren(buttons);
				for (string key : keys)
				{
					entries[key].lock().VsguiSetTabs(this);
				}
				string wanted = GetSelectedKey();
				if (!IsSelectedKeyBound() && (!entries.has(wanted) || wanted == ""))
				{
					wanted = "";
					for (string key : keys)
					{
						TabItem page = entries[key].lock();
						if (page.GetEnabled() && page.GetVisibility() == Visibility::Visible)
						{
							wanted = key;
							break;
						}
					}
					SetSelectedKey(wanted);
				}
				VsguiApplySelection(wanted);
				VsguiUpdateHeaders();
				tabsDirty = false;
			}
			finally
			{
				refreshing = false;
			}
		}
		@ GuiMethod string GetActualSelectedKey() const
		{
			return actualKey;
		}
		void VsguiRefreshKeys()
		{
			if (refreshing || pages == nullptr) return;
			tabsDirty = true;
			map<string, weak<TabItem>> next = map<string, weak<TabItem>>();
			array<string> order = array<string>();
			for (Widget child : pages.GetChildren())
			{
				TabItem page = cast<TabItem>(child);
				string key = page.GetKey();
				if (key == "")
				{
					key = page.GetId();
				}
				if (key == "")
				{
					key = to_string(len(order));
				}
				assert(!next.has(key), "Tab keys must be unique: " + key);
				next[key] = weak<TabItem>(page);
				order.push(key);
			}
			entries = next;
			keys = order;
			selectionReady = false;
			VsguiApplySelection(GetSelectedKey());
			tabsDirty = false;
		}
		void VsguiSelectHeader(Button button)
		{
			for (int64 index = 0; index < len(keys); index++)
			{
				if (headers.GetChild(index) == button)
				{
					Select(keys[index]);
					return;
				}
			}
		}
		@ GuiMethod void Select(string key)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			TabItem page = nullptr;
			if (entries.has(key))
			{
				page = entries[key].lock();
			}
			assert(page != nullptr && page.GetEnabled() && page.GetVisibility() == Visibility::Visible, "Invalid tab selection");
			if (actualKey == key) return;
			if (!IsSelectedKeyBound())
			{
				SetSelectedKey(key);
			}
			VsguiApplySelection(key);
			SelectionChanged.broadcast(this, key);
		}
		void VsguiUpdateHeaders()
		{
			// Header topology has its own native revision. Keep weak views so
			// stable rounds need no per-page native GetChild/wrap operation,
			// while external replacement/removal is still observed.
			uint64 token = headers.__GetNativeHandle().GetMutationToken();
			if (token == 0u64 || token != headerViewsToken)
			{
				array<weak<Widget>> next = array<weak<Widget>>();
				for (Widget button : headers.GetChildren())
				{
					next.push(weak<Widget>(button));
				}
				headerViews = next;
				headerViewsToken = token;
			}
			bool safe = VS::Gui::PropertyEvaluation::SafeDepth > 0;
			for (int64 index = 0; index < len(keys); index++)
			{
				TabItem page = entries[keys[index]].lock();
				if (page == nullptr)
				{
					continue;
				}
				Widget button = nullptr;
				if (index < len(headerViews))
				{
					button = headerViews[index].lock();
				}
				if (button == nullptr)
				{
					button = headers.GetChild(index);
					if (index < len(headerViews))
					{
						headerViews[index] = weak<Widget>(button);
					}
				}
				// Keep the source/destination getter order. Inside a safe
				// synchronization round each source already has one value;
				// direct calls retain their ordinary getter evaluation rules.
				string text = button.GetText();
				string title = page.GetTitle();
				if (text != title)
				{
					button.SetText(safe ? title : page.GetTitle());
				}
				bool enabled = button.GetEnabled();
				bool pageEnabled = page.GetEnabled();
				if (enabled != pageEnabled)
				{
					button.SetEnabled(safe ? pageEnabled : page.GetEnabled());
				}
				Visibility visibility = button.GetVisibility();
				Visibility pageVisibility = page.GetVisibility();
				if (visibility != pageVisibility)
				{
					button.SetVisibility(safe ? pageVisibility : page.GetVisibility());
				}
			}
		}
		void VsguiSynchronizeControl() override
		{
			if (IsInitialized() && tabsDirty)
			{
				VsguiRefreshKeys();
			}
			VsguiApplySelection(GetSelectedKey());
			VsguiUpdateHeaders();
		}
	protected:
		void OnInitialized() override
		{
			RefreshTabs();
		}
		void OnSelectedKeyChanged()
		{
			if (pages != nullptr)
			{
				VsguiApplySelection(GetSelectedKey());
			}
		}
	private:
		void VsguiApplySelection(string key)
		{
			string next = entries.has(key) ? key : "";
			if (actualKey == next && selectionReady) return;
			for (int64 index = 0; index < len(keys); index++)
			{
				TabItem page = entries[keys[index]].lock();
				if (page == nullptr || page.IsDisposed())
				{
					continue;
				}
				bool active = keys[index] == next;
				page.__GetNativeHandle().SetContentVisible(active);
				headers.GetChild(index).SetClass("gui-selected", active);
			}
			actualKey = next;
			selectionReady = true;
		}
		void VsguiHandleKey(Event input)
		{
			if (input.DefaultPrevented || !VS::Gui::Internal::IsWithin(input.Target, headers)) return;
			int64 position = -1;
			for (int64 index = 0; index < len(keys); index++)
			{
				if (VS::Gui::Internal::IsWithin(input.Target, headers.GetChild(index)))
				{
					position = index;
				}
			}
			int direction = 0;
			if (input.Key == cast<int>(Key::Left))
			{
				direction = -1;
			}
			else if (input.Key == cast<int>(Key::Right))
			{
				direction = 1;
			}
			else if (input.Key == cast<int>(Key::Home))
			{
				position = -1;
				direction = 1;
			}
			else if (input.Key == cast<int>(Key::End))
			{
				position = len(keys);
				direction = -1;
			}
			else
			{
				return;
			}
			for (int64 index = position + direction; index >= 0 && index < len(keys); index += direction)
			{
				Widget button = headers.GetChild(index);
				if (!VS::Gui::Internal::IsInteractive(button))
				{
					continue;
				}
				string key = keys[index];
				cast<Button>(button).Focus();
				if (!IsDisposed() && entries.has(key))
				{
					Select(key);
				}
				break;
			}
			input.PreventDefault();
			input.StopPropagation();
		}
		@ GuiProperty(Mark = "selected-key") std::attribute<string> SelectedKey = std::attribute<string>("");
		@ GuiEvent(Mark = "selection-changed") std::multicast_delegate<VS::Gui::TabView, string> SelectionChanged = std::multicast_delegate<VS::Gui::TabView, string>();
		Stack headers;
		VS::Gui::Internal::ContentHost pages;
		array<string> keys;
		map<string, weak<TabItem>> entries;
		array<weak<Widget>> headerViews;
		uint64 headerViewsToken = 0u64;
		string actualKey;
		bool refreshing = false;
		bool selectionReady = false;
		bool tabsDirty = true;
	};
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "CompositeSupport.vs";
export import "Controls.vs";
export import "Widget.vs";
export import "Binding.vs";
export import "Virtualization.vs";
import "delegate.vs";

namespace VS::Gui
{
	@ GuiWidget export class CollectionItem : public VS::Gui::Button
	{
	public:
		CollectionItem()
		{
			cast<Button>(this).AddClicked([](Button sender) -> void
			{
				CollectionItem item = cast<CollectionItem>(sender); VS::Gui::Internal::SelectionView view = item.selectionOwner.lock(); if (view != nullptr)
				{
					view.VsguiActivateItem(item);
				}
			});
		}

		@ GuiMethod bool IsSelected() const
		{
			return selected;
		}
		@ GuiMethod string GetEffectiveKey() const
		{
			return effectiveKey;
		}
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			VsguiUpdateItemSkin();
			VsguiCommitControlConfiguration();
		}

		void VsguiSetSelectionOwner(VS::Gui::Internal::SelectionView value, string key)
		{
			bool changed = selectionOwner.lock() != value;
			selectionOwner = weak<VS::Gui::Internal::SelectionView>(value);
			effectiveKey = key;
			if (changed || value == nullptr)
			{
				selected = false;
				skinReady = false;
				VsguiControlConfigurationDirty = true;
			}
		}
		void VsguiReleaseSelectionOwner(VS::Gui::Internal::SelectionView value)
		{
			if (selectionOwner.lock() == value)
			{
				VsguiSetSelectionOwner(nullptr, "");
			}
		}
		void VsguiSetSelected(VS::Gui::Internal::SelectionView view, bool value)
		{
			if (selectionOwner.lock() != view) return;
			if (IsDisposed() || (skinReady && selected == value)) return;
			selected = value;
			VsguiUpdateItemSkin();
		}
		void VsguiNotifyItemsChanged()
		{
			VS::Gui::Internal::SelectionView view = selectionOwner.lock();
			if (view != nullptr)
			{
				view.VsguiInvalidateItems();
				view.RefreshItems();
			}
		}
		virtual bool VsguiExpandable() const
		{
			return false;
		}
		virtual bool VsguiHasIndicator() const
		{
			return false;
		}
		virtual bool VsguiExpanded() const
		{
			return true;
		}
		virtual bool VsguiNavigate(Key key)
		{
			return false;
		}

	protected:
		void VsguiUpdateItemSkin()
		{
			if (IsDisposed()) return;
			this.__GetNativeHandle().ConfigureItem(selected, VsguiExpandable(), VsguiExpanded(), GetSelectionColor(), VsguiHasIndicator());
			SetClass("gui-selected", selected);
			skinReady = true;
		}
		void OnKeyChanged()
		{
			// Realization publishes the new index before deferred property hooks run.
			// A committed literal key needs no second rebuild; bound keys still poll.
			if (!IsKeyBound() && effectiveKey != "" && GetKey() == effectiveKey) return;
			VsguiNotifyItemsChanged();
		}
		void OnSelectionColorChanged()
		{
			VsguiUpdateItemSkin();
		}

	private:
		@ GuiProperty(Mark = "key") std::attribute<string> Key = std::attribute<string>("");
		@ GuiProperty(Style, Mark = "selection-color") std::attribute<Color> SelectionColor = std::attribute<Color>(Color(0.08, 0.22, 0.45, 0.85));
		weak<VS::Gui::Internal::SelectionView> selectionOwner;
		string effectiveKey;
		bool selected = false;
		bool skinReady = false;
	};

	// Shared selection bookkeeping; all scene objects remain ordinary widgets.
	namespace Internal
	{
		// The virtual source owns its snapshot; a retained, disposed view must
		// not keep the source's data alive after VirtualRows releases its factory.
		export class VirtualListData
		{
		public:
			array<string> Labels;
			map<string, int64> Indices;
		};

		@ GuiWidget export class SelectionView : public VS::Gui::ScrollView
		{
		public:
			SelectionView()
			{
				content = VS::Gui::Internal::ContentHost::Create().Gap(0).Padding(Insets()).Height(Length());
				AddInternalChild(content);
				VsguiSetContentHost(content);
				content.VsguiObserve([](VS::Gui::Internal::ContentHost sender) -> void
				{
					SelectionView view = VS::Gui::Internal::FindAncestorOrSelf<SelectionView>(sender.GetParent()); if (view != nullptr && view.IsInitialized())
					{
						view.VsguiInvalidateItems();
						view.RefreshItems();
					}
				});
				Subscribe("KeyDown", [](Widget sender, Event input) -> void
				{
					cast<SelectionView>(sender).VsguiHandleKey(input);
				});
				cast<ScrollView>(this).AddScrolled([](ScrollView sender, double x, double y) -> void
				{
					SelectionView view = cast<SelectionView>(sender); if (view.virtualRows != nullptr)
					{
						view.virtualRows.Schedule();
					}
				});
			}

			void VsguiInvalidateItems()
			{
				itemsDirty = true;
			}
			@ GuiMethod void RefreshItems()
			{
				assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
				if (refreshing || IsDisposed() || VsguiApplyingVirtualRows()) return;
				bool completed = false;
				try
				{
					refreshing = true;
					itemsDirty = false;
					array<CollectionItem> next = array<CollectionItem>();
					map<string, weak<CollectionItem>> nextKeys = map<string, weak<CollectionItem>>();
					array<Widget> pending = array<Widget>();
					array<Widget> roots = content.GetChildren();
					for (int64 index = len(roots); index > 0; index--)
					{
						pending.push(roots[index - 1]);
					}
					while (len(pending) > 0)
					{
						Widget node = pending[len(pending) - 1];
						pending.remove_at(len(pending) - 1);
						optional<SelectionView> nested = try_cast<SelectionView>(node);
						if (nested.has_value())
						{
							continue;
						}
						optional<CollectionItem> candidate = try_cast<CollectionItem>(node);
						if (candidate.has_value())
						{
							CollectionItem item = candidate.get();
							string key = item.GetKey();
							if (key == "")
							{
								key = item.GetId();
							}
							if (key == "")
							{
								key = to_string(len(next));
							}
							assert(!nextKeys.has(key), "Collection item keys must be unique: " + key);
							nextKeys[key] = weak<CollectionItem>(item);
							next.push(item);
						}
						array<Widget> children = node.GetChildren();
						for (int64 index = len(children); index > 0; index--)
						{
							pending.push(children[index - 1]);
						}
					}
					// Validate the complete candidate before publishing keys or owners.
					for (weak<CollectionItem> old : items)
					{
						CollectionItem item = old.lock();
						if (item != nullptr && !item.IsDisposed())
						{
							item.VsguiReleaseSelectionOwner(this);
						}
					}
					items = array<weak<CollectionItem>>();
					keys = nextKeys;
					for (CollectionItem item : next)
					{
						string key = item.GetKey();
						if (key == "")
						{
							key = item.GetId();
						}
						if (key == "")
						{
							key = to_string(len(items));
						}
						item.VsguiSetSelectionOwner(this, key);
						items.push(weak<CollectionItem>(item));
					}
					string key = GetSelectedKey();
					if (key != "" && !VsguiHasItemKey(key) && !IsSelectedKeyBound())
					{
						SetSelectedKey("");
					}
					VsguiApplySelection(GetSelectedKey());
					VsguiItemsChanged();
					// Only a real notification during the hook requests another
					// refresh. A nested manual refresh or Clear(nil) remains a no-op.
					completed = true;
				}
				finally
				{
					if (!completed)
					{
						itemsDirty = true;
					}
					refreshing = false;
				}
			}

			@ GuiMethod array<CollectionItem> GetItems() const
			{
				array<CollectionItem> result = array<CollectionItem>();
				for (weak<CollectionItem> entry : items)
				{
					CollectionItem item = entry.lock();
					if (item != nullptr && !item.IsDisposed())
					{
						result.push(item);
					}
				}
				return result;
			}
			@ GuiMethod string GetActualSelectedKey() const
			{
				return actualKey;
			}
			@ GuiMethod int64 GetItemCount() const
			{
				return virtualRows != nullptr ? virtualRows.Count() : len(items);
			}
			@ GuiMethod bool IsVirtualized() const
			{
				return virtualRows != nullptr;
			}
			@ GuiMethod int64 GetRealizedItemCount() const
			{
				return len(items);
			}
			@ GuiMethod void SetVirtualSource(array<string> source, double rowHeight, fn(string, CollectionItem) -> CollectionItem factory, int64 overscan = 2)
			{
				VsguiSetVirtualSource(source, rowHeight, factory, overscan, false);
			}
		protected:
			void VsguiSetVirtualSource(array<string> source, double rowHeight, fn(string, CollectionItem) -> CollectionItem factory, int64 overscan, bool builtin)
			{
				assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
				assert(VsguiSupportsVirtualRows(), "This control does not support a virtual row source");
				assert(!IsDisposed() && !VsguiApplyingVirtualRows(), "A virtual source cannot be replaced during realization");
				weak<SelectionView> owner = weak<SelectionView>(this);
				VS::Gui::Internal::VirtualRows next = VS::Gui::Internal::VirtualRows(this, content, source, rowHeight, overscan, [owner, factory](string key, Widget previous) mutable -> Widget
				{
					SelectionView view = owner.lock(); assert(view != nullptr && !view.IsDisposed(), "Virtual view was disposed during realization"); CollectionItem recycled = nullptr; if (previous != nullptr)
					{
						recycled = cast<CollectionItem>(previous);
					}
					if (recycled != nullptr && recycled.GetParent() == nullptr)
					{
						recycled.VsguiReleaseSelectionOwner(view);
					}
					CollectionItem row = factory(key, recycled); assert(row != nullptr && view.VsguiAcceptVirtualRow(row), "Invalid virtual row type"); row.SetKey(key); return row;
				}, [owner](array<Widget> children, bool commit) mutable -> void
				{
					SelectionView view = owner.lock(); if (view == nullptr || view.IsDisposed()) return; if (!commit)
					{
						view.content.SetChildren(children);
					}
					else
					{
						view.RefreshItems();
					}
				});
				string previousKey = GetSelectedKey();
				if (!VsguiClearSourceForReplacement())
				{
					next.Dispose();
					return;
				}
				int64 revision = virtualSourceRevision;
				VsguiConfigureVirtualSource(builtin);
				if (IsDisposed() || virtualSourceRevision != revision)
				{
					next.Dispose();
					return;
				}
				virtualRows = next;
				bool retained = false;
				try
				{
					if (!IsSelectedKeyBound() && GetSelectedKey() == "" && next.Has(previousKey))
					{
						SetSelectedKey(previousKey);
					}
					if (IsDisposed() || virtualRows != next || virtualSourceRevision != revision) return;
					SetFocusable(true);
					if (IsDisposed() || virtualRows != next || virtualSourceRevision != revision) return;
					VsguiRetainBindings(next);
					retained = true;
					next.Update();
				}
				finally
				{
					if (!retained || IsDisposed() || virtualRows != next || virtualSourceRevision != revision)
					{
						// Publish cancellation before releasing a factory that may reenter.
						if (virtualRows == next)
						{
							virtualRows = nullptr;
						}
						VsguiRemoveRetainedBindings(next);
						next.Dispose();
					}
				}
			}
		public:
			@ GuiMethod void ClearVirtualSource()
			{
				assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
				assert(!VsguiApplyingVirtualRows(), "A virtual source cannot be cleared during realization");
				virtualSourceRevision++;
				if (virtualRows == nullptr) return;
				VS::Gui::Internal::VirtualRows previous = virtualRows;
				virtualRows = nullptr;
				VsguiRemoveRetainedBindings(previous);
				try
				{
					content.SetChildren(array<Widget>());
				}
				finally
				{
					previous.Dispose();
				}
			}
			@ GuiMethod void RefreshVirtualItems()
			{
				assert(VS::Gui::PropertyEvaluation::Depth == 0 && !IsDisposed(), "Invalid virtual item mutation");
				assert(virtualRows != nullptr, "No virtual row source");
				virtualRows.Refresh();
			}
			@ GuiMethod void ScrollToItem(string key)
			{
				assert(VS::Gui::PropertyEvaluation::Depth == 0 && !IsDisposed(), "Invalid virtual item mutation");
				assert(virtualRows != nullptr && virtualRows.Has(key), "Unknown virtual row key");
				virtualRows.ScrollToIndex(virtualRows.IndexOf(key));
			}
			@ GuiMethod void SetVirtualOrder(array<string> source)
			{
				assert(VS::Gui::PropertyEvaluation::Depth == 0 && !IsDisposed(), "Invalid virtual item mutation");
				assert(virtualRows != nullptr, "No virtual row source");
				virtualRows.Reorder(source);
			}
			void VsguiSetSelectionModel(string key)
			{
				SetSelectedKey(key);
				VsguiApplySelection(key);
			}
			void VsguiSynchronizeControl() override
			{
				VsguiSynchronizeSelection();
			}
		protected:
			void VsguiSynchronizeSelection()
			{
				if (IsInitialized() && itemsDirty)
				{
					RefreshItems();
				}
				VsguiSynchronizeScrolling();
				// A bound selection remains authoritative after an input candidate.
				VsguiApplySelection(GetSelectedKey());
			}
		public:
			@ GuiMethod CollectionItem FindItem(string key) const
			{
				if (!keys.has(key)) return nullptr;
				CollectionItem item = keys[key].lock();
				if (item == nullptr || item.IsDisposed() || item.GetEffectiveKey() != key) return nullptr;
				return item;
			}
			@ GuiMethod void Select(string key)
			{
				assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
				CollectionItem item = FindItem(key);
				assert(key == "" || VsguiHasItemKey(key), "Invalid collection selection");
				if (actualKey == key) return;
				if (!IsSelectedKeyBound())
				{
					SetSelectedKey(key);
				}
				VsguiApplySelection(key);
				VsguiSelectionChanged(key);
			}
			@ GuiMethod void Activate(string key)
			{
				assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
				CollectionItem item = FindItem(key);
				assert(item != nullptr && VS::Gui::Internal::IsInteractive(item), "Invalid collection item");
				VsguiActivateItem(item);
			}
			@ GuiMethod void FocusSelected()
			{
				assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
				CollectionItem item = FindItem(actualKey);
				if (item != nullptr && VS::Gui::Internal::IsInteractive(item) && item.IsMounted())
				{
					item.Focus();
				}
			}
			void VsguiActivateItem(CollectionItem item)
			{
				if (item == nullptr || item.IsDisposed() || !VS::Gui::Internal::IsInteractive(item)) return;
				string key = item.GetEffectiveKey();
				if (FindItem(key) != item) return;
				Select(key);
				if (!IsDisposed() && !item.IsDisposed() && FindItem(key) == item)
				{
					VsguiItemActivated(key);
				}
			}

		protected:
			bool VsguiClearSourceForReplacement()
			{
				int64 revision = virtualSourceRevision + 1;
				ClearVirtualSource();
				// A nested replacement or even a nested clear wins over this
				// pending operation, including authored/null-to-null changes.
				return !IsDisposed() && virtualSourceRevision == revision;
			}
			virtual bool VsguiSupportsVirtualRows() const
			{
				return false;
			}
			virtual void VsguiConfigureVirtualSource(bool builtin)
			{
			}
			virtual bool VsguiAcceptVirtualRow(CollectionItem row) const
			{
				return true;
			}
			bool VsguiApplyingVirtualRows() const
			{
				return virtualRows != nullptr && virtualRows.IsApplying();
			}
			bool VsguiIsVirtualSpacer(Widget item) const
			{
				return virtualRows != nullptr && virtualRows.IsSpacer(item);
			}
			VS::Gui::Internal::VirtualRows VsguiGetVirtualRows() const
			{
				return virtualRows;
			}
			void OnLayoutChanged() override
			{
				if (virtualRows != nullptr)
				{
					virtualRows.Schedule();
				}
			}
			void OnScrollYChanged()
			{
				if (virtualRows != nullptr)
				{
					virtualRows.Schedule();
				}
			}
			void OnInitialized() override
			{
				RefreshItems();
			}
			virtual void VsguiSelectionChanged(string key)
			{
			}
			virtual void VsguiItemActivated(string key)
			{
			}
			virtual void VsguiItemsChanged()
			{
			}
			void OnSelectedKeyChanged()
			{
				VsguiApplySelection(GetSelectedKey());
			}

			void VsguiApplySelection(string key)
			{
				CollectionItem next = FindItem(key);
				CollectionItem previous = selected.lock();
				if (previous != next)
				{
					if (previous != nullptr && !previous.IsDisposed())
					{
						previous.VsguiSetSelected(this, false);
					}
					if (next != nullptr && !next.IsDisposed())
					{
						next.VsguiSetSelected(this, true);
					}
					selected = weak<CollectionItem>(next);
				}
				else if (next != nullptr)
				{
					next.VsguiSetSelected(this, true);
				}
				actualKey = VsguiHasItemKey(key) ? key : "";
			}
			bool VsguiHasItemKey(string key) const
			{
				return virtualRows != nullptr ? virtualRows.Has(key) : FindItem(key) != nullptr;
			}
			void VsguiHandleKey(Event input)
			{
				if (input.DefaultPrevented || VS::Gui::Internal::IsTextInput(input.Target)) return;
				SelectionView nearest = VS::Gui::Internal::FindAncestorOrSelf<SelectionView>(input.Target);
				if (nearest != this) return;
				int key = input.Key;
				if (virtualRows != nullptr)
				{
					if (key == cast<int>(Key::Enter) || key == cast<int>(Key::Space))
					{
						// A focused row keeps Button's key-down/key-up activation,
						// including its Clicked handlers and collection selection.
						CollectionItem target = VS::Gui::Internal::FindAncestorOrSelf<CollectionItem>(input.Target);
						if (target != nullptr && FindItem(target.GetEffectiveKey()) == target) return;
						VS::Gui::Internal::VirtualRows source = virtualRows;
						string selectedKey = actualKey;
						if (FindItem(selectedKey) == nullptr && source.Has(selectedKey))
						{
							source.ScrollToIndex(source.IndexOf(selectedKey), true);
							if (!IsDisposed() && virtualRows == source)
							{
								source.Update();
							}
						}
						if (IsDisposed() || virtualRows != source || actualKey != selectedKey)
						{
							input.PreventDefault();
							input.StopPropagation();
							return;
						}
						CollectionItem selected = FindItem(actualKey);
						if (selected != nullptr && VS::Gui::Internal::IsInteractive(selected))
						{
							selected.Click();
						}
						input.PreventDefault();
						input.StopPropagation();
						return;
					}
					int64 index = virtualRows.IndexOf(actualKey);
					if (key == cast<int>(Key::Home))
					{
						index = 0;
					}
					else if (key == cast<int>(Key::End))
					{
						index = virtualRows.Count() - 1;
					}
					else if (key == cast<int>(Key::Down))
					{
						index = min(index + 1, virtualRows.Count() - 1);
					}
					else if (key == cast<int>(Key::Up))
					{
						index = max(0i64, index - 1);
					}
					else
					{
						return;
					}
					if (index >= 0 && index < virtualRows.Count())
					{
						VS::Gui::Internal::VirtualRows source = virtualRows;
						this.__GetNativeHandle().FocusControl();
						int64 direction = key == cast<int>(Key::End) || key == cast<int>(Key::Up) ? - 1i64 : 1i64;
						while (!IsDisposed() && virtualRows == source && index >= 0 && index < source.Count())
						{
							string selectedKey = source.KeyAt(index);
							CollectionItem item = cast<CollectionItem>(source.ResolveInputRow(index));
							if (IsDisposed() || virtualRows != source)
							{
								break;
							}
							if (item != nullptr && VS::Gui::Internal::IsInteractive(item))
							{
								Select(selectedKey);
								if (!IsDisposed() && virtualRows == source && source.Has(selectedKey))
								{
									source.ScrollToIndex(source.IndexOf(selectedKey), true);
								}
								break;
							}
							index += direction;
						}
					}
					input.PreventDefault();
					input.StopPropagation();
					return;
				}
				CollectionItem current = VS::Gui::Internal::FindAncestorOrSelf<CollectionItem>(input.Target);
				if (current == nullptr)
				{
					current = FindItem(actualKey);
				}
				if (current != nullptr && (key == cast<int>(VS::Gui::Key::Left) || key == cast<int>(VS::Gui::Key::Right)) && current.VsguiNavigate(cast<VS::Gui::Key>(key)))
				{
					input.PreventDefault();
					input.StopPropagation();
					return;
				}
				if (key != cast<int>(VS::Gui::Key::Home) && key != cast<int>(VS::Gui::Key::End) && key != cast<int>(VS::Gui::Key::Down) && key != cast<int>(VS::Gui::Key::Up)) return;
				array<CollectionItem> visible = array<CollectionItem>();
				int64 position = -1;
				for (CollectionItem item : GetItems())
				{
					if (!VS::Gui::Internal::IsInteractive(item))
					{
						continue;
					}
					if (item == current)
					{
						position = len(visible);
					}
					visible.push(item);
				}
				if (len(visible) == 0) return;
				int64 target = position;
				if (key == cast<int>(VS::Gui::Key::Home))
				{
					target = 0;
				}
				else if (key == cast<int>(VS::Gui::Key::End))
				{
					target = len(visible) - 1;
				}
				else if (key == cast<int>(VS::Gui::Key::Down))
				{
					target = min(position + 1, len(visible) - 1);
				}
				else if (key == cast<int>(VS::Gui::Key::Up))
				{
					target = max(0i64, position - 1);
				}
				else
				{
					return;
				}
				CollectionItem item = visible[target];
				string selectedKey = item.GetEffectiveKey();
				if (item.IsMounted())
				{
					item.Focus();
				}
				if (!IsDisposed() && !item.IsDisposed() && FindItem(selectedKey) == item)
				{
					Select(selectedKey);
				}
				input.PreventDefault();
				input.StopPropagation();
			}

			VS::Gui::Internal::ContentHost content;
		private:
			@ GuiProperty(Mark = "selected-key") std::attribute<string> SelectedKey = std::attribute<string>("");
			array<weak<CollectionItem>> items;
			map<string, weak<CollectionItem>> keys;
			weak<CollectionItem> selected;
			string actualKey;
			bool refreshing = false;
			bool itemsDirty = true;
			int64 virtualSourceRevision = 0;
			VS::Gui::Internal::VirtualRows virtualRows;
		};
	}

	@ GuiWidget(Mark = "list-item") export class ListItem : public VS::Gui::CollectionItem
	{
	};

	@ GuiWidget(Mark = "list-view") export class ListView : public VS::Gui::Internal::SelectionView
	{
	public:
		optional<string> VsguiGetItemText(string key) const
		{
			string text = "";
			if (VsguiTryGetItemText(key, ref text)) return optional<string>(text);
			return optional<string>();
		}
		bool VsguiTryGetItemText(string key, ref string text) const
		{
			CollectionItem item = FindItem(key);
			if (item != nullptr)
			{
				text = item.GetText();
				return true;
			}
			// Built-in virtual data remains available without realizing an offscreen row.
			VS::Gui::Internal::VirtualListData data = virtualData.lock();
			if (IsVirtualized() && virtualDefaults && data != nullptr && data.Indices.has(key))
			{
				text = data.Labels[data.Indices[key]];
				return true;
			}
			return false;
		}
		@ GuiMethod void SetVirtualItems(array<string> labels, double rowHeight = 28, int64 overscan = 2)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0 && !IsDisposed() && !VsguiApplyingVirtualRows(), "Invalid virtual item mutation");
			VS::Gui::Internal::VirtualRows::ValidateExtent(len(labels), rowHeight, overscan);
			VS::Gui::Internal::VirtualListData data = VS::Gui::Internal::VirtualListData();
			for (string label : labels)
			{
				data.Labels.push(label);
			}
			array<string> source = array<string>();
			for (int64 index = 0; index < len(labels); index++)
			{
				string key = to_string(index);
				source.push(key);
				data.Indices[key] = index;
			}
			VS::Gui::Internal::VirtualRows previousSource = VsguiGetVirtualRows();
			weak<VS::Gui::Internal::VirtualListData> previousData = virtualData;
			virtualData = weak<VS::Gui::Internal::VirtualListData>(data);
			try
			{
				VsguiSetVirtualSource(source, rowHeight, [data](string key, CollectionItem previous) mutable -> CollectionItem
				{
					ListItem item = nullptr; if (previous != nullptr)
					{
						item = cast<ListItem>(previous);
					}
					else
					{
						item = ListItem::Create();
					}
					item.SetText(data.Labels[data.Indices[key]]); return item;
				}, overscan, true);
			}
			catch (...)
			{
				if (VsguiGetVirtualRows() == previousSource)
				{
					virtualData = previousData;
				}
				throw;
			}
		}
		@ GuiMethod void SetVirtualItemText(int64 index, string text)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0 && !IsDisposed(), "Invalid virtual item mutation");
			VS::Gui::Internal::VirtualListData data = virtualData.lock();
			assert(IsVirtualized() && virtualDefaults && data != nullptr && index >= 0 && index < len(data.Labels), "Invalid virtual item index");
			data.Labels[index] = text;
			RefreshVirtualItems();
		}
		@ GuiMethod void SetItems(array<string> labels)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			assert(!IsDisposed(), "Invalid collection mutation");
			if (!VsguiClearSourceForReplacement()) return;
			virtualDefaults = false;
			virtualData = weak<VS::Gui::Internal::VirtualListData>();
			array<Widget> rows = array<Widget>();
			for (int64 index = 0; index < len(labels); index++)
			{
				rows.push(VS::Gui::ListItem::Create().Key(to_string(index)).Text(labels[index]));
			}
			SetChildren(rows);
		}
	protected:
		bool VsguiSupportsVirtualRows() const override
		{
			return true;
		}
		bool VsguiAcceptVirtualRow(CollectionItem row) const override
		{
			return true;
		}
		void VsguiConfigureVirtualSource(bool builtin) override
		{
			virtualDefaults = builtin;
		}
		void VsguiSelectionChanged(string key) override
		{
			SelectionChanged.broadcast(this, key);
		}
		void VsguiItemActivated(string key) override
		{
			ItemActivated.broadcast(this, key);
		}
	private:
		weak<VS::Gui::Internal::VirtualListData> virtualData;
		bool virtualDefaults = false;
		@ GuiEvent(Mark = "selection-changed") std::multicast_delegate<VS::Gui::ListView, string> SelectionChanged = std::multicast_delegate<VS::Gui::ListView, string>();
		@ GuiEvent(Mark = "item-activated") std::multicast_delegate<VS::Gui::ListView, string> ItemActivated = std::multicast_delegate<VS::Gui::ListView, string>();
	};

	@ GuiWidget(Mark = "tree-item") export class TreeItem : public VS::Gui::CollectionItem
	{
	public:
		TreeItem()
		{
			SetText("Item");
			SetWhiteSpace("nowrap");
			branch = VS::Gui::Internal::ContentHost::Create();
			AddInternalChild(branch);
			VsguiSetContentHost(branch);
			branch.VsguiObserve([](VS::Gui::Internal::ContentHost sender) -> void
			{
				TreeItem item = VS::Gui::Internal::FindAncestorOrSelf<TreeItem>(sender.GetParent()); if (item != nullptr && item.IsInitialized())
				{
					item.VsguiUpdateBranch(); item.VsguiNotifyItemsChanged();
				}
			});
			Subscribe("PointerDown", [](Widget sender, Event input) -> void
			{
				TreeItem item = cast<TreeItem>(sender); if (input.Button != 1 || input.DefaultPrevented) return; if (VS::Gui::Internal::FindAncestorOrSelf<CollectionItem>(input.Target) != item) return; double line = item.GetLineHeight() > 0 ? item.GetLineHeight() : item.GetFontSize() * 1.2; if (input.X < item.__GetNativeHandle().GetLayoutValue(4) && input.Y < item.__GetNativeHandle().GetLayoutValue(5) + line)
				{
					item.ToggleExpanded(); input.PreventDefault(); input.StopPropagation();
				}
			});
		}
		@ GuiMethod bool IsActuallyExpanded() const
		{
			return actualExpanded;
		}
		@ GuiMethod void ToggleExpanded()
		{
			RequestExpanded(!actualExpanded);
		}
		void VsguiSynchronizeControl() override
		{
			if (!pendingExpansion && !VsguiNeedsControlConfiguration()) return;
			VsguiUpdateBranch();
			VsguiCommitControlConfiguration();
		}
		@ GuiMethod void RequestExpanded(bool value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (actualExpanded == value) return;
			if (!IsExpandedBound())
			{
				SetExpanded(value);
			}
			pendingExpansion = true;
			actualExpanded = value;
			branch.__GetNativeHandle().SetContentVisible(value);
			SetClass("gui-expanded", value);
			this.__GetNativeHandle().ConfigureItem(IsSelected(), VsguiExpandable(), value, GetSelectionColor(), true);
			ExpandedChanged.broadcast(this, value);
		}
		bool VsguiExpandable() const override
		{
			return len(branch.GetChildren()) != 0;
		}
		bool VsguiHasIndicator() const override
		{
			return true;
		}
		bool VsguiExpanded() const override
		{
			return actualExpanded;
		}
		bool VsguiNavigate(VS::Gui::Key key) override
		{
			if (key == VS::Gui::Key::Right)
			{
				if (!actualExpanded && VsguiExpandable())
				{
					RequestExpanded(true);
				}
				else
				{
					VS::Gui::Internal::SelectionView view = VS::Gui::Internal::FindAncestorOrSelf<VS::Gui::Internal::SelectionView>(GetParent());
					if (view != nullptr)
					{
						for (CollectionItem child : view.GetItems())
						{
							if (child != this && VS::Gui::Internal::IsWithin(child, branch) && VS::Gui::Internal::IsInteractive(child))
							{
								string selectedKey = child.GetEffectiveKey();
								child.Focus();
								if (!view.IsDisposed() && !child.IsDisposed() && view.FindItem(selectedKey) == child)
								{
									view.Select(selectedKey);
								}
								break;
							}
						}
					}
				}
				return true;
			}
			if (key == VS::Gui::Key::Left)
			{
				if (actualExpanded && VsguiExpandable())
				{
					RequestExpanded(false);
				}
				else
				{
					TreeItem parent = VS::Gui::Internal::FindAncestorOrSelf<TreeItem>(GetParent());
					if (parent != nullptr && VS::Gui::Internal::IsInteractive(parent))
					{
						VS::Gui::Internal::SelectionView view = VS::Gui::Internal::FindAncestorOrSelf<VS::Gui::Internal::SelectionView>(parent.GetParent());
						string selectedKey = parent.GetEffectiveKey();
						parent.Focus();
						if (view != nullptr && !view.IsDisposed() && !parent.IsDisposed() && view.FindItem(selectedKey) == parent)
						{
							view.Select(selectedKey);
						}
					}
				}
				return true;
			}
			return false;
		}
	protected:
		void OnExpandedChanged()
		{
			VsguiUpdateBranch();
		}
		void VsguiUpdateBranch()
		{
			actualExpanded = GetExpanded();
			branch.__GetNativeHandle().SetContentVisible(actualExpanded);
			SetClass("gui-expanded", actualExpanded);
			VsguiUpdateItemSkin();
			pendingExpansion = false;
		}
	private:
		@ GuiProperty(Mark = "expanded") std::attribute<bool> Expanded = std::attribute<bool>(true);
		@ GuiEvent(Mark = "expanded-changed") std::multicast_delegate<VS::Gui::TreeItem, bool> ExpandedChanged = std::multicast_delegate<VS::Gui::TreeItem, bool>();
		VS::Gui::Internal::ContentHost branch;
		bool actualExpanded = true;
		bool pendingExpansion = true;
	};

	@ GuiWidget(Mark = "tree-view") export class TreeView : public VS::Gui::Internal::SelectionView
	{
	protected:
		void VsguiSelectionChanged(string key) override
		{
			SelectionChanged.broadcast(this, key);
		}
		void VsguiItemActivated(string key) override
		{
			ItemActivated.broadcast(this, key);
		}
	private:
		@ GuiEvent(Mark = "selection-changed") std::multicast_delegate<VS::Gui::TreeView, string> SelectionChanged = std::multicast_delegate<VS::Gui::TreeView, string>();
		@ GuiEvent(Mark = "item-activated") std::multicast_delegate<VS::Gui::TreeView, string> ItemActivated = std::multicast_delegate<VS::Gui::TreeView, string>();
	};
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "Collections.vs";
export import "Controls.vs";
export import "Widget.vs";
export import "Binding.vs";
import "delegate.vs";

namespace VS::Gui
{
	namespace Internal
	{
		export class VirtualTableData
		{
		public:
			map<string, array<string>> Rows;
			int64 ColumnCount = 0;
		};
	}

	@ GuiWidget(Mark = "table-column") export class TableColumn : public VS::Gui::Button
	{
	public:
		TableColumn()
		{
			cast<Button>(this).AddClicked([](Button sender) -> void
			{
				TableColumn column = cast<TableColumn>(sender); TableView table = column.owner.lock(); if (table != nullptr)
				{
					table.VsguiColumnClicked(column);
				}
			});
		}
		void VsguiSetTable(TableView table)
		{
			owner = weak<TableView>(table);
		}
		void VsguiReleaseTable(TableView table)
		{
			if (owner.lock() == table)
			{
				owner = weak<TableView>();
			}
		}
	protected:
		void OnColumnWidthChanged()
		{
			double value = GetColumnWidth();
			assert(value > 0 && value <= 16777216, "Table column width must be positive and finite");
			SetWidth(Length(value));
			TableView table = owner.lock();
			if (table != nullptr)
			{
				table.VsguiLayoutColumns();
			}
		}
		void OnKeyChanged()
		{
			TableView table = owner.lock();
			if (table != nullptr)
			{
				table.VsguiRefreshColumns();
			}
		}
	private:
		@ GuiProperty(Mark = "key") std::attribute<string> Key = std::attribute<string>("");
		@ GuiProperty(Style, Mark = "column-width") std::attribute<double> ColumnWidth = std::attribute<double>(160);
		@ GuiProperty(Mark = "sortable") std::attribute<bool> Sortable = std::attribute<bool>(true);
		weak<TableView> owner;
	};

	@ GuiWidget(Mark = "table-row") export class TableRow : public VS::Gui::CollectionItem
	{
	public:
		TableRow()
		{
			SetLayout(Layout::Horizontal);
			VsguiObserveChildChanges();
		}
		@ GuiMethod void SetCells(array<string> cells)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			array<Widget> widgets = array<Widget>();
			for (string text : cells)
			{
				widgets.push(VS::Gui::Text::Create().Text(text));
			}
			SetChildren(widgets);
		}
	protected:
		void OnChildrenChanged() override
		{
			VsguiNotifyItemsChanged();
		}
	};

	@ GuiWidget(Mark = "table") export class TableView : public VS::Gui::Internal::SelectionView
	{
	public:
		TableView()
		{
			headers = VS::Gui::Internal::ContentHost::Create().Layout(Layout::Horizontal);
			PrependInternalChild(headers);
			headers.VsguiObserve([](VS::Gui::Internal::ContentHost sender) -> void
			{
				TableView table = VS::Gui::Internal::FindAncestorOrSelf<TableView>(sender.GetParent()); if (table != nullptr && table.IsInitialized())
				{
					table.VsguiRefreshColumns();
				}
			});
			content.VsguiObserve([](VS::Gui::Internal::ContentHost sender) -> void
			{
				TableView table = VS::Gui::Internal::FindAncestorOrSelf<TableView>(sender.GetParent()); if (table != nullptr && table.IsInitialized())
				{
					table.VsguiInvalidateItems();
					if (!table.VsguiApplyingVirtualRows())
					{
						table.tableDirty = true;
						table.VsguiRefreshTable();
					}
				}
			});
		}
		void VsguiSynchronizeControl() override
		{
			if (IsInitialized() && tableDirty)
			{
				VsguiRefreshTable();
			}
			VsguiSynchronizeSelection();
		}

		@ GuiMethod void SetColumns(array<TableColumn> columns)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			array<Widget> widgets = array<Widget>();
			map<string, bool> keys = map<string, bool>();
			for (TableColumn column : columns)
			{
				assert(column != nullptr, "Table column must not be null");
				string key = column.GetKey();
				if (key == "")
				{
					key = column.GetId();
				}
				if (key == "")
				{
					key = to_string(len(widgets));
				}
				assert(!keys.has(key), "Table column keys must be unique");
				keys[key] = true;
				double width = column.GetColumnWidth();
				assert(width > 0 && width <= 16777216, "Invalid table column width");
				widgets.push(column);
			}
			for (Widget row : content.GetChildren())
			{
				if (VsguiIsVirtualSpacer(row))
				{
					continue;
				}
				assert(row.GetChildCount() == len(columns), "Replace rows before changing the table column count");
			}
			if (IsVirtualized() && virtualDefaults)
			{
				VS::Gui::Internal::VirtualTableData data = virtualData.lock();
				assert(data != nullptr && data.ColumnCount == len(columns), "Replace rows before changing the table column count");
			}
			headers.SetChildren(widgets);
		}
		@ GuiMethod array<TableColumn> GetColumns() const
		{
			array<TableColumn> result = array<TableColumn>();
			for (Widget widget : headers.GetChildren())
			{
				result.push(cast<TableColumn>(widget));
			}
			return result;
		}
		@ GuiMethod void SetRows(array<array<string>> rows)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			assert(!IsDisposed(), "Invalid table mutation");
			array<Widget> widgets = array<Widget>();
			int64 columnCount = len(GetColumns());
			for (int64 index = 0; index < len(rows); index++)
			{
				assert(len(rows[index]) == columnCount, "Table row must have one cell per column");
				TableRow row = TableRow::Create().Key(to_string(index));
				row.SetCells(rows[index]);
				widgets.push(row);
			}
			if (!VsguiClearSourceForReplacement())
			{
				for (Widget row : widgets)
				{
					row.Dispose();
				}
				return;
			}
			virtualDefaults = false;
			virtualData = weak<VS::Gui::Internal::VirtualTableData>();
			content.SetChildren(widgets);
		}
		@ GuiMethod void SetVirtualRows(array<array<string>> values, double rowHeight = 28, int64 overscan = 2)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0 && !IsDisposed() && !VsguiApplyingVirtualRows(), "Invalid virtual table mutation");
			VS::Gui::Internal::VirtualRows::ValidateExtent(len(values), rowHeight, overscan);
			int64 count = len(GetColumns());
			VS::Gui::Internal::VirtualTableData data = VS::Gui::Internal::VirtualTableData();
			data.ColumnCount = count;
			array<string> source = array<string>();
			for (int64 index = 0; index < len(values); index++)
			{
				assert(len(values[index]) == count, "Table row must have one cell per column");
				string key = to_string(index);
				source.push(key);
				array<string> cells = array<string>();
				for (string cell : values[index])
				{
					cells.push(cell);
				}
				data.Rows[key] = cells;
			}
			VS::Gui::Internal::VirtualRows previousSource = VsguiGetVirtualRows();
			weak<VS::Gui::Internal::VirtualTableData> previousData = virtualData;
			virtualData = weak<VS::Gui::Internal::VirtualTableData>(data);
			try
			{
				VsguiSetVirtualSource(source, rowHeight, [data](string key, CollectionItem previous) mutable -> CollectionItem
				{
					TableRow row = nullptr; if (previous != nullptr)
					{
						row = cast<TableRow>(previous);
					}
					else
					{
						row = TableRow::Create();
					}
					array<string> cells = data.Rows[key]; if (row.GetChildCount() != len(cells))
					{
						row.SetCells(cells);
					}
					else
					{
						for (int64 index = 0; index < len(cells); index++)
						{
							Widget cell = row.GetChild(index);
							if (cell.GetText() != cells[index])
							{
								cell.SetText(cells[index]);
							}
						}
					}
					return row;
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
		@ GuiMethod void SetCellText(string rowKey, string columnKey, string text)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0 && !IsDisposed(), "Invalid table mutation");
			if (IsVirtualized() && virtualDefaults)
			{
				VS::Gui::Internal::VirtualTableData data = virtualData.lock();
				assert(data != nullptr && data.Rows.has(rowKey) && columnIndices.has(columnKey), "Unknown table row or column");
				data.Rows[rowKey][columnIndices[columnKey]] = text;
				RefreshVirtualItems();
				return;
			}
			CollectionItem row = FindItem(rowKey);
			assert(row != nullptr && columnIndices.has(columnKey), "Unknown table row or column");
			row.GetChild(columnIndices[columnKey]).SetText(text);
		}
		@ GuiMethod string GetCellText(string rowKey, string columnKey) const
		{
			if (IsVirtualized() && virtualDefaults)
			{
				VS::Gui::Internal::VirtualTableData data = virtualData.lock();
				assert(data != nullptr && data.Rows.has(rowKey) && columnIndices.has(columnKey), "Unknown table row or column");
				return data.Rows[rowKey][columnIndices[columnKey]];
			}
			CollectionItem row = FindItem(rowKey);
			assert(row != nullptr && columnIndices.has(columnKey), "Unknown table row or column");
			return row.GetChild(columnIndices[columnKey]).GetText();
		}
		@ GuiMethod void SortBy(string columnKey, bool ascending)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			assert(columnIndices.has(columnKey), "Unknown table column");
			int64 columnIndex = columnIndices[columnKey];
			if (IsVirtualized())
			{
				assert(virtualDefaults, "Custom virtual tables sort their source through SetVirtualOrder");
				VS::Gui::Internal::VirtualTableData data = virtualData.lock();
				assert(data != nullptr, "Virtual table source is not available");
				array<string> order = VsguiGetVirtualRows().GetKeys();
				array<string> values = array<string>();
				for (string key : order)
				{
					values.push(data.Rows[key][columnIndex]);
				}
				for (int64 width = 1; width < len(order); width *= 2)
				{
					array<string> merged = array<string>();
					array<string> mergedValues = array<string>();
					for (int64 start = 0; start < len(order); start += width * 2)
					{
						int64 middle = min(start + width, len(order));
						int64 end = min(start + width * 2, len(order));
						int64 left = start, right = middle;
						while (left < middle || right < end)
						{
							bool takeRight = right < end && (left == middle || (ascending ? VS::Gui::__NativeWidget::CompareText(values[right], values[left]) < 0 : VS::Gui::__NativeWidget::CompareText(values[left], values[right]) < 0));
							int64 index = takeRight ? right++: left++;
							merged.push(order[index]);
							mergedValues.push(values[index]);
						}
					}
					order = merged;
					values = mergedValues;
				}
				SetVirtualOrder(order);
				VsguiCommitSort(columnKey, ascending);
				return;
			}
			array<Widget> rows = content.GetChildren();
			array<string> values = array<string>();
			for (Widget row : rows)
			{
				values.push(row.GetChild(columnIndex).GetText());
			}
			// Stable bottom-up merge: read each cell once, keep equal rows in authored order.
			for (int64 width = 1; width < len(rows); width *= 2)
			{
				array<Widget> merged = array<Widget>();
				array<string> keys = array<string>();
				for (int64 start = 0; start < len(rows); start += width * 2)
				{
					int64 middle = min(start + width, len(rows));
					int64 end = min(start + width * 2, len(rows));
					int64 left = start;
					int64 right = middle;
					while (left < middle || right < end)
					{
						bool takeRight = right < end && (left == middle || (ascending ? VS::Gui::__NativeWidget::CompareText(values[right], values[left]) < 0 : VS::Gui::__NativeWidget::CompareText(values[left], values[right]) < 0));
						int64 index = takeRight ? right++: left++;
						merged.push(rows[index]);
						keys.push(values[index]);
					}
				}
				rows = merged;
				values = keys;
			}
			content.SetChildren(rows);
			VsguiCommitSort(columnKey, ascending);
		}
		void VsguiCommitSort(string columnKey, bool ascending)
		{
			sortedColumn = columnKey;
			sortAscending = ascending;
			for (TableColumn column : GetColumns())
			{
				string key = VsguiColumnKey(column);
				column.SetClass("gui-sort-ascending", key == columnKey && ascending);
				column.SetClass("gui-sort-descending", key == columnKey && !ascending);
			}
			Sorted.broadcast(this, columnKey, ascending);
		}

		void VsguiColumnClicked(TableColumn column)
		{
			string key = VsguiColumnKey(column);
			ColumnClicked.broadcast(this, key);
			if (IsDisposed() || column.IsDisposed() || !GetAutoSort() || !column.GetSortable() || (IsVirtualized() && !virtualDefaults)) return;
			for (TableColumn attached : GetColumns())
			{
				if (attached != column)
				{
					continue;
				}
				key = VsguiColumnKey(column);
				SortBy(key, sortedColumn != key || !sortAscending);
				return;
			}
		}
		void VsguiRefreshTable()
		{
			if (rebuilding || IsDisposed() || VsguiApplyingVirtualRows()) return;
			tableDirty = true;
			bool completed = false;
			try
			{
				rebuilding = true;
				for (int pass = 0; pass < 32; pass++)
				{
					if (IsDisposed())
					{
						completed = true;
						return;
					}
					array<Widget> rows = array<Widget>();
					array<Widget> columns = array<Widget>();
					for (Widget child : content.GetChildren())
					{
						if (VsguiIsVirtualSpacer(child))
						{
							continue;
						}
						optional<TableColumn> column = try_cast<TableColumn>(child);
						if (column.has_value())
						{
							columns.push(child);
						}
						else
						{
							assert(try_cast<TableRow>(child).has_value(), "Table content requires TableColumn or TableRow");
							rows.push(child);
						}
					}
					if (len(columns) != 0)
					{
						content.SetChildren(rows);
						headers.SetChildren(columns);
					}
					VsguiRefreshColumns();
					// Consume this pass before invoking a user hook. A nested content
					// notification must be drained while structural changes are allowed.
					tableDirty = false;
					RefreshItems();
					if (!tableDirty || IsDisposed())
					{
						completed = true;
						return;
					}
				}
				assert(false, "Table refresh did not stabilize");
			}
			finally
			{
				if (!completed)
				{
					tableDirty = true;
				}
				rebuilding = false;
			}
		}
		void VsguiRefreshColumns()
		{
			tableDirty = true;
			map<string, int64> next = map<string, int64>();
			int64 index = 0;
			for (Widget child : headers.GetChildren())
			{
				optional<TableColumn> column = try_cast<TableColumn>(child);
				assert(column.has_value(), "Table header requires TableColumn");
				string key = column.get().GetKey();
				if (key == "")
				{
					key = column.get().GetId();
				}
				if (key == "")
				{
					key = to_string(index);
				}
				assert(!next.has(key), "Table column keys must be unique");
				next[key] = index++;
			}
			columnIndices = next;
			for (weak<TableColumn> old : attachedColumns)
			{
				TableColumn column = old.lock();
				if (column != nullptr && !column.IsDisposed())
				{
					column.VsguiReleaseTable(this);
				}
			}
			attachedColumns = array<weak<TableColumn>>();
			for (TableColumn column : GetColumns())
			{
				column.VsguiSetTable(this);
				attachedColumns.push(weak<TableColumn>(column));
			}
			VsguiLayoutColumns();
			tableDirty = false;
		}
		void VsguiLayoutColumns()
		{
			array<TableColumn> columns = GetColumns();
			for (Widget row : content.GetChildren())
			{
				if (!try_cast<TableRow>(row).has_value())
				{
					continue;
				}
				assert(row.GetChildCount() == len(columns), "Table row must have one cell per column");
				for (int64 index = 0; index < len(columns); index++)
				{
					row.GetChild(index).SetWidth(Length(columns[index].GetColumnWidth()));
				}
			}
		}
	protected:
		bool VsguiSupportsVirtualRows() const override
		{
			return true;
		}
		bool VsguiAcceptVirtualRow(CollectionItem row) const override
		{
			return try_cast<TableRow>(row).has_value();
		}
		void VsguiConfigureVirtualSource(bool builtin) override
		{
			virtualDefaults = builtin;
		}
		void OnInitialized() override
		{
			VsguiRefreshTable();
		}
		void VsguiItemsChanged() override
		{
			VsguiLayoutColumns();
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
		weak<VS::Gui::Internal::VirtualTableData> virtualData;
		bool virtualDefaults = false;
		string VsguiColumnKey(TableColumn column) const
		{
			array<TableColumn> columns = GetColumns();
			for (int64 index = 0; index < len(columns); index++)
			{
				if (columns[index] != column)
				{
					continue;
				}
				string key = column.GetKey();
				if (key == "")
				{
					key = column.GetId();
				}
				return key == "" ? to_string(index) : key;
			}
			throw "Table column is no longer attached";
		}
		@ GuiProperty(Mark = "auto-sort") std::attribute<bool> AutoSort = std::attribute<bool>(true);
		@ GuiEvent(Mark = "selection-changed") std::multicast_delegate<VS::Gui::TableView, string> SelectionChanged = std::multicast_delegate<VS::Gui::TableView, string>();
		@ GuiEvent(Mark = "item-activated") std::multicast_delegate<VS::Gui::TableView, string> ItemActivated = std::multicast_delegate<VS::Gui::TableView, string>();
		@ GuiEvent(Mark = "column-clicked") std::multicast_delegate<VS::Gui::TableView, string> ColumnClicked = std::multicast_delegate<VS::Gui::TableView, string>();
		@ GuiEvent(Mark = "sorted") std::multicast_delegate<VS::Gui::TableView, string, bool> Sorted = std::multicast_delegate<VS::Gui::TableView, string, bool>();
		VS::Gui::Internal::ContentHost headers;
		map<string, int64> columnIndices;
		array<weak<TableColumn>> attachedColumns;
		string sortedColumn;
		bool sortAscending = true;
		bool rebuilding = false;
		bool tableDirty = true;
	};
}

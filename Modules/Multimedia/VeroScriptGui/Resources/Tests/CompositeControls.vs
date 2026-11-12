// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "composite.vswml";

using namespace VS::Gui;

Context gui = nullptr;
ListView list = nullptr;
TreeView tree = nullptr;
ComboBox combo = nullptr;
TabView tabs = nullptr;
TabView dynamicTabs = nullptr;
TableView table = nullptr;
Menu menu = nullptr;
Dialog dialog = nullptr;
string selectedModel = "a";
int selectedEvents = 0;
int activatedEvents = 0;
int menuEvents = 0;
int closedEvents = 0;
bool dynamicExpanded = true;
string lastClosed = "";
string dynamicTabKey = "second";
string dynamicTabTitle = "Second";
bool modelOpen = true;
bool checkedModel = false;
array<bool> checkedCandidates = array<bool>();
bool reopenDialog = true;
int menuReentrantInvocations = 0;

void VerifyMenuReentry()
{
	MenuItem leaf = MenuItem::Create().Text("Command");
	MenuItem owner = MenuItem::Create().Text("Menu")[{leaf}];
	Button outside = Button::Create().Text("Outside");
	Stack root = Stack::Create()[{Menu::Create()[{owner}], outside}];
	Context context = Context("test", 400, 300);
	try
	{
		context.SetRoot(root);
		context.Update(0);
		Popup portal = cast<Popup>(owner.GetChild(0));
		Subscription cancel = portal.AddOpenChanged([](Popup sender, bool open)->void
		{
			if (open)
			{
				sender.RequestOpen(false);
			}
		});
		owner.Focus();
		owner.OpenMenu();
		assert(!owner.IsMenuOpen() && context.GetFocus() == owner, "a menu ignored an open cancellation");
		cancel.Disconnect();

		weak<Button> target = weak<Button>(outside);
		Subscription focus = portal.AddOpenChanged([target](Popup sender, bool open) mutable -> void
		{
			if (open)
			{
				target.lock().Focus();
			}
		});
		owner.OpenMenu();
		assert(context.GetFocus() == outside, "a menu overwrote callback-assigned focus");
		focus.Disconnect();
		owner.CloseMenu();
		owner.OpenMenu();

		leaf.AddInvoked([](MenuItem sender, string command)->void
		{
			menuReentrantInvocations++;
		});
		weak<MenuItem> weakOwner = weak<MenuItem>(owner);
		portal.AddOpenChanged([weakOwner](Popup sender, bool open) mutable -> void
		{
			if (!open)
			{
				weakOwner.lock().Dispose();
			}
		});
		leaf.Invoke();
		assert(owner.IsDisposed() && leaf.IsDisposed() && menuReentrantInvocations == 0,
			"a disposed menu item dispatched a command after its close callback");
	}
	finally
	{
		context.Close(); root.Dispose();
	}
}

ListView virtualRetry = nullptr;
Context virtualRetryContext = nullptr;
Widget virtualRetryRoot = nullptr;
bool failVirtualFactory = true;
int virtualCreates = 0;
int virtualRecycles = 0;
int virtualSourceReleases = 0;
class VirtualSourceCapture
{
public:
	~VirtualSourceCapture()
	{
		virtualSourceReleases++;
	}
	string Value = "Captured row";
};

void PrepareVirtualRetry()
{
	virtualRetry = ListView::Create().Height(Length(120));
	virtualRetryRoot = Stack::Create()[{virtualRetry}];
	virtualRetryContext = Context("test", 400, 300);
	virtualRetryContext.SetRoot(virtualRetryRoot);
	virtualRetryContext.Update(0);
}
void TryVirtualFactoryFailure()
{
	array<string> source = array<string>();
	for (int index = 0; index < 1000; index++)
	{
		source.push(to_string(index));
	}
	virtualRetry.SetVirtualSource(source, 24, [](string key, CollectionItem previous)->CollectionItem
	{
		if (key == "2" && failVirtualFactory)
		{
			failVirtualFactory = false;
			throw "virtual factory failed";
		}
		ListItem row = nullptr;
		if (previous == nullptr)
		{
			row = ListItem::Create();
			virtualCreates++;
		}
		else
		{
			row = cast<ListItem>(previous);
			virtualRecycles++;
		}
		row.SetText("Custom " + key);
		return row;
	});
}
void VerifyVirtualRetry()
{
	virtualRetryContext.Update(.001);
	virtualRetryContext.Update(.002);
	assert(virtualRetry.GetItemCount() == 1000 && virtualRetry.GetRealizedItemCount() < 20 &&
		virtualRetry.FindItem("2").GetText() == "Custom 2", "virtual factory failure did not recover");
	int creates = virtualCreates;
	virtualRetry.ScrollToItem("900");
	virtualRetryContext.Update(.003);
	virtualRetryContext.Update(.004);
	assert(virtualRetry.FindItem("900") != nullptr && virtualRecycles > 0 && virtualCreates <= creates + 4,
		"virtual scrolling did not reuse rows: before=" + to_string(creates) + " after=" +
		to_string(virtualCreates) + " reused=" + to_string(virtualRecycles));
	virtualRetry.SetHeight(Length(240));
	virtualRetryContext.Update(.005);
	virtualRetryContext.Update(.006);
	assert(virtualRetry.GetRealizedItemCount() >= 10 && virtualRetry.GetRealizedItemCount() < 25,
		"virtual viewport resize did not realize enough rows");
}
void TryInvalidVirtualSource()
{
	array<string> duplicate = {"same", "same"};
	virtualRetry.SetVirtualSource(duplicate, 24,
		[](string key, CollectionItem previous)->CollectionItem
	{
		return previous;
	});
}
void VerifyVirtualSourcePreserved()
{
	try
	{
		assert(virtualRetry.IsVirtualized() && virtualRetry.GetItemCount() == 1000,
			"an invalid virtual source replaced the committed source");
		virtualRetryContext.Update(.007);
		assert(virtualRetry.GetRealizedItemCount() < 25, "invalid source damaged virtualization");
		virtualRetry.RefreshVirtualItems();
		virtualRetryContext.Close();
		virtualRetryContext = Context("test", 400, 300);
		virtualRetryContext.SetRoot(virtualRetryRoot);
		virtualRetry.ScrollToItem("900");
		virtualRetryContext.Update(0);
		virtualRetryContext.Update(.001);
		assert(virtualRetry.FindItem("900") != nullptr,
			"a pending virtual update in a closed context blocked the new mount");
		virtualRetry.RefreshVirtualItems();
		Context previous = virtualRetryContext;
		previous.SetRoot(nullptr);
		virtualRetryContext = Context("test", 400, 300);
		virtualRetryContext.SetRoot(virtualRetryRoot);
		int recycled = virtualRecycles;
		previous.Update(.010);
		assert(virtualRecycles == recycled, "an old context realized rows after their owner moved");
		previous.Close();
		virtualRetryContext.Update(0);
		virtualRetryContext.Update(.001);
		array<string> empty = array<string>();
		virtualRetry.SetVirtualItems(empty, 24);
		virtualRetryContext.Update(.008);
		virtualRetryContext.Update(.009);
		assert(virtualRetry.GetItemCount() == 0 && virtualRetry.GetRealizedItemCount() == 0,
			"empty virtual source did not release its realized rows");
		VirtualSourceCapture model = VirtualSourceCapture();
		array<string> captured = {"captured"};
		virtualRetry.SetVirtualSource(captured, 24,
			[model](string key, CollectionItem previous)->CollectionItem
			{
				ListItem row = nullptr;
				if (previous == nullptr)
				{
					row = ListItem::Create();
				}
				else
				{
					row = cast<ListItem>(previous);
				}
				row.SetText(model.Value);
				return row;
			});
		model = nullptr;
		assert(virtualSourceReleases == 0, "a live virtual source lost its captured model");
		virtualRetry.Dispose();
		assert(virtualSourceReleases == 1, "a disposed virtual view retained its factory model");
	}
	finally
	{
		virtualRetryContext.Close(); virtualRetryRoot.Dispose();
	}
}
string reopenedResult = "";
MenuItem menuVictim = nullptr;
ListItem migratedItem = nullptr;
int migratedSelections = 0;
double menuTime = .041;

void main()
{
}

void VerifyComboFocusHandoff()
{
	ComboBox box = ComboBox::Create().SelectedKey("item")[{ListItem::Create().Key("item").Text("Item")}];
	Button target = Button::Create().Text("Next field");
	Dialog modal = Dialog::Create()[{Button::Create().Text("Confirm")}];
	Stack root = Stack::Create()[{box, target, modal}];
	Context context = Context("test", 400, 300);
	try
	{
		context.SetRoot(root);
		context.Update(0);
		box.Focus();
		box.RequestOpen(true);
		context.Update(.001);
		box.GetList().FocusSelected();
		Subscription next = box.AddOpenChanged([target](ComboBox source, bool open) mutable -> void
		{
			if (!open)
			{
				target.Focus();
			}
		});
		box.RequestOpen(false);
		assert(context.GetFocus() == target, "closing a combo must preserve focus assigned by its callback");
		next.Disconnect();
		box.Focus();
		box.RequestOpen(true);
		context.Update(.002);
		box.GetList().FocusSelected();
		Subscription show = box.AddOpenChanged([modal](ComboBox source, bool open) mutable -> void
		{
			if (!open)
			{
				modal.Show();
			}
		});
		box.RequestOpen(false);
		context.Update(.003);
		assert(modal.IsActuallyOpen() && VS::Gui::Internal::IsWithin(context.GetFocus(), modal),
			"closing a combo can open a modal without focusing the blocked background");
		show.Disconnect();
		modal.Close();
	}
	finally
	{
		context.Close(); root.Dispose();
	}
}

void VerifyVirtualComboLabels()
{
	array<string> labels = array<string>();
	for (int64 index = 0; index < 1000; index++)
	{
		labels.push("Choice " + to_string(index));
	}
	ComboBox box = ComboBox::Create().Placeholder("Choose");
	box.GetList().SetVirtualItems(labels, 24);
	box.SetSelectedKey("900");
	Context context = Context("test", 400, 300);
	try
	{
		context.SetRoot(box);
		context.Update(0);
		context.Update(.001);
		assert(box.GetList().FindItem("900") == nullptr && box.GetList().GetRealizedItemCount() < 30,
			"an offscreen combo selection must not realize its row");
		assert(box.GetActualSelectedKey() == "900" && box.GetText() == "Choice 900",
			"a virtual combo lost its offscreen selection label");
		box.GetList().SetVirtualItemText(900, "Changed choice");
		context.Update(.002);
		context.Update(.003);
		assert(box.GetText() == "Changed choice", "an offscreen combo label did not refresh");
		array<string> ordinary = {"Ordinary choice"};
		box.SetItems(ordinary);
		box.SetSelectedKey("0");
		context.Update(.004);
		assert(box.GetText() == "Ordinary choice", "a combo retained a replaced virtual label");
	}
	finally
	{
		context.Close();
		box.Dispose();
	}
}

ListView virtualKeyboard = nullptr;
Context virtualKeyboardContext = nullptr;
double virtualKeyboardScroll = 0;
int virtualKeyboardScrollEvents = 0;
bool virtualKeyboardAcceptScroll = true;
bool virtualKeyboardRemoveSource = false;
void PrepareVirtualKeyboard()
{
	array<string> labels = array<string>();
	for (int64 index = 0; index < 1000; index++)
	{
		labels.push("Keyboard " + to_string(index));
	}
	std::attribute<double> scrolling = std::attribute<double>();
	scrolling.bind([]()->double
	{
		return virtualKeyboardScroll;
	});
	virtualKeyboard = ListView::Create().Height(Length(120)).ScrollY(scrolling).SelectedKey("0")
		.OnScrolled([](ScrollView sender, double x, double y)->void
		{
			virtualKeyboardScrollEvents++;
		if (virtualKeyboardAcceptScroll)
		{
			virtualKeyboardScroll = y;
		}
		if (virtualKeyboardRemoveSource)
		{
			cast<ListView>(sender).ClearVirtualSource();
		}
		});
	virtualKeyboard.SetVirtualItems(labels, 24);
	virtualKeyboardContext = Context("test", 400, 300);
	virtualKeyboardContext.SetRoot(virtualKeyboard);
	virtualKeyboardContext.Update(0);
	virtualKeyboardContext.Update(.001);
	virtualKeyboardContext.SetFocus(virtualKeyboard);
}
void VerifyVirtualKeyboardAccepted()
{
	assert(virtualKeyboard.IsScrollYBound(), "virtual keyboard navigation cleared a scroll binding");
	assert(virtualKeyboardScrollEvents == 1 && virtualKeyboardScroll > 0 && virtualKeyboard.GetActualSelectedKey() == "999",
		"virtual keyboard navigation did not report its scroll candidate");
	virtualKeyboardContext.Update(.002);
	virtualKeyboardContext.Update(.003);
	assert(virtualKeyboard.GetActualScrollY() > 0 && virtualKeyboard.FindItem("999") != nullptr,
		"an accepted virtual keyboard scroll was not realized");
	virtualKeyboardAcceptScroll = false;
}
void VerifyVirtualKeyboardRejected()
{
	virtualKeyboardContext.Update(.004);
	virtualKeyboardContext.Update(.005);
	assert(virtualKeyboard.IsScrollYBound() && virtualKeyboardScrollEvents == 2 &&
		virtualKeyboardScroll > 0 && virtualKeyboard.GetScrollY() == virtualKeyboardScroll &&
		virtualKeyboard.GetActualScrollY() > 0 && virtualKeyboard.GetActualSelectedKey() == "0",
		"a rejected virtual keyboard scroll changed its binding or model");
	virtualKeyboardAcceptScroll = true;
	virtualKeyboardScroll = 100000;
	virtualKeyboardContext.Update(.006);
	virtualKeyboardContext.Update(.007);
}
void VerifyVirtualKeyboardClamped()
{
	assert(virtualKeyboard.IsScrollYBound() && virtualKeyboardScrollEvents == 3 && virtualKeyboardScroll < 100000,
		"virtual input did not report a clamped scroll candidate");
	virtualKeyboardContext.Update(.008);
	virtualKeyboardContext.Update(.009);
	assert(virtualKeyboard.GetActualScrollY() == virtualKeyboardScroll, "virtual scroll model and geometry disagree");
	virtualKeyboardAcceptScroll = false;
	virtualKeyboardRemoveSource = true;
}
void VerifyVirtualKeyboardSourceRemoval()
{
	assert(!virtualKeyboard.IsVirtualized() && virtualKeyboard.IsScrollYBound() && virtualKeyboardScrollEvents == 4,
		"a scroll callback could not safely remove its virtual source");
	virtualKeyboardContext.Close();
	virtualKeyboard.Dispose();
}

void PrepareVirtualDisabledKeyboard()
{
	array<string> source = array<string>();
	for (int64 index = 0; index < 1000; index++)
	{
		source.push(to_string(index));
	}
	virtualKeyboard = ListView::Create().Height(Length(120)).SelectedKey("0");
	virtualKeyboard.SetVirtualSource(source, 24, [](string key, CollectionItem previous)->CollectionItem
	{
		ListItem row = nullptr;
		if (previous == nullptr)
		{
			row = ListItem::Create();
		}
		else
		{
			row = cast<ListItem>(previous);
		}
		row.SetText(key);
		row.SetEnabled(key != "1" && key != "999");
		return row;
	});
	virtualKeyboardContext = Context("test", 400, 300);
	virtualKeyboardContext.SetRoot(virtualKeyboard);
	virtualKeyboardContext.Update(0);
	virtualKeyboardContext.Update(.001);
	virtualKeyboardContext.SetFocus(virtualKeyboard);
}
void VerifyVirtualDisabledEnd()
{
	assert(virtualKeyboard.GetActualSelectedKey() == "998" && virtualKeyboard.GetRealizedItemCount() < 20,
		"virtual End selected a disabled offscreen item or realized the full source");
	virtualKeyboardContext.Update(.002);
	virtualKeyboardContext.Update(.003);
}
void VerifyVirtualDisabledHome()
{
	assert(virtualKeyboard.GetActualSelectedKey() == "0", "virtual Home selection");
	virtualKeyboardContext.Update(.004);
	virtualKeyboardContext.Update(.005);
}
void VerifyVirtualDisabledDown()
{
	assert(virtualKeyboard.GetActualSelectedKey() == "2" && virtualKeyboard.GetRealizedItemCount() < 20,
		"virtual navigation did not skip a disabled visible item");
	virtualKeyboardContext.Close();
	virtualKeyboard.Dispose();
}

@GuiWidget export class RefreshCountingTable : public VS::Gui::TableView
{
public:
	int refreshes = 0;
protected:
	void VsguiItemsChanged() override
	{
		refreshes++;
		VsguiLayoutColumns();
	}
};

string committedCollectionKey = "bound";
int committedCollectionKeyReads = 0;
void VerifyCommittedCollectionKeys()
{
	RefreshCountingTable view = RefreshCountingTable::Create();
	view.SetHeight(Length(160));
	array<TableColumn> columns = {TableColumn::Create().Key("name").Text("Name")};
	view.SetColumns(columns);
	array<array<string>> records = array<array<string>>();
	for (int64 index = 0; index < 100; index++)
	{
		array<string> record = {to_string(index)};
		records.push(record);
	}
	view.SetVirtualRows(records, 20, 2);
	Context context = Context("test", 400, 300);
	try
	{
		context.SetRoot(view);
		context.Update(0);
		context.Update(.001);
		view.refreshes = 0;
		view.SetScrollY(140);
		context.Update(.002);
		context.Update(.003);
		assert(view.FindItem("7") != nullptr && view.FindItem("7").GetEffectiveKey() == "7",
			"committed virtual row key did not match its index");
		assert(view.refreshes <= 2, "committed row-key hooks repeatedly rebuilt the entire table");

		ListItem literal = ListItem::Create().Key("literal");
		ListItem bound = ListItem::Create();
		std::attribute<string> key = std::attribute<string>();
		key.bind([]() -> string { committedCollectionKeyReads++; return committedCollectionKey; });
		bound.SetKey(key);
		ListView list = ListView::Create()[{literal, bound}];
		context.SetRoot(list);
		context.Update(.004);
		literal.SetKey("changed");
		context.Update(.005);
		assert(list.FindItem("changed") == literal && list.FindItem("literal") == nullptr,
			"a real literal key change failed to update the collection index");
		int reads = committedCollectionKeyReads;
		committedCollectionKey = "rebound";
		context.Update(.006);
		assert(list.FindItem("rebound") == bound && list.FindItem("bound") == nullptr && committedCollectionKeyReads > reads,
			"dynamic collection keys stopped evaluating or refreshing");
		literal.SetId("fallback");
		literal.SetKey("");
		context.Update(.007);
		assert(list.FindItem("fallback") == literal, "empty collection keys lost their ID fallback");
	}
	finally
	{
		context.Close();
	}
}

void VerifyVirtualSnapshotRelease()
{
	array<string> labels = {"Alpha", "Beta"};
	ListView heldList = ListView::Create();
	heldList.SetVirtualItems(labels, 24);
	int64 listSnapshot = CaptureVirtualSnapshot([heldList]()->void {});
	assert(!VirtualSnapshotExpired(listSnapshot), "active virtual list lost its data");
	labels[0] = "Caller changed";
	optional<string> label = heldList.VsguiGetItemText("0");
	assert(label.has_value() && label.get() == "Alpha", "virtual list aliased caller data");
	heldList.ClearVirtualSource();
	assert(VirtualSnapshotExpired(listSnapshot), "cleared list retained its data snapshot");
	heldList.SetVirtualItems(labels, 24);
	listSnapshot = CaptureVirtualSnapshot([heldList]()->void {});
	array<string> custom = {"custom"};
	heldList.SetVirtualSource(custom, 24, [](string key, CollectionItem previous)->CollectionItem
	{
		ListItem item = ListItem::Create().Text(key); return item;
	});
	assert(VirtualSnapshotExpired(listSnapshot), "custom list source retained old built-in data");
	heldList.SetVirtualItems(labels, 24);
	listSnapshot = CaptureVirtualSnapshot([heldList]()->void {});
	heldList.Dispose();
	assert(heldList.IsDisposed() && VirtualSnapshotExpired(listSnapshot), "retained disposed list kept its data snapshot");

	TableView heldTable = TableView::Create();
	array<TableColumn> columns = {TableColumn::Create().Key("name"), TableColumn::Create().Key("state")};
	heldTable.SetColumns(columns);
	array<string> cells = {"Alpha", "Ready"};
	array<array<string>> records = {cells};
	heldTable.SetVirtualRows(records, 24);
	heldTable.Initialize();
	int64 tableSnapshot = CaptureVirtualSnapshot([heldTable]()->void {});
	records[0][0] = "Caller changed";
	assert(heldTable.GetCellText("0", "name") == "Alpha", "virtual table aliased caller cells");
	heldTable.ClearVirtualSource();
	assert(VirtualSnapshotExpired(tableSnapshot), "cleared table retained its data snapshot");
	heldTable.SetVirtualRows(records, 24);
	tableSnapshot = CaptureVirtualSnapshot([heldTable]()->void {});
	heldTable.SetVirtualSource(custom, 24, [](string key, CollectionItem previous)->CollectionItem
	{
		TableRow row = TableRow::Create();
		array<string> cells = {key, "Ready"};
		row.SetCells(cells);
		return row;
	});
	assert(VirtualSnapshotExpired(tableSnapshot), "custom table source retained old built-in data");
	heldTable.SetVirtualRows(records, 24);
	tableSnapshot = CaptureVirtualSnapshot([heldTable]()->void {});
	heldTable.Dispose();
	assert(heldTable.IsDisposed() && VirtualSnapshotExpired(tableSnapshot), "retained disposed table kept its data snapshot");
}

int columnWidthReads = 0;
double columnWidthModel = 180;
void VerifyTableColumnWidthReads()
{
	TableView view = TableView::Create();
	std::attribute<double> width = std::attribute<double>();
	width.bind([]()->double { columnWidthReads++; return columnWidthModel; });
	TableColumn literal = TableColumn::Create().Key("literal").ColumnWidth(120);
	array<TableColumn> columns = {literal, TableColumn::Create().Key("dynamic").ColumnWidth(width)};
	view.SetColumns(columns);
	array<string> cells = {"A", "B"};
	array<array<string>> rows = {cells, cells};
	view.SetRows(rows);
	view.Initialize();
	columnWidthReads = 0;
	view.VsguiLayoutColumns();
	assert(columnWidthReads == 0, "table layout evaluated a getter outside synchronization");
	assert(view.FindItem("0").GetChild(0).GetWidth() == Length(120) &&
		view.FindItem("1").GetChild(1).GetWidth() == Length(180), "table layout lost literal or dynamic widths");
	literal.SetColumnWidth(140);
	columnWidthReads = 0;
	view.VsguiLayoutColumns();
	assert(columnWidthReads == 0 && view.FindItem("1").GetChild(0).GetWidth() == Length(140),
		"table column widths were cached across layout calls");
	columnWidthModel = 220;
	view.Synchronize();
	assert(columnWidthReads > 0 && view.FindItem("1").GetChild(1).GetWidth() == Length(220),
		"table synchronization lost a dynamic column width update");
	view.Dispose();
}

@GuiWidget export class RetryBuiltinList : public VS::Gui::ListView
{
public:
	bool Fail = false;
	string Expected = "";
protected:
	void VsguiItemsChanged() override
	{
		if (Expected != "" && IsVirtualized())
		{
			assert(VsguiGetItemText("99").get() == Expected, "list hook saw a stale built-in snapshot");
		}
		if (Fail)
		{
			Fail = false;
			throw "retry list items";
		}
	}
};
@GuiWidget export class RetryBuiltinTable : public VS::Gui::TableView
{
public:
	bool Fail = false;
	bool Replace = false;
	string Expected = "";
protected:
	void VsguiItemsChanged() override
	{
		if (Expected != "" && IsVirtualized())
		{
			assert(GetCellText("99", "name") == Expected, "table hook saw a stale built-in snapshot");
		}
		if (Fail)
		{
			Fail = false;
			throw "retry table items";
		}
		if (Replace && IsVirtualized())
		{
			Replace = false;
			Expected = "";
			array<string> keys = {"custom"};
			SetVirtualSource(keys, 24, [](string key, CollectionItem previous)->CollectionItem
			{
				TableRow row = TableRow::Create();
				array<string> cells = {"Custom"}; row.SetCells(cells); return row;
			});
		}
		VsguiLayoutColumns();
	}
};
void VerifyVirtualBuiltinRetry()
{
	array<string> labels = array<string>();
	array<array<string>> records = array<array<string>>();
	for (int64 index = 0; index < 100; index++)
	{
		labels.push(to_string(index));
		array<string> cells = {to_string(index)}; records.push(cells);
	}
	RetryBuiltinList retryList = RetryBuiltinList::Create().Height(Length(120));
	RetryBuiltinTable retryTable = RetryBuiltinTable::Create().Height(Length(120));
	array<TableColumn> columns = {TableColumn::Create().Key("name")};
	retryTable.SetColumns(columns);
	Stack root = Stack::Create()[{retryList, retryTable}];
	Context context = Context("test", 400, 300);
	try
	{
		context.SetRoot(root); context.Update(0);
		retryList.Fail = true; retryList.Expected = "99";
		bool rejected = false;
		try
		{
			retryList.SetVirtualItems(labels, 24);
		}
		catch (...)
		{
			rejected = true;
		}
		assert(rejected && retryList.IsVirtualized(), "list hook did not fail after source installation");
		context.Update(.001); context.Update(.002);
		retryList.Expected = "Retry list";
		retryList.SetVirtualItemText(99, "Retry list");
		assert(retryList.VsguiGetItemText("99").get() == "Retry list", "retried list lost its built-in data mode");
		retryTable.Fail = true; retryTable.Expected = "99"; rejected = false;
		try
		{
			retryTable.SetVirtualRows(records, 24);
		}
		catch (...)
		{
			rejected = true;
		}
		assert(rejected && retryTable.IsVirtualized(), "table hook did not fail after source installation");
		context.Update(.003); context.Update(.004);
		retryTable.Expected = "Retry table";
		retryTable.SetCellText("99", "name", "Retry table");
		assert(retryTable.GetCellText("99", "name") == "Retry table", "retried table lost its built-in data mode");
		retryTable.SortBy("name", true);
		context.Update(.005); context.Update(.006);
		retryTable.Replace = true; retryTable.Expected = "99";
		retryTable.SetVirtualRows(records, 24);
		context.Update(.007); context.Update(.008);
		assert(retryTable.GetItemCount() == 1 && retryTable.GetCellText("custom", "name") == "Custom",
			"outer built-in source installation overwrote a reentrant custom source mode");
	}
	finally
	{
		context.Close();
		root.Dispose();
	}
}

@GuiWidget export class ReentrantColumnTable : public VS::Gui::TableView
{
public:
	bool Replace = false;
	int Refreshes = 0;
protected:
	void VsguiItemsChanged() override
	{
		Refreshes++;
		if (Replace)
		{
			Replace = false;
			TableColumn column = TableColumn::Create().Key("inner").ColumnWidth(220);
			TableRow row = TableRow::Create().Key("inner-row");
			array<string> cells = {"Inner"}; row.SetCells(cells);
			array<Widget> children = {column, row}; SetChildren(children);
		}
		VsguiLayoutColumns();
	}
};
void VerifyReentrantTableColumns()
{
	ReentrantColumnTable view = ReentrantColumnTable::Create();
	array<TableColumn> initialColumns = {TableColumn::Create().Key("seed").ColumnWidth(100)};
	view.SetColumns(initialColumns);
	array<string> initialCells = {"Seed"};
	array<array<string>> initialRows = {initialCells}; view.SetRows(initialRows);
	Context context = Context("test", 400, 200);
	try
	{
		context.SetRoot(view); context.Update(0); context.Update(.001);
		TableColumn outerColumn = TableColumn::Create().Key("outer").ColumnWidth(120);
		TableRow outerRow = TableRow::Create().Key("outer-row");
		array<string> cells = {"Outer"}; outerRow.SetCells(cells);
		array<Widget> children = {outerColumn, outerRow};
		view.Replace = true; view.SetChildren(children);
		array<TableColumn> installedColumns = view.GetColumns();
		array<Widget> installedContent = view.GetContentChildren();
		assert(len(installedColumns) == 1 && installedColumns[0].GetKey() == "inner" &&
			len(installedContent) == 1 && try_cast<TableRow>(installedContent[0]).has_value(),
			"reentrant authored columns were not normalized before SetChildren returned");
		context.Update(.002); context.Update(.003);
		array<TableColumn> columns = view.GetColumns();
		array<Widget> content = view.GetContentChildren();
		assert(len(columns) == 1 && columns[0].GetKey() == "inner" &&
			len(content) == 1 && try_cast<TableRow>(content[0]).has_value(),
			"reentrant authored columns were not extracted into headers");
		assert(view.GetCellText("inner-row", "inner") == "Inner" &&
			view.FindItem("inner-row").GetChild(0).GetWidth() == Length(220),
			"reentrant header keys or column widths were stale");
		int settled = view.Refreshes;
		for (int index = 0; index < 6; index++)
		{
			context.Update(.004 + index * .001);
		}
		view.Synchronize();
		assert(view.Refreshes == settled, "settled table columns kept rebuilding");
	}
	finally
	{
		context.Close();
		view.Dispose();
	}
}

@GuiWidget export class NoopClearList : public VS::Gui::ListView
{
public:
	int Refreshes = 0;
protected:
	void VsguiItemsChanged() override
	{
		Refreshes++;
		ClearVirtualSource();
		RefreshItems();
	}
};
void VerifyNoopClearDoesNotRefresh()
{
	NoopClearList view = NoopClearList::Create();
	array<string> labels = {"Ordinary"}; view.SetItems(labels);
	Context context = Context("test", 400, 200);
	try
	{
		context.SetRoot(view); context.Update(0); context.Update(.001);
		view.RefreshItems(); view.Refreshes = 0;
		for (int index = 0; index < 6; index++)
		{
			context.Update(.002 + index * .001);
		}
		view.Synchronize();
		assert(view.Refreshes == 0 && view.GetItemCount() == 1,
			"a no-op clear or recursive manual refresh kept collection indexing dirty");
	}
	finally
	{
		context.Close();
		view.Dispose();
	}
}

class ClearSourceModel { public: string Text = "Inner custom"; };
weak<ClearSourceModel> clearSourceModel = weak<ClearSourceModel>();
int cancelledSourceCalls = 0;
@GuiWidget export class ClearReentryTable : public VS::Gui::TableView
{
public:
	int Action = 0;
	int Reentries = 0;
protected:
	void VsguiItemsChanged() override
	{
		if (Action != 0 && !IsVirtualized())
		{
			int action = Action; Action = 0; Reentries++;
			if (action == 1)
			{
				ClearSourceModel model = ClearSourceModel();
				clearSourceModel = weak<ClearSourceModel>(model);
				array<string> keys = {"inner"};
				SetVirtualSource(keys, 24, [model](string key, CollectionItem previous) mutable -> CollectionItem
				{
					TableRow row = TableRow::Create();
					array<string> cells = {model.Text}; row.SetCells(cells); return row;
				});
			}
			else if (action == 2)
			{
				array<string> cells = {"Inner builtin"};
				array<array<string>> records = {cells}; SetVirtualRows(records, 24);
			}
			else if (action == 3)
			{
				array<string> cells = {"Inner authored"};
				array<array<string>> records = {cells}; SetRows(records);
			}
			else if (action == 4)
			{
				ClearVirtualSource();
			}
			else
			{
				Dispose();
			}
		}
		if (!IsDisposed())
		{
			VsguiLayoutColumns();
		}
	}
};
@GuiWidget export class ClearReentryList : public VS::Gui::ListView
{
public:
	bool DisposeOnClear = false;
	bool ReplaceOnClear = false;
protected:
	void VsguiItemsChanged() override
	{
		if (!IsVirtualized())
		{
			if (DisposeOnClear)
			{
				DisposeOnClear = false;
				Dispose();
			}
			else if (ReplaceOnClear)
			{
				ReplaceOnClear = false;
				array<string> labels = {"Inner list"}; SetVirtualItems(labels, 24);
			}
		}
	}
};
@GuiWidget export class ConfigureReentryList : public VS::Gui::ListView
{
public:
	bool ReplaceOnConfigure = false;
protected:
	void VsguiConfigureVirtualSource(bool builtin) override
	{
		// This fixture uses custom sources only; its mode remains custom.
		if (ReplaceOnConfigure)
		{
			ReplaceOnConfigure = false;
			array<string> keys = {"configured"};
			SetVirtualSource(keys, 24, [](string key, CollectionItem previous)->CollectionItem
			{
				ListItem row = ListItem::Create().Text("Configured"); return row;
			});
		}
	}
};
weak<ClearSourceModel> InstallCancelledConfigureSource(ConfigureReentryList view)
{
	array<string> keys = {"outer", "another"};
	ClearSourceModel model = ClearSourceModel();
	weak<ClearSourceModel> capture = weak<ClearSourceModel>(model);
	view.SetVirtualSource(keys, 24, [model](string key, CollectionItem previous) mutable -> CollectionItem
	{
		cancelledSourceCalls++; ListItem row = ListItem::Create().Text(model.Text); return row;
	});
	return capture;
}
void VerifyVirtualSourceClearReentry()
{
	array<string> seedCells = {"Seed"};
	array<array<string>> seed = {seedCells};
	array<string> outerCells = {"Outer"};
	array<array<string>> outer = {outerCells, outerCells};
	for (int scenario = 1; scenario <= 7; scenario++)
	{
		int action = scenario <= 5 ? scenario : scenario - 5;
		ClearReentryTable view = ClearReentryTable::Create().Height(Length(120));
		array<TableColumn> columns = {TableColumn::Create().Key("name")};
		view.SetColumns(columns); view.SetVirtualRows(seed, 24);
		Context context = Context("test", 400, 200);
		try
		{
			context.SetRoot(view); context.Update(0);
			int64 previousSnapshot = CaptureVirtualSnapshot([view]()->void {});
			view.Action = action;
			if (scenario <= 5)
			{
				view.SetVirtualRows(outer, 24);
			}
			else
			{
				view.SetRows(outer);
			}
			assert(view.Reentries == 1 && VirtualSnapshotExpired(previousSnapshot),
				"clear reentry did not release the previous source");
			if (action == 5)
			{
				assert(view.IsDisposed(), "source installation continued after disposal");
			}
			else
			{
				context.Update(.001); context.Update(.002);
				if (action == 1)
				{
					assert(view.IsVirtualized() && view.GetItemCount() == 1 && view.GetCellText("inner", "name") == "Inner custom",
						"outer source overwrote the custom source installed during clear");
					assert(!clearSourceModel.expired(), "active custom source lost its capture");
					view.RefreshVirtualItems(); context.Update(.003); context.Update(.004);
					assert(view.GetItemCount() == 1 && view.GetCellText("inner", "name") == "Inner custom", "a superseded source still published rows");
				}
				else if (action == 2)
				{
					assert(view.IsVirtualized() && view.GetItemCount() == 1 && view.GetCellText("0", "name") == "Inner builtin",
						"outer data replaced the built-in source installed during clear");
					view.SetCellText("0", "name", "Changed");
					assert(view.GetCellText("0", "name") == "Changed", "inner built-in mode was lost");
				}
				else if (action == 3)
				{
					assert(!view.IsVirtualized() && view.GetItemCount() == 1 && view.GetCellText("0", "name") == "Inner authored",
						"null-to-null reentry lost authored rows or their index");
				}
				else
				{
					assert(!view.IsVirtualized() && view.GetItemCount() == 0, "outer source ignored a nested clear");
				}
			}
		}
		finally
		{
			context.Close();
			view.Dispose();
		}
		if (action == 1)
		{
			assert(clearSourceModel.expired(), "disposed inner source retained its capture");
		}
	}
	ClearReentryList list = ClearReentryList::Create();
	array<string> labels = {"Seed"}; list.SetVirtualItems(labels, 24); list.Initialize();
	list.DisposeOnClear = true; list.SetVirtualItems(labels, 24);
	assert(list.IsDisposed(), "list tried to set focusability after a clear callback disposed it");
	ClearReentryList replaced = ClearReentryList::Create();
	replaced.SetVirtualItems(labels, 24);
	Context replacementContext = Context("test", 400, 200);
	try
	{
		replacementContext.SetRoot(replaced); replacementContext.Update(0);
		replaced.ReplaceOnClear = true;
		array<string> authored = {"Outer authored"}; replaced.SetItems(authored);
		replacementContext.Update(.001); replacementContext.Update(.002);
		assert(replaced.IsVirtualized() && replaced.GetItemCount() == 1 && replaced.FindItem("0") != nullptr &&
			replaced.VsguiGetItemText("0").get() == "Inner list", "authored list replacement overwrote a reentrant source");
		replaced.SetVirtualItemText(0, "Changed");
		replacementContext.Update(.003); replacementContext.Update(.004);
		assert(replaced.VsguiGetItemText("0").get() == "Changed", "authored replacement reset the inner source mode");
	}
	finally
	{
		replacementContext.Close();
		replaced.Dispose();
	}

	ConfigureReentryList configured = ConfigureReentryList::Create();
	configured.Initialize(); configured.ReplaceOnConfigure = true;
	cancelledSourceCalls = 0;
	weak<ClearSourceModel> capture = InstallCancelledConfigureSource(configured);
	assert(cancelledSourceCalls == 0 && capture.expired() && configured.GetItemCount() == 1,
		"configuration reentry retained or realized a cancelled source");
	configured.RefreshItems();
	assert(configured.FindItem("configured") != nullptr, "configuration reentry lost the latest source");
	configured.Dispose();
}

@GuiWidget export class VirtualFocusProbeList : public VS::Gui::ListView
{
public:
	VS::Gui::Internal::VirtualRows VsguiTestCurrentSource() const { return VsguiGetVirtualRows(); }
};
@GuiWidget export class VirtualFocusProbeTable : public VS::Gui::TableView
{
public:
	VS::Gui::Internal::VirtualRows VsguiTestCurrentSource() const { return VsguiGetVirtualRows(); }
};
VirtualFocusProbeList virtualFocusList = nullptr;
VirtualFocusProbeTable virtualFocusTable = nullptr;
int64 virtualFocusAction = 0;
bool virtualFocusTableCase = false;
int virtualFocusReleases = 0;
weak<VS::Gui::Internal::VirtualRows> virtualFocusCancelledSource = weak<VS::Gui::Internal::VirtualRows>();
class VirtualFocusSourcePayload { public: string Text = "outer"; };
weak<VirtualFocusSourcePayload> virtualFocusPayload = weak<VirtualFocusSourcePayload>();
void PerformVirtualFocusReentry()
{
	virtualFocusReleases++;
	if (virtualFocusAction == 0) return;
	int64 current = virtualFocusAction; virtualFocusAction = 0;
	virtualFocusCancelledSource = weak<VS::Gui::Internal::VirtualRows>(virtualFocusTableCase ? virtualFocusTable.VsguiTestCurrentSource() : virtualFocusList.VsguiTestCurrentSource());
	if (current == 1)
	{
		if (virtualFocusTableCase)
		{
			virtualFocusTable.Dispose();
		}
		else
		{
			virtualFocusList.Dispose();
		}
	}
	else if (current == 2)
	{
		if (virtualFocusTableCase)
		{
			virtualFocusTable.ClearVirtualSource();
		}
		else
		{
			virtualFocusList.ClearVirtualSource();
		}
	}
	else if (current == 4)
	{
		throw "focus cleanup rejected";
	}
	else
	{
		array<string> labels = {"inner"};
		if (virtualFocusTableCase)
		{
			array<array<string>> rows = {labels}; virtualFocusTable.SetVirtualRows(rows,24);
		}
		else
		{
			virtualFocusList.SetVirtualItems(labels, 24);
		}
	}
}
class VirtualFocusRelease
{
public:
	bool Value = true;
	~VirtualFocusRelease() { PerformVirtualFocusReentry(); }
};
void ArmVirtualFocusReentry(VS::Gui::Widget view)
{
	VirtualFocusRelease model = VirtualFocusRelease();
	std::attribute<bool> source = std::attribute<bool>();
	source.bind([model]()->bool { return model.Value; });
	view.SetFocusable(source);
}
int64 virtualFocusScenario = 0;
void PrepareVirtualFocusReentry()
{
	int64 scenario = ++virtualFocusScenario;
	virtualFocusTableCase = (scenario >= 4 && scenario <= 6) || scenario == 8;
	virtualFocusAction = 0;
	virtualFocusReleases = 0;
	virtualFocusList = nullptr; virtualFocusTable = nullptr;
	if (virtualFocusTableCase)
	{
		virtualFocusTable = VirtualFocusProbeTable::Create();
		array<TableColumn> columns = {TableColumn::Create().Key("name")};
		virtualFocusTable.SetColumns(columns);
		ArmVirtualFocusReentry(virtualFocusTable);
	}
	else
	{
		virtualFocusList = VirtualFocusProbeList::Create();
		ArmVirtualFocusReentry(virtualFocusList);
	}
	assert(virtualFocusReleases == 0, "focus payload was not retained by the binding");
	virtualFocusAction = scenario >= 7 ? 4i64 : (virtualFocusTableCase ? scenario-3 : scenario);
}
void InstallVirtualFocusReentry()
{
	VirtualFocusSourcePayload model = VirtualFocusSourcePayload(); virtualFocusPayload = weak<VirtualFocusSourcePayload>(model);
	array<string> keys = {"outer"};
	if (virtualFocusTableCase)
	{
		virtualFocusTable.SetVirtualSource(keys,24,[model](string key,CollectionItem previous) mutable ->CollectionItem
		{
			TableRow row = TableRow::Create(); array<string> cells = {model.Text}; row.SetCells(cells); return row;
		});
	}
	else
	{
		virtualFocusList.SetVirtualSource(keys,24,[model](string key,CollectionItem previous) mutable ->CollectionItem
		{ ListItem row=ListItem::Create().Text(model.Text); return row; });
	}
}
void VerifyVirtualFocusReentry()
{
	int64 scenario = virtualFocusScenario;
	assert(virtualFocusReleases == 1 && virtualFocusAction == 0, "focus destruction did not execute exactly once");
	assert(virtualFocusPayload.expired(), "cancelled virtual source retained its factory payload");
	int64 expected = scenario >= 7 ? 4i64 : (virtualFocusTableCase ? scenario-3 : scenario);
	if (expected == 1)
	{
		assert(virtualFocusTableCase ? virtualFocusTable.IsDisposed() : virtualFocusList.IsDisposed(), "focus destructor did not dispose the view");
	}
	else if (expected == 4)
	{
		assert(virtualFocusTableCase ? !virtualFocusTable.IsDisposed() && !virtualFocusTable.IsVirtualized() : !virtualFocusList.IsDisposed() && !virtualFocusList.IsVirtualized(),
			"failed unregistered source remained installed");
		assert(virtualFocusCancelledSource.expired(), "failed unregistered source remained retained");
	}
	else
	{
		assert(virtualFocusCancelledSource.expired(), "cancelled virtual source was retained after focus reentry");
		if (expected == 2)
		{
			assert(virtualFocusTableCase ? !virtualFocusTable.IsVirtualized() : !virtualFocusList.IsVirtualized(), "focus clear was overwritten");
		}
		else if (virtualFocusTableCase)
		{
			assert(virtualFocusTable.IsVirtualized() && virtualFocusTable.GetCellText("0", "name") == "inner", "focus table replacement was overwritten");
		}
		else
		{
			assert(virtualFocusList.IsVirtualized() && virtualFocusList.VsguiGetItemText("0").get() == "inner", "focus list replacement was overwritten");
		}
	}
}
void CleanupVirtualFocusReentry()
{
	if (virtualFocusList != nullptr)
	{
		virtualFocusList.Dispose();
	}
	if (virtualFocusTable != nullptr)
	{
		virtualFocusTable.Dispose();
	}
	virtualFocusList = nullptr; virtualFocusTable = nullptr;
}

void VerifyVirtualRows()
{
	array<string> labels = array<string>();
	array<array<string>> records = array<array<string>>();
	for (int64 index = 0; index < 1000; index++)
	{
		labels.push("Row " + to_string(index));
		array<string> cells = {to_string(1000 - index), "Ready"};
		records.push(cells);
	}
	ListView virtualList = ListView::Create().Height(Length(120));
	virtualList.SetVirtualItems(labels, 24);
	virtualList.ScrollToItem("900");
	TableView virtualTable = TableView::Create().Height(Length(120));
	array<TableColumn> columns = {TableColumn::Create().Key("name").Text("Name"), TableColumn::Create().Key("state").Text("State")};
	virtualTable.SetColumns(columns);
	virtualTable.SetVirtualRows(records, 24);
	virtualTable.ScrollToItem("900");
	virtualTable.SetScrollY(0);
	Stack root = Stack::Create()[{virtualList, virtualTable}];
	Context context = Context("test", 400, 300);
	try
	{
		context.SetRoot(root);
		context.Update(0);
		context.Update(.001);
		assert(virtualList.FindItem("900") != nullptr,
			"scrolling before first layout did not reveal the requested row");
		assert(virtualTable.FindItem("0") != nullptr && virtualTable.FindItem("900") == nullptr,
			"an initial reveal overwrote a later explicit scroll");
		assert(virtualList.GetItemCount() == 1000 && virtualList.GetRealizedItemCount() < 20 &&
			virtualTable.GetItemCount() == 1000 && virtualTable.GetRealizedItemCount() < 20,
			"virtual rows must realize the viewport instead of the full source");
		assert(virtualList.GetScrollHeight() >= 23990, "virtual list lost its logical scroll extent");
		virtualList.Select("900");
		virtualList.ScrollToItem("900");
		context.Update(.002);
		context.Update(.003);
		CollectionItem selected = virtualList.FindItem("900");
		assert(selected != nullptr && selected.IsSelected() && virtualList.GetActualSelectedKey() == "900" &&
			virtualList.GetRealizedItemCount() < 20, "offscreen virtual selection or scrolling failed");
		selected.Focus();
		virtualList.ScrollToItem("0");
		context.Update(.004);
		context.Update(.005);
		assert(context.GetFocus() == selected && virtualList.FindItem("900") == selected &&
			virtualList.GetRealizedItemCount() < 21, "a focused virtual row was recycled");
		context.SetFocus(nullptr);
		context.Update(.006);
		context.Update(.007);
		assert(virtualList.FindItem("900") == nullptr && virtualList.GetActualSelectedKey() == "900",
			"virtual selection must survive release of its realized row");
		virtualList.SetVirtualItemText(0, "Changed");
		context.Update(.008);
		context.Update(.009);
		assert(virtualList.FindItem("0").GetText() == "Changed", "virtual item data refresh");
		virtualTable.SetCellText("900", "state", "Updated");
		assert(virtualTable.GetCellText("900", "state") == "Updated", "virtual offscreen cell access");
		virtualTable.Select("900");
		virtualTable.SortBy("name", true);
		context.Update(.010);
		context.Update(.011);
		assert(virtualTable.GetActualSelectedKey() == "900" && virtualTable.GetRealizedItemCount() < 20 &&
			virtualTable.GetCellText("900", "state") == "Updated", "virtual table sorting lost logical identity");
		array<string> ordinary = {"Ordinary"};
		virtualList.SetItems(ordinary);
		assert(!virtualList.IsVirtualized() && virtualList.GetItemCount() == 1,
			"ordinary list mode did not replace the virtual source");
	}
	finally
	{
		context.Close(); root.Dispose();
	}
	VerifyVirtualComboLabels();
}

void Setup()
{
	ListItem a = ListItem::Create().Key("a").Text("Alpha").Id("list-a");
	ListItem b = ListItem::Create().Key("b").Text("Beta").Id("list-b");
	std::attribute<string> selection = std::attribute<string>();
	selection.bind([]()->string
	{
		return selectedModel;
	});
	list = ListView::Create().Id("list").Height(Length(90)).SelectedKey(selection)
		.OnSelectionChanged([](ListView source, string value)->void
	{
		selectedEvents++;
	})
		.OnItemActivated([](ListView source, string value)->void
	{
		activatedEvents++;
	})[{a, b}];
	list.Initialize();
	assert(len(list.GetItems()) == 2 && list.FindItem("a") == a, "list item ownership");
	assert(a.IsSelected() && list.GetActualSelectedKey() == "a", "initial selection");
	list.Select("b");
	assert(list.IsSelectedKeyBound() && list.GetSelectedKey() == "a" && b.IsSelected(), "bound selection candidate");
	list.Synchronize();
	assert(a.IsSelected() && !b.IsSelected(), "bound selection restores unchanged model");
	selectedModel = "b";
	list.Synchronize();
	assert(b.IsSelected() && !a.IsSelected(), "dynamic selection model");

	std::attribute<bool> expansion = std::attribute<bool>();
	expansion.bind([]()->bool
	{
		return dynamicExpanded;
	});
	TreeItem branch = TreeItem::Create().Id("branch").Key("branch").Text("Branch").Expanded(expansion)[{
		TreeItem::Create().Id("leaf").Key("leaf").Text("Leaf")
	}];
	tree = TreeView::Create().Height(Length(100))[{branch}];
	tree.Initialize();
	assert(len(tree.GetItems()) == 2, "tree descendants indexed");
	branch.RequestExpanded(false);
	assert(branch.GetContentChildren()[0].__GetNativeHandle().IsContentVisible(),
		"leaf remains authored content");
	tree.Synchronize();
	assert(branch.GetExpanded(), "bound expansion remains intact");
	dynamicExpanded = false;
	tree.Synchronize();
	assert(!VS::Gui::Internal::IsInteractive(tree.FindItem("leaf")), "collapsed descendants do not interact");
	dynamicExpanded = true;
	tree.Synchronize();

	table = TableView::Create().Height(Length(130));
	array<TableColumn> columns = {TableColumn::Create().Key("name").Text("Name").ColumnWidth(140),
		TableColumn::Create().Key("value").Text("Value").ColumnWidth(90)};
	table.SetColumns(columns);
	array<array<string>> rows = {{"Beta", "2"}, {"Alpha", "1"}, {"Alpha", "3"}};
	table.SetRows(rows);
	table.Initialize();
	table.Select("0");
	table.SortBy("name", true);
	assert(table.GetContentChildren()[0].GetText() == "", "table preserves row widgets");
	assert(table.GetContentChildren()[0] == table.FindItem("1") &&
		table.GetContentChildren()[1] == table.FindItem("2"), "stable table sorting");
	assert(table.GetActualSelectedKey() == "0", "sort preserves keyed selection");
	table.SetCellText("0", "value", "20");
	assert(table.GetCellText("0", "value") == "20", "table cell text update");

	combo = ComboBox::Create().Id("combo").Width(Length(180)).Placeholder("Choose").SelectedKey("b")[{
		ListItem::Create().Key("a").Text("Alpha"), ListItem::Create().Key("b").Text("Beta")
	}];
	combo.Initialize();
	assert(combo.GetText() == "Beta", "combo displays selected text");
	combo.Select("a");
	assert(combo.GetSelectedKey() == "a" && combo.GetText() == "Alpha", "combo selection");
	ComboBox boundCombo = ComboBox::Create().SelectedKey(selection)[{
		ListItem::Create().Key("a").Text("Alpha"), ListItem::Create().Key("b").Text("Beta")
	}];
	boundCombo.Initialize();
	boundCombo.Select("a");
	assert(boundCombo.GetActualSelectedKey() == "a" && boundCombo.GetText() == "Alpha", "combo bound candidate label");
	boundCombo.Synchronize();
	assert(boundCombo.GetActualSelectedKey() == "b" && boundCombo.GetText() == "Beta" && boundCombo.IsSelectedKeyBound(),
		"combo restores the unchanged authoritative binding");
	boundCombo.Dispose();

	tabs = TabView::Create().Id("tabs")[{
		TabItem::Create().Key("one").Title("One")[{VS::Gui::Text::Create().Text("Page one")}],
		TabItem::Create().Key("two").Title("Two")[{VS::Gui::Text::Create().Text("Page two")}]
	}];
	tabs.Initialize();
	assert(tabs.GetActualSelectedKey() == "one", "first tab selected");
	tabs.Select("two");
	array<Widget> pages = tabs.GetContentChildren();
	assert(!pages[0].__GetNativeHandle().IsContentVisible() && pages[1].__GetNativeHandle().IsContentVisible(),
		"tab pages remain mounted and separately suppressed");
	array<Widget> replacementPages = {TabItem::Create().Key("two").Title("Replaced")};
	tabs.SetChildren(replacementPages);
	assert(tabs.GetContentChildren()[0].__GetNativeHandle().IsContentVisible(), "same-key page replacement");
	pages[0].Synchronize();
	assert(pages[0].__GetNativeHandle().IsContentVisible() && pages[0].GetVisibility() == Visibility::Visible,
		"a detached inactive page can be reused outside tabs without losing authored visibility");
	std::attribute<string> tabKey = std::attribute<string>();
	tabKey.bind([]()->string
	{
		return dynamicTabKey;
	});
	std::attribute<string> tabTitle = std::attribute<string>();
	tabTitle.bind([]()->string
	{
		return dynamicTabTitle;
	});
	dynamicTabs = TabView::Create()[{
		TabItem::Create().Key("first").Title("First"), TabItem::Create().Key(tabKey).Title(tabTitle)
	}];
	dynamicTabs.Initialize();

	std::attribute<bool> openBinding = std::attribute<bool>();
	openBinding.bind([]()->bool
	{
		return modelOpen;
	});
	Dialog boundDialog = Dialog::Create().Open(openBinding);
	boundDialog.Initialize();
	boundDialog.Close("candidate");
	boundDialog.Synchronize();
	assert(boundDialog.IsOpenBound() && boundDialog.IsActuallyOpen() && boundDialog.GetResult() == "",
		"bound dialog reopen resets the previous candidate result");
	boundDialog.Dispose();

	std::attribute<bool> checkBinding = std::attribute<bool>();
	checkBinding.bind([]()->bool
	{
		return checkedModel;
	});
	MenuItem checkedItem = MenuItem::Create().Checkable(true).Checked(checkBinding);
	checkedItem.Initialize();
	checkedItem.AddCheckedChanged([](MenuItem source, bool value)->void
	{
		checkedCandidates.push(value);
	});
	checkedItem.Invoke();
	assert(checkedItem.IsActuallyChecked() && !checkedItem.GetChecked() && checkedItem.IsCheckedBound(),
		"menu checked candidate retains the model");
	checkedItem.Invoke();
	assert(len(checkedCandidates) == 2 && checkedCandidates[0] && !checkedCandidates[1] && !checkedItem.IsActuallyChecked(),
		"consecutive menu requests toggle the actual candidate");
	checkedItem.Invoke();
	checkedItem.Synchronize();
	assert(!checkedItem.IsActuallyChecked() && checkedItem.IsCheckedBound(), "menu synchronization restores the unchanged model");
	checkedModel = true;
	checkedItem.Synchronize();
	checkedItem.Invoke();
	assert(!checkedItem.IsActuallyChecked() && checkedItem.GetChecked(), "menu candidate follows a changed bound model");
	checkedItem.Dispose();
	Dialog reentrantDialog = Dialog::Create();
	reentrantDialog.Initialize();
	reentrantDialog.AddOpenChanged([](Popup source, bool value)->void
	{
		if (!value && reopenDialog)
		{
			reopenDialog = false;
			cast<Dialog>(source).Show();
		}
	});
	reentrantDialog.AddClosed([](Dialog source, string value)->void
	{
		reopenedResult = value;
	});
	reentrantDialog.Show();
	reentrantDialog.Close("original-result");
	assert(reentrantDialog.IsActuallyOpen() && reentrantDialog.GetResult() == "" && reopenedResult == "original-result",
		"reopening in OpenChanged preserves the completed close result snapshot");
	reentrantDialog.Dispose();

	MenuItem firstMenu = MenuItem::Create()[{MenuItem::Create().Text("First")}];
	MenuItem nextMenu = MenuItem::Create()[{MenuItem::Create().Text("Next")}];
	menuVictim = MenuItem::Create()[{MenuItem::Create().Text("Removed")}];
	Menu reentrantMenu = Menu::Create()[{firstMenu, nextMenu, menuVictim}];
	reentrantMenu.Initialize();
	firstMenu.OpenMenu();
	cast<Popup>(firstMenu.GetChild(0)).AddOpenChanged([](Popup source, bool value)->void
	{
		if (!value && menuVictim != nullptr)
		{
			menuVictim.Dispose();
		}
	});
	nextMenu.OpenMenu();
	assert(menuVictim.IsDisposed() && nextMenu.IsMenuOpen(), "closing a peer may dispose another snapshotted menu");
	reentrantMenu.Dispose();
	menuVictim = nullptr;

	menu = Menu::Create()[{MenuItem::Create().Id("menu-file").Text("File")[{
		MenuItem::Create().Id("open-command").Text("Open").Command("open"),
		MenuItem::Create().Id("menu-recent").Text("Recent")[{MenuItem::Create().Id("menu-first").Text("First").Command("first")}]
	}], MenuItem::Create().Id("menu-view").Text("View")[{MenuItem::Create().Text("Details").Command("details")}]}];
	menu.AddInvoked([](Menu source, string command)->void
	{
		assert(command == "open", "menu command");
		menuEvents++;
	});
	menu.Initialize();
	cast<MenuItem>(menu.Find("menu-file")).OpenMenu();
	assert(cast<MenuItem>(menu.Find("menu-file")).IsMenuOpen(), "unmounted menus can prepare their initial open state");
	menu.CloseMenus();
	dialog = Dialog::Create().Id("dialog").Height(Length(100))[
		{Button::Create().Id("dialog-ok").Text("OK")}];
	dialog.AddClosed([](Dialog source, string result)->void
	{
		closedEvents++;
		lastClosed = result;
	});
	Stack root = Stack::Create().Background(Color(1, 1, 1, 1))[{list, tree, combo, tabs, menu, dialog}];
	gui = Context("test", 700, 700);
	gui.SetRoot(root);
	gui.Update(0);
	combo.RequestOpen(true);
	gui.Update(.01);
	assert(combo.IsActuallyOpen(), "combo popup opens");
	combo.RequestOpen(false);
	gui.Update(.02);
	cast<MenuItem>(menu.Find("open-command")).Invoke();
	// A closed submenu cannot be invoked physically or programmatically through Invoke.
	assert(menuEvents == 0, "closed submenu input suppression");
	cast<MenuItem>(menu.GetContentChildren()[0]).OpenMenu();
	gui.Update(.03);
	cast<MenuItem>(menu.Find("open-command")).Invoke();
	assert(menuEvents == 1 && !cast<MenuItem>(menu.GetContentChildren()[0]).IsMenuOpen(), "menu activation closes chain");
	gui.Update(.04);
}

void VerifyAlive()
{
	assert(list != nullptr && gui != nullptr, "global GUI objects did not survive a session entry");
}
void VerifyTreeCollapsed()
{
	assert(!cast<TreeItem>(tree.Find("branch")).IsActuallyExpanded() &&
		!VS::Gui::Internal::IsInteractive(tree.Find("leaf")), "tree Left collapses the branch candidate");
}
void VerifyTreeExpanded()
{
	assert(cast<TreeItem>(tree.Find("branch")).IsActuallyExpanded() &&
		VS::Gui::Internal::IsInteractive(tree.Find("leaf")), "tree Right expands the branch candidate");
}
void VerifyArrowDown()
{
	assert(list.GetActualSelectedKey() == "b" && activatedEvents == 1, "Down navigates without activating an item");
}
void VerifyArrowHome()
{
	assert(list.GetActualSelectedKey() == "a" && activatedEvents == 1, "Home navigates without activating an item");
}

void TryInvalidColumns()
{
	array<TableColumn> incompatible = {TableColumn::Create().Key("only")};
	table.SetColumns(incompatible);
}
void VerifyInvalidColumns()
{
	assert(len(table.GetColumns()) == 2 && table.GetCellText("0", "value") == "20",
		"invalid column replacement preserves the existing table");
	table.Dispose();
}
void SetDuplicateTabKey()
{
	dynamicTabKey = "first";
}
void TryDuplicateTabs()
{
	dynamicTabs.Synchronize();
}
void VerifyDynamicTabs()
{
	dynamicTabKey = "next";
	dynamicTabTitle = "Updated";
	dynamicTabs.Synchronize();
	dynamicTabs.Select("next");
	assert(dynamicTabs.GetActualSelectedKey() == "next" && dynamicTabs.GetChild(0).GetChild(1).GetText() == "Updated",
		"dynamic tab key/title recover after duplicate key");
	dynamicTabs.Dispose();
}

void MenuUpdate()
{
	gui.Update(menuTime);
	menuTime += .0001;
}
void OpenNestedMenus()
{
	cast<MenuItem>(menu.Find("menu-file")).OpenMenu();
	MenuUpdate();
	cast<MenuItem>(menu.Find("menu-recent")).OpenMenu();
	MenuUpdate();
	assert(cast<MenuItem>(menu.Find("menu-recent")).IsMenuOpen(), "nested menu open");
}
void VerifyNestedEscape()
{
	MenuUpdate();
	assert(!cast<MenuItem>(menu.Find("menu-recent")).IsMenuOpen() &&
		cast<MenuItem>(menu.Find("menu-file")).IsMenuOpen(), "Escape closes only the current menu level");
}
void VerifyMenuClosed()
{
	MenuUpdate();
	assert(!cast<MenuItem>(menu.Find("menu-file")).IsMenuOpen() &&
		!cast<MenuItem>(menu.Find("menu-recent")).IsMenuOpen(), "active menu chain closed");
}
void OpenMenuForSwitch()
{
	cast<MenuItem>(menu.Find("menu-file")).OpenMenu();
	MenuUpdate();
}
void VerifyMenuSwitch()
{
	MenuUpdate();
	assert(!cast<MenuItem>(menu.Find("menu-file")).IsMenuOpen() &&
		cast<MenuItem>(menu.Find("menu-view")).IsMenuOpen(), "one pointer click switches root menus");
	menu.CloseMenus();
	MenuUpdate();
}

void VerifyInput()
{
	assert(list.GetActualSelectedKey() == "a" && selectedEvents >= 2 && activatedEvents == 1, "physical list activation");
	gui.Update(.05);
	assert(list.GetActualSelectedKey() == selectedModel, "physical input preserves model");
	dialog.Show();
	gui.Update(.06);
	assert(dialog.IsActuallyOpen(), "dialog show");
}
void VerifyDialog()
{
	gui.Update(.07);
	assert(!dialog.IsActuallyOpen() && closedEvents == 1 && lastClosed == "", "escape closes dialog once");
	dialog.Show();
	gui.Update(.08);
	dialog.Close("ok");
	gui.Update(.09);
	assert(closedEvents == 2 && lastClosed == "ok", "dialog result");
	tabs.GetContentChildren()[0].Dispose();
	assert(len(tabs.GetContentChildren()) == 0 && tabs.GetActualSelectedKey() == "", "disposing an inactive/active tab does not mutate a disposing page");
	CollectionItem removed = list.FindItem("b");
	removed.Dispose();
	gui.Update(.10);
	assert(list.FindItem("b") == nullptr && list.GetActualSelectedKey() == "", "disposed collection item leaves no stale index or selection");
	list.Select("a");
	migratedItem = cast<ListItem>(list.FindItem("a"));
}
void TryInvalidStructure()
{
	ListItem first = ListItem::Create().Key("same").Text("First");
	ListItem second = ListItem::Create().Key("same").Text("Second");
	array<Widget> replacement = {first, second};
	list.SetChildren(replacement);
}
void VerifyStructureRetry()
{
	ListView other = ListView::Create().Height(Length(40))[{migratedItem}];
	other.Initialize();
	assert(!migratedItem.IsSelected(), "a new owner does not inherit the previous owner's selection");
	other.AddSelectionChanged([](ListView source, string value)->void
	{
		migratedSelections++;
	});
	gui.GetRoot().AddChild(other);
	cast<ListItem>(list.GetContentChildren()[1]).SetKey("repaired");
	gui.Update(.11);
	assert(list.FindItem("same") == list.GetContentChildren()[0] &&
		list.FindItem("repaired") == list.GetContentChildren()[1],
		"failed structure notification retries the committed children before analysis");
	migratedItem.Click();
	assert(migratedSelections == 1 && other.GetActualSelectedKey() == "a" && migratedItem.IsSelected(),
		"an old notification retry cannot steal or deselect a reparented item");
	other.Dispose();
	migratedItem = nullptr;
	gui.Close();
}

void VerifyMarkup()
{
	gui = Context("test", 700, 700);
	Widget root = gui.LoadWidget("composite.vswml");
	gui.SetRoot(root);
	gui.Update(1);
	assert(try_cast<ComboBox>(root.Find("markup-combo")).has_value(), "ML combo type");
	assert(cast<TableView>(root.Find("markup-table")).GetCellText("r", "c") == "Cell", "ML table content routing");
	assert(cast<TabView>(root.Find("markup-tabs")).GetActualSelectedKey() == "first", "ML tab template");
}
void CloseMarkup()
{
	gui.Close();
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using namespace VS::Gui;

Stack sidebar = nullptr;
Stack body = nullptr;
VS::Gui::Text heading = nullptr;
VS::Gui::Text counter = nullptr;
Button menu = nullptr;
Button add = nullptr;
array<Button> navigation = array<Button>();
bool expanded = true;
int completed = 0;

AnimationOptions Motion()
{
	AnimationOptions options = AnimationOptions();
	options.Duration = 0.24;
	options.Easing = Easing::EaseInOut;
	options.HoldEnd = true;
	return options;
}

void ToggleSidebar(Button sender)
{
	expanded = !expanded;
	double width = expanded ? 224.0 : 76.0;
	// A new animation starts from the current presentation, including mid-flight reversal.
	sidebar.AnimateWidth(Length(width), Motion());
	body.AnimatePadding(Insets(28, 28, 28, width + 28), Motion());
	sender.SetText(expanded ? "收起侧栏" : "展开侧栏");
	array<string> fullLabels = {"工作台", "消息", "设置"};
	array<string> shortLabels = {"首", "信", "设"};
	for (int64 i = 0; i < len(navigation); i++)
	{
		navigation[i].SetText(expanded ? fullLabels[i] : shortLabels[i]);
		navigation[i].SetTextAlign(expanded ? "left" : "center");
	}
}

Button Navigation(string label)
{
	return Button::Create().Text(label).Height(Length(44)).CornerRadius(10)
		.TextAlign("left").Padding(Insets(0, 12, 0, 12))
		.Background(Color(0.035, 0.050, 0.080, 1)).TextColor(Color(0.80, 0.86, 0.95, 1))
		.OnClicked([label](Button sender) -> void
		{
			heading.SetText(label);
			heading.StopOpacityAnimation();
			heading.SetOpacity(0.35);
			heading.AnimateOpacity(1, Motion());
		});
}

void main()
{
	menu = Button::Create().Text("收起侧栏").Width(Length(136)).Height(Length(40))
		.CornerRadius(10).Background(Color(0.025, 0.10, 0.52, 1))
		.TextColor(Color(1, 1, 1, 1)).OnClicked(ToggleSidebar);
	heading = VS::Gui::Text::Create().Text("工作台").FontSize(28).Height(Length(44));
	counter = VS::Gui::Text::Create().Text("今天已完成 0 项").FontSize(14).Height(Length(28));
	add = Button::Create().Text("完成一项 +1").Width(Length(164)).Height(Length(40))
		.CornerRadius(10).Background(Color(0.025, 0.10, 0.52, 1)).TextColor(Color(1, 1, 1, 1))
		.OnClicked([](Button sender) -> void
		{
			completed++;
			counter.SetText("今天已完成 " + to_string(completed) + " 项");
		});
	navigation.push(Navigation("工作台"));
	navigation.push(Navigation("消息"));
	navigation.push(Navigation("设置"));
	sidebar = Stack::Create().Width(Length(224)).Padding(Insets(28, 14, 28, 14))
		.Gap(12).ClipContent(true).Background(Color(0.017, 0.025, 0.047, 1))[
	{
		VS::Gui::Text::Create().Text("VS").FontSize(24).Height(Length(52))
			.TextAlign("center").TextColor(Color(0.70, 0.81, 1, 1)),
		navigation[0], navigation[1], navigation[2]
	}];
	Stack card = Stack::Create().Height(Length(236)).Padding(Insets(24, 24, 24, 24))
		.Gap(12).CornerRadius(16).Background(Color(1, 1, 1, 1))
		.BorderWidth(1).BorderColor(Color(0.80, 0.85, 0.92, 1))[
	{
		VS::Gui::Text::Create().Text("一个轻量的小窗口").FontSize(18).Height(Length(32)),
		VS::Gui::Text::Create().Text("点击左上角收起侧栏；动画中再次点击，也会平滑反向。").FontSize(14).Height(Length(48)),
		add, counter
	}];
	body = Stack::Create().Padding(Insets(28, 28, 28, 252)).Gap(18)[
	{
		menu, heading,
		VS::Gui::Text::Create().Text("侧栏收起后，内容会同步让出空间。试试左侧导航和计数按钮。")
			.FontSize(14).Height(Length(48)).TextColor(Color(0.19, 0.25, 0.34, 1)),
		card
	}];
	Overlay root = Overlay::Create().FontFamily("Demo Sans").FontSize(14)
		.TextColor(Color(0.025, 0.040, 0.070, 1)).Background(Color(0.93, 0.95, 0.98, 1))[
	{
		body, sidebar
	}];
	ApplicationOptions options = ApplicationOptions();
	options.Title = "VeroScript - 侧栏动画";
	options.Width = 960;
	options.Height = 620;
	Application app = Application(options);
	try
	{
		// Windows Chinese font; use an available font path on other platforms.
		app.GetContext().LoadFont("C:/Windows/Fonts/msyh.ttc", "Demo Sans");
		app.SetRoot(root);
		app.Run();
	}
	finally
	{
		app.Close();
	}
}

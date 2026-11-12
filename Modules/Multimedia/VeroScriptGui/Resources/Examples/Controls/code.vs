// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using namespace VS::Gui;

double amount = 0.35;
string message = "Edit this text";

void main()
{
	std::attribute<double> value = std::attribute<double>();
	value.bind([]() -> double
	{
		return amount;
	});
	std::attribute<string> text = std::attribute<string>();
	text.bind([]() -> string
	{
		return message;
	});
	Stack root = Stack::Create().Padding(Insets(16, 16, 16, 16)).Gap(10)[
	{
		VS::Gui::Text::Create().Text("Hello, VeroScript!"), Button::Create().Text("Click me").OnClicked([](Button sender) -> void
		{
			message = "Button clicked";
		}), CheckBox::Create().Text("Accept").Checked(true), ToggleSwitch::Create().Text("Enabled"), Slider::Create().Value(value).OnValueChanged([](Slider sender, double candidate) -> void
		{
			amount = candidate;
		}), ProgressBar::Create().Value(value), TextBox::Create().Value(text).OnTextChanged([](TextBox sender, string candidate) -> void
		{
			message = candidate;
		}), TextArea::Create().Value("First line\nSecond line"), ScrollView::Create().Height(Length(80))[
		{
			Border::Create().Height(Length(180))[
			{
				VS::Gui::Text::Create().Text("Scroll content")
			}]
		}]
	}];
	ApplicationOptions options = ApplicationOptions();
	options.Title = "VeroScript controls from code";
	Application app = Application(options);
	try
	{
		app.SetRoot(root);
		app.Run();
	}
	finally
	{
		app.Close();
	}
}

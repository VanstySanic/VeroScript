// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using namespace VS::Gui;

void main()
{
	Observable<double> amount = Observable<double>(0.35);
	Observable<bool> enabled = Observable<bool>(true);
	Binding<double> value = amount.AsBinding();
	Binding<bool> active = enabled.AsBinding();
	array<ChangeSignal> inputs = {amount};
	Binding<string> caption = Binding<string>([amount]() -> string
	{
		return format("Value: {}%", cast<int64>(amount.Get() * 100));
	}, inputs);

	Stack root = Stack::Create().Padding(Insets(24, 24, 24, 24)).Gap(16)[
	{
		VS::Gui::Text::Create().Text("Notification bindings").FontSize(24), VS::Gui::Text::Create().Text("Only changed inputs reevaluate their dependent properties."), VS::Gui::Text::Create().Text(caption), Slider::Create().Value(value).Enabled(active).OnValueChanged([amount](Slider sender, double candidate) mutable -> void
		{
			amount.Set(candidate);
		}), ProgressBar::Create().Value(value), CheckBox::Create().Text("Enable slider").Checked(active).OnCheckedChanged([enabled](CheckBox sender, bool candidate) mutable -> void
		{
			enabled.Set(candidate);
		}), Button::Create().Text("Reset together").OnClicked([amount, enabled](Button sender) mutable -> void
		{
			BindingUpdates::Batch([amount, enabled]() mutable -> void
			{
				amount.Set(0.35); enabled.Set(true);
			});
		})
	}];

	ApplicationOptions options = ApplicationOptions();
	options.Title = "VeroScript notification bindings";
	options.Width = 700;
	options.Height = 360;
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

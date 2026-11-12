// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

namespace VeroScriptMauiHost;

public sealed class App : Application
{
	protected override Window CreateWindow(IActivationState? activationState)
	{
		var result = new Label { Text = "Ready", LineBreakMode = LineBreakMode.WordWrap };
		var run = new Button { Text = "Run native integration checks" };
		run.Clicked += async (_, _) =>
		{
			run.IsEnabled = false;
			try
			{
				string root = await VeroScriptMobileResources.InstallAsync(
					Path.Combine(FileSystem.AppDataDirectory, "veroscript-0.1.1"),
					(name, _) => FileSystem.OpenAppPackageFileAsync(name));
				// Creation, execution, callbacks and disposal remain on one worker thread.
				result.Text = await Task.Run(() => MobileSmoke.Run(root, FileSystem.CacheDirectory));
			}
			catch (Exception error)
			{
				result.Text = error.ToString();
			}
			finally
			{
				run.IsEnabled = true;
			}
		};
		return new Window(new ContentPage
		{
			Content = new ScrollView
			{
				Content = new VerticalStackLayout { Padding = 24, Spacing = 12, Children = { run, result } }
			}
		});
	}
}

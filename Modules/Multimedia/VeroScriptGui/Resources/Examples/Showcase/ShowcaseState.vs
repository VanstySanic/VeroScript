// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace Showcase
{
	export class PageState
	{
	public:
		PageState()
		{
			record("Ready. Change the settings to update the preview.");
		}

		@ GuiMethod string readName() const
		{
			return projectName;
		}
		@ GuiMethod string readPassword() const
		{
			return password;
		}
		@ GuiMethod string readNotes() const
		{
			return notes;
		}
		@ GuiMethod double readGain() const
		{
			return gain;
		}
		@ GuiMethod bool readControlsEnabled() const
		{
			return controlsEnabled;
		}
		@ GuiMethod bool readDetails() const
		{
			return details;
		}
		@ GuiMethod bool readMonochrome() const
		{
			return monochrome;
		}
		@ GuiMethod bool readHelpOpen() const
		{
			return helpOpen;
		}
		@ GuiMethod string readMode() const
		{
			return mode;
		}
		@ GuiMethod string readCount() const
		{
			return to_string(eventCount);
		}
		@ GuiMethod string readGainLabel() const
		{
			return format("{}%", cast<int64>(gain));
		}
		@ GuiMethod string readStatus() const
		{
			return status;
		}

		@ GuiMethod string readTitle() const
		{
			return projectName == "" ? "Untitled workspace" : projectName;
		}

		@ GuiMethod string readModeLabel() const
		{
			return mode == "vivid" ? "Vivid" : "Balanced";
		}

		@ GuiMethod double readPreviewOpacity() const
		{
			return controlsEnabled ? 1.0 : 0.45;
		}

		@ GuiMethod string readPreviewFilter() const
		{
			return monochrome ? "grayscale(1)" : "";
		}

		@ GuiMethod string readPreviewBackground() const
		{
			return mode == "vivid" ? "linear-gradient(120deg, #243b82, #6945a0)" : "linear-gradient(120deg, #19344c, #1d5064)";
		}

		@ GuiMethod VS::Gui::Visibility readDetailsVisibility() const
		{
			return details ? VS::Gui::Visibility::Visible : VS::Gui::Visibility::Collapsed;
		}

		@ GuiMethod string readLog() const
		{
			string result = "";
			for (int64 index = len(history); index > 0; index--)
			{
				result += history[index - 1] + "\n";
			}
			return result;
		}

		@ GuiMethod void changeName(VS::Gui::TextBox source, string value)
		{
			projectName = value;
		}

		@ GuiMethod void submitName(VS::Gui::TextBox source, string value)
		{
			record("Project name committed: " + value);
		}

		@ GuiMethod void changePassword(VS::Gui::TextBox source, string value)
		{
			password = value;
		}

		@ GuiMethod void changeNotes(VS::Gui::TextBox source, string value)
		{
			notes = value;
		}

		@ GuiMethod void changeGain(VS::Gui::Slider source, double value)
		{
			gain = value;
			record("Slider changed gain to " + readGainLabel());
		}

		@ GuiMethod void enterGain(VS::Gui::NumberBox source, double value)
		{
			gain = value;
			record("Number input committed gain " + readGainLabel());
		}

		@ GuiMethod void changeControlsEnabled(VS::Gui::CheckBox source, bool value)
		{
			controlsEnabled = value;
			record(value ? "Gain controls enabled" : "Gain controls disabled");
		}

		@ GuiMethod void changeDetails(VS::Gui::CheckBox source, bool value)
		{
			details = value;
			record(value ? "Details shown" : "Details collapsed");
		}

		@ GuiMethod void changeMonochrome(VS::Gui::CheckBox source, bool value)
		{
			monochrome = value;
			record(value ? "Grayscale filter enabled" : "Grayscale filter cleared");
		}

		@ GuiMethod void changeMode(VS::Gui::RadioGroup source, string value)
		{
			mode = value;
			record("Mode changed to " + readModeLabel());
		}

		@ GuiMethod void applyPreset(VS::Gui::Button source)
		{
			gain = mode == "vivid" ? 85 : 60;
			record("Applied the " + readModeLabel() + " preset");
			source.AnimateBackground(VS::Gui::Color(0.025, 0.30, 0.23, 1), 0.3);
		}

		@ GuiMethod void reset(VS::Gui::Button source)
		{
			projectName = "Northstar workspace";
			password = "";
			notes = "Try text selection, clipboard, undo and multiline editing.";
			gain = 65;
			mode = "balanced";
			controlsEnabled = true;
			details = true;
			monochrome = false;
			helpOpen = false;
			record("Restored the default settings");
		}

		@ GuiMethod void openHelp(VS::Gui::Button source)
		{
			helpOpen = !helpOpen;
			record(helpOpen ? "Help opened" : "Help closed");
		}

		@ GuiMethod void changeHelp(VS::Gui::Popup source, bool value)
		{
			helpOpen = value;
			record(value ? "Help opened" : "Help dismissed");
		}

		@ GuiMethod void clearLog(VS::Gui::Button source)
		{
			history = array<string>();
			record("Activity log cleared");
		}

	private:
		void record(string message)
		{
			eventCount++;
			status = message;
			string line = to_string(eventCount) + ". " + message;
			if (len(history) < 8)
			{
				history.push(line);
			}
			else
			{
				for (int64 index = 1; index < len(history); index++)
				{
					history[index - 1] = history[index];
				}
				history[len(history) - 1] = line;
			}
		}

		string projectName = "Northstar workspace";
		string password = "";
		string notes = "Try text selection, clipboard, undo and multiline editing.";
		double gain = 65;
		bool controlsEnabled = true;
		bool details = true;
		bool monochrome = false;
		bool helpOpen = false;
		string mode = "balanced";
		int64 eventCount = 0;
		string status = "";
		array<string> history = array<string>();
	};
}

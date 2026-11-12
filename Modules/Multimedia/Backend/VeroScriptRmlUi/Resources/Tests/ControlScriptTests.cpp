// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptGuiContext.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptGuiScript.h"
#include "VeroScriptGuiTestResources.h"
#include "VeroScriptGuiTools.h"
#include "VeroScriptPath.h"
#include "VeroScriptRmlUi.h"
#include "VeroScriptSession.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#ifdef VS_GUI_CONTROL_GPU
#include "VeroScriptSdl3.h"
#endif

namespace
{
	using namespace VeroScript;
	using namespace Gui;
	void Require(bool Value, const std::string &Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	class FHost final : public IEngineHost
	{
	public:
		void OnPrintLine(std::string_view) override
		{
		}
		void OnDiagnosticLine(std::string_view Text) override
		{
			std::cerr << Text << '\n';
		}
	};
} // namespace

int main(int Count, char **Arguments)
{
	try
	{
		const auto Started = std::chrono::steady_clock::now();
		const auto Stage = [&](const char *Name)
		{
			std::cout << Name << " ("
			          << std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count() << " s)"
			          << std::endl;
		};
		std::string Scenario = "All";
		bool Render = false;
		for (int Index = 1; Index < Count; ++Index)
		{
			const std::string Argument = Arguments[Index];
			if (Argument == "--render")
			{
				Render = true;
			}
			else if (Argument == "--scenario")
			{
				Require(++Index < Count, "--scenario requires All, SourceArtifact, Markup or CodeExample");
				Scenario = Arguments[Index];
			}
			else
			{
				throw std::runtime_error("unknown control test argument: " + Argument);
			}
		}
		Require(Scenario == "All" || Scenario == "SourceArtifact" || Scenario == "Markup" || Scenario == "CodeExample",
		    "unknown control test scenario: " + Scenario);
		const auto Selected = [&](const char *Name)
		{
			return Scenario == "All" || Scenario == Name;
		};
		std::cout << "GUI control scenario: " << Scenario << std::endl;
		FHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		std::shared_ptr<Graphics::IDevice> Device;
		std::shared_ptr<Graphics::ITexture> Target;
		std::vector<std::shared_ptr<Graphics::ITexture>> Targets;
		#ifdef VS_GUI_CONTROL_GPU
		std::string GraphicsError;
		auto GraphicsService = Render ? Sdl3::CreateGraphics({}, GraphicsError) : nullptr;
		if (Render)
		{
			if (!GraphicsService || !(Device = GraphicsService->CreateDevice({}, GraphicsError)))
			{
				std::cerr << GraphicsError << '\n';
				return 77;
			}
		}
		#else
		(void)Render;
		#endif
		std::shared_ptr<FContext> Native;
		FScriptServices Services;
		const auto CreateContext = [&](const auto &, const auto &Viewport, auto &Error)
		{
			RmlUi::FOptions Options = GuiTestResourceOptions();
			Options.Device = Device;
			if (Device)
			{
				Target = Device->CreateTexture({Viewport.PixelWidth, Viewport.PixelHeight}, Error);
				if (!Target)
				{
					return std::shared_ptr<FContext>{};
				}
				Targets.push_back(Target);
			}
			Options.Texture = Target;
			Native = FContext::Create(RmlUi::CreateBackend(Options, Error), Viewport, Error);
			return Native;
		};
		Services.CreateContext = CreateContext;
		Services.IsCurrentProgram = [&](const auto &Program, auto Generation)
		{
			const auto P = Session.GetDebugProgram();
			return P.Generation == Generation && P.Owner.lock() == Program.lock();
		};
		Services.Invoke = [&](auto Function, auto &Arguments, auto &Out, auto &Error)
		{
			const auto R = Session.CallFunction(Function, Arguments, &Out);
			Error = R.Error.Message;
			return R.bSuccess;
		};
		std::string Error;
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		Require(RegisterScriptBindings(Session.GetBindingRegistry(), std::move(Services), Error) &&
		        ConfigureGuiScriptEngine(Config, Error),
		    Error);
		Session.ApplyConfig(Config);
		const std::string ControlSource = R"vs(
using namespace VS::Gui;
int Clicks = 0;
double Model = 0.3;
double Requested = -1;
VS::Gui::Cursor cursorModel = VS::Gui::Cursor::Text;
Button button = nullptr;
CheckBox check = nullptr;
Slider slider = nullptr;
TextBox edit = nullptr;
Context gui = nullptr;
void main() {}
void UpdateFrames()
{
    for (int frame = 0; frame < 60; frame++)
    {
        gui.Update(0);
    }
}
void Setup()
{
    std::attribute<VS::Gui::Cursor> cursorSource = std::attribute<VS::Gui::Cursor>();
    cursorSource.bind([]()->VS::Gui::Cursor { return cursorModel; });
    VS::Gui::Widget cursorProbe = VS::Gui::Widget::Create().Cursor(cursorSource);
    assert(cursorProbe.GetCursor() == VS::Gui::Cursor::Text, "cursor builder binding");
    cursorModel = VS::Gui::Cursor::Wait;
    cursorProbe.Synchronize();
    assert(cursorProbe.GetCursor() == VS::Gui::Cursor::Wait, "cursor Attribute refresh");
    cursorProbe.ClearCursor();
    assert(cursorProbe.GetCursor() == VS::Gui::Cursor::Auto, "cursor fallback");
    // Attach after a static synchronization, then replace, clear and reattach a high-index property.
    VS::Gui::Widget attributeProbe = VS::Gui::Widget::Create().Width(Length(10));
    attributeProbe.Synchronize();
    std::attribute<string> transform = std::attribute<string>();
    transform.bind([]()->string { return cursorModel == VS::Gui::Cursor::Wait ? "scale(2)" : "scale(1)"; });
    attributeProbe.SetTransform(transform);
    attributeProbe.Synchronize();
    assert(attributeProbe.GetTransform() == "scale(2)", "late native attribute attachment");
    attributeProbe.SetTransform("scale(3)");
    cursorModel = VS::Gui::Cursor::Text;
    attributeProbe.Synchronize();
    assert(attributeProbe.GetTransform() == "scale(3)", "constant replacement stops the old binding");
    attributeProbe.ClearTransform();
    attributeProbe.Synchronize();
    assert(attributeProbe.GetTransform() == "", "cleared native attribute uses fallback");
    attributeProbe.SetTransform(transform);
    attributeProbe.Synchronize();
    assert(attributeProbe.GetTransform() == "scale(1)", "native attribute reattachment");
    attributeProbe.Dispose();
    cursorModel = VS::Gui::Cursor::Text;
    button = Button::Create().Id("button").Text("Press").Cursor(VS::Gui::Cursor::Pointer)
        .OnClicked([](Button sender)->void { Clicks++; })[{
            VS::Gui::Text::Create().Text("Child content")
        }];
    check = CheckBox::Create().Id("check").Text("Check").Checked(true);
    std::attribute<double> value = std::attribute<double>();
    value.bind([]()->double { return Model; });
    slider = Slider::Create().Id("slider").Value(value)
        .OnValueChanged([](Slider sender, double candidate)->void { Requested = candidate; });
    edit = TextBox::Create().Id("edit").Value("Hello\r\n\t").Placeholder("Type");
    ScrollView scroll = ScrollView::Create().Height(Length(60)) [{
        Border::Create().Height(Length(180))
    }];
    Stack root = Stack::Create()[{
        button, check, slider, edit, scroll,
        ProgressBar::Create().Value(0.5),
        ToggleSwitch::Create().Checked(true),
        ScrollBar::Create(), TextArea::Create().Value("a\nb"),
        Overlay::Create(), Spacer::Create(), Separator::Create(), VS::Gui::Image::Create()
    }];
    gui = Context("test",400,600);
    gui.SetRoot(root);
    gui.Update(0);
    assert(check.GetChecked(), "checked construction");
    assert(button.GetCursor() == VS::Gui::Cursor::Pointer, "typed cursor builder");
    assert(edit.GetValue() == "Hello" && edit.GetActualValue() == "Hello", "single-line normalization");
    assert(slider.IsValueBound(), "builder lost Attribute binding");
    button.Click();
    assert(Clicks == 1, "typed Clicked callback");
    check.Toggle();
    assert(!check.GetChecked(), "unbound checkbox did not accept user value");
}
void VerifyInput()
{
    assert(Clicks == 2, "pointer did not reach script Clicked event");
    assert(Requested > 0.7 && Requested < 0.9, "range event candidate");
    assert(slider.IsValueBound(), "user edit removed Attribute binding");
    gui.Update(1);
    assert(slider.GetValue() == Model, "bound property was overwritten");
    assert(edit.GetValue() == "Hello!", "native text edit did not update script value");
    gui.Close();
}
NumberBox number = nullptr;
RadioGroup choices = nullptr;
Popup popup = nullptr;
string selected = "b";
string requestedKey = "";
double acceptedNumber = -1;
void SetupAdditional()
{
    std::attribute<string> key = std::attribute<string>();
    key.bind([]()->string { return selected; });
    RadioButton first = RadioButton::Create().Key("a").Text("First");
    RadioButton second = RadioButton::Create().Key("b").Text("Second");
    choices = RadioGroup::Create().SelectedKey(key)
        .OnSelectionChanged([](RadioGroup sender, string candidate)->void { requestedKey = candidate; })[{first, second}];
    number = NumberBox::Create().Value(12.5).Maximum(10).Step(0.5)
        .OnValueChanged([](NumberBox sender, double candidate)->void { acceptedNumber = candidate; });
    popup = Popup::Create().Open(true).Width(Length(120)).Height(Length(40))[{Button::Create().Text("Floating")}];
    Stack root = Stack::Create()[{
        Grid::Create().Columns(2).Gap(4).Height(Length(50))[{Border::Create(), Border::Create(), Border::Create()}],
        Wrap::Create().Gap(4).Height(Length(50))[{Border::Create().Width(Length(250)), Border::Create().Width(Length(250))}],
        Splitter::Create().Height(Length(80)).Value(0.4).DividerSize(8)[{Border::Create(), Border::Create()}],
        choices, number, PasswordBox::Create().Value("secret"),
        Button::Create().Text("Anchor")[{popup, ToolTip::Create().Text("Help").Delay(0.1)}]
    }];
    gui = Context("test",400,600);
    gui.SetRoot(root);
    gui.Update(0);
    assert(number.GetValue() == 10, "number normalization");
    assert(second.IsChecked() && !first.IsChecked(), "radio construction");
    first.Select();
    assert(requestedKey == "a" && choices.IsSelectedKeyBound(), "radio controlled candidate");
    gui.Update(1);
    assert(second.IsChecked() && choices.GetSelectedKey() == "b", "radio binding restored");
}
void VerifyAdditional()
{
    gui.Update(2);
    assert(acceptedNumber == 3.5 && number.GetValue() == 3.5, "typed numeric event");
    assert(!popup.GetOpen() && !popup.IsActuallyOpen(), "popup dismissal synchronized");
    gui.Close();
}
)vs";
		const auto Exercise = [&](auto &ActiveSession)
		{
			const auto Setup = ActiveSession.CallFunction("Setup", {});
			Require(Setup.bSuccess, Setup.Error.Message);
			const auto Started = std::chrono::steady_clock::now();
			const auto Frames = ActiveSession.CallFunction("UpdateFrames", {});
			Require(Frames.bSuccess, Frames.Error.Message);
			const auto Duration =
			    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
			std::cout << "Script-driven control frame average (ms): " << Duration / 60 << std::endl;
			const auto Root = Native->GetRoot();
			const auto Button = Root->GetChild(0, Error), Slider = Root->GetChild(2, Error),
			           Text = Root->GetChild(3, Error);
			const auto Send = [&](const auto &W, Platform::EEventType Type, double Ratio)
			{
				FLayoutBox B;
				Require(W->GetLayout(B, Error), Error);
				Platform::FEvent I;
				I.WindowId = 1;
				I.Type = Type;
				I.X = B.Content.X + B.Content.Width * Ratio;
				I.Y = B.Content.Y + 2;
				I.Button = 1;
				bool Handled;
				Require(Native->ProcessEvent(I, Handled, Error), Error);
			};
			Send(Button, Platform::EEventType::PointerDown, .5);
			Send(Button, Platform::EEventType::PointerUp, .5);
			Send(Slider, Platform::EEventType::PointerDown, .8);
			Send(Slider, Platform::EEventType::PointerUp, .8);
			Require(Native->SetFocus(Text, Error) && Text->SetTextSelection(5, 5, Error), Error);
			Platform::FEvent Input;
			Input.WindowId = 1;
			Input.Type = Platform::EEventType::TextInput;
			Input.Text = "!";
			bool Handled;
			Require(Native->ProcessEvent(Input, Handled, Error), Error);
			const auto Verify = ActiveSession.CallFunction("VerifyInput", {});
			Require(Verify.bSuccess, Verify.Error.Message);
			const auto Additional = ActiveSession.CallFunction("SetupAdditional", {});
			Require(Additional.bSuccess, Additional.Error.Message);
			const auto Number = Native->GetRoot()->GetChild(4, Error);
			Require(Number->FocusControl(Error) && Number->SetTextSelection(0, 2, Error), Error);
			Input.Type = Platform::EEventType::TextInput;
			Input.Text = "3.7";
			Require(Native->ProcessEvent(Input, Handled, Error), Error);
			Input.Type = Platform::EEventType::KeyDown;
			Input.Key = Platform::EKey::Enter;
			Require(Native->ProcessEvent(Input, Handled, Error), Error);
			Input.Key = Platform::EKey::Escape;
			Require(Native->ProcessEvent(Input, Handled, Error), Error);
			const auto AdditionalResult = ActiveSession.CallFunction("VerifyAdditional", {});
			Require(AdditionalResult.bSuccess, AdditionalResult.Error.Message);
		};
		FCompileOptions Offline;
		VeroScript::Test::Configure(Offline);
		Offline.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		if (Selected("SourceArtifact"))
		{
			const auto Loaded = Session.LoadSource(ControlSource, "control-script-test.vs");
			Require(Loaded.bSuccess, Loaded.Error.Message);
			Stage("Control source compiled");
			Exercise(Session);
			const auto CompiledControls = CompileGuiSource(ControlSource, "control-script-test.vs", Offline);
			Require(CompiledControls.bSuccess, CompiledControls.Error.Message);
			Stage("Offline control source compiled");
			FPackageBuildOptions ControlPackage;
			ControlPackage.PackageName = "gui-controls-regression";
			ControlPackage.bStripDebugInfo = true;
			std::vector<std::uint8_t> ControlBytes;
			Require(BakeGuiApplication(CompiledControls, ControlPackage, ControlBytes, Error), Error);
			FPackageData Deployed;
			Require(ReadPackage({ControlBytes.data(), ControlBytes.size()}, Deployed, Error), Error);
			Stage("Control package serialized and read");
			VeroScriptSession Runtime(Host);
			VeroScript::Test::Configure(Runtime);
			Runtime.SetHotReloadEnabled(true);
			FScriptServices DeployedServices;
			DeployedServices.CreateContext = CreateContext;
			DeployedServices.IsCurrentProgram = [&](const auto &Program, auto Generation)
			{
				const auto P = Runtime.GetDebugProgram();
				return P.Generation == Generation && P.Owner.lock() == Program.lock();
			};
			DeployedServices.Invoke = [&](auto Function, auto &Args, auto &Out, auto &Failure)
			{
				const auto Result = Runtime.CallFunction(Function, Args, &Out);
				Failure = Result.Error.Message;
				return Result.bSuccess;
			};
			Require(RegisterGuiArtifactBindings(Runtime.GetBindingRegistry(), Deployed, std::move(DeployedServices), Error),
			    Error);
			const auto PackageLoaded = Runtime.LoadPackage(Deployed, "relocated/controls.vsapp");
			Require(PackageLoaded.bSuccess, PackageLoaded.Error.Message);
			Exercise(Runtime);
		}
		const std::string Examples = VEROSCRIPT_GUI_CONTROL_EXAMPLES;
		if (Selected("Markup"))
		{
			const std::string Page = Examples + "/page.vswml";
			std::string MarkupSource = R"vs(
import "__PAGE__";
import "__ADDITIONAL_PAGE__";
import "__SHOWCASE_PAGE__";
VS::Gui::Context markupGui = nullptr;
void CloseMarkup() { markupGui.Close(); }
void CheckMarkup()
{
    auto page = VS::Gui::LoadFactory<VS::Gui::Stack>("__PAGE__").Create();
    markupGui = VS::Gui::Context("test",800,700);
    markupGui.SetRoot(page);
    markupGui.Update(0);
    VS::Gui::Button button = cast<VS::Gui::Button>(page.Find("button"));
    button.Click();
    markupGui.Update(1);
    assert(page.Find("heading").GetText() == "Button clicked", "ML event/binding");
}
void CheckCursorMarkup()
{
    auto page = VS::Gui::LoadFactory<VS::Gui::Stack>("__SHOWCASE_PAGE__").Create();
    markupGui = VS::Gui::Context("test",1200,840);
    markupGui.SetRoot(page);
    markupGui.Update(0);
    assert(page.GetCursor() == VS::Gui::Cursor::Auto, "ML cursor literal");
    assert(page.Find("chart").GetCursor() == VS::Gui::Cursor::Crosshair, "typed CSS cursor");
}

void CheckAdditionalMarkup()
{
    auto page = VS::Gui::LoadFactory<VS::Gui::Stack>("__ADDITIONAL_PAGE__").Create();
    markupGui = VS::Gui::Context("test",800,700);
    markupGui.SetRoot(page);
    markupGui.Update(0);
    auto grid = cast<VS::Gui::Grid>(page.Find("grid"));
    auto split = cast<VS::Gui::Splitter>(page.Find("splitter"));
    auto number = cast<VS::Gui::NumberBox>(page.Find("number"));
    auto choices = cast<VS::Gui::RadioGroup>(page.Find("choices"));
    assert(grid.GetColumns() == 2 && split.GetDividerSize() == 8, "custom numeric CSS properties");
    assert(number.GetValue() == 12.5 && choices.GetSelectedKey() == "first", "ML control values");
    cast<VS::Gui::Button>(page.Find("anchor")).Click();
    markupGui.Update(1);
    assert(cast<VS::Gui::Popup>(page.Find("popup")).IsActuallyOpen(), "ML event opens bound popup");
}
)vs";
			for (std::size_t At = MarkupSource.find("__PAGE__"); At != std::string::npos;
			    At = MarkupSource.find("__PAGE__", At + Page.size()))
			{
				MarkupSource.replace(At, 8, Page);
			}
			const std::string AdditionalPage = Examples + "/../AdditionalControls/page.vswml";
			for (std::size_t At = MarkupSource.find("__ADDITIONAL_PAGE__"); At != std::string::npos;
			    At = MarkupSource.find("__ADDITIONAL_PAGE__", At + AdditionalPage.size()))
			{
				MarkupSource.replace(At, 19, AdditionalPage);
			}
			const std::string ShowcasePage = Examples + "/../Showcase/page.vswml";
			for (std::size_t At = MarkupSource.find("__SHOWCASE_PAGE__"); At != std::string::npos;
			    At = MarkupSource.find("__SHOWCASE_PAGE__", At + ShowcasePage.size()))
			{
				MarkupSource.replace(At, 17, ShowcasePage);
			}
			const auto Markup = Session.LoadSource(MarkupSource, Examples + "/test.vs");
			Require(Markup.bSuccess, Markup.Error.Message);
			Stage("Control markup compiled");
			const auto CheckMarkup = Session.CallFunction("CheckMarkup", {});
			Require(CheckMarkup.bSuccess, CheckMarkup.Error.Message);
			#ifdef VS_GUI_CONTROL_GPU
			if (Render)
			{
				bool Presented = false;
				Require(Native->Render(Presented, Error), Error);
				std::vector<std::uint8_t> Pixels;
				Require(Device->ReadTexture(Target, Pixels, Error), Error);
				std::size_t Ink = 0;
				for (std::size_t At = 0; At + 3 < Pixels.size(); At += 4)
				{
					if (Pixels[At] > 80 || Pixels[At + 1] > 80 || Pixels[At + 2] > 80)
					{
						++Ink;
					}
				}
				Require(Ink > 1000, "control scene did not produce GPU geometry");
				const auto PageRoot = Native->GetRoot();
				const auto Find = [&](const std::string &Id)
				{
					std::vector<std::shared_ptr<FWidget>> Pending{PageRoot};
					while (!Pending.empty())
					{
						auto W = Pending.back();
						Pending.pop_back();
						if (W->GetId() == Id)
						{
							return W;
						}
						for (std::int64_t N = 0; N < W->GetChildCount(); ++N)
						{
							Pending.push_back(W->GetChild(N, Error));
						}
					}
					return std::shared_ptr<FWidget>{};
				};
				const auto BlueIndicator = [&](const char *Id, int X, int Y)
				{
					const auto W = Find(Id);
					FLayoutBox B;
					Require(W && W->GetLayout(B, Error), Error);
					const auto Offset = static_cast<std::size_t>((static_cast<int>(B.Border.Y) + Y) * 800 +
					                        static_cast<int>(B.Border.X) + X) *
					    4;
					Require(Offset + 3 < Pixels.size() && Pixels[Offset + 2] > 100 &&
					        Pixels[Offset + 2] > Pixels[Offset] * 1.3,
					    std::string(Id) + " indicator or checked styling was not rendered");
				};
				BlueIndicator("check", 8, 8);
				BlueIndicator("toggle", 26, 10);
			}
			#endif
			const auto CloseMarkup = Session.CallFunction("CloseMarkup", {});
			Require(CloseMarkup.bSuccess, CloseMarkup.Error.Message);
			const auto AdditionalMarkup = Session.CallFunction("CheckAdditionalMarkup", {});
			Require(AdditionalMarkup.bSuccess, AdditionalMarkup.Error.Message);
			#ifdef VS_GUI_CONTROL_GPU
			if (Render)
			{
				bool Presented = false;
				Require(Native->Render(Presented, Error), Error);
				std::vector<std::uint8_t> Pixels;
				Require(Device->ReadTexture(Target, Pixels, Error), Error);
				Require(!Pixels.empty(), "extended controls were not rendered");
				std::vector<std::shared_ptr<FWidget>> Pending{Native->GetRoot()};
				std::shared_ptr<FWidget> Popup;
				while (!Pending.empty())
				{
					const auto W = Pending.back();
					Pending.pop_back();
					if (W->GetId() == "popup")
					{
						Popup = W;
						break;
					}
					for (std::int64_t N = 0; N < W->GetChildCount(); ++N)
					{
						Pending.push_back(W->GetChild(N, Error));
					}
				}
				FLayoutBox Box;
				Require(Popup && Popup->GetLayout(Box, Error), "popup geometry");
				const auto X = static_cast<std::size_t>(Box.Border.X + Box.Border.Width - 4);
				const auto Y = static_cast<std::size_t>(Box.Border.Y + Box.Border.Height - 4);
				const auto Offset = (Y * 800 + X) * 4;
				// This unorm target stores linear RGB; CSS #34516e decodes to approximately (9, 21, 40).
				Require(Offset + 3 < Pixels.size() && std::abs(static_cast<int>(Pixels[Offset]) - 9) <= 2 &&
				        std::abs(static_cast<int>(Pixels[Offset + 1]) - 21) <= 2 &&
				        std::abs(static_cast<int>(Pixels[Offset + 2]) - 40) <= 2,
				    "portalled popup background was clipped or not rendered");
			}
			#endif
			const auto CloseAdditionalMarkup = Session.CallFunction("CloseMarkup", {});
			Require(CloseAdditionalMarkup.bSuccess, CloseAdditionalMarkup.Error.Message);
			const auto CursorMarkup = Session.CallFunction("CheckCursorMarkup", {});
			Require(CursorMarkup.bSuccess, CursorMarkup.Error.Message);
			const auto CloseCursorMarkup = Session.CallFunction("CloseMarkup", {});
			Require(CloseCursorMarkup.bSuccess, CloseCursorMarkup.Error.Message);
			Stage("Control markup exercised");
		}
		if (Selected("CodeExample"))
		{
			const auto Code = CompileGuiFile(Examples + "/code.vs", Offline);
			Require(Code.bSuccess, "code example: " + Code.Error.Message);
			Stage("Code example compiled");
			FPackageBuildOptions Package;
			Package.PackageName = "gui-controls";
			Package.bStripDebugInfo = true;
			std::vector<std::uint8_t> Bytes;
			Require(BakeGuiApplication(Code, Package, Bytes, Error) && !Bytes.empty(), Error);
		}
		for (const auto &Texture : Targets)
		{
			Require(Texture->Close(Error), Error);
		}
		if (Device)
		{
			Require(Device->Close(Error), Error);
		}
		std::cout << "GUI control builders, typed events, controlled attributes and native editing passed\n";
		return 0;
	}
	catch (const std::exception &E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
}

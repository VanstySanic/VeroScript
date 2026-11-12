// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiScript.h"
#include "VeroScriptGuiTestResources.h"
#include "VeroScriptSession.h"
#include "VeroScriptJitBackendLoader.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#ifdef VS_GUI_CONTROL_GPU
#include "VeroScriptSdl3.h"
#endif

namespace
{
	using namespace VeroScript;
	using namespace Gui;
	void Require(bool Value, const std::string& Why)
	{
		if (!Value)
		{
			throw std::runtime_error(Why);
		}
	}
	class FHost final : public IEngineHost
	{
	public:
		void OnPrintLine(std::string_view Text) override
		{
			std::cout << Text << '\n';
		}
		void OnDiagnosticLine(std::string_view Text) override
		{
			std::cerr << Text << '\n';
		}
	};
}

int main(int Count, char** Arguments)
{
	try
	{
		FHost Host;
		std::vector<std::weak_ptr<FScriptObject>> VirtualSnapshots;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		// Independent host entries share globals through the persistent session state.
		Session.SetHotReloadEnabled(true);
		std::string Error;
		const bool Render = Count == 2 && std::string(Arguments[1]) == "--render";
		std::shared_ptr<Graphics::IDevice> Device;
		std::shared_ptr<Graphics::ITexture> Target;
		#ifdef VS_GUI_CONTROL_GPU
		auto GraphicsService = Render ? Sdl3::CreateGraphics({}, Error) : nullptr;
		if (Render && (!GraphicsService || !(Device = GraphicsService->CreateDevice({}, Error))))
		{
			std::cerr << Error << '\n';
			return 77;
		}
		#else
		Require(!Render, "GPU support is not built");
		#endif
		std::shared_ptr<FContext> Context;
		FScriptServices Services;
		Services.CreateContext = [&](const auto&, const auto& Viewport, auto& Error)
		{
			auto Options = GuiTestResourceOptions();
			if (Device)
			{
				Target = Device->CreateTexture({Viewport.PixelWidth, Viewport.PixelHeight}, Error);
				if (!Target) return std::shared_ptr<FContext>{};
				Options.Device = Device;
				Options.Texture = Target;
			}
			Context = FContext::Create(RmlUi::CreateBackend(Options, Error), Viewport, Error);
			return Context;
		};
		Services.IsCurrentProgram = [&](const auto& Program, auto Generation)
		{
			const auto P = Session.GetDebugProgram();
			return P.Generation == Generation && P.Owner.lock() == Program.lock();
		};
		Services.Invoke = [&](auto Function, auto& Arguments, auto& Out, auto& Error)
		{
			const auto R = Session.CallFunction(Function, Arguments, &Out);
			Error = R.Error.Message;
			return R.bSuccess;
		};
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		Require(Count <= 2, "usage: VeroScriptRmlUiCompositeScriptTests [jit-backend]");
		if (Count == 2 && !Render)
		{
			Config.ExecutionBackend = EExecutionBackend::Jit;
			Config.JitBackendFactory = LoadJitBackend(Arguments[1], Error);
			Require(Config.JitBackendFactory != nullptr, Error);
		}
		Require(RegisterScriptBindings(Session.GetBindingRegistry(), std::move(Services), Error) &&
		        ConfigureGuiScriptEngine(Config, Error),
		    Error);
		// Observe the actual source-owned snapshot without retaining the view,
		// factory or model. Working-set counters alone cannot prove its release.
		Require(Session.GetBindingRegistry().RegisterNative(
		    "CaptureVirtualSnapshot", FNativeTypeDesc::Scalar(ENativeType::Int64),
		    {{FNativeTypeDesc::Function("fn()->void")}},
		    [](const FNativeCallContext& Call, FValue& Out, std::string& Error)
		{
			auto& VirtualSnapshots = *static_cast<std::vector<std::weak_ptr<FScriptObject>>*>(Call.UserData);
			std::vector<FValue> Pending{Call.Args[0]};
			std::unordered_set<const void*> Seen;
			while (!Pending.empty())
			{
				auto Value = std::move(Pending.back());
				Pending.pop_back();
				const auto* Unwrapped = UnwrapClosureReferenceCell(&Value);
				if (!Unwrapped)
				{
					continue;
				}
				if (const auto* Object = std::get_if<FObjectHandle>(Unwrapped); Object && *Object)
				{
					if (!Seen.insert(Object->get()).second)
					{
						continue;
					}
					if ((*Object)->Kind == EScriptObjectKind::Class &&
					    ((*Object)->GetTypeName().find("VirtualListData") != std::string::npos ||
					    (*Object)->GetTypeName().find("VirtualTableData") != std::string::npos))
					{
						Out = static_cast<std::int64_t>(VirtualSnapshots.size());
						VirtualSnapshots.push_back(*Object);
						return true;
					}
					for (const auto &Field : (*Object)->Fields)
					{
						Pending.push_back(Field);
					}
				}
				else if (const auto* Function = std::get_if<FFunctionHandle>(Unwrapped); Function && *Function)
				{
					if (!Seen.insert(Function->get()).second)
					{
						continue;
					}
					Pending.push_back((*Function)->CapturedThis);
					for (const auto &Capture : (*Function)->Captures)
					{
						Pending.push_back(Capture);
					}
				}
				else if (const auto* Array = std::get_if<FDynamicArrayHandle>(Unwrapped); Array && *Array)
				{
					if (!Seen.insert(Array->get()).second)
					{
						continue;
					}
					for (const auto &Element : (*Array)->Elements)
					{
						Pending.push_back(Element);
					}
				}
			}
			Error = "missing virtual source snapshot";
			return false;
		}, &VirtualSnapshots, Error), Error);
		Require(Session.GetBindingRegistry().RegisterNative(
		    "VirtualSnapshotExpired", FNativeTypeDesc::Scalar(ENativeType::Bool),
		    {{FNativeTypeDesc::Scalar(ENativeType::Int64)}},
		    [](const FNativeCallContext& Call, FValue& Out, std::string& Error)
		{
			auto& VirtualSnapshots = *static_cast<std::vector<std::weak_ptr<FScriptObject>>*>(Call.UserData);
			const auto Index = std::get<std::int64_t>(Call.Args[0]);
			if (Index < 0 || static_cast<std::size_t>(Index) >= VirtualSnapshots.size())
			{
				Error = "invalid virtual snapshot observer";
				return false;
			}
			Out = VirtualSnapshots[static_cast<std::size_t>(Index)].expired();
			return true;
		}, &VirtualSnapshots, Error), Error);
		Session.ApplyConfig(Config);
		const std::string Path = VEROSCRIPT_GUI_COMPOSITE_SOURCE;
		std::ifstream Stream(Path, std::ios::binary);
		Require(bool(Stream), "missing composite script fixture");
		std::ostringstream Source;
		Source << Stream.rdbuf();
		const auto Loaded = Session.LoadSource(Source.str(), Path);
		Require(Loaded.bSuccess, Loaded.Error.Message);
		const auto Call = [&](const char* Name)
		{
			const auto Result = Session.CallFunction(Name, {});
			Require(Result.bSuccess, std::string(Name) + ": " + Result.Error.Message);
			std::cout << Name << " passed" << std::endl;
		};
		const auto Reject = [&](const char* Name, const char* Message)
		{
			const auto Result = Session.CallFunction(Name, {});
			const bool Matches = Result.Error.Message.find(Message) != std::string::npos ||
			    (Result.Error.MessageId == "diag.assertion_failed_with_message" &&
			        std::any_of(Result.Error.MessageArgs.begin(), Result.Error.MessageArgs.end(),
			            [&](const std::string& Argument)
			{
				return Argument.find(Message) != std::string::npos;
			}));
			Require(!Result.bSuccess && Matches,
			    std::string(Name) + ": " + Result.Error.Message);
			Require(!Session.RequiresReset(), "completed validation cleanup requires an unexpected session reset");
			std::cout << Name << " rejected as expected" << std::endl;
		};
		const auto Draw = [&]
		{
			std::vector<std::uint8_t> Pixels;
			if (Device)
			{
				bool Presented = false;
				const bool Rendered = Context->Render(Presented, Error);
				Require(Rendered, "composite render: " + Error);
				// Offscreen rendering submits GPU work without presenting a swapchain image.
				const bool Completed = Device->WaitIdle(Error);
				Require(Completed, "composite GPU completion: " + Error);
				const bool Read = Device->ReadTexture(Target, Pixels, Error);
				Require(Read, "composite readback: " + Error);
			}
			return Pixels;
		};
		Call("VerifyMenuReentry");
		Call("VerifyComboFocusHandoff");
		Call("VerifyVirtualComboLabels");
		Call("VerifyVirtualRows");
		Call("VerifyVirtualSnapshotRelease");
		Call("VerifyTableColumnWidthReads");
		Call("VerifyVirtualBuiltinRetry");
		Call("VerifyVirtualSourceClearReentry");
		for (int Scenario = 1; Scenario <= 8; ++Scenario)
		{
			Call("PrepareVirtualFocusReentry");
			if (Scenario == 1 || Scenario == 4)
			{
				Reject("InstallVirtualFocusReentry", "gui.widget_disposed");
			}
			else if (Scenario >= 7)
			{
				Reject("InstallVirtualFocusReentry", "uncaught script exception");
			}
			else
			{
				Call("InstallVirtualFocusReentry");
			}
			Call("VerifyVirtualFocusReentry");
			Call("CleanupVirtualFocusReentry");
		}
		Call("VerifyNoopClearDoesNotRefresh");
		Call("VerifyReentrantTableColumns");
		Call("VerifyCommittedCollectionKeys");
		Call("PrepareVirtualKeyboard");
		Platform::FEvent KeyboardInput;
		KeyboardInput.Type = Platform::EEventType::KeyDown;
		KeyboardInput.WindowId = 1;
		KeyboardInput.Key = Platform::EKey::End;
		FEventResult KeyboardResult;
		Require(Context->ProcessEvent(KeyboardInput, KeyboardResult, Error), Error);
		Call("VerifyVirtualKeyboardAccepted");
		KeyboardInput.Key = Platform::EKey::Home;
		Require(Context->ProcessEvent(KeyboardInput, KeyboardResult, Error), Error);
		Call("VerifyVirtualKeyboardRejected");
		KeyboardInput.Key = Platform::EKey::End;
		Require(Context->ProcessEvent(KeyboardInput, KeyboardResult, Error), Error);
		Call("VerifyVirtualKeyboardClamped");
		KeyboardInput.Key = Platform::EKey::Home;
		Require(Context->ProcessEvent(KeyboardInput, KeyboardResult, Error), Error);
		Call("VerifyVirtualKeyboardSourceRemoval");
		Call("PrepareVirtualDisabledKeyboard");
		KeyboardInput.Key = Platform::EKey::End;
		Require(Context->ProcessEvent(KeyboardInput, KeyboardResult, Error), Error);
		Call("VerifyVirtualDisabledEnd");
		KeyboardInput.Key = Platform::EKey::Home;
		Require(Context->ProcessEvent(KeyboardInput, KeyboardResult, Error), Error);
		Call("VerifyVirtualDisabledHome");
		KeyboardInput.Key = Platform::EKey::Down;
		Require(Context->ProcessEvent(KeyboardInput, KeyboardResult, Error), Error);
		Call("VerifyVirtualDisabledDown");
		Call("PrepareVirtualRetry");
		Reject("TryVirtualFactoryFailure", "uncaught script exception");
		Call("VerifyVirtualRetry");
		Reject("TryInvalidVirtualSource", "Virtual row keys must be nonempty and unique");
		Call("VerifyVirtualSourcePreserved");
		Call("Setup");
		Call("VerifyAlive");
		Reject("TryInvalidColumns", "column count");
		Call("VerifyInvalidColumns");
		Call("SetDuplicateTabKey");
		Reject("TryDuplicateTabs", "Tab keys must be unique");
		Call("VerifyDynamicTabs");
		auto Pixels = Draw();
		if (Device)
		{
			Require(Pixels.size() == 700 * 700 * 4 && Pixels[(690 * 700 + 690) * 4] > 250,
			    "opaque background before modal");
		}
		const auto Find = [&](const std::string& Id)
		{
			std::vector<std::shared_ptr<FWidget>> Pending{Context->GetRoot()};
			while (!Pending.empty())
			{
				auto Widget = Pending.back();
				Pending.pop_back();
				if (Widget->GetId() == Id) return Widget;
				for (std::int64_t Index = 0; Index < Widget->GetChildCount(); ++Index)
				{
					auto Child = Widget->GetChild(Index, Error);
					Require(Child != nullptr, Error);
					Pending.push_back(std::move(Child));
				}
			}
			throw std::runtime_error("missing physical widget: " + Id);
		};
		FLayoutBox Box;
		const auto Send = [&](Platform::EEventType Type, Platform::EKey Key = Platform::EKey::Unknown)
		{
			Platform::FEvent Input;
			Input.Type = Type;
			Input.WindowId = 1;
			Input.Key = Key;
			Input.Button = 1;
			Input.X = Box.Border.X + Box.Border.Width * .5;
			Input.Y = Box.Border.Y + Box.Border.Height * .5;
			FEventResult Result;
			Require(Context->ProcessEvent(Input, Result, Error), Error);
		};
		Call("OpenNestedMenus");
		Send(Platform::EEventType::KeyDown, Platform::EKey::Escape);
		Call("VerifyNestedEscape");
		Send(Platform::EEventType::KeyDown, Platform::EKey::Escape);
		Call("VerifyMenuClosed");
		Call("OpenNestedMenus");
		Box.Border = {690, 690, 1, 1};
		Send(Platform::EEventType::PointerDown);
		Send(Platform::EEventType::PointerUp);
		Call("VerifyMenuClosed");
		Call("OpenMenuForSwitch");
		Send(Platform::EEventType::KeyDown, Platform::EKey::Right);
		Call("VerifyMenuSwitch");
		Call("OpenMenuForSwitch");
		Require(Find("menu-view")->GetLayout(Box, Error), Error);
		Send(Platform::EEventType::PointerDown);
		Send(Platform::EEventType::PointerUp);
		Call("VerifyMenuSwitch");
		Require(Context->SetFocus(Find("branch"), Error), Error);
		Send(Platform::EEventType::KeyDown, Platform::EKey::Left);
		Call("VerifyTreeCollapsed");
		Send(Platform::EEventType::KeyDown, Platform::EKey::Right);
		Call("VerifyTreeExpanded");
		Require(Find("list-a")->GetLayout(Box, Error), Error);
		Send(Platform::EEventType::PointerDown);
		Call("VerifyAlive");
		Send(Platform::EEventType::PointerUp);
		Call("VerifyAlive");
		Send(Platform::EEventType::KeyDown, Platform::EKey::Down);
		Call("VerifyArrowDown");
		Send(Platform::EEventType::KeyDown, Platform::EKey::Home);
		Call("VerifyArrowHome");
		Call("VerifyInput");
		Pixels = Draw();
		if (Device)
		{
			Require(Pixels[(690 * 700 + 690) * 4] > 120 && Pixels[(690 * 700 + 690) * 4] < 170,
			    "modal backdrop did not cover ordinary content");
		}
		Send(Platform::EEventType::KeyDown, Platform::EKey::Escape);
		Call("VerifyDialog");
		Reject("TryInvalidStructure", "Collection item keys must be unique");
		Call("VerifyStructureRetry");
		Call("VerifyMarkup");
		Draw();
		Call("CloseMarkup");
		std::cout << "Composite widgets, candidates, ownership, native input and ML/CSS passed\n";
		return 0;
	}
	catch (const std::exception& Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUiControls.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/TextInputContext.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace VeroScript::RmlUi
{
	namespace
	{
		constexpr std::uint32_t CommandMask()
		{
			#if defined(__APPLE__)
			return 2 | 8;
			#else
			return 2;
			#endif
		}
		int Modifiers(std::uint32_t Value)
		{
			using namespace Rml::Input;
			return ((Value & 1) ? KM_SHIFT : 0) | ((Value & CommandMask()) ? KM_CTRL : 0) | ((Value & 4) ? KM_ALT : 0) |
			    ((Value & 16) ? KM_CAPSLOCK : 0) | ((Value & 32) ? KM_NUMLOCK : 0);
		}
		Rml::Input::KeyIdentifier Key(Platform::EKey Value)
		{
			using Platform::EKey;
			using namespace Rml::Input;
			if (Value >= EKey::A && Value <= EKey::Z)
			{
				return static_cast<KeyIdentifier>(KI_A + static_cast<int>(Value) - static_cast<int>(EKey::A));
			}
			switch (Value)
			{
			case EKey::Backspace:
				{
					return KI_BACK;
				}
			case EKey::Delete:
				{
					return KI_DELETE;
				}
			case EKey::Left:
				{
					return KI_LEFT;
				}
			case EKey::Right:
				{
					return KI_RIGHT;
				}
			case EKey::Up:
				{
					return KI_UP;
				}
			case EKey::Down:
				{
					return KI_DOWN;
				}
			case EKey::Home:
				{
					return KI_HOME;
				}
			case EKey::End:
				{
					return KI_END;
				}
			case EKey::PageUp:
				{
					return KI_PRIOR;
				}
			case EKey::PageDown:
				{
					return KI_NEXT;
				}
			case EKey::Enter:
				{
					return KI_RETURN;
				}
			case EKey::Tab:
				{
					return KI_TAB;
				}
			case EKey::Escape:
				{
					return KI_ESCAPE;
				}
			case EKey::Insert:
				{
					return KI_INSERT;
				}
			default:
				{
					return KI_UNKNOWN;
				}
			}
		}
		std::string SingleLine(std::string Value)
		{
			std::size_t Written = 0;
			for (std::size_t Read = 0; Read < Value.size();)
			{
				const auto Break = Rml::StringUtilities::GetLineBreakLength(Value.data() + Read, Value.data() + Value.size());
				if (Break || Value[Read] == '\t')
				{
					Read += Break ? Break : 1;
				}
				else
				{
					Value[Written++] = Value[Read++];
				}
			}
			Value.resize(Written);
			return Value;
		}
	}

	void FControls::OnActivate(Rml::TextInputContext *Context)
	{
		Active = Context;
	}

	void FControls::OnDeactivate(Rml::TextInputContext *Context)
	{
		if (Active == Context)
		{
			Active = nullptr;
		}
	}

	void FControls::OnDestroy(Rml::TextInputContext *Context)
	{
		OnDeactivate(Context);
	}

	bool FControls::Synchronize(Rml::ElementDocument &Document, Rml::Element &Parent, const Gui::FBackendNode &Node,
	    std::string &Error)
	{
		if (!Gui::IsTextControl(Node.Control.Kind))
		{
			return true;
		}
		auto &E = Editors[Node.Id];
		if (!E.Element)
		{
			auto Element =
			    Document.CreateElement(Node.Control.Kind == Gui::EControlKind::TextArea ? "textarea" : "input");
			E.Element = dynamic_cast<Rml::ElementFormControl *>(Element.get());
			if (!E.Element)
			{
				Error = "rmlui.editor_creation_failed";
				return false;
			}
			if (Node.Control.Kind != Gui::EControlKind::TextArea)
			{
				Element->SetAttribute("type",
				    Node.Control.Kind == Gui::EControlKind::PasswordBox ? "password" : "text");
			}
			Element->SetProperty("width", "100%");
			Element->SetProperty("height", "100%");
			// Leave room for authored child widgets in the same content flow.
			Element->SetProperty("flex-shrink", "1");
			Element->SetProperty("box-sizing", "border-box");
			Element->SetProperty("background-color", "transparent");
			Element->SetProperty("overflow-x", "hidden");
			Element->SetProperty("overflow-y", "hidden");
			Parent.InsertBefore(std::move(Element), Parent.GetFirstChild());
			E.Element->SetValue(Node.Control.Text);
			if (E.Saved.Text == Node.Control.Text)
			{
				Restore(E, E.Saved);
				E.bRestoreSelection = true;
			}
		}
		const bool bNewText = E.Options.Text != Node.Control.Text || E.Options.Kind != Node.Control.Kind;
		if (bNewText || (Node.Control.bReadOnly && !E.Options.bReadOnly))
		{
			CancelComposition(E);
			if (bNewText)
			{
				E.Undo.clear();
				E.Redo.clear();
			}
			if (E.Element->GetValue() != Node.Control.Text)
			{
				const auto Before = Read(E);

				Restore(E, {Node.Control.Text, Before.Start, Before.End});
			}
		}
		E.Options = Node.Control;
		if (E.bRestoreSelection)
		{
			const auto Length = static_cast<int>(Rml::StringUtilities::LengthUTF8(Node.Control.Text));
			E.Saved.Text = Node.Control.Text;
			E.Saved.Start = std::min(E.Saved.Start, Length);
			E.Saved.End = std::min(E.Saved.End, Length);
		}
		E.Element->SetAttribute("placeholder", Node.Control.Placeholder);
		E.Element->SetDisabled(!Node.Properties.bEnabled);
		if (Node.Id == FocusId && E.Element->GetContext()->GetFocusElement() != E.Element)
		{
			E.Element->Focus();
		}
		return true;
	}

	void FControls::BeforeRebuild()
	{
		for (auto &Item : Editors)
		{
			auto &E = Item.second;
			if (E.Element)
			{
				if (Item.first == FocusId)
				{
					E.Element->GetContext()->ProcessMouseButtonUp(0, 0);
				}
				CancelComposition(E);
				if (!E.bRestoreSelection)
				{
					E.Saved = Read(E);
				}
				E.bRestoreSelection = true;
			}
			E.Element = nullptr;
		}
		Active = nullptr;
	}

	void FControls::AfterRebuild(const std::vector<Gui::FBackendNode> &Nodes)
	{
		std::unordered_set<std::uint64_t> Live;
		for (const auto &Node : Nodes)
		{
			if (Gui::IsTextControl(Node.Control.Kind))
			{
				Live.insert(Node.Id);
			}
		}
		for (auto It = Editors.begin(); It != Editors.end();)
		{
			if (!Live.count(It->first))
			{
				It = Editors.erase(It);
			}
			else
			{
				++It;
			}
		}
	}

	bool FControls::Focus(std::uint64_t Id, const Gui::FTextInputServices &Value, std::string &Error)
	{
		if (Id && !Editors.count(Id))
		{
			Error = "rmlui.editor_not_ready";
			return false;
		}
		if (FocusId && FocusId != Id)
		{
			auto It = Editors.find(FocusId);
			if (It != Editors.end() && It->second.Element)
			{
				It->second.Element->GetContext()->ProcessMouseButtonUp(0, 0);
				CancelComposition(It->second);
				if (!It->second.bRestoreSelection)
				{
					It->second.Saved = Read(It->second);
				}
				It->second.bRestoreSelection = true;
				It->second.Element->Blur();
			}
			if (Services.SetInputArea && !Services.SetInputArea(false, {}, 0, Error))
			{
				return false;
			}
		}
		Services = Value;
		FocusId = Id;
		if (Id && Editors.at(Id).Element)
		{
			Editors.at(Id).Element->Focus();
		}
		return true;
	}

	bool FControls::Edit(std::uint64_t Id, const Platform::FEvent &I, Rml::Context &Context, double Scale,
	    Gui::FTextEditResult &Result, const std::string &ClipboardError, std::string &Error)
	{
		const auto It = Editors.find(Id);
		if (It == Editors.end() || !It->second.Element)
		{
			Error = "rmlui.editor_not_ready";
			return false;
		}
		if ((I.Type == Platform::EEventType::TextInput || I.Type == Platform::EEventType::TextEditing) &&
		    (!Gui::ValidateGuiText(I.Text) || I.Text.size() > 1024 * 1024))
		{
			Error = "gui.text_limit_or_encoding";
			return false;
		}
		auto &E = It->second;
		using Platform::EEventType;
		using Platform::EKey;
		const auto Before = Read(E);
		if (I.Type == EEventType::Wheel)
		{
			const auto X = E.Element->GetScrollLeft(), Y = E.Element->GetScrollTop();
			E.Element->SetScrollLeft(static_cast<float>(X - I.DeltaX * 40 * Scale));
			E.Element->SetScrollTop(static_cast<float>(Y - I.DeltaY * 40 * Scale));
			Result.bHandled = X != E.Element->GetScrollLeft() || Y != E.Element->GetScrollTop();
			return true;
		}
		if (Id != FocusId)
		{
			return true;
		}

		const bool bCommand = (I.Modifiers & CommandMask()) != 0;
		const bool bMutationKey =
		    I.Key == EKey::Backspace || I.Key == EKey::Delete ||
		    (I.Key == EKey::Enter && E.Options.Kind == Gui::EControlKind::TextArea) ||
		    (bCommand && (I.Key == EKey::X || I.Key == EKey::V || I.Key == EKey::Z || I.Key == EKey::Y));
		if (E.Options.bReadOnly && (I.Type == EEventType::TextInput || I.Type == EEventType::TextEditing || (I.Type == EEventType::KeyDown && bMutationKey)))
		{
			Result.Text = Before.Text;
			return true;
		}
		if (I.Type == EEventType::TextEditing)
		{
			if (I.Text.empty())
			{
				CancelComposition(E);
				return true;
			}
			if (!E.Composition)
			{
				E.Composition = Before;
			}
			const auto Base = *E.Composition;
			const auto Text = E.Options.Kind != Gui::EControlKind::TextArea ? SingleLine(I.Text) : I.Text;
			const auto Begin = Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(Base.Text, Base.Start);
			const auto End = Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(Base.Text, Base.End);
			std::string Preview = Base.Text;
			Preview.replace(Begin, End - Begin, Text);
			if (Preview.size() > 1024 * 1024)
			{
				Error = "gui.text_limit";
				return false;
			}
			const auto Count = static_cast<int>(Rml::StringUtilities::LengthUTF8(Text));
			const auto Selection =
			    static_cast<int>(std::clamp<std::int64_t>(I.SelectionStart < 0 ? Count : I.SelectionStart, 0, Count));
			Restore(E, {Preview, Base.Start + Selection, Base.Start + Selection + static_cast<int>(std::clamp<std::int64_t>(I.SelectionLength, 0, Count - Selection))});
			if (auto *Input = dynamic_cast<Rml::ElementFormControlInput *>(E.Element))
			{
				Input->SetCompositionRange(Base.Start, Base.Start + Count);
			}
			if (auto *Area = dynamic_cast<Rml::ElementFormControlTextArea *>(E.Element))
			{
				Area->SetCompositionRange(Base.Start, Base.Start + Count);
			}
			return UpdateInputArea(Scale, Error);
		}
		if (E.Composition && (I.Type == EEventType::KeyDown || I.Type == EEventType::KeyUp))
		{
			if (I.Type == EEventType::KeyDown && I.Key == EKey::Escape)
			{
				CancelComposition(E);
			}
			return true;
		}
		if (I.Type == EEventType::KeyDown && I.Key == EKey::Enter && E.Options.Kind != Gui::EControlKind::TextArea)
		{
			Result.Text = E.Element->GetValue();
			Result.bSubmitted = true;
			return true;
		}
		FSnapshot UndoBase = Before;
		if (E.Composition && I.Type == EEventType::TextInput)
		{
			UndoBase = *E.Composition;
			CancelComposition(E);
		}
		const bool bUndo = I.Type == EEventType::KeyDown && bCommand && I.Key == EKey::Z && !(I.Modifiers & 1);
		const bool bRedo =
		    I.Type == EEventType::KeyDown && bCommand && (I.Key == EKey::Y || (I.Key == EKey::Z && (I.Modifiers & 1)));
		if (bUndo || bRedo)
		{
			auto &Source = bUndo ? E.Undo : E.Redo;
			auto &Destination = bUndo ? E.Redo : E.Undo;
			if (!Source.empty())
			{
				Remember(Destination, Read(E));
				Restore(E, Source.back());
				Source.pop_back();
			}
		}
		else
		{
			switch (I.Type)
			{
			case EEventType::KeyDown:
				{
					Context.ProcessKeyDown(Key(I.Key), Modifiers(I.Modifiers));
					break;
				}
			case EEventType::KeyUp:
				{
					Context.ProcessKeyUp(Key(I.Key), Modifiers(I.Modifiers));
					break;
				}
			case EEventType::TextInput:
				{
					Context.ProcessTextInput(E.Options.Kind != Gui::EControlKind::TextArea ? SingleLine(I.Text)
					                                                                       : I.Text);
					break;
				}
			case EEventType::PointerDown:
				{
					Context.ProcessMouseMove(
					    static_cast<int>(std::lround(std::clamp(I.X * Scale, -16777216.0, 16777216.0))),
					    static_cast<int>(std::lround(std::clamp(I.Y * Scale, -16777216.0, 16777216.0))),
					    Modifiers(I.Modifiers));
					Context.ProcessMouseButtonDown(0, Modifiers(I.Modifiers));
					break;
				}
			case EEventType::PointerCancelled:
			case EEventType::PointerUp:
				{
					Context.ProcessMouseButtonUp(0, Modifiers(I.Modifiers));
					break;
				}
			case EEventType::PointerMove:
				{
					Context.ProcessMouseMove(
					    static_cast<int>(std::lround(std::clamp(I.X * Scale, -16777216.0, 16777216.0))),
					    static_cast<int>(std::lround(std::clamp(I.Y * Scale, -16777216.0, 16777216.0))),
					    Modifiers(I.Modifiers));
					break;
				}
			default:
				{
					break;
				}
			}
		}
		if (!ClipboardError.empty())
		{
			Restore(E, UndoBase);
			Error = ClipboardError;
			return false;
		}
		const auto After = Read(E);
		if (After.Text.size() > 1024 * 1024 || !Gui::ValidateGuiText(After.Text))
		{
			Restore(E, UndoBase);
			Error = "gui.text_limit_or_encoding";
			return false;
		}
		Result.Text = After.Text;
		Result.SelectionStart = After.Start;
		Result.SelectionEnd = After.End;
		Result.bChanged = After.Text != UndoBase.Text;
		if (Result.bChanged && !bUndo && !bRedo)
		{
			Remember(E.Undo, std::move(UndoBase));
			E.Redo.clear();
		}
		// A subsequent unchanged configuration must preserve selection and undo history.
		if (Result.bChanged)
		{
			E.Options.Text = After.Text;
		}
		return UpdateInputArea(Scale, Error);
	}

	bool FControls::IsComposing(std::uint64_t Id) const
	{
		const auto It = Editors.find(Id);
		return It != Editors.end() && It->second.Composition.has_value();
	}

	bool FControls::Select(std::uint64_t Id, std::int64_t Start, std::int64_t End, std::string &Error)
	{
		const auto It = Editors.find(Id);
		if (It == Editors.end() || !It->second.Element || Start < 0 || End < Start || End > INT32_MAX)
		{
			Error = "gui.invalid_text_selection";
			return false;
		}
		auto &E = It->second;
		const auto Committed = E.Composition ? E.Composition->Text : E.Element->GetValue();
		if (End > static_cast<std::int64_t>(Rml::StringUtilities::LengthUTF8(Committed)))
		{
			Error = "gui.invalid_text_selection";
			return false;
		}
		CancelComposition(E);
		E.Saved = {Committed, static_cast<int>(Start), static_cast<int>(End)};
		E.bRestoreSelection = true;
		if (E.Element->GetContext()->GetFocusElement() == E.Element)
		{
			Restore(E, E.Saved);
			E.bRestoreSelection = false;
		}
		return true;
	}

	bool FControls::UpdateInputArea(double Scale, std::string &Error)
	{
		for (auto &Item : Editors)
		{
			auto &E = Item.second;
			if (E.bRestoreSelection && E.Element && E.Element->GetContext()->GetFocusElement() == E.Element)
			{
				Restore(E, E.Saved);
				E.bRestoreSelection = false;
			}
		}
		if (!FocusId || !Active || !Services.SetInputArea)
		{
			return true;
		}
		const auto It = Editors.find(FocusId);
		if (It == Editors.end() || !It->second.Element)
		{
			return true;
		}
		Rml::Rectanglef Rect;
		if (Active->GetCaretBoundingBox(Rect))
		{
			return Services.SetInputArea(
			    !It->second.Options.bReadOnly,
			    {Rect.Left() / Scale, Rect.Top() / Scale, Rect.Width() / Scale, Rect.Height() / Scale}, 0, Error);
		}
		// Compatibility with third-party text-input contexts that do not expose laid-out caret geometry.
		if (!Active->GetBoundingBox(Rect))
		{
			return true;
		}
		const auto S = Read(It->second);
		const auto Byte = Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(S.Text, S.End);
		const auto Begin = Byte > 0 ? S.Text.rfind('\n', static_cast<std::size_t>(Byte - 1)) : std::string::npos;
		const auto Line = Begin == std::string::npos ? 0 : Begin + 1;
		const auto Prefix = S.Text.substr(Line, static_cast<std::size_t>(Byte) - Line);
		const auto X = std::clamp(Rml::ElementUtilities::GetStringWidth(It->second.Element, Prefix) / Scale, 0.0,
		    Rect.Width() / Scale);
		return Services.SetInputArea(
		    !It->second.Options.bReadOnly,
		    {Rect.Left() / Scale, Rect.Top() / Scale, Rect.Width() / Scale, Rect.Height() / Scale},
		    static_cast<std::int64_t>(std::lround(X)), Error);
	}

	FControls::FSnapshot FControls::Read(const FEditor &Editor) const
	{
		FSnapshot S;
		S.Text = Editor.Element->GetValue();
		if (auto *Input = dynamic_cast<Rml::ElementFormControlInput *>(Editor.Element))
		{
			Input->GetSelection(&S.Start, &S.End, nullptr);
		}
		if (auto *Area = dynamic_cast<Rml::ElementFormControlTextArea *>(Editor.Element))
		{
			Area->GetSelection(&S.Start, &S.End, nullptr);
		}
		Rml::TextSelectionState Selection;
		if (Active && Editor.Element->GetContext()->GetFocusElement() == Editor.Element && Active->GetSelectionState(Selection))
		{
			S.Selection = Selection;
		}
		return S;
	}

	void FControls::Restore(FEditor &Editor, const FSnapshot &S)
	{
		Editor.Element->SetValue(S.Text);
		if (auto *Input = dynamic_cast<Rml::ElementFormControlInput *>(Editor.Element))
		{
			Input->SetSelectionRange(S.Start, S.End);
			Input->SetCompositionRange(0, 0);
		}
		if (auto *Area = dynamic_cast<Rml::ElementFormControlTextArea *>(Editor.Element))
		{
			Area->SetSelectionRange(S.Start, S.End);
			Area->SetCompositionRange(0, 0);
		}
		if (S.Selection && Active && Editor.Element->GetContext()->GetFocusElement() == Editor.Element)
		{
			Active->SetSelectionState(*S.Selection);
		}
	}

	void FControls::CancelComposition(FEditor &Editor)
	{
		if (Editor.Composition && Editor.Element)
		{
			Restore(Editor, *Editor.Composition);
		}
		Editor.Composition.reset();
	}

	void FControls::Remember(std::deque<FSnapshot> &History, FSnapshot Snapshot)
	{
		History.push_back(std::move(Snapshot));
		std::size_t Bytes = 0;
		for (const auto &S : History)
		{
			Bytes += S.Text.size();
		}
		while (History.size() > 128 || Bytes > 8 * 1024 * 1024)
		{
			Bytes -= History.front().Text.size();
			History.pop_front();
		}
	}
} // namespace VeroScript::RmlUi

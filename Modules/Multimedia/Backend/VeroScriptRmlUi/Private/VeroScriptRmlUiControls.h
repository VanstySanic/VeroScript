// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiBackend.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Header.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/Traits.h>
#include <deque>
#include <map>

namespace VeroScript::RmlUi
{
	/** Owns adapter state only. Element lifetimes remain with the RmlUi document. */
	class FControls final : public Rml::TextInputHandler
	{
	public:
		//~ Begin Rml::TextInputHandler Interface
		void OnActivate(Rml::TextInputContext *Context) override;
		void OnDeactivate(Rml::TextInputContext *Context) override;
		void OnDestroy(Rml::TextInputContext *Context) override;
		//~ End Rml::TextInputHandler Interface

		bool Synchronize(Rml::ElementDocument &Document, Rml::Element &Parent, const Gui::FBackendNode &Node,
						 std::string &Error);
		void BeforeRebuild();
		void AfterRebuild(const std::vector<Gui::FBackendNode> &Nodes);
		bool Focus(std::uint64_t Id, const Gui::FTextInputServices &Services, std::string &Error);
		bool Edit(std::uint64_t Id, const Platform::FEvent &Input, Rml::Context &Context, double Scale,
				  Gui::FTextEditResult &Result, const std::string &ClipboardError, std::string &Error);
		bool IsComposing(std::uint64_t Id) const;
		bool Select(std::uint64_t Id, std::int64_t Start, std::int64_t End, std::string &Error);
		bool UpdateInputArea(double Scale, std::string &Error);

	private:
		struct FSnapshot
		{
			std::string Text;
			int Start = 0, End = 0;
			std::optional<Rml::TextSelectionState> Selection;
		};
		struct FEditor
		{
			Rml::ElementFormControl *Element = nullptr;
			Gui::FControlOptions Options;
			bool bRestoreSelection = false;
			FSnapshot Saved;
			std::optional<FSnapshot> Composition;
			std::deque<FSnapshot> Undo, Redo;
		};
		FSnapshot Read(const FEditor &Editor) const;
		void Restore(FEditor &Editor, const FSnapshot &Snapshot);
		void CancelComposition(FEditor &Editor);
		static void Remember(std::deque<FSnapshot> &History, FSnapshot Snapshot);
		std::map<std::uint64_t, FEditor> Editors;
		std::uint64_t FocusId = 0;
		Rml::TextInputContext *Active = nullptr;
		Gui::FTextInputServices Services;
	};
} // namespace VeroScript::RmlUi

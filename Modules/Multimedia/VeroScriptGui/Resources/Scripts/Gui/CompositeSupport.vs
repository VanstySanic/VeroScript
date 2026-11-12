// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "Controls.vs";
export import "Widget.vs";
export import "Binding.vs";

namespace VS::Gui
{
	export enum class Key
	{
		Backspace = 8,
		Tab = 9,
		Enter = 13,
		Escape = 27,
		Space = 32,
		Left = 256,
		Right = 257,
		Up = 258,
		Down = 259,
		Home = 260,
		End = 261,
		PageUp = 262,
		PageDown = 263
	};

	namespace Internal
	{
		export T FindAncestorOrSelf<T>(Widget node)
		{
			while (node != nullptr)
			{
				optional<T> found = try_cast<T>(node);
				if (found.has_value()) return found.get();
				node = node.GetParent();
			}
			return nullptr;
		}

		export bool IsWithin(Widget node, Widget ancestor)
		{
			while (node != nullptr && node != ancestor)
			{
				node = node.GetParent();
			}
			return node != nullptr;
		}

		export bool IsInteractive(Widget node)
		{
			if (node == nullptr) return false;
			while (node != nullptr)
			{
				if (node.IsDisposed() || !node.GetEnabled() || node.GetVisibility() != Visibility::Visible || !node.__GetNativeHandle().IsContentVisible()) return false;
				int kind = node.__GetNativeHandle().GetControlKind();
				if ((kind == 17 || kind == 18) && !node.__GetNativeHandle().GetControlOpen()) return false;
				node = node.GetParent();
			}
			return true;
		}

		export bool IsTextInput(Widget node)
		{
			if (node == nullptr) return false;
			int kind = node.__GetNativeHandle().GetControlKind();
			return kind == 8 || kind == 9 || kind == 15 || kind == 16;
		}

		// Internal content container. Its callback observes the composite weakly.
		@ GuiWidget export class ContentHost : public VS::Gui::Stack
		{
		public:
			ContentHost()
			{
				VsguiObserveChildChanges();
			}
			void VsguiObserve(fn(ContentHost) -> void value)
			{
				changed = optional < fn(ContentHost) -> void > (value);
			}
		protected:
			void OnChildrenChanged() override
			{
				if (changed.has_value())
				{
					fn(ContentHost) -> void callback = changed.get();
					callback(this);
				}
			}
		private:
			optional < fn(ContentHost) -> void > changed;
		};
	}
}

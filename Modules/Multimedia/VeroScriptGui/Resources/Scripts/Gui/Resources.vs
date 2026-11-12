// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "Widget.vs";

namespace VS::Gui
{
	export enum class BindingContextMode
	{
		None,
		Automatic,
		Provided
	};
	export class FactoryArgument
	{
	};

	export class FactoryArgumentOf<T> : public FactoryArgument
	{
	public:
		FactoryArgumentOf(T value)
		{
			Value = value;
		}
		T Value;
	};

	export struct FactoryDefinition
	{
		array<string> RootTypes;
		string ContextType;
		BindingContextMode ContextMode = VS::Gui::BindingContextMode::None;
		fn(FactoryArgument) -> Widget Create;
	};

	export class FactoryRegistry
	{
	public:
		static bool Register(string identity, array<string> rootTypes, string contextType, fn(FactoryArgument) -> Widget create)
		{
			return VS::Gui::FactoryRegistry::Register(identity, rootTypes, contextType, create, contextType == "" ? VS::Gui::BindingContextMode::None : VS::Gui::BindingContextMode::Provided);
		}
		static bool Register(string identity, array<string> rootTypes, string contextType, fn(FactoryArgument) -> Widget create, BindingContextMode mode)
		{
			map<string, FactoryDefinition> values = VS::Gui::FactoryRegistry::Definitions;
			if (values.has(identity))
			{
				FactoryDefinition previous = values[identity];
				if (previous.Create != create || previous.ContextType != contextType || previous.ContextMode != mode)
				{
					throw "Conflicting GUI resource factory registration";
				}
				if (len(previous.RootTypes) != len(rootTypes))
				{
					throw "Conflicting GUI factory root types";
				}
				for (int64 i = 0; i < len(rootTypes); i++)
				{
					if (previous.RootTypes[i] != rootTypes[i])
					{
						throw "Conflicting GUI factory root types";
					}
				}
				return true;
			}
			FactoryDefinition next = FactoryDefinition();
			for (string name : rootTypes)
			{
				next.RootTypes.push(name);
			}
			next.ContextType = contextType;
			next.ContextMode = mode;
			next.Create = create;
			values[identity] = next;
			return true;
		}

		static FactoryDefinition Get(string identity)
		{
			map<string, FactoryDefinition> values = VS::Gui::FactoryRegistry::Definitions;
			if (!values.has(identity))
			{
				throw "GUI resource has not been compiled or imported: " + identity;
			}
			return values[identity];
		}

	private:
		static map<string, FactoryDefinition> Definitions = map<string, FactoryDefinition>();
	};

	export struct WidgetFactory<TWidget>
	{
	public:
		WidgetFactory(string identity, FactoryDefinition definition)
		{
			type<TWidget> expected = type<TWidget>();
			bool compatible = false;
			for (string name : definition.RootTypes)
			{
				if (name == expected.qualified_name)
				{
					compatible = true;
				}
			}
			if (!compatible)
			{
				throw "GUI factory root type does not match the requested widget type";
			}
			resource = identity;
			blueprint = definition;
		}

		TWidget Create()
		{
			return VsguiCreate(nullptr);
		}

		string GetResourceIdentity() const
		{
			return resource;
		}
		string GetBindingContextType() const
		{
			return blueprint.ContextType;
		}
		BindingContextMode GetBindingContextMode() const
		{
			return blueprint.ContextMode;
		}

		TWidget VsguiCreate(FactoryArgument argument)
		{
			if (argument == nullptr && blueprint.ContextMode == VS::Gui::BindingContextMode::Provided)
			{
				throw "GUI page requires a binding context";
			}
			if (argument != nullptr && blueprint.ContextMode != VS::Gui::BindingContextMode::Provided)
			{
				throw "GUI page defines its own binding context";
			}
			fn(FactoryArgument) -> Widget create = blueprint.Create;
			if (create == nullptr)
			{
				throw "GUI factory has not been initialized";
			}
			Widget root = create(argument);
			TWidget result = cast<TWidget>(root);
			if (result == nullptr)
			{
				throw "GUI factory produced an unexpected widget type";
			}
			return result;
		}

	private:
		string resource;
		FactoryDefinition blueprint;
	};

	export TWidget Create<TWidget, TContext>(WidgetFactory<TWidget> factory, TContext context)
	{
		if (factory.GetBindingContextMode() != VS::Gui::BindingContextMode::Provided)
		{
			throw "GUI page defines its own binding context";
		}
		type<TContext> provided = type<TContext>();
		if (provided.qualified_name != factory.GetBindingContextType())
		{
			throw "GUI factory binding context type mismatch";
		}
		FactoryArgument argument = FactoryArgumentOf<TContext>(context);
		return factory.VsguiCreate(argument);
	}

	export WidgetFactory<TWidget> LoadFactory<TWidget>(string reference)
	{
		string identity = VS::Gui::__NativeResources::Resolve(reference);
		return WidgetFactory<TWidget>(identity, VS::Gui::FactoryRegistry::Get(identity));
	}
} // namespace VS::Gui

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiMarkupInternal.h"
#include <algorithm>

namespace VeroScript::Gui
{
	namespace
	{
		const FMarkupAttribute* Attribute(const FMarkupElement& Element, const std::string& Name)
		{
			const auto Found = std::find_if(Element.Attributes.begin(), Element.Attributes.end(),
			    [&](const auto& Item)
			{
				return Item.Name == Name;
			});
			return Found == Element.Attributes.end() ? nullptr : &*Found;
		}

		std::string Value(const FMarkupElement& Element, const std::string& Name)
		{
			const auto* Found = Attribute(Element, Name);
			return Found ? Found->Value : std::string{};
		}

		bool QualifiedType(std::string_view Name)
		{
			while (!Name.empty())
			{
				const auto Separator = Name.find("::");
				const auto Part = Name.substr(0, Separator);
				if (!IsMarkupName(Part) || Part.find_first_of("-.") != std::string_view::npos)
				{
					return false;
				}
				if (Separator == std::string_view::npos)
				{
					return true;
				}
				Name.remove_prefix(Separator + 2);
			}
			return false;
		}

		bool Attributes(const FMarkupElement& Element, std::initializer_list<const char*> Allowed,
		    FMarkupDiagnostic& Diagnostic)
		{
			for (const auto& Item : Element.Attributes)
			{
				if (std::find(Allowed.begin(), Allowed.end(), Item.Name) == Allowed.end())
				{
					return MarkupFail(Diagnostic, Item.Location, "gui.unknown_directive_attribute: " + Item.Name);
				}
			}
			return true;
		}

		bool Read(FMarkupData& Data, const FMarkupSource& Requester, const FMarkupAttribute& Reference,
		    FMarkupSource& Source, FMarkupDiagnostic& Diagnostic)
		{
			if (Data.Options.CheckCancelled)
			{
				Data.Options.CheckCancelled();
			}
			std::string Error;
			if (Reference.Value.empty() || !Data.Options.Read ||
			    !Data.Options.Read(Requester.Identity, Reference.Value, Source, Error))
			{
				return MarkupFail(Diagnostic, Reference.Location,
				    Error.empty() ? "gui.resource_reader_required: " + Reference.Value : Error);
			}
			if (Source.Identity.empty() || Source.Text.size() > Data.Options.MaxSourceBytes)
			{
				return MarkupFail(Diagnostic, Reference.Location, "gui.invalid_resource_or_source_limit");
			}
			const auto Existing = std::find_if(Data.Dependencies.begin(), Data.Dependencies.end(),
			    [&](const auto& Item)
			{
				return Item.Identity == Source.Identity;
			});
			if (Existing != Data.Dependencies.end())
			{
				if (Existing->Text != Source.Text)
				{
					return MarkupFail(Diagnostic, Reference.Location, "gui.resource_changed_during_compile");
				}
			}
			else
			{
				if (Data.Dependencies.size() >= Data.Options.MaxDependencies)
				{
					return MarkupFail(Diagnostic, Reference.Location, "gui.dependency_limit");
				}
				Data.Dependencies.push_back(Source);
			}
			return true;
		}

		bool ValidateNode(const FMarkupElement& Element, const FMarkupData& Data, FMarkupDiagnostic& Diagnostic,
		    bool bRoot)
		{
			if (!IsMarkupName(Element.Name) ||
			    (!Data.Types.count(Element.Name) && !Data.Components.count(Element.Name)))
			{
				return MarkupFail(Diagnostic, Element.Location, "gui.unknown_widget: " + Element.Name);
			}
			if (!TrimMarkup(Element.Text).empty())
			{
				return MarkupFail(Diagnostic, Element.TextLocation, "gui.raw_text_requires_text_property");
			}
			std::set<EProperty> Properties;
			std::set<Platform::EEventType> Events;
			for (const auto& Item : Element.Attributes)
			{
				if (Item.Name == "id" || Item.Name == "class")
				{
					continue;
				}
				if (Item.Name == "Id" || Item.Name == "ID")
				{
					return MarkupFail(Diagnostic, Item.Location, "gui.use_lowercase_id");
				}
				const auto Colon = Item.Name.find(':');
				const auto Prefix = Colon == std::string::npos ? std::string{} : Item.Name.substr(0, Colon);
				const auto Name = Colon == std::string::npos ? Item.Name : Item.Name.substr(Colon + 1);
				if (Prefix == "on")
				{
					Platform::EEventType Event;
					if (!FindMarkupEvent(Name, Event))
					{
						return MarkupFail(Diagnostic, Item.Location, "gui.unknown_event: " + Name);
					}
					if (!Events.insert(Event).second)
					{
						return MarkupFail(Diagnostic, Item.Location, "gui.duplicate_event: " + Name);
					}
				}
				else
				{
					if (!Prefix.empty() && Prefix != "bind")
					{
						return MarkupFail(Diagnostic, Item.Location, "gui.unknown_prefix: " + Prefix);
					}
					EProperty Property;
					FPropertyValue Parsed;
					std::string Error;
					if (!(Prefix == "bind" ? FindMarkupProperty(Name, Property, false, Error)
					                       : ParseMarkupProperty(Name, Item.Value, Property, Parsed, false, Error)))
					{
						return MarkupFail(Diagnostic, Item.Location, Error);
					}
					if (!Properties.insert(Property).second)
					{
						return MarkupFail(Diagnostic, Item.Location, "gui.duplicate_property: " + Name);
					}
				}
				if (!Prefix.empty() && TrimMarkup(Item.Value).empty())
				{
					return MarkupFail(Diagnostic, Item.Location, "gui.empty_binding");
				}
			}
			for (const auto& Child : Element.Children)
			{
				if (bRoot && (Child.Name == "import" || Child.Name == "style" || Child.Name == "script"))
				{
					continue;
				}
				if (!ValidateNode(Child, Data, Diagnostic, false))
				{
					return false;
				}
			}
			return true;
		}
	} // namespace

	bool CompileMarkupUnit(FMarkupData& Data, const FMarkupSource& Source, std::set<std::string>& Visiting,
	    FMarkupDiagnostic& Diagnostic)
	{
		if (Visiting.count(Source.Identity))
		{
			return MarkupFail(Diagnostic, MarkupLocation(Source, 0), "gui.import_cycle");
		}
		if (Data.Units.count(Source.Identity))
		{
			return true;
		}
		if (Visiting.size() >= Data.Options.MaxDepth)
		{
			return MarkupFail(Diagnostic, MarkupLocation(Source, 0), "gui.import_depth");
		}
		Visiting.insert(Source.Identity);
		FMarkupUnit Unit;
		Unit.Source = Source;
		if (!ParseMarkup(Source, Data.Options, Unit.Root, Diagnostic))
		{
			return false;
		}
		Unit.bComponent = Unit.Root.Name == "component";
		Unit.Script.bComponent = Unit.bComponent;
		Unit.Script.Location = Unit.Root.Location;
		if (!TrimMarkup(Unit.Root.Text).empty())
		{
			return MarkupFail(Diagnostic, Unit.Root.TextLocation, "gui.raw_text_requires_text_property");
		}
		if (Unit.bComponent)
		{
			if (!Attributes(Unit.Root, {"type", "mark", "base", "id"}, Diagnostic))
			{
				return false;
			}
			Unit.Type = Value(Unit.Root, "type");
			Unit.Mark = Value(Unit.Root, "mark");
			Unit.Base = Value(Unit.Root, "base");
			Unit.DefaultId = Value(Unit.Root, "id");
			if (Unit.Base.empty() || Unit.Base == "VS::Gui::Widget")
			{
				Unit.Base = "Widget";
			}
			const auto Qualified = Unit.Type.rfind("::");
			const auto Short = Qualified == std::string::npos ? Unit.Type : Unit.Type.substr(Qualified + 2);
			if (!QualifiedType(Unit.Type) || (!Unit.Mark.empty() && !IsMarkupName(Unit.Mark)))
			{
				return MarkupFail(Diagnostic, Unit.Root.Location, "gui.invalid_component_name");
			}
			for (const auto& Name : std::vector<std::string>{Short, Unit.Mark, Unit.Type})
			{
				if (Name.empty())
				{
					continue;
				}
				if (Name == "component" || Name == "import" || Name == "script" || Name == "style" ||
				    Name == "template" || Data.Types.count(Name) ||
				    (Data.Components.count(Name) && Data.Components.at(Name) != Source.Identity))
				{
					return MarkupFail(Diagnostic, Unit.Root.Location, "gui.widget_name_conflict: " + Name);
				}
				Data.Components[Name] = Source.Identity;
			}
		}
		bool bTemplate = false;
		for (const auto& Child : Unit.Root.Children)
		{
			if (Child.Name == "import")
			{
				if (!Attributes(Child, {"src"}, Diagnostic))
				{
					return false;
				}
				const auto* Src = Attribute(Child, "src");
				if (!Src || !Child.Children.empty() || !TrimMarkup(Child.Text).empty())
				{
					return MarkupFail(Diagnostic, Child.Location, "gui.invalid_import");
				}
				FMarkupSource Imported;
				if (!Read(Data, Source, *Src, Imported, Diagnostic))
				{
					return false;
				}
				Unit.Imports.push_back(Imported.Identity);
				if (Src->Value.size() >= 6 && Src->Value.substr(Src->Value.size() - 6) == ".vswml")
				{
					if (!CompileMarkupUnit(Data, Imported, Visiting, Diagnostic))
					{
						return false;
					}
					if (!Data.Units.at(Imported.Identity).bComponent)
					{
						return MarkupFail(Diagnostic, Src->Location, "gui.import_requires_component");
					}
				}
				else if (Src->Value.size() >= 3 && Src->Value.substr(Src->Value.size() - 3) == ".vs")
				{
					Unit.Script.Imports.push_back(std::move(Imported));
				}
				else
				{
					return MarkupFail(Diagnostic, Src->Location, "gui.unsupported_import_type");
				}
			}
			else if (Child.Name == "style")
			{
				if (!Attributes(Child, {"src"}, Diagnostic) || !Child.Children.empty())
				{
					return MarkupFail(Diagnostic, Child.Location, "gui.invalid_style_directive");
				}
				const auto* Src = Attribute(Child, "src");
				FMarkupSource StyleSource{Source.Identity, Child.Text};
				if (Src)
				{
					if (!TrimMarkup(Child.Text).empty())
					{
						return MarkupFail(Diagnostic, Child.Location, "gui.style_src_and_inline");
					}
					if (!Read(Data, Source, *Src, StyleSource, Diagnostic))
					{
						return false;
					}
				}
				auto Sheet = std::make_shared<FStyleSheetData>();
				if (!ParseStyleSheet(StyleSource, *Sheet, Diagnostic, !Data.bDeferSchema))
				{
					if (!Src)
					{
						Diagnostic.Location =
						    MarkupLocation(Source, Child.TextLocation.Offset + Diagnostic.Location.Offset);
					}
					return false;
				}
				if (!Src)
				{
					for (auto& Rule : Sheet->Rules)
					{
						for (auto& Declaration : Rule.Declarations)
						{
							Declaration.Location =
							    MarkupLocation(Source, Child.TextLocation.Offset + Declaration.Location.Offset);
						}
					}
				}
				for (auto& Rule : Sheet->Rules)
				{
					for (auto& Declaration : Rule.Declarations)
					{
						Declaration.ResolveResource = Data.Options.ResolveResource;
					}
				}
				Unit.Styles.push_back(std::move(Sheet));
			}
			else if (Child.Name == "script")
			{
				if (Unit.bHasScript)
				{
					return MarkupFail(Diagnostic, Child.Location, "gui.multiple_scripts");
				}
				if (!Attributes(Child, {"src", "type"}, Diagnostic) || !Child.Children.empty())
				{
					return MarkupFail(Diagnostic, Child.Location, "gui.invalid_script_directive");
				}
				Unit.bHasScript = true;
				Unit.Script.Location = Child.TextLocation;
				Unit.Script.Type = Value(Child, "type");
				const auto* Src = Attribute(Child, "src");
				if (Src)
				{
					if (Unit.bComponent || Unit.Script.Type.empty() || !TrimMarkup(Child.Text).empty())
					{
						return MarkupFail(Diagnostic, Child.Location, "gui.invalid_external_script");
					}
					if (!Read(Data, Source, *Src, Unit.Script.Source, Diagnostic))
					{
						return false;
					}
				}
				else
				{
					if (!Unit.Script.Type.empty())
					{
						return MarkupFail(Diagnostic, Child.Location, "gui.script_type_requires_src");
					}
					Unit.Script.bInline = true;
					Unit.Script.Source = {Source.Identity, Child.Text};
					Unit.Script.TextSpans = Child.TextSpans;
				}
			}
			else if (Unit.bComponent)
			{
				if (Child.Name != "template" || bTemplate || !Child.Attributes.empty() ||
				    !TrimMarkup(Child.Text).empty())
				{
					return MarkupFail(Diagnostic, Child.Location, "gui.single_template_required");
				}
				bTemplate = true;
			}
		}
		if (Unit.bComponent && !bTemplate)
		{
			return MarkupFail(Diagnostic, Unit.Root.Location, "gui.template_required");
		}
		if (Unit.bComponent)
		{
			Unit.Script.Type = Unit.Type;
			Unit.Script.bInline = true;
		}
		if (Unit.bComponent && !Data.bDeferSchema)
		{
			if (!Data.Types.count(Unit.Base) && !Data.Components.count(Unit.Base))
			{
				return MarkupFail(Diagnostic, Unit.Root.Location, "gui.unknown_base: " + Unit.Base);
			}
			for (const auto& Child : Unit.Root.Children)
			{
				if (Child.Name != "template")
				{
					continue;
				}
				for (const auto& Node : Child.Children)
				{
					if (!ValidateNode(Node, Data, Diagnostic, false))
					{
						return false;
					}
				}
			}
		}
		else if (!Data.bDeferSchema && !ValidateNode(Unit.Root, Data, Diagnostic, true))
		{
			return false;
		}
		Visiting.erase(Source.Identity);
		Data.Units.emplace(Source.Identity, std::move(Unit));
		return true;
	}

	bool ValidateMarkupGraph(const FMarkupData& Data, FMarkupDiagnostic& Diagnostic)
	{
		std::set<std::string> Active, Complete;
		const auto VisitUnit = [&](auto&& Self, const std::string& Identity) -> bool
		{
			if (Complete.count(Identity))
			{
				return true;
			}
			const auto& Unit = Data.Units.at(Identity);
			if (!Active.insert(Identity).second)
			{
				return MarkupFail(Diagnostic, Unit.Root.Location, "gui.recursive_component_template");
			}
			if (Unit.bComponent)
			{
				const auto Base = Data.Components.find(Unit.Base);
				if (Base != Data.Components.end() && !Self(Self, Base->second))
				{
					return false;
				}
			}
			std::vector<const FMarkupElement*> Stack;
			if (Unit.bComponent)
			{
				for (const auto& Child : Unit.Root.Children)
				{
					if (Child.Name == "template")
					{
						for (const auto& Entry : Child.Children)
						{
							Stack.push_back(&Entry);
						}
					}
				}
			}
			else
			{
				Stack.push_back(&Unit.Root);
			}
			while (!Stack.empty())
			{
				const auto* Element = Stack.back();
				Stack.pop_back();
				const auto Component = Data.Components.find(Element->Name);
				if (Component != Data.Components.end() && !Self(Self, Component->second))
				{
					return false;
				}
				for (const auto& Child : Element->Children)
				{
					if (Child.Name != "import" && Child.Name != "script" && Child.Name != "style")
					{
						Stack.push_back(&Child);
					}
				}
			}
			Active.erase(Identity);
			Complete.insert(Identity);
			return true;
		};
		for (const auto& Unit : Data.Units)
		{
			if (!VisitUnit(VisitUnit, Unit.first))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::Gui

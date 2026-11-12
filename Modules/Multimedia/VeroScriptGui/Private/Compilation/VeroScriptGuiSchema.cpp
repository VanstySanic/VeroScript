// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiSchema.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptGuiTypes.h"
#include "VeroScriptGuiSource.h"
#include <algorithm>
#include <charconv>
#include <functional>
#include <iterator>
#include <set>

namespace VeroScript::Gui
{
	namespace
	{
		std::string TypeName(std::string Value)
		{
			for (std::size_t At = Value.find("::"); At != std::string::npos; At = Value.find("::", At + 1))
			{
				Value.replace(At, 2, ".");
			}
			return Value;
		}

		std::string Leaf(const std::string& Name)
		{
			const auto At = Name.rfind('.');
			return At == std::string::npos ? Name : Name.substr(At + 1);
		}

		bool MarkName(const std::string& Name)
		{
			if (Name.empty())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Name.size(); ++Index)
			{
				const auto C = static_cast<unsigned char>(Name[Index]);
				if (!(C == '_' || (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
				        (Index && (C == '-' || C == '.' || (C >= '0' && C <= '9')))))
				{
					return false;
				}
			}
			return true;
		}

		void Fail(std::vector<FScriptError>& Errors, const FAnnotationSourcePosition& Where, const std::string& Message)
		{
			FScriptError Error;
			Error.MessageId = "gui.schema.declaration";
			Error.Message = Message;
			Error.Location.FileLabel = Where.FileLabel;
			Error.Location.Line = Where.Line;
			Error.Location.Column = Where.Column;
			Errors.push_back(std::move(Error));
		}

		const FDeclarationAnnotation* FindAnnotation(const FAnnotationDeclarationView& Declaration,
		    const FSourceMetadataSnapshot& Metadata, const char* Name,
		    std::vector<FScriptError>& Errors)
		{
			const FDeclarationAnnotation* Result = nullptr;
			for (const auto& Item : Declaration.Metadata.Annotations)
			{
				const auto Found = std::find_if(Metadata.Definitions.begin(), Metadata.Definitions.end(),
				    [&](const auto& Value)
				{
					return Value.Id == Item.DefinitionId;
				});
				const auto* Definition = Found == Metadata.Definitions.end() ? nullptr : &*Found;
				std::string_view Written = Item.Name, Expected = Name;
				if (!Written.empty() && Written.front() == '@')
				{
					Written.remove_prefix(1);
				}
				if (!Expected.empty() && Expected.front() == '@')
				{
					Expected.remove_prefix(1);
				}
				if (!AnnotationNamesEqual(Written, Expected, !Definition || Definition->CaseSensitive))
				{
					continue;
				}
				if (Definition && Definition->CaseSensitive)
				{
					Fail(Errors, Item.NamePosition, "GUI annotations require CaseSensitive=false");
				}
				if (Result)
				{
					Fail(Errors, Item.NamePosition, "Duplicate GUI annotation: " + std::string(Name));
				}
				Result = &Item;
			}
			return Result;
		}

		struct FOptions
		{
			std::string Mark, Id;
			bool bStyle = false;
		};

		FOptions Options(const FDeclarationAnnotation& Annotation, bool bProperty, bool bWidget,
		    std::vector<FScriptError>& Errors)
		{
			FOptions Result;
			std::set<std::string> Seen;
			for (const auto& Argument : Annotation.Arguments)
			{
				std::string Key = Argument.Name;
				for (auto& C : Key)
				{
					if (C >= 'A' && C <= 'Z')
					{
						C = static_cast<char>(C + ('a' - 'A'));
					}
				}
				if (!Seen.insert(Key).second)
				{
					Fail(Errors, Argument.Range.Begin, "Duplicate GUI annotation parameter: " + Argument.Name);
					continue;
				}
				if (Key == "style" && bProperty && Argument.Kind == EAnnotationArgumentKind::Flag)
				{
					Result.bStyle = true;
				}
				else if ((Key == "mark" || (Key == "id" && bWidget)) &&
				    Argument.Kind == EAnnotationArgumentKind::Scalar &&
				    Argument.ScalarKind == EAnnotationScalarKind::String)
				{
					if (Key == "mark")
					{
						if (!MarkName(Argument.Value))
						{
							Fail(Errors, Argument.Range.Begin, "Invalid GUI Mark: " + Argument.Value);
						}
						Result.Mark = Argument.Value;
					}
					else
					{
						Result.Id = Argument.Value;
					}
				}
				else
				{
					Fail(Errors, Argument.Range.Begin, "Invalid GUI annotation parameter: " + Argument.Name);
				}
			}
			return Result;
		}

		bool Generic(const std::string& Source, const std::string& Base, std::vector<std::string>& Arguments)
		{
			const auto Marker = FindGenericTypeMarker(Source);
			if (Marker != std::string::npos)
			{
				const auto Template = Source.substr(0, Marker);
				const auto Simple = Base.compare(0, 4, "std.") == 0 ? Base.substr(4) : Base;
				if (Template != Base && Template != Simple)
				{
					return false;
				}
				std::vector<std::string> Values;
				for (std::size_t Position = Marker + 1; Position < Source.size();)
				{
					std::size_t Length = 0;
					const auto Parsed =
					    std::from_chars(Source.data() + Position, Source.data() + Source.size(), Length);
					if (Parsed.ec != std::errc{} || Parsed.ptr == Source.data() + Source.size() || *Parsed.ptr != ':')
					{
						return false;
					}
					Position = static_cast<std::size_t>(Parsed.ptr - Source.data()) + 1;
					if (!Length || Length > Source.size() - Position)
					{
						return false;
					}
					Values.push_back(Source.substr(Position, Length));
					Position += Length;
				}
				Arguments = std::move(Values);
				return true;
			}
			std::string Text;
			for (const auto C : Source)
			{
				if (C != ' ' && C != '\t' && C != '\r' && C != '\n')
				{
					Text += C;
				}
			}
			const auto Simple = Base.compare(0, 4, "std.") == 0 ? Base.substr(4) : Base;
			const auto Prefix = Text.compare(0, Base.size() + 1, Base + "<") == 0 ? Base : Simple;
			if (Text.compare(0, Prefix.size() + 1, Prefix + "<") != 0 || Text.back() != '>')
			{
				return false;
			}
			std::size_t Begin = Prefix.size() + 1;
			unsigned Depth = 0;
			for (std::size_t Index = Begin; Index + 1 < Text.size(); ++Index)
			{
				if (Text[Index] == '<' || Text[Index] == '(')
				{
					++Depth;
				}
				else if (Text[Index] == '>' || Text[Index] == ')')
				{
					if (!Depth)
					{
						return false;
					}
					--Depth;
				}
				else if (Text[Index] == ',' && !Depth)
				{
					Arguments.push_back(Text.substr(Begin, Index - Begin));
					Begin = Index + 1;
				}
			}
			if (Depth)
			{
				return false;
			}
			if (Begin + 1 < Text.size())
			{
				Arguments.push_back(Text.substr(Begin, Text.size() - Begin - 1));
			}
			return true;
		}

		template <class T>
		bool UniqueNames(const std::vector<T>& Members)
		{
			std::map<std::string, std::string> Names;
			for (const auto& Member : Members)
			{
				for (const auto& Name : {Member.Name, Member.Mark})
				{
					if (Name.empty())
					{
						continue;
					}
					const auto Inserted = Names.emplace(Name, Member.Identity);
					if (!Inserted.second && Inserted.first->second != Member.Identity)
					{
						return false;
					}
				}
			}
			return true;
		}
	} // namespace

	std::shared_ptr<const FGuiSchema> FGuiSchema::Build(const std::vector<FAnnotationDeclarationView>& Declarations,
	    const FSourceMetadataSnapshot& Metadata,
	    std::vector<FScriptError>& Diagnostics)
	{
		Diagnostics.clear();
		auto Result = std::shared_ptr<FGuiSchema>(new FGuiSchema);
		std::map<std::string, std::size_t> Owners;
		for (const auto& Declaration : Declarations)
		{
			const auto* Annotation = FindAnnotation(Declaration, Metadata, "GuiWidget", Diagnostics);
			if (Annotation && Declaration.Target != EAnnotationTarget::Class)
			{
				Fail(Diagnostics, Declaration.Location, "GuiWidget requires a class");
			}
			if (Declaration.Target != EAnnotationTarget::Class)
			{
				continue;
			}
			if (!Annotation && GuiHasTemplateParameters(Declaration))
			{
				continue;
			}
			FGuiTypeSchema Type;
			Type.Identity = Declaration.ModulePath + "|" + Declaration.Name;
			Type.DeclarationId = Declaration.Id;
			Type.ModuleId = Declaration.ModulePath;
			Type.Name = TypeName(Declaration.Name);
			Type.CanonicalName = Declaration.bExported || Declaration.ModulePath.empty()
			    ? Type.Name
			    : Declaration.ModulePath + '\x1f' + Type.Name;
			Type.Location = Declaration.Location;
			Type.bRegistered = Annotation || Type.Name == "VS.Gui.Widget";
			Type.bWidget = Type.Name == "VS.Gui.Widget";
			Type.bExported = Declaration.bExported;
			Type.Bases =
			    Declaration.CanonicalBaseTypes.empty() ? Declaration.BaseTypes : Declaration.CanonicalBaseTypes;
			Type.BaseAccess = Declaration.BaseAccess;
			for (auto& Base : Type.Bases)
			{
				Base = TypeName(std::move(Base));
			}
			if (Annotation)
			{
				const auto Parsed = Options(*Annotation, false, true, Diagnostics);
				Type.Mark = Parsed.Mark;
				Type.DefaultId = Parsed.Id;
				if (Declaration.bIsStatic || GuiHasTemplateParameters(Declaration) || Declaration.bNested ||
				    Declaration.bLocal)
				{
					Fail(Diagnostics, Declaration.Location, "GuiWidget requires a closed, nonstatic namespace-scope class");
				}
			}
			Owners.emplace(Declaration.Id, Result->Types.size());
			if (!Result->TypeNames.emplace(Type.CanonicalName, Result->Types.size()).second)
			{
				Fail(Diagnostics, Declaration.Location, "Ambiguous GUI type name: " + Type.Name);
			}
			Result->Types.push_back(std::move(Type));
		}
		std::vector<unsigned char> Lineage(Result->Types.size());
		std::function<bool(std::size_t)> Classify = [&](std::size_t Index)
		{
			if (Lineage[Index] == 1)
			{
				Fail(Diagnostics, Result->Types[Index].Location, "Recursive GUI inheritance");
				return false;
			}
			if (Lineage[Index] == 2)
			{
				return true;
			}
			Lineage[Index] = 1;
			auto& Type = Result->Types[Index];
			unsigned WidgetBases = 0;
			for (std::size_t BaseIndex = 0; BaseIndex < Type.Bases.size(); ++BaseIndex)
			{
				const auto Found = Result->TypeNames.find(Type.Bases[BaseIndex]);
				if (Found == Result->TypeNames.end())
				{
					continue;
				}
				if (!Classify(Found->second))
				{
					return false;
				}
				if (Result->Types[Found->second].bWidget)
				{
					++WidgetBases;
					Type.bWidget = true;
					if (BaseIndex >= Type.BaseAccess.size() || Type.BaseAccess[BaseIndex] != EMemberAccess::Public)
					{
						Fail(Diagnostics, Type.Location, "GUI widget inheritance must be public");
					}
				}
			}
			if (WidgetBases > 1 || (Type.bRegistered && !Type.bWidget))
			{
				Fail(Diagnostics, Type.Location, "GuiWidget requires exactly one public Widget inheritance path");
			}
			Lineage[Index] = 2;
			return true;
		};
		for (std::size_t Index = 0; Index < Result->Types.size(); ++Index)
		{
			if (!Classify(Index))
			{
				return nullptr;
			}
		}
		for (const auto& Declaration : Declarations)
		{
			const auto* Property = FindAnnotation(Declaration, Metadata, "GuiProperty", Diagnostics);
			const auto* Method = FindAnnotation(Declaration, Metadata, "GuiMethod", Diagnostics);
			const auto* Event = FindAnnotation(Declaration, Metadata, "GuiEvent", Diagnostics);
			if (!Property && !Method && !Event)
			{
				continue;
			}
			const auto Owner = Owners.find(Declaration.OwnerId);
			if (Owner == Owners.end() || (Property && Event) || (Method && (Property || Event)))
			{
				Fail(Diagnostics, Declaration.Location, "GUI member requires one annotation on a class member");
				continue;
			}
			auto& Type = Result->Types[Owner->second];
			if ((Property || Event) && (!Type.bWidget || Declaration.Target != EAnnotationTarget::Field || Declaration.bIsStatic || Declaration.bIsConst || Declaration.bIsRef))
			{
				Fail(Diagnostics, Declaration.Location, "GUI properties/events require mutable widget instance fields");
				continue;
			}
			const auto Identity = Type.Identity + "|" + Declaration.Name;
			const auto StorageType =
			    Declaration.CanonicalTypeName.empty() ? TypeName(Declaration.TypeName) : Declaration.CanonicalTypeName;
			if (Property)
			{
				const auto Parsed = Options(*Property, true, false, Diagnostics);
				FGuiPropertySchema Value;
				Value.Identity = Identity;
				Value.DeclarationId = Declaration.Id;
				Value.Name = Declaration.Name;
				Value.Mark = Parsed.Mark;
				Value.bStyle = Parsed.bStyle;
				for (const auto& Name : {Value.Name, Value.Mark})
				{
					if (AnnotationNamesEqual(Name, "id", false) || AnnotationNamesEqual(Name, "class", false))
					{
						Fail(Diagnostics, Declaration.Location, "GUI property uses a reserved identifier: " + Name);
					}
				}
				Value.StorageType = StorageType;
				Value.ValueType = StorageType;
				Value.DefaultSource = Declaration.DefaultValueSource;
				Value.Location = Declaration.Location;
				std::vector<std::string> Arguments;
				if (Generic(StorageType, "std.attribute", Arguments))
				{
					if (Arguments.size() != 1)
					{
						Fail(Diagnostics, Declaration.Location, "GuiProperty attribute requires one complete type");
					}
					else
					{
						Value.bBindable = true;
						Value.ValueType = Arguments.front();
					}
				}
				if (Value.ValueType.empty() || Value.ValueType == "void" || Value.ValueType == "auto")
				{
					Fail(Diagnostics, Declaration.Location, "GuiProperty requires a complete value type");
				}
				Type.Properties.push_back(std::move(Value));
			}
			else if (Event)
			{
				const auto Parsed = Options(*Event, false, false, Diagnostics);
				FGuiEventSchema Value;
				Value.Identity = Identity;
				Value.DeclarationId = Declaration.Id;
				Value.Name = Declaration.Name;
				Value.Mark = Parsed.Mark;
				Value.Location = Declaration.Location;
				std::vector<std::string> Arguments;
				if (Generic(StorageType, "std.delegate", Arguments) && !Arguments.empty() && Arguments.size() <= 4)
				{
					Value.ReturnType = Arguments.front();
					Value.ParameterTypes.assign(Arguments.begin() + 1, Arguments.end());
				}
				else
				{
					Arguments.clear();
					if (!Generic(StorageType, "std.multicast_delegate", Arguments) || Arguments.size() > 3)
					{
						Fail(Diagnostics, Declaration.Location,
						    "GuiEvent requires a supported std delegate type: " + StorageType);
						continue;
					}
					Value.Kind = EGuiEventKind::Multicast;
					Value.ReturnType = "void";
					Value.ParameterTypes = std::move(Arguments);
				}
				Type.Events.push_back(std::move(Value));
			}
			else
			{
				if (Declaration.Target != EAnnotationTarget::Method || Declaration.bIsStatic || Declaration.bDeleted ||
				    GuiHasTemplateParameters(Declaration))
				{
					Fail(Diagnostics, Declaration.Location, "GuiMethod requires a callable instance method");
					continue;
				}
				const auto Parsed = Options(*Method, false, false, Diagnostics);
				FGuiMethodSchema Value;
				Value.Identity = Identity + "(";
				for (const auto& Parameter : Declaration.Parameters)
				{
					Value.Identity += Parameter.CanonicalTypeName + ";";
				}
				Value.Identity += Declaration.bIsConst ? ")const" : ")";
				Value.DeclarationId = Declaration.Id;
				Value.Name = Declaration.Name;
				Value.Mark = Parsed.Mark;
				Value.ReturnType = StorageType;
				Value.Parameters = Declaration.Parameters;
				Value.Location = Declaration.Location;
				Value.Access = Declaration.Access;
				Value.bConst = Declaration.bIsConst;
				Type.Methods.push_back(std::move(Value));
			}
		}
		for (auto& Type : Result->Types)
		{
			if (Type.Name != "VS.Gui.Widget")
			{
				continue;
			}
			for (std::size_t Index = 0; Index < static_cast<std::size_t>(EProperty::Count); ++Index)
			{
				const auto& Definition = GetPropertyDefinition(static_cast<EProperty>(Index));
				const auto Setter = std::find_if(Declarations.begin(), Declarations.end(),
				    [&](const auto& D)
				{
					return D.OwnerId == Type.DeclarationId &&
					    D.Name == "Set" + std::string(Definition.Name) &&
					    D.Parameters.size() == 1;
				});
				if (Setter == Declarations.end())
				{
					continue;
				}
				FGuiPropertySchema Value;
				Value.Identity = "veroscript.gui.builtin." + std::string(Definition.Name);
				Value.Name = Definition.Name;
				Value.Mark = Definition.Mark;
				Value.StorageType = Setter->Parameters.front().CanonicalTypeName.empty()
				    ? TypeName(Setter->Parameters.front().TypeName)
				    : Setter->Parameters.front().CanonicalTypeName;
				Value.ValueType = Value.StorageType;
				Value.bBuiltin = true;
				Value.bStyle = Definition.bStyle;
				Value.Interpolator = Definition.Interpolator;
				Value.bAnimatable = !Value.Interpolator.empty();
				Value.bBindable = std::any_of(
				    Declarations.begin(), Declarations.end(),
				    [&](const auto& D)
				{
					if (D.OwnerId != Type.DeclarationId || D.Name != Setter->Name || D.Parameters.size() != 1)
					{
						return false;
					}
					std::vector<std::string> Arguments;
					return Generic(D.Parameters.front().CanonicalTypeName, "std.attribute", Arguments) &&
					    Arguments.size() == 1 && Arguments.front() == Value.ValueType;
				});
				Value.Location = Setter->Location;
				Type.Properties.push_back(std::move(Value));
			}
		}
		std::vector<unsigned char> Visited(Result->Types.size());
		std::function<bool(std::size_t)> Inherit = [&](std::size_t Index)
		{
			if (Visited[Index] == 1)
			{
				Fail(Diagnostics, Result->Types[Index].Location, "Recursive GUI inheritance");
				return false;
			}
			if (Visited[Index] == 2)
			{
				return true;
			}
			Visited[Index] = 1;
			auto& Type = Result->Types[Index];
			bool bWidgetBase = Type.Name == "VS.Gui.Widget";
			for (std::size_t BaseIndex = 0; BaseIndex < Type.Bases.size(); ++BaseIndex)
			{
				const auto& Base = Type.Bases[BaseIndex];
				const auto Found = Result->TypeNames.find(Base);
				if (Found == Result->TypeNames.end())
				{
					continue;
				}
				if (!Inherit(Found->second))
				{
					return false;
				}
				const auto& Parent = Result->Types[Found->second];
				if (Type.bWidget && Parent.bWidget &&
				    (BaseIndex >= Type.BaseAccess.size() || Type.BaseAccess[BaseIndex] != EMemberAccess::Public))
				{
					Fail(Diagnostics, Type.Location, "GuiWidget inheritance must be public");
				}
				bWidgetBase = bWidgetBase || Parent.bWidget;
				Type.Properties.insert(Type.Properties.begin(), Parent.Properties.begin(), Parent.Properties.end());
				Type.Events.insert(Type.Events.begin(), Parent.Events.begin(), Parent.Events.end());
				Type.Methods.insert(Type.Methods.begin(), Parent.Methods.begin(), Parent.Methods.end());
			}
			if (Type.bWidget && !bWidgetBase)
			{
				Fail(Diagnostics, Type.Location, "GuiWidget must publicly derive from VS::Gui::Widget");
			}
			if (!UniqueNames(Type.Properties) || !UniqueNames(Type.Events))
			{
				Fail(Diagnostics, Type.Location, "Ambiguous original name or GUI Mark in inherited members");
			}
			Visited[Index] = 2;
			return true;
		};
		for (std::size_t Index = 0; Index < Result->Types.size(); ++Index)
		{
			if (!Inherit(Index))
			{
				break;
			}
			auto& Type = Result->Types[Index];
			if (!Type.bRegistered)
			{
				continue;
			}
			std::set<std::string> Names{Leaf(Type.Name), Type.Mark};
			for (const auto& Name : Names)
			{
				if (Name.empty())
				{
					continue;
				}
				if (!MarkName(Name) || Name == "component" || Name == "template" || Name == "import" ||
				    Name == "style" || Name == "script")
				{
					Fail(Diagnostics, Type.Location, "Reserved or invalid GUI widget tag: " + Name);
				}
				Result->Tags[Name].push_back(Index);
			}
		}
		std::map<std::string, std::multimap<std::string, const FAnnotationDeclarationView*>> Members;
		for (const auto& Declaration : Declarations)
		{
			if (!Declaration.bGenerated)
			{
				Members[Declaration.OwnerId].emplace(Declaration.Name, &Declaration);
			}
		}
		for (const auto& Type : Result->Types)
		{
			if (!Type.bWidget || Type.Name == "VS.Gui.Widget")
			{
				continue;
			}
			const auto& Own = Members[Type.DeclarationId];
			const auto Reserve = [&](const std::string& Name)
			{
				const auto Found = Own.find(Name);
				if (Found != Own.end())
				{
					Fail(Diagnostics, Found->second->Location, "Member conflicts with generated GUI API: " + Name);
				}
			};
			if (Type.bRegistered)
			{
				Reserve("Create");
			}
			for (const auto& Property : Type.Properties)
			{
				if (Property.bBuiltin)
				{
					continue;
				}
				for (const auto* Prefix :
				    {"Get", "Set", "Clear", "VsguiRefresh", "VsguiRelease", "VsguiSetStyle", "VsguiClearStyle"})
				{
					Reserve(Prefix + Property.Name);
				}
				if (Property.bBindable)
				{
					Reserve("Is" + Property.Name + "Bound");
				}
				const auto Hooks = Own.equal_range("On" + Property.Name + "Changed");
				for (auto It = Hooks.first; It != Hooks.second; ++It)
				{
					const auto& Hook = *It->second;
					const auto Return = Hook.CanonicalTypeName.empty() ? Hook.TypeName : Hook.CanonicalTypeName;
					if (Hook.Target != EAnnotationTarget::Method || Return != "void" || Hook.bIsStatic ||
					    Hook.bIsConst || Hook.bDeleted || !Hook.Parameters.empty() || GuiHasTemplateParameters(Hook) ||
					    Hooks.first != std::prev(Hooks.second))
					{
						Fail(Diagnostics, Hook.Location,
						    "GUI property change hook must be one void instance method with no parameters: " +
						        Hook.Name);
					}
				}
			}
			for (const auto& Event : Type.Events)
			{
				for (const auto* Prefix : {"Clear", "VsguiGet", "VsguiClear", "VsguiPrune"})
				{
					Reserve(
					    Prefix + Event.Name +
					    (std::string(Prefix) == "VsguiGet" || std::string(Prefix) == "VsguiClear" ? "Revision" : ""));
				}
				if (Event.Kind == EGuiEventKind::Single)
				{
					Reserve("Set" + Event.Name);
				}
				else
				{
					Reserve("Add" + Event.Name);
					Reserve("Remove" + Event.Name);
				}
			}
		}
		return Diagnostics.empty() ? Result : nullptr;
	}

	const std::vector<FGuiTypeSchema>& FGuiSchema::GetTypes() const noexcept
	{
		return Types;
	}

	const FGuiTypeSchema* FGuiSchema::FindType(const std::string& Name) const
	{
		const auto Found = TypeNames.find(TypeName(Name));
		return Found == TypeNames.end() ? nullptr : &Types[Found->second];
	}

	const FGuiTypeSchema* FGuiSchema::FindWidget(const std::string& Tag, std::string& Error) const
	{
		Error.clear();
		const auto Found = Tags.find(Tag);
		if (Found == Tags.end() || Found->second.size() != 1)
		{
			Error = Found == Tags.end() ? "Unknown GUI widget tag: " + Tag : "Ambiguous GUI widget tag: " + Tag;
			return nullptr;
		}
		return &Types[Found->second.front()];
	}

	const FGuiPropertySchema* FGuiSchema::FindProperty(const FGuiTypeSchema& Type, const std::string& Name) const
	{
		const auto Found = std::find_if(Type.Properties.begin(), Type.Properties.end(), [&](const auto& Value)
		{
			return Value.Name == Name || (!Value.Mark.empty() && Value.Mark == Name);
		});
		return Found == Type.Properties.end() ? nullptr : &*Found;
	}

	const FGuiEventSchema* FGuiSchema::FindEvent(const FGuiTypeSchema& Type, const std::string& Name) const
	{
		const auto Found = std::find_if(Type.Events.begin(), Type.Events.end(), [&](const auto& Value)
		{
			return Value.Name == Name || (!Value.Mark.empty() && Value.Mark == Name);
		});
		return Found == Type.Events.end() ? nullptr : &*Found;
	}
	const std::vector<FEventDefinition>& FGuiSchema::GetRoutedEvents() const
	{
		return GetEventDefinitions();
	}

} // namespace VeroScript::Gui

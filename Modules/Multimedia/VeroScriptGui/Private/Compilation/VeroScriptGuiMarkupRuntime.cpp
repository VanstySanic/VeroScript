// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiMarkupInternal.h"
#include "VeroScriptGuiContext.h"
#include <algorithm>
#include <atomic>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace VeroScript::Gui
{
	namespace
	{

		bool Contains(const std::shared_ptr<FWidget>& Ancestor, std::shared_ptr<FWidget> Node)
		{
			for (; Node; Node = Node->GetParent())
			{
				if (Node == Ancestor)
				{
					return true;
				}
			}
			return false;
		}

		bool ExpandVariables(const std::string& Value, const std::map<std::string, std::string>& Variables,
		    std::set<std::string>& Stack, std::string& Result, std::string& Error, unsigned Depth = 0)
		{
			if (Depth > 64)
			{
				Error = "gui.style_variable_depth";
				return false;
			}
			std::size_t Offset = 0;
			while (Offset < Value.size())
			{
				const auto Begin = Value.find("var(", Offset);
				if (Begin == std::string::npos)
				{
					Result += Value.substr(Offset);
					break;
				}
				Result += Value.substr(Offset, Begin - Offset);
				unsigned Level = 1;
				std::size_t End = Begin + 4, Comma = std::string::npos;
				for (; End < Value.size() && Level; ++End)
				{
					if (Value[End] == '(')
					{
						++Level;
					}
					else if (Value[End] == ')')
					{
						--Level;
					}
					else if (Value[End] == ',' && Level == 1 && Comma == std::string::npos)
					{
						Comma = End;
					}
				}
				if (Level)
				{
					Error = "gui.unclosed_style_variable";
					return false;
				}
				const auto Name = TrimMarkup(std::string_view(Value).substr(
				    Begin + 4, (Comma == std::string::npos ? End - 1 : Comma) - Begin - 4));
				if (Name.compare(0, 2, "--") != 0 || !IsMarkupName(std::string_view(Name).substr(2)))
				{
					Error = "gui.invalid_style_variable: " + Name;
					return false;
				}
				const auto Found = Variables.find(Name);
				if (Found == Variables.end())
				{
					if (Comma == std::string::npos)
					{
						Error = "gui.missing_style_variable: " + Name;
						return false;
					}
					if (!ExpandVariables(Value.substr(Comma + 1, End - Comma - 2), Variables, Stack, Result, Error,
					        Depth + 1))
					{
						return false;
					}
				}
				else
				{
					if (!Stack.insert(Name).second)
					{
						Error = "gui.style_variable_cycle: " + Name;
						return false;
					}
					if (!ExpandVariables(Found->second, Variables, Stack, Result, Error, Depth + 1))
					{
						return false;
					}
					Stack.erase(Name);
				}
				if (Result.size() > 16 * 1024 * 1024)
				{
					Error = "gui.style_value_limit";
					return false;
				}
				Offset = End;
			}
			return true;
		}

		FMarkupExpression Expression(const FMarkupAttribute& Attribute)
		{
			FMarkupExpression Result;
			Result.Location = Attribute.ValueLocation;
			Result.Text = TrimMarkup(Attribute.Value);
			Result.bCode = !Result.Text.empty() && Result.Text.front() == '{';
			if (Result.bCode && Result.Text.back() == '}')
			{
				Result.Text = Result.Text.substr(1, Result.Text.size() - 2);
			}
			if (Attribute.Name.compare(0, 3, "on:") == 0)
			{
				FindMarkupEvent(Attribute.Name.substr(3), Result.Event);
			}
			else
			{
				std::string Ignored;
				FindMarkupProperty(Attribute.Name.substr(5), Result.Property, false, Ignored);
			}
			return Result;
		}

		class FBuilder
		{
		public:
			FBuilder(std::shared_ptr<const FMarkupData> InData, const FMarkupScriptFactory& InScripts,
			    FMarkupDiagnostic& InDiagnostic)
			    : Data(std::move(InData)), Scripts(InScripts), Diagnostic(InDiagnostic),
			      Runtime(std::make_shared<FMarkupRuntime>())
			{
			}

			std::shared_ptr<FWidget> Create(std::shared_ptr<IMarkupScriptContext> BindingContext)
			{
				const auto& Unit = Data->Units.at(Data->Entry);
				std::shared_ptr<FWidget> Root;
				if (Unit.bComponent)
				{
					if (BindingContext)
					{
						Fail(Unit.Root.Location, "gui.component_uses_self_context");
						return {};
					}
					Root = Type(Unit.Mark.empty() ? ShortName(Unit.Type) : Unit.Mark, 1);
				}
				else
				{
					if (BindingContext && Unit.bHasScript)
					{
						Fail(Unit.Root.Location, "gui.multiple_binding_contexts");
						return {};
					}
					auto Scope = NewScope(Unit);
					Scope->bPage = true;
					Scope->Script = std::move(BindingContext);
					Root = Node(Unit.Root, Scope, 1, true);
					Scope->Root = Root;
				}
				if (!Root)
				{
					if (Diagnostic.Location.Source.empty())
					{
						Diagnostic.Location = Unit.Root.Location;
					}
					return {};
				}
				for (std::size_t Index = 0; Index < Runtime->Scopes.size(); ++Index)
				{
					auto& Scope = Runtime->Scopes[Index];
					std::vector<FMarkupExpression> Expressions;
					for (const auto& Work : Bindings)
					{
						if (Work.Scope == Scope)
						{
							Expressions.push_back(Work.Expression);
						}
					}
					auto Script = ScopeScripts[Index];
					Script.Root = Scope->Root;
					const std::weak_ptr<FMarkupRuntime> WeakRuntime = Runtime;
					const std::weak_ptr<FMarkupRuntime::FScope> WeakScope = Scope;
					Script.FindElement = [WeakRuntime, WeakScope](const std::string& Id) -> std::shared_ptr<FWidget>
					{
						const auto Owner = WeakRuntime.lock();
						const auto Names = WeakScope.lock();
						if (!Owner || !Names || Owner->Thread != std::this_thread::get_id() || Id.empty())
						{
							return {};
						}
						const auto Found = Names->Names.find(Id);
						const auto Widget = Found == Names->Names.end() ? nullptr : Found->second.lock();
						return Widget && !Widget->IsDisposed() && Widget->GetId() == Id ? Widget : nullptr;
					};
					if (!Scope->Script && (HasScripts[Index] || !Expressions.empty()))
					{
						if (!Scripts)
						{
							Fail(Expressions.empty() ? Script.Location : Expressions.front().Location,
							    "gui.script_adapter_required");
							return {};
						}
						FMarkupDiagnostic ScriptDiagnostic;
						Scope->Script = Scripts(Script, Expressions, ScriptDiagnostic);
						if (!Scope->Script)
						{
							Diagnostic = ScriptDiagnostic.Message.empty()
							    ? FMarkupDiagnostic{Script.Location, "gui.script_creation_failed"}
							    : std::move(ScriptDiagnostic);
							return {};
						}
					}
				}
				for (const auto& Work : Bindings)
				{
					std::string Error;
					if (Work.Expression.Event == Platform::EEventType::None)
					{
						FPropertyGetter Getter;
						if (!Work.Scope->Script->ResolveGetter(Work.Expression, Work.Widget, Getter, Error) || !Getter)
						{
							Fail(Work.Expression.Location, Error.empty() ? "gui.getter_resolution_failed" : Error);
							return {};
						}
						const auto Property = Work.Expression.Property;
						if (Property == EProperty::Image || Property == EProperty::BackgroundImage ||
						    Property == EProperty::MaskImage)
						{
							Getter = [Raw = std::move(Getter), Property, Source = Work.Expression.Location.Source,
							             Resolve = Data->Options.ResolveResource](auto& Out, auto& Failure)
							{
								std::optional<FPropertyValue> Value;
								if (!Raw(Value, Failure))
								{
									return false;
								}
								const auto* Text = Value ? std::get_if<std::string>(&*Value) : nullptr;
								if (!Text || Text->empty())
								{
									Out = std::move(Value);
									return true;
								}
								std::string Identity;
								if (Property == EProperty::Image)
								{
									if (!(Resolve ? Resolve(Source, *Text, Identity, Failure)
									              : ResolveGuiFileReference(Source, *Text, Identity, Failure)))
									{
										return false;
									}
									if (Identity.empty() || Identity.find('\0') != std::string::npos)
									{
										Failure = "gui.invalid_image_identity";
										return false;
									}
								}
								else if (!ResolveGuiStyleImages(Source, *Text, Resolve, Identity, Failure))
								{
									return false;
								}
								Out = std::move(Identity);
								return true;
							};
						}
						if (!Work.Widget->Bind(Property, std::move(Getter), Error))
						{
							Fail(Work.Expression.Location, Error);
							return {};
						}
					}
					else
					{
						std::function<void(FEvent&)> Callback;
						if (!Work.Scope->Script->ResolveEvent(Work.Expression, Work.Widget, Callback, Error) ||
						    !Callback ||
						    !Work.Widget->Subscribe(Work.Expression.Event, false, std::move(Callback), Error)
						        .IsConnected())
						{
							Fail(Work.Expression.Location, Error.empty() ? "gui.event_resolution_failed" : Error);
							return {};
						}
					}
				}
				return Root;
			}

		private:
			struct FBinding
			{
				std::shared_ptr<FMarkupRuntime::FScope> Scope;
				std::shared_ptr<FWidget> Widget;
				FMarkupExpression Expression;
			};

			bool Fail(const FMarkupLocation& Location, std::string Error)
			{
				return MarkupFail(Diagnostic, Location, std::move(Error));
			}

			std::string ShortName(const std::string& Name) const
			{
				const auto Separator = Name.rfind("::");
				return Separator == std::string::npos ? Name : Name.substr(Separator + 2);
			}

			std::shared_ptr<FMarkupRuntime::FScope> NewScope(const FMarkupUnit& Unit)
			{
				auto Scope = std::make_shared<FMarkupRuntime::FScope>();
				Scope->Id = FWidget::AllocateNameScope();
				Scope->Styles = Unit.Styles;
				Runtime->Scopes.push_back(Scope);
				ScopeScripts.push_back(Unit.Script);
				HasScripts.push_back(Unit.bHasScript);
				return Scope;
			}

			std::shared_ptr<FWidget> Type(const std::string& Name, std::size_t Depth)
			{
				if (Depth > Data->Options.MaxDepth || Runtime->Nodes.size() >= Data->Options.MaxNodes)
				{
					Fail({}, "gui.expanded_tree_limit");
					return {};
				}
				if (Data->Options.CheckCancelled)
				{
					Data->Options.CheckCancelled();
				}
				if (const auto Native = Data->Types.find(Name); Native != Data->Types.end())
				{
					auto Widget = Native->second.Create();
					if (!Widget || Widget->IsDisposed() || Widget->IsMounted() || Widget->GetParent() ||
					    !Created.insert(Widget.get()).second)
					{
						Fail({}, "gui.factory_requires_new_unattached_widget: " + Name);
						return {};
					}
					std::string Error;
					if (!Runtime->ValidateFactoryWidget(*Widget, Error))
					{
						Fail({}, Error);
						return {};
					}
					Runtime->Attach(Widget, Native->second.Name, Native->second.Mark, {});
					if (Widget->GetChildCount() != 0)
					{
						auto Scope = NewScope({});
						Scope->Root = Widget;
						Runtime->Attach(Widget, {}, {}, Scope);
						std::vector<std::pair<std::shared_ptr<FWidget>, std::size_t>> Pending{{Widget, Depth}};
						while (!Pending.empty())
						{
							const auto Item = Pending.back();
							Pending.pop_back();
							if (Item.second > Data->Options.MaxDepth || Runtime->Nodes.size() > Data->Options.MaxNodes)
							{
								Fail({}, "gui.expanded_tree_limit");
								return {};
							}
							for (std::int64_t Index = 0; Index < Item.first->GetChildCount(); ++Index)
							{
								if (Item.second >= Data->Options.MaxDepth ||
								    Runtime->Nodes.size() >= Data->Options.MaxNodes)
								{
									Fail({}, "gui.expanded_tree_limit");
									return {};
								}
								const auto Child = Item.first->GetChild(Index, Error);
								if (!Child || Child->IsDisposed() || Child->IsMounted() ||
								    !Created.insert(Child.get()).second)
								{
									Fail({}, "gui.invalid_factory_subtree");
									return {};
								}
								if (!Runtime->ValidateFactoryWidget(*Child, Error))
								{
									Fail({}, Error);
									return {};
								}
								Runtime->Attach(Child, "Widget", {}, Scope);
								if (!Runtime->CheckId(*Child, Child->GetId(), Error))
								{
									Fail({}, Error);
									return {};
								}
								Pending.emplace_back(Child, Item.second + 1);
							}
						}
					}
					if (!Native->second.DefaultId.empty() && !Widget->SetId(Native->second.DefaultId, Error))
					{
						Fail({}, Error);
						return {};
					}
					return Widget;
				}
				const auto Component = Data->Components.find(Name);
				if (Component == Data->Components.end())
				{
					Fail({}, "gui.unknown_widget: " + Name);
					return {};
				}
				const auto& Unit = Data->Units.at(Component->second);
				if (!Creating.insert(Unit.Source.Identity).second)
				{
					Fail(Unit.Root.Location, "gui.recursive_component_template");
					return {};
				}
				auto Root = Type(Unit.Base, Depth + 1);
				if (!Root)
				{
					return {};
				}
				auto Scope = NewScope(Unit);
				Scope->Root = Root;
				Runtime->Attach(Root, ShortName(Unit.Type), Unit.Mark, Scope);
				std::string Error;
				if (!Root->SetId(Unit.DefaultId, Error))
				{
					Fail(Unit.Root.Location, Error);
					return {};
				}
				for (const auto& Child : Unit.Root.Children)
				{
					if (Child.Name != "template")
					{
						continue;
					}
					for (const auto& Entry : Child.Children)
					{
						auto Widget = Node(Entry, Scope, Depth + 1, false);
						if (!Widget || !Root->AddChild(Widget, Error))
						{
							if (Widget)
							{
								Fail(Entry.Location, Error);
							}
							return {};
						}
					}
				}
				Creating.erase(Unit.Source.Identity);
				return Root;
			}

			std::shared_ptr<FWidget> Node(const FMarkupElement& Element,
			    const std::shared_ptr<FMarkupRuntime::FScope>& Scope, std::size_t Depth,
			    bool bRoot)
			{
				auto Widget = Type(Element.Name, Depth);
				if (!Widget)
				{
					if (Diagnostic.Location.Source.empty())
					{
						Diagnostic.Location = Element.Location;
					}
					return {};
				}
				Runtime->Attach(Widget, {}, {}, Scope);
				std::string Error;
				for (const auto& Attribute : Element.Attributes)
				{
					bool bSuccess = true;
					if (Attribute.Name == "id")
					{
						bSuccess = Widget->SetId(Attribute.Value, Error);
					}
					else if (Attribute.Name == "class")
					{
						std::istringstream Stream(Attribute.Value);
						std::vector<std::string> Classes;
						std::string Class;
						while (Stream >> Class)
						{
							Classes.push_back(Class);
						}
						bSuccess = Widget->SetClasses(std::move(Classes), Error);
					}
					else if (Attribute.Name.find(':') != std::string::npos)
					{
						Bindings.push_back({Scope, Widget, Expression(Attribute)});
					}
					else
					{
						EProperty Property;
						FPropertyValue Parsed;
						bSuccess = ParseMarkupProperty(Attribute.Name, Attribute.Value, Property, Parsed, false, Error);
						if (bSuccess && Property == EProperty::Image && !std::get<std::string>(Parsed).empty())
						{
							std::string Identity;
							const auto& Resolve = Data->Options.ResolveResource;
							bSuccess =
							    Resolve
							    ? Resolve(Attribute.Location.Source, std::get<std::string>(Parsed), Identity, Error)
							    : ResolveGuiFileReference(Attribute.Location.Source, std::get<std::string>(Parsed),
							          Identity, Error);
							Parsed = std::move(Identity);
						}
						if (bSuccess && (Property == EProperty::BackgroundImage || Property == EProperty::MaskImage))
						{
							std::string Value;
							bSuccess = ResolveGuiStyleImages(Attribute.Location.Source, std::get<std::string>(Parsed),
							    Data->Options.ResolveResource, Value, Error);
							Parsed = std::move(Value);
						}
						bSuccess = bSuccess && Widget->Set(Property, std::move(Parsed), Error);
					}
					if (!bSuccess)
					{
						Fail(Attribute.Location, Error);
						return {};
					}
				}
				if (!Runtime->CheckId(*Widget, Widget->GetId(), Error))
				{
					Fail(Element.Location, Error);
					return {};
				}
				for (const auto& Child : Element.Children)
				{
					if (bRoot && (Child.Name == "import" || Child.Name == "style" || Child.Name == "script"))
					{
						continue;
					}
					auto Added = Node(Child, Scope, Depth + 1, false);
					if (!Added || !Widget->AddChild(Added, Error))
					{
						if (Added)
						{
							Fail(Child.Location, Error);
						}
						return {};
					}
				}
				return Widget;
			}

			std::shared_ptr<const FMarkupData> Data;
			const FMarkupScriptFactory& Scripts;
			FMarkupDiagnostic& Diagnostic;
			std::shared_ptr<FMarkupRuntime> Runtime;
			std::vector<FBinding> Bindings;
			std::vector<FMarkupScript> ScopeScripts;
			std::vector<bool> HasScripts;
			std::set<std::string> Creating;
			std::set<FWidget*> Created;
		};
	} // namespace

	bool FMarkupRuntime::Apply(FContext& Context, std::uint64_t Round, std::string& Error)
	{
		if (LastContext.lock().get() == &Context && LastRound == Round)
		{
			return true;
		}
		// A scene with no rules and no previous compiled values has nothing to clear.
		// Keep the full path when removing an active theme or stylesheet.
		if (bCompiledStyle && !Theme && !Context.Theme &&
		    std::all_of(Scopes.begin(), Scopes.end(),
		        [](const auto& Scope)
		{
			return std::all_of(Scope->Styles.begin(), Scope->Styles.end(),
			    [](const auto& Sheet)
			{
				return Sheet->Rules.empty();
			});
		}))
		{
			const bool HasValues = !bEmptyStylesCleared && std::any_of(Nodes.begin(), Nodes.end(), [](const auto& Node)
			{
				const auto Widget = Node.Widget.lock();
				return Widget && (!Widget->StyleValues.empty() || std::any_of(Widget->Properties.begin(), Widget->Properties.end(), [](const auto& Cell)
				{
					return Cell.CompiledStyle.has_value();
				}));
			});
			if (!HasValues)
			{
				bEmptyStylesCleared = true;
				LastContext = Context.shared_from_this();
				LastRound = Round;
				return true;
			}
		}
		using FPriority = std::tuple<std::size_t, std::array<std::size_t, 3>, std::size_t>;
		const auto ActiveTheme = Theme ? Theme : (Context.Theme ? Context.Theme->Data : nullptr);
		std::unordered_map<FWidget*, std::size_t> Indices;
		for (std::size_t Index = 0; Index < Nodes.size(); ++Index)
		{
			if (const auto Widget = Nodes[Index].Widget.lock())
			{
				Indices.emplace(Widget.get(), Index);
			}
		}
		// Only selector inputs invalidate winners. Ordinary geometry, text and
		// animation changes must not rematch a node or its entire subtree.
		constexpr EProperty EligibilityProperties[]{EProperty::Enabled, EProperty::Visibility,
		    EProperty::HitTest, EProperty::Focusable};
		std::vector<std::uint64_t> Input;
		const auto Identity = [](const auto& Value)
		{
			return Value ? Value->GetIdentity() : 0;
		};
		const auto Focus = Context.Focus.lock();
		Input.push_back(Identity(Focus));
		Input.push_back(0);
		bool Cacheable = bCompiledStyle;
		// Effective focus also depends on the focused descendant's eligibility.
		// A descendant change can invalidate :focus-within on its ancestors.
		for (auto Node = Focus; Node; Node = Node->Parent.lock())
		{
			++Input[1];
			for (const auto Property : EligibilityProperties)
			{
				const auto Revision = Node->Properties[static_cast<std::size_t>(Property)].Revision;
				Input.push_back(Revision);
				if (!Revision)
				{
					Cacheable = false;
				}
			}
			Input.push_back(static_cast<std::uint64_t>(Node->Control.Kind));
			Input.push_back(Node->Control.bOpen);
			Input.push_back(Node->bContentVisible);
			Input.push_back(Node->bDisposed || Node->bDisposing);
		}
		Input.push_back(Identity(Context.ActiveControl.lock()));
		Input.push_back(Identity(Context.GetModalRoot()));
		Input.push_back(Context.Hovered.size());
		for (const auto& Item : Context.Hovered)
		{
			Input.push_back(Identity(Item.second.lock()));
		}
		Input.push_back(Context.Pressed.size());
		for (const auto& Item : Context.Pressed)
		{
			Input.push_back(Identity(Item.second.lock()));
		}
		for (const auto& Node : Nodes)
		{
			const auto Widget = Node.Widget.lock();
			// Arbitrary getters may change eligibility without a setter/version.
			// Do not sample them early or freeze their normal matching order.
			if (Widget && (Widget->Properties[static_cast<std::size_t>(EProperty::Enabled)].Getter || Widget->Properties[static_cast<std::size_t>(EProperty::Visibility)].Getter || Widget->Properties[static_cast<std::size_t>(EProperty::HitTest)].Getter || Widget->Properties[static_cast<std::size_t>(EProperty::Focusable)].Getter))
			{
				Cacheable = false;
			}
		}
		const bool SameInput = Cacheable && bSelectorCache && SelectorInput == Input &&
		    SelectorSources.size() == Nodes.size() && SelectorWinners.size() == Nodes.size();
		// A failed match must not leave a partially replaced winner map reusable.
		bSelectorCache = false;
		std::vector<bool> Changed(Nodes.size(), !SameInput);
		std::vector<FSelectorSource> Sources(Nodes.size());
		if (SelectorWinners.size() != Nodes.size())
		{
			SelectorWinners.resize(Nodes.size());
		}
		for (std::size_t Index = 0; Index < Nodes.size(); ++Index)
		{
			const auto Widget = Nodes[Index].Widget.lock();
			if (!Widget || Widget->IsDisposed() || Widget->Context.lock().get() != &Context)
			{
				Changed[Index] = true;
				continue;
			}
			auto& Source = Sources[Index];
			Source.Id = Widget->Id;
			Source.Classes = Widget->Classes;
			for (std::size_t Property = 0; Property < Source.Eligibility.size(); ++Property)
			{
				const auto PropertyIndex = static_cast<std::size_t>(EligibilityProperties[Property]);
				Source.Eligibility[Property] = Widget->Properties[PropertyIndex].Revision;
			}
			Source.StyleMutation = Widget->StyleMutationToken;
			Source.Kind = Widget->Control.Kind;
			Source.bChecked = Widget->Control.bChecked;
			Source.bReadOnly = Widget->Control.bReadOnly;
			Source.bOpen = Widget->Control.bOpen;
			Source.bContentVisible = Widget->bContentVisible;
			if (SameInput)
			{
				const auto& Before = SelectorSources[Index];
				Changed[Index] = !Source.StyleMutation ||
				    std::find(Source.Eligibility.begin(), Source.Eligibility.end(), 0) != Source.Eligibility.end() ||
				    std::tie(Source.Id, Source.Classes, Source.Eligibility, Source.StyleMutation, Source.Kind, Source.bChecked,
				        Source.bReadOnly, Source.bOpen, Source.bContentVisible) !=
				        std::tie(Before.Id, Before.Classes, Before.Eligibility, Before.StyleMutation, Before.Kind, Before.bChecked,
				            Before.bReadOnly, Before.bOpen, Before.bContentVisible);
			}
			if (const auto Parent = Widget->Parent.lock())
			{
				const auto Found = Indices.find(Parent.get());
				if (Found != Indices.end() && Found->second < Index && Changed[Found->second])
				{
					Changed[Index] = true;
				}
			}
		}
		auto& Winners = SelectorWinners;
		for (std::size_t Index = 0; Index < Nodes.size(); ++Index)
		{
			if (!Changed[Index])
			{
				continue;
			}
			Winners[Index].clear();
			const auto& Node = Nodes[Index];
			const auto Widget = Node.Widget.lock();
			if (!Widget || Widget->IsDisposed() || Widget->Context.lock().get() != &Context)
			{
				continue;
			}
			std::size_t Rank = 0, Order = 0;
			for (const auto& Scope : Node.Scopes)
			{
				std::vector<std::shared_ptr<const FStyleSheetData>> Sheets;
				if (ActiveTheme)
				{
					Sheets.push_back(ActiveTheme);
				}
				Sheets.insert(Sheets.end(), Scope->Styles.begin(), Scope->Styles.end());
				for (const auto& Sheet : Sheets)
				{
					const auto Layer = Sheet == ActiveTheme ? 0 : Rank + 1;
					for (const auto& Rule : Sheet->Rules)
					{
						std::optional<std::array<std::size_t, 3>> Specificity;
						for (const auto& Selector : Rule.Selectors)
						{
							if (Match(Selector, Node, *Scope, Context) &&
							    (!Specificity || Selector.Specificity > *Specificity))
							{
								Specificity = Selector.Specificity;
							}
						}
						for (const auto& Declaration : Rule.Declarations)
						{
							const FPriority Priority{Layer, Specificity.value_or(std::array<std::size_t, 3>{}),
							    Order++};
							if (!Specificity)
							{
								continue;
							}
							std::string Key = Declaration.Name;
							if (Key.compare(0, 2, "--") != 0)
							{
								EProperty Property;
								if (Declaration.Literal)
								{
									Key = GetPropertyDefinition(Declaration.Literal->first).Name;
								}
								else if (!FindMarkupProperty(Key, Property, true, Error))
								{
									const auto Custom = std::find_if(
									    Widget->StyleProfile.begin(), Widget->StyleProfile.end(), [&](const auto& Entry)
									{
										return Entry.first == Key || Entry.second.Mark == Key;
									});
									if (Custom == Widget->StyleProfile.end())
									{
										return false;
									}
									Error.clear();
									Key = Custom->first;
								}
								else
								{
									Key = GetPropertyDefinition(Property).Name;
								}
							}
							const auto Found = Winners[Index].find(Key);
							if (Found == Winners[Index].end() || Found->second.Priority <= Priority)
							{
								Winners[Index][Key] = {Priority, &Declaration};
							}
						}
					}
				}
				++Rank;
			}
		}
		// These are the inputs before applying outputs. Compiled styles can alter
		// eligibility; their setters must invalidate the following stabilization
		// round rather than being committed as if they had already been matched.
		SelectorSources = std::move(Sources);
		SelectorInput = std::move(Input);
		bSelectorCache = Cacheable;
		std::vector<std::map<std::string, std::string>> Variables(Nodes.size());
		std::set<std::size_t> Resolved;
		const auto ResolveVariables = [&](auto&& Self, std::size_t Index) -> bool
		{
			if (!Resolved.insert(Index).second)
			{
				return true;
			}
			const auto Widget = Nodes[Index].Widget.lock();
			const auto Parent = Widget ? Widget->GetParent() : nullptr;
			if (Parent)
			{
				const auto Found = Indices.find(Parent.get());
				if (Found != Indices.end())
				{
					if (!Self(Self, Found->second))
					{
						return false;
					}
					Variables[Index] = Variables[Found->second];
				}
			}
			for (const auto& Winner : Winners[Index])
			{
				if (Winner.first.compare(0, 2, "--") == 0)
				{
					const auto& Declaration = *Winner.second.Declaration;
					std::string References;
					if (!ResolveGuiStyleImages(Declaration.Location.Source, Declaration.Value,
					        Declaration.ResolveResource, References, Error))
					{
						Error = FMarkupDiagnostic{Declaration.Location, Error}.Format();
						return false;
					}
					Variables[Index][Winner.first] = std::move(References);
				}
			}
			for (const auto& Winner : Winners[Index])
			{
				if (Winner.first.compare(0, 2, "--") != 0)
				{
					continue;
				}
				std::set<std::string> Stack{Winner.first};
				std::string Expanded;
				if (!ExpandVariables(Variables[Index].at(Winner.first), Variables[Index], Stack, Expanded, Error))
				{
					Error = FMarkupDiagnostic{Winner.second.Declaration->Location, Error}.Format();
					return false;
				}
				Variables[Index][Winner.first] = std::move(Expanded);
			}
			return true;
		};
		using FValues = std::array<std::optional<FPropertyValue>, static_cast<std::size_t>(EProperty::Count)>;
		std::vector<FValues> Values(Nodes.size());
		std::vector<std::map<std::string, FStyleValue>> CustomValues(Nodes.size());
		for (std::size_t Index = 0; Index < Nodes.size(); ++Index)
		{
			if (!ResolveVariables(ResolveVariables, Index))
			{
				return false;
			}
			for (const auto& Winner : Winners[Index])
			{
				if (Winner.first.compare(0, 2, "--") == 0)
				{
					continue;
				}
				const auto& Declaration = *Winner.second.Declaration;
				const auto Widget = Nodes[Index].Widget.lock();
				const auto Custom = Widget ? Widget->StyleProfile.find(Winner.first)
				                           : std::map<std::string, FStyleProperty>::const_iterator{};
				if (Declaration.Literal && Widget && Custom == Widget->StyleProfile.end())
				{
					Values[Index][static_cast<std::size_t>(Declaration.Literal->first)] = Declaration.Literal->second;
					continue;
				}
				std::string Expanded;
				std::set<std::string> Stack;
				EProperty Property;
				FPropertyValue Parsed;
				if (!ExpandVariables(Declaration.Value, Variables[Index], Stack, Expanded, Error))
				{
					Error = FMarkupDiagnostic{Declaration.Location, Error}.Format();
					return false;
				}
				if (Declaration.Name == "background-image" || Declaration.Name == "BackgroundImage" ||
				    Declaration.Name == "mask-image" || Declaration.Name == "MaskImage")
				{
					std::string ImageReferences;
					if (!ResolveGuiStyleImages(Declaration.Location.Source, Expanded, Declaration.ResolveResource,
					        ImageReferences, Error))
					{
						Error = FMarkupDiagnostic{Declaration.Location, Error}.Format();
						return false;
					}
					Expanded = std::move(ImageReferences);
				}
				if (Widget && Custom != Widget->StyleProfile.end())
				{
					FStyleValue Value;
					if (!Custom->second.Decode(Expanded, Value, Error))
					{
						Error = FMarkupDiagnostic{Declaration.Location, Error}.Format();
						return false;
					}
					CustomValues[Index].emplace(Winner.first, std::move(Value));
				}
				else
				{
					if (!ParseMarkupProperty(Declaration.Name, Expanded, Property, Parsed, true, Error))
					{
						Error = FMarkupDiagnostic{Declaration.Location, Error}.Format();
						return false;
					}
					Values[Index][static_cast<std::size_t>(Property)] = std::move(Parsed);
				}
			}
		}
		// Resolve and validate every candidate before changing any live style slot.
		for (std::size_t Index = 0; Index < Nodes.size(); ++Index)
		{
			const auto Widget = Nodes[Index].Widget.lock();
			if (!Widget || Widget->IsDisposed() || Widget->Context.lock().get() != &Context)
			{
				continue;
			}
			for (std::size_t Property = 0; Property < Values[Index].size(); ++Property)
			{
				if (GetPropertyDefinition(static_cast<EProperty>(Property)).bStyle &&
				    !(bCompiledStyle
				            ? Widget->SetCompiledStyle(static_cast<EProperty>(Property), Values[Index][Property], Error)
				            : Widget->SetStyle(static_cast<EProperty>(Property), Values[Index][Property], Error)))
				{
					return false;
				}
			}
			if (bCompiledStyle)
			{
				const auto& Current = Widget->StyleValues;
				const auto& Next = CustomValues[Index];
				const bool Same =
				    Current.size() == Next.size() &&
				    std::equal(Current.begin(), Current.end(), Next.begin(),
				        [](const auto& A, const auto& B)
				{
					if (A.first != B.first || A.second.index() != B.second.index())
					{
						return false;
					}
					switch (A.second.index())
					{
					case 0:
						return PropertyValuesEqual(std::get<0>(A.second), std::get<0>(B.second));
					case 1:
						return std::get<1>(A.second) == std::get<1>(B.second);
					case 2:
						return std::get<2>(A.second) == std::get<2>(B.second);
					case 3:
						return std::get<3>(A.second) == std::get<3>(B.second);
					default:
						return false;
					}
				});
				if (!Same)
				{
					Widget->StyleValues = std::move(CustomValues[Index]);
					if (Widget->StyleMutationToken)
					{
						++Widget->StyleMutationToken;
					}
				}
			}
		}
		LastContext = Context.shared_from_this();
		LastRound = Round;
		return true;
	}

	std::shared_ptr<FMarkupRuntime> FMarkupRuntime::BuildScene(const std::vector<std::shared_ptr<FWidget>>& Widgets,
	    std::shared_ptr<const FStyleSheet> Theme,
	    std::shared_ptr<FMarkupRuntime> Previous)
	{
		if (Previous && Previous->MatchesScene(Widgets, Theme)) return Previous;
		auto Result = std::make_shared<FMarkupRuntime>();
		Result->bCompiledStyle = true;
		Result->Theme = Theme ? Theme->Data : nullptr;
		std::map<std::uint64_t, std::shared_ptr<FScope>> ById;
		for (const auto& Widget : Widgets)
		{
			for (const auto& Definition : Widget->StyleSheets)
			{
				auto Scope = std::make_shared<FScope>();
				Scope->Id = Definition.first;
				Scope->Root = Widget;
				for (const auto& Sheet : Definition.second)
				{
					Scope->Styles.push_back(Sheet->Data);
				}
				ById.emplace(Scope->Id, Scope);
				Result->Scopes.push_back(std::move(Scope));
			}
		}
		if (Theme)
		{
			// A theme also reaches declarations without a local stylesheet. Parent-first
			// input selects the outermost visible node as each scope's root.
			for (const auto& Widget : Widgets)
			{
				if (Widget->Markup)
				{
					continue;
				}
				const auto AddScope = [&](std::uint64_t Id)
				{
					if (ById.count(Id))
					{
						return;
					}
					auto Scope = std::make_shared<FScope>();
					Scope->Id = Id;
					Scope->Root = Widget;
					ById.emplace(Id, Scope);
					Result->Scopes.push_back(std::move(Scope));
				};
				AddScope(Widget->NameScope);
				for (const auto& Membership : Widget->StyleMemberships)
				{
					AddScope(Membership.Scope);
				}
			}
		}
		for (const auto& Widget : Widgets)
		{
			FSceneSource Source;
			Source.Widget = Widget;
			Source.Parent = Widget->Parent;
			Source.Markup = Widget->Markup;
			Source.NameScope = Widget->NameScope;
			Source.Sheets = Widget->StyleSheets;
			for (const auto& Membership : Widget->StyleMemberships)
			{
				Source.Memberships.emplace_back(Membership.Scope, Membership.Type, Membership.Mark);
			}
			Result->SceneSources.push_back(std::move(Source));
			FNode Node;
			Node.Widget = Widget;
			Node.Type = "Widget";
			if (Widget->Markup)
			{
				Result->NodeIndices.emplace(Widget.get(), Result->Nodes.size());
				Result->Nodes.push_back(std::move(Node));
				continue;
			}
			for (const auto& Membership : Widget->StyleMemberships)
			{
				const auto Found = ById.find(Membership.Scope);
				if (Found != ById.end())
				{
					Node.Scopes.push_back(Found->second);
					Node.ScopeTypes.emplace(Membership.Scope, std::make_pair(Membership.Type, Membership.Mark));
				}
			}
			if (Node.Scopes.empty() && (Widget->NameScope || Theme))
			{
				const auto Found = ById.find(Widget->NameScope);
				if (Found != ById.end())
				{
					Node.Scopes.push_back(Found->second);
				}
			}
			if (Node.Scopes.empty() && !Widget->NameScope)
			{
				const auto Parent = Widget->Parent.lock();
				const auto Found = Parent ? Result->NodeIndices.find(Parent.get()) : Result->NodeIndices.end();
				if (Found != Result->NodeIndices.end())
				{
					Node.Scopes = Result->Nodes[Found->second].Scopes;
				}
			}
			Result->NodeIndices.emplace(Widget.get(), Result->Nodes.size());
			Result->Nodes.push_back(std::move(Node));
		}
		return Result;
	}

	bool FMarkupRuntime::SynchronizeScripts(FContext& Context, std::string& Error)
	{
		if (LastScriptContext.lock().get() != &Context || LastScriptFrame != Context.Frame)
		{
			for (const auto& Scope : Scopes)
			{
				const auto Root = Scope->Root.lock();
				if (Root && Root->Context.lock().get() == &Context && Scope->Script &&
				    !Scope->Script->Synchronize(Error))
				{
					return false;
				}
			}
			LastScriptContext = Context.weak_from_this();
			LastScriptFrame = Context.Frame;
		}
		return true;
	}

	bool FMarkupRuntime::ValidateFactoryWidget(const FWidget& Widget, std::string& Error) const
	{
		if (!Widget.Check(Error, true))
		{
			return false;
		}
		if (Widget.Markup || Widget.bInitialized)
		{
			Error = "gui.factory_requires_new_unattached_widget";
			return false;
		}
		return true;
	}

	void FMarkupRuntime::Attach(const std::shared_ptr<FWidget>& Widget, const std::string& Type,
	    const std::string& Mark, const std::shared_ptr<FScope>& Scope)
	{
		auto Index = NodeIndices.find(Widget.get());
		if (Index == NodeIndices.end())
		{
			Index = NodeIndices.emplace(Widget.get(), Nodes.size()).first;
			Nodes.push_back({Widget, Type, Mark, {}, {}});
		}
		auto* Found = &Nodes[Index->second];
		if (!Type.empty())
		{
			Found->Type = Type;
			Found->Mark = Mark;
		}
		if (Scope)
		{
			Found->Scopes.push_back(Scope);
			Scope->Nodes.push_back(Index->second);
			if (!Widget->GetId().empty())
			{
				Scope->Names.emplace(Widget->GetId(), Widget);
			}
			Widget->NameScope = Scope->Id;
			Widget->InvalidateNames();
		}
		Widget->Markup = shared_from_this();
	}

	bool FMarkupRuntime::CheckId(const FWidget& Widget, const std::string& Id, std::string& Error) const
	{
		if (Id.empty())
		{
			return true;
		}
		const auto Found = NodeIndices.find(&Widget);
		if (Found == NodeIndices.end())
		{
			return true;
		}
		for (const auto& Scope : Nodes[Found->second].Scopes)
		{
			const auto Entry = Scope->Names.find(Id);
			if (Entry != Scope->Names.end())
			{
				const auto Other = Entry->second.lock();
				if (Other && Other.get() != &Widget && !Other->IsDisposed() && Other->GetId() == Id)
				{
					Error = "gui.duplicate_scope_id: " + Id;
					return false;
				}
			}
		}
		return true;
	}

	void FMarkupRuntime::IdChanged(const std::shared_ptr<FWidget>& Widget, const std::string& Previous)
	{
		const auto Found = NodeIndices.find(Widget.get());
		if (Found == NodeIndices.end())
		{
			return;
		}
		for (const auto& Scope : Nodes[Found->second].Scopes)
		{
			const auto Old = Scope->Names.find(Previous);
			if (Old != Scope->Names.end() && Old->second.lock() == Widget)
			{
				Scope->Names.erase(Old);
			}
			if (!Widget->GetId().empty())
			{
				Scope->Names[Widget->GetId()] = Widget;
			}
		}
	}

	std::shared_ptr<FWidget> FMarkupRuntime::Find(const std::shared_ptr<FWidget>& Root, const std::string& Id) const
	{
		if (Thread != std::this_thread::get_id() || Id.empty())
		{
			return {};
		}
		std::shared_ptr<FScope> Selected;
		for (const auto& Scope : Scopes)
		{
			if (Scope->Root.lock() != Root)
			{
				continue;
			}
			Selected = Scope;
			if (Scope->bPage)
			{
				break;
			}
		}
		if (Selected)
		{
			const auto Found = Selected->Names.find(Id);
			const auto Widget = Found == Selected->Names.end() ? nullptr : Found->second.lock();
			if (Widget && !Widget->IsDisposed() && Widget->GetId() == Id)
			{
				return Widget;
			}
		}
		return {};
	}

	bool FMarkupRuntime::Match(const FStyleSelector& Selector, const FNode& Node, const FScope& Scope,
	    FContext& Context) const
	{
		const auto Count = Selector.Parts.size();
		if (!Count || !MatchPart(Selector.Parts.back(), Node, Scope, Context))
		{
			return false;
		}
		if (Count == 1)
		{
			return true;
		}
		// Track all viable prefixes together. Recursive ancestor backtracking can be exponential.
		std::vector<bool> Pending(Count - 1), Next(Count - 1);
		Pending.back() = true;
		const auto Widget = Node.Widget.lock();
		for (auto Parent = Widget ? Widget->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
		{
			const auto Found = NodeIndices.find(Parent.get());
			if (Found == NodeIndices.end())
			{
				return false;
			}
			const auto& ParentNode = Nodes[Found->second];
			if (std::none_of(ParentNode.Scopes.begin(), ParentNode.Scopes.end(),
			        [&](const auto& Item)
			{
				return Item.get() == &Scope;
			}))
			{
				return false;
			}
			std::fill(Next.begin(), Next.end(), false);
			bool bContinue = false;
			for (std::size_t Part = 0; Part + 1 < Count; ++Part)
			{
				if (!Pending[Part])
				{
					continue;
				}
				if (Selector.Parts[Part + 1].Relation != '>')
				{
					Next[Part] = bContinue = true;
				}
				if (MatchPart(Selector.Parts[Part], ParentNode, Scope, Context))
				{
					if (!Part)
					{
						return true;
					}
					Next[Part - 1] = bContinue = true;
				}
			}
			if (!bContinue)
			{
				return false;
			}
			Pending.swap(Next);
		}
		return false;
	}

	bool FMarkupRuntime::MatchPart(const FSelectorPart& Part, const FNode& Node, const FScope& Scope,
	    FContext& Context) const
	{
		const auto Widget = Node.Widget.lock();
		const auto Tags = Node.ScopeTypes.find(Scope.Id);
		const auto& Type = Tags == Node.ScopeTypes.end() ? Node.Type : Tags->second.first;
		const auto& Mark = Tags == Node.ScopeTypes.end() ? Node.Mark : Tags->second.second;
		if (!Widget || Widget->IsDisposed() || (!Part.Type.empty() && Part.Type != Type && Part.Type != Mark) ||
		    (!Part.Id.empty() && Part.Id != Widget->GetId()))
		{
			return false;
		}
		for (const auto& Class : Part.Classes)
		{
			if (std::find(Widget->Classes.begin(), Widget->Classes.end(), Class) == Widget->Classes.end())
			{
				return false;
			}
		}
		for (const auto State : Part.States)
		{
			bool bMatches = false;
			switch (State)
			{
			case ESelectorState::Root:
				{
					bMatches = Scope.Root.lock() == Widget;
					break;
				}
			case ESelectorState::Hover:
				{
					bMatches = Context.Eligible(Widget, true, false) &&
					    std::any_of(Context.Hovered.begin(), Context.Hovered.end(),
					        [&](const auto& Item)
					{
						return Contains(Widget, Item.second.lock());
					});
					break;
				}
			case ESelectorState::Active:
				{
					bMatches = Context.Eligible(Widget, true, false) &&
					    (Contains(Widget, Context.ActiveControl.lock()) ||
					        std::any_of(Context.Pressed.begin(), Context.Pressed.end(),
					            [&](const auto& Item)
					{
						return Contains(Widget, Item.second.lock());
					}));
					break;
				}
			case ESelectorState::Checked:
				{
					bMatches = Widget->Control.bChecked;
					break;
				}
			case ESelectorState::ReadOnly:
				{
					bMatches = Widget->Control.bReadOnly;
					break;
				}
			case ESelectorState::Focus:
				{
					bMatches = Context.GetFocus() == Widget;
					break;
				}
			case ESelectorState::FocusWithin:
				{
					bMatches = Contains(Widget, Context.GetFocus());
					break;
				}
			case ESelectorState::Disabled:
				{
					for (auto Current = Widget; Current; Current = Current->GetParent())
					{
						FPropertyValue Value;
						std::string Ignored;
						if (Current->Get(EProperty::Enabled, Value, Ignored) && !std::get<bool>(Value))
						{
							bMatches = true;
						}
					}
					break;
				}
			default:
				{
					break;
				}
			}
			if (!bMatches)
			{
				return false;
			}
		}
		return true;
	}

	bool FMarkupRuntime::MatchesScene(const std::vector<std::shared_ptr<FWidget>>& Widgets,
	    const std::shared_ptr<const FStyleSheet>& ActiveTheme) const
	{
		if (!bCompiledStyle || Theme != (ActiveTheme ? ActiveTheme->Data : nullptr) ||
		    SceneSources.size() != Widgets.size())
		{
			return false;
		}
		for (std::size_t Index = 0; Index < Widgets.size(); ++Index)
		{
			const auto& Widget = Widgets[Index];
			const auto& Previous = SceneSources[Index];
			if (Previous.Widget.lock() != Widget || Previous.Parent.lock() != Widget->Parent.lock() ||
			    Previous.Markup.lock() != Widget->Markup || Previous.NameScope != Widget->NameScope ||
			    Previous.Memberships.size() != Widget->StyleMemberships.size() || Previous.Sheets != Widget->StyleSheets)
			{
				return false;
			}
			for (std::size_t Item = 0; Item < Previous.Memberships.size(); ++Item)
			{
				const auto& Membership = Widget->StyleMemberships[Item];
				if (Previous.Memberships[Item] != std::tie(Membership.Scope, Membership.Type, Membership.Mark))
				{
					return false;
				}
			}
		}
		return true;
	}

	std::shared_ptr<FWidget> CreateMarkup(const std::shared_ptr<const FMarkupData>& Data,
	    const FMarkupScriptFactory& Scripts,
	    std::shared_ptr<IMarkupScriptContext> BindingContext,
	    FMarkupDiagnostic& Diagnostic)
	{
		return FBuilder(Data, Scripts, Diagnostic).Create(std::move(BindingContext));
	}
} // namespace VeroScript::Gui

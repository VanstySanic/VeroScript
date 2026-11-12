// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiScript.h"
#include "Compilation/VeroScriptGuiMarkupInternal.h"
#include "Compilation/VeroScriptGuiScriptExpression.h"
#include "Compilation/VeroScriptGuiSourceImports.h"
#include "VeroScriptFontBindings.h"
#include "VeroScriptGuiCompilation.h"
#include "VeroScriptGuiLifecycleBindings.h"
#include "VeroScriptGuiPaintBindings.h"
#include "VeroScriptGuiResourceBindings.h"
#include "VeroScriptGuiScriptCatalog.h"
#include "VeroScriptGuiScriptInternal.h"
#include "VeroScriptGuiScriptLibrary.h"
#include "VeroScriptGuiStyleBindings.h"
#include "VeroScriptImageBindings.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptSession.h"
#include <array>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace VeroScript::Gui
{
	namespace ScriptBindings
	{
		bool Invoke(const FNativeCallContext &Context, FValue &Result, std::string &Error)
		{
			try
			{
				return (*static_cast<const FCall *>(Context.UserData))(Context, Result, Error);
			}
			catch (const std::exception &Exception)
			{
				Error = std::string("gui.script_native_exception: ") + Exception.what();
				return false;
			}
		}

		bool Member(FHostBindingRegistry &Session, FHostTypeId Type, const std::string &Name, EHostMemberKind Kind,
		    FNativeTypeDesc Return, std::vector<FNativeTypeDesc> Parameters, FCall Call, std::string &Error,
		    bool bReadOnly)
		{
			auto Owner = std::make_shared<FCall>(
			    [Call = std::move(Call), Thread = std::this_thread::get_id()](const auto &Context, auto &Out,
			        auto &Error)
			{
				if (Thread != std::this_thread::get_id())
				{
					Error = "gui.wrong_thread";
					return false;
				}
				return Call(Context, Out, Error);
			});
			FHostMemberDesc Desc;
			Desc.Name = Name;
			Desc.Kind = Kind;
			Desc.bConstMethod =
			    bReadOnly || Kind == EHostMemberKind::PropertyGetter ||
			    (Kind == EHostMemberKind::Method && (Name.compare(0, 3, "Get") == 0 || Name.compare(0, 2, "Is") == 0));
			Desc.ReturnType = Return.Type;
			Desc.ReturnHostTypeName = Return.HostTypeName;
			for (const auto &Parameter : Parameters)
			{
				Desc.ParamTypes.push_back(Parameter.Type);
				Desc.ParamHostTypeNames.push_back(Parameter.HostTypeName);
				Desc.ParamElementTypeNames.push_back(Parameter.ElementTypeName);
			}
			Desc.Callback = Invoke;
			Desc.UserData = Owner.get();
			Desc.CallbackOwner = Owner;
			return Session.RegisterMember(Type, std::move(Desc), Error);
		}

		template <class T>
		bool RegisterValueProtocol(FHostBindingRegistry &Registry, const std::shared_ptr<FBridge> &Bridge,
		    std::size_t Type, std::string &Error)
		{
			auto Clone = std::make_shared<FCall>(
			    [Bridge, Type](const auto &Call, auto &Out, auto &)
			{
				Out = Make(Bridge, Type, Payload<T>(Call.Args[0]));
				return true;
			});
			auto Equals = std::make_shared<FCall>(
			    [](const auto &Call, auto &Out, auto &)
			{
				Out = PropertyValuesEqual(FPropertyValue(Payload<T>(Call.Args[0])),
				    FPropertyValue(Payload<T>(Call.Args[1])));
				return true;
			});
			if (!Registry.RegisterTypeClone(Bridge->Types[Type], Invoke, Clone.get(), Error) ||
			    !Registry.RegisterTypeEquals(Bridge->Types[Type], Invoke, Equals.get(), Error))
			{
				return false;
			}
			Registry.RetainNativeCallback(Clone.get(), Clone);
			Registry.RetainNativeCallback(Equals.get(), Equals);
			const std::string QualifiedName = TypeNames[Type];
			return Member(
			    Registry, Bridge->Types[Type], QualifiedName.substr(QualifiedName.rfind('.') + 1),
			    EHostMemberKind::Constructor, FNativeTypeDesc::Host(TypeNames[Type]), {},
			    [Bridge, Type](const auto &, auto &Out, auto &)
			{
				Out = Make(Bridge, Type, T{});
				return true;
			},
			    Error);
		}

		FNativeTypeDesc PropertyType(EProperty Property)
		{
			const auto &Default = GetPropertyDefinition(Property).DefaultValue;
			if (std::holds_alternative<std::string>(Default))
			{
				return FNativeTypeDesc::Scalar(ENativeType::String);
			}
			if (std::holds_alternative<bool>(Default))
			{
				return FNativeTypeDesc::Scalar(ENativeType::Bool);
			}
			if (Property == EProperty::Width || Property == EProperty::Height)
			{
				return FNativeTypeDesc::Host(TypeNames[1]);
			}
			if (Property == EProperty::Padding)
			{
				return FNativeTypeDesc::Host(TypeNames[2]);
			}
			if (std::holds_alternative<Graphics::FColor>(Default))
			{
				return FNativeTypeDesc::Host(TypeNames[3]);
			}
			if (Property == EProperty::Layout)
			{
				return FNativeTypeDesc::Enum("VS.Gui.Layout");
			}
			if (Property == EProperty::Visibility)
			{
				return FNativeTypeDesc::Enum("VS.Gui.Visibility");
			}
			if (Property == EProperty::HitTest)
			{
				return FNativeTypeDesc::Enum("VS.Gui.HitTestMode");
			}
			if (Property == EProperty::Cursor)
			{
				return FNativeTypeDesc::Enum("VS.Gui.Cursor");
			}
			return FNativeTypeDesc::Scalar(Property == EProperty::Enabled || Property == EProperty::Focusable
			        ? ENativeType::Bool
			        : ENativeType::Double);
		}

		std::string ScriptType(EProperty Property)
		{
			const auto Type = PropertyType(Property);
			if (Type.Type == ENativeType::String)
			{
				return "string";
			}
			if (Type.Type == ENativeType::Bool)
			{
				return "bool";
			}
			if (Type.Type == ENativeType::Double)
			{
				return "double";
			}
			std::string Name = Type.HostTypeName;
			std::string Result;
			for (const char C : Name)
			{
				Result += C == '.' ? "::" : std::string(1, C);
			}
			return Result;
		}

		FValue ToScript(const std::shared_ptr<FBridge> &Bridge, EProperty Property, const FPropertyValue &Value)
		{
			if (const auto *Item = std::get_if<std::string>(&Value))
			{
				return *Item;
			}
			if (const auto *Item = std::get_if<FLength>(&Value))
			{
				return Make(Bridge, 1, *Item);
			}
			if (const auto *Item = std::get_if<FInsets>(&Value))
			{
				return Make(Bridge, 2, *Item);
			}
			if (const auto *Item = std::get_if<Graphics::FColor>(&Value))
			{
				return Make(Bridge, 3, *Item);
			}
			if (const auto *Item = std::get_if<double>(&Value))
			{
				return *Item;
			}
			if (const auto *Item = std::get_if<bool>(&Value))
			{
				return *Item;
			}
			const auto Type = PropertyType(Property);
			if (const auto *Item = std::get_if<ELayout>(&Value))
			{
				return FEnumValue{Type.HostTypeName, static_cast<std::int64_t>(*Item)};
			}
			if (const auto *Item = std::get_if<EVisibility>(&Value))
			{
				return FEnumValue{Type.HostTypeName, static_cast<std::int64_t>(*Item)};
			}
			if (const auto *Item = std::get_if<ECursor>(&Value))
			{
				return FEnumValue{Type.HostTypeName, static_cast<std::int64_t>(*Item)};
			}
			return FEnumValue{Type.HostTypeName, static_cast<std::int64_t>(std::get<EHitTestMode>(Value))};
		}

		FPropertyValue FromScript(EProperty Property, const FValue &Value)
		{
			const auto &Default = GetPropertyDefinition(Property).DefaultValue;
			if (std::holds_alternative<std::string>(Default))
			{
				return std::get<std::string>(Value);
			}
			if (Property == EProperty::Cursor)
			{
				const auto Number = std::get<FEnumValue>(Value).Value;
				if (Number < 0 || Number > static_cast<std::int64_t>(ECursor::None))
				{
					throw std::invalid_argument("gui.invalid_enum_value");
				}
				return static_cast<ECursor>(Number);
			}
			if (Property == EProperty::Layout || Property == EProperty::Visibility || Property == EProperty::HitTest)
			{
				const auto Number = std::get<FEnumValue>(Value).Value;
				if (Number < 0 || Number > (Property == EProperty::HitTest ? 3 : 2))
				{
					throw std::invalid_argument("gui.invalid_enum_value");
				}
			}
			if (Property == EProperty::Width || Property == EProperty::Height)
			{
				return Payload<FLength>(Value);
			}
			if (Property == EProperty::Padding)
			{
				return Payload<FInsets>(Value);
			}
			if (std::holds_alternative<Graphics::FColor>(Default))
			{
				return Payload<Graphics::FColor>(Value);
			}
			if (Property == EProperty::Layout)
			{
				return static_cast<ELayout>(std::get<FEnumValue>(Value).Value);
			}
			if (Property == EProperty::Visibility)
			{
				return static_cast<EVisibility>(std::get<FEnumValue>(Value).Value);
			}
			if (Property == EProperty::HitTest)
			{
				return static_cast<EHitTestMode>(std::get<FEnumValue>(Value).Value);
			}
			if (std::holds_alternative<bool>(Default))
			{
				return std::get<bool>(Value);
			}
			return Runtime::ToDouble(Value);
		}

		bool RegisterBindings(FHostBindingRegistry &Session, const std::shared_ptr<FBridge> &Bridge, std::string &Error)
		{
			const auto Void = FNativeTypeDesc::Scalar(ENativeType::Void);
			const auto Double = FNativeTypeDesc::Scalar(ENativeType::Double);
			const auto String = FNativeTypeDesc::Scalar(ENativeType::String);
			const auto Int64 = FNativeTypeDesc::Scalar(ENativeType::Int64);
			for (std::size_t Index = 0; Index < std::size(TypeNames); ++Index)
			{
				if (!Session.RegisterType(TypeNames[Index],
				        Index >= 1 && Index <= 3 ? EHostTypeKind::Struct : EHostTypeKind::Class,
				        Bridge->Types[Index], Error))
				{
					return false;
				}
			}
			if (!RegisterValueProtocol<FLength>(Session, Bridge, 1, Error) ||
			    !RegisterValueProtocol<FInsets>(Session, Bridge, 2, Error) ||
			    !RegisterValueProtocol<Graphics::FColor>(Session, Bridge, 3, Error))
			{
				return false;
			}
			auto EventFactory = std::make_shared<FCall>(
			    [Bridge](const auto &, auto &Out, auto &)
			{
				Out = Make(Bridge, 5, std::make_shared<FScriptEvent>());
				return true;
			});
			if (!Session.SetTypeInheritable(Bridge->Types[5], true, Error) ||
			    !Session.RegisterTypeSubclassFactory(Bridge->Types[5], Invoke, EventFactory.get(), Error))
			{
				return false;
			}
			Session.RetainNativeCallback(EventFactory.get(), EventFactory);
			if (!Member(
			        Session, Bridge->Types[5], "VsguiAdopt", EHostMemberKind::Method, Void,
			        {FNativeTypeDesc::Host(TypeNames[5])},
			        [](const auto &Call, auto &, auto &)
			{
				*static_cast<std::shared_ptr<FScriptEvent> *>(Call.HostThis) =
				    Payload<std::shared_ptr<FScriptEvent>>(Call.Args[1]);
				return true;
			},
			        Error))
			{
				return false;
			}
			if (!Member(
			        Session, Bridge->Types[0], "__NativeWidget", EHostMemberKind::Constructor,
			        FNativeTypeDesc::Host(TypeNames[0]), {},
			        [Bridge](const auto &Call, auto &Out, auto &Error)
			{
				FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
				if (Bridge->bRootPending)
				{
					Bridge->bRootPending = false;
					if (Bridge->Root.expired())
					{
						Error = "gui.component_root_expired";
						return false;
					}
					Out = Make(Bridge, 0, Bridge->Root);
					if (!ObserveScriptWidget(Bridge, Bridge->Root.lock(), Error))
					{
						return false;
					}
				}
				else
				{
					auto Widget = FWidget::Create();
					if (!ObserveScriptWidget(Bridge, Widget, Error))
					{
						return false;
					}
					Out = Make(Bridge, 0, FWidgetView{Widget, Widget});
				}
				return true;
			},
			        Error))
			{
				return false;
			}
			if (!Member(
			        Session, Bridge->Types[0], "GetId", EHostMemberKind::Method, String, {},
			        [](const auto &Call, auto &Out, auto &Error)
			{
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				if (!Widget)
				{
					Error = "gui.widget_expired";
					return false;
				}
				Out = Widget->GetId();
				return true;
			},
			        Error) ||
			    !Member(
			        Session, Bridge->Types[0], "SetId", EHostMemberKind::Method, Void, {String},
			        [](const auto &Call, auto &, auto &Error)
			{
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				return Widget && Widget->SetId(std::get<std::string>(Call.Args[1]), Error);
			},
			        Error) ||
			    !Member(
			        Session, Bridge->Types[0], "GetIdentity", EHostMemberKind::Method, Int64, {},
			        [](const auto &Call, auto &Out, auto &Error)
			{
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				if (!Widget || Widget->GetIdentity() > static_cast<std::uint64_t>(INT64_MAX))
				{
					Error = "gui.widget_expired_or_identity_overflow";
					return false;
				}
				Out = static_cast<std::int64_t>(Widget->GetIdentity());
				return true;
			},
			        Error) ||
			    !Member(
			        Session, Bridge->Types[0], "SetChildren", EHostMemberKind::Method, Void,
			        {FNativeTypeDesc::ArrayOf(TypeNames[0])},
			        [Bridge](const auto &Call, auto &, auto &Error)
			{
				FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				const auto &Values = std::get<FDynamicArrayHandle>(Call.Args[1]);
				if (!Widget || !Values)
				{
					Error = "gui.invalid_children";
					return false;
				}
				std::vector<std::shared_ptr<FWidget>> Children;
				Children.reserve(Values->Elements.size());
				for (const auto &Value : Values->Elements)
				{
					Children.push_back(Payload<FWidgetView>(Value).lock());
				}
				return Widget->SetChildren(std::move(Children), Error);
			},
			        Error))
			{
				return false;
			}
			if (!Member(
				Session, Bridge->Types[0], "AppendChild", EHostMemberKind::Method, Void,
				{FNativeTypeDesc::Host(TypeNames[0]), FNativeTypeDesc::ArrayOf(TypeNames[0])},
				[Bridge](const auto &Call, auto &, auto &Error)
				{
					FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
					const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
					if (!Widget)
					{
						Error = "gui.invalid_children";
						return false;
					}
					// Keep SetChildren's complete candidate validation and callback order.
					// Only move the array construction across the script/native boundary.
					std::vector<std::shared_ptr<FWidget>> Children;
					const auto Count = Widget->GetChildCount();
					const auto &Internal = std::get<FDynamicArrayHandle>(Call.Args[2]);
					if (!Internal)
					{
						Error = "gui.invalid_children";
						return false;
					}
					Children.reserve(static_cast<std::size_t>(Count) + Internal->Elements.size() + 1);
					for (const auto &Value : Internal->Elements)
					{
						Children.push_back(Payload<FWidgetView>(Value).lock());
					}
					const auto InternalCount = Children.size();
					for (std::int64_t Index = 0; Index < Count; ++Index)
					{
						const auto Existing = Widget->GetChild(Index, Error);
						if (std::find(Children.begin(), Children.begin() + InternalCount, Existing) == Children.begin() + InternalCount)
						{
							Children.push_back(Existing);
						}
					}
					Children.push_back(Payload<FWidgetView>(Call.Args[1]).lock());
					return Widget->SetChildren(std::move(Children), Error);
				},
				Error))
			{
				return false;
			}
			const char *EnumNames[]{"VS.Gui.Layout", "VS.Gui.Visibility", "VS.Gui.HitTestMode", "VS.Gui.Cursor"};
			if (!Member(
			        Session, Bridge->Types[0], "GetChildCount", EHostMemberKind::Method, Int64, {},
			        [](const auto &Call, auto &Out, auto &Error)
			{
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				if (!Widget)
				{
					Error = "gui.widget_expired";
					return false;
				}
				Out = Widget->GetChildCount();
				return true;
			},
			        Error) ||
			    !Member(
			        Session, Bridge->Types[0], "GetChild", EHostMemberKind::Method, FNativeTypeDesc::Host(TypeNames[0]),
			        {Int64},
			        [Bridge](const auto &Call, auto &Out, auto &Error)
			{
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				const auto Child =
				    Widget ? Widget->GetChild(std::get<std::int64_t>(Call.Args[1]), Error) : nullptr;
				if (!Child)
				{
					return false;
				}
				Out = Make(Bridge, 0, std::weak_ptr<FWidget>(Child));
				return true;
			},
			        Error) ||
			    !Member(
			        Session, Bridge->Types[0], "GetParent", EHostMemberKind::Method,
			        FNativeTypeDesc::Host(TypeNames[0]), {},
			        [Bridge](const auto &Call, auto &Out, auto &Error)
			{
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				if (!Widget)
				{
					Error = "gui.widget_expired";
					return false;
				}
				const auto Parent = Widget->GetParent();
				Out = Parent ? Make(Bridge, 0, std::weak_ptr<FWidget>(Parent)) : FValue(FHostObjectHandle{});
				return true;
			},
			        Error))
			{
				return false;
			}
			std::vector<std::vector<std::string>> Values{{"Vertical", "Horizontal", "Overlay"},
			    {"Visible", "Hidden", "Collapsed"},
			    {"SelfAndChildren", "SelfOnly", "ChildrenOnly", "None"}};
			Values.emplace_back();
			for (unsigned Index = 0; Index <= static_cast<unsigned>(ECursor::None); ++Index)
			{
				Values.back().push_back(GetCursorName(static_cast<ECursor>(Index)));
			}
			for (std::size_t Index = 0; Index < std::size(EnumNames); ++Index)
			{
				if (!Session.RegisterType(EnumNames[Index], EHostTypeKind::Enum, Bridge->Enums[Index], Error) ||
				    !Session.SetEnumScoped(Bridge->Enums[Index], true, Error))
				{
					return false;
				}
				for (std::size_t Entry = 0; Entry < Values[Index].size(); ++Entry)
				{
					FHostMemberDesc Desc;
					Desc.Kind = EHostMemberKind::EnumMember;
					Desc.Name = Values[Index][Entry];
					Desc.EnumValue = static_cast<std::int64_t>(Entry);
					if (!Session.RegisterMember(Bridge->Enums[Index], std::move(Desc), Error))
					{
						return false;
					}
				}
			}
			if (!Member(
			        Session, Bridge->Types[1], "Length", EHostMemberKind::Constructor,
			        FNativeTypeDesc::Host(TypeNames[1]), {Double},
			        [Bridge](const auto &C, auto &Out, auto &)
			{
				Out = Make(Bridge, 1, FLength{ELengthUnit::Pixel, std::get<double>(C.Args[0])});
				return true;
			},
			        Error) ||
			    !Member(
			        Session, Bridge->Types[1], "Percent", EHostMemberKind::StaticMethod,
			        FNativeTypeDesc::Host(TypeNames[1]), {Double},
			        [Bridge](const auto &C, auto &Out, auto &)
			{
				Out = Make(Bridge, 1, FLength{ELengthUnit::Percent, std::get<double>(C.Args[0])});
				return true;
			},
			        Error) ||
			    !Member(
			        Session, Bridge->Types[1], "Auto", EHostMemberKind::StaticMethod,
			        FNativeTypeDesc::Host(TypeNames[1]), {},
			        [Bridge](const auto &, auto &Out, auto &)
			{
				Out = Make(Bridge, 1, FLength{});
				return true;
			},
			        Error) ||
			    !Member(
			        Session, Bridge->Types[2], "Insets", EHostMemberKind::Constructor,
			        FNativeTypeDesc::Host(TypeNames[2]), {Double, Double, Double, Double},
			        [Bridge](const auto &C, auto &Out, auto &)
			{
				Out = Make(Bridge, 2,
				    FInsets{std::get<double>(C.Args[0]), std::get<double>(C.Args[1]),
				        std::get<double>(C.Args[2]), std::get<double>(C.Args[3])});
				return true;
			},
			        Error) ||
			    !Member(
			        Session, Bridge->Types[3], "Color", EHostMemberKind::Constructor,
			        FNativeTypeDesc::Host(TypeNames[3]), {Double, Double, Double, Double},
			        [Bridge](const auto &C, auto &Out, auto &)
			{
				Out = Make(Bridge, 3,
				    Graphics::FColor{static_cast<float>(std::get<double>(C.Args[0])),
				        static_cast<float>(std::get<double>(C.Args[1])),
				        static_cast<float>(std::get<double>(C.Args[2])),
				        static_cast<float>(std::get<double>(C.Args[3]))});
				return true;
			},
			        Error))
			{
				return false;
			}
			for (std::size_t Index = 0; Index < static_cast<std::size_t>(EProperty::Count); ++Index)
			{
				const auto Property = static_cast<EProperty>(Index);
				const auto &Definition = GetPropertyDefinition(Property);
				const auto Type = PropertyType(Property);
				if (Property == EProperty::Width || Property == EProperty::Height || Property == EProperty::Padding ||
				    Property == EProperty::Gap || Property == EProperty::Opacity || Property == EProperty::Background)
				{
					if (!Member(
					        Session, Bridge->Types[0], "Animate" + std::string(Definition.Name),
					        EHostMemberKind::Method, Void,
					        {Type, Double, Double, Double, FNativeTypeDesc::Scalar(ENativeType::Int),
					            FNativeTypeDesc::Scalar(ENativeType::Bool)},
					        [Property](const auto &Call, auto &, auto &Error)
					{
						const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
						const auto Easing = std::get<std::int32_t>(Call.Args[5]);
						if (Easing < 0 || Easing > static_cast<int>(EEasing::EaseInOut))
						{
							Error = "gui.invalid_animation_options";
							return false;
						}
						FAnimationOptions Options;
						Options.Duration = std::get<double>(Call.Args[2]);
						Options.Delay = std::get<double>(Call.Args[3]);
						Options.MaxStep = std::get<double>(Call.Args[4]);
						Options.Easing = static_cast<EEasing>(Easing);
						Options.bHoldEnd = std::get<bool>(Call.Args[6]);
						return Widget &&
						    Widget->Animate(Property, FromScript(Property, Call.Args[1]), Options, Error);
					},
					        Error) ||
					    !Member(
					        Session, Bridge->Types[0], "Stop" + std::string(Definition.Name) + "Animation",
					        EHostMemberKind::Method, Void, {},
					        [Property](const auto &Call, auto &, auto &Error)
					{
						const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
						return Widget && Widget->StopAnimation(Property, Error);
					},
					        Error) ||
					    !Member(
					        Session, Bridge->Types[0], "GetPresented" + std::string(Definition.Name),
					        EHostMemberKind::Method, Type, {},
					        [Bridge, Property](const auto &Call, auto &Out, auto &Error)
					{
						const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
						FPropertyValue Value;
						if (!Widget || !Widget->GetPresentation(Property, Value, Error))
						{
							return false;
						}
						Out = ToScript(Bridge, Property, Value);
						return true;
					},
					        Error))
					{
						return false;
					}
				}
				if (!Member(
				        Session, Bridge->Types[0], "GetFallback" + std::string(Definition.Name),
				        EHostMemberKind::Method, Type, {},
				        [Bridge, Property](const auto &Call, auto &Out, auto &Error)
				{
					const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
					FPropertyValue Value;
					if (!Widget || !Widget->GetFallback(Property, Value, Error))
					{
						return false;
					}
					Out = ToScript(Bridge, Property, Value);
					return true;
				},
				        Error))
				{
					return false;
				}
				if (!Member(
				        Session, Bridge->Types[0], "Get" + std::string(Definition.Name), EHostMemberKind::Method, Type,
				        {},
				        [Bridge, Property](const auto &C, auto &Out, auto &Error)
				{
					const auto Widget = static_cast<const FWidgetView *>(C.HostThis)->lock();
					struct FInvokerScope
					{
						~FInvokerScope()
						{
							Bridge.Invoker = Previous;
						}
						FBridge &Bridge;
						IScriptCallInvoker *Previous;
					} Guard{*Bridge, Bridge->Invoker};
					Bridge->Invoker = C.ScriptInvoker;
					FPropertyValue Value;
					if (!Widget || !Widget->Get(Property, Value, Error))
					{
						return false;
					}
					Out = ToScript(Bridge, Property, Value);
					return true;
				},
				        Error) ||
				    !Member(
				        Session, Bridge->Types[0], "Set" + std::string(Definition.Name), EHostMemberKind::Method, Void,
				        {Type},
				        [Property](const auto &C, auto &, auto &Error)
				{
					const auto Widget = static_cast<const FWidgetView *>(C.HostThis)->lock();
					return Widget && Widget->Set(Property, FromScript(Property, C.Args[1]), Error);
				},
				        Error) ||
				    !Member(
				        Session, Bridge->Types[0], "Apply" + std::string(Definition.Name), EHostMemberKind::Method, Void,
				        {Type},
				        [Property](const auto &C, auto &, auto &Error)
				{
					const auto Widget = static_cast<const FWidgetView *>(C.HostThis)->lock();
					return Widget && Widget->SetSynchronized(Property, FromScript(Property, C.Args[1]), Error);
				}, Error) ||
				    !Member(
				        Session, Bridge->Types[0], "ApplyClear" + std::string(Definition.Name), EHostMemberKind::Method,
				        Void, {},
				        [Property](const auto &C, auto &, auto &Error)
				{
					const auto Widget = static_cast<const FWidgetView *>(C.HostThis)->lock();
					return Widget && Widget->ClearSynchronized(Property, Error);
				}, Error) ||
				    !Member(
				        Session, Bridge->Types[0], "Clear" + std::string(Definition.Name), EHostMemberKind::Method,
				        Void, {},
				        [Property](const auto &C, auto &, auto &Error)
				{
					const auto Widget = static_cast<const FWidgetView *>(C.HostThis)->lock();
					return Widget && Widget->Clear(Property, Error);
				},
				        Error))
				{
					return false;
				}
			}
			if (!Member(
			        Session, Bridge->Types[4], "__NativeElements", EHostMemberKind::Constructor,
			        FNativeTypeDesc::Host(TypeNames[4]), {},
			        [Bridge](const auto &, auto &Out, auto &)
			{
				Out = Make(Bridge, 4, Bridge->Find);
				return true;
			},
			        Error))
			{
				return false;
			}
			if (!Member(
			        Session, Bridge->Types[4], "HasLookup", EHostMemberKind::Method,
			        FNativeTypeDesc::Scalar(ENativeType::Bool), {},
			        [](const auto &Call, auto &Out, auto &)
			{
				Out = static_cast<bool>(
				    *static_cast<const std::function<std::shared_ptr<FWidget>(const std::string &)> *>(
				        Call.HostThis));
				return true;
			},
			        Error, true))
			{
				return false;
			}
			for (const auto &Name : {"Find", "Require"})
			{
				if (!Member(
				        Session, Bridge->Types[4], Name, EHostMemberKind::Method, FNativeTypeDesc::Host(TypeNames[0]),
				        {String},
				        [Bridge, bRequired = std::string(Name) == "Require"](const auto &C, auto &Out, auto &Error)
				{
					const auto Find =
					    static_cast<const std::function<std::shared_ptr<FWidget>(const std::string &)> *>(
					        C.HostThis);
					const auto &Id = std::get<std::string>(C.Args[1]);
					const auto Widget = *Find ? (*Find)(Id) : nullptr;
					if (!Widget && bRequired)
					{
						Error = "gui.element_required: " + Id;
						return false;
					}
					Out =
					    Widget ? Make(Bridge, 0, std::weak_ptr<FWidget>(Widget)) : FValue(FHostObjectHandle{});
					return true;
				},
				        Error, true))
				{
					return false;
				}
			}
			for (const auto &Name : {"StopPropagation", "StopImmediatePropagation", "PreventDefault"})
			{
				if (!Member(
				        Session, Bridge->Types[5], Name, EHostMemberKind::Method, Void, {},
				        [Name = std::string(Name)](const auto &C, auto &, auto &Error)
				{
					const auto &Event = *static_cast<const std::shared_ptr<FScriptEvent> *>(C.HostThis);
					if (!Event->Active)
					{
						Error = "gui.event_expired";
						return false;
					}
					if (Name == "PreventDefault")
					{
						Event->Active->PreventDefault();
					}
					else if (Name == "StopPropagation")
					{
						Event->Active->StopPropagation();
					}
					else
					{
						Event->Active->StopImmediatePropagation();
					}
					return true;
				},
				        Error))
				{
					return false;
				}
			}
			for (const auto &Name : {"NativeTarget", "NativeCurrentTarget"})
			{
				if (!Member(
				        Session, Bridge->Types[5], Name, EHostMemberKind::PropertyGetter,
				        FNativeTypeDesc::Host(TypeNames[0]), {},
				        [Bridge, bTarget = std::string(Name) == "NativeTarget"](const auto &C, auto &Out, auto &)
				{
					const auto &Event = *static_cast<const std::shared_ptr<FScriptEvent> *>(C.HostThis);
					Out = Make(Bridge, 0, bTarget ? Event->Target : Event->Sender);
					return true;
				},
				        Error))
				{
					return false;
				}
			}
			const auto InputProperty =
			    [&](const char *Name, ENativeType Type, std::function<FValue(const Platform::FEvent &)> Read)
			{
				return Member(
				    Session, Bridge->Types[5], Name, EHostMemberKind::PropertyGetter, FNativeTypeDesc::Scalar(Type), {},
				    [Read = std::move(Read)](const auto &C, auto &Out, auto &)
				{
					Out = Read((*static_cast<const std::shared_ptr<FScriptEvent> *>(C.HostThis))->Input);
					return true;
				},
				    Error);
			};
			if (!Member(Session, Bridge->Types[5], "DefaultPrevented", EHostMemberKind::PropertyGetter,
			        FNativeTypeDesc::Scalar(ENativeType::Bool), {},
			        [](const auto &Call, auto &Out, auto &)
			{
				const auto &Event = *static_cast<const std::shared_ptr<FScriptEvent> *>(Call.HostThis);
				Out = Event->Active ? Event->Active->bDefaultPrevented : Event->bDefaultPrevented;
				return true;
			}, Error) ||
			    !InputProperty("Type", ENativeType::Int,
			        [](const auto &I)
			{
				return static_cast<std::int32_t>(I.Type);
			}) ||
			    !InputProperty("Key", ENativeType::Int,
			        [](const auto &I)
			{
				return static_cast<std::int32_t>(I.Key);
			}) ||
			    !InputProperty("ScanCode", ENativeType::Int, [](const auto &I)
			{
				return I.ScanCode;
			}) ||
			    !InputProperty("Button", ENativeType::Int, [](const auto &I)
			{
				return I.Button;
			}) ||
			    !InputProperty("Modifiers", ENativeType::Unsigned, [](const auto &I)
			{
				return I.Modifiers;
			}) ||
			    !InputProperty("Buttons", ENativeType::Unsigned, [](const auto &I)
			{
				return I.Buttons;
			}) ||
			    !InputProperty("PointerId", ENativeType::UInt64, [](const auto &I)
			{
				return I.PointerId;
			}) ||
			    !InputProperty("DeviceId", ENativeType::UInt64, [](const auto &I)
			{
				return I.DeviceId;
			}) ||
			    !InputProperty("WindowId", ENativeType::UInt64, [](const auto &I)
			{
				return I.WindowId;
			}) ||
			    !InputProperty("TimestampNanoseconds", ENativeType::UInt64, [](const auto &I)
			{
				return I.TimestampNanoseconds;
			}) ||
			    !InputProperty("SelectionStart", ENativeType::Int64, [](const auto &I)
			{
				return I.SelectionStart;
			}) ||
			    !InputProperty("SelectionLength", ENativeType::Int64, [](const auto &I)
			{
				return I.SelectionLength;
			}) ||
			    !InputProperty("Text", ENativeType::String, [](const auto &I)
			{
				return I.Text;
			}) ||
			    !InputProperty("Repeat", ENativeType::Bool, [](const auto &I)
			{
				return I.bRepeat;
			}) ||
			    !InputProperty("PointerKind", ENativeType::Int, [](const auto &I)
			{
				return static_cast<std::int32_t>(I.PointerKind);
			}) ||
			    !InputProperty("DeviceKind", ENativeType::Int, [](const auto &I)
			{
				return static_cast<std::int32_t>(I.DeviceKind);
			}) ||
			    !InputProperty("DeviceName", ENativeType::String, [](const auto &I)
			{
				return I.DeviceName;
			}) ||
			    !InputProperty("AggregateDevice", ENativeType::Bool, [](const auto &I)
			{
				return I.bAggregateDevice;
			}) ||
			    !InputProperty("Synthetic", ENativeType::Bool, [](const auto &I)
			{
				return I.bSynthetic;
			}))
			{
				return false;
			}
			if (!Member(
			        Session, Bridge->Types[5], "Phase", EHostMemberKind::PropertyGetter,
			        FNativeTypeDesc::Scalar(ENativeType::Int), {},
			        [](const auto &Call, auto &Out, auto &)
			{
				Out = static_cast<std::int32_t>(
				    (*static_cast<const std::shared_ptr<FScriptEvent> *>(Call.HostThis))->Phase);
				return true;
			},
			        Error))
			{
				return false;
			}
			for (const auto &Name : {"X", "Y", "DeltaX", "DeltaY", "Pressure"})
			{
				if (!Member(
				        Session, Bridge->Types[5], Name, EHostMemberKind::PropertyGetter, Double, {},
				        [Name = std::string(Name)](const auto &C, auto &Out, auto &)
				{
					const auto &Input =
					    (*static_cast<const std::shared_ptr<FScriptEvent> *>(C.HostThis))->Input;
					Out = Name == "X"      ? Input.X
					    : Name == "Y"      ? Input.Y
					    : Name == "DeltaX" ? Input.DeltaX
					    : Name == "DeltaY" ? Input.DeltaY
					                       : Input.Pressure;
					return true;
				},
				        Error))
				{
					return false;
				}
			}
			return RegisterResourceBindings(Session, Bridge, Error) && RegisterStyleBindings(Session, Bridge, Error) &&
			    RegisterRuntimeBindings(Session, Bridge, Error) && RegisterWidgetEvents(Session, Bridge, Error) &&
			    RegisterWidgetGeometry(Session, Bridge, Error) && RegisterControlBindings(Session, Bridge, Error) &&
			    RegisterPaintBindings(Session, Bridge, Error);
		}

		struct FGenerated
		{
			void Append(const std::string &Value, FMarkupLocation Origin, bool bSourceLines = false)
			{
				for (const char C : Value)
				{
					if (Lines.size() <= Line)
					{
						Lines.push_back(Origin);
					}
					Text += C;
					if (C == '\n')
					{
						++Line;
						if (bSourceLines)
						{
							++Origin.Line;
							Origin.Column = 1;
						}
					}
				}
			}
			FMarkupDiagnostic Diagnostic(const FRunResult &Result) const
			{
				if (Result.bExitRequested)
				{
					return {Lines.empty() ? FMarkupLocation{} : Lines.front(),
					    "gui.script_exit_requested: " + std::to_string(Result.ExitCode)};
				}
				if (!Result.Error.Location.FileLabel.empty() && Result.Error.Location.FileLabel != Label)
				{
					return {
					    {Result.Error.Location.FileLabel, 0, Result.Error.Location.Line, Result.Error.Location.Column},
					    Result.Error.Message.empty() ? Result.Error.MessageId : Result.Error.Message};
				}
				const auto LineIndex = Result.Error.Location.Line > 0 ? Result.Error.Location.Line - 1 : 0;
				const auto Location =
				    LineIndex < Lines.size() ? Lines[LineIndex] : FMarkupLocation{Result.Error.Location.FileLabel};
				return {Location, Result.Error.Message.empty() ? Result.Error.MessageId : Result.Error.Message};
			}
			std::string Error(const FRunResult &Result) const
			{
				return Diagnostic(Result).Format();
			}

			std::string Text;
			std::string Label;
			std::vector<FMarkupLocation> Lines;
			std::size_t Line = 0;
		};

		class FState final : public IEngineHost
		{
		public:
			//~ Begin IEngineHost Interface
			void OnPrintLine(std::string_view Line) override
			{
				if (Print)
				{
					Print(Line);
				}
			}
			void OnDiagnosticLine(std::string_view) override
			{
			}
			//~ End IEngineHost Interface

			explicit FState(const FMarkupScriptOptions &Options) : Print(Options.Print), Session(*this)
			{
				Session.SetHotReloadEnabled(true);
			}
			~FState()
			{
				Object.Reset();
				std::string Ignored;
				Session.DrainValueRoots(Ignored);
			}
			bool Call(const std::string &Method, std::vector<FValue> Arguments, FValue &Result, std::string &Error)
			{
				if (Thread != std::this_thread::get_id())
				{
					Error = "gui.wrong_thread";
					return false;
				}
				if (Bridge->Invoker)
				{
					return Bridge->Invoker->InvokeFunction(Method, Arguments, Result, Error);
				}
				const auto Called = Session.CallFunction(Method, Arguments, &Result);
				if (!Called.bSuccess || Called.bExitRequested)
				{
					Error = Generated.Error(Called);
					return false;
				}
				return true;
			}

			std::thread::id Thread = std::this_thread::get_id();
			std::function<void(std::string_view)> Print;
			VeroScriptSession Session;
			FValueRoot Object;
			std::shared_ptr<FBridge> Bridge = std::make_shared<FBridge>();
			FGenerated Generated;
			std::string Type;
		};

		struct FRetired
		{
			std::mutex Mutex;
			std::vector<std::unique_ptr<FState>> States;
		};
		FRetired &Retired()
		{
			static auto *Value = new FRetired;
			return *Value;
		}

		class FScriptContext final : public IMarkupScriptContext
		{
		public:
			//~ Begin IMarkupScriptContext Interface
			bool Synchronize(std::string &Error) override
			{
				const auto *Root = State->Object.Read();
				if (!Root)
				{
					Error = "gui.script_context_closed";
					return false;
				}
				FValue Ignored;
				return State->Call("__VSGuiSynchronizeContext", {*Root}, Ignored, Error);
			}
			bool ResolveGetter(const FMarkupExpression &Expression, std::weak_ptr<FWidget> Self,
			    FPropertyGetter &Getter, std::string &Error) override
			{
				const auto Found = Methods.find(Key(Expression));
				if (Found == Methods.end())
				{
					Error = "gui.expression_not_compiled";
					return false;
				}
				Getter = [Owner = State, Method = Found->second, Property = Expression.Property, Self](auto &Out,
				             auto &Error)
				{
					const auto Widget = Self.lock();
					if (!Widget || Widget->IsDisposed())
					{
						Out.reset();
						return true;
					}
					if (Owner->Thread != std::this_thread::get_id())
					{
						Error = "gui.wrong_thread";
						return false;
					}
					const auto *Root = Owner->Object.Read();
					if (!Root)
					{
						Error = "gui.script_context_closed";
						return false;
					}
					FValue Result;
					if (!Owner->Call(Method, {*Root, Make(Owner->Bridge, 0, Self)}, Result, Error))
					{
						return false;
					}
					try
					{
						Out = FromScript(Property, Result);
					}
					catch (const std::exception &Exception)
					{
						Error = Exception.what();
						return false;
					}
					return true;
				};
				return true;
			}
			bool ResolveEvent(const FMarkupExpression &Expression, std::weak_ptr<FWidget> Self,
			    std::function<void(FEvent &)> &Callback, std::string &Error) override
			{
				const auto Found = Methods.find(Key(Expression));
				if (Found == Methods.end())
				{
					Error = "gui.expression_not_compiled";
					return false;
				}
				Callback = [Owner = State, Method = Found->second, Self](FEvent &Event)
				{
					if (Owner->Thread != std::this_thread::get_id())
					{
						throw std::runtime_error("gui.wrong_thread");
					}
					const auto *Object = Owner->Object.Read();
					if (!Object)
					{
						throw std::runtime_error("gui.script_context_closed");
					}
					auto Input = std::make_shared<FScriptEvent>();
					Input->Input = Event.Input;
					Input->Phase = Event.Phase;
					Input->Target = Event.Target;
					Input->Sender = Event.CurrentTarget;
					Input->Active = &Event;
					struct FDeactivate
					{
						~FDeactivate()
						{
							Event.bDefaultPrevented = Event.Active && Event.Active->bDefaultPrevented;
							Event.Active = nullptr;
						}
						FScriptEvent &Event;
					} Guard{*Input};
					FValue Result;
					std::string Error;
					if (!Owner->Call(Method,
					        {*Object, Make(Owner->Bridge, 0, Self),
					            Make(Owner->Bridge, 0, std::weak_ptr<FWidget>(Event.CurrentTarget)),
					            Make(Owner->Bridge, 5, Input)},
					        Result, Error))
					{
						throw std::runtime_error(Error);
					}
				};
				return true;
			}
			//~ End IMarkupScriptContext Interface

			static std::string Key(const FMarkupExpression &Expression)
			{
				return Expression.Location.Source + ":" + std::to_string(Expression.Location.Offset) + ":" +
				    Expression.Text;
			}

			std::shared_ptr<FState> State;
			std::map<std::string, std::string> Methods;
		};

		std::string Quote(const std::string &Value)
		{
			std::string Result = "\"";
			for (const char C : Value)
			{
				if (C == '\\' || C == '"')
				{
					Result += '\\';
				}
				if (C == '\n')
				{
					Result += "\\n";
				}
				else if (C == '\r')
				{
					Result += "\\r";
				}
				else
				{
					Result += C;
				}
			}
			return Result + '"';
		}

		std::string MetadataType(std::string Type)
		{
			for (auto Offset = Type.find("::"); Offset != std::string::npos; Offset = Type.find("::", Offset + 1))
			{
				Type.replace(Offset, 2, ".");
			}
			return Type;
		}

		bool MethodNames(VeroScriptSession &Session, const std::string &Type,
		    std::map<std::string, FInstanceMethodInfo> &Names, std::string &Error)
		{
			std::vector<FInstanceMethodInfo> Methods;
			if (!Session.EnumerateInstanceMethods(MetadataType(Type), Methods))
			{
				Error = "gui.script_type_missing: " + Type;
				return false;
			}
			for (const auto &Method : Methods)
			{
				const FDeclarationAnnotation *Annotation = nullptr;
				for (const auto &Item : Method.DeclMeta.Annotations)
				{
					if (!AnnotationNamesEqual(Item.Name, "@GuiMethod", false))
					{
						continue;
					}
					if (Annotation)
					{
						Error = "gui.duplicate_gui_method";
						return false;
					}
					Annotation = &Item;
				}
				if (!Annotation)
				{
					continue;
				}
				std::string Mark;
				for (const auto &Argument : Annotation->Arguments)
				{
					if (!AnnotationNamesEqual(Argument.Name, "Mark", false) || !Mark.empty() ||
					    Argument.Kind != EAnnotationArgumentKind::Scalar ||
					    Argument.ScalarKind != EAnnotationScalarKind::String || !IsMarkupName(Argument.Value))
					{
						Error = "gui.invalid_gui_method_mark";
						return false;
					}
					Mark = Argument.Value;
				}
				std::set<std::string> AddedNames;
				for (const auto &Name : std::vector<std::string>{Method.Name, Mark})
				{
					if (Name.empty() || !AddedNames.insert(Name).second)
					{
						continue;
					}
					if (!Names.emplace(Name, Method).second)
					{
						Error = "gui.method_name_conflict: " + Name;
						return false;
					}
				}
			}
			return true;
		}

		FGenerated Generate(const FMarkupScript &Script, const std::string &ClassName, const std::string &Body,
		    const std::string &FrameworkPath,
		    const std::vector<std::pair<std::string, FMarkupLocation>> &Methods)
		{
			FGenerated Result;
			Result.Append("import " + Quote(FrameworkPath) + ";\n", Script.Location);
			for (const auto &Import : Script.Imports)
			{
				Result.Append("import " + Quote(Import.Identity) + ";\n", Script.Location);
			}
			const bool bExternal = !Script.bInline && !Script.Source.Text.empty();
			if (bExternal)
			{
				Result.Append("import " + Quote(Script.Source.Identity) + ";\n", Script.Location);
			}
			const auto Separator = ClassName.rfind("::");
			const std::string Name = Separator == std::string::npos ? ClassName : ClassName.substr(Separator + 2);
			if (Separator != std::string::npos)
			{
				Result.Append("namespace " + ClassName.substr(0, Separator) + " {\n", Script.Location);
			}
			Result.Append("class " + Name +
			        (Script.bComponent ? " : public VS::Gui::Widget" : " : public VS::Gui::__BindingScope") +
			        "\n{\n",
			    Script.Location);
			if (bExternal)
			{
				Result.Append("public:\n" + Script.Type + " context;\n" + Name + "() { context = " + Script.Type +
				        "(); }\n",
				    Script.Location);
			}
			else
			{
				Result.Append(Body + "\n", Script.Location, true);
			}
			Result.Append("public:\n", Script.Location);
			for (const auto &Method : Methods)
			{
				Result.Append(Method.first + "\n", Method.second);
			}
			Result.Append("};\n", Script.Location);
			if (Separator != std::string::npos)
			{
				Result.Append("}\n", Script.Location);
			}
			Result.Append(ClassName + " __VSGuiCreateContext() { return " + ClassName + "(); }\n", Script.Location);
			Result.Append("void __VSGuiSynchronizeContext(" + ClassName + " context) { context.Synchronize(); }\n",
			    Script.Location);
			return Result;
		}

	} // namespace ScriptBindings

	using namespace ScriptBindings;

	bool RegisterScriptBindings(FHostBindingRegistry &Registry, std::string &Error)
	{
		return RegisterScriptBindings(Registry, {}, Error);
	}

	bool RegisterScriptBindings(FHostBindingRegistry &Registry, FScriptServices Services, std::string &Error)
	{
		auto Candidate = Registry;
		auto Images = Services.Application.Image;
		auto Fonts = Services.Application.Font;
		if (Services.ResolveApplicationFactories)
		{
			const auto Resolve = Services.ResolveApplicationFactories;
			const auto Base = Services.Application;
			Images = [Resolve, Base](const std::string &Backend, std::string &Failure) -> std::shared_ptr<Image::IService>
			{
				auto Current = Base;
				if (!Resolve(Current, Failure)) return {};
				if (Current.Image) return Current.Image(Backend, Failure);
				Failure = "gui.provider_unavailable: image/" + Backend;
				return {};
			};
			Fonts = [Resolve, Base](const std::string &Backend, std::string &Failure) -> std::shared_ptr<Font::IService>
			{
				auto Current = Base;
				if (!Resolve(Current, Failure)) return {};
				if (Current.Font) return Current.Font(Backend, Failure);
				Failure = "gui.provider_unavailable: font/" + Backend;
				return {};
			};
		}
		if ((!Candidate.FindType("VS.Image.Bitmap") &&
		        !Image::RegisterScriptBindings(Candidate, std::move(Images), Error, Services.ReadResource)) ||
		    (!Candidate.FindType("VS.Font.Face") &&
		        !Font::RegisterScriptBindings(Candidate, std::move(Fonts), Error, Services.ReadResource)))
		{
			return false;
		}
		auto Bridge = std::make_shared<FBridge>();
		Bridge->Services = std::move(Services);
		if (Bridge->Services.Invoke && !Bridge->Services.IsCurrentProgram)
		{
			Error = "gui.program_identity_bridge_required";
			return false;
		}
		if (!RegisterBindings(Candidate, Bridge, Error))
		{
			return false;
		}
		Registry = std::move(Candidate);
		return true;
	}

	bool ConfigureGuiScriptEngine(FEngineConfig &Config, std::string &Error)
	{
		return ConfigureGuiScriptEngine(Config, {}, Error);
	}

	bool ConfigureGuiScriptEngine(FEngineConfig &Config, const FGuiCompilationConfiguration &Configuration,
	    std::string &Error)
	{
		auto Next = Config;
		if (!RemoveGuiSourceImports(Next.CompileProcessing, Error))
		{
			return false;
		}
		if (Next.ModuleEnvironment)
		{
			auto Description = Next.ModuleEnvironment->GetDescription();
			if (!RemoveGuiSourceImports(Description.CompileProcessing, Error))
			{
				return false;
			}
			if (!ConfigureGuiCompilation(Description.CompileProcessing, Configuration, Error))
			{
				return false;
			}
			// The immutable environment owns each GUI processor and definition once.
			// Keep independent caller processors while removing a repeated GUI installation.
			if (Next.CompileProcessing.Registry)
			{
				auto Validated = Next.CompileProcessing;
				if (!ConfigureGuiCompilation(Validated, Configuration, Error))
				{
					return false;
				}
				auto Registry = std::make_shared<FCompileProcessorRegistry>();
				for (const auto &Processor : Next.CompileProcessing.Registry->GetProcessors())
				{
					if (Processor.ProcessorId == "veroscript.gui.declarations" ||
					    Processor.ProcessorId == "veroscript.gui.markup")
					{
						continue;
					}
					if (!Registry->Register(Processor, Error))
					{
						return false;
					}
				}
				Registry->Freeze();
				Next.CompileProcessing.Registry = Registry->GetProcessors().empty() ? nullptr : std::move(Registry);
			}
			auto &Definitions = Next.CompileProcessing.AnnotationDefinitions;
			for (auto It = Definitions.begin(); It != Definitions.end();)
			{
				bool bGui = false;
				for (const auto *Name : {"@GuiWidget", "@GuiProperty", "@GuiMethod", "@GuiEvent"})
				{
					bGui = bGui || AnnotationNamesEqual(It->Name, Name, false);
				}
				if (!bGui)
				{
					++It;
					continue;
				}
				const auto Published =
				    std::find_if(Description.CompileProcessing.AnnotationDefinitions.begin(),
				        Description.CompileProcessing.AnnotationDefinitions.end(),
				        [&](const auto &D)
				{
					return AnnotationNamesEqual(D.Name, It->Name, false);
				});
				if (Published == Description.CompileProcessing.AnnotationDefinitions.end())
				{
					++It;
					continue;
				}
				if (It->Id != Published->Id || It->CaseSensitive != Published->CaseSensitive)
				{
					Error = "gui.annotation_definition_conflict: " + It->Name;
					return false;
				}
				It = Definitions.erase(It);
			}
			Next.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
			if (!Next.ModuleEnvironment)
			{
				return false;
			}
		}
		else if (!ConfigureGuiCompilation(Next.CompileProcessing, Configuration, Error))
		{
			return false;
		}
		if (!ConfigureGuiLibrarySources(Next, Error))
		{
			return false;
		}
		Config = std::move(Next);
		return true;
	}

	std::shared_ptr<FWidget> GetScriptWidget(const FValue &Handle, std::string &Error)
	{
		Error.clear();
		try
		{
			auto Widget = Payload<FWidgetView>(Handle).lock();
			if (!Widget || Widget->IsDisposed())
			{
				Error = "gui.widget_expired";
				return {};
			}
			return Widget;
		}
		catch (const std::exception &Exception)
		{
			Error = Exception.what();
			return {};
		}
	}

	FMarkupScriptFactory MakeMarkupScriptFactory(FMarkupScriptOptions Options)
	{
		return [Options = std::move(Options)](const FMarkupScript &Script,
		           const std::vector<FMarkupExpression> &Expressions,
		           FMarkupDiagnostic &Diagnostic) -> std::shared_ptr<IMarkupScriptContext>
		{
			CollectMarkupScripts();
			Diagnostic = {};
			std::string Error;
			bool bSuccess = false;
			struct FDiagnosticGuard
			{
				~FDiagnosticGuard()
				{
					if (!bSuccess && Diagnostic.Message.empty())
					{
						Diagnostic = {Location, Error.empty() ? "gui.script_creation_failed" : Error};
					}
				}
				FMarkupDiagnostic &Diagnostic;
				FMarkupLocation Location;
				std::string &Error;
				bool &bSuccess;
			} DiagnosticGuard{Diagnostic, Script.Location, Error, bSuccess};
			auto LocalOptions = Options;
			const auto Environment = LocalOptions.Engine.ModuleEnvironment;
			LocalOptions.Engine.ModuleEnvironment.reset();
			auto Definitions = LocalOptions.Engine.CompileProcessing.AnnotationDefinitions;
			if (Environment)
			{
				const auto &Published = Environment->GetDescription().CompileProcessing.AnnotationDefinitions;
				Definitions.insert(Definitions.end(), Published.begin(), Published.end());
			}
			for (const char *Name : {"@GuiWidget", "@GuiProperty", "@GuiMethod", "@GuiEvent"})
			{
				bool bExists = false;
				for (const auto &Existing : Definitions)
				{
					if (!AnnotationNamesEqual(Existing.Name, Name, false))
					{
						continue;
					}
					if (Existing.CaseSensitive)
					{
						Error = "gui.annotation_policy_conflict: " + Existing.Name;
						return {};
					}
					bExists = true;
				}
				if (bExists)
				{
					continue;
				}
				FAnnotationDefinition Definition;
				Definition.Name = Name;
				Definition.Id = std::string("gui:") + Name;
				Definition.CaseSensitive = false;
				LocalOptions.Engine.CompileProcessing.AnnotationDefinitions.push_back(std::move(Definition));
			}
			auto Processing = LocalOptions.Engine.CompileProcessing;
			Processing.AnnotationDefinitions = Definitions;
			if (!ConfigureGuiCompilation(Processing, Error))
			{
				return {};
			}
			LocalOptions.Engine.CompileProcessing.Registry = std::move(Processing.Registry);
			auto State = std::shared_ptr<FState>(new FState(LocalOptions),
			    [](FState *Value)
			{
				if (Value->Thread == std::this_thread::get_id())
				{
					delete Value;
				}
				else
				{
					auto &Queue = Retired();
					std::lock_guard<std::mutex> Guard(Queue.Mutex);
					Queue.States.emplace_back(Value);
				}
			});
			State->Bridge->Services.Invoke =
			    [Owner = std::weak_ptr<FState>(State)](auto Function, auto &Arguments, auto &Out, auto &Error)
			{
				const auto State = Owner.lock();
				if (!State)
				{
					Error = "gui.script_context_closed";
					return false;
				}
				return State->Call(std::string(Function), Arguments, Out, Error);
			};
			State->Bridge->Services.IsCurrentProgram =
			    [Owner = std::weak_ptr<FState>(State)](const auto &Program, auto Generation)
			{
				const auto State = Owner.lock();
				if (!State)
				{
					return false;
				}
				const auto Current = State->Session.GetDebugProgram();
				return Current.Generation == Generation && Current.Owner.lock() == Program.lock();
			};
			State->Bridge->Root = Script.Root;
			State->Bridge->Find = Script.FindElement;
			std::unique_ptr<VeroScriptSession> RegistrationSession;
			if (Environment)
			{
				RegistrationSession = std::make_unique<VeroScriptSession>(*State);
			}
			auto &Bindings = RegistrationSession ? *RegistrationSession : State->Session;
			if (!RegisterBindings(Bindings.GetBindingRegistry(), State->Bridge, Error))
			{
				return {};
			}
			auto Config = LocalOptions.Engine;
			const auto FrameworkPath = GetGuiScriptLibraryPath();
			for (const auto &Import : Script.Imports)
			{
				Config.SourceOverlays.push_back({Import.Identity, Import.Text});
			}
			const bool bExternal = !Script.bInline && !Script.Source.Text.empty();
			if (bExternal)
			{
				Config.SourceOverlays.push_back({Script.Source.Identity, Script.Source.Text});
			}
			if (Environment)
			{
				auto Description = Environment->GetDescription();
				std::uint32_t Offset = 0;
				Description.Bindings = MergeGuiScriptCatalog(
				    Description.Bindings, std::make_shared<const FHostBindingRegistry>(Bindings.GetBindingRegistry()),
				    Offset, Error);
				if (!Description.Bindings)
				{
					return {};
				}
				for (auto &Type : State->Bridge->Types)
				{
					Type.Value += Offset;
				}
				for (auto &Type : State->Bridge->Enums)
				{
					Type.Value += Offset;
				}
				Config.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
				if (!Config.ModuleEnvironment)
				{
					return {};
				}
			}
			if (!ConfigureGuiScriptEngine(Config, Error))
			{
				return {};
			}
			if (!State->Session.ApplyConfig(Config, &Error))
			{
				return {};
			}
			State->Type = Script.bComponent ? Script.Type : "VsguiGeneratedPage";
			const std::string Body = Script.bInline ? Script.Source.Text : std::string{};
			const auto Label = PathUtil::ToUtf8(
			    std::filesystem::absolute(PathUtil::FromUtf8(Script.Location.Source + ".gui.generated.vs")));
			State->Generated = Generate(Script, State->Type, Body, FrameworkPath, {});
			State->Generated.Label = Label;
			auto Loaded = State->Session.LoadSource(State->Generated.Text, Label);
			if (!Loaded.bSuccess)
			{
				Diagnostic = State->Generated.Diagnostic(Loaded);
				return {};
			}
			std::map<std::string, FInstanceMethodInfo> Names;
			if (!MethodNames(State->Session, bExternal ? Script.Type : State->Type, Names, Error))
			{
				return {};
			}
			std::unordered_set<std::string> Members;
			if (bExternal)
			{
				std::vector<FLoadedFieldInfo> Fields;
				std::vector<FInstanceMethodInfo> AllMethods;
				State->Session.EnumerateFields(MetadataType(Script.Type), Fields);
				State->Session.EnumerateInstanceMethods(MetadataType(Script.Type), AllMethods);
				for (const auto &Field : Fields)
				{
					Members.insert(Field.Name);
				}
				for (const auto &Method : AllMethods)
				{
					Members.insert(Method.Name);
				}
			}
			auto Result = std::make_shared<FScriptContext>();
			Result->State = State;
			std::vector<std::pair<std::string, FMarkupLocation>> Methods;
			std::vector<std::pair<std::string, FMarkupLocation>> Entries;
			for (std::size_t Index = 0; Index < Expressions.size(); ++Index)
			{
				const auto &Expression = Expressions[Index];
				DiagnosticGuard.Location = Expression.Location;
				const auto Name = "__VSGuiCallback" + std::to_string(Index);
				std::string BodyText = Expression.Text;
				if (!Expression.bCode)
				{
					const auto Found = Names.find(Expression.Text);
					if (Found == Names.end())
					{
						Error = "gui.gui_method_required: " + Expression.Text;
						return {};
					}
					const auto &Method = Found->second;
					BodyText = (bExternal ? "context." : "") + Method.Name + "(";
					if (Expression.Event == Platform::EEventType::None && Method.ParamCount != 0)
					{
						Error = "gui.getter_requires_no_arguments";
						return {};
					}
					if (Expression.Event != Platform::EEventType::None)
					{
						if (Method.ParamCount == 1)
						{
							BodyText += !Method.ParamTypeNames.empty() && Method.ParamTypeNames[0] == "VS.Gui.Event"
							    ? "event"
							    : "sender";
						}
						else if (Method.ParamCount == 2)
						{
							BodyText += "sender, event";
						}
						else if (Method.ParamCount != 0)
						{
							Error = "gui.routed_event_signature_mismatch";
							return {};
						}
					}
					BodyText += ")";
				}
				else
				{
					if (!RewriteMarkupExpression(Expression, bExternal, Members, BodyText, Error))
					{
						return {};
					}
				}
				if (Expression.Event == Platform::EEventType::None)
				{
					Methods.emplace_back(ScriptType(Expression.Property) + " " + Name +
					        "(VS::Gui::Widget self)\n{\nreturn (" + BodyText + ");\n}",
					    Expression.Location);
					Entries.emplace_back(
					    ScriptType(Expression.Property) + " " + Name + "(" + State->Type +
					        " context, VS::Gui::__NativeWidget nativeSelf) { "
					        "int previousDepth=VS::Gui::PropertyEvaluation::Depth;"
					        "int previousSafeDepth=VS::Gui::PropertyEvaluation::SafeDepth;try{"
					        "VS::Gui::PropertyEvaluation::Depth=previousDepth+1;"
					        "VS::Gui::PropertyEvaluation::SafeDepth=previousSafeDepth+1;VS::Gui::Widget self = "
					        "VS::Gui::Widget::VsguiWrap(nativeSelf); return context." +
					        Name +
					        "(self); } finally { VS::Gui::PropertyEvaluation::Depth=previousDepth;"
					        "VS::Gui::PropertyEvaluation::SafeDepth=previousSafeDepth; } }\n",
					    Expression.Location);
				}
				else
				{
					if (!Expression.bCode)
					{
						BodyText += ";";
					}
					Methods.emplace_back(
					    "void " + Name + "(VS::Gui::Widget self, VS::Gui::Widget sender, VS::Gui::Event event)\n{\n" +
					        "VS::Gui::Widget arg0 = sender; VS::Gui::Event arg1 = event;\n" + BodyText + "\n}",
					    Expression.Location);
					Entries.emplace_back(
					    "void " + Name + "(" + State->Type +
					        " context, VS::Gui::__NativeWidget nativeSelf, VS::Gui::__NativeWidget nativeSender, "
					        "VS::Gui::__NativeEvent nativeEvent) { VS::Gui::Widget self = "
					        "VS::Gui::Widget::VsguiWrap(nativeSelf); "
					        "VS::Gui::Widget sender = VS::Gui::Widget::VsguiWrap(nativeSender); context." +
					        Name + "(self, sender, VS::Gui::Event(nativeEvent, sender)); }\n",
					    Expression.Location);
				}
				Result->Methods[FScriptContext::Key(Expression)] = Name;
			}
			State->Generated = Generate(Script, State->Type, Body, FrameworkPath, Methods);
			State->Generated.Label = Label;
			for (const auto &Entry : Entries)
			{
				State->Generated.Append(Entry.first, Entry.second);
			}
			Loaded = State->Session.LoadSource(State->Generated.Text, Label);
			if (!Loaded.bSuccess)
			{
				Diagnostic = State->Generated.Diagnostic(Loaded);
				return {};
			}
			const auto Initialized = State->Session.Run(ERunMode::EvalOnce);
			if (!Initialized.bSuccess || Initialized.bExitRequested)
			{
				Diagnostic = State->Generated.Diagnostic(Initialized);
				return {};
			}
			State->Bridge->bRootPending = Script.bComponent;
			FValue Object;
			const auto Created = State->Session.CallFunction("__VSGuiCreateContext", {}, &Object);
			if (!Created.bSuccess || Created.bExitRequested)
			{
				Diagnostic = State->Generated.Diagnostic(Created);
				return {};
			}
			if (!State->Session.RetainValue(Object, State->Object, Error))
			{
				return {};
			}
			Error.clear();
			bSuccess = true;
			return Result;
		};
	}

	void CollectMarkupScripts()
	{
		std::vector<std::unique_ptr<FState>> States;
		auto &Queue = Retired();
		{
			std::lock_guard<std::mutex> Guard(Queue.Mutex);
			for (auto Item = Queue.States.begin(); Item != Queue.States.end();)
			{
				if ((*Item)->Thread == std::this_thread::get_id())
				{
					States.push_back(std::move(*Item));
					Item = Queue.States.erase(Item);
				}
				else
				{
					++Item;
				}
			}
		}
	}
} // namespace VeroScript::Gui

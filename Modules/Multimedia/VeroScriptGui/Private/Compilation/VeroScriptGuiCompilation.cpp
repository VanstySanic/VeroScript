// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiCompilation.h"
#include "VeroScriptGuiSchema.h"
#include "VeroScriptGuiSource.h"
#include "VeroScriptGuiMarkupGeneration.h"
#include "VeroScriptGuiStyle.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptLexer.h"
#include "Bindings/VeroScriptGuiScriptLibrary.h"
#include <algorithm>
#include <charconv>
#include <set>
#include <stdexcept>

namespace VeroScript::Gui
{
	namespace
	{
		constexpr const char* ProcessorId = "veroscript.gui.declarations";

		std::string ShortName(const std::string& Name)
		{
			const auto Separator = Name.rfind('.');
			return Separator == std::string::npos ? Name : Name.substr(Separator + 1);
		}

		std::string Namespace(const std::string& Name)
		{
			const auto Separator = Name.rfind('.');
			return Separator == std::string::npos ? std::string{} : Name.substr(0, Separator);
		}

		std::string FunctionType(const FGuiEventSchema& Event)
		{
			std::string Result = "fn(";
			for (const auto& Type : Event.ParameterTypes)
			{
				if (Result.back() != '(')
				{
					Result += ',';
				}
				Result += GuiSourceType(Type);
			}
			return Result + ")->" + GuiSourceType(Event.ReturnType);
		}

		const FAnnotationDeclarationView* Declaration(const FCompileProcessingContext& Context, const std::string& Id)
		{
			const auto Found = std::find_if(Context.Declarations.begin(), Context.Declarations.end(),
			    [&](const auto& Value)
			{
				return Value.Id == Id;
			});
			return Found == Context.Declarations.end() ? nullptr : &*Found;
		}

		bool IsOwn(const FCompileProcessingContext& Context, const FGuiTypeSchema& Type, const std::string& Id)
		{
			const auto* Found = Declaration(Context, Id);
			return Found && Found->OwnerId == Type.DeclarationId;
		}

		bool HasMethod(const FCompileProcessingContext& Context, const FGuiTypeSchema& Type, const std::string& Name)
		{
			return std::any_of(Context.Declarations.begin(), Context.Declarations.end(), [&](const auto& Value)
			{
				return Value.OwnerId == Type.DeclarationId && Value.Name == Name;
			});
		}

		enum EPropertyStateFlag : unsigned
		{
			LocalValueFlag = 1,
			StyleValueFlag = 2,
			CachedValueFlag = 4,
			ReadValueFlag = 8,
			ReadingFlag = 16,
			ConstantValueFlag = 32,
			// The default configuration snapshot may still need releasing after invalidation.
			DefaultConfigurationFlag = 64,
			BoundSourceFlag = 128,
		};

		bool ScalarProperty(const std::string& Type)
		{
			return Type == "bool" || Type == "int" || Type == "int64" || Type == "uint64" ||
			    Type == "float" || Type == "double" || Type == "string";
		}

		struct FPropertySlot
		{
			std::string Declare(bool bMutable = false) const
			{
				const std::string Prefix = bMutable ? "mutable " : "";
				return Prefix + (bScalar ? Type : "optional<" + Type + ">") + " " + Name + ";";
			}
			std::string Has() const
			{
				return bScalar ? "((" + State + "&" + std::to_string(Mask) + ")!=0)" : Name + ".has_value()";
			}
			std::string Get() const
			{
				return bScalar ? Name : Name + ".get()";
			}
			std::string Set(const std::string& Value) const
			{
				return Name + "=" + (bScalar ? Value : "optional<" + Type + ">(" + Value + ")") + ";" +
				    (bScalar ? State + "=" + State + "|" + std::to_string(Mask) + ";" : "");
			}
			std::string Reset() const
			{
				const auto Zero = Type == "bool" ? "false"
				    : Type == "string"           ? "\"\""
				    : Type == "float"            ? "0.0f"
				    : Type == "double"           ? "0.0"
				    : Type == "uint64"           ? "0u64"
				    : Type == "int64"            ? "0i64"
				                                 : "0";
				return Name + "=" + (bScalar ? Zero : "optional<" + Type + ">()") + ";" +
				    (bScalar ? State + "=" + State + "&~" + std::to_string(Mask) + ";" : "");
			}

			std::string Type;
			std::string Name;
			bool bScalar;
			std::string State;
			unsigned Mask = 0;
		};

		bool NumericControl(const std::string& Type)
		{
			return Type == "VS.Gui.ProgressBar" || Type == "VS.Gui.Slider" || Type == "VS.Gui.ScrollBar";
		}

		struct FPropertyEmitter
		{

			explicit FPropertyEmitter(const FGuiPropertySchema& Value, bool bNativeScope = false, bool bCacheSources = false)
			    : Property(Value), Type(GuiSourceType(Value.ValueType)), Name(Value.Name),
			      Slot("VsguiProperty" + Name), bScalar(ScalarProperty(Type)),
			      bConstant(bScalar && Value.bBindable), bNativeScope(bNativeScope),
			      bCacheSources(bNativeScope && bCacheSources),
			      Local{Type, Slot + "Local", bScalar, Slot + "State", LocalValueFlag},
			      Style{Type, Slot + "Style", bScalar, Slot + "State", StyleValueFlag},
			      Cache{Type, Slot + "Cache", bScalar, Slot + "State", CachedValueFlag},
			      Read{Type, Slot + "ReadValue", bScalar, Slot + "State", ReadValueFlag}
			{
			}
			std::string DisposalCheck() const
			{
				return bNativeScope ? "(" + NativeEvaluation + "::Widget!=this&&IsDisposed())"
				                    : "IsDisposed()";
			}

			std::string Flag(unsigned Mask) const
			{
				return "((" + Slot + "State&" + std::to_string(Mask) + ")!=0)";
			}
			std::string Mark(unsigned Mask, bool Value) const
			{
				return Slot + "State=" + Slot + "State" + (Value ? "|" : "&~") + std::to_string(Mask) + ";";
			}
			std::string ConstantCurrent() const
			{
				return Flag(ConstantValueFlag) + "&&(!" + Flag(DefaultConfigurationFlag) + "||" +
				    (bNativeScope ? "(" + NativeEvaluation + "::Widget==this&&" + Evaluation +
				                "::NativeSourcesStable)||"
				                  : "") +
				    "this." + Name + ".has_same_configuration(ref " + Slot + "ConfigurationSnapshot))";
			}
			std::string Resolve() const
			{
				std::string Code;
				if (Property.bBindable)
				{
					Code += "found=" + Slot + "Binding.try_get(ref value);";
				}
				Code += "if(!found){if(" + Local.Has() + "){value=" + Local.Get() + ";}else if(" +
				    Style.Has() + "){value=" + Style.Get() + ";}else{";
				Code += Property.bBindable
				    ? "found=this." + Name +
				        ".try_get(ref value);assert(found,\"std::attribute has no value\");"
				    : "value=this." + Name + ";";
				Code += "}}";
				return Code;
			}
			// Only primitive slots use this fragment inline. Nontrivial values keep
			// the original function boundaries and language-copy behavior.
			std::string ReadScalar() const
			{
				// Publish recursion state before invoking a getter. Combine flag writes
				// without moving successful-read state across the exception boundary.
				const auto BeginRead = bConstant
				    ? Slot + "State=(" + Slot + "State|" + std::to_string(ReadingFlag) + ")&~" +
				        std::to_string(ConstantValueFlag) + ";"
				    : Mark(ReadingFlag, true);
				std::string Code = "assert(!" + Flag(ReadingFlag) + ",\"Recursive GUI property getter\");"
				                                                    "int __vsguiPreviousDepth=" +
				    Evaluation + "::Depth;try{" +
				    BeginRead + Evaluation + "::Depth=__vsguiPreviousDepth+1;";
				if (bConstant)
				{
					if (bCacheSources)
					{
						Code += "bool constantSource=false;";
					}
					if (bCacheSources)
					{
						// A source snapshot proves configuration identity, never the getter's
						// result. Bound attributes still run once in every evaluation epoch.
						Code += "if(!(" + NativeEvaluation + "::Widget==this&&" + Evaluation +
						    "::NativeSourcesStable&&" + Flag(BoundSourceFlag) + ")){";
					}
					Code += "bool bindingBound=" + Slot +
					    "Binding.is_bound();bool defaultSource=!bindingBound&&!" + Slot +
					    "Binding.is_set()&&!" + Local.Has() + "&&!" + Style.Has() +
					    ";" + std::string(bCacheSources ? "" : "bool ") + "constantSource=!bindingBound&&(!defaultSource||!this." + Name +
					    ".is_bound());" +
					    "if(constantSource&&defaultSource){" + Slot + "ConfigurationSnapshot=this." +
					    Name + ";" + Mark(DefaultConfigurationFlag, true) + "}else if(" +
					    Flag(DefaultConfigurationFlag) + "){" + Slot + "ConfigurationSnapshot.reset();" +
					    Mark(DefaultConfigurationFlag, false) + "}";
					if (bCacheSources)
					{
						Code += '}';
					}
				}
				Code += Type + " value=" +
				    (Type == "bool"            ? "false"
				            : Type == "string" ? "\"\""
				            : Type == "float"  ? "0.0f"
				            : Type == "double" ? "0.0"
				            : Type == "uint64" ? "0u64"
				            : Type == "int64"  ? "0i64"
				                               : "0") +
				    ";bool found=false;" + Resolve() +
				    (bConstant ? Read.Name + "=value;" + Slot + "State=" +
				                (bCacheSources ? "(" + Slot + "State&~" + std::to_string(BoundSourceFlag) + ")" : Slot + "State") +
				                "|" + std::to_string(ReadValueFlag) + "|(constantSource?" +
				                std::to_string(ConstantValueFlag) + ":" +
				                std::to_string(bCacheSources ? BoundSourceFlag : 0) + ");"
				               : Read.Set("value")) +
				    Slot +
				    "ReadEpoch=" + Evaluation + "::Epoch;";
				return Code + "}finally{" + Mark(ReadingFlag, false) + Evaluation + "::Depth=__vsguiPreviousDepth; }";
			}
			std::string Notify(const std::string& Value, const std::string& EmptyHookProof = {}) const
			{
				return "if(!" + Cache.Has() + "||!(" + Cache.Get() + "==" + Value + ")){" +
				    "VsguiControlConfigurationDirty=true;" +
				    (EmptyHookProof.empty() ? "" : "if(!" + EmptyHookProof + ")") +
				    "On" + Name + "Changed();if(!" + DisposalCheck() +
				    "){" + Cache.Set(Value) + "}}";
			}
			std::string Synchronize(const std::string& NativeStable = {}, bool bDefaultHook = false) const
			{
				if (!bConstant)
				{
					return "VsguiRefresh" + Name + "();";
				}
				std::string Code = "{if(" + Slot + "ReadEpoch!=" + Evaluation + "::Epoch){if(" +
				    ConstantCurrent() + "){" + Slot + "ReadEpoch=" + Evaluation +
				    "::Epoch;}else{" + ReadScalar() + "}}" + Type + " value=" + Read.Get() +
				    ";" + Notify("value", bDefaultHook ? NativeStable : std::string{}) + "}";
				if (bNativeScope)
				{
					const auto Proof = NativeEvaluation + "::Widget==this&&" + Evaluation + "::NativeSourcesStable";
					if (!NativeStable.empty())
					{
						Code += NativeStable + "=" + Proof + ";";
					}
					Code = "if(!(" + (NativeStable.empty() ? Proof : NativeStable) + "&&((" + Slot + "State&" +
					    std::to_string(ConstantValueFlag | CachedValueFlag) + ")==" +
					    std::to_string(ConstantValueFlag | CachedValueFlag) + "))){" +
					    Code + "}";
				}
				return Code;
			}
			const FGuiPropertySchema& Property;
			std::string Type, Name, Slot;
			const std::string Evaluation = "VS::Gui::PropertyEvaluation";
			const std::string NativeEvaluation = "VS::Gui::Internal::NativeEvaluation";
			bool bScalar, bConstant, bNativeScope, bCacheSources;
			FPropertySlot Local, Style, Cache, Read;
		};

		std::string PropertyMembers(const FCompileProcessingContext& Context, const FGuiTypeSchema& Owner,
		    const FGuiPropertySchema& Property, bool bNativeScope)
		{
			const FPropertyEmitter E(Property, bNativeScope, NumericControl(Owner.CanonicalName));
			const auto& Name = E.Name;
			const auto& Type = E.Type;
			const auto& Slot = E.Slot;
			const auto& Evaluation = E.Evaluation;
			const auto& NativeEvaluation = E.NativeEvaluation;
			const auto Mutation = "if(" + E.DisposalCheck() + "){throw \"GUI widget is disposed\";}assert(" +
			    Evaluation + "::Depth==0,\"GUI mutation is not allowed in a getter\");";
			const auto ClearNotification = Property.bBindable
			    ? "VsguiClearNotifiedBinding(" + GuiQuote(Name) + ");"
			    : std::string{};
			const auto Invalidate = "VsguiInvalidate();" + Evaluation + "::Epoch++;" + Slot + "ReadEpoch=0;" +
			    Evaluation + "::NativeSourcesStable=false;" +
			    (E.bConstant ? E.Mark(ConstantValueFlag | (E.bCacheSources ? BoundSourceFlag : 0), false) : "");
			std::string Code =
			    "private:" + E.Local.Declare() + E.Style.Declare() + E.Cache.Declare() + "mutable int64 " +
			    Slot + "ReadEpoch;" +
			    (E.bScalar ? "mutable int " + Slot + "State;" : "mutable bool " + Slot + "Reading;") +
			    E.Read.Declare(true);
			if (Property.bBindable)
			{
				Code += "std::attribute<" + Type + "> " + Slot + "Binding;";
			}
			if (E.bConstant)
			{
				Code += "mutable std::attribute<" + Type + "> " + Slot + "ConfigurationSnapshot;";
			}
			Code += "public:" + Type + " Get" + Name + "() const{";
			if (E.bConstant && E.bCacheSources)
			{
				Code += "if(" + NativeEvaluation + "::Widget==this&&" + Evaluation +
				    "::NativeSourcesStable&&" + E.Flag(ConstantValueFlag) + "){return " + E.Read.Get() + ";}";
			}
			Code += "if(" + E.DisposalCheck() +
			    "){throw \"GUI widget is disposed\";}if(" + Slot + "ReadEpoch==" + Evaluation +
			    "::Epoch){return " + E.Read.Get() + ";}";
			if (E.bConstant)
			{
				Code += "if(" + E.ConstantCurrent() + "){" + Slot + "ReadEpoch=" + Evaluation +
				    "::Epoch;return " + E.Read.Get() + ";}";
			}
			if (Property.bBindable)
			{
				Code += "if(" + Evaluation + "::SafeDepth==0&&(" + Slot + "Binding.is_bound()||(!" + Slot +
				    "Binding.is_set()&&!" + E.Local.Has() + "&&!" + E.Style.Has() + "&&this." + Name +
				    ".is_bound()))){if(!" + E.Read.Has() +
				    "){throw \"GUI binding has not been initialized\";}return " + E.Read.Get() + ";}";
			}
			if (E.bScalar)
			{
				Code += "VsguiRead" + Name + "();return " + E.Read.Get() + ";}private:void VsguiRead" + Name +
				    "() const{" + E.ReadScalar() + "}";
			}
			else
			{
				Code += "assert(!" + Slot + "Reading,\"Recursive GUI property getter\");"
				                            "int __vsguiPreviousDepth=" +
				    Evaluation + "::Depth;try{" + Slot +
				    "Reading=true;" + Evaluation + "::Depth=__vsguiPreviousDepth+1;" + Type + " value=VsguiResolve" +
				    Name + "();" + E.Read.Set("value") + Slot + "ReadEpoch=" + Evaluation +
				    "::Epoch;return value;}finally{" + Slot + "Reading=false;" + Evaluation +
				    "::Depth=__vsguiPreviousDepth;}}private:" + Type + " VsguiResolve" + Name + "() const{";
				if (Property.bBindable)
				{
					Code += "if(" + Slot + "Binding.is_set()){auto value=" + Slot +
					    "Binding.try_get();if(value.has_value()){return value.get();}}";
				}
				Code += "if(" + E.Local.Has() + "){return " + E.Local.Get() + ";}if(" + E.Style.Has() +
				    "){return " + E.Style.Get() + ";}return this." + Name +
				    (Property.bBindable ? ".get()" : "") + ";}";
			}
			Code += "public:void Set" + Name + "(" + Type + " value){" + Mutation + ClearNotification + Invalidate +
			    E.Local.Set("value") + E.Read.Set("value") +
			    (Property.bBindable ? Slot + "Binding.reset();" : "") + "}" + "void Clear" + Name +
			    "(){" + Mutation + ClearNotification + Invalidate + E.Local.Reset() +
			    (Property.bBindable ? Slot + "Binding.reset();" : "") + "}";
			if (Property.bBindable)
			{
				Code += "bool Is" + Name + "Bound() const{return " + Slot + "Binding.is_bound()||(!" + Slot +
				    "Binding.is_set()&&!" + E.Local.Has() + "&&!" + E.Style.Has() + "&&this." + Name +
				    ".is_bound());}" + "void Set" + Name + "(std::attribute<" + Type + "> value){" +
				    Mutation + ClearNotification + Invalidate + Slot + "Binding=value;" + E.Local.Reset() + "}";
				const auto WidgetType = GuiSourceType(Owner.Name);
				Code += "void Set" + Name + "(VS::Gui::Binding<" + Type + "> value){weak<" + WidgetType +
				    "> owner=weak<" + WidgetType + ">(this);VsguiBind<" + Type + ">(" + GuiQuote(Name) +
				    ",value,[owner](std::attribute<" + Type + "> configuration)->void{owner.lock().Set" + Name +
				    "(configuration);});}";
			}
			if (Property.bStyle)
			{
				Code += "void VsguiSetStyle" + Name + "(" + Type + " value){if(" + E.Style.Has() + "&&" +
				    E.Style.Get() + "==value){return;}" + Mutation + Invalidate + E.Style.Set("value") +
				    "}" + "void VsguiClearStyle" + Name + "(){" + Mutation + "if(!" + E.Style.Has() +
				    "){return;}" + Invalidate + E.Style.Reset() + "}";
			}
			Code += "void VsguiRefresh" + Name + "(){" + Type + " value=Get" + Name + "();" +
			    E.Notify("value") + "}" + "protected:void VsguiRelease" + Name + "(){" + E.Local.Reset() +
			    E.Style.Reset() + E.Cache.Reset() + E.Read.Reset() + Slot + "ReadEpoch=0;";
			if (E.bConstant)
			{
				Code += E.Mark(ConstantValueFlag | (E.bCacheSources ? BoundSourceFlag : 0), false) + Slot + "ConfigurationSnapshot.reset();" +
				    E.Mark(DefaultConfigurationFlag, false);
			}
			if (Property.bBindable)
			{
				Code += Slot + "Binding.reset();this." + Name + ".reset();";
			}
			Code += "}";
			if (!HasMethod(Context, Owner, "On" + Name + "Changed"))
			{
				Code += "protected:virtual void On" + Name + "Changed(){}";
			}
			return Code;
		}

		std::string EventMembers(const FGuiTypeSchema& Type, const FGuiEventSchema& Event)
		{
			const auto& Name = Event.Name;
			const auto Handler = FunctionType(Event);
			const auto Slot = "VsguiEvent" + Name;
			if (Event.Kind == EGuiEventKind::Single)
			{
				return "private: int64 " + Slot + "Revision; public: void Set" + Name + "(" + Handler +
				    " handler){" + Name + ".bind(handler);" + Slot + "Revision++;}void Clear" + Name +
				    "(){" + Name + ".unbind();" + Slot + "Revision++;}int64 VsguiGet" + Name +
				    "Revision(){return " + Slot + "Revision;}void VsguiClear" + Name +
				    "Revision(int64 revision){if(" + Slot + "Revision==revision){Clear" + Name + "();}}";
			}
			const auto Widget = GuiSourceType(Type.Name);
			return "private: array<" + Handler + "> " + Slot + "Handlers; array<VS::Gui::Subscription> " +
			    Slot + "Connections; public: VS::Gui::Subscription Add" + Name + "(" + Handler +
			    " handler){auto connection=VS::Gui::Subscription();" + Slot + "Handlers.push(handler);" +
			    Slot + "Connections.push(connection);" + Name + ".add(handler);auto observer=weak<" +
			    Widget + ">(this);connection.VsguiSetDisconnect([observer]()->void{" + Widget +
			    " owner=observer.lock();if(owner!=nullptr){owner.VsguiPrune" + Name +
			    "();}});return connection;}void Remove" + Name + "(" + Handler +
			    " handler){for(int64 i=0;i<len(" + Slot + "Handlers);i++){if(" + Slot +
			    "Connections[i].IsConnected()&&" + Slot + "Handlers[i]==handler){" + Slot +
			    "Connections[i].Disconnect();break;}}VsguiPrune" + Name + "();}void Clear" + Name +
			    "(){auto previous=" + Slot + "Connections;" + Slot + "Handlers=array<" + Handler + ">();" +
			    Slot + "Connections=array<VS::Gui::Subscription>();" + Name +
			    ".clear();for(VS::Gui::Subscription connection:previous){connection.Disconnect();}}void "
			    "VsguiPrune" +
			    Name + "(){array<" + Handler + "> handlers=array<" + Handler +
			    ">();array<VS::Gui::Subscription> connections=array<VS::Gui::Subscription>();" + Name +
			    ".clear();for(int64 i=0;i<len(" + Slot + "Handlers);i++){auto connection=" + Slot +
			    "Connections[i];if(connection.IsConnected()){auto handler=" + Slot +
			    "Handlers[i];handlers.push(handler);connections.push(connection);" + Name +
			    ".add(handler);}}" + Slot + "Handlers=handlers;" + Slot + "Connections=connections;}";
		}

		std::string BuilderPropertyMembers(const FGuiTypeSchema& Type,
		    const FCompileProcessingContext* Context = nullptr,
		    const std::string& Owner = {})
		{
			const auto Builder = ShortName(Type.Name) + "Builder";
			const auto Widget = GuiSourceType(Type.Name);
			std::string Methods = "public:";
			for (const auto& Property : Type.Properties)
			{
				const auto& Name = Property.Name;
				if (Context &&
				    std::any_of(Context->Declarations.begin(), Context->Declarations.end(), [&](const auto& D)
				{
					return D.OwnerId == Owner && D.bGenerated && D.Name == Name;
				}))
				{
					continue;
				}

				const auto ValueType = GuiSourceType(Property.ValueType);
				for (const auto& Argument :
				    Property.bBindable
				        ? std::vector<std::string>{ValueType, "std::attribute<" + ValueType + ">", "VS::Gui::Binding<" + ValueType + ">"}
				        : std::vector<std::string>{ValueType})
				{
					Methods += Builder + " " + Name + "(" + Argument + " value){" + Builder +
					    " result=this;result.VsguiAppend([value](" + Widget +
					    " widget)->void{widget.Set" + Name + "(value);});return result;}";
				}
			}
			return Methods;
		}

		FGuiTypeSchema EarlyBuilderSchema(const FCompileProcessingContext& Context)
		{
			FGuiTypeSchema Type;
			const auto Widget = std::find_if(
			    Context.Declarations.begin(), Context.Declarations.end(), [](const auto& Item)
			{
				return Item.Name == "VS.Gui.Widget" && Item.Target == EAnnotationTarget::Class;
			});
			if (Widget == Context.Declarations.end())
			{
				return Type;
			}
			std::set<std::string> Setters, Shadowed;
			for (const auto& D : Context.Declarations)
			{
				if (D.OwnerId == Widget->Id && D.Parameters.size() == 1 && D.Name.compare(0, 3, "Set") == 0)
				{
					Setters.insert(D.Name.substr(3));
				}
				if (std::any_of(D.Metadata.Annotations.begin(), D.Metadata.Annotations.end(),
				        [](const auto& A)
				{
					return AnnotationNamesEqual(A.Name, "@GuiProperty", false);
				}))
				{
					Shadowed.insert(ShortName(D.Name));
				}
			}
			constexpr const char* ValueTypes[]{
			    "VS.Gui.Length", "VS.Gui.Insets", "double", "VS.Gui.Layout", "VS.Gui.Visibility",
			    "VS.Gui.HitTestMode", "bool", "VS.Gui.Color", "string", "VS.Gui.Cursor"};
			static_assert(std::size(ValueTypes) == std::variant_size_v<FPropertyValue>);
			for (std::size_t Index = 0; Index < static_cast<std::size_t>(EProperty::Count); ++Index)
			{
				const auto& Definition = GetPropertyDefinition(static_cast<EProperty>(Index));
				// Custom shadows are resolved in Members; common builtin methods
				// can be emitted together with the complete Builder type in Types.
				if (!Setters.count(Definition.Name) || Shadowed.count(Definition.Name))
				{
					continue;
				}
				FGuiPropertySchema Property;
				Property.Name = Definition.Name;
				Property.ValueType = ValueTypes[Definition.DefaultValue.index()];
				Property.bBuiltin = true;
				Property.bBindable = true;
				Type.Properties.push_back(std::move(Property));
			}
			return Type;
		}

		std::string BuilderMembers(const FGuiTypeSchema& Type, const FCompileProcessingContext& Context,
		    const std::string& Owner)
		{
			const auto Builder = ShortName(Type.Name) + "Builder";
			const auto Widget = GuiSourceType(Type.Name);
			std::string Fields =
			    "private: array<fn(" + Widget +
			    ")->void> VsguiActions;array<VS::Gui::Widget> VsguiChildren;bool VsguiHasChildren;";
			std::string Methods = "public:";
			Methods +=
			    Builder + " operator[](array<VS::Gui::Widget> children){" + Builder +
			    " result=this;array<VS::Gui::Widget> snapshot=array<VS::Gui::Widget>();for(VS::Gui::Widget "
			    "child:children){snapshot.push(child);}result.VsguiChildren=snapshot;result.VsguiHasChildren="
			    "true;return result;}";
			Methods += Builder + " Id(string value){" + Builder + " result=this;result.VsguiAppend([value](" +
			    Widget + " widget)->void{widget.SetId(value);});return result;}";
			Methods += BuilderPropertyMembers(Type, &Context, Owner);
			for (const auto& Event : Type.Events)
			{
				Methods += Builder + " On" + Event.Name + "(" + FunctionType(Event) + " handler){" + Builder +
				    " result=this;result.VsguiAppend([handler](" + Widget + " widget)->void{widget." +
				    (Event.Kind == EGuiEventKind::Single ? "Set" : "Add") + Event.Name +
				    "(handler);});return result;}";
			}
			Methods +=
			    "void VsguiAppend(fn(" + Widget + ")->void action){array<fn(" + Widget +
			    ")->void> next=array<fn(" + Widget + ")->void>();for(fn(" + Widget +
			    ")->void existing:VsguiActions){next.push(existing);}next.push(action);VsguiActions=next;}";
			Methods += Widget + " operator " + Widget +
			    "(){int __vsguiPreviousDepth=VS::Gui::Construction::Depth;bool completed=false;try{"
			    "VS::Gui::Construction::Begin();" +
			    Widget +
			    " widget=" + Widget + "();widget.VsguiPrepareTemplate();";
			if (!Type.DefaultId.empty())
			{
				Methods += "widget.SetId(" + GuiQuote(Type.DefaultId) + ");";
			}
			Methods += "for(fn(" + Widget +
			    ")->void "
			    "action:VsguiActions){action(widget);}if(VsguiHasChildren){widget."
			    "SetChildren(VsguiChildren);}widget.VsguiFinalizeConfiguration();"
			    "VS::Gui::Construction::Complete(widget);completed=true;return "
			    "widget;}finally{if(VS::Gui::Construction::Depth>__vsguiPreviousDepth)"
			    "{VS::Gui::Construction::End(completed);}}}";
			if (Type.Name != "VS.Gui.Widget")
			{
				Methods += "VS::Gui::Widget operator VS::Gui::Widget(){" + Widget + " widget=this;return widget;}";
			}
			return Fields + Methods;
		}

		std::string StyleMembers(const FCompileProcessingContext& Context, const FGuiTypeSchema& Type,
		    FCompileProcessingOutput& Output, const FGuiCompilationConfiguration& Configuration)
		{
			std::string Register = "public: void VsguiRegisterStyleProperties() override{";
			std::string Pull = "public: void VsguiPullStyles() override{";
			std::string Declarations;
			bool bCacheable = true, bHasProperties = false;
			for (const auto& Property : Type.Properties)
			{
				if (Property.bBuiltin || !Property.bStyle)
				{
					continue;
				}
				const auto ValueType = GuiSourceType(Property.ValueType);
				bHasProperties = true;
				bCacheable &= ScalarProperty(ValueType);
				const std::map<std::string, EStyleValueType> Kinds{{"string", EStyleValueType::String},
				    {"std::i18n::text", EStyleValueType::String},
				    {"bool", EStyleValueType::Bool},
				    {"int", EStyleValueType::Int},
				    {"int64", EStyleValueType::Int64},
				    {"uint64", EStyleValueType::UInt64},
				    {"float", EStyleValueType::Float},
				    {"double", EStyleValueType::Double},
				    {"VS::Gui::Length", EStyleValueType::Length},
				    {"VS::Gui::Insets", EStyleValueType::Insets},
				    {"VS::Gui::Color", EStyleValueType::Color}};
				const auto Found = Kinds.find(ValueType);
				auto Kind = Found == Kinds.end() ? EStyleValueType::Enum : Found->second;
				const auto Codec =
				    std::find_if(Configuration.ValueCodecs.begin(), Configuration.ValueCodecs.end(), [&](const auto& C)
				{
					return GuiSourceType(C.Type) == ValueType && !C.ParseStyle.empty();
				});
				if (Codec != Configuration.ValueCodecs.end())
				{
					bCacheable = false;
					Kind = EStyleValueType::Raw;
				}
				std::string Names, Values, Decoder;
				if (Found == Kinds.end() && Codec == Configuration.ValueCodecs.end())
				{
					const auto Enum = std::find_if(
					    Context.Declarations.begin(), Context.Declarations.end(), [&](const auto& D)
					{
						return D.Target == EAnnotationTarget::Enum && GuiSourceType(D.Name) == ValueType;
					});
					if (Enum == Context.Declarations.end())
					{
						FScriptError Error;
						Error.MessageId = "gui.style_codec_required";
						Error.Message = "Style property requires a supported value codec: " + ValueType;
						Error.Location.FileLabel = Property.Location.FileLabel;
						Error.Location.Line = Property.Location.Line;
						Error.Location.Column = Property.Location.Column;
						Output.ReportDiagnostic(std::move(Error));
						continue;
					}
					Decoder = "protected: " + ValueType + " VsguiDecodeStyle" + Property.Name + "(int64 value){";
					std::size_t Index = 0;
					for (const auto& Member : Context.Declarations)
					{
						if (Member.OwnerId != Enum->Id || Member.Target != EAnnotationTarget::EnumMember)
						{
							continue;
						}
						const auto Symbol = ValueType + "::" + ShortName(Member.Name);
						if (!Names.empty())
						{
							Names += ',';
							Values += ',';
						}
						Names += GuiQuote(ShortName(Member.Name)) + ',' + GuiQuote(Symbol);
						Values += std::to_string(Index) + ',' + std::to_string(Index);
						Decoder += "if(value==" + std::to_string(Index++) + "){return " + Symbol + ";}";
					}
					Decoder += "throw \"Invalid GUI style enum\";}";
				}
				const char* Suffixes[]{"String", "Bool", "Int", "Int64", "UInt64", "Float",
				    "Double", "Length", "Insets", "Color", "Enum", "String"};
				Register += "{array<string> names={" + Names + "};array<int64> values={" + Values +
				    "};this.__GetNativeHandle().RegisterStyleProperty(" + GuiQuote(Property.Name) + ',' +
				    GuiQuote(Property.Mark) + ',' + std::to_string(static_cast<int>(Kind)) + ",names,values);}";
				std::string Read = "this.__GetNativeHandle().GetStyle" + std::string(Suffixes[static_cast<int>(Kind)]) +
				    '(' + GuiQuote(Property.Name) + ')';
				if (Codec != Configuration.ValueCodecs.end())
				{
					Read = GuiSourceType(Codec->ParseStyle) + '(' + Read + ')';
				}
				else if (Kind == EStyleValueType::Enum)
				{
					Read = "VsguiDecodeStyle" + Property.Name + '(' + Read + ')';
				}
				else if (ValueType == "std::i18n::text")
				{
					Read = ValueType + '(' + Read + ')';
				}
				Pull += "if(this.__GetNativeHandle().HasStyleValue(" + GuiQuote(Property.Name) + ")){VsguiSetStyle" +
				    Property.Name + '(' + Read + ");}else{VsguiClearStyle" + Property.Name + "();}";
				if (IsOwn(Context, Type, Property.DeclarationId))
				{
					Declarations += Decoder;
				}
			}
			// Decoder declarations belong at class scope, outside the pull method.
			if (bCacheable && bHasProperties)
			{
				const auto Token = "VsguiStyleMutationToken_" + GuiScopeSuffix(Type.CanonicalName);
				const std::string Header = "public: void VsguiPullStyles() override{";
				Pull = "private:uint64 " + Token + ";" + Header +
				    "uint64 token=this.__GetNativeHandle().GetStyleMutationToken();" +
				    "if(token!=0u64&&token==" + Token + "){return;}" + Pull.substr(Header.size()) + Token +
				    "=token;";
			}
			return Register + '}' + Pull + '}' + Declarations;
		}

		bool HasLifecycleHook(const FCompileProcessingContext& Context, const FGuiSchema& Schema,
		    const FGuiTypeSchema& Type, const char* Name = "OnInitialized")
		{
			if (Type.Name == "VS.Gui.Widget")
			{
				return false;
			}
			if (HasMethod(Context, Type, Name))
			{
				return true;
			}
			for (const auto& Base : Type.Bases)
			{
				const auto* Parent = Schema.FindType(Base);
				if (Parent && HasLifecycleHook(Context, Schema, *Parent, Name))
				{
					return true;
				}
			}
			return false;
		}

		bool HasInheritedImplementation(const FGuiSchema& Schema, const FGuiTypeSchema& Type,
		    const std::set<std::string>& Owners)
		{
			if (Type.Name == "VS.Gui.Widget")
			{
				return false;
			}
			if (Owners.count(Type.DeclarationId))
			{
				return true;
			}
			for (const auto& Base : Type.Bases)
			{
				const auto* Parent = Schema.FindType(Base);
				if (Parent && HasInheritedImplementation(Schema, *Parent, Owners))
				{
					return true;
				}
			}
			return false;
		}

		bool CanSuspendSynchronization(const FCompileProcessingContext& Context, const FGuiSchema& Schema,
		    const FGuiTypeSchema& Type, const FGuiCompilationConfiguration& Configuration,
		    const std::set<std::string>& Controls)
		{
			if (Type.CanonicalName == "VS.Gui.Widget" || Type.CanonicalName == "VS.Gui.Button" ||
			    Type.CanonicalName == "VS.Gui.Stack")
			{
				return true;
			}
			if (const auto* Source = Declaration(Context, Type.DeclarationId);
			    Source && Source->bFromPackage &&
			    !HasMethod(Context, Type, "VsguiStableProperties_" + GuiScopeSuffix(Type.CanonicalName)))
			{
				// A previously compiled passive base may predate this generated helper.
				return false;
			}
			if (!Controls.count(Type.CanonicalName))
			{
				// An exact registered wrapper may inherit a control's synchronization,
				// but arbitrary helpers/overrides can be called virtually from that path.
				// Only hooks driven by an existing change/lifecycle event are passive.
				if (!Type.bRegistered) return false;
				for (const auto& Member : Context.Declarations)
				{
					if (Member.OwnerId != Type.DeclarationId)
					{
						continue;
					}
					// Only this processor's own generated API is known here. Another
					// extension can generate arbitrary virtual synchronization work.
					if (Member.bGenerated && Member.Id.rfind(std::string("generated:") + ProcessorId + ':', 0) == 0)
					{
						continue;
					}
					// A user constructor may install native periodic callbacks without
					// declaring a synchronization override. Keep that case conservative.
					if (Member.Target == EAnnotationTarget::Constructor && !Member.bDefaulted &&
					    (Member.bHasBody || Member.bFromPackage))
					{
						return false;
					}
					if (Member.Target != EAnnotationTarget::Method)
					{
						continue;
					}
					const auto& Name = Member.Name;
					if (Name == "OnInitialized" || Name == "OnMounted" || Name == "OnUnmounted" ||
					    Name == "OnLayoutChanged" || Name == "OnPaint" || Name == "VsguiItemsChanged")
					{
						continue;
					}
					if (std::any_of(Type.Properties.begin(), Type.Properties.end(),
					        [&](const auto& Property)
					{
						return Name == "On" + Property.Name + "Changed";
					}))
					{
						continue;
					}
					return false;
				}
			}
			for (const auto& Property : Type.Properties)
			{
				if (Property.bBuiltin)
				{
					continue;
				}
				const FPropertyEmitter E(Property);
				if (!E.bConstant && E.Type != "VS::Gui::Color") return false;
				if (Property.bStyle &&
				    std::any_of(Configuration.ValueCodecs.begin(), Configuration.ValueCodecs.end(),
				        [&](const auto& Codec)
				{
					return !Codec.ParseStyle.empty() && GuiSourceType(Codec.Type) == E.Type;
				}))
				{
					return false;
				}
			}
			for (const auto& Base : Type.Bases)
			{
				const auto* Parent = Schema.FindType(Base);
				if (!Parent || !Parent->bWidget ||
				    !CanSuspendSynchronization(Context, Schema, *Parent, Configuration, Controls))
				{
					return false;
				}
			}
			return !Type.Bases.empty();
		}

		std::string SuspensionMembers(const FCompileProcessingContext& Context, const FGuiSchema& Schema,
		    const FGuiTypeSchema& Type,
		    const FGuiCompilationConfiguration& Configuration)
		{
			// These framework controls have no periodic script work beyond the
			// generated primitive properties and their cached native configuration.
			static const std::set<std::string> Controls{
			    "VS.Gui.CheckBox", "VS.Gui.ToggleSwitch", "VS.Gui.ProgressBar", "VS.Gui.Slider",
			    "VS.Gui.ScrollBar", "VS.Gui.TextBox", "VS.Gui.TextArea", "VS.Gui.Grid",
			    "VS.Gui.Splitter", "VS.Gui.RadioButton", "VS.Gui.RadioGroup", "VS.Gui.NumberBox",
			    "VS.Gui.PasswordBox", "VS.Gui.Popup", "VS.Gui.ToolTip",
			    "VS.Gui.ScrollView",
			    "VS.Gui.CollectionItem", "VS.Gui.ListItem", "VS.Gui.Internal.SelectionView", "VS.Gui.ListView",
			    "VS.Gui.TreeItem", "VS.Gui.TreeView", "VS.Gui.TableColumn", "VS.Gui.TableRow", "VS.Gui.TableView",
			    "VS.Gui.MenuItem", "VS.Gui.Dialog"};
			if (!CanSuspendSynchronization(Context, Schema, Type, Configuration, Controls)) return {};
			std::string Ready;
			for (const auto& Property : Type.Properties)
			{
				if (Property.bBuiltin)
				{
					continue;
				}
				const FPropertyEmitter E(Property);
				const bool bColor = E.Type == "VS::Gui::Color";
				if (!IsOwn(Context, Type, Property.DeclarationId))
				{
					continue;
				}
				if (!Ready.empty())
				{
					Ready += "&&";
				}
				Ready += "(" + (bColor ? "!Is" + Property.Name + "Bound()" : E.Flag(ConstantValueFlag)) +
				    "||VsguiHasStableBinding(" + GuiQuote(Property.Name) + "))&&" + E.Cache.Has();
			}
			for (const auto& Base : Type.Bases)
			{
				const auto* Parent = Schema.FindType(Base);
				if (!Parent || Parent->CanonicalName == "VS.Gui.Widget" || Parent->CanonicalName == "VS.Gui.Button" ||
				    Parent->CanonicalName == "VS.Gui.Stack")
				{
					continue;
				}
				if (!Ready.empty())
				{
					Ready += "&&";
				}
				Ready += "VsguiStableProperties_" + GuiScopeSuffix(Parent->CanonicalName) + "()";
			}
			if (Ready.empty())
			{
				Ready = "true";
			}
			const auto Helper = "VsguiStableProperties_" + GuiScopeSuffix(Type.CanonicalName);
			return "protected:bool " + Helper + "()const{return " + Ready +
			    ";}"
			    "public:bool VsguiCanSuspendSynchronization() override{return " +
			    Helper + "()" +
			    "&&VsguiAllowsSourceSnapshot();}"
			    "public:bool VsguiAllowsSourceSnapshot() override{return "
			    "VS::Gui::__NativeWidget::IsScriptType("
			    "fn()->void{VsguiSynchronizeControl();}," +
			    GuiQuote(Type.CanonicalName) + ");}";
		}

		class FProcessor final : public ICompileProcessor
		{
		public:
			explicit FProcessor(std::shared_ptr<const FGuiCompilationConfiguration> Configuration)
			    : Configuration(std::move(Configuration))
			{
			}
			void Process(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output) override
			{
				if (Context.CheckCancelled)
				{
					Context.CheckCancelled();
				}
				if (Context.Phase == ECompileProcessingPhase::Types)
				{
					// Complete common Builder methods in one type operation. User
					// properties and canonical inheritance remain a Members concern.
					auto BuilderSchema = EarlyBuilderSchema(Context);
					for (const auto& Type : Context.Declarations)
					{
						if (Type.ModulePath != Context.ModuleId || Type.Target != EAnnotationTarget::Class)
						{
							continue;
						}
						const bool bRegistered =
						    Type.Name == "VS.Gui.Widget" ||
						    std::any_of(Type.Metadata.Annotations.begin(), Type.Metadata.Annotations.end(),
						        [](const auto& A)
						{
							return AnnotationNamesEqual(A.Name, "@GuiWidget", false);
						});
						if (bRegistered)
						{
							if (Type.bIsStatic || GuiHasTemplateParameters(Type) || Type.bNested || Type.bLocal)
							{
								FScriptError Error;
								Error.MessageId = "gui.schema.declaration";
								Error.Message = "GuiWidget requires a closed, nonstatic namespace-scope class";
								Error.Location.FileLabel = Type.Location.FileLabel;
								Error.Location.Line = Type.Location.Line;
								Error.Location.Column = Type.Location.Column;
								Output.ReportDiagnostic(std::move(Error));
								continue;
							}
							BuilderSchema.Name = Type.Name;
							Output.AddDeclaration({Context.ModuleId, Namespace(Type.Name), {}, Context.Snapshot},
							    (Type.bExported ? "export " : "") + std::string("struct ") +
							        ShortName(Type.Name) + "Builder{" +
							        BuilderPropertyMembers(BuilderSchema) + "};");
						}
					}
					return;
				}
				if (CachedSnapshot != Context.Snapshot || !CachedSchema)
				{
					std::vector<FScriptError> Diagnostics;
					CachedSchema = FGuiSchema::Build(Context.Declarations, Context.SourceMetadata, Diagnostics);
					CachedSnapshot = Context.Snapshot;
					SynchronizationOwners.clear();
					DisposalOwners.clear();
					for (const auto& Declaration : Context.Declarations)
					{
						if (Declaration.Name == "IsDisposed")
						{
							DisposalOwners.insert(Declaration.OwnerId);
						}
						if (Declaration.Name == "VsguiSynchronizeProperties" ||
						    Declaration.Name == "VsguiSynchronizeControl" || Declaration.Name == "VsguiPullStyles")
						{
							SynchronizationOwners.insert(Declaration.OwnerId);
						}
					}
					for (auto& Diagnostic : Diagnostics)
					{
						Output.ReportDiagnostic(std::move(Diagnostic));
					}
				}
				const auto& Schema = CachedSchema;
				if (!Schema)
				{
					return;
				}
				if (Context.Phase == ECompileProcessingPhase::Bodies)
				{
					if (Context.ModuleId.size() >= 6 &&
					    Context.ModuleId.substr(Context.ModuleId.size() - 6) == ".vswml")
					{
						return;
					}
					for (const auto& Type : Schema->GetTypes())
					{
						if (Type.ModuleId != Context.ModuleId || !Type.bWidget || Type.Name == "VS.Gui.Widget")
						{
							continue;
						}
						for (const auto& Member : Context.Declarations)
						{
							if (Member.OwnerId != Type.DeclarationId || Member.bGenerated || Member.bIsStatic ||
							    Member.BodySource.empty() ||
							    std::any_of(Member.Parameters.begin(), Member.Parameters.end(),
							        [](const auto& P)
							{
								return P.Name == "elements";
							}))
							{
								continue;
							}
							const auto Body = GuiScopedBody(Member.BodySource, Type.CanonicalName, true);
							if (Body != Member.BodySource)
							{
								Output.SubmitOperation(GuiBodyOperation(Context, Member, Body));
							}
						}
					}
					return;
				}
				for (const auto& Type : Schema->GetTypes())
				{
					if (Type.ModuleId != Context.ModuleId || !Type.bWidget)
					{
						continue;
					}
					const auto Builder = ShortName(Type.Name) + "Builder";
					const bool bNativeScope = !HasInheritedImplementation(*Schema, Type, DisposalOwners);
					const bool bReuseScope = bNativeScope && NumericControl(Type.CanonicalName);
					std::string Members = Type.Name == "VS.Gui.Widget"
					    ? std::string{}
					    : "public: void VsguiSynchronizeProperties() override{";
					if (bReuseScope)
					{
						Members += "bool nativeStable=VS::Gui::Internal::NativeEvaluation::Widget==this&&"
						           "VS::Gui::PropertyEvaluation::NativeSourcesStable;";
					}
					for (const auto& Property : Type.Properties)
					{
						if (!Property.bBuiltin)
						{
							// Exact framework controls have no user override of an absent
							// change hook. Derived widgets retain the virtual call.
							Members += IsOwn(Context, Type, Property.DeclarationId)
							    ? FPropertyEmitter(Property, bNativeScope, bReuseScope)
							          .Synchronize(bReuseScope ? "nativeStable" : std::string{},
							              bReuseScope && !HasMethod(Context, Type, "On" + Property.Name + "Changed"))
							    : "VsguiRefresh" + Property.Name + "();";
						}
					}
					if (Type.Name != "VS.Gui.Widget")
					{
						Members += '}';
						Members += SuspensionMembers(Context, *Schema, Type, *Configuration);
						const bool IsMarkup = Context.ModuleId.size() >= 6 &&
						    Context.ModuleId.substr(Context.ModuleId.size() - 6) == ".vswml";
						const bool NeedsScope =
						    IsMarkup ||
						    std::any_of(Context.Declarations.begin(), Context.Declarations.end(),
						        [&](const auto& D)
						{
							return D.OwnerId == Type.DeclarationId && !D.bGenerated &&
							    (D.BodySource.find("elements") != std::string::npos ||
							        D.BodySource.find("AddInternalChild") != std::string::npos);
						});
						if (NeedsScope)
						{
							const auto Suffix = GuiScopeSuffix(Type.CanonicalName);
							const auto Scope = "VsguiDeclarationScope_" + Suffix;
							if (Context.ModuleId.size() < 6 ||
							    Context.ModuleId.substr(Context.ModuleId.size() - 6) != ".vswml")
							{
								Members += "private:int64 " + Scope + ';';
							}
							Members += "protected:int64 VsguiGetScope" + Suffix + "(){if(" + Scope + "==0){" + Scope +
							    "=VS::Gui::__NativeWidget::AllocateNameScope();}return " + Scope +
							    ";}"
							    "void VsguiScopedAddInternalChild" +
							    Suffix +
							    "(VS::Gui::Widget "
							    "child){child.__GetNativeHandle().AdoptDeclarationScope(VsguiGetScope" +
							    Suffix +
							    "());AddInternalChild(child);}"
							    "void VsguiScopedAddInternalChildren" +
							    Suffix +
							    "(array<VS::Gui::Widget> values){for(auto "
							    "child:values){child.__GetNativeHandle().AdoptDeclarationScope(VsguiGetScope" +
							    Suffix + "());}AddInternalChildren(values);}";
						}
						Members += StyleMembers(Context, Type, Output, *Configuration);
						// A subclass without @GuiWidget can add its own synchronization hooks.
						// Cache an exact runtime type check, never an inherited static hint.
						const bool HasProperties = std::any_of(Type.Properties.begin(), Type.Properties.end(),
						    [](const auto& Property)
						{
							return !Property.bBuiltin;
						});
						const bool HasSynchronization =
						    HasProperties || HasInheritedImplementation(*Schema, Type, SynchronizationOwners);
						if (!HasSynchronization)
						{
							const auto ExactType = "VsguiExactSynchronizationType_" + GuiScopeSuffix(Type.CanonicalName);
							Members += "private:optional<bool> " + ExactType + ';' +
							    "public:bool VsguiHasCustomSynchronization() override{"
							    "if(!" +
							    ExactType + ".has_value()){" + ExactType +
							    "=VS::Gui::__NativeWidget::IsScriptType("
							    "fn()->void{VsguiSynchronizeControl();}," +
							    GuiQuote(Type.CanonicalName) + ");}"
							                                   "return !" +
							    ExactType + ".get();}";
						}
						else
						{
							Members += "public:bool VsguiHasCustomSynchronization() override{return true;}";
						}
						Members += "public: bool VsguiHasInitializationHook() const override{return " +
						    std::string(HasLifecycleHook(Context, *Schema, Type) ? "true" : "false") + ";}";
						for (const auto* Hook : {"Mounted", "LayoutChanged", "Paint"})
						{
							const auto Name = "On" + std::string(Hook);
							Members +=
							    "public:bool VsguiHas" + std::string(Hook) + "Hook() const override{return " +
							    std::string(HasLifecycleHook(Context, *Schema, Type, Name.c_str()) ? "true" : "false") +
							    ";}";
						}
						Members += "protected: void VsguiDisposeProperties() override{";
						for (const auto& Property : Type.Properties)
						{
							if (!Property.bBuiltin)
							{
								Members += "VsguiRelease" + Property.Name + "();";
							}
						}
						for (const auto& Event : Type.Events)
						{
							Members += "Clear" + Event.Name + "();";
						}
						Members += '}';
					}
					for (const auto& Property : Type.Properties)
					{
						if (IsOwn(Context, Type, Property.DeclarationId))
						{
							for (const auto& Codec : Configuration->ValueCodecs)
							{
								if (GuiSourceType(Codec.Type) == GuiSourceType(Property.ValueType) &&
								    !Codec.ParseLiteral.empty())
								{
									Members += "public: " + GuiSourceType(Property.ValueType) + " VsguiDecodeMarkup" +
									    Property.Name + "(string value){return " +
									    GuiSourceType(Codec.ParseLiteral) + "(value);}";
								}
							}
							GuiSubmitMembers(Context, Output, Type.DeclarationId,
							    PropertyMembers(Context, Type, Property, bNativeScope), Property.Location);
						}
					}
					for (const auto& Event : Type.Events)
					{
						if (IsOwn(Context, Type, Event.DeclarationId))
						{
							GuiSubmitMembers(Context, Output, Type.DeclarationId, EventMembers(Type, Event),
							    Event.Location);
						}
					}
					if (Type.bRegistered)
					{
						Members += "public: static " + Builder + " Create(){return " + Builder + "();}";
						const auto FullBuilder =
						    Namespace(Type.Name).empty() ? Builder : Namespace(Type.Name) + '.' + Builder;
						const auto Found = std::find_if(Context.Declarations.begin(), Context.Declarations.end(),
						    [&](const auto& D)
						{
							return D.ModulePath == Context.ModuleId &&
							    D.Name == FullBuilder &&
							    D.Target == EAnnotationTarget::Struct;
						});
						if (Found == Context.Declarations.end())
						{
							throw std::runtime_error("GUI Builder was not available in Members phase");
						}
						GuiSubmitMembers(Context, Output, Found->Id, BuilderMembers(Type, Context, Found->Id), Type.Location);
					}
					GuiSubmitMembers(Context, Output, Type.DeclarationId, Members, Type.Location);
				}
			}

		private:
			std::shared_ptr<const FGuiCompilationConfiguration> Configuration;
			std::uint64_t CachedSnapshot = 0;
			std::shared_ptr<const FGuiSchema> CachedSchema;
			std::set<std::string> SynchronizationOwners;
			std::set<std::string> DisposalOwners;
		};
	} // namespace

	bool ConfigureGuiCompilation(FCompileProcessingOptions& Options, std::string& Error)
	{
		return ConfigureGuiCompilation(Options, {}, Error);
	}

	bool ConfigureGuiCompilation(FCompileProcessingOptions& Options, const FGuiCompilationConfiguration& Configuration,
	    std::string& Error)
	{
		Error.clear();
		auto Next = Options;
		auto Profile = std::make_shared<FGuiCompilationConfiguration>(Configuration);
		std::string ConfigurationKey = "schema-3:" + std::string(GetGuiCompilationFingerprint());
		const auto Append = [&](const std::string& Value)
		{
			ConfigurationKey += ':' + std::to_string(Value.size()) + ':' + Value;
		};
		std::set<std::string> Codecs, Bindings;
		for (const auto& Codec : Profile->ValueCodecs)
		{
			if (Codec.Type.empty() || (Codec.ParseLiteral.empty() && Codec.ParseStyle.empty()) ||
			    !Codecs.insert(GuiSourceType(Codec.Type)).second)
			{
				Error = "gui.invalid_or_duplicate_value_codec";
				return false;
			}
			Append(Codec.Type);
			Append(Codec.ParseLiteral);
			Append(Codec.ParseStyle);
		}
		ConfigurationKey += ":bindings";
		for (const auto& Binding : Profile->ResourceBindings)
		{
			if (Binding.ResourceIdentity.empty() || Binding.ContextModule.empty() || Binding.ContextType.empty() ||
			    !Bindings.insert(Binding.ResourceIdentity).second)
			{
				Error = "gui.invalid_or_duplicate_resource_binding";
				return false;
			}
			Append(Binding.ResourceIdentity);
			Append(Binding.ContextModule);
			Append(Binding.ContextType);
		}
		for (const auto* Name : {"@GuiWidget", "@GuiProperty", "@GuiMethod", "@GuiEvent"})
		{
			const auto Found =
			    std::find_if(Next.AnnotationDefinitions.begin(), Next.AnnotationDefinitions.end(),
			        [&](const auto& Item)
			{
				return AnnotationNamesEqual(Item.Name, Name, false);
			});
			if (Found != Next.AnnotationDefinitions.end())
			{
				if (Found->CaseSensitive)
				{
					Error = "gui.annotation_policy_conflict: " + Found->Name;
					return false;
				}
				continue;
			}
			FAnnotationDefinition Definition;
			Definition.Name = Name;
			Definition.Id = std::string("gui:") + Name;
			Definition.ProviderId = "veroscript.gui";
			Definition.CaseSensitive = false;
			Next.AnnotationDefinitions.push_back(std::move(Definition));
		}
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		bool bHasDeclarations = false, bHasMarkup = false;
		if (Next.Registry)
		{
			for (const auto& Existing : Next.Registry->GetProcessors())
			{
				if (!Registry->Register(Existing, Error))
				{
					return false;
				}
				if (Existing.ProcessorId == ProcessorId)
				{
					if (Existing.Version != "0.1.1" || Existing.ConfigurationKey != ConfigurationKey)
					{
						Error = "gui.processor_configuration_conflict";
						return false;
					}
					if (!Next.Registry->IsFrozen())
					{
						Error = "gui.processor_registry_must_be_frozen";
						return false;
					}
					bHasDeclarations = true;
				}
				if (Existing.ProcessorId == "veroscript.gui.markup")
				{
					const auto Expected = MakeGuiMarkupProcessor(Profile, ConfigurationKey);
					if (Existing.Version != Expected.Version || Existing.ConfigurationKey != Expected.ConfigurationKey)
					{
						Error = "gui.markup_processor_configuration_conflict";
						return false;
					}
					bHasMarkup = true;
				}
			}
		}
		FCompileProcessorDesc Processor;
		Processor.ProcessorId = ProcessorId;
		Processor.Version = "0.1.1";
		Processor.ConfigurationKey = ConfigurationKey;
		Processor.Phases = CompilePhaseMask(ECompileProcessingPhase::Types) |
		    CompilePhaseMask(ECompileProcessingPhase::Members) |
		    CompilePhaseMask(ECompileProcessingPhase::Bodies);
		Processor.Operations = CompileOperationMask(ECompileOperation::AddDeclaration) |
		    CompileOperationMask(ECompileOperation::ReplaceBody);
		FCompileSubscription Subscription;
		Subscription.Event = ECompileProcessingEvent::Module;
		Processor.Subscriptions.push_back(std::move(Subscription));
		Processor.Factory = [Profile]
		{
			return std::make_unique<FProcessor>(Profile);
		};
		if (!bHasDeclarations && !Registry->Register(std::move(Processor), Error))
		{
			return false;
		}
		if (!bHasMarkup && !Registry->Register(MakeGuiMarkupProcessor(Profile, ConfigurationKey), Error))
		{
			return false;
		}
		Registry->Freeze();
		Next.Registry = std::move(Registry);
		Options = std::move(Next);
		return true;
	}
} // namespace VeroScript::Gui

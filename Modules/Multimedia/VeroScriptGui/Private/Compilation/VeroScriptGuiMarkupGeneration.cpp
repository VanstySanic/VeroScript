// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiMarkupGeneration.h"
#include "VeroScriptGuiMarkupInternal.h"
#include "VeroScriptGuiSchema.h"
#include "VeroScriptGuiSource.h"
#include "VeroScriptGuiScriptExpression.h"
#include "Bindings/VeroScriptGuiScriptLibrary.h"
#include "VeroScriptPath.h"
#include "VeroScriptResources.h"
#include "Tools/VeroScriptGuiArtifact.h"
#include "VeroScriptFloatParsing.h"
#include <filesystem>
#include <charconv>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <limits>

namespace VeroScript::Gui
{
	namespace
	{
		bool HasSuffix(std::string_view Value, std::string_view Suffix)
		{
			return Value.size() >= Suffix.size() && Value.substr(Value.size() - Suffix.size()) == Suffix;
		}

		std::string PageName(std::string_view Identity)
		{
			std::uint64_t Hash = 14695981039346656037ull;
			for (const unsigned char C : Identity)
			{
				Hash = (Hash ^ C) * 1099511628211ull;
			}
			std::ostringstream Stream;
			Stream << "VsguiPage" << std::hex << Hash;
			return Stream.str();
		}

		FAnnotationSourcePosition Position(const FMarkupLocation& Location)
		{
			FAnnotationSourcePosition Result;
			Result.FileLabel = Location.Source;
			Result.Line = static_cast<std::uint32_t>(std::min<std::size_t>(Location.Line, UINT32_MAX));
			Result.Column = static_cast<std::uint32_t>(std::min<std::size_t>(Location.Column, UINT32_MAX));
			return Result;
		}

		void Report(FCompileProcessingOutput& Output, const FMarkupLocation& Location, const std::string& Message)
		{
			FScriptError Error;
			Error.MessageId = "gui.markup.compilation";
			Error.Message = Message;
			Error.Location.FileLabel = Location.Source;
			Error.Location.Line = static_cast<std::uint32_t>(std::min<std::size_t>(Location.Line, UINT32_MAX));
			Error.Location.Column = static_cast<std::uint32_t>(std::min<std::size_t>(Location.Column, UINT32_MAX));
			Output.ReportDiagnostic(std::move(Error));
		}

		std::string Shell(const FMarkupUnit& Unit, const std::string& Name, const std::string& Base,
		    bool bWrapNamespace = true)
		{
			std::string Result;
			for (const auto& Import : Unit.Imports)
			{
				Result += "export import " + GuiQuote(Import) + ";\n";
			}
			if (!Unit.Script.bInline && !Unit.Script.Source.Text.empty() && !Unit.Script.Source.Identity.empty())
			{
				Result += "import " + GuiQuote(Unit.Script.Source.Identity) + ";\n";
			}
			const auto Separator = Name.rfind("::");
			const auto Short = Separator == std::string::npos ? Name : Name.substr(Separator + 2);
			if (bWrapNamespace && Separator != std::string::npos)
			{
				Result += "namespace " + Name.substr(0, Separator) + " {\n";
			}
			if (!Unit.bComponent)
			{
				const auto Suffix = GuiScopeSuffix(Name);
				Result += "class " + Short + " : public VS::Gui::BindingState {public: " + Short + "(){}\n";
				if (Unit.Script.bInline)
				{
					Result += Unit.Script.Source.Text + '\n';
				}
				else if (!Unit.Script.Source.Text.empty())
				{
					Result += "private: " + Unit.Script.Type + " VsguiStrongContext; weak<" + Unit.Script.Type +
					    "> VsguiWeakContext; public: " + Unit.Script.Type +
					    " GetBindingContext(){if(VsguiStrongContext!=nullptr){return VsguiStrongContext;}return "
					    "VsguiWeakContext.lock();}\n";
					Result +=
					    "void VsguiReleaseBindings() override {VsguiStrongContext=nullptr;VsguiWeakContext=weak<" +
					    Unit.Script.Type + ">();VsguiElements_" + Suffix + "=VS::Gui::Elements();}\n";
				}
				Result +=
				    "private: VS::Gui::Elements VsguiElements_" + Suffix + ";int64 VsguiDeclarationScope_" + Suffix +
				    ";public: VS::Gui::Widget VsguiBuild(VS::Gui::FactoryArgument argument){return nullptr;}\n};\n";
				Result += "VS::Gui::Widget " + Short +
				    "Factory(VS::Gui::FactoryArgument argument){"
				    "int __vsguiPreviousDepth=VS::Gui::Construction::Depth;bool completed=false;try{"
				    "VS::Gui::Construction::Begin();auto state=" +
				    Short +
				    "();auto root=state.VsguiBuild(argument);"
				    "VS::Gui::Construction::Complete(root);completed=true;return "
				    "root;}finally{if(VS::Gui::Construction::Depth>__vsguiPreviousDepth)"
				    "{VS::Gui::Construction::End(completed);}}}\n";
				if (bWrapNamespace && Separator != std::string::npos)
				{
					Result += "}\n";
				}
				return Result;
			}
			Result += "@GuiWidget(";
			if (!Unit.Mark.empty())
			{
				Result += "Mark=" + GuiQuote(Unit.Mark);
			}
			if (!Unit.DefaultId.empty())
			{
				Result += (Unit.Mark.empty() ? "" : ",") + std::string("Id=") + GuiQuote(Unit.DefaultId);
			}
			Result += ") export class " + Short + " : public " + Base + " {\n";
			if (!Unit.bComponent)
			{
				Result += "public: " + Short + "(){}\n";
			}
			if (Unit.Script.bInline)
			{
				Result += Unit.Script.Source.Text + '\n';
			}
			else if (!Unit.Script.Source.Text.empty())
			{
				Result += "private: @GuiProperty " + Unit.Script.Type + " BindingContext;\n";
				Result +=
				    "public: void VsguiFinalizeConfiguration() "
				    "override{Super.VsguiFinalizeConfiguration();if(GetBindingContext()==nullptr){SetBindingContext(" +
				    Unit.Script.Type + "());}}\n";
			}
			Result += "private: VS::Gui::Elements VsguiElements_" + GuiScopeSuffix(Name) +
			    "; int64 VsguiDeclarationScope_" + GuiScopeSuffix(Name) +
			    "; public: void VsguiInstallTemplate() override {}\n};\n";
			if (bWrapNamespace)
			{
				Result += "VS::Gui::Widget " + Short +
				    "Factory(VS::Gui::FactoryArgument argument){"
				    "if(argument!=nullptr){throw \"GUI component uses its own binding context\";}return " +
				    Short + "::Create();}\n";
			}
			if (bWrapNamespace && Separator != std::string::npos)
			{
				Result += "}\n";
			}
			return Result;
		}

		std::string NativeLiteral(const FCompileProcessingContext& Context, const FGuiPropertySchema& Property,
		    const std::string& Text, std::string& Error)
		{
			const auto SourceType = GuiSourceType(Property.ValueType);
			EProperty NativeProperty;
			FPropertyValue Value;
			const std::map<std::string, std::string> NativeTypes{
			    {"VS::Gui::Length", "Width"}, {"VS::Gui::Insets", "Padding"},
			    {"VS::Gui::Color", "Background"}, {"VS::Gui::Layout", "Layout"},
			    {"VS::Gui::Visibility", "Visibility"}, {"VS::Gui::HitTestMode", "HitTest"},
			    {"VS::Gui::Cursor", "Cursor"}};
			const auto NativeType = NativeTypes.find(SourceType);
			if (Property.bBuiltin || NativeType != NativeTypes.end())
			{
				if (!ParseMarkupProperty(Property.bBuiltin ? Property.Name : NativeType->second, Text, NativeProperty,
				        Value, false, Error))
				{
					return {};
				}
				const auto Number = [](double Value)
				{
					std::ostringstream S;
					S.precision(17);
					S << Value;
					return S.str();
				};
				if (const auto* Item = std::get_if<FLength>(&Value))
				{
					return Item->Unit == ELengthUnit::Auto ? "VS::Gui::Length::Auto()"
					    : Item->Unit == ELengthUnit::Percent
					    ? "VS::Gui::Length::Percent(" + Number(Item->Value) + ")"
					    : "VS::Gui::Length(" + Number(Item->Value) + ")";
				}
				if (const auto* Item = std::get_if<FInsets>(&Value))
				{
					return "VS::Gui::Insets(" + Number(Item->Top) + ',' + Number(Item->Right) + ',' +
					    Number(Item->Bottom) + ',' + Number(Item->Left) + ')';
				}
				if (const auto* Item = std::get_if<Graphics::FColor>(&Value))
				{
					return "VS::Gui::Color(" + Number(Item->R) + ',' + Number(Item->G) + ',' + Number(Item->B) + ',' +
					    Number(Item->A) + ')';
				}
				if (const auto* Item = std::get_if<bool>(&Value))
				{
					return *Item ? "true" : "false";
				}
				if (const auto* Item = std::get_if<double>(&Value))
				{
					return Number(*Item);
				}
				if (const auto* Item = std::get_if<std::string>(&Value))
				{
					return GuiQuote(*Item);
				}
				std::string Suffix;
				if (const auto* Item = std::get_if<ELayout>(&Value))
				{
					Suffix = *Item == ELayout::Vertical ? "Vertical"
					    : *Item == ELayout::Horizontal  ? "Horizontal"
					                                    : "Overlay";
				}
				else if (const auto* Visibility = std::get_if<EVisibility>(&Value))
				{
					Suffix = *Visibility == EVisibility::Visible ? "Visible"
					    : *Visibility == EVisibility::Hidden     ? "Hidden"
					                                             : "Collapsed";
				}
				else if (const auto* Cursor = std::get_if<ECursor>(&Value))
				{
					Suffix = GetCursorName(*Cursor);
				}
				else
				{
					const auto HitTest = std::get<EHitTestMode>(Value);
					Suffix = HitTest == EHitTestMode::SelfAndChildren ? "SelfAndChildren"
					    : HitTest == EHitTestMode::SelfOnly           ? "SelfOnly"
					    : HitTest == EHitTestMode::ChildrenOnly       ? "ChildrenOnly"
					                                                  : "None";
				}
				return GuiSourceType(Property.ValueType) + "::" + Suffix;
			}
			const auto Type = GuiSourceType(Property.ValueType);
			if (Type == "string")
			{
				return GuiQuote(Text);
			}
			if (Type == "wstring")
			{
				return "L" + GuiQuote(Text);
			}
			if (Type == "std::i18n::text")
			{
				return Type + '(' + GuiQuote(Text) + ')';
			}
			if (Type == "bool" && (Text == "true" || Text == "false"))
			{
				return Text;
			}
			if (Type == "int" || Type == "int64")
			{
				std::int64_t Number = 0;
				const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Number);
				if (Result.ec == std::errc{} && Result.ptr == Text.data() + Text.size() &&
				    (Type == "int64" || (Number >= INT32_MIN && Number <= INT32_MAX)))
				{
					if (Number == INT64_MIN)
					{
						return "(-9223372036854775807i64-1i64)";
					}
					return Text + (Type == "int64" ? "i64" : "");
				}
			}
			if (Type == "uint64" || Type == "unsigned int")
			{
				std::uint64_t Number = 0;
				const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Number);
				if (Result.ec == std::errc{} && Result.ptr == Text.data() + Text.size() &&
				    (Type == "uint64" || Number <= UINT32_MAX))
				{
					return "cast<" + Type + ">(" + Text + "u64)";
				}
			}
			if (Type == "double" || Type == "float")
			{
				double Number = 0;
				const auto Result = FloatParsingDetail::FromCharsDecimal(Text, Number);
				if (Result.ec == std::errc{} && Result.ptr == Text.data() + Text.size() && std::isfinite(Number) &&
				    (Type == "double" || std::abs(Number) <= std::numeric_limits<float>::max()))
				{
					std::ostringstream Literal;
					Literal.precision(17);
					Literal << std::scientific << Number;
					return "cast<" + Type + ">(" + Literal.str() + ')';
				}
			}
			for (const auto& Enum : Context.Declarations)
			{
				if (Enum.Target != EAnnotationTarget::Enum || GuiSourceType(Enum.Name) != Type)
				{
					continue;
				}
				for (const auto& Member : Context.Declarations)
				{
					if (Member.OwnerId != Enum.Id || Member.Target != EAnnotationTarget::EnumMember)
					{
						continue;
					}
					const auto Dot = Member.Name.rfind('.');
					const auto Leaf = Member.Name.substr(Dot == std::string::npos ? 0 : Dot + 1);
					if (Text == Leaf || Text == Type + "::" + Leaf)
					{
						return Type + "::" + Leaf;
					}
				}
			}
			Error = "gui.literal_codec_missing_or_invalid: " + Type;
			return {};
		}

		struct FInput
		{
			FMarkupUnit Unit;
			std::string Name, Base;
			bool bInjectedContext = false;
			std::string RawSourceId;
		};

		std::vector<FSourceDocument::FMapping> CopiedMappings(const FInput& Input, std::size_t Output,
		    std::size_t Begin, std::size_t Length,
		    const std::vector<FMarkupTextSpan>& Spans)
		{
			std::vector<FSourceDocument::FMapping> Result;
			for (const auto& Span : Spans)
			{
				const auto Left = std::max(Begin, Span.Begin), Right = std::min(Begin + Length, Span.End);
				if (Left >= Right)
				{
					continue;
				}
				FSourceOrigin Origin;
				Origin.Range.SourceId = Input.RawSourceId;
				Origin.Range.Begin = Span.SourceBegin + Left - Span.Begin;
				Origin.Range.End = Origin.Range.Begin + Right - Left;
				Origin.Range.Position.Begin = Position(MarkupLocation(Input.Unit.Source, Origin.Range.Begin));
				Origin.Range.Position.End = Position(MarkupLocation(Input.Unit.Source, Origin.Range.End));
				Result.push_back({Output + Left - Begin, Output + Right - Begin, std::move(Origin)});
			}
			return Result;
		}

		std::vector<FSourceDocument::FMapping> InlineMappings(const FInput& Input, const std::string& Generated,
		    const std::string& Chunk)
		{
			if (!Input.Unit.Script.bInline || Chunk.empty())
			{
				return {};
			}
			const auto Part = Input.Unit.Script.Source.Text.find(Chunk);
			const auto Output = Generated.find(Chunk);
			if (Part == std::string::npos || Output == std::string::npos)
			{
				return {};
			}
			return CopiedMappings(Input, Output, Part, Chunk.size(), Input.Unit.Script.TextSpans);
		}

		class FMarkupProcessor final : public ICompileProcessor
		{
		public:
			FMarkupProcessor(std::shared_ptr<const FGuiCompilationConfiguration> Configuration, bool bImportFramework)
			    : Configuration(std::move(Configuration)), bImportFramework(bImportFramework)
			{
			}
			void Process(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output) override
			{
				if (Context.CheckCancelled)
				{
					Context.CheckCancelled();
				}
				if (Context.Phase == ECompileProcessingPhase::RawSource)
				{
					if (!Context.Source || !HasSuffix(Context.ModuleId, ".vswml"))
					{
						return;
					}
					FMarkupData Data;
					Data.bDeferSchema = true;
					Data.Options.CheckCancelled = Context.CheckCancelled;
					Data.Options.Read = [&](const auto& Requester, const auto& Reference, auto& Out, auto& Error)
					{
						FResourceRequest Request;
						std::string From;
						const bool bExplicitResource = IsResourceDependencyId(Reference);
						if (bExplicitResource || IsResourceDependencyId(Requester))
						{
							if (!ParseResourceDependencyId(bExplicitResource ? Reference : Requester, Request, From))
							{
								Error = "gui.invalid_resource_identity";
								return false;
							}
							if (!bExplicitResource)
							{
								Request.Path = PathUtil::ToUtf8(
								    (PathUtil::FromUtf8(Request.Path).parent_path() / PathUtil::FromUtf8(Reference))
								        .lexically_normal());
							}
							FResourceData Resource;
							if (!ValidateResourceRequest(Request, Error) || !Context.ReadResource ||
							    !Context.ReadResource(Request, Resource))
							{
								if (Error.empty())
								{
									Error = "gui.resource_read_failed: " + Reference;
								}
								return false;
							}
							Out.Identity = MakeResourceDependencyId(Request, Context.ResourceRequester);
							Out.Text.assign(Resource.Bytes.begin(), Resource.Bytes.end());
							return true;
						}
						const auto Path = (PathUtil::FromUtf8(Requester).parent_path() / PathUtil::FromUtf8(Reference))
						                      .lexically_normal();
						Out.Identity = PathUtil::ToUtf8(Path);
						if (!Context.ReadDependency || !Context.ReadDependency(Out.Identity, Out.Text))
						{
							Error = "gui.resource_read_failed: " + Reference;
							return false;
						}
						return true;
					};
					std::set<std::string> Visiting;
					FMarkupDiagnostic Diagnostic;
					if (!CompileMarkupUnit(Data, {Context.ModuleId, Context.Source->Text}, Visiting, Diagnostic) ||
					    !ValidateMarkupGraph(Data, Diagnostic))
					{
						Report(Output, Diagnostic.Location, Diagnostic.Message);
						return;
					}
					auto Unit = Data.Units.at(Context.ModuleId);
					bool Injected = false;
					for (const auto& Binding : Configuration->ResourceBindings)
					{
						if (Binding.ResourceIdentity != Context.ModuleId)
						{
							continue;
						}
						if (Unit.bComponent || Unit.Script.bInline || !Unit.Script.Source.Text.empty())
						{
							Report(Output, Unit.Root.Location, "gui.conflicting_binding_context");
							return;
						}
						Unit.Script.Type = Binding.ContextType;
						Unit.Script.Source.Identity = Binding.ContextModule;
						if (!Context.ReadDependency ||
						    !Context.ReadDependency(Binding.ContextModule, Unit.Script.Source.Text))
						{
							Report(Output, Unit.Root.Location, "gui.binding_context_module_unavailable");
							return;
						}
						Injected = true;
					}
					FInput Input{
					    Unit, Unit.bComponent ? Unit.Type : PageName(Context.ModuleId), "VS::Gui::Widget", false, {}};
					Input.bInjectedContext = Injected;
					Input.RawSourceId = Context.Source->Id;
					FSourceRange Range;
					Range.SourceId = Context.Source->Id;
					Range.End = Context.Source->Text.size();
					auto Source = Shell(Input.Unit, Input.Name, Input.Base);
					if (bImportFramework)
					{
						Source += "\nexport import " + GuiQuote(GetGuiScriptLibraryPath()) + ";\n";
					}
					Output.EditSource(Range, Source, {}, InlineMappings(Input, Source, Input.Unit.Script.Source.Text));
					Input.RawSourceId = Context.ModuleId + ":raw";
					Inputs.emplace(Context.ModuleId, std::move(Input));
					return;
				}
				const auto Found = Inputs.find(Context.ModuleId);
				if (Found == Inputs.end())
				{
					return;
				}
				if (Context.Phase == ECompileProcessingPhase::Types)
				{
					ResolveBase(Context, Output, Found->second);
					return;
				}
				if (Context.Phase == ECompileProcessingPhase::Members)
				{
					EmitTemplate(Context, Output, Found->second);
				}
				if (Context.Phase == ECompileProcessingPhase::Bodies)
				{
					const auto Body = Bodies.find(Context.ModuleId);
					if (Body != Bodies.end())
					{
						Output.ReplaceBody(Body->second.first, Context.Snapshot, Body->second.second);
					}
					const auto& Input = Found->second;
					std::string OwnerName = Input.Name;
					for (auto At = OwnerName.find("::"); At != std::string::npos; At = OwnerName.find("::"))
					{
						OwnerName.replace(At, 2, ".");
					}
					const auto Owner = std::find_if(Context.Declarations.begin(), Context.Declarations.end(),
					    [&](const auto& D)
					{
						return D.ModulePath == Context.ModuleId &&
						    D.Name == OwnerName &&
						    D.Target == EAnnotationTarget::Class;
					});
					if (Owner != Context.Declarations.end() && Input.Unit.Script.bInline)
					{
						for (const auto& D : Context.Declarations)
						{
							if (D.OwnerId != Owner->Id || D.Name.compare(0, 5, "Vsgui") == 0 || D.BodySource.empty() ||
							    Input.Unit.Script.Source.Text.find(D.BodySource) == std::string::npos)
							{
								continue;
							}
							if (std::any_of(D.Parameters.begin(), D.Parameters.end(),
							        [](const auto& P)
							{
								return P.Name == "elements";
							}))
							{
								continue;
							}
							auto Operation =
							    GuiBodyOperation(Context, D,
							        "{auto elements=VsguiElements_" + GuiScopeSuffix(Input.Name) + ";" +
							            D.BodySource + "}");
							if (Operation.SourceMappings.empty())
							{
								Operation.SourceMappings = InlineMappings(Input, Operation.Source, D.BodySource);
							}
							Output.SubmitOperation(std::move(Operation));
						}
					}
				}
			}

		private:
			void ResolveBase(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output, FInput& Input)
			{
				const auto Requested = Input.Unit.bComponent ? Input.Unit.Base : Input.Unit.Root.Name;
				std::vector<const FAnnotationDeclarationView*> Matches;
				for (const auto& Declaration : Context.Declarations)
				{
					if (Declaration.Target != EAnnotationTarget::Class)
					{
						continue;
					}
					const auto At = Declaration.Name.rfind('.');
					bool Match = Declaration.Name == Requested || GuiSourceType(Declaration.Name) == Requested ||
					    Declaration.Name.substr(At == std::string::npos ? 0 : At + 1) == Requested;
					for (const auto& Annotation : Declaration.Metadata.Annotations)
					{
						if (!Context.SourceMetadata.MatchesAnnotationName(Annotation, "@GuiWidget"))
						{
							continue;
						}
						for (const auto& Argument : Annotation.Arguments)
						{
							if (AnnotationNamesEqual(Argument.Name, "Mark", false) && Argument.Value == Requested)
							{
								Match = true;
							}
						}
					}
					if (Match)
					{
						Matches.push_back(&Declaration);
					}
				}
				if (Matches.size() != 1)
				{
					Report(Output, Input.Unit.Root.Location, "gui.unknown_or_ambiguous_base: " + Requested);
					return;
				}
				Input.Base = GuiSourceType(Matches.front()->Name);
				if (!Input.Unit.bComponent)
				{
					return;
				}
				if (Input.Base == "VS::Gui::Widget")
				{
					return;
				}
				std::string Name = Input.Name;
				for (auto At = Name.find("::"); At != std::string::npos; At = Name.find("::"))
				{
					Name.replace(At, 2, ".");
				}
				const auto Owner = std::find_if(Context.Declarations.begin(), Context.Declarations.end(),
				    [&](const auto& D)
				{
					return D.ModulePath == Context.ModuleId && D.Name == Name &&
					    D.Target == EAnnotationTarget::Class;
				});
				if (Owner == Context.Declarations.end())
				{
					Report(Output, Input.Unit.Root.Location, "gui.generated_type_missing");
					return;
				}
				// A declaration replacement contains exactly one type; imports remain at module scope.
				auto Unit = Input.Unit;
				Unit.Imports.clear();
				Unit.Script.Source.Identity.clear();
				FCompileGenerationOperation Operation;
				Operation.Kind = ECompileOperation::ReplaceDeclaration;
				Operation.TargetId = Owner->Id;
				Operation.Snapshot = Context.Snapshot;
				Operation.Scope.ModuleId = Context.ModuleId;
				Operation.Source = Shell(Unit, Input.Name, Input.Base, false);
				Operation.SourceMappings = InlineMappings(Input, Operation.Source, Unit.Script.Source.Text);
				Output.SubmitOperation(std::move(Operation));
			}

			void EmitTemplate(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output,
			    const FInput& Input);
			std::map<std::string, FInput> Inputs;
			std::map<std::string, std::pair<std::string, std::string>> Bodies;
			std::shared_ptr<const FGuiCompilationConfiguration> Configuration;
			bool bImportFramework;
		};

		void FMarkupProcessor::EmitTemplate(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output,
		    const FInput& Input)
		{
			std::vector<FScriptError> Errors;
			const auto Schema = FGuiSchema::Build(Context.Declarations, Context.SourceMetadata, Errors);
			if (!Schema)
			{
				for (auto& Error : Errors)
				{
					Output.ReportDiagnostic(std::move(Error));
				}
				return;
			}
			const auto* Owner =
			    Schema->FindType(Input.Unit.bComponent ? Input.Name : Context.ModuleId + '\x1f' + Input.Name);
			if (!Owner)
			{
				Report(Output, Input.Unit.Root.Location, "gui.generated_type_missing");
				return;
			}
			const auto Scope = "VsguiDeclarationScope_" + GuiScopeSuffix(Input.Name);
			const auto Elements = "VsguiElements_" + GuiScopeSuffix(Input.Name);
			const auto* RootType = Input.Unit.bComponent ? Owner : Schema->FindType(Input.Base);
			if (!RootType || !RootType->bWidget)
			{
				Report(Output, Input.Unit.Root.Location, "gui.root_widget_type_missing");
				return;
			}
			const auto Root = Input.Unit.bComponent ? std::string("this") : std::string("root");
			std::string Code = Input.Unit.bComponent ? "{Super.VsguiInstallTemplate();" : "{";
			if (!Input.Unit.bComponent)
			{
				const bool External = !Input.Unit.Script.bInline && !Input.Unit.Script.Source.Text.empty();
				if (External)
				{
					const auto* ContextType = Schema->FindType(Input.Unit.Script.Type);
					const auto Type = GuiSourceType(Input.Unit.Script.Type);
					Code +=
					    Type + " supplied=nullptr; if(argument==nullptr){" +
					    (Input.bInjectedContext ? std::string("throw \"GUI page requires a binding context\";")
					                            : "supplied=" + Type + "();") +
					    "}else{"
					    "auto boxed=cast<VS::Gui::FactoryArgumentOf<" +
					    Type +
					    ">>(argument);"
					    "if(boxed==nullptr){throw \"GUI factory binding context type mismatch\";}supplied=boxed.Value;}"
					    "if(supplied==nullptr){throw \"GUI binding context is null\";}";
					Code += ContextType && ContextType->bWidget ? "VsguiWeakContext=weak<" + Type + ">(supplied);"
					                                            : "VsguiStrongContext=supplied;";
				}
				else
				{
					Code += "if(argument!=nullptr){throw \"GUI page does not accept a binding context\";}";
				}
				Code += GuiSourceType(RootType->Name) + " root=" + GuiSourceType(RootType->Name) + "::Create();";
			}
			Code += "if(" + Scope + "==0){" + Scope + "=VS::Gui::__NativeWidget::AllocateNameScope();}" + Elements +
			    "=VS::Gui::Elements(" + Root + ',' + Scope + ");";
			if (Input.Unit.bComponent)
			{
				Code += "elements=" + Elements + ';';
			}
			else
			{
				Code += "root.VsguiRetainBindings(this);";
			}
			std::size_t Next = 0;
			bool Failed = false;
			std::set<std::string> Ids;
			const auto Fail = [&](const FMarkupLocation& Where, const std::string& Message)
			{
				Failed = true;
				Report(Output, Where, Message);
			};
			const auto Method =
			    [&](const std::string& Name,
			        const std::function<bool(const FGuiMethodSchema&)>& Accept) -> const FGuiMethodSchema*
			{
				const FGuiMethodSchema* Found = nullptr;
				const auto* BindingType = Input.Unit.Script.bInline || Input.Unit.Script.Source.Text.empty()
				    ? Owner
				    : Schema->FindType(Input.Unit.Script.Type);
				if (!BindingType)
				{
					return nullptr;
				}
				for (const auto& Candidate : BindingType->Methods)
				{
					if (BindingType != Owner && Candidate.Access != EMemberAccess::Public)
					{
						continue;
					}
					if (Candidate.Name != Name && Candidate.Mark != Name)
					{
						continue;
					}
					if (!Accept(Candidate))
					{
						continue;
					}
					if (Found)
					{
						return nullptr;
					}
					Found = &Candidate;
				}
				return Found;
			};
			const bool bExternal = !Input.Unit.Script.bInline && !Input.Unit.Script.Source.Text.empty();
			std::unordered_set<std::string> ContextMembers;
			if (bExternal)
			{
				std::set<std::string> Visited;
				const auto Collect = [&](auto&& Self, const FGuiTypeSchema& Type) -> void
				{
					if (!Visited.insert(Type.Identity).second)
					{
						return;
					}
					for (const auto& Declaration : Context.Declarations)
					{
						if (Declaration.OwnerId == Type.DeclarationId && !Declaration.bIsStatic &&
						    (Declaration.Target == EAnnotationTarget::Field ||
						        Declaration.Target == EAnnotationTarget::Method))
						{
							ContextMembers.insert(Declaration.Name);
						}
					}
					for (std::size_t I = 0; I < Type.Bases.size(); ++I)
					{
						if (I < Type.BaseAccess.size() && Type.BaseAccess[I] != EMemberAccess::Public)
						{
							continue;
						}
						if (const auto* Base = Schema->FindType(Type.Bases[I]))
						{
							Self(Self, *Base);
						}
					}
				};
				if (const auto* Type = Schema->FindType(Input.Unit.Script.Type))
				{
					Collect(Collect, *Type);
				}
			}
			const auto BindingPrefix = bExternal ? std::string("context.") : std::string{};
			const auto BindContext =
			    bExternal ? "auto context=GetBindingContext();assert(context!=nullptr,\"GUI binding context is null\");"
			              : std::string{};
			std::function<void(const FMarkupElement&, const FGuiTypeSchema&, const std::string&)> Configure;
			Configure = [&](const FMarkupElement& Element, const FGuiTypeSchema& Type, const std::string& Variable)
			{
				const auto Separator = Type.Name.rfind('.');
				Code += Variable + ".__GetNativeHandle().SetStyleType(" + Scope + ',' +
				    GuiQuote(Type.Name.substr(Separator == std::string::npos ? 0 : Separator + 1)) + ',' +
				    GuiQuote(Type.Mark) + ");";
				std::set<std::string> Properties, Events;
				for (const auto& Attribute : Element.Attributes)
				{
					if (Context.CheckCancelled)
					{
						Context.CheckCancelled();
					}
					if (Attribute.Name == "id")
					{
						if (!Attribute.Value.empty() && !Ids.insert(Attribute.Value).second)
						{
							Fail(Attribute.Location, "gui.duplicate_scope_id: " + Attribute.Value);
						}
						Code += Variable + ".SetId(" + GuiQuote(Attribute.Value) + ");";
						continue;
					}
					if (Attribute.Name == "class")
					{
						Code += Variable + ".SetClasses(" + GuiQuote(Attribute.Value) + ");";
						continue;
					}
					const auto Colon = Attribute.Name.find(':');
					const auto Prefix = Colon == std::string::npos ? std::string{} : Attribute.Name.substr(0, Colon);
					const auto Name = Colon == std::string::npos ? Attribute.Name : Attribute.Name.substr(Colon + 1);
					auto Text = TrimMarkup(Attribute.Value);
					const bool bExpression = Text.size() >= 2 && Text.front() == '{' && Text.back() == '}';
					if (bExpression)
					{
						Text = TrimMarkup(std::string_view(Text).substr(1, Text.size() - 2));
					}
					const auto Index = std::to_string(Next++);
					std::vector<FMarkupTextSpan> ExpressionSpans;
					const auto OriginalText = Text;
					if (bExpression)
					{
						FMarkupExpression Expression;
						Expression.Text = Text;
						Expression.Location = Attribute.ValueLocation;
						Expression.bCode = true;
						if (Prefix == "on")
						{
							Expression.Event = Platform::EEventType::PointerDown;
						}
						std::string Error, Rewritten;
						if (RewriteMarkupExpression(Expression, bExternal, ContextMembers, Rewritten, Error,
						        &ExpressionSpans))
						{
							Text = std::move(Rewritten);
						}
						else if (Error.rfind("gui.", 0) == 0)
						{
							Fail(Attribute.ValueLocation, Error);
							continue;
						}
						else
						{
							ExpressionSpans = {{0, Text.size(), 0}};
						}
					}
					const auto MapExpression = [&](std::size_t Offset)
					{
						std::vector<FSourceDocument::FMapping> Result;
						if (!bExpression)
						{
							return Result;
						}
						const auto InputOffset = Attribute.Value.find(OriginalText);
						for (const auto& Span : ExpressionSpans)
						{
							auto Part = CopiedMappings(Input, Offset + Span.Begin, InputOffset + Span.SourceBegin,
							    Span.End - Span.Begin, Attribute.ValueSpans);
							Result.insert(Result.end(), std::make_move_iterator(Part.begin()),
							    std::make_move_iterator(Part.end()));
						}
						return Result;
					};
					if (Prefix == "on")
					{
						Platform::EEventType Native = Platform::EEventType::None;
						const auto* Event = Schema->FindEvent(Type, Name);
						const bool bNative = !Event && FindMarkupEvent(Name, Native);
						if (!Event && !bNative)
						{
							Fail(Attribute.Location, "gui.unknown_event: " + Name);
							continue;
						}
						const auto Identity =
						    Event ? Event->Identity : "native:" + std::to_string(static_cast<unsigned>(Native));
						if (!Events.insert(Identity).second)
						{
							Fail(Attribute.Location, "gui.duplicate_event: " + Name);
							continue;
						}
						const auto Return = Event ? GuiSourceType(Event->ReturnType) : "void";
						const auto Parameters =
						    Event ? Event->ParameterTypes : std::vector<std::string>{"VS.Gui.Widget", "VS.Gui.Event"};
						std::string Signature, Arguments;
						for (std::size_t P = 0; P < Parameters.size(); ++P)
						{
							if (P)
							{
								Signature += ',';
								Arguments += ',';
							}
							const auto ParameterName =
							    bNative ? (P == 0 ? "sender" : "event") : "arg" + std::to_string(P);
							Signature += GuiSourceType(Parameters[P]) + ' ' + ParameterName;
							Arguments += ParameterName;
						}
						std::string Body;
						if (bExpression)
						{
							Body = Text;
						}
						else
						{
							const auto CompatibleSender = [&](const std::string& Parameter)
							{
								std::set<std::string> Seen;
								std::function<bool(const FGuiTypeSchema&)> Find = [&](const FGuiTypeSchema& Candidate)
								{
									if (!Seen.insert(Candidate.CanonicalName).second)
									{
										return false;
									}
									if (GuiSourceType(Parameter) == GuiSourceType(Candidate.Name) ||
									    Parameter == Candidate.CanonicalName)
									{
										return true;
									}
									for (const auto& Base : Candidate.Bases)
									{
										if (const auto* Parent = Schema->FindType(Base); Parent && Find(*Parent))
										{
											return true;
										}
									}
									return false;
								};
								return Find(Type);
							};
							const auto* Handler =
							    Method(Text,
							        [&](const FGuiMethodSchema& Candidate)
							{
								if (GuiSourceType(Candidate.ReturnType) != Return)
								{
									return false;
								}
								if (bNative)
								{
									if (Candidate.Parameters.empty())
									{
										return true;
									}
									if (Candidate.Parameters.size() == 1)
									{
										return GuiSourceType(Candidate.Parameters[0].CanonicalTypeName) ==
										    "VS::Gui::Event" ||
										    CompatibleSender(Candidate.Parameters[0].CanonicalTypeName);
									}
									return Candidate.Parameters.size() == 2 &&
									    CompatibleSender(Candidate.Parameters[0].CanonicalTypeName) &&
									    GuiSourceType(Candidate.Parameters[1].CanonicalTypeName) ==
									    "VS::Gui::Event";
								}
								if (Candidate.Parameters.size() != Parameters.size())
								{
									return false;
								}
								for (std::size_t P = 0; P < Parameters.size(); ++P)
								{
									if (GuiSourceType(Candidate.Parameters[P].CanonicalTypeName) !=
									    GuiSourceType(Parameters[P]))
									{
										return false;
									}
								}
								return true;
							});
							if (!Handler)
							{
								Fail(Attribute.Location, "gui.event_method_signature: " + Text);
								continue;
							}
							std::string Forward;
							if (Handler->Parameters.size() == Parameters.size())
							{
								Forward = Arguments;
							}
							else if (bNative && Handler->Parameters.size() == 1)
							{
								Forward = GuiSourceType(Handler->Parameters[0].CanonicalTypeName) == "VS::Gui::Event"
								    ? "event"
								    : "sender";
							}
							else if (!Handler->Parameters.empty())
							{
								Fail(Attribute.Location, "gui.event_method_signature: " + Text);
								continue;
							}
							Body = (Return == "void" ? "" : "return ") + BindingPrefix + Handler->Name + '(' + Forward +
							    ");";
						}
						const auto Entry = "VsguiMarkupEvent" + Index;
						const auto EntrySignature =
						    bNative ? GuiSourceType(Type.Name) + " sender,VS::Gui::Event event" : Signature;
						const auto EntryPrefix = "public: " + Return + ' ' + Entry + '(' + GuiSourceType(Type.Name) +
						    " self" + (EntrySignature.empty() ? "" : ",") + EntrySignature +
						    "){auto elements=" + Elements + ";" + BindContext;
						const auto Mapping = MapExpression(EntryPrefix.size());
						GuiSubmitMembers(Context, Output, Owner->DeclarationId, EntryPrefix + Body + '}',
						    Position(Attribute.Location), Mapping);
						Code += "weak<" + GuiSourceType(Input.Name) + "> state" + Index + "=weak<" +
						    GuiSourceType(Input.Name) + ">(this);weak<" + GuiSourceType(Type.Name) + "> target" +
						    Index + "=weak<" + GuiSourceType(Type.Name) + ">(" + Variable + ");";
						Code += Variable + (bNative ? ".Subscribe(" + GuiQuote(Name) + ',' : (Event->Kind == EGuiEventKind::Single ? ".Set" : ".Add") + Event->Name + '(');
						Code += "[state" + Index + ",target" + Index + "](" + Signature + ")->" + Return +
						    "{auto context=state" + Index + ".lock();auto self=target" + Index +
						    ".lock();"
						    "if(context==nullptr||self==nullptr){" +
						    (Return == "void" ? std::string("return;")
						                      : std::string("throw \"GUI event scope expired\";")) +
						    '}' + (Return == "void" ? "" : "return ") + "context." + Entry + "(self" +
						    (Arguments.empty() ? "" : ",") + (bNative ? "self,event" : Arguments) + ");});";
						continue;
					}
					if (!Prefix.empty() && Prefix != "bind")
					{
						Fail(Attribute.Location, "gui.unknown_prefix: " + Prefix);
						continue;
					}
					const auto* Property = Schema->FindProperty(Type, Name);
					if (!Property)
					{
						Fail(Attribute.Location, "gui.unknown_property: " + Name);
						continue;
					}
					if (!Properties.insert(Property->Identity).second)
					{
						Fail(Attribute.Location, "gui.duplicate_property: " + Name);
						continue;
					}
					if (Prefix.empty())
					{
						std::string Error;
						const auto Codec =
						    std::find_if(Configuration->ValueCodecs.begin(), Configuration->ValueCodecs.end(),
						        [&](const auto& C)
						{
							return GuiSourceType(C.Type) == GuiSourceType(Property->ValueType) &&
							    !C.ParseLiteral.empty();
						});
						const auto Value = Codec == Configuration->ValueCodecs.end()
						    ? NativeLiteral(Context, *Property, Attribute.Value, Error)
						    : Variable + ".VsguiDecodeMarkup" + Property->Name + '(' +
						        GuiQuote(Attribute.Value) + ')';
						if (Value.empty())
						{
							Fail(Attribute.Location, Error);
							continue;
						}
						const auto ResolvedValue =
						    Property->bBuiltin && Property->Name == "Image" && !Attribute.Value.empty()
						    ? "VS::Gui::__NativeResources::ResolveFrom(" +
						        GuiQuote(std::string(GuiSourceMarker) + Attribute.Location.Source) + "," + Value +
						        ")"
						    : Value;
						Code += Variable + ".Set" + Property->Name + '(' + ResolvedValue + ");";
						continue;
					}
					if (!Property->bBindable)
					{
						Fail(Attribute.Location, "gui.binding_requires_attribute: " + Name);
						continue;
					}
					if (!bExpression)
					{
						const auto* Getter = Method(
						    Text,
						    [&](const FGuiMethodSchema& Candidate)
						{
							return Candidate.Parameters.empty() &&
							    (GuiSourceType(Candidate.ReturnType) == GuiSourceType(Property->ValueType) ||
							        GuiSourceType(Candidate.ReturnType) ==
							            "optional<" + GuiSourceType(Property->ValueType) + '>');
						});
						if (!Getter || !Getter->Parameters.empty())
						{
							Fail(Attribute.Location, "gui.getter_method_required: " + Text);
							continue;
						}
						Text = BindingPrefix + Getter->Name + "()";
					}
					const auto ValueType = GuiSourceType(Property->ValueType);
					const auto Entry = "VsguiMarkupRead" + Index;
					const auto TemporaryValue = "VsguiMarkupValue" + Index;
					const auto EntryPrefix = "public: optional<" + ValueType + "> " + Entry + '(' +
					    GuiSourceType(Type.Name) + " self){auto elements=" + Elements + ";" +
					    BindContext + "auto " + TemporaryValue + '=';
					const auto Mapping = MapExpression(EntryPrefix.size());
					GuiSubmitMembers(Context, Output, Owner->DeclarationId,
					    EntryPrefix + Text + ";return VS::Gui::Internal::BindingValue<" + ValueType + ">(" +
					        TemporaryValue + ");}",
					    Position(Attribute.Location), Mapping);
					Code += "weak<" + GuiSourceType(Input.Name) + "> state" + Index + "=weak<" +
					    GuiSourceType(Input.Name) + ">(this);weak<" + GuiSourceType(Type.Name) + "> target" +
					    Index + "=weak<" + GuiSourceType(Type.Name) + ">(" + Variable + ");std::attribute<" +
					    ValueType + "> binding" + Index + "=std::attribute<" + ValueType + ">();";
					Code += "binding" + Index + ".bind_optional([state" + Index + ",target" + Index + "]()->optional<" +
					    ValueType + ">{auto context=state" + Index + ".lock();auto self=target" + Index +
					    ".lock();if(context==nullptr||self==nullptr){return optional<" + ValueType +
					    ">();}return context." + Entry + "(self);});" + Variable + ".Set" + Property->Name +
					    "(binding" + Index + ");";
				}
			};
			std::function<std::string(const FMarkupElement&)> Node;
			Node = [&](const FMarkupElement& Element) -> std::string
			{
				std::string Error;
				const auto* Type = Schema->FindWidget(Element.Name, Error);
				if (!Type)
				{
					Fail(Element.Location, Error);
					return {};
				}
				if (!TrimMarkup(Element.Text).empty())
				{
					Fail(Element.TextLocation, "gui.raw_text_requires_text_property");
				}
				const auto Variable = "node" + std::to_string(Next++);
				Code += GuiSourceType(Type->Name) + ' ' + Variable + '=' + GuiSourceType(Type->Name) + "::Create();";
				Code += Variable + ".__GetNativeHandle().SetDeclarationScope(" + Scope + ");";
				Configure(Element, *Type, Variable);
				if (!Element.Children.empty())
				{
					const auto Children = "children" + std::to_string(Next++);
					Code += "array<VS::Gui::Widget> " + Children + "=array<VS::Gui::Widget>();";
					for (const auto& Child : Element.Children)
					{
						const auto Value = Node(Child);
						if (!Value.empty())
						{
							Code += Children + ".push(" + Value + ");";
						}
					}
					Code += Variable + ".SetChildren(" + Children + ");";
				}
				return Variable;
			};
			Code += "array<VS::Gui::Widget> content=array<VS::Gui::Widget>();";
			if (Input.Unit.bComponent)
			{
				const auto Separator = Owner->Name.rfind('.');
				Code += "this.__GetNativeHandle().SetStyleType(" + Scope + ',' +
				    GuiQuote(Owner->Name.substr(Separator == std::string::npos ? 0 : Separator + 1)) + ',' +
				    GuiQuote(Owner->Mark) + ");";
				for (const auto& Area : Input.Unit.Root.Children)
				{
					if (Area.Name != "template")
					{
						continue;
					}
					for (const auto& Child : Area.Children)
					{
						const auto Value = Node(Child);
						if (!Value.empty())
						{
							Code += "content.push(" + Value + ");";
						}
					}
				}
			}
			else
			{
				Code += Root + ".__GetNativeHandle().SetDeclarationScope(" + Scope + ");";
				Configure(Input.Unit.Root, *RootType, Root);
				for (const auto& Child : Input.Unit.Root.Children)
				{
					if (Child.Name == "import" || Child.Name == "script" || Child.Name == "style")
					{
						continue;
					}
					const auto Value = Node(Child);
					if (!Value.empty())
					{
						Code += "content.push(" + Value + ");";
					}
				}
			}
			Code += Root + (Input.Unit.bComponent ? ".AddInternalChildren(content);" : ".SetChildren(content);");
			for (const auto& Sheet : Input.Unit.Styles)
			{
				Code += Root + ".__GetNativeHandle().AttachStyleSheet(" + Scope + ',' +
				    GuiQuote(std::string(GuiSourceMarker) + Sheet->Source.Identity) + ',' +
				    GuiQuote(Sheet->Source.Text) + ");";
			}
			if (!Input.Unit.bComponent)
			{
				Code += "return root;";
			}
			Code += '}';
			if (Failed)
			{
				return;
			}
			const auto Body =
			    std::find_if(Context.Declarations.begin(), Context.Declarations.end(),
			        [&](const auto& D)
			{
				return D.OwnerId == Owner->DeclarationId &&
				    D.Name == (Input.Unit.bComponent ? "VsguiInstallTemplate" : "VsguiBuild");
			});
			if (Body == Context.Declarations.end())
			{
				Fail(Input.Unit.Root.Location, "gui.template_method_missing");
				return;
			}
			Bodies[Context.ModuleId] = {Body->Id, std::move(Code)};
			{
				const auto Separator = Owner->Name.rfind('.');
				const auto Namespace =
				    Separator == std::string::npos ? std::string{} : Owner->Name.substr(0, Separator);
				const auto Leaf = Separator == std::string::npos ? Input.Name : Owner->Name.substr(Separator + 1);
				std::string Registration = "bool " + Leaf + "Register(){array<string> roots=array<string>();";
				std::set<std::string> Seen;
				std::function<void(const FGuiTypeSchema&)> AddRoot = [&](const FGuiTypeSchema& Type)
				{
					if (!Seen.insert(Type.CanonicalName).second)
					{
						return;
					}
					Registration += "roots.push(" + GuiQuote(Type.CanonicalName) + ");";
					for (const auto& Base : Type.Bases)
					{
						if (const auto* Parent = Schema->FindType(Base))
						{
							AddRoot(*Parent);
						}
					}
				};
				AddRoot(*RootType);
				const auto* ContextType =
				    !Input.Unit.Script.bInline ? Schema->FindType(Input.Unit.Script.Type) : nullptr;
				const auto Mode = Input.bInjectedContext ? "Provided" : ContextType ? "Automatic"
				                                                                    : "None";
				Registration += "return VS::Gui::FactoryRegistry::Register(VS::Gui::__NativeResources::Resolve(" +
				    GuiQuote(std::string(GuiModuleMarker) + Context.ModuleId) + "),roots," +
				    GuiQuote(ContextType ? ContextType->CanonicalName : "") + ',' + Leaf +
				    "Factory,VS::Gui::BindingContextMode::" + Mode + ");}";
				Output.AddDeclaration({Context.ModuleId, Namespace, {}, Context.Snapshot}, std::move(Registration));
				Output.AddDeclaration({Context.ModuleId, Namespace, {}, Context.Snapshot},
				    "bool " + Leaf + "Registered=" + Leaf + "Register();");
			}
		}
	} // namespace

	FCompileProcessorDesc MakeGuiMarkupProcessor(std::shared_ptr<const FGuiCompilationConfiguration> Configuration,
	    std::string ConfigurationKey, bool bImportFramework)
	{
		FCompileProcessorDesc Result;
		Result.ProcessorId = "veroscript.gui.markup";
		Result.Version = "0.1.1";
		Result.ConfigurationKey = std::move(ConfigurationKey) + (bImportFramework ? ":source-imports" : "");
		Result.Phases =
		    CompilePhaseMask(ECompileProcessingPhase::RawSource) | CompilePhaseMask(ECompileProcessingPhase::Types) |
		    CompilePhaseMask(ECompileProcessingPhase::Members) | CompilePhaseMask(ECompileProcessingPhase::Bodies);
		Result.Operations = CompileOperationMask(ECompileOperation::EditSource) |
		    CompileOperationMask(ECompileOperation::ReplaceDeclaration) |
		    CompileOperationMask(ECompileOperation::ReplaceBody) |
		    CompileOperationMask(ECompileOperation::AddDeclaration);
		FCompileSubscription Subscription;
		Subscription.Event = ECompileProcessingEvent::Module;
		Result.Subscriptions.push_back(Subscription);
		Result.Order = -100;
		Result.Factory = [Configuration = std::move(Configuration), bImportFramework]
		{
			return std::make_unique<FMarkupProcessor>(Configuration, bImportFramework);
		};
		return Result;
	}
} // namespace VeroScript::Gui

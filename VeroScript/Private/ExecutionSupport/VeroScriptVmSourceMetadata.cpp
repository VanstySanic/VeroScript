// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptSourceMetadata.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptBuiltins.h"
#include "TypeSupport/VeroScriptTemplateReflection.h"
#include "Parser/VeroScriptParserDetail.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <type_traits>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptVm::CallSourceMetadataBuiltin(EBuiltinId Builtin, const FValue* A, std::size_t Count)
	{
		if (Count != 4 || !A)
		{
			ReportError("invalid source metadata argument count");
			return false;
		}
		const auto* H = std::get_if<FObjectHandle>(&A[0]);
		const auto* Q = std::get_if<std::int32_t>(&A[1]);
		const auto* Index = std::get_if<std::int32_t>(&A[2]);
		const auto* Name = std::get_if<std::string>(&A[3]);
		if (!H || !*H || !Q || !Index || !Name)
		{
			ReportError("invalid source metadata arguments");
			return false;
		}
		auto& O = **H;
		if (*Q >= static_cast<std::int32_t>(ETemplateReflectionQuery::InstanceDeclarationId) && *Q <= static_cast<std::int32_t>(ETemplateReflectionQuery::EnclosingArgumentName))
		{
			FValue Result;
			std::string Failure;
			if (!ActiveModule || O.ProgramOwner != ProgramOwner)
			{
				ReportError("template descriptor belongs to another program");
				return false;
			}
			if (!QueryTemplateInstanceMetadata(*ActiveModule, O, static_cast<ETemplateReflectionQuery>(*Q), *Index, Result, Failure))
			{
				ReportError(std::move(Failure));
				return false;
			}
			Push(std::move(Result));
			return true;
		}
		if (*Q >= static_cast<std::int32_t>(Builtins::EReflectionQuery::TypeDisplayName) && *Q <= static_cast<std::int32_t>(Builtins::EReflectionQuery::TypeNestedDeclaration))
		{
			if (!ActiveModule || ModuleMeta::TypeSimpleName(O.GetTypeName()) != "type" || O.ProgramOwner != ProgramOwner)
			{
				ReportError("invalid scoped type descriptor");
				return false;
			}
			const FTypeDeclarationScope* Scope = nullptr;
			const std::vector<FNestedTypeDeclaration>* Children = nullptr;
			const auto Find = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					if (Declaration.Name != O.GetDescriptorData().ReflectionOwnerType)
					{
						continue;
					}
					Scope = &Declaration.Scope;
					if constexpr (!std::is_same_v<std::decay_t<decltype(Declaration)>, FBytecodeEnumDesc>)
					{
						Children = &Declaration.NestedTypes;
					}
				}
			};
			Find(ActiveModule->Types);
			Find(ActiveModule->Interfaces);
			Find(ActiveModule->Enums);
			std::vector<const FNestedTypeDeclaration*> Visible;
			if (Children)
			{
				std::unordered_set<std::string_view> Grants;
				const auto& Data = O.GetDescriptorData().ReflectionSignature;
				std::size_t Offset = 0;
				while (Offset < Data.size())
				{
					std::size_t Length = 0;
					const auto Begin = Offset;
					while (Offset < Data.size() && Data[Offset] >= '0' && Data[Offset] <= '9')
					{
						if (Length > Data.size() / 10)
						{
							break;
						}
						Length = Length * 10 + static_cast<std::size_t>(Data[Offset++] - '0');
					}
					if (Offset == Begin || Offset >= Data.size() || Data[Offset++] != ':' || Length > Data.size() - Offset)
					{
						ReportError("invalid scoped type access binding");
						return false;
					}
					Grants.emplace(Data.data() + Offset, Length);
					Offset += Length;
				}
				for (const auto& Child : *Children)
				{
					if (O.GetDescriptorData().ReflectionMemberName == "$scope-bound" ? Grants.count(Child.Scope.SimpleName) != 0
					                                                                 : Child.Scope.Access == EMemberAccess::Public)
					{
						Visible.push_back(&Child);
					}
				}
			}
			const auto Query = static_cast<Builtins::EReflectionQuery>(*Q);
			const FNestedTypeDeclaration* Child = *Index >= 0 && static_cast<std::size_t>(*Index) < Visible.size()
			    ? Visible[static_cast<std::size_t>(*Index)]
			    : nullptr;
			switch (Query)
			{
			case Builtins::EReflectionQuery::TypeDisplayName:
				{
					Push(Scope && !Scope->DisplayName.empty() ? Scope->DisplayName : O.GetDescriptorData().ReflectionOwnerType);
					return true;
				}
			case Builtins::EReflectionQuery::TypeIsNested:
				{
					Push(Scope && !Scope->OwnerTypeName.empty());
					return true;
				}
			case Builtins::EReflectionQuery::TypeIsLocal:
				{
					Push(Scope && Scope->bLocal);
					return true;
				}
			case Builtins::EReflectionQuery::TypeDeclaringType:
				{
					Push(Scope ? Scope->OwnerTypeName : std::string{});
					return true;
				}
			case Builtins::EReflectionQuery::TypeDeclaringFunction:
				{
					Push(Scope ? Scope->OwnerFunctionName : std::string{});
					return true;
				}
			case Builtins::EReflectionQuery::TypeAccess:
				{
					Push(std::string(!Scope || Scope->OwnerTypeName.empty() ? "none" : Scope->Access == EMemberAccess::Public ? "public"
					        : Scope->Access == EMemberAccess::Protected                                                       ? "protected"
					                                                                                                          : "private"));
					return true;
				}
			case Builtins::EReflectionQuery::TypeNestedCount:
				{
					Push(static_cast<std::int32_t>(Visible.size()));
					return true;
				}
			case Builtins::EReflectionQuery::TypeNestedName:
				{
					if (Child)
					{
						Push(Child->Scope.SimpleName);
						return true;
					}
					break;
				}
			case Builtins::EReflectionQuery::TypeNestedParameterCount:
				{
					if (Child)
					{
						Push(static_cast<std::int32_t>(Child->Scope.OwnTypeParameterCount));
						return true;
					}
					break;
				}
			case Builtins::EReflectionQuery::TypeNestedDeclaration:
				{
					std::uint16_t TypeIndex = 0;
					if (Child && ModuleMeta::FindTypeIndex(*ActiveModule, "source_node_info", TypeIndex))
					{
						auto Result = CreateObject(TypeIndex, {});
						if (!Result) return false;
						auto Data = std::make_shared<FScriptObjectDescriptorData>();
						Data->SourceMetadataSnapshot = O.GetDescriptorData().SourceMetadataSnapshot;
						Data->SourceMetadataNodeId = Child->DeclarationId;
						Result->DescriptorData = std::move(Data);
						Push(Result);
						return true;
					}
					break;
				}
			default:
				{
					break;
				}
			}
			ReportError("nested type index out of range");
			return false;
		}
		BindSourceMetadataDescriptor(O);
		const auto S = O.GetDescriptorData().SourceMetadataSnapshot;
		const auto* Node = S ? S->FindNode(O.GetDescriptorData().SourceMetadataNodeId) : nullptr;
		const FDeclarationAnnotation* Annotation = nullptr;
		const FAnnotationArgument* Arg = nullptr;
		const FSourceComment* Comment = nullptr;
		const FAnnotationDefinition* Definition = nullptr;
		const FAnnotationArgumentDefinition* DefinitionArg = nullptr;
		if (Node && !O.GetDescriptorData().SourceMetadataPath.empty())
		{
			const auto I = O.GetDescriptorData().SourceMetadataPath.front();
			if ((O.GetDescriptorData().SourceMetadataCategory == 1 || O.GetDescriptorData().SourceMetadataCategory == 2 || O.GetDescriptorData().SourceMetadataCategory == 4) && I < Node->Annotations.size())
			{
				Annotation = &Node->Annotations[I];
				if (O.GetDescriptorData().SourceMetadataCategory == 2)
				{
					const auto* Children = &Annotation->Arguments;
					for (std::size_t P = 1; P < O.GetDescriptorData().SourceMetadataPath.size(); ++P)
					{
						auto J = O.GetDescriptorData().SourceMetadataPath[P];
						if (J >= Children->size())
						{
							Arg = nullptr;
							break;
						}
						Arg = &(*Children)[J];
						Children = &Arg->Children;
					}
				}
				if (S)
				{
					Definition = S->FindDefinition(Annotation->DefinitionId);
				}
			}
			if (O.GetDescriptorData().SourceMetadataCategory == 3 && I < Node->Comments.size())
			{
				Comment = &Node->Comments[I];
			}
		}
		if (Definition && O.GetDescriptorData().SourceMetadataCategory == 4 && O.GetDescriptorData().SourceMetadataPath.size() > 1)
		{
			const auto* Children = &Definition->Arguments;
			for (std::size_t I = 1; I < O.GetDescriptorData().SourceMetadataPath.size(); ++I)
			{
				auto J = O.GetDescriptorData().SourceMetadataPath[I];
				if (J >= Children->size())
				{
					DefinitionArg = nullptr;
					break;
				}
				DefinitionArg = &(*Children)[J];
				Children = &DefinitionArg->Children;
			}
		}
		const bool IsDefinition = O.GetDescriptorData().SourceMetadataCategory == 4;
		const auto* RootDefinition = IsDefinition && O.GetDescriptorData().SourceMetadataPath.size() == 1 ? Definition : nullptr;
		const auto Query = static_cast<ESourceMetadataQuery>(*Q);
		const bool Valid = O.GetDescriptorData().SourceMetadataCategory == 0 ? Node != nullptr
		    : O.GetDescriptorData().SourceMetadataCategory == 1              ? Annotation != nullptr
		    : O.GetDescriptorData().SourceMetadataCategory == 2              ? Arg != nullptr
		    : O.GetDescriptorData().SourceMetadataCategory == 3
		    ? Comment != nullptr
		    : (O.GetDescriptorData().SourceMetadataPath.size() > 1 ? DefinitionArg != nullptr : Definition != nullptr);
		auto Fail = [&](const char* Message)
		{
			ReportError(Message);
			return false;
		};
		auto ChildNodes = [&](bool Params)
		{
			std::vector<const FSourceNodeMetadata*> Ns;
			if (S && Node)
			{
				for (const auto& N : S->Nodes)
				{
					if ((N.OwnerId == O.GetDescriptorData().SourceMetadataNodeId || N.ParentId == O.GetDescriptorData().SourceMetadataNodeId) &&
					    (Params ? N.Target == AnnotationTargetMask(EAnnotationTarget::Parameter)
					            : N.Kind != "declaration"))
					{
						Ns.push_back(&N);
					}
				}
			}
			return Ns;
		};
		if (Builtin >= EBuiltinId::SourceMetadataNode)
		{
			const char* Type = Builtin == EBuiltinId::SourceMetadataNode ? "source_node_info" : Builtin == EBuiltinId::SourceMetadataAnnotation ? "annotation_info"
			    : Builtin == EBuiltinId::SourceMetadataArgument                                                                                 ? "annotation_argument_info"
			    : Builtin == EBuiltinId::SourceMetadataComment                                                                                  ? "comment_info"
			                                                                                                                                    : "annotation_definition_info";
			std::uint16_t TI = 0;
			bool Ambiguous = false;
			if (!ActiveModule || !ModuleMeta::FindTypeIndex(*ActiveModule, Type, TI, &Ambiguous)) return Fail("source metadata information type is unavailable");
			auto Result = CreateObject(TI, {});
			if (!Result) return false;
			auto Data = std::make_shared<FScriptObjectDescriptorData>();
			Data->SourceMetadataSnapshot = S;
			Data->SourceMetadataNodeId = O.GetDescriptorData().SourceMetadataNodeId;
			Data->SourceMetadataPath = O.GetDescriptorData().SourceMetadataPath;
			Data->SourceMetadataCategory = Builtin == EBuiltinId::SourceMetadataAnnotation ? 1
			    : Builtin == EBuiltinId::SourceMetadataArgument                            ? 2
			    : Builtin == EBuiltinId::SourceMetadataComment                             ? 3
			    : Builtin == EBuiltinId::SourceMetadataDefinition                          ? 4
			                                                                               : 0;
			if (Builtin == EBuiltinId::SourceMetadataAnnotation)
			{
				if (!Node || *Index < 0 || static_cast<std::size_t>(*Index) >= Node->Annotations.size())
				{
					return Fail("annotation index out of range");
				}
				Data->SourceMetadataPath = {static_cast<std::uint32_t>(*Index)};
			}
			else if (Builtin == EBuiltinId::SourceMetadataArgument)
			{
				const auto* List = Arg ? &Arg->Children : Annotation ? &Annotation->Arguments
				                                                     : nullptr;
				if (!List || *Index < 0 || static_cast<std::size_t>(*Index) >= List->size())
				{
					return Fail("annotation argument index out of range");
				}
				Data->SourceMetadataPath.push_back(static_cast<std::uint32_t>(*Index));
			}
			else if (Builtin == EBuiltinId::SourceMetadataComment)
			{
				if (!Node || *Index < 0 || static_cast<std::size_t>(*Index) >= Node->Comments.size())
				{
					return Fail("comment index out of range");
				}
				Data->SourceMetadataPath = {static_cast<std::uint32_t>(*Index)};
			}
			else if (Builtin == EBuiltinId::SourceMetadataDefinition && *Q == 1)
			{
				const auto* List = DefinitionArg ? &DefinitionArg->Children
				    : RootDefinition             ? &RootDefinition->Arguments
				                                 : nullptr;
				if (!List || *Index < 0 || static_cast<std::size_t>(*Index) >= List->size())
				{
					return Fail("annotation description index out of range");
				}
				Data->SourceMetadataPath.push_back(static_cast<std::uint32_t>(*Index));
			}
			else if (Builtin == EBuiltinId::SourceMetadataDefinition && *Q == 2)
			{
				if (!Annotation || O.GetDescriptorData().SourceMetadataCategory != 2)
				{
					return Fail("argument description requires an annotation argument");
				}
				Data->SourceMetadataPath = {O.GetDescriptorData().SourceMetadataPath.front()};
				const auto* Values = &Annotation->Arguments;
				const auto* Descriptions = Definition ? &Definition->Arguments : nullptr;
				for (std::size_t P = 1; P < O.GetDescriptorData().SourceMetadataPath.size(); ++P)
				{
					const auto I = O.GetDescriptorData().SourceMetadataPath[P];
					if (I >= Values->size()) return Fail("invalid annotation argument path");
					const auto& Value = (*Values)[I];
					std::size_t Match = Descriptions ? Descriptions->size() : 0;
					if (Descriptions)
					{
						for (std::size_t J = 0; J < Descriptions->size(); ++J)
						{
							if ((*Descriptions)[J].Name == Value.Name)
							{
								Match = J;
								break;
							}
						}
					}

					if (!Descriptions || Match == Descriptions->size())
					{
						Data->SourceMetadataPath.push_back(UINT32_MAX);
						break;
					}
					Data->SourceMetadataPath.push_back(static_cast<std::uint32_t>(Match));
					Descriptions = &(*Descriptions)[Match].Children;
					Values = &Value.Children;
				}
			}
			else if (Builtin == EBuiltinId::SourceMetadataNode && *Q != 0)
			{
				auto Ns = ChildNodes(*Q == 2);
				if (*Index < 0 || static_cast<std::size_t>(*Index) >= Ns.size())
				{
					return Fail("source metadata node index out of range");
				}
				Data->SourceMetadataNodeId = Ns[static_cast<std::size_t>(*Index)]->Id;
				Data->SourceMetadataPath.clear();
			}
			Result->DescriptorData = std::move(Data);
			Push(Result);
			return true;
		}
		if (O.GetDescriptorData().SourceMetadataCategory != 4)
		{
			Definition = nullptr;
			DefinitionArg = nullptr;
		}
		if (Builtin == EBuiltinId::SourceMetadataBool)
		{
			if (Query == ESourceMetadataQuery::MatchesName)
			{
				Push(Annotation && S && S->MatchesAnnotationName(*Annotation, *Name));
				return true;
			}
			if (Query == ESourceMetadataQuery::CaseSensitive)
			{
				Push(!RootDefinition || RootDefinition->CaseSensitive);
				return true;
			}
			if (Query == ESourceMetadataQuery::IsValid)
			{
				Push(Valid);
				return true;
			}
			if (Query == ESourceMetadataQuery::CommentsAvailable)
			{
				Push(Node &&
				    (Node->CommentRetention == ECommentRetention::Full ||
				        Node->CommentRetention == ECommentRetention::Documentation));
				return true;
			}
			if (Query == ESourceMetadataQuery::BoolValue && Arg &&
			    Arg->Kind == EAnnotationArgumentKind::Scalar &&
			    Arg->ScalarKind == EAnnotationScalarKind::Boolean &&
			    (Arg->Value == "true" || Arg->Value == "false"))
			{
				Push(Arg->Value == "true");
				return true;
			}
			return Fail("metadata value is not boolean");
		}
		if (Builtin == EBuiltinId::SourceMetadataInt)
		{
			std::int32_t V = 0;
			switch (Query)
			{
			case ESourceMetadataQuery::AnnotationCount:
				{
					V = Node ? static_cast<std::int32_t>(Node->Annotations.size()) : 0;
					break;
				}
			case ESourceMetadataQuery::CommentCount:
				{
					V = Node ? static_cast<std::int32_t>(Node->Comments.size()) : 0;
					break;
				}
			case ESourceMetadataQuery::ArgumentCount:
				{

					if (IsDefinition)
					{
						V = DefinitionArg    ? static_cast<std::int32_t>(DefinitionArg->Children.size())
						    : RootDefinition ? static_cast<std::int32_t>(RootDefinition->Arguments.size())
						                     : 0;
					}

					else
					{
						V = Arg          ? static_cast<std::int32_t>(Arg->Children.size())
						    : Annotation ? static_cast<std::int32_t>(Annotation->Arguments.size())
						                 : 0;
					}

					break;
				}
			case ESourceMetadataQuery::Kind:
				{
					V = Arg       ? static_cast<std::int32_t>(Arg->Kind)
					    : Comment ? static_cast<std::int32_t>(Comment->Kind)
					              : 0;
					break;
				}
			case ESourceMetadataQuery::ScalarKind:
				{
					V = Arg ? static_cast<std::int32_t>(Arg->ScalarKind) : 0;
					break;
				}
			case ESourceMetadataQuery::Activity:
				{
					V = Comment ? static_cast<std::int32_t>(Comment->Activity) : 0;
					break;
				}
			case ESourceMetadataQuery::Association:
				{
					V = Comment ? static_cast<std::int32_t>(Comment->Association) : 0;
					break;
				}
			case ESourceMetadataQuery::CommentRetention:
				{
					V = Node ? static_cast<std::int32_t>(Node->CommentRetention) : 3;
					break;
				}
			case ESourceMetadataQuery::ParameterCount:
				{
					V = static_cast<std::int32_t>(ChildNodes(true).size());
					break;
				}
			case ESourceMetadataQuery::TargetMask:
				{
					V = RootDefinition ? static_cast<std::int32_t>(RootDefinition->Targets) : 0;
					break;
				}
			case ESourceMetadataQuery::SourceNodeCount:
				{
					V = static_cast<std::int32_t>(ChildNodes(false).size());
					break;
				}
			case ESourceMetadataQuery::BeginLine:
				{
					V = static_cast<std::int32_t>(Comment ? Comment->Range.Position.Begin.Line
					        : Arg                         ? Arg->Range.Begin.Line
					        : Annotation                  ? Annotation->Range.Begin.Line
					        : Node                        ? Node->Range.Position.Begin.Line
					                                      : 0);
					break;
				}
			case ESourceMetadataQuery::BeginColumn:
				{
					V = static_cast<std::int32_t>(Comment ? Comment->Range.Position.Begin.Column
					        : Arg                         ? Arg->Range.Begin.Column
					        : Annotation                  ? Annotation->Range.Begin.Column
					        : Node                        ? Node->Range.Position.Begin.Column
					                                      : 0);
					break;
				}
			case ESourceMetadataQuery::EndLine:
				{
					V = static_cast<std::int32_t>(Comment ? Comment->Range.Position.End.Line
					        : Arg                         ? Arg->Range.End.Line
					        : Annotation                  ? Annotation->Range.End.Line
					        : Node                        ? Node->Range.Position.End.Line
					                                      : 0);
					break;
				}
			case ESourceMetadataQuery::EndColumn:
				{
					V = static_cast<std::int32_t>(Comment ? Comment->Range.Position.End.Column
					        : Arg                         ? Arg->Range.End.Column
					        : Annotation                  ? Annotation->Range.End.Column
					        : Node                        ? Node->Range.Position.End.Column
					                                      : 0);
					break;
				}
			case ESourceMetadataQuery::OriginLine:
				{
					V = static_cast<std::int32_t>(Comment ? Comment->Origin.Range.Position.Begin.Line
					        : Node                        ? Node->Origin.Range.Position.Begin.Line
					                                      : 0);
					break;
				}
			case ESourceMetadataQuery::OriginColumn:
				{
					V = static_cast<std::int32_t>(Comment ? Comment->Origin.Range.Position.Begin.Column
					        : Node                        ? Node->Origin.Range.Position.Begin.Column
					                                      : 0);
					break;
				}
			default:
				return Fail("invalid integer metadata query");
			}
			Push(V);
			return true;
		}
		if (Builtin == EBuiltinId::SourceMetadataInt64)
		{
			std::int64_t V = 0;
			if (!Arg || !AnnotationArgumentInt64(*Arg, V))
			{
				return Fail("metadata integer is outside int64 or has the wrong kind");
			}
			Push(V);
			return true;
		}
		if (Builtin == EBuiltinId::SourceMetadataUInt64)
		{
			std::uint64_t V = 0;
			if (!Arg || !AnnotationArgumentUInt64(*Arg, V))
			{
				return Fail("metadata integer is outside uint64 or has the wrong kind");
			}
			Push(V);
			return true;
		}
		if (Builtin == EBuiltinId::SourceMetadataFloat)
		{
			if (!Arg || Arg->Kind != EAnnotationArgumentKind::Scalar ||
			    Arg->ScalarKind != EAnnotationScalarKind::Float)
			{
				return Fail("metadata value is not floating point");
			}
			double V = 0;
			std::string ParseError;
			if (!ParserDetail::ParseFloatLiteral(Arg->Value, V, ParseError) || !std::isfinite(V))
			{
				return Fail("invalid floating metadata value");
			}
			Push(V);
			return true;
		}
		if (Builtin == EBuiltinId::SourceMetadataString)
		{
			std::string V;
			switch (Query)
			{
			case ESourceMetadataQuery::Name:
				{

					if (IsDefinition)
					{
						V = DefinitionArg ? DefinitionArg->Name : RootDefinition ? RootDefinition->Name
						                                                         : "";
					}

					else
					{
						V = Arg ? Arg->Name : Annotation ? Annotation->Name
						    : Node                       ? Node->Name
						                                 : "";
					}

					break;
				}
			case ESourceMetadataQuery::RawText:
				{
					V = Comment      ? Comment->RawText
					    : Arg        ? Arg->RawText
					    : Annotation ? Annotation->RawText
					                 : "";
					break;
				}
			case ESourceMetadataQuery::Text:
				{
					V = Comment ? Comment->Text : "";
					break;
				}
			case ESourceMetadataQuery::Value:
				{
					if (!Arg || Arg->Kind != EAnnotationArgumentKind::Scalar)
					{
						return Fail("metadata argument is not scalar");
					}
					V = Arg->Value;
					break;
				}
			case ESourceMetadataQuery::Documentation:
				{
					V = IsDefinition ? (DefinitionArg           ? DefinitionArg->Documentation
					                           : RootDefinition ? RootDefinition->Documentation
					                                            : "")
					                 : (Node ? Node->Documentation : "");
					break;
				}
			case ESourceMetadataQuery::Detail:
				{
					V = DefinitionArg ? DefinitionArg->Detail : RootDefinition ? RootDefinition->Detail
					                                                           : "";
					break;
				}
			case ESourceMetadataQuery::Provider:
				{
					V = IsDefinition && Valid && Definition ? Definition->ProviderId : "";
					break;
				}
			case ESourceMetadataQuery::SourceId:
				{
					V = Comment      ? Comment->Range.SourceId
					    : Arg        ? Arg->Range.Begin.FileLabel
					    : Annotation ? Annotation->Range.Begin.FileLabel
					    : Node       ? Node->Range.SourceId
					                 : "";
					break;
				}
			case ESourceMetadataQuery::NodeId:
				{
					V = Node ? Node->Id : "";
					break;
				}
			case ESourceMetadataQuery::OwnerId:
				{
					V = Node ? Node->OwnerId : "";
					break;
				}
			case ESourceMetadataQuery::ParentId:
				{
					V = Node ? Node->ParentId : "";
					break;
				}
			case ESourceMetadataQuery::OriginSourceId:
				{
					V = Comment ? Comment->Origin.Range.SourceId : Node ? Node->Origin.Range.SourceId
					                                                    : "";
					break;
				}
			case ESourceMetadataQuery::OriginProcessorId:
				{
					V = Comment ? Comment->Origin.ProcessorId : Node ? Node->Origin.ProcessorId
					                                                 : "";
					break;
				}
			case ESourceMetadataQuery::OriginGenerationId:
				{
					V = Comment ? Comment->Origin.GenerationId : Node ? Node->Origin.GenerationId
					                                                  : "";
					break;
				}
			case ESourceMetadataQuery::TypeName:
				{
					V = Node ? Node->TypeName : "";
					break;
				}
			case ESourceMetadataQuery::Kind:
				{
					V = Node ? Node->Kind : "";
					break;
				}
			default:
				return Fail("invalid text metadata query");
			}
			Push(std::move(V));
			return true;
		}
		return Fail("invalid source metadata builtin");
	}
	void VeroScriptVm::BindSourceMetadataDescriptor(FScriptObject& O)
	{
		if (!ActiveModule || !O.GetDescriptorData().SourceMetadataNodeId.empty()) return;
		auto Data = O.GetDescriptorData();
		if (!Data.SourceMetadataSnapshot)
		{
			Data.SourceMetadataSnapshot = ActiveModule->SourceMetadata;
		}

		if (!Data.SourceMetadataSnapshot) return;
		auto StringField = [&](const char* Name)
		{
			std::uint16_t I = 0;
			if (ModuleMeta::FindFieldIndex(*ActiveModule, O.GetTypeName(), Name, I) && I < O.Fields.size())
			{
				if (const auto* S = std::get_if<std::string>(&O.Fields[I])) return *S;
			}
			return std::string{};
		};
		auto IndexField = [&]
		{
			std::uint16_t I = 0;
			if (ModuleMeta::FindFieldIndex(*ActiveModule, O.GetTypeName(), "index", I) && I < O.Fields.size())
			{
				if (const auto* V = std::get_if<std::int32_t>(&O.Fields[I])) return *V;
			}
			return std::int32_t{0};
		};
		auto Kind = ModuleMeta::TypeSimpleName(O.GetTypeName());
		const auto Less = Kind.find('<');
		if (Less != std::string::npos)
		{
			Kind.erase(Less);
		}

		std::string Owner = StringField("owner_name");
		FDeclMeta OwnedMeta;
		const FDeclMeta* Meta = nullptr;
		const auto Index = IndexField();
		if (Kind == "type" || Kind == "field_info" || Kind == "method_info" || Kind == "enum_member_info")
		{
			if (Kind == "type")
			{
				Owner = StringField("qualified_name");
			}

			std::uint16_t TI = 0;
			bool Ambiguous = false;
			if (ModuleMeta::FindTypeIndex(*ActiveModule, Owner, TI, &Ambiguous) && TI < ActiveModule->Types.size())
			{
				const auto& T = ActiveModule->Types[TI];
				if (Kind == "type")
				{
					Meta = &T.DeclMeta;
				}
			}
			if (Kind == "field_info")
			{
				std::vector<FLoadedFieldInfo> Fields;
				if (ModuleMeta::EnumerateFields(*ActiveModule, Owner, Fields, false) && Index >= 0 &&
				    static_cast<std::size_t>(Index) < Fields.size())
				{
					OwnedMeta = Fields[Index].DeclMeta;
					Meta = &OwnedMeta;
				}
			}
			if (Kind == "method_info" && Index >= 0)
			{
				std::vector<FInstanceMethodInfo> Methods;
				std::vector<FStaticMethodInfo> Statics;
				ModuleMeta::EnumerateInstanceMethods(*ActiveModule, Owner, Methods, true, true, true);
				ModuleMeta::EnumerateStaticMethods(*ActiveModule, Owner, Statics);
				const auto I = static_cast<std::size_t>(Index);
				if (I < Methods.size())
				{
					OwnedMeta = Methods[I].DeclMeta;
					Meta = &OwnedMeta;
				}
				else if (I - Methods.size() < Statics.size())
				{
					OwnedMeta = Statics[I - Methods.size()].DeclMeta;
					Meta = &OwnedMeta;
				}
			}
			if (!Meta)
			{
				for (const auto& T : ActiveModule->Interfaces)
				{
					if (T.Name == Owner && Kind == "type")
					{
						Meta = &T.DeclMeta;
					}
				}
			}

			if (!Meta)
			{
				for (const auto& T : ActiveModule->Enums)
				{
					if (T.Name == Owner)
					{
						if (Kind == "type")
						{
							Meta = &T.DeclMeta;
						}
						else if (Kind == "enum_member_info")
						{
							auto I = IndexField();
							if (I >= 0 && static_cast<std::size_t>(I) < T.Members.size())
							{
								Meta = &T.Members[I].DeclMeta;
							}
						}
					}
				}
			}
		}
		else if (Kind == "function_info")
		{
			std::vector<FFreeFunctionInfo> Functions;
			ModuleMeta::EnumerateFreeFunctions(*ActiveModule, Functions, true);
			if (Index >= 0 && static_cast<std::size_t>(Index) < Functions.size())
			{
				OwnedMeta = Functions[Index].DeclMeta;
				Meta = &OwnedMeta;
			}
		}
		else if (Kind == "global_info")
		{
			std::vector<FLoadedGlobalInfo> SourceGlobals;
			ModuleMeta::EnumerateGlobals(*ActiveModule, SourceGlobals, true, false);
			if (Index >= 0 && static_cast<std::size_t>(Index) < SourceGlobals.size())
			{
				OwnedMeta = SourceGlobals[Index].DeclMeta;
				Meta = &OwnedMeta;
			}
		}
		else if (Kind == "module_info")
		{
			Data.SourceMetadataNodeId = "module:" + (Owner.empty() ? ActiveModule->EntryModulePath : Owner);
		}

		if (Meta)
		{
			Data.SourceMetadataNodeId = Meta->StableId;
		}
		O.DescriptorData = std::make_shared<const FScriptObjectDescriptorData>(std::move(Data));
	}
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAst.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptSourceMetadata.h"
#include "VeroScriptBuiltins.h"
#include "TypeSupport/VeroScriptTemplateReflection.h"
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptCompiler::RegisterSourceMetadataTemplates(const FProgram& Program)
	{
		const std::vector<std::string> OrderedNames={"source_node_info","annotation_info","annotation_argument_info","comment_info","annotation_definition_info"};
		const std::unordered_set<std::string> Names(OrderedNames.begin(), OrderedNames.end());
		auto Call = [](const char* Function, ESourceMetadataQuery Q, std::string Index = "0")
		{
			return std::string(Function) + "(this," + std::to_string(static_cast<int>(Q)) + "," + Index +
				",\"\")";
		};
		auto Scalar = [&](const char* T, const char* Name, const char* Function, ESourceMetadataQuery Q)
		{
			return std::string(T) + " " + Name + "() const {return " + Call(Function, Q) + ";}";
		};
		auto Info = [](const char* T, const char* Name, const char* Function, int Query, bool Indexed)
		{
			return std::string(T) + " " + Name + "(" + (Indexed ? "int index" : "") + ") const {return " +
				Function + "(this," + std::to_string(Query) + "," + (Indexed ? "index" : "0") + ",\"\");}";
		};
		auto Array = [](const char* T, const char* Name, const char* CountFunction, int Query,
						 const char* Getter, int GetterQuery = 0)
		{
			return std::string("array<") + T + "> " + Name + "() const {array<" + T +
				"> values={};int count=" + CountFunction + "(this," + std::to_string(Query) +
				",0,\"\");for(int i=0;i<count;i++){values.push(" + Getter + "(this," +
				std::to_string(GetterQuery) + ",i,\"\"));}return values;}";
		};
		auto Common = [&]
		{
			std::string S = Scalar(
				"int", "annotation_count", "__source_metadata_int", ESourceMetadataQuery::AnnotationCount);
			S+=Info("annotation_info","annotation","__source_metadata_annotation",0,true);
			S+=Array("annotation_info","annotations","__source_metadata_int",static_cast<int>(ESourceMetadataQuery::AnnotationCount),"__source_metadata_annotation");
			S+="array<annotation_info> find_annotations(string name) const {array<annotation_info> values={};int count=__source_metadata_int(this,"+std::to_string(static_cast<int>(ESourceMetadataQuery::AnnotationCount))+",0,\"\");for(int i=0;i<count;i++){annotation_info item=__source_metadata_annotation(this,0,i,\"\");if(item.matches_name(name)){values.push(item);}}return values;}";
			S+=Scalar("int","comment_count","__source_metadata_int",ESourceMetadataQuery::CommentCount);
			S+=Info("comment_info","comment","__source_metadata_comment",0,true);
			S+=Array("comment_info","comments","__source_metadata_int",static_cast<int>(ESourceMetadataQuery::CommentCount),"__source_metadata_comment");
			S+=Scalar("string","documentation","__source_metadata_string",ESourceMetadataQuery::Documentation);
			S+=Scalar("bool","comments_available","__source_metadata_bool",ESourceMetadataQuery::CommentsAvailable);
			S+=Scalar("int","comments_retention","__source_metadata_int",ESourceMetadataQuery::CommentRetention);
			S+=Array("source_node_info","source_metadata","__source_metadata_int",static_cast<int>(ESourceMetadataQuery::SourceNodeCount),"__source_metadata_node",1);
			return S;
		};
		auto Add = [&](FStructDecl& D, const std::string& Methods)
		{
			const std::string Text = "class SourceMetadataMethodHolder {public:" + Methods + "}";
			VeroScriptLexer Lexer(Text,"veroscript-source-metadata");
			const auto Tokens=Lexer.Tokenize();
			VeroScriptParser Parser(Tokens, "", &Program);
			Parser.SetHostTypeNames(Names);
			Parser.SetAllowInternalBuiltins(true);
			auto P = Parser.ParseProgram();
			if (!P || Parser.HasError())
			{
				ReportError("invalid compiler metadata methods: " + Parser.GetError().Message);
				return false;
			}
			for (auto& M : P->Classes.front().Methods)
			{
				D.Methods.push_back(std::move(M));
			}

			return true;
		};
		for(const auto& N:OrderedNames)
		{
			const auto HasName = [&](const auto& Types)
			{
				for (const auto& T : Types)
				{
					if (T.Name == N && TemplateParameterNames(T).empty()) return true;
				}
				return false;
			};
			if (HasName(Program.Structs) || HasName(Program.Classes) || HasName(Program.Interfaces))
			{
				continue;
			}

			FStructDecl D;
			D.Name = N;
			D.bExported = true;
			std::string S;
			S+=Scalar("bool","is_valid","__source_metadata_bool",ESourceMetadataQuery::IsValid);
			S+=Scalar("string","name","__source_metadata_string",ESourceMetadataQuery::Name);
			S+=Scalar("string","source_id","__source_metadata_string",ESourceMetadataQuery::SourceId);
			S+=Scalar("int","line","__source_metadata_int",ESourceMetadataQuery::BeginLine);
			S += Scalar("int", "column", "__source_metadata_int", ESourceMetadataQuery::BeginColumn);
			if (N == "source_node_info" || N == "comment_info")
			{
				S += Scalar("string", "origin_source_id", "__source_metadata_string",
					ESourceMetadataQuery::OriginSourceId);
				S+=Scalar("string","origin_processor_id","__source_metadata_string",ESourceMetadataQuery::OriginProcessorId);
				S+=Scalar("string","generation_id","__source_metadata_string",ESourceMetadataQuery::OriginGenerationId);
				S+=Scalar("int","origin_line","__source_metadata_int",ESourceMetadataQuery::OriginLine);
				S+=Scalar("int","origin_column","__source_metadata_int",ESourceMetadataQuery::OriginColumn);
			}
			if(N=="source_node_info")
			{
				S+=Common();
				S+=Scalar("string","kind","__source_metadata_string",ESourceMetadataQuery::Kind);
				S+=Scalar("string","id","__source_metadata_string",ESourceMetadataQuery::NodeId);
				S+=Scalar("string","owner_id","__source_metadata_string",ESourceMetadataQuery::OwnerId);
				S+=Scalar("string","parent_id","__source_metadata_string",ESourceMetadataQuery::ParentId);
				S+=Scalar("string","type_name","__source_metadata_string",ESourceMetadataQuery::TypeName);
				S+=Scalar("int","parameter_count","__source_metadata_int",ESourceMetadataQuery::ParameterCount);
				S+=Info("source_node_info","parameter","__source_metadata_node",2,true);
			}
			else if (N == "annotation_info" || N == "annotation_argument_info")
			{
				S+=Scalar("string","raw_text","__source_metadata_string",ESourceMetadataQuery::RawText);
				S+=Scalar("int","argument_count","__source_metadata_int",ESourceMetadataQuery::ArgumentCount);
				S+=Info("annotation_argument_info","argument","__source_metadata_argument",0,true);
				S+=Array("annotation_argument_info","arguments","__source_metadata_int",static_cast<int>(ESourceMetadataQuery::ArgumentCount),"__source_metadata_argument");
				S +=
					"array<annotation_argument_info> find_arguments(string name) const {array<annotation_argument_info> values={};int count=__source_metadata_int(this," +
					std::to_string(static_cast<int>(ESourceMetadataQuery::ArgumentCount)) +
					",0,\"\");for(int i=0;i<count;i++){annotation_argument_info item=__source_metadata_argument(this,0,i,\"\");if(item.name()==name){values.push(item);}}return values;}";
				if (N == "annotation_info")
				{
					S += Info(
						"annotation_definition_info", "definition", "__source_metadata_definition", 0, false);
					S+="bool matches_name(string name) const {return __source_metadata_bool(this,"+std::to_string(static_cast<int>(ESourceMetadataQuery::MatchesName))+",0,name);}";
				}
				else
				{
					S+=Info("annotation_definition_info","definition","__source_metadata_definition",2,false);
					S+=Scalar("int","kind","__source_metadata_int",ESourceMetadataQuery::Kind);
					S+=Scalar("int","scalar_kind","__source_metadata_int",ESourceMetadataQuery::ScalarKind);
					S+=Scalar("int64","int64_value","__source_metadata_int64",ESourceMetadataQuery::SignedValue);
					S+=Scalar("uint64","uint64_value","__source_metadata_uint64",ESourceMetadataQuery::UnsignedValue);
					S+=Scalar("double","float_value","__source_metadata_float",ESourceMetadataQuery::FloatValue);
					S+=Scalar("bool","bool_value","__source_metadata_bool",ESourceMetadataQuery::BoolValue);
					S+=Scalar("string","value","__source_metadata_string",ESourceMetadataQuery::Value);
				}
			}
			else if (N == "comment_info")
			{
				S+=Scalar("string","raw_text","__source_metadata_string",ESourceMetadataQuery::RawText);
				S+=Scalar("string","text","__source_metadata_string",ESourceMetadataQuery::Text);
				S+=Scalar("int","kind","__source_metadata_int",ESourceMetadataQuery::Kind);
				S+=Scalar("int","activity","__source_metadata_int",ESourceMetadataQuery::Activity);
				S+=Scalar("int","association","__source_metadata_int",ESourceMetadataQuery::Association);
			}
			else
			{
				S+=Scalar("string","documentation","__source_metadata_string",ESourceMetadataQuery::Documentation);
				S+=Scalar("string","detail","__source_metadata_string",ESourceMetadataQuery::Detail);
				S+=Scalar("string","provider","__source_metadata_string",ESourceMetadataQuery::Provider);
				S+=Scalar("int","argument_count","__source_metadata_int",ESourceMetadataQuery::ArgumentCount);
				S+=Scalar("int","target_mask","__source_metadata_int",ESourceMetadataQuery::TargetMask);
				S+=Scalar("bool","case_sensitive","__source_metadata_bool",ESourceMetadataQuery::CaseSensitive);
				S+=Info("annotation_definition_info","argument","__source_metadata_definition",1,true);
				S+=Array("annotation_definition_info","arguments","__source_metadata_int",static_cast<int>(ESourceMetadataQuery::ArgumentCount),"__source_metadata_definition",1);
			}
			if(!Add(D,S))return false;
			BuiltinStructTemplates.push_back(std::move(D));
		}
		for (auto& D : BuiltinStructTemplates)
		{
			if (D.Name == "type" || D.Name == "field_info" || D.Name == "method_info" ||
				D.Name == "module_info" || D.Name == "function_info" || D.Name == "global_info" ||
				D.Name == "enum_member_info")
			{
				if (D.Name == "type" || D.Name == "method_info" || D.Name == "function_info")
				{
					std::string Queries;
					const auto Query = [&](const char* Result, const char* Name, const ETemplateReflectionQuery Id, const bool Indexed = false)
					{
						Queries += std::string(Result) + " " + Name + "(" + (Indexed ? "int index" : "")
							+ ") const {return " + (Result == std::string_view("int") ? "__source_metadata_int" : "__source_metadata_string")
							+ "(this," + std::to_string(static_cast<int>(Id)) + "," + (Indexed ? "index" : "0") + ",\"\");}";
					};
					Query("string", "template_declaration_id", ETemplateReflectionQuery::InstanceDeclarationId);
					Query("int", "template_argument_count", ETemplateReflectionQuery::ArgumentCount);
					Query("string", "template_argument_encoding", ETemplateReflectionQuery::ArgumentEncoding, true);
					Query("string", "template_argument_name", ETemplateReflectionQuery::ArgumentName, true);
					Query("string", "template_argument_kind", ETemplateReflectionQuery::ArgumentKind, true);
					Query("string", "template_argument_value_type", ETemplateReflectionQuery::ArgumentValueType, true);
					Query("int", "template_enclosing_argument_count", ETemplateReflectionQuery::EnclosingArgumentCount);
					Query("string", "template_enclosing_argument_encoding", ETemplateReflectionQuery::EnclosingArgumentEncoding, true);
					Query("string", "template_enclosing_argument_name", ETemplateReflectionQuery::EnclosingArgumentName, true);
					if (!Add(D, Queries)) return false;
				}
				if (D.Name == "type")
				{
					std::string Queries;
					const auto Query = [&](const char* Result, const char* Name, const char* Builtin,
						const Builtins::EReflectionQuery Id, const bool Indexed = false)
					{
						Queries += std::string(Result) + " " + Name + "(" + (Indexed ? "int index" : "")
							+ ") const {return " + Builtin + "(this," + std::to_string(static_cast<int>(Id))
							+ "," + (Indexed ? "index" : "0") + ",\"\");}";
					};
					Query("string", "display_name", "__source_metadata_string", Builtins::EReflectionQuery::TypeDisplayName);
					Query("bool", "is_nested", "__source_metadata_bool", Builtins::EReflectionQuery::TypeIsNested);
					Query("bool", "is_local", "__source_metadata_bool", Builtins::EReflectionQuery::TypeIsLocal);
					Query("string", "declaring_type_name", "__source_metadata_string", Builtins::EReflectionQuery::TypeDeclaringType);
					Query("string", "declaring_function_name", "__source_metadata_string", Builtins::EReflectionQuery::TypeDeclaringFunction);
					Query("string", "access", "__source_metadata_string", Builtins::EReflectionQuery::TypeAccess);
					Query("int", "nested_type_count", "__source_metadata_int", Builtins::EReflectionQuery::TypeNestedCount);
					Query("string", "nested_type_name", "__source_metadata_string", Builtins::EReflectionQuery::TypeNestedName, true);
					Query("int", "nested_type_parameter_count", "__source_metadata_int", Builtins::EReflectionQuery::TypeNestedParameterCount, true);
					Query("source_node_info", "nested_type_declaration", "__source_metadata_node", Builtins::EReflectionQuery::TypeNestedDeclaration, true);
					if (!Add(D, Queries)) return false;
				}
				if (!Add(D,
						Common() +
							Info("source_node_info", "declaration", "__source_metadata_node", 0, false)))
				{
					return false;
				}
				for (auto& C : D.Constructors)
				{
					if (C.Body)
					{
						std::vector<std::unique_ptr<FExpr>> Args;
						Args.push_back(std::make_unique<FThisExpr>());
						C.Body->Statements.push_back(std::make_unique<FExprStmt>(
							std::make_unique<FCallExpr>("__reflection_bind", std::move(Args))));
					}
				}
			}
		}

		return true;
	}
}

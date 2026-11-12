// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "TypeSupport/VeroScriptTemplateReflection.h"
#include "VeroScriptCompilerVariantInternal.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VeroScript
{

	bool VeroScriptCompiler::RegisterBuiltinTemplates(const FProgram& Program)
	{
		// Soft builtins yield only when no arity-matching user type exists (soft-yield).
		// Arity is type-param count: soft pair<K,V> (2) coexists with non-generic `struct pair`;
		// soft handle<T> (1) yields to user handle with exactly one type param.
		{
			bool bUserPair = false;
			for (const FStructDecl& StructDecl : Program.Structs)
			{
				if (StructDecl.Name == "pair" && StructDecl.TypeParams.size() == 2)
				{
					bUserPair = true;
					break;
				}
			}
			if (!bUserPair)
			{
				for (const FClassDecl& ClassDecl : Program.Classes)
				{
					if (ClassDecl.Name == "pair" && ClassDecl.TypeParams.size() == 2)
					{
						bUserPair = true;
						break;
					}
				}
			}
			if (!bUserPair)
			{
				FStructDecl PairDecl;
				PairDecl.Name = "pair";
				PairDecl.ModulePath.clear();
				PairDecl.bExported = true;
				PairDecl.TypeParams = {"T", "U"};
				FFieldDecl First;
				First.TypeName = "T";
				First.Name = "first";
				First.bAllowUninitializedHostStruct = true;
				FFieldDecl Second;
				Second.TypeName = "U";
				Second.Name = "second";
				Second.bAllowUninitializedHostStruct = true;
				PairDecl.Fields.push_back(std::move(First));
				PairDecl.Fields.push_back(std::move(Second));
				BuiltinStructTemplates.push_back(std::move(PairDecl));
			}
		}
		{
			// Soft handle<T> (arity 1). Non-generic `class handle` or other arities coexist.
			bool bUserHandle = false;
			for (const FClassDecl& ClassDecl : Program.Classes)
			{
				if (ClassDecl.Name == "handle" && ClassDecl.TypeParams.size() == 1)
				{
					bUserHandle = true;
					break;
				}
			}
			if (!bUserHandle)
			{
				for (const FStructDecl& StructDecl : Program.Structs)
				{
					if (StructDecl.Name == "handle" && StructDecl.TypeParams.size() == 1)
					{
						bUserHandle = true;
						break;
					}
				}
			}
			if (!bUserHandle)
			{
				FClassDecl HandleDecl;
				HandleDecl.Name = "handle";
				HandleDecl.ModulePath.clear();
				HandleDecl.bExported = true;
				HandleDecl.TypeParams = {"T"};
				FFieldDecl ValueField;
				ValueField.TypeName = "T";
				ValueField.Name = "value";
				ValueField.bAllowUninitializedHostStruct = true;
				HandleDecl.Fields.push_back(std::move(ValueField));

				// The constructor prefix default-initializes the payload (struct T is
				// constructed; class T is null). The handle retains reference identity.
				{
					FConstructorDecl DefaultCtor;
					DefaultCtor.bDefaulted = true;
					DefaultCtor.Body = std::make_unique<FBlockStmt>();
					HandleDecl.Constructors.push_back(std::move(DefaultCtor));
				}
				{
					FConstructorDecl HandleCtor;
					FFunctionParam Param;
					Param.TypeName = "T";
					Param.Name = "v";
					HandleCtor.Parameters.push_back(std::move(Param));
					FFieldCtorInit Initializer;
					Initializer.FieldName = "value";
					Initializer.Args.push_back(std::make_unique<FIdentifierExpr>("v"));
					HandleCtor.FieldInits.push_back(std::move(Initializer));
					HandleCtor.Body = std::make_unique<FBlockStmt>();
					HandleDecl.Constructors.push_back(std::move(HandleCtor));
				}
				BuiltinClassTemplates.push_back(std::move(HandleDecl));
			}
		}
		const auto HasUserTypeNamed = [&Program](const std::string_view Name)
		{
			for (const FStructDecl& Decl : Program.Structs)
			{
				if (Decl.Name == Name)
				{
					return true;
				}
			}
			for (const FClassDecl& Decl : Program.Classes)
			{
				if (Decl.Name == Name)
				{
					return true;
				}
			}
			for (const FInterfaceDecl& Decl : Program.Interfaces)
			{
				if (Decl.Name == Name)
				{
					return true;
				}
			}
			for (const FEnumDecl& Decl : Program.Enums)
			{
				if (Decl.Name == Name)
				{
					return true;
				}
			}
			for (const FTypeAliasDecl& Decl : Program.TypeAliases)
			{
				if (Decl.Name == Name)
				{
					return true;
				}
			}
			return false;
		};
		for (const std::string_view ReservedName :
		    {"variant", "optional", "readonly", "type", "field_info", "method_info", "module_info",
		        "function_info", "global_info", "enum_member_info", "weak", "shared", "source_node_info", "annotation_info",
		        "annotation_argument_info", "comment_info", "annotation_definition_info"})
		{
			if (HasUserTypeNamed(ReservedName))
			{
				ReportError(
				    "reserved builtin type name cannot be redeclared: " + std::string(ReservedName),
				    {});
				ActiveProgram = nullptr;
				ActiveNatives = nullptr;
				ActiveBindings = nullptr;
				return false;
			}
		}
		const auto ReservedParameters = [&](const auto& Declaration)
		{
			return std::find(Declaration.TypeParams.begin(), Declaration.TypeParams.end(), "variant") != Declaration.TypeParams.end();
		};
		bool InvalidVariantName = false;
		for (const auto& Function : Program.Functions)
		{
			InvalidVariantName |= ReservedParameters(Function) || CompilerDetail::IsVariantIntrinsicName(Function.Name) || Function.Name == "__variant_empty";
		}
		for (const auto& Declaration : Program.Structs)
		{
			InvalidVariantName |= ReservedParameters(Declaration);
		}
		for (const auto& Declaration : Program.Classes)
		{
			InvalidVariantName |= ReservedParameters(Declaration);
		}
		for (const auto& Declaration : Program.Interfaces)
		{
			InvalidVariantName |= ReservedParameters(Declaration);
		}
		if (InvalidVariantName)
		{
			ReportError("variant intrinsic/type parameter name is reserved", {});
			return false;
		}
		if (!HasUserTypeNamed("optional"))
		{
			// `optional<T>` is a value type. Its representation remains private so
			// callers cannot manufacture a present/value mismatch.
			FStructDecl OptionalDecl;
			OptionalDecl.Name = "optional";
			OptionalDecl.bExported = true;
			OptionalDecl.TypeParams = {"T"};
			FFieldDecl PresentField;
			PresentField.TypeName = "bool";
			PresentField.Name = "present";
			PresentField.Access = EMemberAccess::Private;
			PresentField.DefaultValue = std::make_unique<FBooleanLiteralExpr>(false);
			OptionalDecl.Fields.push_back(std::move(PresentField));
			FFieldDecl ValueField;
			ValueField.TypeName = "T";
			ValueField.Name = "value";
			ValueField.Access = EMemberAccess::Private;
			ValueField.bAllowUninitializedHostStruct = true;
			ValueField.DeclMeta.emplace_back(std::string(UninitializedPayloadMetaKey), "");
			OptionalDecl.Fields.push_back(std::move(ValueField));
			FFieldDecl EmptyValueField;
			EmptyValueField.TypeName = "T";
			EmptyValueField.Name = "empty_value";
			EmptyValueField.Access = EMemberAccess::Private;
			EmptyValueField.bAllowUninitializedHostStruct = true;
			EmptyValueField.DeclMeta.emplace_back(std::string(UninitializedPayloadMetaKey), "");
			OptionalDecl.Fields.push_back(std::move(EmptyValueField));
			{
				FConstructorDecl Ctor;
				Ctor.Body = std::make_unique<FBlockStmt>();
				OptionalDecl.Constructors.push_back(std::move(Ctor));
			}
			{
				FConstructorDecl Ctor;
				FFunctionParam Param;
				Param.TypeName = "T";
				Param.Name = "v";
				Ctor.Parameters.push_back(std::move(Param));
				FFieldCtorInit Initializer;
				Initializer.FieldName = "value";
				Initializer.Args.push_back(std::make_unique<FIdentifierExpr>("v"));
				Ctor.FieldInits.push_back(std::move(Initializer));
				auto Body = std::make_unique<FBlockStmt>();
				Body->Statements.push_back(std::make_unique<FExprStmt>(
				    std::make_unique<FAssignExpr>(
				        std::make_unique<FIdentifierExpr>("present"),
				        std::make_unique<FBooleanLiteralExpr>(true))));
				Ctor.Body = std::move(Body);
				OptionalDecl.Constructors.push_back(std::move(Ctor));
			}
			{
				FMethodDecl Method;
				Method.ReturnType = "bool";
				Method.Name = "has_value";
				Method.bIsConst = true;
				Method.Body = std::make_unique<FBlockStmt>();
				Method.Body->Statements.push_back(std::make_unique<FReturnStmt>(
				    std::make_unique<FIdentifierExpr>("present")));
				OptionalDecl.Methods.push_back(std::move(Method));
			}
			{
				FMethodDecl Method;
				Method.ReturnType = "T";
				Method.Name = "get";
				Method.bIsConst = true;
				Method.Body = std::make_unique<FBlockStmt>();
				std::vector<std::unique_ptr<FExpr>> AssertArgs;
				AssertArgs.push_back(std::make_unique<FIdentifierExpr>("present"));
				Method.Body->Statements.push_back(std::make_unique<FExprStmt>(
				    std::make_unique<FCallExpr>("assert", std::move(AssertArgs))));
				Method.Body->Statements.push_back(std::make_unique<FReturnStmt>(
				    std::make_unique<FIdentifierExpr>("value")));
				OptionalDecl.Methods.push_back(std::move(Method));
			}
			{
				FMethodDecl Method;
				Method.ReturnType = "T";
				Method.Name = "value_or";
				Method.bIsConst = true;
				FFunctionParam Param;
				Param.TypeName = "T";
				Param.Name = "fallback";
				Method.Parameters.push_back(std::move(Param));
				Method.Body = std::make_unique<FBlockStmt>();
				Method.Body->Statements.push_back(std::make_unique<FReturnStmt>(
				    std::make_unique<FConditionalExpr>(
				        std::make_unique<FIdentifierExpr>("present"),
				        std::make_unique<FIdentifierExpr>("value"),
				        std::make_unique<FIdentifierExpr>("fallback"))));
				OptionalDecl.Methods.push_back(std::move(Method));
			}
			{
				FMethodDecl Method;
				Method.ReturnType = "void";
				Method.Name = "reset";
				Method.Body = std::make_unique<FBlockStmt>();
				Method.Body->Statements.push_back(std::make_unique<FExprStmt>(
				    std::make_unique<FAssignExpr>(
				        std::make_unique<FIdentifierExpr>("present"),
				        std::make_unique<FBooleanLiteralExpr>(false))));
				Method.Body->Statements.push_back(std::make_unique<FExprStmt>(
				    std::make_unique<FAssignExpr>(
				        std::make_unique<FIdentifierExpr>("value"),
				        std::make_unique<FIdentifierExpr>("empty_value"))));
				OptionalDecl.Methods.push_back(std::move(Method));
			}
			BuiltinStructTemplates.push_back(std::move(OptionalDecl));
		}
		if (!HasUserTypeNamed("readonly"))
		{
			// `readonly<T>` is an assignable value wrapper whose payload can only be
			// observed through a const reference. This deliberately differs from a
			// const variable: wrappers remain usable as ordinary container elements,
			// while mutation of the wrapped value requires an explicit const_cast<T>.
			FStructDecl ReadonlyDecl;
			ReadonlyDecl.Name = "readonly";
			ReadonlyDecl.bExported = true;
			ReadonlyDecl.TypeParams = {"T"};
			FFieldDecl ValueField;
			ValueField.TypeName = "T";
			ValueField.Name = "value";
			ValueField.Access = EMemberAccess::Private;
			ValueField.bAllowUninitializedHostStruct = true;
			ReadonlyDecl.Fields.push_back(std::move(ValueField));
			{
				FConstructorDecl Ctor;
				FFunctionParam Param;
				Param.TypeName = "T";
				Param.Name = "v";
				Ctor.Parameters.push_back(std::move(Param));
				FFieldCtorInit Initializer;
				Initializer.FieldName = "value";
				Initializer.Args.push_back(std::make_unique<FIdentifierExpr>("v"));
				Ctor.FieldInits.push_back(std::move(Initializer));
				Ctor.Body = std::make_unique<FBlockStmt>();
				ReadonlyDecl.Constructors.push_back(std::move(Ctor));
			}
			{
				FMethodDecl Method;
				Method.ReturnType = "const ref T";
				Method.Name = "get";
				Method.bIsConst = true;
				Method.Body = std::make_unique<FBlockStmt>();
				Method.Body->Statements.push_back(std::make_unique<FReturnStmt>(
				    std::make_unique<FIdentifierExpr>("value")));
				ReadonlyDecl.Methods.push_back(std::move(Method));
			}
			BuiltinStructTemplates.push_back(std::move(ReadonlyDecl));
		}
		const auto MakeReflectionCall = [](
		                                    const char* BuiltinName,
		                                    const Builtins::EReflectionQuery Query,
		                                    const char* OwnerName,
		                                    const char* IndexName,
		                                    const char* DetailName,
		                                    const char* QueryName) -> std::unique_ptr<FExpr>
		{
			std::vector<std::unique_ptr<FExpr>> Arguments;
			Arguments.push_back(std::make_unique<FIdentifierExpr>(OwnerName));
			Arguments.push_back(std::make_unique<FIntegerLiteralExpr>(
			    static_cast<std::int32_t>(Query)));
			Arguments.push_back(IndexName
			        ? std::unique_ptr<FExpr>(std::make_unique<FIdentifierExpr>(IndexName))
			        : std::unique_ptr<FExpr>(std::make_unique<FIntegerLiteralExpr>(0)));
			Arguments.push_back(DetailName
			        ? std::unique_ptr<FExpr>(std::make_unique<FIdentifierExpr>(DetailName))
			        : std::unique_ptr<FExpr>(std::make_unique<FIntegerLiteralExpr>(0)));
			Arguments.push_back(QueryName
			        ? std::unique_ptr<FExpr>(std::make_unique<FIdentifierExpr>(QueryName))
			        : std::unique_ptr<FExpr>(std::make_unique<FStringLiteralExpr>(std::string{})));
			return std::make_unique<FCallExpr>(BuiltinName, std::move(Arguments));
		};
		const auto AddReflectionQueryMethod = [&MakeReflectionCall](
		                                          FStructDecl& Decl,
		                                          std::string ReturnType,
		                                          std::string Name,
		                                          const char* BuiltinName,
		                                          const Builtins::EReflectionQuery Query,
		                                          const char* OwnerName,
		                                          const char* QueryIndexName,
		                                          const bool bAddIndexParam,
		                                          const bool bHasDetail = false,
		                                          const char* QueryName = nullptr)
		{
			FMethodDecl Method;
			Method.ReturnType = std::move(ReturnType);
			Method.Name = std::move(Name);
			Method.bIsConst = true;
			if (bAddIndexParam)
			{
				FFunctionParam Param;
				Param.TypeName = "int";
				Param.Name = "index";
				Method.Parameters.push_back(std::move(Param));
			}
			if (bHasDetail)
			{
				FFunctionParam Param;
				Param.TypeName = "int";
				Param.Name = "detail";
				Method.Parameters.push_back(std::move(Param));
			}
			if (QueryName)
			{
				FFunctionParam Param;
				Param.TypeName = "string";
				Param.Name = QueryName;
				Method.Parameters.push_back(std::move(Param));
			}
			Method.Body = std::make_unique<FBlockStmt>();
			Method.Body->Statements.push_back(std::make_unique<FReturnStmt>(
			    MakeReflectionCall(
			        BuiltinName,
			        Query,
			        OwnerName,
			        QueryIndexName,
			        bHasDetail ? "detail" : nullptr,
			        QueryName)));
			Decl.Methods.push_back(std::move(Method));
		};
		const auto AddReflectionInfoConstructor = [](FStructDecl& Decl)
		{
			FConstructorDecl Ctor;
			{
				FFunctionParam Param;
				Param.TypeName = "string";
				Param.Name = "owner";
				Ctor.Parameters.push_back(std::move(Param));
			}
			{
				FFunctionParam Param;
				Param.TypeName = "int";
				Param.Name = "item_index";
				Ctor.Parameters.push_back(std::move(Param));
			}
			Ctor.Body = std::make_unique<FBlockStmt>();
			Ctor.Body->Statements.push_back(std::make_unique<FExprStmt>(
			    std::make_unique<FAssignExpr>(
			        std::make_unique<FIdentifierExpr>("owner_name"),
			        std::make_unique<FIdentifierExpr>("owner"))));
			Ctor.Body->Statements.push_back(std::make_unique<FExprStmt>(
			    std::make_unique<FAssignExpr>(
			        std::make_unique<FIdentifierExpr>("index"),
			        std::make_unique<FIdentifierExpr>("item_index"))));
			Ctor.Body->Statements.push_back(std::make_unique<FExprStmt>(
			    std::make_unique<FCallExpr>(
			        "__reflection_bind",
			        [&]()
			{
				std::vector<std::unique_ptr<FExpr>> Args;
				Args.push_back(std::make_unique<FThisExpr>());
				return Args;
			}())));
			Decl.Constructors.push_back(std::move(Ctor));
		};
		const auto AddIndexedReflectionInfoConstructor = [](FStructDecl& Decl)
		{
			FConstructorDecl Ctor;
			FFunctionParam Param;
			Param.TypeName = "int";
			Param.Name = "item_index";
			Ctor.Parameters.push_back(std::move(Param));
			Ctor.Body = std::make_unique<FBlockStmt>();
			Ctor.Body->Statements.push_back(std::make_unique<FExprStmt>(
			    std::make_unique<FAssignExpr>(
			        std::make_unique<FIdentifierExpr>("index"),
			        std::make_unique<FIdentifierExpr>("item_index"))));
			Decl.Constructors.push_back(std::move(Ctor));
		};
		const auto AddMetadataLookupMethods = [&AddReflectionQueryMethod](
		                                          FStructDecl& Decl,
		                                          const Builtins::EReflectionQuery HasMetadata,
		                                          const Builtins::EReflectionQuery MetadataByKey,
		                                          const Builtins::EReflectionQuery HasAnnotation,
		                                          const char* OwnerName,
		                                          const char* IndexName)
		{
			AddReflectionQueryMethod(
			    Decl, "bool", "has_metadata", "__reflection_bool", HasMetadata,
			    OwnerName, IndexName, false, false, "key");
			AddReflectionQueryMethod(
			    Decl, "string", "metadata", "__reflection_string", MetadataByKey,
			    OwnerName, IndexName, false, false, "key");
			AddReflectionQueryMethod(
			    Decl, "bool", "has_annotation", "__reflection_bool", HasAnnotation,
			    OwnerName, IndexName, false, false, "name");
		};
		if (!HasUserTypeNamed("module_info") && Program.UsedCompilerBuiltinTypes.count("module_info") != 0)
		{
			FStructDecl Decl;
			Decl.Name = "module_info";
			Decl.bExported = true;
			FFieldDecl Owner;
			Owner.TypeName = "string";
			Owner.Name = "owner_name";
			Owner.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Owner));
			FConstructorDecl Ctor;
			Ctor.Body = std::make_unique<FBlockStmt>();
			Decl.Constructors.push_back(std::move(Ctor));
			AddReflectionQueryMethod(Decl, "string", "path", "__reflection_string", Builtins::EReflectionQuery::ModulePath, "owner_name", nullptr, false);
			AddReflectionQueryMethod(Decl, "int", "function_count", "__reflection_int", Builtins::EReflectionQuery::ModuleFunctionCount, "owner_name", nullptr, false);
			AddReflectionQueryMethod(Decl, "int", "global_count", "__reflection_int", Builtins::EReflectionQuery::ModuleGlobalCount, "owner_name", nullptr, false);
			AddReflectionQueryMethod(Decl, "int", "type_count", "__reflection_int", Builtins::EReflectionQuery::ModuleTypeCount, "owner_name", nullptr, false);
			AddReflectionQueryMethod(Decl, "string", "type_name", "__reflection_string", Builtins::EReflectionQuery::ModuleTypeName, "owner_name", "index", true);
			const auto TemplateQuery = [&](const char* Result, const char* Name, const ETemplateReflectionQuery Query,
			                               const bool Indexed = true, const bool Detail = false)
			{
				AddReflectionQueryMethod(Decl, Result, Name, Result == std::string_view("int") ? "__reflection_int" : Result == std::string_view("bool") ? "__reflection_bool"
				                                                                                                                                         : "__reflection_string",
				    static_cast<Builtins::EReflectionQuery>(Query), "owner_name", Indexed ? "index" : nullptr, Indexed, Detail);
			};
			TemplateQuery("int", "template_declaration_count", ETemplateReflectionQuery::DeclarationCount, false);
			TemplateQuery("string", "template_declaration_id", ETemplateReflectionQuery::DeclarationId);
			TemplateQuery("string", "template_declaration_name", ETemplateReflectionQuery::DeclarationName);
			TemplateQuery("string", "template_declaration_kind", ETemplateReflectionQuery::DeclarationKind);
			TemplateQuery("string", "template_declaration_owner", ETemplateReflectionQuery::DeclarationOwner);
			TemplateQuery("int", "template_parameter_count", ETemplateReflectionQuery::ParameterCount);
			TemplateQuery("int", "template_own_parameter_count", ETemplateReflectionQuery::OwnParameterCount);
			TemplateQuery("int", "template_enclosing_parameter_count", ETemplateReflectionQuery::EnclosingParameterCount);
			TemplateQuery("string", "template_parameter_id", ETemplateReflectionQuery::ParameterId, true, true);
			TemplateQuery("string", "template_parameter_name", ETemplateReflectionQuery::ParameterName, true, true);
			TemplateQuery("string", "template_parameter_kind", ETemplateReflectionQuery::ParameterKind, true, true);
			TemplateQuery("string", "template_parameter_value_type", ETemplateReflectionQuery::ParameterValueType, true, true);
			TemplateQuery("string", "template_parameter_default", ETemplateReflectionQuery::ParameterDefault, true, true);
			TemplateQuery("bool", "template_parameter_is_enclosing", ETemplateReflectionQuery::ParameterIsEnclosing, true, true);
			BuiltinStructTemplates.push_back(std::move(Decl));
		}
		if (!HasUserTypeNamed("function_info") && Program.UsedCompilerBuiltinTypes.count("function_info") != 0)
		{
			FStructDecl Decl;
			Decl.Name = "function_info";
			Decl.bExported = true;
			FFieldDecl Owner;
			Owner.TypeName = "string";
			Owner.Name = "owner_name";
			Owner.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Owner));
			FFieldDecl Index;
			Index.TypeName = "int";
			Index.Name = "index";
			Index.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Index));
			AddIndexedReflectionInfoConstructor(Decl);
			AddReflectionQueryMethod(Decl, "string", "name", "__reflection_string", Builtins::EReflectionQuery::FunctionName, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "linkage_name", "__reflection_string", Builtins::EReflectionQuery::FunctionLinkageName, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "module_path", "__reflection_string", Builtins::EReflectionQuery::FunctionModulePath, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "return_type", "__reflection_string", Builtins::EReflectionQuery::FunctionReturnType, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "int", "parameter_count", "__reflection_int", Builtins::EReflectionQuery::FunctionParameterCount, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "parameter_type", "__reflection_string", Builtins::EReflectionQuery::FunctionParameterType, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "string", "parameter_name", "__reflection_string", Builtins::EReflectionQuery::FunctionParameterName, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "bool", "parameter_is_ref", "__reflection_bool", Builtins::EReflectionQuery::FunctionParameterIsRef, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "bool", "parameter_is_const", "__reflection_bool", Builtins::EReflectionQuery::FunctionParameterIsConst, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "bool", "is_deleted", "__reflection_bool", Builtins::EReflectionQuery::FunctionIsDeleted, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_exported", "__reflection_bool", Builtins::EReflectionQuery::FunctionIsExported, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_constexpr", "__reflection_bool", Builtins::EReflectionQuery::FunctionIsConstexpr, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "int", "metadata_count", "__reflection_int", Builtins::EReflectionQuery::FunctionMetadataCount, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "metadata_key", "__reflection_string", Builtins::EReflectionQuery::FunctionMetadataKey, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "string", "metadata_value", "__reflection_string", Builtins::EReflectionQuery::FunctionMetadataValue, "owner_name", "index", false, true);
			AddMetadataLookupMethods(Decl, Builtins::EReflectionQuery::FunctionHasMetadata, Builtins::EReflectionQuery::FunctionMetadataByKey, Builtins::EReflectionQuery::FunctionHasAnnotation, "owner_name", "index");
			BuiltinStructTemplates.push_back(std::move(Decl));
		}
		if (!HasUserTypeNamed("global_info") && Program.UsedCompilerBuiltinTypes.count("global_info") != 0)
		{
			FStructDecl Decl;
			Decl.Name = "global_info";
			Decl.bExported = true;
			FFieldDecl Owner;
			Owner.TypeName = "string";
			Owner.Name = "owner_name";
			Owner.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Owner));
			FFieldDecl Index;
			Index.TypeName = "int";
			Index.Name = "index";
			Index.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Index));
			AddIndexedReflectionInfoConstructor(Decl);
			AddReflectionQueryMethod(Decl, "string", "name", "__reflection_string", Builtins::EReflectionQuery::GlobalName, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "linkage_name", "__reflection_string", Builtins::EReflectionQuery::GlobalLinkageName, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "module_path", "__reflection_string", Builtins::EReflectionQuery::GlobalModulePath, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "declared_type", "__reflection_string", Builtins::EReflectionQuery::GlobalDeclaredType, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "runtime_type", "__reflection_string", Builtins::EReflectionQuery::GlobalRuntimeType, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_const", "__reflection_bool", Builtins::EReflectionQuery::GlobalIsConst, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_constexpr", "__reflection_bool", Builtins::EReflectionQuery::GlobalIsConstexpr, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_exported", "__reflection_bool", Builtins::EReflectionQuery::GlobalIsExported, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "int", "metadata_count", "__reflection_int", Builtins::EReflectionQuery::GlobalMetadataCount, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "metadata_key", "__reflection_string", Builtins::EReflectionQuery::GlobalMetadataKey, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "string", "metadata_value", "__reflection_string", Builtins::EReflectionQuery::GlobalMetadataValue, "owner_name", "index", false, true);
			AddMetadataLookupMethods(Decl, Builtins::EReflectionQuery::GlobalHasMetadata, Builtins::EReflectionQuery::GlobalMetadataByKey, Builtins::EReflectionQuery::GlobalHasAnnotation, "owner_name", "index");
			BuiltinStructTemplates.push_back(std::move(Decl));
		}
		if (!HasUserTypeNamed("enum_member_info"))
		{
			FStructDecl Decl;
			Decl.Name = "enum_member_info";
			Decl.bExported = true;
			Decl.TypeParams = {"T"};
			FFieldDecl Owner;
			Owner.TypeName = "string";
			Owner.Name = "owner_name";
			Owner.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Owner));
			FFieldDecl Index;
			Index.TypeName = "int";
			Index.Name = "index";
			Index.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Index));
			AddReflectionInfoConstructor(Decl);
			AddReflectionQueryMethod(Decl, "string", "name", "__reflection_string", Builtins::EReflectionQuery::EnumMemberName, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "int64", "value", "__reflection_int64", Builtins::EReflectionQuery::EnumMemberValue, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "int", "metadata_count", "__reflection_int", Builtins::EReflectionQuery::EnumMemberMetadataCount, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "metadata_key", "__reflection_string", Builtins::EReflectionQuery::EnumMemberMetadataKey, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "string", "metadata_value", "__reflection_string", Builtins::EReflectionQuery::EnumMemberMetadataValue, "owner_name", "index", false, true);
			AddMetadataLookupMethods(Decl, Builtins::EReflectionQuery::EnumMemberHasMetadata, Builtins::EReflectionQuery::EnumMemberMetadataByKey, Builtins::EReflectionQuery::EnumMemberHasAnnotation, "owner_name", "index");
			BuiltinStructTemplates.push_back(std::move(Decl));
		}
		if (!HasUserTypeNamed("field_info"))
		{
			FStructDecl Decl;
			Decl.Name = "field_info";
			Decl.bExported = true;
			Decl.TypeParams = {"T"};
			FFieldDecl Owner;
			Owner.TypeName = "string";
			Owner.Name = "owner_name";
			Owner.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Owner));
			FFieldDecl Index;
			Index.TypeName = "int";
			Index.Name = "index";
			Index.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Index));
			AddReflectionInfoConstructor(Decl);
			AddReflectionQueryMethod(Decl, "string", "name", "__reflection_string", Builtins::EReflectionQuery::FieldName, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "declared_type", "__reflection_string", Builtins::EReflectionQuery::FieldDeclaredType, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "runtime_type", "__reflection_string", Builtins::EReflectionQuery::FieldRuntimeType, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_static", "__reflection_bool", Builtins::EReflectionQuery::FieldIsStatic, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_mutable", "__reflection_bool", Builtins::EReflectionQuery::FieldIsMutable, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_constexpr", "__reflection_bool", Builtins::EReflectionQuery::FieldIsConstexpr, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_declared_on_type", "__reflection_bool", Builtins::EReflectionQuery::FieldIsDeclaredOnType, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "int", "metadata_count", "__reflection_int", Builtins::EReflectionQuery::FieldMetadataCount, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "metadata_key", "__reflection_string", Builtins::EReflectionQuery::FieldMetadataKey, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "string", "metadata_value", "__reflection_string", Builtins::EReflectionQuery::FieldMetadataValue, "owner_name", "index", false, true);
			AddMetadataLookupMethods(Decl, Builtins::EReflectionQuery::FieldHasMetadata, Builtins::EReflectionQuery::FieldMetadataByKey, Builtins::EReflectionQuery::FieldHasAnnotation, "owner_name", "index");
			BuiltinStructTemplates.push_back(std::move(Decl));
		}
		if (!HasUserTypeNamed("method_info"))
		{
			FStructDecl Decl;
			Decl.Name = "method_info";
			Decl.bExported = true;
			Decl.TypeParams = {"T"};
			FFieldDecl Owner;
			Owner.TypeName = "string";
			Owner.Name = "owner_name";
			Owner.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Owner));
			FFieldDecl Index;
			Index.TypeName = "int";
			Index.Name = "index";
			Index.Access = EMemberAccess::Private;
			Decl.Fields.push_back(std::move(Index));
			AddReflectionInfoConstructor(Decl);
			AddReflectionQueryMethod(Decl, "string", "name", "__reflection_string", Builtins::EReflectionQuery::MethodName, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "return_type", "__reflection_string", Builtins::EReflectionQuery::MethodReturnType, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "int", "parameter_count", "__reflection_int", Builtins::EReflectionQuery::MethodParameterCount, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "parameter_type", "__reflection_string", Builtins::EReflectionQuery::MethodParameterType, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "string", "parameter_name", "__reflection_string", Builtins::EReflectionQuery::MethodParameterName, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "bool", "parameter_is_ref", "__reflection_bool", Builtins::EReflectionQuery::MethodParameterIsRef, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "bool", "parameter_is_const", "__reflection_bool", Builtins::EReflectionQuery::MethodParameterIsConst, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "bool", "is_static", "__reflection_bool", Builtins::EReflectionQuery::MethodIsStatic, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_virtual", "__reflection_bool", Builtins::EReflectionQuery::MethodIsVirtual, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_abstract", "__reflection_bool", Builtins::EReflectionQuery::MethodIsAbstract, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_const", "__reflection_bool", Builtins::EReflectionQuery::MethodIsConst, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_constexpr", "__reflection_bool", Builtins::EReflectionQuery::MethodIsConstexpr, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_deleted", "__reflection_bool", Builtins::EReflectionQuery::MethodIsDeleted, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_defaulted", "__reflection_bool", Builtins::EReflectionQuery::MethodIsDefaulted, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_available", "__reflection_bool", Builtins::EReflectionQuery::MethodIsAvailable, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "special_member_kind", "__reflection_string", Builtins::EReflectionQuery::MethodSpecialMemberKind, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "unavailable_reason", "__reflection_string", Builtins::EReflectionQuery::MethodUnavailableReason, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_constructor", "__reflection_bool", Builtins::EReflectionQuery::MethodIsConstructor, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "bool", "is_destructor", "__reflection_bool", Builtins::EReflectionQuery::MethodIsDestructor, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "int", "metadata_count", "__reflection_int", Builtins::EReflectionQuery::MethodMetadataCount, "owner_name", "index", false);
			AddReflectionQueryMethod(Decl, "string", "metadata_key", "__reflection_string", Builtins::EReflectionQuery::MethodMetadataKey, "owner_name", "index", false, true);
			AddReflectionQueryMethod(Decl, "string", "metadata_value", "__reflection_string", Builtins::EReflectionQuery::MethodMetadataValue, "owner_name", "index", false, true);
			AddMetadataLookupMethods(Decl, Builtins::EReflectionQuery::MethodHasMetadata, Builtins::EReflectionQuery::MethodMetadataByKey, Builtins::EReflectionQuery::MethodHasAnnotation, "owner_name", "index");
			BuiltinStructTemplates.push_back(std::move(Decl));
		}
		if (!HasUserTypeNamed("type"))
		{
			// `type<T>` is a strongly typed runtime type token. Each specialization
			// receives its canonical T name during monomorphization below.
			FStructDecl TypeDecl;
			TypeDecl.Name = "type";
			TypeDecl.bExported = true;
			TypeDecl.TypeParams = {"T"};
			FFieldDecl NameField;
			NameField.TypeName = "string";
			NameField.Name = "name";
			TypeDecl.Fields.push_back(std::move(NameField));
			FFieldDecl QualifiedNameField;
			QualifiedNameField.TypeName = "string";
			QualifiedNameField.Name = "qualified_name";
			TypeDecl.Fields.push_back(std::move(QualifiedNameField));
			FConstructorDecl Ctor;
			Ctor.Body = std::make_unique<FBlockStmt>();
			TypeDecl.Constructors.push_back(std::move(Ctor));
			AddReflectionQueryMethod(TypeDecl, "string", "kind", "__reflection_string", Builtins::EReflectionQuery::TypeKind, "qualified_name", nullptr, false);
			AddReflectionQueryMethod(TypeDecl, "bool", "is_abstract", "__reflection_bool", Builtins::EReflectionQuery::TypeIsAbstract, "qualified_name", nullptr, false);
			AddReflectionQueryMethod(TypeDecl, "int", "base_count", "__reflection_int", Builtins::EReflectionQuery::TypeBaseCount, "qualified_name", nullptr, false);
			AddReflectionQueryMethod(TypeDecl, "string", "base_name", "__reflection_string", Builtins::EReflectionQuery::TypeBaseName, "qualified_name", "index", true);
			AddReflectionQueryMethod(TypeDecl, "int", "field_count", "__reflection_int", Builtins::EReflectionQuery::TypeFieldCount, "qualified_name", nullptr, false);
			AddReflectionQueryMethod(TypeDecl, "int", "method_count", "__reflection_int", Builtins::EReflectionQuery::TypeMethodCount, "qualified_name", nullptr, false);
			AddReflectionQueryMethod(TypeDecl, "int", "metadata_count", "__reflection_int", Builtins::EReflectionQuery::TypeMetadataCount, "qualified_name", nullptr, false);
			AddReflectionQueryMethod(TypeDecl, "string", "metadata_key", "__reflection_string", Builtins::EReflectionQuery::TypeMetadataKey, "qualified_name", "index", true);
			AddReflectionQueryMethod(TypeDecl, "string", "metadata_value", "__reflection_string", Builtins::EReflectionQuery::TypeMetadataValue, "qualified_name", "index", true);
			AddMetadataLookupMethods(TypeDecl, Builtins::EReflectionQuery::TypeHasMetadata, Builtins::EReflectionQuery::TypeMetadataByKey, Builtins::EReflectionQuery::TypeHasAnnotation, "qualified_name", nullptr);
			AddReflectionQueryMethod(TypeDecl, "int", "enum_member_count", "__reflection_int", Builtins::EReflectionQuery::TypeEnumMemberCount, "qualified_name", nullptr, false);
			AddReflectionQueryMethod(TypeDecl, "string", "enum_member_name", "__reflection_string", Builtins::EReflectionQuery::TypeEnumMemberName, "qualified_name", "index", true);
			AddReflectionQueryMethod(TypeDecl, "int64", "enum_member_value", "__reflection_int64", Builtins::EReflectionQuery::TypeEnumMemberValue, "qualified_name", "index", true);
			AddReflectionQueryMethod(TypeDecl, "string", "enum_underlying_type", "__reflection_string", Builtins::EReflectionQuery::TypeEnumUnderlyingType, "qualified_name", nullptr, false);
			AddReflectionQueryMethod(TypeDecl, "bool", "is_scoped_enum", "__reflection_bool", Builtins::EReflectionQuery::TypeEnumIsScoped, "qualified_name", nullptr, false);
			BuiltinStructTemplates.push_back(std::move(TypeDecl));
		}
		if (!HasUserTypeNamed("weak"))
		{
			// Weak references are VM-owned slots containing std::weak_ptr targets.
			// The wrapper itself is a script value type, so copies own independent slots.
			FStructDecl WeakDecl;
			WeakDecl.Name = "weak";
			WeakDecl.bExported = true;
			WeakDecl.TypeParams = {"T"};
			FFieldDecl SlotField;
			SlotField.TypeName = "int64";
			SlotField.Name = "slot";
			SlotField.Access = EMemberAccess::Private;
			SlotField.DefaultValue =
			    std::make_unique<FIntegerLiteralExpr>(std::int32_t{-1});
			WeakDecl.Fields.push_back(std::move(SlotField));
			{
				FConstructorDecl Ctor;
				Ctor.Body = std::make_unique<FBlockStmt>();
				WeakDecl.Constructors.push_back(std::move(Ctor));
			}
			{
				FConstructorDecl Ctor;
				FFunctionParam Param;
				Param.TypeName = "T";
				Param.Name = "target";
				Ctor.Parameters.push_back(std::move(Param));
				std::vector<std::unique_ptr<FExpr>> Args;
				Args.push_back(std::make_unique<FIdentifierExpr>("target"));
				auto Body = std::make_unique<FBlockStmt>();
				Body->Statements.push_back(std::make_unique<FExprStmt>(
				    std::make_unique<FAssignExpr>(
				        std::make_unique<FIdentifierExpr>("slot"),
				        std::make_unique<FCallExpr>(
				            "__weak_create", std::move(Args)))));
				Ctor.Body = std::move(Body);
				WeakDecl.Constructors.push_back(std::move(Ctor));
			}
			{
				FMethodDecl Method;
				Method.ReturnType = "T";
				Method.Name = "lock";
				Method.bIsConst = true;
				std::vector<std::unique_ptr<FExpr>> Args;
				Args.push_back(std::make_unique<FIdentifierExpr>("slot"));
				Method.Body = std::make_unique<FBlockStmt>();
				Method.Body->Statements.push_back(std::make_unique<FReturnStmt>(
				    std::make_unique<FCallExpr>("__weak_lock", std::move(Args))));
				WeakDecl.Methods.push_back(std::move(Method));
			}
			{
				FMethodDecl Method;
				Method.ReturnType = "bool";
				Method.Name = "expired";
				Method.bIsConst = true;
				std::vector<std::unique_ptr<FExpr>> Args;
				Args.push_back(std::make_unique<FIdentifierExpr>("slot"));
				Method.Body = std::make_unique<FBlockStmt>();
				Method.Body->Statements.push_back(std::make_unique<FReturnStmt>(
				    std::make_unique<FCallExpr>("__weak_expired", std::move(Args))));
				WeakDecl.Methods.push_back(std::move(Method));
			}
			{
				FMethodDecl Method;
				Method.ReturnType = "void";
				Method.Name = "reset";
				Method.Body = std::make_unique<FBlockStmt>();
				Method.Body->Statements.push_back(std::make_unique<FExprStmt>(
				    std::make_unique<FAssignExpr>(
				        std::make_unique<FIdentifierExpr>("slot"),
				        std::make_unique<FIntegerLiteralExpr>(
				            std::int32_t{-1}))));
				WeakDecl.Methods.push_back(std::move(Method));
			}
			BuiltinStructTemplates.push_back(std::move(WeakDecl));
		}
		return RegisterSourceMetadataTemplates(Program);
	}

}

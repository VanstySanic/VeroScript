// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptProgramPreparation.h"
#include "ConstantEvaluation/VeroScriptConstantRequest.h"
#include "ConstantEvaluation/VeroScriptConstantBranches.h"
#include "VeroScriptTemplateQuery.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAstClone.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptLexer.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptNamespaceLookup.h"
#include "VeroScriptParser.h"
#include "VeroScriptRuntime.h"
#include "Processing/VeroScriptSourceMetadataIO.h"
#include "Processing/VeroScriptProcessingSession.h"
#include "VeroScriptScriptCompileExtensions.h"
#include "Parser/VeroScriptParserDetail.h"
#include "TypeSupport/VeroScriptScopedTypeEnvironments.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace VeroScript
{
	struct FProgramPreparationAccess
	{
		FProgramPreparationAccess(const FProgram& P, const std::vector<FNativeFunction>* N,
		    const IHostBindingCatalog* B)
		    : ImportIndex(P.Imports)
		{
			Bindings = B;
			Compiler.ResetCompilationState(P, N, B, ImportIndex);
			Compiler.RegisterBuiltinTemplates(P);
			for (const auto& A : P.TypeAliases)
			{
				if (!Compiler.ConstantBranchState->Includes(A.Scope.ScopeId))
				{
					continue;
				}
				VeroScriptCompiler::FUserTypeAlias Info;
				Info.TargetTypeName = A.TypeName;
				Info.ModulePath = A.ModulePath;
				Info.EnclosingParameterNames = EnclosingTemplateParameterNames(A.Scope);
				Info.bExported = A.bExported;
				Compiler.UserTypeAliases.emplace(A.Name, std::move(Info));
			}
			for (const auto& E : P.Enums)
			{
				Compiler.EnumTypeNames.insert(E.Name);
				Compiler.EnumTypeNames.insert(Compiler.GetSymbolLinkageName(E.ModulePath, E.bExported, E.Name));
			}
		}

		std::string Linkage(const std::string& Name, const std::string& Module, bool Exported)
		{
			return Compiler.GetSymbolLinkageName(Module, Exported, Name);
		}
		void RefreshDeclarationIndexes()
		{
			Compiler.IndexSourceDeclarations(*Compiler.ActiveProgram);
		}
		void RefreshConditionalScopes()
		{
			Compiler.ConstantBranchState = std::make_shared<FConstantBranchState>(*Compiler.ActiveProgram);
		}
		bool IncludesScope(std::string_view Scope) const
		{
			return Compiler.ConstantBranchState->Includes(Scope);
		}

		std::string InheritanceKey(const std::string& T, const std::string& Module,
		    const std::string& Namespace)
		{
			Compiler.CurrentModulePath = Module;
			Compiler.CurrentNamespacePrefix = Namespace;
			auto Normalized = Compiler.NormalizeTypeAlias(T, Module);
			std::string Base;
			std::vector<std::string> Args;
			if (!Compiler.SplitUserGenericTypeApp(Normalized, Base, Args))
			{
				Base = Normalized;
			}

			return Compiler.ResolveTypeLinkageName(Base, Module, Args.size()) + '\x1F' +
			    std::to_string(Args.size());
		}

		std::string FunctionKey(const FFunctionDecl& F)
		{
			Compiler.CurrentModulePath = F.ModulePath;
			const auto Dot = F.Name.rfind('.');
			Compiler.CurrentNamespacePrefix = Dot == std::string::npos ? "" : F.Name.substr(0, Dot);
			return Compiler.MakeFreeFunctionLinkageName(F);
		}

		std::string Type(const std::string& T, const std::string& Module, const std::string& Namespace)
		{
			Compiler.CurrentModulePath = Module;
			Compiler.CurrentNamespacePrefix = Namespace;
			return Compiler.QualifyTypeName(Compiler.NormalizeTypeAlias(T, Module), Module);
		}

		std::string ConversionType(const std::string& T, const std::string& Module, const std::string& Namespace)
		{
			return Compiler.CanonicalTypeNameForCoercion(Type(T, Module, Namespace));
		}

		bool ValidateTemplateDeclarations(const FProgram& Program)
		{
			// Defaults are checked before the runtime module and interface tables
			// exist. Validate signatures without attempting runtime type layouts.
			struct FRestoreModule
			{
				~FRestoreModule()
				{
					Destination = std::move(Original);
				}
				std::unique_ptr<FBytecodeModule>& Destination;
				std::unique_ptr<FBytecodeModule> Original;
			} RestoreModule{Compiler.Module, std::move(Compiler.Module)};

			const auto Check = [&](const FTemplateParameterList& Parameters, const std::string& Module,
			                       std::unordered_map<std::string, std::string> Bindings)
			{
				bool Defaulted = false;
				Compiler.CurrentModulePath = Module;
				// Captured environments are already bound as a whole, independently
				// of the local declaration's own left-to-right parameter list.
				for (std::size_t Index = 0; Index < Parameters.GetEnclosingCount(); ++Index)
				{
					const auto& Parameter = Parameters.GetParameters()[Index];
					if (Parameter.Kind == ETemplateParameterKind::Type)
					{
						Bindings.emplace(Parameter.Name, "@type_parameter_" + Parameter.Id);
					}
				}
				for (std::size_t Index = 0; Index < Parameters.GetParameters().size(); ++Index)
				{
					CheckAnalysisCancellation();
					const auto& Parameter = Parameters.GetParameters()[Index];
					Compiler.CurrentNamespacePrefix = Parameter.Namespace;
					if (Index >= Parameters.GetEnclosingCount() && Bindings.count(Parameter.Name))
					{
						Compiler.ReportError("template parameter shadows an enclosing parameter", Parameter.Location);
						return false;
					}
					if (Index >= Parameters.GetEnclosingCount())
					{
						if (Defaulted && !Parameter.HasDefault())
						{
							Compiler.ReportError("a required template parameter cannot follow a defaulted parameter", Parameter.Location);
							return false;
						}
						Defaulted = Defaulted || Parameter.HasDefault();
					}
					const auto Dependent = [&](const std::string& Text)
					{
						return std::any_of(Bindings.begin(), Bindings.end(), [&](const auto& Binding)
						{
							return ParserDetail::TypeMentionsParam(Text, Binding.first);
						});
					};
					FTemplateValueType Type;
					const bool DependentType = Parameter.Kind == ETemplateParameterKind::Value && Dependent(Parameter.ValueTypeName);
					if (Parameter.Kind == ETemplateParameterKind::Value && !DependentType && !Compiler.ResolveTemplateValueType(Parameter.ValueTypeName, Module, Bindings, Type))
					{
						Compiler.ReportError("template value parameter requires an integral, bool, or enum type", Parameter.Location);
						return false;
					}
					if (Parameter.HasDefault())
					{
						if (Parameter.Kind == ETemplateParameterKind::Type)
						{
							if (!Dependent(Parameter.DefaultTypeName) && Compiler.NormalizeTypeAlias(Compiler.QualifyTypeName(Parameter.DefaultTypeName, Module)) != "void" && !Compiler.ValidateFieldTypeName(Parameter.DefaultTypeName, Parameter.Location))
							{
								if (!Compiler.HasError())
								{
									Compiler.ReportErrorId("diag.template.invalid_type_default", Parameter.Location,
									    {Parameter.Name, Parameter.DefaultTypeName});
								}
								return false;
							}
						}
						else
						{
							auto Result = Compiler.EvaluateTemplateSource(Parameter.DefaultSource, Module,
							    Bindings, Parameter.Location, &Parameter.Namespace, false);
							if (Result.Succeeded() && !DependentType)
							{
								Result = ConvertTemplateConstant(Result.Value, Type, false, Parameter.Location);
							}
							if (!Result.Succeeded() && Result.Status != ETemplateEvaluationStatus::Dependent)
							{
								Compiler.ReportError("invalid template default for '" + Parameter.Name + "': " + Result.Reason, Parameter.Location);
								return false;
							}
						}
					}
					Bindings.emplace(Parameter.Name, Parameter.Kind == ETemplateParameterKind::Type ? "@type_parameter_" + std::to_string(Index) : "@dependent-value:" + Parameter.ValueTypeName);
				}
				return true;
			};
			const auto Types = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					if (!Compiler.ConstantBranchState->Includes(Declaration.Scope.ScopeId))
					{
						continue;
					}
					if (!Check(Declaration.TemplateParameters, Declaration.ModulePath, {})) return false;
					std::unordered_map<std::string, std::string> Owner;
					for (const auto& Parameter : Declaration.TemplateParameters.GetParameters())
					{
						Owner.emplace(Parameter.Name, Parameter.Kind == ETemplateParameterKind::Type ? "@type_parameter_" + Parameter.Id : "@dependent-value:" + Parameter.ValueTypeName);
					}
					for (const auto& Method : Declaration.Methods)
					{
						if (!Check(Method.TemplateParameters, Declaration.ModulePath, Owner)) return false;
						if (!TemplateParameterNames(Method).empty())
						{
							auto Names = Declaration.TypeParams;
							Names.insert(Names.end(), Method.TypeParams.begin(), Method.TypeParams.end());
							auto Records = Declaration.TemplateParameters.GetParameters();
							const auto& Own = Method.TemplateParameters.GetParameters();
							Records.insert(Records.end(), Own.begin(), Own.end());
							FTemplateParameterList Schema;
							Schema.Assign(std::move(Records));
							if (!Compiler.ValidateTypeParamBoundDeclarations(Names, Method.Bounds,
							        Declaration.ModulePath, Method.Location, "method", &Schema))
							{
								return false;
							}
						}
					}
				}
				return true;
			};
			if (!Types(Program.Classes) || !Types(Program.Structs) || !Types(Program.Interfaces)) return false;
			for (const auto& Function : Program.Functions)
			{
				if (!Check(Function.TemplateParameters, Function.ModulePath, {})) return false;
			}
			return !Compiler.HasError();
		}

		void SetQueryContext(const std::string& Module, const std::string& Namespace)
		{
			Compiler.CurrentModulePath = Module;
			Compiler.CurrentNamespacePrefix = Namespace;
			Compiler.Diagnostics.clear();
			Compiler.bHasError = false;
			Compiler.Error = {};
		}

		bool QueryExpressionType(const FExpr& Expression, std::string& Type)
		{
			return Compiler.TryResolveExprTypeName(Expression, Type);
		}

		std::string QueryTypeSpelling(const std::string& Type, unsigned Depth = 0)
		{
			CheckAnalysisCancellation();
			if (Depth >= 64)
			{
				return {};
			}
			for (const auto Prefix : {"const ", "ref ", "lvalue "})
			{
				if (Type.rfind(Prefix, 0) == 0)
				{
					return std::string(Prefix) +
					    QueryTypeSpelling(Type.substr(std::char_traits<char>::length(Prefix)), Depth + 1);
				}
			}
			// Function components may each contain a private module linkage. Strip
			// those inside the parameter/result types, preserving the fn structure.
			std::vector<std::string> Parameters;
			std::string ReturnType;
			if (Runtime::ParseFunctionType(Type, Parameters, ReturnType))
			{
				for (auto& Parameter : Parameters)
				{
					Parameter = QueryTypeSpelling(Parameter, Depth + 1);
					if (Parameter.empty()) return {};
				}
				ReturnType = QueryTypeSpelling(ReturnType, Depth + 1);
				return ReturnType.empty() ? std::string{} : Runtime::MakeFunctionTypeName(Parameters, ReturnType);
			}
			// Encoded values preserve enum identity and exact signed/unsigned width.
			if (Type.rfind("@value:", 0) == 0)
			{
				return Type;
			}
			std::string Head;
			std::vector<std::string> Args;
			// Builtin containers can contain private module linkages too. Split
			// their arguments before stripping a linkage from the outer head.
			const bool bContainer = Runtime::IsDynamicArrayTypeName(Type) || Runtime::IsCommonContainerTypeName(Type);
			const auto Open = Type.find('<');
			if (bContainer && Open != std::string::npos && Type.back() == '>')
			{
				Head = Type.substr(0, Open);
				Args = Compiler.SplitTopLevelDelimited(Type.substr(Open + 1, Type.size() - Open - 2), ',');
			}
			else if (!Compiler.SplitUserGenericTypeApp(Type, Head, Args))
			{
				Head = Compiler.GenericTemplateBaseLinkage(Type);
				if (!Head.empty())
				{
					Args = Compiler.DecodeGenericConcreteTypes(Type);
				}
				else
				{
					Head = Type;
				}
			}
			const auto Unit = Head.find('\x1f');
			if (Unit != std::string::npos)
			{
				Head.erase(0, Unit + 1);
			}
			if (Args.empty())
			{
				return Head;
			}
			Head += '<';
			for (std::size_t Index = 0; Index < Args.size(); ++Index)
			{
				if (Index)
				{
					Head += ',';
				}
				Head += QueryTypeSpelling(Args[Index], Depth + 1);
			}
			return Head + '>';
		}

		std::string QueryReturnType(const std::string& Owner, const std::string& Name,
		    const std::vector<std::string>& Arguments, const std::vector<std::string>* Explicit,
		    bool ReceiverIsConst, bool StaticOnly)
		{
			CheckAnalysisCancellation();
			if (Name.empty() || Arguments.size() > 65536)
			{
				return {};
			}
			std::vector<std::string> Types;
			for (const auto& Argument : Arguments)
			{
				Types.push_back(Argument.empty() ? Argument
				                                 : Compiler.QualifyTypeName(Argument, Compiler.CurrentModulePath));
			}
			if (!Owner.empty())
			{
				const FMethodDecl* Method = nullptr;
				std::string DeclOwner;
				if (!Compiler.ResolveMethodForCall(Compiler.QualifyTypeName(Owner, Compiler.CurrentModulePath), Name,
				        Types, DeclOwner, Method, {}, false, false, nullptr, ReceiverIsConst,
				        Explicit) ||
				    !Method || Method->bDeleted || (StaticOnly && !Method->bIsStatic))
				{
					return {};
				}
				std::unordered_map<std::string, std::string> TypeBindings;
				Compiler.TryBuildGenericOwnerSubst(DeclOwner, TypeBindings, {});
				const auto Module = Compiler.ResolveOwnerModulePath(DeclOwner);
				return QueryTypeSpelling(Compiler.QualifyTypeName(
				    Compiler.SubstituteTypeNameWith(Method->ReturnType, TypeBindings), Module));
			}
			const FFunctionDecl* Function = nullptr;
			Compiler.ResolveGenericFreeFunctionForCall(Name, Explicit ? *Explicit : std::vector<std::string>{}, Types,
			    Function, {}, Explicit != nullptr);
			if (!Function && !Explicit && !Compiler.HasError())
			{
				Compiler.ResolveFunctionForCall(Name, Types, Function, {});
			}
			if (!Function || Function->bDeleted)
			{
				return {};
			}
			std::vector<std::string> Concrete;
			if (!TemplateParameterNames(*Function).empty() &&
			    !Compiler.TryInferGenericFunctionTypeParams(
			        *Function, Types, Explicit ? *Explicit : std::vector<std::string>{}, Concrete))
			{
				return {};
			}
			std::unordered_map<std::string, std::string> TypeBindings;
			const auto& Names = TemplateParameterNames(*Function);
			for (std::size_t Index = 0; Index < Names.size(); ++Index)
			{
				TypeBindings.emplace(Names[Index], Concrete[Index]);
			}
			return QueryTypeSpelling(Compiler.QualifyTypeName(
			    Compiler.SubstituteTypeNameWith(Function->ReturnType, TypeBindings), Function->ModulePath));
		}

		const ModuleVisibility::FImportIndex ImportIndex;
		VeroScriptCompiler Compiler;

		const IHostBindingCatalog* Bindings = nullptr;
	};

	struct FTemplateCallTypeQuery::FState
	{
		FState(const FProgram& P, const IHostBindingCatalog* B, const std::string& Module, const std::string& Namespace)
		    : Access(P, nullptr, B), Module(Module), Namespace(Namespace)
		{
		}

		void ResetContext()
		{
			Access.SetQueryContext(Module, Namespace);
		}

		FProgramPreparationAccess Access;
		std::string Module, Namespace;
	};

	FTemplateCallTypeQuery::FTemplateCallTypeQuery(const FProgram& P, const IHostBindingCatalog* B,
	    const std::string& Module, const std::string& Namespace)
	    : State(std::make_unique<FState>(P, B, Module, Namespace))
	{
	}

	FTemplateCallTypeQuery::~FTemplateCallTypeQuery() = default;

	std::string FTemplateCallTypeQuery::ResolveReturnType(const std::string& Owner, const std::string& Name,
	    const std::vector<std::string>& Arguments,
	    const std::vector<std::string>* Explicit,
	    bool ReceiverIsConst, bool StaticOnly)
	{
		State->ResetContext();
		return State->Access.QueryReturnType(Owner, Name, Arguments, Explicit, ReceiverIsConst, StaticOnly);
	}

	std::string FTemplateCallTypeQuery::ResolveExpressionType(const FExpr& Expression)
	{
		State->ResetContext();
		std::string Type;
		return State->Access.QueryExpressionType(Expression, Type) ? State->Access.QueryTypeSpelling(Type)
		                                                           : std::string{};
	}

	namespace
	{
		std::string NamespaceOf(const std::string& Name)
		{
			const auto Dot = Name.rfind('.');
			return Dot == std::string::npos ? "" : Name.substr(0, Dot);
		}
		FSourceLocation Location(const FAnnotationSourcePosition& P)
		{
			return {P.FileLabel, P.Line, P.Column};
		}
		void Error(std::vector<FScriptError>& D, std::string Message, FSourceLocation L = {})
		{
			FScriptError E;
			E.Message = std::move(Message);
			E.Location = L;
			D.push_back(std::move(E));
		}
		bool HasErrors(const std::vector<FScriptError>& D)
		{
			return std::any_of(D.begin(), D.end(),
			    [](const auto& E)
			{
				return E.Severity == ECompileDiagnosticSeverity::Error;
			});
		}
		void Check(const FAnnotationProcessingOptions& O)
		{
			CheckAnalysisCancellation();
			if (O.CheckCancelled)
			{
				O.CheckCancelled();
			}
		}
		// Length framing makes fingerprints independent of string delimiters.
		void Append(std::string& S, std::string_view V)
		{
			S += std::to_string(V.size()) + ":";
			S.append(V);
		}
		template <class T>
		void AppendNumber(std::string& S, T V)
		{
			Append(S, std::to_string(static_cast<std::uint64_t>(V)));
		}
		std::string HostIdentity(const std::vector<FNativeFunction>* N, const IHostBindingCatalog* B)
		{
			std::string S;
			if (B)
			{
				N = &B->GetNativeFunctions();
			}

			AppendNumber(S, N ? N->size() : 0);
			if (N)
			{
				for (const auto& F : *N)
				{
					Append(S, F.Name);
					AppendNumber(S, F.ReturnType);
					Append(S, F.HostTypeName);
					Append(S, F.ReturnHostTypeName);
					Append(S, F.ReturnElementTypeName);
					Append(S, F.ReturnKeyTypeName);
					Append(S, F.ReturnValueTypeName);
					AppendNumber(S, F.ParamTypes.size());
					for (auto T : F.ParamTypes)
					{
						AppendNumber(S, T);
					}

					for (const auto* V :
					    {&F.ParamHostTypeNames, &F.ParamElementTypeNames, &F.ParamKeyTypeNames,
					        &F.ParamValueTypeNames, &F.TypeParams, &F.ReturnGenericTypeArguments})
					{
						AppendNumber(S, V->size());
						for (const auto& T : *V)
						{
							Append(S, T);
						}
					}
					AppendNumber(S, F.ParamFlags.size());
					for (auto Flag : F.ParamFlags)
					{
						AppendNumber(S, Flag);
					}

					Append(S, F.ScriptReturnTypeName);
					AppendNumber(S, F.bHostStaticMethod);
					AppendNumber(S, F.bGenericHostObjectCast);
					AppendNumber(S, F.ReturnGenericTemplateId);
					AppendNumber(S, F.bHasStructuredTypeDescs);
					AppendNumber(S, reinterpret_cast<std::uintptr_t>(F.Callback));
					AppendNumber(S, reinterpret_cast<std::uintptr_t>(F.UserData));
					Append(S, F.RuntimeReturnTypeName);
					AppendNumber(S, F.RuntimeParamTypeNames.size());
					for (const auto& T : F.RuntimeParamTypeNames)
					{
						Append(S, T);
					}

					std::function<void(const FNativeTypeDesc&, unsigned)> TypeIdentity =
					    [&](const FNativeTypeDesc& T, unsigned Depth)
					{
						if (Depth >= 64)
						{
							throw std::invalid_argument("native type descriptor nesting limit exceeded");
						}

						AppendNumber(S, T.Type);
						Append(S, T.HostTypeName);
						Append(S, T.ElementTypeName);
						Append(S, T.KeyTypeName);
						Append(S, T.ValueTypeName);
						AppendNumber(S, T.TypeArguments.size());
						for (const auto& A : T.TypeArguments)
						{
							TypeIdentity(A, Depth + 1);
						}
					};
					TypeIdentity(F.StructuredReturnType, 0);
					AppendNumber(S, F.StructuredParamTypes.size());
					for (const auto& T : F.StructuredParamTypes)
					{
						TypeIdentity(T, 0);
					}
				}
			}

			if (B)
			{
				// Catalog objects are borrowed and immutable for a preparation task.
				// Retain identity as well as content so a prepared result cannot silently
				// switch to another catalog with different dispatch ownership.
				AppendNumber(S, reinterpret_cast<std::uintptr_t>(B));
				std::string Catalog;
				if (!ExportBindingCatalogJson(*B, Catalog))
				{
					throw std::invalid_argument("cannot fingerprint host catalog");
				}

				Append(S, Catalog);
				for (const auto& T : B->GetGenericTypeTemplates())
				{
					AppendNumber(S, T.Id.Value);
					Append(S, T.Name);
					AppendNumber(S, T.Arity);
					AppendNumber(S, reinterpret_cast<std::uintptr_t>(T.Instantiate));
					AppendNumber(S, reinterpret_cast<std::uintptr_t>(T.UserData));
				}
				for (const auto& T : B->GetTypes())
				{
					Append(S, T.Name);
					AppendNumber(S, T.Kind);
					AppendNumber(S, T.bInheritable);
					for (const auto& M : T.Members)
					{
						Append(S, M.Name);
						AppendNumber(S, M.Kind);
						AppendNumber(S, M.ReturnType);
						for (auto P : M.ParamTypes)
						{
							AppendNumber(S, P);
						}
					}
				}
				for (const auto& A : B->GetTypeAliases())
				{
					Append(S, A.Name);
					Append(S, A.TargetTypeName);
				}
			}
			return S;
		}
		struct FOwner
		{
			std::string Id, Name, Module;
			const std::vector<std::string>* TypeParams;
			const FTemplateParameterList* TemplateParameters;
			std::vector<FMethodDecl>* Methods;
			bool Exported, Package, Static;
			const FTypeDeclarationScope* Scope;
			std::shared_ptr<const FDeferredSyntax> Context;
		};
		std::vector<FOwner> Owners(FProgram& P)
		{
			std::vector<FOwner> R;
			const auto Add = [&](auto& Decls)
			{
				for (auto& D : Decls)
				{
					bool Static = false;
					if constexpr (!std::is_same_v<std::decay_t<decltype(D)>, FInterfaceDecl>)
					{
						Static = D.bIsStatic;
					}

					R.push_back(
					    {D.ModulePath + '\x1F' + D.Name + '\x1F' + std::to_string(TemplateParameterNames(D).size()),
					        D.Name, D.ModulePath, &TemplateParameterNames(D), &D.TemplateParameters, &D.Methods, D.bExported,
					        D.bPrecompiledPackage, Static, &D.Scope, D.DeclarationContext});
				}
			};
			Add(P.Structs);
			Add(P.Classes);
			Add(P.Interfaces);
			return R;
		}
		std::string Signature(FProgramPreparationAccess& A, const std::string& Name,
		    const std::vector<FFunctionParam>& Params, bool Const,
		    const std::string& Module, const std::string& Ns)
		{
			std::string S;
			Append(S, Name);
			AppendNumber(S, Const);
			for (const auto& P : Params)
			{
				Append(S, A.Type(P.TypeName, Module, Ns));
				AppendNumber(S, P.bIsRef);
				AppendNumber(S, P.bIsConst);
				AppendNumber(S, P.bIsVariadic);
			}
			return S;
		}
		template <typename TDeclaration>
		std::string TemplatePattern(const TDeclaration& Declaration, const std::string& Type)
		{
			std::vector<std::string> Symbols;
			for (std::size_t Index = 0; Index < TemplateParameterNames(Declaration).size(); ++Index)
			{
				Symbols.push_back("__template_parameter_" + std::to_string(Index));
			}
			return SubstituteScopedTypeParameters(Type, TemplateParameterNames(Declaration), Symbols);
		}
		template <typename TDeclaration>
		std::string TemplateSignature(FProgramPreparationAccess& Access, const TDeclaration& Declaration,
		    const std::string& Name, const bool Const, const std::string& Module, const std::string& Namespace)
		{
			std::string Result;
			Append(Result, Name);
			AppendNumber(Result, Const);
			AppendNumber(Result, TemplateParameterNames(Declaration).size());
			for (const auto& Parameter : Declaration.TemplateParameters.GetParameters())
			{
				AppendNumber(Result, Parameter.Kind);
				Append(Result, TemplatePattern(Declaration, Parameter.ValueTypeName));
			}
			for (const auto& Parameter : Declaration.Parameters)
			{
				Append(Result, Access.Type(TemplatePattern(Declaration, Parameter.TypeName), Module, Namespace));
				AppendNumber(Result, Parameter.bIsRef);
				AppendNumber(Result, Parameter.bIsConst);
				AppendNumber(Result, Parameter.bIsVariadic);
			}
			return Result;
		}

		std::string MethodId(FProgramPreparationAccess& A, const FOwner& O, const FMethodDecl& M)
		{
			return O.Id + '\x1E' +
			    TemplateSignature(A, M, M.Name, M.bIsConst, O.Module, NamespaceOf(O.Name));
		}
		void SetBodyContext(FBlockStmt& B, const FSourceDefinition& D,
		    const std::vector<FFunctionParam>& Params, const std::string& Namespace)
		{
			B.DefinitionModulePath = D.ModulePath;
			B.DefinitionNamespace = Namespace;
			for (const auto& P : Params)
			{
				B.DefinitionParameterNames.push_back(P.Name);
			}
		}
		template <typename TDeclaration>
		bool SameBounds(FProgramPreparationAccess& A, const TDeclaration& Declaration,
		    const TDeclaration& Definition, const std::string& DeclarationModule = {},
		    const std::string& DefinitionModule = {})
		{
			// Binding transfers only the body; the declaration retains its bounds.
			// Omitted `where` therefore inherits the contract, while an explicit
			// definition clause must still be equivalent in its own module.
			if (Definition.Bounds.empty())
			{
				return true;
			}

			const auto Canonical = [&](const TDeclaration& F, const std::string& ContextModule)
			{
				std::string Module = ContextModule;
				if constexpr (std::is_same_v<TDeclaration, FFunctionDecl>)
				{
					Module = F.ModulePath;
				}
				std::multiset<std::pair<std::string, std::string>> Bounds;
				for (const auto& B : F.Bounds)
				{
					Bounds.emplace(
					    TemplatePattern(F, B.TypeParam), A.Type(TemplatePattern(F, B.InterfaceTypeName), Module, NamespaceOf(F.Name)));
				}

				return Bounds;
			};
			return Canonical(Declaration, DeclarationModule) == Canonical(Definition, DefinitionModule);
		}
		bool BindSource(FProgram& P, FProgramPreparationAccess& A, std::vector<FOwner>& Os,
		    std::vector<FScriptError>& Diagnostics, const FAnnotationProcessingOptions& Options)
		{
			std::vector<std::unique_ptr<FProgram>> Fragments;
			const FNamespaceLookup Names(P);
			for (auto& D : P.SourceDefinitions)
			{
				Check(Options);
				if (D.bBound)
				{
					continue;
				}

				const auto& T = D.Tokens;
				const auto Separator = ParserDetail::FindQualifiedDefinitionSeparator(T, 0);
				if (Separator >= T.size())
				{
					Error(Diagnostics, "invalid qualified definition", T.front().Location);
					return false;
				}
				const auto Sep = Separator;
				std::size_t Start = Sep;
				std::vector<std::string> Written;
				std::vector<std::pair<std::string, std::vector<std::string>>> Segments;
				while (Start > 0)
				{
					std::vector<std::string> Parameters;
					auto Segment = Start;
					if (T[Segment - 1].Lexeme == ">")
					{
						const auto End = --Segment;
						while (Segment > 0 && T[Segment].Lexeme != "<")
						{
							--Segment;
						}
						if (T[Segment].Lexeme != "<")
						{
							break;
						}
						bool ExpectName = true;
						for (auto I = Segment + 1; I < End; ++I)
						{
							if (ExpectName ? T[I].Kind != ETokenKind::Identifier : T[I].Lexeme != ",")
							{
								Error(Diagnostics, "source definition owner requires generic parameter names", T[I].Location);
								return false;
							}
							if (ExpectName)
							{
								Parameters.emplace_back(T[I].Lexeme);
							}
							ExpectName = !ExpectName;
						}
						if (ExpectName && !Parameters.empty())
						{
							Error(Diagnostics, "invalid source definition owner parameters", T[Segment].Location);
							return false;
						}
					}
					if (!Segment || T[Segment - 1].Kind != ETokenKind::Identifier)
					{
						break;
					}
					Start = Segment - 1;
					Segments.insert(Segments.begin(), {std::string(T[Start].Lexeme), std::move(Parameters)});
					if (Start < 2 || T[Start - 1].Lexeme != "::" || (T[Start - 2].Kind != ETokenKind::Identifier && T[Start - 2].Lexeme != ">"))
					{
						break;
					}
					--Start;
				}
				const bool Global = Start > 0 && T[Start - 1].Lexeme == "::";
				std::string Prefix;
				for (const auto& Segment : Segments)
				{
					if (!Prefix.empty())
					{
						Prefix += '.';
					}
					Prefix += Segment.first;
					Written.insert(Written.end(), Segment.second.begin(), Segment.second.end());
				}
				if (Global)
				{
					--Start;
				}

				const auto Targets = Prefix.empty() ? std::vector<std::string>{""}
				    : BindNamespacePath(Global ? "::" + Prefix : Prefix, D.Namespace,
				    [&](const std::string& Scope)
				{
					if (Names.Contains(P, Scope, D.ModulePath, &A.ImportIndex)) return true;
					return std::any_of(Os.begin(), Os.end(), [&](const FOwner& Owner)
					{
						return (Owner.Name == Scope || Owner.Scope->DisplayName == Scope) &&
						    ModuleVisibility::CanAccessSymbol(P, Owner.Module, Owner.Exported, D.ModulePath, &A.ImportIndex);
					});
				});
				if (Targets.size() != 1)
				{
					Error(Diagnostics, "qualified definition owner not found", T.front().Location);
					return false;
				}
				Prefix = Targets.front();

				std::vector<FToken> Fragment(T.begin(), T.begin() + Start);
				Fragment.insert(Fragment.end(), T.begin() + Sep + 1, T.end());
				std::vector<FOwner*> Candidates;
				for (auto& O : Os)
				{
					if ((O.Name == Prefix || O.Scope->DisplayName == Prefix) &&
					    O.TypeParams->size() == Written.size() &&
					    ModuleVisibility::CanAccessSymbol(P, O.Module, O.Exported, D.ModulePath, &A.ImportIndex))
					{
						Candidates.push_back(&O);
					}
				}

				if (Candidates.size() > 1)
				{
					Error(Diagnostics, "ambiguous qualified definition owner", T.front().Location);
					return false;
				}
				const auto Component = [&](const std::string& Module)
				{
					for (const auto& Source : P.PackageSourceModules)
					{
						if (Source.ModulePath == Module)
						{
							return Source.OwnerComponentId;
						}
					}

					return std::string{};
				};
				if (!Candidates.empty())
				{
					auto& O = *Candidates.front();
					if (Component(O.Module) != Component(D.ModulePath))
					{
						Error(Diagnostics, "source definition cannot cross package component ownership",
						    T.front().Location);
						return false;
					}
					if (O.Package && !Component(O.Module).empty())
					{
						D.bBound = true;
						continue;
					}
					if (O.Package || Written.size() != O.TypeParams->size())
					{
						Error(Diagnostics, "invalid owner or generic parameters for source definition",
						    T.front().Location);
						return false;
					}
					std::vector<std::string> OwnerPrefixNames;
					std::size_t HeaderIndex = 0;
					for (const auto& Segment : Segments)
					{
						if (Segment.second.empty())
						{
							continue;
						}
						if (!D.TemplateHeaders.empty())
						{
							if (HeaderIndex >= D.TemplateHeaders.size() || D.TemplateHeaders[HeaderIndex].GetNames() != Segment.second)
							{
								Error(Diagnostics, "template header does not match its owner segment", T.front().Location);
								return false;
							}
							const auto& Header = D.TemplateHeaders[HeaderIndex++];
							for (const auto& Parameter : Header.GetParameters())
							{
								const auto Index = OwnerPrefixNames.size();
								if (Parameter.HasDefault() || Index >= O.TemplateParameters->GetParameters().size() || Parameter.Kind != O.TemplateParameters->GetParameters()[Index].Kind)
								{
									Error(Diagnostics, "owner template parameter kind or default mismatch", Parameter.Location);
									return false;
								}
								if (Parameter.Kind == ETemplateParameterKind::Value && A.Type(SubstituteScopedTypeParameters(Parameter.ValueTypeName, Written, *O.TypeParams), D.ModulePath, O.Name) != A.Type(O.TemplateParameters->GetParameters()[Index].ValueTypeName, O.Module, O.Name))
								{
									Error(Diagnostics, "owner template value type mismatch", Parameter.Location);
									return false;
								}
								OwnerPrefixNames.push_back(Parameter.Name);
							}
						}
						else
						{
							OwnerPrefixNames.insert(OwnerPrefixNames.end(), Segment.second.begin(), Segment.second.end());
						}
					}
					if (D.TemplateHeaders.empty() && OwnerPrefixNames != *O.TypeParams)
					{
						Error(Diagnostics, "renamed owner parameters require template headers", T.front().Location);
						return false;
					}
					FTemplateParameterList OwnHeader;
					if (HeaderIndex < D.TemplateHeaders.size())
					{
						OwnHeader = D.TemplateHeaders[HeaderIndex++];
					}
					if (HeaderIndex != D.TemplateHeaders.size())
					{
						Error(Diagnostics, "too many template headers for qualified definition", T.front().Location);
						return false;
					}
					if (!OwnHeader.IsEmpty())
					{
						auto Parameters = OwnHeader.GetParameters();
						for (auto& Parameter : Parameters)
						{
							if (Parameter.HasDefault())
							{
								Error(Diagnostics, "source method definition cannot repeat template defaults", Parameter.Location);
								return false;
							}
							Parameter.ValueTypeName = SubstituteScopedTypeParameters(Parameter.ValueTypeName, Written, *O.TypeParams);
						}
						OwnHeader.Assign(std::move(Parameters));
					}
					for (auto& Token : Fragment)
					{
						for (std::size_t Index = 0; Index < Written.size(); ++Index)
						{
							if (Token.Kind == ETokenKind::Identifier && Token.Lexeme == Written[Index])
							{
								Token.Lexeme = (*O.TypeParams)[Index];
								break;
							}
						}
					}

					std::string Path = O.Scope->DisplayName.substr(0, O.Scope->DisplayName.size() - Prefix.size());
					std::size_t ParentArity = 0;
					for (const auto& Segment : Segments)
					{
						Path += Segment.first;
						ParentArity += Segment.second.size();
						const auto Parent = std::find_if(Os.begin(), Os.end(), [&](const auto& Candidate)
						{
							return (Candidate.Name == Path || Candidate.Scope->DisplayName == Path) && Candidate.Module == O.Module && Candidate.TypeParams->size() == ParentArity;
						});
						if ((Parent == Os.end() ? std::size_t{0} : OwnTemplateParameterCount(*Parent->Scope)) != Segment.second.size())
						{
							Error(Diagnostics, "generic arguments must match each source definition owner segment", T.front().Location);
							return false;
						}
						Path += '.';
					}
					VeroScriptParser Parser(Fragment, D.ModulePath, &P);
					std::unordered_set<std::string> HostNames;
					if (A.Bindings)
					{
						for (const auto& HostType : A.Bindings->GetTypes())
						{
							HostNames.insert(HostType.Name);
						}

						for (const auto& HostType : A.Bindings->GetTypeAliases())
						{
							HostNames.insert(HostType.Name);
						}

						for (const auto& HostType : A.Bindings->GetGenericTypeTemplates())
						{
							HostNames.insert(HostType.Name);
						}
					}
					Parser.SetHostTypeNames(std::move(HostNames));
					FDeferredSyntax Context = O.Context ? *O.Context : FDeferredSyntax{};
					Context.TypeOwner = O.Name;
					Context.TypeScope = std::make_shared<FTypeDeclarationScope>(*O.Scope);
					Context.ScopeId = O.Scope->Id;
					Context.ModulePath = D.ModulePath;
					Context.Namespace = O.Name;
					Context.TypeParameters.clear();
					Context.ValueParameters.clear();
					for (const auto& Parameter : O.TemplateParameters->GetParameters())
					{
						if (Parameter.Kind == ETemplateParameterKind::Type)
						{
							Context.TypeParameters.push_back(Parameter.Name);
						}
						else
						{
							Context.ValueParameters.emplace(Parameter.Name, Parameter);
						}
					}
					Context.FunctionOwner.clear();
					Parser.RestoreDeclarationContext(Context, false);
					if (P.SourceMetadata)
					{
						if (const auto* Document = P.SourceMetadata->FindDocument(std::string(T.front().Location.FileLabel)))
						{
							Parser.SetSourceText(Document->Text);
						}
					}
					FMethodDecl Definition;
					if (!Parser.ParseMethodFragment(O.Name, Context.TypeParameters, EMemberAccess::Public,
					        Definition, std::move(OwnHeader)))
					{
						Diagnostics.push_back(Parser.GetError());
						return false;
					}
					const auto DefinitionName = [&](const FMethodDecl& M, const std::string& Module,
					                                const std::string& Namespace)
					{
						return M.Name.rfind("operator conv ", 0) == 0
						    ? "operator conv " + A.ConversionType(M.ReturnType, Module, Namespace)
						    : M.Name;
					};
					const auto Key = TemplateSignature(A, Definition, DefinitionName(Definition, D.ModulePath, O.Name), Definition.bIsConst,
					    D.ModulePath, O.Name);
					FMethodDecl* Found = nullptr;
					for (auto& M : *O.Methods)
					{
						if (TemplateSignature(A, M, DefinitionName(M, O.Module, NamespaceOf(O.Name)),
						        M.bIsConst, O.Module, NamespaceOf(O.Name)) == Key)
						{
							if (Found)
							{
								Error(Diagnostics, "ambiguous method definition", T.front().Location);
								return false;
							}
							Found = &M;
						}
					}

					if (!Found || Found->bConstexpr != Definition.bConstexpr || Found->Body || Found->bDeleted || !Definition.Body || Definition.bVirtual ||
					    Definition.bOverride || Definition.bFinal || Definition.bIsStatic ||
					    std::any_of(Definition.TemplateParameters.GetParameters().begin(), Definition.TemplateParameters.GetParameters().end(),
					        [](const auto& Parameter)
					{
						return Parameter.HasDefault();
					}) ||
					    (Found && !SameBounds(A, *Found, Definition, O.Module, D.ModulePath)) ||
					    (Definition.Name.rfind("operator conv ", 0) == 0
					            ? A.ConversionType(Found->ReturnType, O.Module, NamespaceOf(O.Name)) !=
					                A.ConversionType(Definition.ReturnType, D.ModulePath, O.Name)
					            : A.Type(TemplatePattern(*Found, Found->ReturnType), O.Module, NamespaceOf(O.Name)) !=
					                A.Type(TemplatePattern(Definition, Definition.ReturnType), D.ModulePath, O.Name)) ||
					    std::any_of(Definition.Parameters.begin(), Definition.Parameters.end(),
					        [](const auto& Q)
					{
						return bool(Q.DefaultValue);
					}))
					{
						Error(Diagnostics,
						    "source method definition has no eligible matching declaration, "
						    "or it is "
						    "already defined",
						    T.front().Location);
						return false;
					}
					SetBodyContext(*Definition.Body, D, Definition.Parameters, O.Name);
					for (std::size_t Index = 0; Index < TemplateParameterNames(Definition).size(); ++Index)
					{
						Definition.Body->DefinitionTemplateAliases.emplace(TemplateParameterNames(Definition)[Index],
						    TemplateParameterNames(*Found)[Index]);
					}
					Found->Body = std::move(Definition.Body);
					Fragments.push_back(Parser.TakeFragmentDeclarations());
				}
				else
				{
					if (!Written.empty())
					{
						Error(Diagnostics, "generic source definition owner not found", T.front().Location);
						return false;
					}
					FFunctionDecl Definition;
					VeroScriptParser Parser(Fragment, D.ModulePath, &P);
					std::unordered_set<std::string> HostNames;
					if (A.Bindings)
					{
						for (const auto& HostType : A.Bindings->GetTypes())
						{
							HostNames.insert(HostType.Name);
						}

						for (const auto& HostType : A.Bindings->GetTypeAliases())
						{
							HostNames.insert(HostType.Name);
						}

						for (const auto& HostType : A.Bindings->GetGenericTypeTemplates())
						{
							HostNames.insert(HostType.Name);
						}
					}
					Parser.SetHostTypeNames(std::move(HostNames));
					if (D.TemplateHeaders.size() > 1)
					{
						Error(Diagnostics, "a free function definition has only one template parameter group", T.front().Location);
						return false;
					}
					const auto Schema = D.TemplateHeaders.empty() ? FTemplateParameterList{} : D.TemplateHeaders.front();
					if (!Parser.ParseFunctionFragment(Prefix.empty() ? D.Namespace : Prefix, D.PrefixTypeParams, Definition, Schema))
					{
						Diagnostics.push_back(Parser.GetError());
						return false;
					}
					const std::string Name = Prefix.empty() ? std::string(T[Sep + 1].Lexeme)
					                                        : Prefix + "." + std::string(T[Sep + 1].Lexeme);
					FFunctionDecl* Found = nullptr;
					const auto Key =
					    TemplateSignature(A, Definition, "", false, D.ModulePath, Prefix);
					for (auto& F : P.Functions)
					{
						if (F.Name == Name &&
						    ModuleVisibility::CanAccessSymbol(P, F.ModulePath, F.bExported, D.ModulePath, &A.ImportIndex) &&
						    TemplateSignature(A, F, "", false, F.ModulePath, NamespaceOf(F.Name)) == Key)
						{
							if (Found)
							{
								Error(Diagnostics, "ambiguous function definition", T.front().Location);
								return false;
							}
							Found = &F;
						}
					}

					if (Found && Component(Found->ModulePath) != Component(D.ModulePath))
					{
						Error(Diagnostics, "source definition cannot cross package component ownership",
						    T.front().Location);
						return false;
					}
					if (Found && Found->bPrecompiledPackage && !Component(Found->ModulePath).empty())
					{
						D.bBound = true;
						continue;
					}
					if (!Found || Found->bConstexpr != Definition.bConstexpr || Found->Body || Found->bPrecompiledPackage || Found->bDeleted ||
					    !Definition.Body ||
					    A.Type(TemplatePattern(*Found, Found->ReturnType), Found->ModulePath, NamespaceOf(Found->Name)) !=
					        A.Type(TemplatePattern(Definition, Definition.ReturnType), D.ModulePath, Prefix) ||
					    std::any_of(Definition.TemplateParameters.GetParameters().begin(), Definition.TemplateParameters.GetParameters().end(),
					        [](const auto& Parameter)
					{
						return Parameter.HasDefault();
					}) ||
					    std::any_of(Definition.Parameters.begin(), Definition.Parameters.end(),
					        [](const auto& Q)
					{
						return bool(Q.DefaultValue);
					}) ||
					    !SameBounds(A, *Found, Definition))
					{
						Error(Diagnostics,
						    "source function definition has no eligible matching "
						    "declaration, or it is "
						    "already defined",
						    T.front().Location);
						return false;
					}
					SetBodyContext(*Definition.Body, D, Definition.Parameters, NamespaceOf(Found->Name));
					for (std::size_t Index = 0; Index < TemplateParameterNames(Definition).size(); ++Index)
					{
						Definition.Body->DefinitionTemplateAliases.emplace(TemplateParameterNames(Definition)[Index],
						    TemplateParameterNames(*Found)[Index]);
					}
					Found->Body = std::move(Definition.Body);
					Fragments.push_back(Parser.TakeFragmentDeclarations());
				}
				D.bBound = true;
			}
			for (auto& Fragment : Fragments)
			{
				const auto Move = [](auto& To, auto& From)
				{
					To.insert(To.end(), std::make_move_iterator(From.begin()), std::make_move_iterator(From.end()));
				};
				Move(P.Classes, Fragment->Classes);
				Move(P.Structs, Fragment->Structs);
				Move(P.Interfaces, Fragment->Interfaces);
				Move(P.Enums, Fragment->Enums);
				Move(P.TypeAliases, Fragment->TypeAliases);
				Move(P.ScopedTypeUses, Fragment->ScopedTypeUses);
			}
			Os = Owners(P);

			// Non-qualified bodies can complete only a prototype from their own file.
			std::map<std::string, std::size_t> Functions;
			for (std::size_t I = 0; I < P.Functions.size();)
			{
				auto& F = P.Functions[I];
				const auto Key = F.ModulePath + '\x1F' +
				    TemplateSignature(A, F, F.Name, false, F.ModulePath, NamespaceOf(F.Name));
				const auto Insert = Functions.emplace(Key, I);
				if (Insert.second)
				{
					++I;
					continue;
				}
				auto& Original = P.Functions[Insert.first->second];
				if (std::any_of(F.TemplateParameters.GetParameters().begin(), F.TemplateParameters.GetParameters().end(),
				        [](const auto& Parameter)
				{
					return Parameter.HasDefault();
				}) ||
				    std::any_of(F.TypeParamDefaults.begin(), F.TypeParamDefaults.end(),
				        [](const auto& Default)
				{
					return !Default.empty();
				}))
				{
					Error(Diagnostics, "source function definition cannot repeat default type arguments",
					    F.Location);
					return false;
				}
				if (Original.bConstexpr != F.bConstexpr || bool(Original.Body) == bool(F.Body) || F.bDeleted || Original.bDeleted || F.bExported ||
				    !F.DeclMeta.Annotations.empty() ||
				    !SameBounds(A, Original, F) ||
				    std::any_of(F.Parameters.begin(), F.Parameters.end(),
				        [](const auto& Q)
				{
					return bool(Q.DefaultValue);
				}) ||
				    A.Type(TemplatePattern(F, F.ReturnType), F.ModulePath, NamespaceOf(F.Name)) !=
				        A.Type(TemplatePattern(Original, Original.ReturnType), Original.ModulePath, NamespaceOf(Original.Name)))
				{
					Error(Diagnostics, "duplicate function declaration or definition: " + F.Name, F.Location);
					return false;
				}
				if (Original.Body)
				{
					Error(Diagnostics, "function prototype must precede its same-file definition",
					    F.Location);
					return false;
				}
				FSourceDefinition D;
				D.ModulePath = F.ModulePath;
				D.Namespace = NamespaceOf(F.Name);
				SetBodyContext(*F.Body, D, F.Parameters, NamespaceOf(F.Name));
				for (std::size_t Index = 0; Index < TemplateParameterNames(F).size(); ++Index)
				{
					F.Body->DefinitionTemplateAliases.emplace(TemplateParameterNames(F)[Index], TemplateParameterNames(Original)[Index]);
				}
				Original.Body = std::move(F.Body);
				P.Functions.erase(P.Functions.begin() + I);
			}
			return !A.Compiler.HasError();
		}
		std::vector<FAnnotationParameterView> Params(const std::vector<FFunctionParam>& Ps)
		{
			std::vector<FAnnotationParameterView> R;
			for (const auto& P : Ps)
			{
				R.push_back({P.Name, P.TypeName, P.bIsRef, P.bIsConst, P.bIsVariadic, bool(P.DefaultValue),
				    P.DefaultValueSource});
			}

			return R;
		}
		void BindCompileViews(std::vector<FAnnotationDeclarationView>& Declarations, FProgramPreparationAccess& A)
		{
			A.RefreshConditionalScopes();
			std::map<std::string, const FAnnotationDeclarationView*> ById;
			for (const auto& D : Declarations)
			{
				ById.emplace(D.Id, &D);
			}

			for (auto& D : Declarations)
			{
				bool Available = true;
				const auto* Ancestor = &D;
				for (std::size_t Depth = 0; Ancestor && Depth <= Declarations.size(); ++Depth)
				{
					if (!A.IncludesScope(Ancestor->ScopeId))
					{
						Available = false;
						break;
					}
					const auto Parent = ById.find(Ancestor->OwnerId);
					Ancestor = Parent == ById.end() ? nullptr : Parent->second;
				}
				if (!Available)
				{
					continue;
				}
				const auto Owner = ById.find(D.OwnerId);
				const auto Ns = NamespaceOf(Owner == ById.end() ? D.Name : Owner->second->Name);
				D.CanonicalTypeName = D.TypeName.empty() ? "" : A.Type(D.TypeName, D.ModulePath, Ns);
				for (const auto& Base : D.BaseTypes)
				{
					D.CanonicalBaseTypes.push_back(A.Type(Base, D.ModulePath, Ns));
				}

				for (auto& Bound : D.TypeBounds)
				{
					Bound.CanonicalInterfaceTypeName = A.Type(Bound.InterfaceTypeName, D.ModulePath, Ns);
				}

				for (auto& Param : D.Parameters)
				{
					Param.CanonicalTypeName = A.Type(Param.TypeName, D.ModulePath, Ns);
				}
			}
			if (A.Compiler.HasError())
			{
				throw std::runtime_error(A.Compiler.GetError().Message);
			}
		}

	} // namespace
	const std::vector<FGeneratedDeclarationDocument>& FPreparedProgram::GetGeneratedDocuments() const
	{
		return Program->GeneratedDocuments;
	}

	bool FPreparedProgram::MatchesHost(const std::vector<FNativeFunction>* N,
	    const IHostBindingCatalog* B) const
	{
		return HostFingerprint == HostIdentity(N, B);
	}

	const FSourceMetadataSnapshot& FPreparedProgram::GetSourceMetadata() const
	{
		static const FSourceMetadataSnapshot Empty;
		return Program->SourceMetadata ? *Program->SourceMetadata : Empty;
	}

	const std::vector<FCompileGenerationOperation>& FPreparedProgram::GetCompileGeneratedOperations() const
	{
		return Program->CompileGeneratedOperations;
	}

	bool FPreparedProgram::MatchesProcessing(
	    const FCompileProcessingOptions& O, const IHostBindingCatalog* B) const
	{
		auto Effective = O;
		if (B)
		{
			for (auto Definition : GetAnnotationDefinitions(*B))
			{
				Effective.AnnotationDefinitions.push_back(std::move(Definition));
			}
		}

		FCompileProcessingSession S(std::move(Effective));
		return !S.HasErrors() && Program->CompileProcessingFingerprint == S.Fingerprint();
	}

	bool VeroScriptProgramPreparation::TryGetCacheInputFingerprint(const FProgram& P,
	    const FAnnotationProcessingOptions& O, std::string& OutFingerprint,
	    const std::vector<FNativeFunction>* N, const IHostBindingCatalog* B)
	{
		FConstantEvaluationRequestScope ConstantRequest(P.ConstantEvaluationLimits);
		Check(O);
		OutFingerprint.clear();
		if (P.bDeclarationsPrepared || B || (N && !N->empty()) || O.Registry || P.ModuleEnvironment || !P.PrecompiledPackageModules.empty() || P.ScriptCompileExtensionTask || !P.AnnotationDependencies.empty() || !P.CompileGeneratedOperations.empty() || std::any_of(P.PackageSourceModules.begin(), P.PackageSourceModules.end(), [](const auto& Module)
		{
			return Module.bFromPackage;
		}))
		{
			return false;
		}
		const auto& Session = P.CompileProcessingSession;
		if (!Session || !Session->Cacheable || Session->Options.Registry || !Session->Diagnostics.empty() || !Session->Dependencies.empty() || !Session->ReplayOperations.empty() || !P.SourceMetadata || P.SourceMetadata->Documents.empty())
		{
			return false;
		}
		Session->Check();
		const auto Request = CurrentConstantEvaluationRequest();
		const auto& Limits = Request ? Request->Limits : P.ConstantEvaluationLimits;
		if (!Limits.MaxWork || !Limits.MaxCallDepth || !Limits.MaxExpressionDepth || !Limits.MaxMemoryBytes) return false;
		std::string Fingerprint = "preparation-input-1";
		Append(Fingerprint, Session->Fingerprint());
		Append(Fingerprint, P.EntryModulePath);
		AppendNumber(Fingerprint, Limits.MaxWork);
		AppendNumber(Fingerprint, Limits.MaxCallDepth);
		AppendNumber(Fingerprint, Limits.MaxExpressionDepth);
		AppendNumber(Fingerprint, Limits.MaxMemoryBytes);
		AppendNumber(Fingerprint, Limits.MaxCacheBytes);
		AppendNumber(Fingerprint, O.MaxOperations);
		AppendNumber(Fingerprint, O.MaxSourceBytes);
		AppendNumber(Fingerprint, O.MaxProcessorsPerAnnotation);
		// Source hooks may change mappings/comments without changing the emitted
		// text. Include the owned input snapshot, before preparation trims documents.
		std::vector<std::uint8_t> MetadataBytes;
		std::string MetadataError;
		if (!EncodeSourceMetadata(*P.SourceMetadata, MetadataBytes, MetadataError)) return false;
		AppendNumber(Fingerprint, Runtime::Fnv1a64(14695981039346656037ull, MetadataBytes.data(), MetadataBytes.size()));
		OutFingerprint = std::move(Fingerprint);
		return true;
	}

	bool VeroScriptProgramPreparation::MatchesHost(const FProgram& P, const std::vector<FNativeFunction>* N,
	    const IHostBindingCatalog* B)
	{
		return P.PreparationSeal && P.PreparationSeal->HostFingerprint == HostIdentity(N, B) && P.PreparationSeal->ConfigurationFingerprint == P.DeclarationPreparationFingerprint;
	}

	bool FPreparedProgram::MatchesDependencies(const FCompileProcessingOptions& O) const
	{
		for (const auto& D : Dependencies)
		{
			CheckAnalysisCancellation();
			if (O.CheckCancelled)
			{
				O.CheckCancelled();
			}

			if (!O.ReadDependency) return false;
			std::string Content;
			const bool Exists = O.ReadDependency(D.Path, Content);
			if (Exists != D.bExists || Content != D.Content) return false;
		}
		return true;
	}

	FProgramPreparationResult VeroScriptProgramPreparation::Prepare(std::unique_ptr<FProgram> P,
	    const FAnnotationProcessingOptions& O,
	    const std::vector<FNativeFunction>* N,
	    const IHostBindingCatalog* B)
	{
		FConstantEvaluationRequestScope ConstantRequest(P ? P->ConstantEvaluationLimits : FConstantEvaluationLimits{});
		FProgramPreparationResult R;
		static thread_local bool Active = false;
		if (Active)
		{
			Error(R.Diagnostics, "annotation preparation cannot be reentered");
			return R;
		}
		struct FGuard
		{
			FGuard(bool& F) : Flag(F)
			{
				Flag = true;
			}

			~FGuard()
			{
				Flag = false;
			}

			bool& Flag;
		} Guard(Active);
		try
		{
			Check(O);
			if (!P)
			{
				Error(R.Diagnostics, "cannot prepare a null program");
				return R;
			}
			if (!P->bDeclarationsPrepared)
			{
				if (const auto Request = CurrentConstantEvaluationRequest())
				{
					P->ConstantEvaluationLimits = Request->Limits;
				}
			}
			const auto& InitialConstantLimits = P->ConstantEvaluationLimits;
			if (!InitialConstantLimits.MaxWork || !InitialConstantLimits.MaxCallDepth ||
			    !InitialConstantLimits.MaxExpressionDepth || !InitialConstantLimits.MaxMemoryBytes)
			{
				R.Diagnostics.push_back(MakeDiagnostic("diag.constexpr.positive_limits"));
				return R;
			}
			if (P->bDeclarationsPrepared && !MatchesHost(*P, N, B))
			{
				Error(R.Diagnostics, "prepared program host configuration does not match");
				return R;
			}
			if (P->bDeclarationsPrepared && O.Registry)
			{
				Error(R.Diagnostics, "cannot apply processors again to a prepared program");
				return R;
			}
			if (O.Registry && !O.Registry->IsFrozen())
			{
				Error(R.Diagnostics, "annotation processor registry must be frozen");
				return R;
			}
			auto Prepared = std::make_shared<FPreparedProgram>();
			Prepared->HostFingerprint = HostIdentity(N, B);
			if (!P->bDeclarationsPrepared && !P->CompileProcessingSession)
			{
				FCompileProcessingOptions Processing;
				if (B)
				{
					Processing.AnnotationDefinitions = GetAnnotationDefinitions(*B);
				}

				P->CompileProcessingSession =
				    std::make_shared<FCompileProcessingSession>(std::move(Processing));
				if (P->SourceMetadata)
				{
					*P->CompileProcessingSession->Metadata = *P->SourceMetadata;
				}

				P->SourceMetadata = P->CompileProcessingSession->Metadata;
			}
			if (!P->bDeclarationsPrepared && P->CompileProcessingSession)
			{
				std::string AdapterError;
				if (!AttachAnnotationProcessors(*P->CompileProcessingSession, O, AdapterError))
				{
					Error(R.Diagnostics, AdapterError);
					return R;
				}
				if (!RestoreCompileProcessing(*P, R.Diagnostics)) return R;
				CollectProgramSourceMetadata(*P);
				if (P->CompileProcessingSession->Metadata->Documents.empty())
				{
					Prepared->bCacheable = false;
				}

				for (auto& Source : P->PackageSourceModules)
				{
					if (!Source.bFromPackage)
					{
						auto Initial = std::make_shared<FSourceMetadataSnapshot>();
						Initial->Retention = P->CompileProcessingSession->Options.CommentRetention;
						// Replay resolves generated annotations against the producer's active
						// registrations, independently of imported or consumer definitions.
						Initial->Definitions = P->CompileProcessingSession->Options.AnnotationDefinitions;
						for (const auto& D : P->CompileProcessingSession->Metadata->Documents)
						{
							if (D.ModuleId == Source.ModulePath)
							{
								Initial->Documents.push_back(D);
							}
						}

						for (const auto& Node : P->CompileProcessingSession->Metadata->Nodes)
						{
							if (Node.ModuleId == Source.ModulePath)
							{
								Initial->Nodes.push_back(Node);
							}
						}

						Source.InitialSourceMetadata = std::move(Initial);
					}
				}

				if (!ProcessCompileDeclarations(*P, ECompileProcessingPhase::Types, B, R.Diagnostics))
				{
					return R;
				}
				if (!P->GeneratedBlockTypes.empty() && !ResolveDeferredProgram(*P, B, R.Diagnostics, true))
				{
					return R;
				}

				if (!ResolveOutlineDeclarationTypes(*P, B, R.Diagnostics)) return R;
			}
			FScriptError ScopeError;
			if (!BindScopedTypeEnvironments(*P, ScopeError))
			{
				R.Diagnostics.push_back(std::move(ScopeError));
				return R;
			}
			FProgramPreparationAccess A(*P, N, B);
			auto Os = Owners(*P);
			if (P->bDeclarationsPrepared)
			{
				std::size_t Bytes = 0;
				for (const auto& D : P->GeneratedDocuments)
				{
					if (D.Source.size() > O.MaxSourceBytes - std::min(Bytes, O.MaxSourceBytes))
					{
						Error(R.Diagnostics, "prepared generation source budget exceeded");
						return R;
					}
					Bytes += D.Source.size();
				}
				if (P->GeneratedDocuments.size() > O.MaxOperations)
				{
					Error(R.Diagnostics, "prepared generation operation budget exceeded");
					return R;
				}
				Prepared->Fingerprint = P->DeclarationPreparationFingerprint;
				Prepared->bCacheable = P->bDeclarationPreparationCacheable;
				Prepared->Operations = P->DeclarationGeneratedOperations;
				Prepared->Declarations = GetCompileDeclarationViews(*P);
				BindCompileViews(Prepared->Declarations, A);
				Prepared->Dependencies = P->AnnotationDependencies;
				R.Dependencies = P->AnnotationDependencies;
				Prepared->Program = std::move(P);
				R.Prepared = std::move(Prepared);
				return R;
			}
			for (const auto& Alias : P->TypeAliases)
			{
				if (!A.IncludesScope(Alias.Scope.ScopeId))
				{
					continue;
				}
				A.Type(Alias.Name, Alias.ModulePath, NamespaceOf(Alias.Name));
			}

			if (A.Compiler.HasError())
			{
				R.Diagnostics.push_back(A.Compiler.GetError());
				return R;
			}
			// Validate the inheritance graph before executing native processors. Member
			// contracts are checked later, after generated virtual slots are available.
			std::map<std::string, std::vector<std::string>> Edges;
			std::map<std::string, FSourceLocation> TypeLocations;
			std::set<std::string> FinalTypes;
			const auto AddBases = [&](const auto& Ds)
			{
				for (const auto& D : Ds)
				{
					if (!A.IncludesScope(D.Scope.ScopeId))
					{
						continue;
					}
					const auto Id = A.Linkage(D.Name, D.ModulePath, D.bExported) + '\x1F' +
					    std::to_string(TemplateParameterNames(D).size());
					TypeLocations.emplace(Id, D.Location);
					if (D.bFinal)
					{
						FinalTypes.insert(Id);
					}

					std::vector<std::string> Bases;
					using T = std::decay_t<decltype(D)>;
					if constexpr (std::is_same_v<T, FStructDecl>)
					{
						if (!D.BaseTypeName.empty())
						{
							Bases.push_back(D.BaseTypeName);
						}

						Bases.insert(Bases.end(), D.InterfaceTypeNames.begin(), D.InterfaceTypeNames.end());
					}
					else if constexpr (std::is_same_v<T, FClassDecl>)
					{
						Bases = D.BaseTypeNames;
						Bases.insert(Bases.end(), D.InterfaceTypeNames.begin(), D.InterfaceTypeNames.end());
					}
					else
					{
						Bases = D.BaseInterfaceNames;
					}

					for (auto Base : Bases)
					{
						auto Target = A.InheritanceKey(Base, D.ModulePath, NamespaceOf(D.Name));
						Edges[Id].push_back(std::move(Target));
					}
				}
			};
			AddBases(P->Structs);
			AddBases(P->Classes);
			AddBases(P->Interfaces);
			std::map<std::string, unsigned> Colors;
			std::function<bool(const std::string&, unsigned)> Visit = [&](const auto& Id, unsigned Depth)
			{
				Check(O);
				if (Depth >= 256)
				{
					Error(R.Diagnostics, "inheritance nesting limit exceeded", TypeLocations[Id]);
					return false;
				}
				if (Colors[Id] == 2)
				{
					return true;
				}

				if (Colors[Id] == 1)
				{
					Error(R.Diagnostics, "cyclic inheritance", TypeLocations[Id]);
					return false;
				}
				Colors[Id] = 1;
				for (const auto& Base : Edges[Id])
				{
					if (FinalTypes.count(Base))
					{
						Error(R.Diagnostics, "cannot inherit a final type", TypeLocations[Id]);
						return false;
					}
					if (Edges.count(Base) && !Visit(Base, Depth + 1))
					{
						return false;
					}
				}
				Colors[Id] = 2;
				return true;
			};
			for (const auto& Pair : Edges)
			{
				if (!Visit(Pair.first, 0))
				{
					return R;
				}
			}

			if (P->CompileProcessingSession)
			{
				P->CompileProcessingSession->BindDeclarationViews = [&](auto& Declarations)
				{
					BindCompileViews(Declarations, A);
				};
				if (!ProcessCompileDeclarations(*P, ECompileProcessingPhase::Members, B, R.Diagnostics) ||
				    !ResolveDeferredProgram(*P, B, R.Diagnostics))
				{
					return R;
				}

				Os = Owners(*P);
			}
			A.RefreshDeclarationIndexes();
			if (!BindSource(*P, A, Os, R.Diagnostics, O))
			{
				if (R.Diagnostics.empty())
				{
					R.Diagnostics.push_back(A.Compiler.GetError());
				}

				return R;
			}
			// Interface source is a declaration transport. Restore the lossless
			// metadata by canonical linkage/signature, including annotations that
			// cannot be represented by the legacy flattened interface text.
			const auto ParamTypes = [&](const auto& Ps, const std::string& Module, const std::string& Ns)
			{
				std::vector<std::string> Types;
				for (const auto& Q : Ps)
				{
					auto T = A.Type(Q.TypeName, Module, Ns);
					if (Q.bIsVariadic)
					{
						T = "array<" + T + ">";
					}

					if (Q.bIsRef)
					{
						T = (Q.bIsConst ? "const ref " : "ref ") + T;
					}

					Types.push_back(std::move(T));
				}
				return Types;
			};
			const auto FunctionMeta = [&](FDeclMeta& Meta, const std::string& Key)
			{
				for (const auto& Module : P->PrecompiledPackageModules)
				{
					if (Module)
					{
						const auto It = Module->FunctionIndices.find(Key);
						if (It != Module->FunctionIndices.end())
						{
							Meta = Module->Functions[It->second].DeclMeta;
							return;
						}
					}
				}
			};
			const auto RestoreTypes = [&](auto& Ds)
			{
				for (auto& D : Ds)
				{
					if (!D.bPrecompiledPackage)
					{
						continue;
					}

					const auto Owner = A.Linkage(D.Name, D.ModulePath, D.bExported);
					const auto Ns = NamespaceOf(D.Name);
					for (const auto& Module : P->PrecompiledPackageModules)
					{
						if (Module)
						{
							const auto Restore = [&](const auto& B)
							{
								D.DeclMeta = B.DeclMeta;
								for (auto& F : D.Fields)
								{
									const auto& Fields = F.bIsStatic ? B.StaticFields : B.Fields;
									for (const auto& Field : Fields)
									{
										if (Field.Name == F.Name)
										{
											F.DeclMeta = Field.DeclMeta;
											break;
										}
									}
								}
							};
							using TDecl = std::decay_t<decltype(D)>;
							if constexpr (std::is_same_v<TDecl, FInterfaceDecl>)
							{
								for (const auto& B : Module->Interfaces)
								{
									if (B.Name == Owner)
									{
										Restore(B);
										break;
									}
								}
							}
							else
							{
								for (const auto& B : Module->Types)
								{
									if (B.Name == Owner)
									{
										Restore(B);
										break;
									}
								}
							}
						}
					}

					for (auto& M : D.Methods)
					{
						FunctionMeta(M.DeclMeta,
						    MakeMethodFunctionName(
						        Owner, M.Name, ParamTypes(M.Parameters, D.ModulePath, Ns), M.bIsConst));
					}

					using TDecl = std::decay_t<decltype(D)>;
					if constexpr (!std::is_same_v<TDecl, FInterfaceDecl>)
					{
						const auto Dot = D.Name.rfind('.');
						const auto Leaf = Dot == std::string::npos ? D.Name : D.Name.substr(Dot + 1);
						for (auto& C : D.Constructors)
						{
							FunctionMeta(C.DeclMeta,
							    MakeConstructorFunctionName(
							        Owner, Leaf, ParamTypes(C.Parameters, D.ModulePath, Ns)));
						}

						if (D.Destructor)
						{
							FunctionMeta(D.Destructor->DeclMeta, MakeDestructorFunctionName(Owner, Leaf));
						}
					}
				}
			};
			RestoreTypes(P->Structs);
			RestoreTypes(P->Classes);
			RestoreTypes(P->Interfaces);
			for (auto& F : P->Functions)
			{
				if (F.bPrecompiledPackage)
				{
					FunctionMeta(F.DeclMeta, A.FunctionKey(F));
				}
			}

			for (auto& E : P->Enums)
			{
				if (E.bPrecompiledPackage)
				{
					for (const auto& Module : P->PrecompiledPackageModules)
					{
						if (Module)
						{
							for (const auto& BinaryEnum : Module->Enums)
							{
								if (BinaryEnum.Name == A.Linkage(E.Name, E.ModulePath, E.bExported))
								{
									E.DeclMeta = BinaryEnum.DeclMeta;
									for (auto& M : E.Members)
									{
										for (const auto& BM : BinaryEnum.Members)
										{
											if (BM.Name == M.Name)
											{
												M.DeclMeta = BM.DeclMeta;
												break;
											}
										}
									}
								}
							}
						}
					}
				}
			}

			const auto GlobalMeta = [&](FVarDeclStmt& G)
			{
				if (!G.bPrecompiledPackage)
				{
					return;
				}

				const auto Key = A.Linkage(G.Name, G.ModulePath, G.bExported);
				for (const auto& Module : P->PrecompiledPackageModules)
				{
					if (Module)
					{
						const auto Slot = Module->GlobalSlots.find(Key);
						if (Slot != Module->GlobalSlots.end() && Slot->second < Module->GlobalMetadata.size())
						{
							G.DeclMeta = Module->GlobalMetadata[Slot->second].DeclMeta;
							return;
						}
					}
				}
			};
			for (auto& S : P->TopLevelStatements)
			{
				if (S)
				{
					if (S->Kind == EStmtKind::VarDecl)
					{
						GlobalMeta(static_cast<FVarDeclStmt&>(*S));
					}

					else if (S->Kind == EStmtKind::VarDeclGroup)
					{
						for (auto& G : static_cast<FVarDeclGroupStmt&>(*S).Declarations)
						{
							GlobalMeta(*G);
						}
					}
				}
			}

			const std::vector<FAnnotationProcessorDesc> Empty;
			const auto& Processors = O.Registry ? O.Registry->GetProcessors() : Empty;
			if (P->CompileProcessingSession)
			{
				if (!ProcessCompileDeclarations(*P, ECompileProcessingPhase::Bodies, B, R.Diagnostics)) return R;
				Os = Owners(*P);
				auto Session = P->CompileProcessingSession;
				R.Diagnostics.insert(R.Diagnostics.end(), Session->Diagnostics.begin(), Session->Diagnostics.end());
				P->CompileGeneratedOperations = Session->Operations;
				for (auto& Source : P->PackageSourceModules)
				{
					if (!Source.bFromPackage)
					{
						for (const auto& Op : Session->Operations)
						{
							if (Op.Scope.ModuleId == Source.ModulePath)
							{
								Source.CompileGeneratedOperations.push_back(Op);
							}
						}
					}
				}

				P->CompileProcessingFingerprint = Session->Fingerprint();
				Prepared->bCacheable = Prepared->bCacheable && Session->Cacheable;
				R.Dependencies.insert(R.Dependencies.end(), Session->Dependencies.begin(), Session->Dependencies.end());
				CollectProgramSourceMetadata(*P);
				for (const auto& D : Session->Metadata->Documents)
				{
					Append(Prepared->Fingerprint, D.Id);
					AppendNumber(Prepared->Fingerprint, Runtime::Fnv1a64(14695981039346656037ull, D.Text));
				}
				FinalizeCompileProcessing(*P);
				if (P->ScriptCompileExtensionTask)
				{
					if (!P->ScriptCompileExtensionTask->Close(R.Diagnostics)) return R;
					P->ScriptCompileExtensionTask.reset();
				}
			}
			if (!BindScopedTypeEnvironments(*P, ScopeError))
			{
				R.Diagnostics.push_back(std::move(ScopeError));
				return R;
			}
			if (!A.ValidateTemplateDeclarations(*P))
			{
				R.Diagnostics.push_back(A.Compiler.GetError());
				return R;
			}
			P->bDeclarationsPrepared = true;
			P->PreparationHostFingerprint = Prepared->HostFingerprint;
			Prepared->Declarations = GetCompileDeclarationViews(*P);
			BindCompileViews(Prepared->Declarations, A);
			Prepared->Dependencies = R.Dependencies;
			P->AnnotationDependencies = R.Dependencies;
			const auto SourceFingerprint = std::move(Prepared->Fingerprint);
			Prepared->Fingerprint = Prepared->HostFingerprint;
			const auto& ConstantLimits = P->ConstantEvaluationLimits;
			Prepared->Fingerprint += "$constexpr-1:" + std::to_string(ConstantLimits.MaxWork) + ":" + std::to_string(ConstantLimits.MaxCallDepth) + ":" + std::to_string(ConstantLimits.MaxExpressionDepth) + ":" + std::to_string(ConstantLimits.MaxMemoryBytes) + ":" + std::to_string(ConstantLimits.MaxCacheBytes);

			Append(Prepared->Fingerprint, SourceFingerprint);
			Append(Prepared->Fingerprint, P->CompileProcessingFingerprint);
			std::vector<std::uint8_t> ProcessingBytes;
			std::string ProcessingError;
			if (!EncodeCompileOperations(P->CompileGeneratedOperations, ProcessingBytes, ProcessingError))
			{
				Error(R.Diagnostics, ProcessingError);
				return R;
			}
			AppendNumber(Prepared->Fingerprint,
			    Runtime::Fnv1a64(14695981039346656037ull, ProcessingBytes.data(), ProcessingBytes.size()));
			if (P->SourceMetadata)
			{
				if (!EncodeSourceMetadata(*P->SourceMetadata, ProcessingBytes, ProcessingError))
				{
					Error(R.Diagnostics, ProcessingError);
					return R;
				}
				AppendNumber(Prepared->Fingerprint,
				    Runtime::Fnv1a64(
				        14695981039346656037ull, ProcessingBytes.data(), ProcessingBytes.size()));
			}
			Append(Prepared->Fingerprint, P->EntryModulePath);
			AppendNumber(Prepared->Fingerprint, O.MaxOperations);
			AppendNumber(Prepared->Fingerprint, O.MaxSourceBytes);
			AppendNumber(Prepared->Fingerprint, O.MaxProcessorsPerAnnotation);
			for (const auto& Import : P->Imports)
			{
				Append(Prepared->Fingerprint, Import.ImporterModulePath);
				Append(Prepared->Fingerprint, Import.ResolvedPath);
				AppendNumber(Prepared->Fingerprint, Import.bReExport);
				AppendNumber(Prepared->Fingerprint, Import.bImplicit);
			}
			std::vector<std::string> Visible(P->ProjectVisibleModulePaths.begin(),
			    P->ProjectVisibleModulePaths.end());
			std::sort(Visible.begin(), Visible.end());
			for (const auto& Path : Visible)
			{
				Append(Prepared->Fingerprint, Path);
			}

			for (const auto& Op : Prepared->Operations)
			{
				Append(Prepared->Fingerprint, Op.TargetId);
				Append(Prepared->Fingerprint, Op.Source);
				AppendNumber(Prepared->Fingerprint, Op.Kind);
				AppendNumber(Prepared->Fingerprint, Op.Access);
				Append(Prepared->Fingerprint, Op.GenerationId);
			}
			for (const auto& M : P->PackageSourceModules)
			{
				Append(Prepared->Fingerprint, M.ModulePath);
				Append(Prepared->Fingerprint, M.Source);
			}
			for (const auto& D : Processors)
			{
				Append(Prepared->Fingerprint, D.Name);
				Append(Prepared->Fingerprint, D.ProcessorId);
				Append(Prepared->Fingerprint, D.Version);
				Append(Prepared->Fingerprint, D.ConfigurationKey);
				AppendNumber(Prepared->Fingerprint, D.Targets);
				AppendNumber(Prepared->Fingerprint, D.Operations);
				AppendNumber(Prepared->Fingerprint, D.Order);
			}
			for (const auto& D : R.Dependencies)
			{
				Append(Prepared->Fingerprint, D.Path);
				Append(Prepared->Fingerprint, D.Content);
				AppendNumber(Prepared->Fingerprint, D.bExists);
			}
			P->DeclarationPreparationFingerprint = Prepared->Fingerprint;
			P->PreparationSeal = std::shared_ptr<const FProgramPreparationSeal>(new FProgramPreparationSeal(Prepared->HostFingerprint, Prepared->Fingerprint));
			P->bDeclarationPreparationCacheable = Prepared->bCacheable;
			P->DeclarationGeneratedOperations = Prepared->Operations;
			Prepared->Program = std::move(P);
			R.Prepared = std::move(Prepared);
			return R;
		}
		catch (const FAnalysisCancelled&)
		{
			throw;
		}
		catch (const std::invalid_argument& E)
		{
			const std::string_view Id = E.what();
			if (Id == "diag.memory_native_limit" || Id == "diag.memory_reserved_type" ||
			    Id == "diag.stdlib_native_limit" || Id == "diag.stdlib_reserved_binding")
			{
				R.Diagnostics.push_back(MakeDiagnostic(Id));
			}

			else
			{
				Error(R.Diagnostics, std::string("program preparation failed: ") + E.what());
			}

			return R;
		}
		catch (const std::exception& E)
		{
			Error(R.Diagnostics, std::string("program preparation failed: ") + E.what());
			return R;
		}
		catch (...)
		{
			Error(R.Diagnostics, "program preparation failed");
			return R;
		}
	}

	FProgramPreparationResult VeroScriptProgramPreparation::Prepare(const FProgram& P,
	    const FAnnotationProcessingOptions& O,
	    const std::vector<FNativeFunction>* N,
	    const IHostBindingCatalog* B)
	{
		try
		{
			Check(O);
			std::unique_ptr<FProgram> Working;
			if (!P.bDeclarationsPrepared && P.CompileProcessingSession &&
			    (P.CompileProcessingSession->Options.Registry &&
			        (P.CompileProcessingSession->Options.Registry->HasPhase(ECompileProcessingPhase::RawSource) ||
			            P.CompileProcessingSession->Options.Registry->HasPhase(ECompileProcessingPhase::ActiveSource))))
			{
				FProgramPreparationResult R;
				Error(R.Diagnostics, "source processing preparation requires ownership of the loading session; use the unique program entry point");
				return R;
			}
			{
				const auto* Previous = ActiveAnalysisControl;
				FAnalysisControlScope CloneControl(
				    [&]
				{
					if (Previous && Previous->ShouldCancel && Previous->ShouldCancel())
					{
						return true;
					}

					if (O.CheckCancelled)
					{
						O.CheckCancelled();
					}

					return false;
				},
				    Previous ? Previous->RecordStage : std::function<void(std::string_view, double)>{});
				Working = CloneProgram(P);
			}
			return Prepare(std::move(Working), O, N, B);
		}
		catch (const FAnalysisCancelled&)
		{
			throw;
		}
		catch (const std::exception& E)
		{
			FProgramPreparationResult R;
			Error(R.Diagnostics, std::string("program clone failed: ") + E.what());
			return R;
		}
	}

	FProgramPreparationResult VeroScriptProgramPreparation::PrepareWithProcessing(std::unique_ptr<FProgram> P,
	    const FCompileProcessingOptions& O, const std::vector<FNativeFunction>* N, const IHostBindingCatalog* B)
	{
		FConstantEvaluationRequestScope ConstantRequest(P ? P->ConstantEvaluationLimits : FConstantEvaluationLimits{});
		if (!P)
		{
			FProgramPreparationResult R;
			Error(R.Diagnostics, "cannot prepare a null program");
			return R;
		}
		auto Effective = O;
		if (B)
		{
			for (auto Definition : GetAnnotationDefinitions(*B))
			{
				Effective.AnnotationDefinitions.push_back(std::move(Definition));
			}
		}

		auto S = std::make_shared<FCompileProcessingSession>(std::move(Effective));
		if (S->HasErrors())
		{
			FProgramPreparationResult R;
			R.Diagnostics = S->Diagnostics;
			return R;
		}
		if (P->bDeclarationsPrepared)
		{
			if (P->CompileProcessingFingerprint != S->Fingerprint())
			{
				FProgramPreparationResult R;
				Error(R.Diagnostics,
				    "prepared processing configuration does not match; prepare original input again");
				return R;
			}
			return Prepare(std::move(P), {}, N, B);
		}
		if (O.Registry && (O.Registry->HasPhase(ECompileProcessingPhase::RawSource) || O.Registry->HasPhase(ECompileProcessingPhase::ActiveSource)))
		{
			if (!P->CompileProcessingSession ||
			    P->CompileProcessingSession->Fingerprint() != S->Fingerprint())
			{
				FProgramPreparationResult R;
				Error(R.Diagnostics, "source processing requires the configured source loading service");
				return R;
			}
		}
		else
		{
			if (P->SourceMetadata)
			{
				*S->Metadata = *P->SourceMetadata;
			}

			const auto PreviousDefinitions = std::move(S->Metadata->Definitions);
			S->Metadata->Definitions = S->Options.AnnotationDefinitions;
			for (const auto& D : PreviousDefinitions)
			{
				if (!S->Metadata->FindDefinition(D.Id))
				{
					S->Metadata->Definitions.push_back(D);
				}
			}

			P->CompileProcessingSession = S;
			P->SourceMetadata = S->Metadata;
		}
		return Prepare(std::move(P), {}, N, B);
	}
} // namespace VeroScript

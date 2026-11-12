// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCodegen.h"
#include "VeroScriptCompile.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptAst.h"
#include "VeroScriptAstClone.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptProgramPreparation.h"
#include <algorithm>
#include <limits>
#include <type_traits>
#include <unordered_map>
namespace VeroScript
{
	namespace
	{
		void PruneUnusedStandardLibraryDeclarations(FProgram& Program)
		{
			// Reserved markers survive package path sanitization. Keep all user
			// declarations; discard only unused declarations from trusted std modules.
			std::unordered_set<std::string> StandardModules, Referenced;
			for (const auto& F : Program.Functions)
			{
				if (F.Name.compare(0, 6, "__std_") == 0)
				{
					StandardModules.insert(F.ModulePath);
				}
			}
			const auto Extract = [&](auto& Declarations)
			{
				std::decay_t<decltype(Declarations)> Candidates, Retained;
				for (auto& D : Declarations)
				{
					(StandardModules.count(D.ModulePath) ? Candidates : Retained).push_back(std::move(D));
				}
				Declarations = std::move(Retained);
				return Candidates;
			};
			auto Functions = Extract(Program.Functions);
			auto Classes = Extract(Program.Classes);
			auto Structs = Extract(Program.Structs);
			std::unordered_set<std::string> FunctionNames;
			for (const auto& F : Functions)
			{
				FunctionNames.insert(F.Name);
			}
			std::unordered_multimap<std::string, std::string> Aliases;
			std::size_t AliasCount = 0;
			const auto* Names = StandardLibrary::GetFunctionAliases(AliasCount);
			for (std::size_t I = 0; I < AliasCount; ++I)
			{
				const auto Name = Names[I].QualifiedName;
				Aliases.emplace(Name, Names[I].CanonicalName);
				if (const auto Dot = Name.rfind('.'); Dot != std::string_view::npos)
				{
					Aliases.emplace(Name.substr(Dot + 1), Names[I].CanonicalName);
				}
			}
			const auto Record = [&](std::string_view Name)
			{
				if (Name.empty()) return;
				Referenced.emplace(Name);
				const auto Range = Aliases.equal_range(std::string(Name));
				for (auto I = Range.first; I != Range.second; ++I)
				{
					Referenced.insert(I->second);
				}
			};
			const auto RecordType = [&](std::string_view Name)
			{
				Record(Name);
				const auto Identifier = [](char C)
				{
					return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_' || (C >= '0' && C <= '9');
				};
				for (std::size_t I = 0; I < Name.size();)
				{
					if (!Identifier(Name[I]))
					{
						++I;
						continue;
					}
					const auto Begin = I++;
					while (I < Name.size() && (Identifier(Name[I]) || Name[I] == '.' || Name[I] == ':'))
					{
						++I;
					}
					std::string Symbol(Name.substr(Begin, I - Begin));
					for (std::size_t Sep; (Sep = Symbol.find("::")) != std::string::npos;)
					{
						Symbol.replace(Sep, 2, ".");
					}
					Record(StandardLibrary::ResolveTypeName(Symbol));
					Record(Symbol);
				}
			};
			const auto Parameters = [&](const auto& Params)
			{
				for (const auto& P : Params)
				{
					RecordType(P.TypeName);
				}
			};
			const auto Scan = [&](FProgram& P)
			{
				const auto Types = [&](const auto& Ds)
				{
					for (const auto& D : Ds)
					{
						for (const auto& T : D.TypeParamDefaults)
						{
							RecordType(T);
						}
						for (const auto& F : D.Fields)
						{
							RecordType(F.TypeName);
						}
						for (const auto& M : D.Methods)
						{
							RecordType(M.ReturnType);
							Parameters(M.Parameters);
						}
						if constexpr (!std::is_same_v<std::decay_t<decltype(D)>, FInterfaceDecl>)
						{
							for (const auto& C : D.Constructors)
							{
								Parameters(C.Parameters);
							}
						}
						if constexpr (std::is_same_v<std::decay_t<decltype(D)>, FClassDecl>)
						{
							for (const auto& B : D.BaseTypeNames)
							{
								RecordType(B);
							}
						}
						if constexpr (std::is_same_v<std::decay_t<decltype(D)>, FStructDecl>)
						{
							RecordType(D.BaseTypeName);
						}
						for (const auto& B : D.InterfaceTypeNames)
						{
							RecordType(B);
						}
					}
				};
				Types(P.Classes);
				Types(P.Structs);
				for (const auto& I : P.Interfaces)
				{
					for (const auto& B : I.BaseInterfaceNames)
					{
						RecordType(B);
					}
					for (const auto& M : I.Methods)
					{
						RecordType(M.ReturnType);
						Parameters(M.Parameters);
					}
				}
				for (const auto& F : P.Functions)
				{
					RecordType(F.ReturnType);
					Parameters(F.Parameters);
					for (const auto& T : F.TypeParamDefaults)
					{
						RecordType(T);
					}
				}
				for (const auto& A : P.TypeAliases)
				{
					RecordType(A.TypeName);
				}
				VisitProgramNodes(P, [&](FExpr& E)
				{
					switch (E.Kind)
					{
					case EExprKind::Call:
						{
							const auto& C = static_cast<FCallExpr&>(E);
							RecordType(C.Callee);
							for (const auto& T : C.TypeArgs)
							{
								RecordType(T);
							}
							break;
						}
					case EExprKind::Identifier:
						{
							const auto& Name = static_cast<FIdentifierExpr&>(E).Name;
							const auto Canonical = StandardLibrary::ResolveFunctionName(Name);
							if (FunctionNames.count(Name))
							{
								Record(Name);
							}
							if (!Canonical.empty() && FunctionNames.count(std::string(Canonical)))
							{
								Record(Canonical);
							}
							break;
						}
					case EExprKind::MethodCall:
						{
							const auto& C = static_cast<FMethodCallExpr&>(E);
							Record(C.Method);
							for (const auto& T : C.TypeArgs)
							{
								RecordType(T);
							}
							break;
						}
					case EExprKind::MemberAccess:
						{
							Record(static_cast<FMemberAccessExpr&>(E).Member);
							break;
						}
					case EExprKind::ArrayInit:
						{
							RecordType(static_cast<FArrayInitExpr&>(E).TypeName);
							break;
						}
					case EExprKind::MapInit:
						{
							RecordType(static_cast<FMapInitExpr&>(E).TypeName);
							break;
						}
					case EExprKind::Lambda:
						{
							const auto& L = static_cast<FLambdaExpr&>(E);
							RecordType(L.ReturnType);
							Parameters(L.Parameters);
							break;
						}
					default:
						{
							break;
						}
					}
				}, [&](FStmt& S)
				{
					if (S.Kind == EStmtKind::VarDecl)
					{
						RecordType(static_cast<FVarDeclStmt&>(S).TypeName);
					}
					if (S.Kind == EStmtKind::TypeAlias)
					{
						RecordType(static_cast<FTypeAliasStmt&>(S).TypeName);
					}
					if (S.Kind == EStmtKind::ForEach)
					{
						RecordType(static_cast<FForEachStmt&>(S).ElementTypeName);
					}
					if (S.Kind == EStmtKind::Try)
					{
						for (const auto& C : static_cast<FTryStmt&>(S).Catches)
						{
							RecordType(C.TypeName);
						}
					}
				});
			};
			Scan(Program);
			for (bool Changed = true; Changed;)
			{
				Changed = false;
				FProgram One;
				const auto Retain = [&](auto& Candidates, auto& Selected)
				{
					for (auto& D : Candidates)
					{
						if (!D.Name.empty() && Referenced.count(D.Name))
						{
							Selected.push_back(std::move(D));
							D.Name.clear();
							Changed = true;
						}
					}
				};
				Retain(Functions, One.Functions);
				Retain(Classes, One.Classes);
				Retain(Structs, One.Structs);
				Scan(One);
				for (auto& F : One.Functions)
				{
					Program.Functions.push_back(std::move(F));
				}
				for (auto& C : One.Classes)
				{
					Program.Classes.push_back(std::move(C));
				}
				for (auto& S : One.Structs)
				{
					Program.Structs.push_back(std::move(S));
				}
			}
		}

		std::unique_ptr<FProgram> NormalizeCodegenProgram(const FProgramPreparationResult& Result,
		    const IHostBindingCatalog* Bindings)
		{
			const auto& Input = Result.Prepared->GetProgram();
			const auto CheckSpecialMembers = [&](const auto& Types)
			{
				for (const auto& Type : Types)
				{
					for (const auto& Method : Type.Methods)
					{
						if (Method.bDefaulted)
						{
							throw std::runtime_error("AOT: defaulted assignment/equality requires the Core AOT runtime; standalone source export is unavailable");
						}
					}
					for (const auto& Ctor : Type.Constructors)
					{
						if (Ctor.bDefaulted && !Ctor.Parameters.empty())
						{
							throw std::runtime_error("AOT: defaulted copy construction requires the Core AOT runtime; standalone source export is unavailable");
						}
						if constexpr (std::is_same_v<std::decay_t<decltype(Type)>, FStructDecl>)
						{
							if (Ctor.Parameters.size() != 1 || !Ctor.Parameters[0].bIsRef || !Ctor.Parameters[0].bIsConst)
							{
								continue;
							}
							std::string Name = Ctor.Parameters[0].TypeName;
							std::unordered_set<std::string> Seen;
							while (Seen.insert(Name).second)
							{
								const auto Alias = std::find_if(Input.TypeAliases.begin(), Input.TypeAliases.end(),
								    [&](const auto& A)
								{
									return A.Name == Name && (A.ModulePath == Type.ModulePath || A.bExported);
								});
								if (Alias == Input.TypeAliases.end())
								{
									break;
								}
								Name = Alias->TypeName;
							}
							if (Name == Type.Name || Name.compare(0, Type.Name.size() + 1, Type.Name + "<") == 0)
							{
								throw std::runtime_error("AOT: script copy constructors require the Core AOT runtime; standalone source export is unavailable");
							}
						}
					}
				}
			};
			CheckSpecialMembers(Input.Structs);
			CheckSpecialMembers(Input.Classes);
			if (Input.ModuleEnvironment)
			{
				VeroScriptCompiler Verifier;
				// A published environment catalog remains authoritative. Only an
				// environment without a catalog can use the explicit host bindings.
				const auto* VerificationBindings = Input.ModuleEnvironment->GetDescription().Bindings.get();
				if (!VerificationBindings)
				{
					VerificationBindings = Bindings;
				}

				auto Bytecode = Verifier.CompilePrepared(*Result.Prepared, nullptr, VerificationBindings);
				if (!Bytecode || Verifier.HasError())
				{
					throw std::runtime_error(Verifier.GetError().Message);
				}

				if (!Bytecode->RequiredExternalDependencies.empty())
				{
					throw std::runtime_error(
					    "AOT: native package contracts require a target runtime binding adapter; standalone output is unavailable");
				}
			}
			auto Program = CloneProgram(Input);
			const auto NormalizeDefinition =
			    [&](auto& Method, const std::string& Module, const std::string& OwnerId)
			{
				if (!Method.Body || Method.Body->DefinitionModulePath.empty())
				{
					return;
				}

				// Bind signature aliases in the declaration module before the
				// emitter enters the implementation module's lexical scope.
				for (const auto& V : Result.Prepared->GetDeclarations())
				{
					if (V.ModulePath != Module || V.OwnerId != OwnerId || V.Name != Method.Name ||
					    V.TypeName != Method.ReturnType || V.Parameters.size() != Method.Parameters.size())
					{
						continue;
					}

					if constexpr (std::is_same_v<std::decay_t<decltype(Method)>, FFunctionDecl>)
					{
						if (V.Target != EAnnotationTarget::Function || V.TypeParameters != Method.TypeParams)
						{
							continue;
						}
					}
					else
					{
						if ((V.Target != EAnnotationTarget::Method &&
						        V.Target != EAnnotationTarget::StaticMethod) ||
						    V.bIsConst != Method.bIsConst || V.bIsStatic != Method.bIsStatic)
						{
							continue;
						}
					}
					bool Match = true;
					for (std::size_t I = 0; I < V.Parameters.size(); ++I)
					{
						Match = Match && V.Parameters[I].TypeName == Method.Parameters[I].TypeName &&
						    V.Parameters[I].bIsRef == Method.Parameters[I].bIsRef &&
						    V.Parameters[I].bIsConst == Method.Parameters[I].bIsConst &&
						    V.Parameters[I].bIsVariadic == Method.Parameters[I].bIsVariadic;
					}

					if (!Match)
					{
						continue;
					}

					if (!V.CanonicalTypeName.empty())
					{
						Method.ReturnType = V.CanonicalTypeName;
					}
					for (std::size_t I = 0; I < V.Parameters.size(); ++I)
					{
						if (!V.Parameters[I].CanonicalTypeName.empty())
						{
							Method.Parameters[I].TypeName = V.Parameters[I].CanonicalTypeName;
						}
					}

					break;
				}
				if (Method.Body->DefinitionParameterNames.empty())
				{
					return;
				}

				if (Method.Parameters.size() != Method.Body->DefinitionParameterNames.size())
				{
					throw std::logic_error("definition parameter count mismatch");
				}

				for (std::size_t I = 0; I < Method.Parameters.size(); ++I)
				{
					Method.Parameters[I].Name = Method.Body->DefinitionParameterNames[I];
				}
			};
			for (auto& F : Program->Functions)
			{
				NormalizeDefinition(F, F.ModulePath, "");
			}

			const auto Types = [&](auto& Ds)
			{
				for (auto& D : Ds)
				{
					auto OwnerId = D.DeclMeta.StableId;
					if (OwnerId.empty())
					{
						for (const auto& V : Result.Prepared->GetDeclarations())
						{
							if (V.ModulePath == D.ModulePath && V.Name == D.Name &&
							    V.TypeParameters == D.TypeParams &&
							    (V.Target == EAnnotationTarget::Class ||
							        V.Target == EAnnotationTarget::Struct ||
							        V.Target == EAnnotationTarget::Interface))
							{
								OwnerId = V.Id;
								break;
							}
						}
					}

					for (auto& M : D.Methods)
					{
						NormalizeDefinition(M, D.ModulePath, OwnerId);
					}
				}
			};
			Types(Program->Structs);
			Types(Program->Classes);
			Types(Program->Interfaces);
			PruneUnusedStandardLibraryDeclarations(*Program);
			return Program;
		}
	} // namespace

	bool ApplyCodegenModuleEnvironment(FCodegenOptions& Options, std::string& Error)
	{
		if (!Options.ModuleEnvironment || Options.CompileProcessing.ModuleEnvironment == Options.ModuleEnvironment) return true;
		FCompileOptions O;
		O.ModuleEnvironment = Options.ModuleEnvironment;
		O.Bindings = Options.Bindings;
		O.SourceOverlays = Options.SourceOverlays;
		O.CompileProcessing = Options.CompileProcessing;
		O.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		for (const auto& P : Options.ImportPaths)
		{
			O.ImportPaths.push_back(P);
		}

		for (const auto& P : Options.StdlibSearchPaths)
		{
			O.StdlibSearchPaths.push_back(P);
		}

		for (const auto& P : Options.ImplicitImportPaths)
		{
			O.ImplicitImportPaths.push_back(P);
		}

		if (!Options.ModuleEnvironment->Apply(O, Error)) return false;
		// Copy string views before replacing their backing vectors.
		auto Copy = [](const auto& Values)
		{
			std::vector<std::string> R;
			for (const auto& V : Values)
			{
				R.emplace_back(V);
			}
			return R;
		};
		auto Imports = Copy(O.ImportPaths), Stdlib = Copy(O.StdlibSearchPaths),
		     Implicit = Copy(O.ImplicitImportPaths);
		Options.ImportPaths = std::move(Imports);
		Options.StdlibSearchPaths = std::move(Stdlib);
		Options.ImplicitImportPaths = std::move(Implicit);
		Options.Bindings = O.Bindings;
		Options.SourceOverlays = std::move(O.SourceOverlays);
		Options.CompileProcessing = std::move(O.CompileProcessing);
		Options.ScriptCompileExtensions = std::move(O.ScriptCompileExtensions);
		return true;
	}
	std::unique_ptr<FProgram> PrepareCodegenProgram(std::unique_ptr<FProgram> Input,
	    const FAnnotationProcessingOptions& Options,
	    const IHostBindingCatalog* Bindings, std::string& Error)
	{
		auto Result = VeroScriptProgramPreparation::Prepare(std::move(Input), Options, nullptr, Bindings);
		if (!Result.Succeeded())
		{
			const auto Primary = std::find_if(Result.Diagnostics.begin(), Result.Diagnostics.end(),
			    [](const auto& D)
			{
				return D.Severity == ECompileDiagnosticSeverity::Error;
			});
			Error = Primary == Result.Diagnostics.end() ? "AOT: declaration preparation failed" : Primary->Message;
			return {};
		}
		try
		{
			return NormalizeCodegenProgram(Result, Bindings);
		}
		catch (const std::exception& E)
		{
			Error = E.what();
			return {};
		}
	}
	std::unique_ptr<FProgram> PrepareCodegenProgram(const FProgram& Input, const FAnnotationProcessingOptions& Options,
	    const IHostBindingCatalog* Bindings, std::string& Error)
	{
		auto Processing = Options;
		if (Input.bDeclarationsPrepared)
		{
			// Emission reuses generation already accepted by the source stage. Its
			// default preparation options must not replace the producer's budget.
			Processing.MaxOperations = std::max(Processing.MaxOperations, Input.GeneratedDocuments.size());
			std::size_t SourceBytes = 0;
			for (const auto& Document : Input.GeneratedDocuments)
			{
				if (Document.Source.size() > std::numeric_limits<std::size_t>::max() - SourceBytes)
				{
					Error = "AOT: generated source size overflow";
					return {};
				}
				SourceBytes += Document.Source.size();
			}
			Processing.MaxSourceBytes = std::max(Processing.MaxSourceBytes, SourceBytes);
		}
		auto Result = VeroScriptProgramPreparation::Prepare(Input, Processing, nullptr, Bindings);
		if (!Result.Succeeded())
		{
			const auto Primary = std::find_if(Result.Diagnostics.begin(), Result.Diagnostics.end(),
			    [](const auto& D)
			{
				return D.Severity == ECompileDiagnosticSeverity::Error;
			});
			Error = Primary == Result.Diagnostics.end() ? "AOT: declaration preparation failed" : Primary->Message;
			return {};
		}
		try
		{
			return NormalizeCodegenProgram(Result, Bindings);
		}
		catch (const std::exception& E)
		{
			Error = E.what();
			return {};
		}
	}
} // namespace VeroScript

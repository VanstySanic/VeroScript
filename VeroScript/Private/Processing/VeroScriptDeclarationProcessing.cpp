// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptProcessingSession.h"
#include "../VeroScriptAstTraversal.h"
#include "VeroScriptAst.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptSourceMetadataIO.h"
#include "VeroScriptDeclarationRangeIndex.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptPath.h"
#include "Parser/VeroScriptParserBindings.h"
#include "Parser/VeroScriptParserLookup.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <unordered_map>

namespace VeroScript
{
	namespace
	{
		void MapGeneratedDocument(FSourceDocument& Document, const FCompileGenerationOperation& Operation,
		    std::size_t Prefix)
		{
			auto Coarse = Operation.Origin;
			if (Coarse.GenerationId.empty())
			{
				Coarse.GenerationId = Operation.GenerationId;
			}
			std::size_t Previous = 0;
			const auto Boundary = [&](std::size_t Offset)
			{
				return Offset <= Operation.Source.size() &&
				    (Offset == Operation.Source.size() ||
				        (static_cast<unsigned char>(Operation.Source[Offset]) & 0xc0) != 0x80);
			};
			if (Prefix)
			{
				Document.Mappings.push_back({0, Prefix, Coarse});
			}
			for (const auto& Mapping : Operation.SourceMappings)
			{
				if (Mapping.Begin < Previous || Mapping.Begin >= Mapping.End ||
				    Mapping.Origin.Range.Begin > Mapping.Origin.Range.End || !Boundary(Mapping.Begin) ||
				    !Boundary(Mapping.End) ||
				    (Mapping.Origin.GenerationId.empty() &&
				        Mapping.End - Mapping.Begin != Mapping.Origin.Range.End - Mapping.Origin.Range.Begin))
				{
					throw std::runtime_error("invalid generated source mapping");
				}
				if (Mapping.Begin > Previous)
				{
					Document.Mappings.push_back({Prefix + Previous, Prefix + Mapping.Begin, Coarse});
				}
				Document.Mappings.push_back({Prefix + Mapping.Begin, Prefix + Mapping.End, Mapping.Origin});
				Previous = Mapping.End;
			}
			if (Prefix + Previous < Document.Text.size())
			{
				Document.Mappings.push_back({Prefix + Previous, Document.Text.size(), Coarse});
			}
			Document.LineOffsets = {0};
			for (std::size_t I = 0; I < Document.Text.size(); ++I)
			{
				if (Document.Text[I] == '\n')
				{
					Document.LineOffsets.push_back(I + 1);
				}
			}
		}

		struct FDecl
		{
			FAnnotationDeclarationView View;
			FDeclMeta* Meta = nullptr;
			void* Object = nullptr;
			std::unique_ptr<FBlockStmt>* Body = nullptr;
			std::vector<FFunctionParam>* Parameters = nullptr;
		};
		std::string Namespace(const std::string& S)
		{
			const auto P = S.rfind('.');
			return P == std::string::npos ? "" : S.substr(0, P);
		}
		std::string Leaf(const std::string& S)
		{
			const auto P = S.rfind('.');
			return P == std::string::npos ? S : S.substr(P + 1);
		}
		std::string CPPNamespace(std::string S)
		{
			std::string R;
			for (char C : S)
			{
				R += C == '.' ? "::" : std::string(1, C);
			}
			return R;
		}
		// Keep byte-position lookup private to one collection. Retained package
		// line tables can describe the producer while Text holds a declaration facade.
		class FSourceOffsetIndex
		{
		public:
			std::size_t GetOffset(const FSourceDocument& Document, const FSourceLocation& Location)
			{
				auto [Entry, bInserted] = Lines.try_emplace(&Document);
				auto& Offsets = Entry->second;
				if (bInserted)
				{
					Offsets.push_back(0);
					for (std::size_t Index = 0; Index < Document.Text.size(); ++Index)
					{
						if ((Index & 4095u) == 0u)
						{
							CheckAnalysisCancellation();
						}
						if (Document.Text[Index] == '\n')
						{
							Offsets.push_back(Index + 1u);
						}
					}
				}
				if (Location.Line > Offsets.size()) return Document.Text.size();
				const auto Begin = Location.Line ? Offsets[Location.Line - 1u] : 0u;
				const auto Column = static_cast<std::size_t>(Location.Column ? Location.Column - 1u : 0u);
				return Begin + std::min(Document.Text.size() - Begin, Column);
			}

		private:
			std::unordered_map<const FSourceDocument*, std::vector<std::size_t>> Lines;
		};
		FSourceRange RangeAt(const FProgram& P, FSourceLocation L, FSourceOffsetIndex& Offsets)
		{
			FSourceRange R;
			R.SourceId = std::string(L.FileLabel);
			R.Position.Begin = R.Position.End = {R.SourceId, L.Line, L.Column};
			if (P.SourceMetadata)
			{
				if (const auto* D = P.SourceMetadata->FindDocument(R.SourceId))
				{
					R.Begin = Offsets.GetOffset(*D, L);
					R.End = R.Begin;
				}
			}

			return R;
		}
		std::string StableId(FDeclMeta& M, const std::string& Module, const std::string& Name,
		    EAnnotationTarget K, FSourceLocation L)
		{
			if (M.StableId.empty())
			{
				M.StableId = "source:" + Module + ":" + std::to_string(static_cast<unsigned>(K)) + ":" +
				    std::string(L.FileLabel) + ":" + std::to_string(L.Line) + ":" + std::to_string(L.Column) +
				    ":" + Name;
			}

			return M.StableId;
		}
		std::vector<FDecl> Collect(FProgram& P, bool IncludeBodySource = false)
		{
			FSourceOffsetIndex Offsets;
			std::vector<FDecl> R;
			auto Add = [&](FDeclMeta& M, const std::string& Module, const std::string& Name,
			               EAnnotationTarget Kind, FSourceLocation L, void* Object,
			               const std::string& Owner = "") -> FDecl&
			{
				if (M.SourceRange.SourceId.empty())
				{
					M.SourceRange = RangeAt(P, L, Offsets);
				}

				FDecl D;
				D.Meta = &M;
				D.Object = Object;
				D.View.Id = StableId(M, Module, Name, Kind, L);
				D.View.OwnerId = Owner;
				D.View.Name = Name;
				D.View.ModulePath = Module;
				D.View.Target = Kind;
				D.View.Metadata = M;
				D.View.Location = {std::string(L.FileLabel), L.Line, L.Column};
				D.View.bGenerated = D.View.Id.rfind("generated:", 0) == 0;
				D.View.bGenerated = D.View.bGenerated || L.FileLabel.rfind("veroscript-generated:", 0) == 0;
				R.push_back(std::move(D));
				return R.back();
			};
			auto Params = [&](FDecl& D, std::vector<FFunctionParam>& Ps)
			{
				D.Parameters = &Ps;
				for (std::size_t I = 0; I < Ps.size(); ++I)
				{
					auto& Q = Ps[I];
					if (Q.DeclMeta.StableId.empty())
					{
						Q.DeclMeta.StableId = D.View.Id + ":parameter:" + std::to_string(I);
					}

					FAnnotationParameterView V;
					V.Id = Q.DeclMeta.StableId;
					V.Name = Q.Name;
					V.TypeName = Q.TypeName;
					V.bIsRef = Q.bIsRef;
					V.bIsConst = Q.bIsConst;
					V.bIsVariadic = Q.bIsVariadic;
					V.bHasDefault = bool(Q.DefaultValue);
					V.DefaultValueSource = Q.DefaultValueSource;
					V.Metadata = Q.DeclMeta;
					D.View.Parameters.push_back(std::move(V));
				}
			};
			const auto Templates = [](const auto& Declaration, const FTemplateParameterList& Owner = FTemplateParameterList{})
			{
				std::vector<FAnnotationTemplateParameterView> Result;
				for (const auto& Parameter : Owner.GetParameters())
				{
					Result.push_back({Parameter.Id, Parameter.Name, Parameter.ValueTypeName,
					    Parameter.Kind == ETemplateParameterKind::Type ? Parameter.DefaultTypeName : Parameter.DefaultSource,
					    Parameter.Kind == ETemplateParameterKind::Value, Parameter.HasDefault(), true});
				}
				std::size_t Index = 0;
				for (const auto& Parameter : Declaration.TemplateParameters.GetParameters())
				{
					Result.push_back({Parameter.Id, Parameter.Name, Parameter.ValueTypeName,
					    Parameter.Kind == ETemplateParameterKind::Type ? Parameter.DefaultTypeName : Parameter.DefaultSource,
					    Parameter.Kind == ETemplateParameterKind::Value, Parameter.HasDefault(),
					    Index++ < Declaration.TemplateParameters.GetEnclosingCount()});
				}
				return Result;
			};
			auto Types = [&](auto& Ds, EAnnotationTarget Kind)
			{
				for (auto& T : Ds)
				{
					auto& TD = Add(T.DeclMeta, T.ModulePath, T.Name, Kind, T.Location, &T);
					TD.View.bExported = T.bExported;
					TD.View.bFromPackage = T.bPrecompiledPackage;
					TD.View.bFinal = T.bFinal;
					TD.View.TypeParameters = T.TypeParams;
					TD.View.TemplateParameters = Templates(T);
					TD.View.TypeParameterDefaults = T.TypeParamDefaults;
					for (const auto& B : T.Bounds)
					{
						TD.View.TypeBounds.push_back({B.TypeParam, B.InterfaceTypeName, {}});
					}

					const auto Owner = TD.View.Id;
					using TType = std::decay_t<decltype(T)>;
					if constexpr (std::is_same_v<TType, FClassDecl>)
					{
						TD.View.BaseTypes = T.BaseTypeNames;
						TD.View.bFinal = T.bFinal;
						TD.View.bIsStatic = T.bIsStatic;
					}
					else if constexpr (std::is_same_v<TType, FStructDecl>)
					{
						TD.View.bIsStatic = T.bIsStatic;
						if (!T.BaseTypeName.empty())
						{
							TD.View.BaseTypes.push_back(T.BaseTypeName);
							TD.View.BaseAccess.push_back(T.BaseInheritanceAccess);
						}
					}
					else
					{
						TD.View.BaseTypes = T.BaseInterfaceNames;
					}

					if constexpr (!std::is_same_v<TType, FInterfaceDecl>)
					{
						TD.View.BaseTypes.insert(TD.View.BaseTypes.end(), T.InterfaceTypeNames.begin(),
						    T.InterfaceTypeNames.end());
					}

					if constexpr (std::is_same_v<TType, FClassDecl>)
					{
						TD.View.BaseAccess = T.BaseInheritanceAccess;
					}

					TD.View.BaseAccess.resize(TD.View.BaseTypes.size(), EMemberAccess::Public);
					for (auto& F : T.Fields)
					{
						auto& D = Add(F.DeclMeta, T.ModulePath, F.Name,
						    F.bIsStatic ? EAnnotationTarget::StaticField : EAnnotationTarget::Field,
						    F.Location, &F, Owner);
						D.View.TypeName = F.TypeName;
						D.View.bIsConst = F.bIsConst;
						D.View.bConstexpr = F.bConstexpr;
						D.View.bIsStatic = F.bIsStatic;
						D.View.bIsMutable = F.bIsMutable;
						D.View.Access = F.Access;
						D.View.bHasDefault = bool(F.DefaultValue);
						D.View.DefaultValueSource = F.DefaultValueSource;
						D.View.bFromPackage = T.bPrecompiledPackage;
					}
					for (auto& M : T.Methods)
					{
						auto& D = Add(M.DeclMeta, T.ModulePath, M.Name,
						    M.bIsStatic ? EAnnotationTarget::StaticMethod : EAnnotationTarget::Method,
						    M.Location, &M, Owner);
						D.Body = &M.Body;
						D.View.TypeName = M.ReturnType;
						D.View.bHasBody = bool(M.Body);
						D.View.Access = M.Access;
						D.View.bVirtual = M.bVirtual;
						D.View.bOverride = M.bOverride;
						D.View.bFinal = M.bFinal;
						D.View.bIsConst = M.bIsConst;
						D.View.bConstexpr = M.bConstexpr;
						D.View.bIsStatic = M.bIsStatic;
						D.View.bDeleted = M.bDeleted;
						D.View.bDefaulted = M.bDefaulted;
						D.View.bPureVirtual = M.bPureVirtual;
						D.View.TypeParameters = M.TypeParams;
						D.View.TypeParameterDefaults = M.TypeParamDefaults;
						D.View.TemplateParameters = Templates(M, T.TemplateParameters);
						D.View.bFromPackage = T.bPrecompiledPackage;
						Params(D, M.Parameters);
					}
					if constexpr (!std::is_same_v<TType, FInterfaceDecl>)
					{
						for (auto& C : T.Constructors)
						{
							auto& D = Add(C.DeclMeta, T.ModulePath, Leaf(T.Name),
							    EAnnotationTarget::Constructor, C.Location, &C, Owner);
							D.Body = &C.Body;
							D.View.bHasBody = bool(C.Body);
							D.View.bDeleted = C.bDeleted;
							D.View.bExplicit = C.bExplicit;
							D.View.Access = C.Access;
							D.View.bDefaulted = C.bDefaulted;
							D.View.bFromPackage = T.bPrecompiledPackage;
							Params(D, C.Parameters);
						}
						if (T.Destructor)
						{
							auto& M = *T.Destructor;
							auto& D = Add(M.DeclMeta, T.ModulePath, "~" + Leaf(T.Name),
							    EAnnotationTarget::Destructor, M.Location, &M, Owner);
							D.Body = &M.Body;
							D.View.Access = M.Access;
							D.View.bHasBody = bool(M.Body);
							D.View.bFromPackage = T.bPrecompiledPackage;
							D.View.bVirtual = M.bVirtual;
							D.View.bPureVirtual = M.bPureVirtual;
							D.View.bDefaulted = M.bDefaulted;
						}
					}
				}
			};
			Types(P.Classes, EAnnotationTarget::Class);
			Types(P.Structs, EAnnotationTarget::Struct);
			Types(P.Interfaces, EAnnotationTarget::Interface);
			for (auto& F : P.Functions)
			{
				auto& D = Add(F.DeclMeta, F.ModulePath, F.Name, EAnnotationTarget::Function, F.Location, &F);
				D.Body = &F.Body;
				D.View.TypeName = F.ReturnType;
				D.View.bHasBody = bool(F.Body);
				D.View.bExported = F.bExported;
				D.View.TypeParameters = F.TypeParams;
				D.View.TemplateParameters = Templates(F);
				D.View.TypeParameterDefaults = F.TypeParamDefaults;
				D.View.bFromPackage = F.bPrecompiledPackage;
				for (const auto& B : F.Bounds)
				{
					D.View.TypeBounds.push_back({B.TypeParam, B.InterfaceTypeName, {}});
				}

				D.View.bDeleted = F.bDeleted;
				D.View.bConstexpr = F.bConstexpr;
				Params(D, F.Parameters);
			}
			for (auto& E : P.Enums)
			{
				auto& ED = Add(E.DeclMeta, E.ModulePath, E.Name, EAnnotationTarget::Enum, E.Location, &E);
				ED.View.bExported = E.bExported;
				ED.View.bFromPackage = E.bPrecompiledPackage;
				const auto Owner = ED.View.Id;
				for (auto& M : E.Members)
				{
					Add(M.DeclMeta, E.ModulePath, M.Name, EAnnotationTarget::EnumMember, M.Location, &M,
					    Owner);
				}
			}
			for (auto& A : P.TypeAliases)
			{
				auto& D = Add(A.DeclMeta, A.ModulePath, A.Name, EAnnotationTarget::TypeAlias, A.Location, &A);
				D.View.TypeName = A.TypeName;
				D.View.bExported = A.bExported;
				D.View.bFromPackage = A.bPrecompiledPackage;
			}
			for (auto& N : P.Namespaces)
			{
				Add(N.DeclMeta, N.ModulePath, N.Name, EAnnotationTarget::Namespace, N.Location, &N);
			}

			auto Global = [&](FVarDeclStmt& G)
			{
				auto& D =
				    Add(G.DeclMeta, G.ModulePath, G.Name, EAnnotationTarget::GlobalVariable, G.Location, &G);
				D.View.TypeName = G.TypeName;
				D.View.bIsConst = G.bIsConst;
				D.View.bConstexpr = G.bConstexpr;
				D.View.bIsRef = G.bIsRef;
				D.View.bExported = G.bExported;
				D.View.bFromPackage = G.bPrecompiledPackage;
				D.View.bHasDefault = bool(G.Initializer);
				D.View.DefaultValueSource = G.InitializerSource;
			};
			for (auto& S : P.TopLevelStatements)
			{
				if (S)
				{
					if (S->Kind == EStmtKind::VarDecl)
					{
						Global(static_cast<FVarDeclStmt&>(*S));
					}

					else if (S->Kind == EStmtKind::VarDeclGroup)
					{
						for (auto& G : static_cast<FVarDeclGroupStmt&>(*S).Declarations)
						{
							Global(*G);
						}
					}
				}
			}

			// Parameters are independently addressable without changing their signature.
			const auto Count = R.size();
			for (std::size_t I = 0; I < Count; ++I)
			{
				if (R[I].Parameters)
				{
					const auto Owner = R[I].View.Id, Module = R[I].View.ModulePath;
					for (auto& Q : *R[I].Parameters)
					{
						FDecl D;
						D.Meta = &Q.DeclMeta;
						D.Object = &Q;
						D.View.Id = Q.DeclMeta.StableId;
						D.View.OwnerId = Owner;
						D.View.Name = Q.Name;
						D.View.ModulePath = Module;
						D.View.TypeName = Q.TypeName;
						D.View.Target = EAnnotationTarget::Parameter;
						D.View.Metadata = Q.DeclMeta;
						D.View.Location = Q.DeclMeta.SourceRange.Position.Begin;
						if (D.View.Location.FileLabel.empty())
						{
							D.View.Location = R[I].View.Location;
						}

						D.View.bIsConst = Q.bIsConst;
						D.View.bIsRef = Q.bIsRef;
						D.View.bHasDefault = bool(Q.DefaultValue);
						D.View.DefaultValueSource = Q.DefaultValueSource;
						R.push_back(std::move(D));
					}
				}
			}

			for (auto& D : R)
			{
				if (IncludeBodySource && D.Body && *D.Body)
				{
					D.View.BodyModulePath = (*D.Body)->DefinitionModulePath.empty()
					    ? D.View.ModulePath
					    : (*D.Body)->DefinitionModulePath;
					if ((*D.Body)->DeferredSyntax)
					{
						D.View.BodySource = (*D.Body)->DeferredSyntax->Source;
					}

					else if (P.SourceMetadata)
					{
						if (const auto* Doc = P.SourceMetadata->FindDocument((*D.Body)->SourceRange.SourceId))
						{
							const auto& Range = (*D.Body)->SourceRange;
							if (Range.Begin <= Range.End && Range.End <= Doc->Text.size())
							{
								D.View.BodySource = Doc->Text.substr(Range.Begin, Range.End - Range.Begin);
							}
						}
					}
				}
				for (const auto& M : P.PackageSourceModules)
				{
					if (M.bFromPackage && M.ModulePath == D.View.ModulePath)
					{
						D.View.bFromPackage = true;
					}
				}
			}
			std::unordered_map<std::string, const FTypeDeclarationScope*> Scopes;
			std::unordered_map<std::string, std::string> TypeOwners;
			const auto IndexScopes = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					Scopes.emplace(Declaration.DeclMeta.StableId, &Declaration.Scope);
					TypeOwners.emplace(Declaration.ModulePath + '\x1F' + Declaration.Name, Declaration.DeclMeta.StableId);
				}
			};
			IndexScopes(P.Classes);
			IndexScopes(P.Structs);
			IndexScopes(P.Interfaces);
			IndexScopes(P.Enums);
			IndexScopes(P.TypeAliases);
			std::unordered_map<std::string, std::string> FunctionOwners;
			for (const auto& Declaration : R)
			{
				if (Declaration.Body && *Declaration.Body && !(*Declaration.Body)->CallableOwner.empty())
				{
					const auto& Body = **Declaration.Body;
					FunctionOwners.emplace((Body.DefinitionModulePath.empty() ? Declaration.View.ModulePath
					                                                          : Body.DefinitionModulePath) +
					        '\x1F' + Body.CallableOwner,
					    Declaration.View.Id);
				}
			}
			for (auto& Declaration : R)
			{
				const auto Found = Scopes.find(Declaration.View.Id);
				if (Found == Scopes.end())
				{
					continue;
				}
				const auto& Scope = *Found->second;
				Declaration.View.ScopeId = Scope.ScopeId;
				Declaration.View.ParentScopeId = Scope.ParentScopeId;
				Declaration.View.bNested = !Scope.OwnerTypeName.empty();
				Declaration.View.bLocal = Scope.bLocal;
				Declaration.View.Access = Scope.Access;
				const auto Owner = TypeOwners.find(Scope.ModulePath + '\x1F' + Scope.OwnerTypeName);
				if (!Scope.OwnerTypeName.empty() && Owner != TypeOwners.end())
				{
					Declaration.View.OwnerId = Owner->second;
				}
				else if (const auto Function = FunctionOwners.find(Scope.ModulePath + '\x1F' + Scope.OwnerFunctionName);
				    Function != FunctionOwners.end())
				{
					Declaration.View.OwnerId = Function->second;
				}
			}
			return R;
		}
		std::string Digest(const FDecl& D)
		{
			std::string R;
			auto Add = [&](std::string_view S)
			{
				R += std::to_string(S.size()) + ":";
				R.append(S);
			};
			const auto& V = D.View;
			const auto ScopeIdentity = [&](std::string Value)
			{
				if (!V.ModulePath.empty())
				{
					for (std::size_t Index = 0; (Index = Value.find(V.ModulePath, Index)) != std::string::npos;)
					{
						Value.replace(Index, V.ModulePath.size(), "$module");
						Index += 7;
					}
				}
				return Value;
			};
			Add(V.Name);
			Add(ScopeIdentity(V.ScopeId));
			Add(ScopeIdentity(V.ParentScopeId));
			Add(ScopeIdentity(V.OwnerId));
			Add(V.bNested ? "1" : "0");
			Add(V.bLocal ? "1" : "0");
			Add(V.TypeName);
			Add(std::to_string(static_cast<unsigned>(V.Target)));
			Add(std::to_string(static_cast<unsigned>(V.Access)));
			Add(V.bHasBody ? "1" : "0");
			for (bool Flag :
			    {V.bExported, V.bVirtual, V.bOverride, V.bFinal, V.bIsConst, V.bIsStatic, V.bDeleted,
			        V.bPureVirtual, V.bHasDefault, V.bIsRef, V.bIsMutable, V.bExplicit, V.bDefaulted})
			{
				Add(Flag ? "1" : "0");
			}

			Add(V.DefaultValueSource);
			Add(V.BodySource);
			Add(std::to_string(V.TypeParameters.size()));
			for (const auto& T : V.TypeParameters)
			{
				Add(T);
			}

			Add(std::to_string(V.TypeParameterDefaults.size()));
			for (const auto& T : V.TypeParameterDefaults)
			{
				Add(T);
			}

			Add(std::to_string(V.BaseTypes.size()));
			for (const auto& T : V.BaseTypes)
			{
				Add(T);
			}

			for (const auto& B : V.TypeBounds)
			{
				Add(B.TypeParameter);
				Add(B.InterfaceTypeName);
			}
			for (const auto& P : V.Parameters)
			{
				Add(P.Name);
				Add(P.TypeName);
				Add(P.DefaultValueSource);
				for (bool Flag : {P.bIsConst, P.bIsRef, P.bIsVariadic, P.bHasDefault})
				{
					Add(Flag ? "1" : "0");
				}
			}
			std::function<void(const std::vector<FAnnotationArgument>&)> Args = [&](const auto& Values)
			{
				Add(std::to_string(Values.size()));
				for (const auto& A : Values)
				{
					Add(A.Name);
					Add(A.RawText);
					Add(A.Value);
					Add(std::to_string(static_cast<unsigned>(A.Kind)));
					Add(std::to_string(static_cast<unsigned>(A.ScalarKind)));
					Args(A.Children);
				}
			};
			for (const auto& A : D.Meta->Annotations)
			{
				Add(A.Name);
				Add(A.RawText);
				Args(A.Arguments);
			}
			for (const auto& C : D.Meta->Comments)
			{
				Add(C.RawText);
				Add(C.Text);
				Add(std::to_string(static_cast<unsigned>(C.Kind)));
			}
			Add(D.Meta->Documentation);
			return R;
		}
		std::string Digest(const FSourceNodeMetadata& N)
		{
			std::string R;
			auto Add = [&](std::string_view S)
			{
				R += std::to_string(S.size()) + ":";
				R.append(S);
			};
			Add(N.Kind);
			Add(N.Kind == "module" ? std::string_view{} : std::string_view(N.Name));
			Add(N.TypeName);
			Add(std::to_string(N.Target));
			Add(std::to_string(N.Range.Begin));
			Add(std::to_string(N.Range.End));
			std::function<void(const std::vector<FAnnotationArgument>&)> Args = [&](const auto& Values)
			{
				Add(std::to_string(Values.size()));
				for (const auto& A : Values)
				{
					Add(A.Name);
					Add(A.Value);
					Add(A.RawText);
					Add(std::to_string(static_cast<unsigned>(A.Kind)));
					Add(std::to_string(static_cast<unsigned>(A.ScalarKind)));
					Args(A.Children);
				}
			};
			Add(std::to_string(N.Annotations.size()));
			for (const auto& A : N.Annotations)
			{
				Add(A.Name);
				Add(A.RawText);
				Args(A.Arguments);
			}
			Add(std::to_string(N.Comments.size()));
			for (const auto& C : N.Comments)
			{
				Add(C.RawText);
				Add(C.Text);
				Add(std::to_string(static_cast<unsigned>(C.Kind)));
			}
			Add(N.Documentation);
			return R;
		}
		void ApplyMetadata(FDeclMeta& D, const FCompileMetadata& M)
		{
			if (M.Annotations)
			{
				std::set<std::string> OldKeys{"__annot"};
				std::function<void(const std::vector<FAnnotationArgument>&, std::string)> Keys =
				    [&](const auto& Args, std::string Prefix)
				{
					for (const auto& A : Args)
					{
						const auto Key = Prefix.empty() ? A.Name : Prefix + "." + A.Name;
						OldKeys.insert(Key);
						Keys(A.Children, Key);
					}
				};
				for (const auto& A : D.Annotations)
				{
					Keys(A.Arguments, "");
				}

				D.erase(std::remove_if(D.begin(), D.end(),
				            [&](const auto& P)
				{
					return OldKeys.count(P.first) != 0;
				}),
				    D.end());
				D.Annotations = *M.Annotations;
				std::function<void(const std::vector<FAnnotationArgument>&, std::string)> Projection =
				    [&](const auto& Args, std::string Prefix)
				{
					for (const auto& A : Args)
					{
						const auto Key = Prefix.empty() ? A.Name : Prefix + "." + A.Name;
						D.emplace_back(Key, A.Kind == EAnnotationArgumentKind::Group ? "__annot" : A.Value);
						Projection(A.Children, Key);
					}
				};
				for (const auto& A : D.Annotations)
				{
					D.emplace_back("__annot", A.Name);
					Projection(A.Arguments, "");
				}
			}
			if (M.Comments)
			{
				D.Comments = *M.Comments;
				D.Documentation = FormatSourceDocumentation(D.Comments);
			}
		}
		void ApplyReplacementMetadata(
		    FDeclMeta& D, const FDeclMeta& Previous, const FCompileMetadata& Payload)
		{
			auto Effective = Payload;
			if (!Effective.Annotations && D.Annotations.empty())
			{
				Effective.Annotations = Previous.Annotations;
			}

			const bool InheritComments = !Effective.Comments && D.Comments.empty();
			if (InheritComments)
			{
				Effective.Comments = Previous.Comments;
				// The replaced body/type scope no longer owns its old interior comments.
				auto& Cs = *Effective.Comments;
				Cs.erase(std::remove_if(Cs.begin(), Cs.end(),
				             [](const auto& C)
				{
					return C.Association == ECommentAssociation::Body ||
					    C.Association == ECommentAssociation::Scope;
				}),
				    Cs.end());
			}
			D.StableId = Previous.StableId;
			// Rebuild the ordinary metadata projection as well as structured annotations.
			ApplyMetadata(D, Effective);
			if (InheritComments)
			{
				D.Documentation = Previous.Documentation;
			}
		}
		void DiscardReplacedDeclarationChildren(
		    FSourceMetadataSnapshot& Metadata, const std::string& TargetId)
		{
			std::unordered_map<std::string, std::vector<std::string>> Children;
			for (const auto& N : Metadata.Nodes)
			{
				if (!N.ParentId.empty())
				{
					Children[N.ParentId].push_back(N.Id);
				}

				if (!N.OwnerId.empty() && N.OwnerId != N.ParentId)
				{
					Children[N.OwnerId].push_back(N.Id);
				}
			}
			std::unordered_set<std::string> Replaced{TargetId};
			std::vector<std::string> Pending{TargetId};
			for (std::size_t I = 0; I < Pending.size(); ++I)
			{
				if (const auto C = Children.find(Pending[I]); C != Children.end())
				{
					for (const auto& Id : C->second)
					{
						if (Replaced.insert(Id).second)
						{
							Pending.push_back(Id);
						}
					}
				}
			}

			// Keep the stable target and its exterior documentation. Its old
			// descendants must neither reappear from package metadata nor turn
			// their source comments into documentation on the module root.
			for (auto& D : Metadata.Documents)
			{
				for (auto& C : D.Comments)
				{
					if (Replaced.count(C.OwnerId) &&
					    (C.OwnerId != TargetId || C.Association == ECommentAssociation::Body ||
					        C.Association == ECommentAssociation::Scope))
					{
						C.Activity = ESourceActivity::Removed;
					}
				}
			}

			Metadata.Nodes.erase(std::remove_if(Metadata.Nodes.begin(), Metadata.Nodes.end(),
			                         [&](const auto& N)
			{
				return N.Id != TargetId && Replaced.count(N.Id);
			}),
			    Metadata.Nodes.end());
		}
		void CheckFragmentMetadata(const FDeclMeta& Parsed, const FCompileMetadata& Payload)
		{
			if (Payload.Annotations && !Parsed.Annotations.empty())
			{
				const auto& A = Parsed.Annotations;
				const auto& B = *Payload.Annotations;
				bool Equal = A.size() == B.size();
				for (std::size_t I = 0; Equal && I < A.size(); ++I)
				{
					Equal = A[I].Name == B[I].Name && A[I].Arguments == B[I].Arguments;
				}

				if (!Equal)
				{
					throw std::runtime_error(
					    "fragment annotations conflict with the explicit metadata payload");
				}
			}
			if (Payload.Comments && !Parsed.Comments.empty())
			{
				const auto& A = Parsed.Comments;
				const auto& B = *Payload.Comments;
				bool Equal = A.size() == B.size();
				for (std::size_t I = 0; Equal && I < A.size(); ++I)
				{
					Equal = A[I].Kind == B[I].Kind && A[I].RawText == B[I].RawText;
				}

				if (!Equal)
				{
					throw std::runtime_error("fragment comments conflict with the explicit metadata payload");
				}
			}
		}
		void Configure(VeroScriptParser& Parser, const IHostBindingCatalog* B, const FProgram& P)
		{
			std::unordered_set<std::string> Names, Interfaces;
			if (B)
			{
				for (const auto& T : B->GetTypes())
				{
					Names.insert(T.Name);
				}
				for (const auto& T : B->GetTypeAliases())
				{
					Names.insert(T.Name);
				}
				for (const auto& T : B->GetGenericTypeTemplates())
				{
					Names.insert(T.Name);
				}
			}
			Parser.SetHostTypeNames(Names);
			for (const auto& T : P.Classes)
			{
				Names.insert(T.Name);
			}

			for (const auto& T : P.Structs)
			{
				Names.insert(T.Name);
			}

			for (const auto& T : P.Enums)
			{
				Names.insert(T.Name);
			}

			for (const auto& T : P.TypeAliases)
			{
				Names.insert(T.Name);
			}

			for (const auto& T : P.Interfaces)
			{
				Names.insert(T.Name);
				Interfaces.insert(T.Name);
			}
			Parser.SetAdditionalForwardTypeNames(std::move(Names), std::move(Interfaces));
		}
		template <class P>
		void WalkExpr(P& E, const std::function<void(FExpr&)>& F, const std::function<void(FStmt&)>& S,
		    std::unordered_map<const void*, const void*>* Parents = nullptr, const void* Parent = nullptr);
		template <class P>
		void WalkStmt(P& V, const std::function<void(FExpr&)>& F, const std::function<void(FStmt&)>& S,
		    std::unordered_map<const void*, const void*>* Parents = nullptr, const void* Parent = nullptr)
		{
			if (!V) return;
			FStmt& Base = *V;
			if (Parents)
			{
				(*Parents)[&Base] = Parent;
			}
			S(Base);
			switch (V->Kind)
			{
			case EStmtKind::Expr:
				{

					WalkExpr(static_cast<FExprStmt&>(Base).Expression, F, S, Parents, &Base);
					break;
				}
			case EStmtKind::Block:
				{

					for (auto& X : static_cast<FBlockStmt&>(Base).Statements)
					{
						WalkStmt(X, F, S, Parents, &Base);
					}
					break;
				}
			case EStmtKind::VarDecl:
				{

					WalkExpr(static_cast<FVarDeclStmt&>(Base).Initializer, F, S, Parents, &Base);
					break;
				}
			case EStmtKind::VarDeclGroup:
				{

					for (auto& X : static_cast<FVarDeclGroupStmt&>(Base).Declarations)
					{
						WalkStmt(X, F, S, Parents, &Base);
					}
					break;
				}
			case EStmtKind::Return:
				{

					WalkExpr(static_cast<FReturnStmt&>(Base).Value, F, S, Parents, &Base);
					break;
				}
			case EStmtKind::Throw:
				{
					WalkExpr(static_cast<FThrowStmt&>(Base).Value, F, S, Parents, &Base);
					break;
				}
			case EStmtKind::If:
				{
					auto& X = static_cast<FIfStmt&>(Base);
					WalkExpr(X.Condition, F, S, Parents, &Base);
					WalkStmt(X.ThenBranch, F, S, Parents, &Base);
					WalkStmt(X.ElseBranch, F, S, Parents, &Base);
					break;
				}
			case EStmtKind::While:
				{
					auto& X = static_cast<FWhileStmt&>(Base);
					WalkExpr(X.Condition, F, S, Parents, &Base);
					WalkStmt(X.Body, F, S, Parents, &Base);
					break;
				}
			case EStmtKind::DoWhile:
				{
					auto& X = static_cast<FDoWhileStmt&>(Base);
					WalkStmt(X.Body, F, S, Parents, &Base);
					WalkExpr(X.Condition, F, S, Parents, &Base);
					break;
				}
			case EStmtKind::For:
				{
					auto& X = static_cast<FForStmt&>(Base);
					WalkStmt(X.Init, F, S, Parents, &Base);
					WalkExpr(X.Condition, F, S, Parents, &Base);
					WalkExpr(X.Step, F, S, Parents, &Base);
					for (auto& Q : X.InitExpressions)
					{
						WalkExpr(Q, F, S, Parents, &Base);
					}
					for (auto& Q : X.StepExpressions)
					{
						WalkExpr(Q, F, S, Parents, &Base);
					}
					WalkStmt(X.Body, F, S, Parents, &Base);
					break;
				}
			case EStmtKind::ForEach:
				{
					auto& X = static_cast<FForEachStmt&>(Base);
					WalkExpr(X.Collection, F, S, Parents, &Base);
					WalkStmt(X.Body, F, S, Parents, &Base);
					break;
				}
			case EStmtKind::Switch:
				{
					auto& X = static_cast<FSwitchStmt&>(Base);
					WalkExpr(X.Discriminant, F, S, Parents, &Base);
					for (auto& C : X.Cases)
					{
						for (auto& L : C.Labels)
						{
							WalkExpr(L, F, S, Parents, &Base);
						}
						WalkStmt(C.Body, F, S, Parents, &Base);
					}
					break;
				}
			case EStmtKind::Try:
				{
					auto& X = static_cast<FTryStmt&>(Base);
					WalkStmt(X.TryBlock, F, S, Parents, &Base);
					for (auto& C : X.Catches)
					{
						WalkStmt(C.Body, F, S, Parents, &Base);
					}
					WalkStmt(X.FinallyBlock, F, S, Parents, &Base);
					break;
				}
			default:
				{

					break;
				}
			}
		}
		template <class P>
		void WalkExpr(P& E, const std::function<void(FExpr&)>& F, const std::function<void(FStmt&)>& S,
		    std::unordered_map<const void*, const void*>* Parents, const void* Parent)
		{
			if (!E) return;
			if (Parents)
			{
				(*Parents)[E.get()] = Parent;
			}
			F(*E);
			switch (E->Kind)
			{
			case EExprKind::Unary:
				{
					WalkExpr(static_cast<FUnaryExpr&>(*E).Operand, F, S, Parents, E.get());
					break;
				}
			case EExprKind::Binary:
				{
					auto& X = static_cast<FBinaryExpr&>(*E);
					WalkExpr(X.Left, F, S, Parents, E.get());
					WalkExpr(X.Right, F, S, Parents, E.get());
					break;
				}
			case EExprKind::Assign:
				{
					auto& X = static_cast<FAssignExpr&>(*E);
					WalkExpr(X.Target, F, S, Parents, E.get());
					WalkExpr(X.Value, F, S, Parents, E.get());
					break;
				}
			case EExprKind::Conditional:
				{
					auto& X = static_cast<FConditionalExpr&>(*E);
					WalkExpr(X.Condition, F, S, Parents, E.get());
					WalkExpr(X.ThenExpr, F, S, Parents, E.get());
					WalkExpr(X.ElseExpr, F, S, Parents, E.get());
					break;
				}
			case EExprKind::Ref:
				{

					WalkExpr(static_cast<FRefExpr&>(*E).Operand, F, S, Parents, E.get());
					break;
				}
			case EExprKind::Call:
				{
					auto& X = static_cast<FCallExpr&>(*E);
					WalkExpr(X.CalleeExpr, F, S, Parents, E.get());
					for (auto& A : X.Arguments)
					{
						WalkExpr(A, F, S, Parents, E.get());
					}
					break;
				}
			case EExprKind::MemberAccess:
				{

					WalkExpr(static_cast<FMemberAccessExpr&>(*E).Object, F, S, Parents, E.get());
					break;
				}
			case EExprKind::MethodCall:
				{
					auto& X = static_cast<FMethodCallExpr&>(*E);
					WalkExpr(X.Object, F, S, Parents, E.get());
					for (auto& A : X.Arguments)
					{
						WalkExpr(A, F, S, Parents, E.get());
					}
					break;
				}
			case EExprKind::IndexAccess:
				{
					auto& X = static_cast<FIndexAccessExpr&>(*E);
					WalkExpr(X.Object, F, S, Parents, E.get());
					WalkExpr(X.Index, F, S, Parents, E.get());
					break;
				}
			case EExprKind::ArrayInit:
				{

					for (auto& A : static_cast<FArrayInitExpr&>(*E).Elements)
					{
						WalkExpr(A, F, S, Parents, E.get());
					}
					break;
				}
			case EExprKind::MapInit:
				{

					for (auto& A : static_cast<FMapInitExpr&>(*E).Entries)
					{
						WalkExpr(A.Key, F, S, Parents, E.get());
						WalkExpr(A.Value, F, S, Parents, E.get());
					}
					break;
				}
			case EExprKind::Lambda:
				{
					auto& X = static_cast<FLambdaExpr&>(*E);
					for (auto& Capture : X.Captures)
					{
						WalkExpr(Capture.Initializer, F, S, Parents, E.get());
					}
					WalkStmt(X.Body, F, S, Parents, E.get());
					WalkExpr(X.ExpressionBody, F, S, Parents, E.get());
					for (auto& Q : X.Parameters)
					{
						WalkExpr(Q.DefaultValue, F, S, Parents, E.get());
					}
					break;
				}
			default:
				{

					break;
				}
			}
		}
		template <class F, class S>
		void WalkProgram(FProgram& P, const F& Expr, const S& Stmt,
		    std::unordered_map<const void*, const void*>* Parents = nullptr,
		    const std::function<void(const void*, const void*)>& RootOwner = {})
		{
			// Ownership is separate from syntax parenting: declaration roots must not
			// change ParentId for existing source-backed metadata or other walkers.
			const auto Root = [&](const auto& Node, const void* Declaration)
			{
				if (Node && RootOwner)
				{
					RootOwner(Node.get(), Declaration);
				}
			};
			auto Types = [&](auto& Ts)
			{
				for (auto& T : Ts)
				{
					for (auto& X : T.Fields)
					{
						Root(X.DefaultValue, &X);
						WalkExpr(X.DefaultValue, Expr, Stmt, Parents);
					}

					for (auto& X : T.Methods)
					{
						Root(X.Body, &X);
						WalkStmt(X.Body, Expr, Stmt, Parents);
						for (auto& Q : X.Parameters)
						{
							Root(Q.DefaultValue, &Q);
							WalkExpr(Q.DefaultValue, Expr, Stmt, Parents);
						}
					}
					using TT = std::decay_t<decltype(T)>;
					if constexpr (!std::is_same_v<TT, FInterfaceDecl>)
					{
						for (auto& X : T.Constructors)
						{
							Root(X.Body, &X);
							WalkStmt(X.Body, Expr, Stmt, Parents);
							for (auto& Q : X.Parameters)
							{
								Root(Q.DefaultValue, &Q);
								WalkExpr(Q.DefaultValue, Expr, Stmt, Parents);
							}
							for (auto& B : X.BaseInits)
							{
								for (auto& A : B.Args)
								{
									Root(A, &X);
									WalkExpr(A, Expr, Stmt, Parents);
								}
							}
							for (auto& B : X.FieldInits)
							{
								for (auto& A : B.Args)
								{
									Root(A, &X);
									WalkExpr(A, Expr, Stmt, Parents);
								}
							}
						}
						if (T.Destructor)
						{
							Root(T.Destructor->Body, T.Destructor.get());
							WalkStmt(T.Destructor->Body, Expr, Stmt, Parents);
						}
					}
					if constexpr (std::is_same_v<TT, FClassDecl>)
					{
						Root(T.ClassDefaults, &T);
						WalkStmt(T.ClassDefaults, Expr, Stmt, Parents);
					}
				}
			};
			Types(P.Classes);
			Types(P.Structs);
			Types(P.Interfaces);
			for (auto& Enum : P.Enums)
			{
				for (auto& Member : Enum.Members)
				{
					Root(Member.Initializer, &Member);
					WalkExpr(Member.Initializer, Expr, Stmt, Parents);
				}
			}
			for (auto& Function : P.Functions)
			{
				Root(Function.Body, &Function);
				WalkStmt(Function.Body, Expr, Stmt, Parents);
				for (auto& Q : Function.Parameters)
				{
					Root(Q.DefaultValue, &Q);
					WalkExpr(Q.DefaultValue, Expr, Stmt, Parents);
				}
			}
			for (auto& Statement : P.TopLevelStatements)
			{
				WalkStmt(Statement, Expr, Stmt, Parents);
			}
		}
		bool ApplyFragmentMetadata(FProgram& P)
		{
			auto Session = P.CompileProcessingSession;
			if (!Session || Session->PendingFragmentMetadata.empty()) return false;
			auto Decls = Collect(P);
			for (const auto& Pending : Session->PendingFragmentMetadata)
			{
				for (const auto& Child : Pending.Metadata.Children)
				{
					if (Child.Begin > Child.End)
					{
						throw std::runtime_error("invalid fragment metadata range");
					}

					FDeclMeta* Target = nullptr;
					std::size_t Matches = 0;
					auto Match = [&](FDeclMeta& M, const FSourceRange& R, const std::string& Kind)
					{
						if (R.SourceId == Pending.SourceId && R.Begin == Pending.Offset + Child.Begin &&
						    R.End == Pending.Offset + Child.End && Kind == Child.Kind)
						{
							Target = &M;
							++Matches;
						}
					};
					for (auto& D : Decls)
					{
						Match(*D.Meta, D.Meta->SourceRange, "declaration");
					}

					WalkProgram(
					    P,
					    [&](FExpr& E)
					{
						Match(E.DeclMeta, E.SourceRange,
						    "expression:" + std::to_string(static_cast<unsigned>(E.Kind)));
					},
					    [&](FStmt& S)
					{
						Match(S.DeclMeta, S.SourceRange,
						    "statement:" + std::to_string(static_cast<unsigned>(S.Kind)));
					});
					if (Matches != 1)
					{
						throw std::runtime_error("fragment metadata must select exactly one parsed node: " +
						    Child.Kind + " at " + std::to_string(Pending.Offset + Child.Begin) + ":" +
						    std::to_string(Pending.Offset + Child.End) + " (matches " +
						    std::to_string(Matches) + ")");
					}

					FCompileMetadata M;
					M.Annotations = Child.Metadata.Annotations;
					M.Comments = Child.Metadata.Comments;
					ApplyMetadata(*Target, M);
					if (M.Comments)
					{
						Session->ExplicitCommentTargets.insert(Target->StableId);
					}

					if (!Child.Metadata.Documentation.empty())
					{
						Target->Documentation = Child.Metadata.Documentation;
					}
				}
			}

			Session->PendingFragmentMetadata.clear();
			return true;
		}
	}
	std::vector<FAnnotationDeclarationView> GetCompileDeclarationViews(FProgram& P)
	{
		std::vector<FAnnotationDeclarationView> R;
		for (auto& D : Collect(P, true))
		{
			R.push_back(std::move(D.View));
		}
		return R;
	}

	std::vector<FCompileBlockView> GetCompileBlockViews(const FProgram& Program)
	{
		std::vector<FCompileBlockView> Result;
		std::unordered_map<std::string_view, const FSourceNodeMetadata*> Nodes;
		if (Program.SourceMetadata)
		{
			Nodes.reserve(Program.SourceMetadata->Nodes.size());
			for (const auto& Node : Program.SourceMetadata->Nodes)
			{
				Nodes.emplace(Node.Id, &Node);
			}
		}
		VisitProgramNodes(Program, [](const FExpr&)
		{
		}, [&](const FStmt& Statement)
		{
			if (Statement.Kind != EStmtKind::Block || Statement.ScopeId.empty() || Statement.SourceRange.SourceId.empty() || !Statement.DeclarationContext)
			{
				return;
			}
			const auto& Block = static_cast<const FBlockStmt&>(Statement);
			FCompileBlockView View;
			View.ModuleId = Block.BodyContext ? Block.BodyContext->ModulePath : Block.DeclarationContext->ModulePath;
			View.ScopeId = Block.ScopeId;
			View.ParentScopeId = Block.ParentScopeId;
			View.EndAnchorId = Block.DeclMeta.StableId;
			if (const auto Node = Nodes.find(Block.DeclMeta.StableId); Node != Nodes.end())
			{
				View.OwnerId = Node->second->OwnerId;
			}
			for (const auto& Child : Block.Statements)
			{
				if (Child && !Child->DeclMeta.StableId.empty())
				{
					View.Statements.push_back({Child->DeclMeta.StableId, Child->SourceRange});
				}
			}
			Result.push_back(std::move(View));
		});
		return Result;
	}

	namespace
	{
		std::unique_ptr<FProgram> ParseDeclaration(FProgram& P, const FCompileGenerationOperation& O,
		    const FAnnotationDeclarationView* Owner, const IHostBindingCatalog* B,
		    std::vector<FScriptError>& Diagnostics, const FDeferredSyntax* BlockContext = nullptr,
		    std::unique_ptr<FStmt>* LocalStatement = nullptr)
		{
			const FTypeDeclarationScope* OwnerScope = nullptr;
			std::shared_ptr<const FDeferredSyntax> OwnerContext;
			if (Owner)
			{
				const auto Find = [&](const auto& Declarations)
				{
					for (const auto& Declaration : Declarations)
					{
						if (Declaration.DeclMeta.StableId == Owner->Id && Declaration.DeclarationContext)
						{
							OwnerScope = &Declaration.Scope;
							OwnerContext = Declaration.DeclarationContext;
							return;
						}
					}
				};
				Find(P.Classes);
				Find(P.Structs);
				Find(P.Interfaces);
			}
			auto Text = std::make_shared<std::string>();
			if (!OwnerContext && !BlockContext && !O.Scope.NamespacePath.empty())
			{
				*Text += "namespace " + CPPNamespace(O.Scope.NamespacePath) + " {\n";
			}

			if (Owner)
			{
				if (!Owner->TemplateParameters.empty())
				{
					bool First = true;
					for (const auto& Parameter : Owner->TemplateParameters)
					{
						if (Parameter.bEnclosing)
						{
							continue;
						}
						*Text += First ? "template<" : ",";
						*Text += Parameter.bValue ? StripTypeScopeMarkers(Parameter.ValueTypeName) + " " + Parameter.Name : "typename " + Parameter.Name;
						First = false;
					}
					if (!First)
					{
						*Text += "> ";
					}
				}
				else
				{
					const auto Captured = OwnerScope ? OwnerScope->EnclosingTypeParameters.size() : 0;
					if (Owner->TypeParameters.size() > Captured)
					{
						*Text += "template<";
						for (std::size_t I = Captured; I < Owner->TypeParameters.size(); ++I)
						{
							if (I != Captured)
							{
								*Text += ",";
							}
							*Text += Owner->TypeParameters[I];
						}
						*Text += "> ";
					}
				}
				if (Owner->bIsStatic)
				{
					*Text += "static ";
				}

				*Text += (Owner->Target == EAnnotationTarget::Struct
				        ? "struct "
				        : (Owner->Target == EAnnotationTarget::Interface ? "interface " : "class "));
				*Text += Leaf(Owner->Name) + " {\n";
			}
			const auto FragmentOffset = Text->size();
			*Text += O.Source;
			if (Owner)
			{
				*Text += "\n}";
			}

			if (!OwnerContext && !BlockContext && !O.Scope.NamespacePath.empty())
			{
				*Text += "\n}";
			}

			auto Label = std::make_shared<std::string>("veroscript-generated:" + O.GenerationId);
			P.LifetimeAnchors.push_back(Text);
			P.LifetimeAnchors.push_back(Label);
			P.GeneratedDocuments.push_back({*Label, *Text, O.GenerationId, O.Origin.Range.Position.Begin,
			    O.Origin.Range.Position.Begin});
			if (P.CompileProcessingSession)
			{
				FSourceDocument D;
				D.Id = *Label;
				D.ModuleId = O.Scope.ModuleId;
				D.Text = *Text;
				D.Comments = CollectSourceComments(*Text, *Label, ESourceActivity::Active);
				MapGeneratedDocument(D, O, FragmentOffset);
				P.CompileProcessingSession->Metadata->Documents.push_back(std::move(D));
			}
			VeroScriptLexer Lexer(*Text, *Label);
			auto Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				FScriptError E;
				E.Message = Lexer.GetErrorMessage();
				Diagnostics.push_back(std::move(E));
				return nullptr;
			}
			for (const auto& T : Tokens)
			{
				if (T.Lexeme == "#" || (T.Kind == ETokenKind::Keyword && T.Lexeme == "import"))
				{
					FScriptError E;
					E.Message = "declaration fragments cannot contain imports or preprocessor directives";
					Diagnostics.push_back(std::move(E));
					return nullptr;
				}
			}

			VeroScriptParser Parser(Tokens, O.Scope.ModuleId, &P);
			Configure(Parser, B, P);
			Parser.SetDeclarationOutline(true, *Text);
			auto Part = BlockContext ? Parser.ParseLocalTypeFragment(*BlockContext, *LocalStatement)
			    : OwnerContext       ? Parser.ParseTypeFragment(*OwnerScope, *OwnerContext)
			                         : Parser.ParseProgram();
			if (!Part || Parser.HasError())
			{
				auto Error = Parser.GetError();
				if (P.CompileProcessingSession)
				{
					MapGeneratedDiagnostic(*P.CompileProcessingSession->Metadata, Error);
				}
				Diagnostics.push_back(std::move(Error));
				return nullptr;
			}
			if (!Part->Imports.empty() || (!OwnerContext && !BlockContext && !Part->UsingNamespaces.empty()) || !Part->SourceDefinitions.empty())
			{
				FScriptError E;
				E.Message =
				    "declaration fragment contains unsupported scope directives or supplemental definitions";
				Diagnostics.push_back(std::move(E));
				return nullptr;
			}
			if (OwnerContext || BlockContext)
			{
				Part->UsingNamespaces.clear();
			}
			if (P.CompileProcessingSession && !O.Metadata.Children.empty())
			{
				P.CompileProcessingSession->PendingFragmentMetadata.push_back(
				    {*Label, FragmentOffset, O.Metadata});
			}

			return Part;
		}
		template <class T>
		void AddOrReplace(
		    std::vector<T>& Into, std::vector<T>& From, FDecl* Target, const FCompileGenerationOperation& O)
		{
			if (From.empty())
			{
				throw std::runtime_error("expected exactly one declaration");
			}

			const auto Kind = []
			{
				if constexpr (std::is_same_v<T, FClassDecl>)
				{
					return EAnnotationTarget::Class;
				}

				else if constexpr (std::is_same_v<T, FStructDecl>)
				{
					return EAnnotationTarget::Struct;
				}

				else if constexpr (std::is_same_v<T, FInterfaceDecl>)
				{
					return EAnnotationTarget::Interface;
				}

				else if constexpr (std::is_same_v<T, FEnumDecl>)
				{
					return EAnnotationTarget::Enum;
				}

				else if constexpr (std::is_same_v<T, FTypeAliasDecl>)
				{
					return EAnnotationTarget::TypeAlias;
				}

				else
				{
					return EAnnotationTarget::Function;
				}
			}();
			if (Target && Target->View.Target != Kind)
			{
				throw std::runtime_error("declaration replacement cannot change target kind");
			}

			auto V = std::move(From.front());
			From.erase(From.begin());
			CheckFragmentMetadata(V.DeclMeta, O.Metadata);
			if (Target)
			{
				if (V.Name != Target->View.Name)
				{
					throw std::runtime_error("declaration replacement cannot rename its target");
				}

				if constexpr (std::is_same_v<T, FClassDecl> || std::is_same_v<T, FStructDecl> ||
				    std::is_same_v<T, FInterfaceDecl> || std::is_same_v<T, FFunctionDecl>)
				{
					if (V.TypeParams.size() != Target->View.TypeParameters.size())
					{
						throw std::runtime_error("declaration replacement cannot change generic arity");
					}
				}

				ApplyReplacementMetadata(V.DeclMeta, *Target->Meta, O.Metadata);
				*static_cast<T*>(Target->Object) = std::move(V);
			}
			else
			{
				V.DeclMeta.StableId = "generated:" + O.GenerationId;
				ApplyMetadata(V.DeclMeta, O.Metadata);
				Into.push_back(std::move(V));
			}
		}
		template <class T>
		bool ApplyMember(T& Owner, FProgram& Part, FDecl* Target, const FCompileGenerationOperation& O)
		{
			const auto CheckDescendants = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					if (Declaration.Name != Owner.Name && !Declaration.Scope.bLocal)
					{
						throw std::runtime_error("nested type declarations require the Types phase");
					}
				}
			};
			CheckDescendants(Part.Classes);
			CheckDescendants(Part.Structs);
			CheckDescendants(Part.Interfaces);
			CheckDescendants(Part.Enums);
			CheckDescendants(Part.TypeAliases);
			auto Move = [&](auto& Temp)
			{
				std::size_t N = Temp.Fields.size() + Temp.Methods.size();
				using TT = std::decay_t<decltype(Temp)>;
				if constexpr (!std::is_same_v<TT, FInterfaceDecl>)
				{
					N += Temp.Constructors.size() + (Temp.Destructor ? 1 : 0);
				}

				if (N != 1)
				{
					throw std::runtime_error(
					    "member fragment must contain exactly one field, callable, or operator");
				}

				auto Update = [&](auto& Into, auto& From, EAnnotationTarget Kind)
				{
					if (From.empty()) return false;
					if (Target && Target->View.Target != Kind &&
					    !(Kind == EAnnotationTarget::Method &&
					        Target->View.Target == EAnnotationTarget::StaticMethod) &&
					    !(Kind == EAnnotationTarget::Field &&
					        Target->View.Target == EAnnotationTarget::StaticField))
					{
						throw std::runtime_error("declaration replacement cannot change target kind");
					}

					auto V = std::move(From.front());
					CheckFragmentMetadata(V.DeclMeta, O.Metadata);
					if (Target)
					{
						if constexpr (!std::is_same_v<std::decay_t<decltype(V)>, FConstructorDecl>)
						{
							if (V.Name != Target->View.Name)
							{
								throw std::runtime_error("member replacement cannot rename its target");
							}
						}

						ApplyReplacementMetadata(V.DeclMeta, *Target->Meta, O.Metadata);
						*static_cast<std::decay_t<decltype(V)>*>(Target->Object) = std::move(V);
					}
					else
					{
						V.DeclMeta.StableId = "generated:" + O.GenerationId;
						ApplyMetadata(V.DeclMeta, O.Metadata);
						Into.push_back(std::move(V));
					}
					return true;
				};
				if (Update(Owner.Fields, Temp.Fields, EAnnotationTarget::Field)) return true;
				if (Update(Owner.Methods, Temp.Methods, EAnnotationTarget::Method)) return true;
				using OT = std::decay_t<decltype(Owner)>;
				if constexpr (!std::is_same_v<OT, FInterfaceDecl>)
				{
					if constexpr (!std::is_same_v<TT, FInterfaceDecl>)
					{
						if (Update(Owner.Constructors, Temp.Constructors, EAnnotationTarget::Constructor))
						{
							return true;
						}
						if (Temp.Destructor)
						{
							if (Target && Target->View.Target != EAnnotationTarget::Destructor)
							{
								throw std::runtime_error("member replacement cannot change target kind");
							}

							if (!Target && Owner.Destructor)
							{
								throw std::runtime_error("duplicate destructor");
							}

							CheckFragmentMetadata(Temp.Destructor->DeclMeta, O.Metadata);
							if (Target)
							{
								ApplyReplacementMetadata(
								    Temp.Destructor->DeclMeta, *Target->Meta, O.Metadata);
							}

							else
							{
								Temp.Destructor->DeclMeta.StableId = "generated:" + O.GenerationId;
								ApplyMetadata(Temp.Destructor->DeclMeta, O.Metadata);
							}
							Owner.Destructor = std::move(Temp.Destructor);
							return true;
						}
					}
				}
				return false;
			};
			if constexpr (std::is_same_v<T, FClassDecl>)
			{
				const auto I = std::find_if(Part.Classes.begin(), Part.Classes.end(), [&](const auto& D)
				{
					return D.Name == Owner.Name;
				});
				if (I == Part.Classes.end())
				{
					throw std::runtime_error("missing member fragment owner");
				}
				const bool Result = Move(*I);
				Part.Classes.erase(I);
				return Result;
			}

			else if constexpr (std::is_same_v<T, FStructDecl>)
			{
				const auto I = std::find_if(Part.Structs.begin(), Part.Structs.end(), [&](const auto& D)
				{
					return D.Name == Owner.Name;
				});
				if (I == Part.Structs.end())
				{
					throw std::runtime_error("missing member fragment owner");
				}
				const bool Result = Move(*I);
				Part.Structs.erase(I);
				return Result;
			}

			else
			{
				const auto I = std::find_if(Part.Interfaces.begin(), Part.Interfaces.end(), [&](const auto& D)
				{
					return D.Name == Owner.Name;
				});
				if (I == Part.Interfaces.end())
				{
					throw std::runtime_error("missing member fragment owner");
				}
				const bool Result = Move(*I);
				Part.Interfaces.erase(I);
				return Result;
			}
		}

		void MergeFragmentTypes(FProgram& Program, FProgram& Fragment)
		{
			const auto Mark = [&](auto& Declarations)
			{
				for (auto& Declaration : Declarations)
				{
					if (std::any_of(Program.PackageSourceModules.begin(), Program.PackageSourceModules.end(), [&](const auto& Module)
					{
						return Module.bFromPackage && Module.ModulePath == Declaration.ModulePath;
					}))
					{
						using T = std::decay_t<decltype(Declaration)>;
						if constexpr (std::is_same_v<T, FEnumDecl> || std::is_same_v<T, FTypeAliasDecl>)
						{
							Declaration.bPrecompiledPackage = true;
						}
						else
						{
							Declaration.bPrecompiledPackage = Declaration.TypeParams.empty();
						}
					}
				}
			};
			Mark(Fragment.Classes);
			Mark(Fragment.Structs);
			Mark(Fragment.Interfaces);
			Mark(Fragment.Enums);
			Mark(Fragment.TypeAliases);
			const auto Move = [](auto& Into, auto& From)
			{
				Into.insert(Into.end(), std::make_move_iterator(From.begin()), std::make_move_iterator(From.end()));
				From.clear();
			};
			Move(Program.Classes, Fragment.Classes);
			Move(Program.Structs, Fragment.Structs);
			Move(Program.Interfaces, Fragment.Interfaces);
			Move(Program.Enums, Fragment.Enums);
			Move(Program.TypeAliases, Fragment.TypeAliases);
			Move(Program.ScopedTypeUses, Fragment.ScopedTypeUses);
		}

		// Separate the source fragment root from its flat-owned descendants.
		std::size_t SelectFragmentRoots(FProgram& Part, const std::string& Owner)
		{
			std::size_t Count = Part.Functions.size() + Part.TopLevelStatements.size();
			const auto Select = [&](auto& Declarations)
			{
				std::stable_partition(Declarations.begin(), Declarations.end(), [&](const auto& D)
				{
					const bool Root = D.Scope.OwnerTypeName == Owner && (!Owner.empty() || !D.Scope.bLocal);
					Count += Root ? 1 : 0;
					return Root;
				});
			};
			Select(Part.Classes);
			Select(Part.Structs);
			Select(Part.Interfaces);
			Select(Part.Enums);
			Select(Part.TypeAliases);
			return Count;
		}
		template <class T>
		bool HasFragmentRoot(const std::vector<T>& Declarations, const std::string& Owner)
		{
			return !Declarations.empty() && Declarations.front().Scope.OwnerTypeName == Owner && (!Owner.empty() || !Declarations.front().Scope.bLocal);
		}

		void DiscardScopedDeclarations(FProgram& Program, const FSourceRange& Range)
		{
			if (Range.SourceId.empty() || Range.Begin >= Range.End) return;
			std::vector<std::string> GeneratedRoots;
			const auto Within = [&](const FSourceRange& R)
			{
				return R.SourceId == Range.SourceId && R.Begin >= Range.Begin && R.End <= Range.End;
			};
			for (const auto& Binding : Program.GeneratedBlockTypes)
			{
				if (Within(Binding.Anchor) && Binding.TypeName.rfind("$local", 0) == 0)
				{
					GeneratedRoots.push_back(Binding.TypeName);
				}
			}
			const auto IsGeneratedChild = [&](const std::string& Name)
			{
				return std::any_of(GeneratedRoots.begin(), GeneratedRoots.end(), [&](const auto& Root)
				{
					return Name == Root || Name.rfind(Root + ".", 0) == 0;
				});
			};
			const auto Remove = [&](auto& Declarations)
			{
				Declarations.erase(std::remove_if(Declarations.begin(), Declarations.end(), [&](const auto& D)
				{
					const auto& R = D.DeclMeta.SourceRange;
					return Within(R) || IsGeneratedChild(D.Name);
				}),
				    Declarations.end());
			};
			Remove(Program.Classes);
			Remove(Program.Structs);
			Remove(Program.Interfaces);
			Remove(Program.Enums);
			Remove(Program.TypeAliases);
			const auto Position = [](const auto& P)
			{
				return std::make_pair(P.Line, P.Column);
			};
			Program.ScopedTypeUses.erase(std::remove_if(Program.ScopedTypeUses.begin(), Program.ScopedTypeUses.end(), [&](const auto& Use)
			{
				return (Use.Location.FileLabel == Range.SourceId && Position(Use.Location) >= Position(Range.Position.Begin) && Position(Use.Location) < Position(Range.Position.End)) || IsGeneratedChild(Use.ContextType);
			}),
			    Program.ScopedTypeUses.end());
			Program.GeneratedBlockTypes.erase(std::remove_if(Program.GeneratedBlockTypes.begin(), Program.GeneratedBlockTypes.end(),
			                                      [&](const auto& Binding)
			{
				return Within(Binding.Anchor);
			}),
			    Program.GeneratedBlockTypes.end());
		}
	}
	bool ProcessCompileDeclarations(FProgram& P, ECompileProcessingPhase Phase,
	    const IHostBindingCatalog* Bindings, std::vector<FScriptError>& Diagnostics)
	{
		auto Session = P.CompileProcessingSession;
		if (!Session) return true;
		try
		{
			const auto& Registry = Session->Options.Registry;
			const bool NeedsOriginalDeclarations = Phase == ECompileProcessingPhase::Types && Registry && (Registry->HasPhase(ECompileProcessingPhase::Types) || Registry->HasPhase(ECompileProcessingPhase::Members) || Registry->HasPhase(ECompileProcessingPhase::Bodies));
			const bool HasReplay = std::any_of(Session->ReplayOperations.begin(), Session->ReplayOperations.end(),
			    [Phase](const auto& Operation)
			{
				return Operation.Phase == Phase;
			});
			// Empty stages have no observer of the declaration snapshot. Keep the
			// cancellation/snapshot contract, and collect final metadata after binding.
			if (!NeedsOriginalDeclarations && !(Registry && Registry->HasPhase(Phase)) && !HasReplay)
			{
				Session->Check();
				Session->Program = &P;
				++Session->Snapshot;
				if (Session->HasErrors())
				{
					Diagnostics.insert(Diagnostics.end(), Session->Diagnostics.begin(), Session->Diagnostics.end());
					return false;
				}
				return true;
			}
			CollectProgramSourceMetadata(P);
			auto Refs = Collect(P, true);
			std::vector<FAnnotationDeclarationView> Views;
			for (const auto& D : Refs)
			{
				Views.push_back(D.View);
			}

			if (Session->BindDeclarationViews)
			{
				Session->BindDeclarationViews(Views);
			}

			Session->Program = &P;
			auto Ops = Session->Dispatch(Phase, Views);
			const auto LiveCount = Ops.size();
			for (const auto& O : Session->ReplayOperations)
			{
				if (O.Phase == Phase)
				{
					Ops.push_back(O);
				}
			}

			if (Session->HasErrors())
			{
				Diagnostics.insert(
				    Diagnostics.end(), Session->Diagnostics.begin(), Session->Diagnostics.end());
				return false;
			}
			std::unordered_map<std::string, FBlockStmt*> BlockIndex;
			if (std::any_of(Ops.begin(), Ops.end(), [](const auto& O)
			{
				return !O.Scope.ScopeId.empty();
			}))
			{
				VisitProgramNodes(P, [](FExpr&)
				{
				}, [&](FStmt& Statement)
				{
					if (Statement.Kind == EStmtKind::Block && !Statement.ScopeId.empty() && !Statement.SourceRange.SourceId.empty())
					{
						if (!BlockIndex.emplace(Statement.ScopeId, &static_cast<FBlockStmt&>(Statement)).second)
						{
							throw std::runtime_error("ambiguous block scope identity");
						}
					}
				});
			}
			struct FBlockInsertion
			{
				FSourceRange Anchor;
				FDeferredSyntax Context;
				const FSourceNodeMetadata* Node = nullptr;
				bool AtEnd = false;
			};
			const auto ResolveInsertion = [&](const FCompileGenerationOperation& Operation)
			{
				const auto Found = BlockIndex.find(Operation.Scope.ScopeId);
				if (Found == BlockIndex.end() || Operation.Scope.InsertionBeforeId.empty() || !Operation.Scope.OwnerId.empty() || !Operation.Scope.NamespacePath.empty())
				{
					throw std::runtime_error("block additions require a valid scope and statement boundary without another owner");
				}
				const auto& Block = *Found->second;
				FBlockInsertion Insertion;
				const FDeferredSyntax* Context = nullptr;
				if (Operation.Scope.InsertionBeforeId == Block.DeclMeta.StableId)
				{
					Insertion.AtEnd = true;
					Context = Block.EndContext.get();
					Insertion.Anchor = Block.SourceRange;
					Insertion.Anchor.Begin = Insertion.Anchor.End - 1;
					Insertion.Anchor.End = Insertion.Anchor.Begin;
					Insertion.Anchor.Position.Begin = Insertion.Anchor.Position.End;
					--Insertion.Anchor.Position.Begin.Column;
					Insertion.Anchor.Position.End = Insertion.Anchor.Position.Begin;
				}
				else
				{
					for (const auto& Statement : Block.Statements)
					{
						if (Statement->DeclMeta.StableId == Operation.Scope.InsertionBeforeId)
						{
							Context = Statement->DeclarationContext.get();
							Insertion.Anchor = Statement->SourceRange;
							break;
						}
					}
				}
				if (!Context || Context->ModulePath != Operation.Scope.ModuleId || Phase != ECompileProcessingPhase::Types)
				{
					throw std::runtime_error("local declarations require a current source block anchor in Types");
				}
				Insertion.Context = *Context;
				Insertion.Context.ScopeId = Block.ScopeId;
				Insertion.Context.Range = Insertion.Anchor;
				Insertion.Node = Session->Metadata->FindNode(Operation.Scope.InsertionBeforeId);
				if (!Insertion.Node)
				{
					throw std::runtime_error("unknown block insertion anchor");
				}
				return Insertion;
			};
			std::set<std::string> Mutations;
			for (std::size_t OpIndex = 0; OpIndex < Ops.size(); ++OpIndex)
			{
				auto& O = Ops[OpIndex];
				const bool Replay = OpIndex >= LiveCount;
				if (O.Kind == ECompileOperation::AddDeclaration)
				{
					if (!O.TargetId.empty())
					{
						throw std::runtime_error("declaration addition cannot carry a replacement target");
					}
					if (!O.Scope.ScopeId.empty())
					{
						const auto Insertion = ResolveInsertion(O);
						if (!Replay && std::any_of(P.PackageSourceModules.begin(), P.PackageSourceModules.end(), [&](const auto& Module)
						{
							return Module.bFromPackage && Module.ModulePath == O.Scope.ModuleId;
						}))
						{
							throw std::runtime_error("package block scopes are read-only");
						}
						const auto Input = Digest(*Insertion.Node);
						if (Replay && O.InputDigest != Input)
						{
							throw std::runtime_error("package block anchor input does not match its target");
						}
						if (!Replay)
						{
							O.InputDigest = Input;
						}
						continue;
					}

					if (!O.Scope.OwnerId.empty())
					{
						auto I = std::find_if(Refs.begin(), Refs.end(),
						    [&](const auto& D)
						{
							return D.View.Id == O.Scope.OwnerId;
						});
						if (I == Refs.end() || (I->View.bFromPackage && !Replay) ||
						    I->View.ModulePath != O.Scope.ModuleId)
						{
							throw std::runtime_error("invalid declaration owner");
						}

						if (Phase != ECompileProcessingPhase::Members && Phase != ECompileProcessingPhase::Types)
						{
							throw std::runtime_error("type declarations require Types and ordinary members require Members");
						}

						if (Replay && O.InputDigest != Digest(*I))
						{
							throw std::runtime_error("package member owner input does not match its target");
						}

						if (!Replay)
						{
							O.InputDigest = Digest(*I);
						}
					}
					continue;
				}
				auto I = std::find_if(Refs.begin(), Refs.end(),
				    [&](const auto& D)
				{
					return D.View.Id == O.TargetId;
				});
				if (I == Refs.end() && O.Kind == ECompileOperation::SetMetadata)
				{
					const auto* N = Session->Metadata->FindNode(O.TargetId);
					if (!N)
					{
						throw std::runtime_error("unknown syntax metadata target");
					}

					if (!Replay &&
					    std::any_of(P.PackageSourceModules.begin(), P.PackageSourceModules.end(),
					        [&](const auto& M)
					{
						return M.bFromPackage && M.ModulePath == N->ModuleId;
					}))
					{
						throw std::runtime_error("package syntax metadata is read-only");
					}

					if (N->ModuleId != O.Scope.ModuleId &&
					    std::find(Session->Options.WritableModules.begin(),
					        Session->Options.WritableModules.end(),
					        N->ModuleId) == Session->Options.WritableModules.end())
					{
						throw std::runtime_error("syntax metadata target is outside writable modules");
					}

					if (N->Kind != "module" && Phase != ECompileProcessingPhase::Bodies)
					{
						throw std::runtime_error("syntax metadata requires Bodies");
					}

					if (!Mutations.insert(O.TargetId).second)
					{
						throw std::runtime_error("conflicting metadata operations");
					}

					if (Replay && O.InputDigest != Digest(*N))
					{
						throw std::runtime_error("package syntax metadata input does not match its target");
					}

					if (!Replay)
					{
						O.InputDigest = Digest(*N);
					}

					continue;
				}
				if (I == Refs.end() || (I->View.bFromPackage && !Replay))
				{
					throw std::runtime_error("target does not belong to writable source");
				}

				if (I->View.ModulePath != O.Scope.ModuleId &&
				    std::find(Session->Options.WritableModules.begin(),
				        Session->Options.WritableModules.end(),
				        I->View.ModulePath) == Session->Options.WritableModules.end())
				{
					throw std::runtime_error("target is outside writable modules");
				}

				if (!Mutations.insert(O.TargetId).second)
				{
					throw std::runtime_error("conflicting compile operations on the same target");
				}

				if (Replay && O.InputDigest != Digest(*I))
				{
					throw std::runtime_error("package processing input does not match its target");
				}

				if (!Replay)
				{
					O.InputDigest = Digest(*I);
				}

				if (O.Kind == ECompileOperation::CompleteBody || O.Kind == ECompileOperation::ReplaceBody)
				{
					if (!I->Body || I->View.bDeleted ||
					    (I->View.bPureVirtual && I->View.Target != EAnnotationTarget::Destructor))
					{
						throw std::runtime_error("body operation requires a concrete callable");
					}

					if ((O.Kind == ECompileOperation::CompleteBody) == bool(*I->Body))
					{
						throw std::runtime_error("body operation conflicts with the existing body");
					}
				}
			}
			for (const auto& O : Ops)
			{
				for (const auto& D : Refs)
				{
					if (D.View.OwnerId == O.TargetId && Mutations.count(D.View.Id) &&
					    O.Kind == ECompileOperation::ReplaceDeclaration)
					{
						throw std::runtime_error("parent replacement conflicts with child operations");
					}
				}

				if (O.Kind == ECompileOperation::ReplaceBody)
				{
					for (const auto& N : Session->Metadata->Nodes)
					{
						if (N.Kind != "declaration" && N.OwnerId == O.TargetId && Mutations.count(N.Id))
						{
							throw std::runtime_error(
							    "body replacement conflicts with metadata on old syntax");
						}
					}
				}

				if (O.Kind == ECompileOperation::ReplaceDeclaration)
				{
					for (const auto& A : Ops)
					{
						if (!A.Scope.ScopeId.empty())
						{
							const auto Insertion = ResolveInsertion(A);
							const auto Target = std::find_if(Refs.begin(), Refs.end(), [&](const auto& D)
							{
								return D.View.Id == O.TargetId;
							});
							if (Target != Refs.end())
							{
								const auto& R = Target->Meta->SourceRange;
								if (Insertion.Anchor.SourceId == R.SourceId && Insertion.Anchor.Begin >= R.Begin && Insertion.Anchor.End <= R.End)
								{
									throw std::runtime_error("declaration replacement conflicts with a descendant block insertion");
								}
							}
						}
						if (A.Scope.OwnerId == O.TargetId)
						{
							throw std::runtime_error("type replacement conflicts with member additions");
						}
					}
				}
			}
			// A declaration-append batch preserves existing type owners.
			// Borrow the validated owner snapshot instead of rebuilding every growing member list.
			// A fragment may still append local types; storage changes require fresh references.
			const bool DeclarationAppendsOnly = Phase == ECompileProcessingPhase::Members &&
			    std::all_of(Ops.begin(), Ops.end(), [&](const auto& O)
			{
				return O.Kind == ECompileOperation::AddDeclaration && O.TargetId.empty() &&
				    O.Scope.ScopeId.empty() && (O.Scope.OwnerId.empty() || std::any_of(Refs.begin(), Refs.end(), [&](const auto& D)
				{
					return D.View.Id == O.Scope.OwnerId && (D.View.Target == EAnnotationTarget::Class || D.View.Target == EAnnotationTarget::Struct || D.View.Target == EAnnotationTarget::Interface);
				}));
			});
			const auto* ClassStorage = P.Classes.data();
			const auto* StructStorage = P.Structs.data();
			const auto* InterfaceStorage = P.Interfaces.data();
			for (auto O : Ops)
			{
				Session->Check();
				const bool Replay =
				    std::any_of(Session->ReplayOperations.begin(), Session->ReplayOperations.end(),
				        [&](const auto& R)
				{
					return R.GenerationId == O.GenerationId;
				});
				if (O.Metadata.Comments)
				{
					Session->ExplicitCommentTargets.insert(
					    O.TargetId.empty() ? "generated:" + O.GenerationId : O.TargetId);
				}

				const bool ReuseOwners = DeclarationAppendsOnly && P.Classes.data() == ClassStorage &&
				    P.Structs.data() == StructStorage && P.Interfaces.data() == InterfaceStorage;
				auto Current = ReuseOwners ? std::vector<FDecl>{} : Collect(P);
				auto& CurrentDeclarations = ReuseOwners ? Refs : Current;
				auto Find = [&](const std::string& Id) -> FDecl*
				{
					for (auto& D : CurrentDeclarations)
					{
						if (D.View.Id == Id) return &D;
					}
					return nullptr;
				};
				auto* Target = ReuseOwners ? nullptr : Find(O.TargetId);
				if (O.Kind == ECompileOperation::AddDeclaration && !O.Scope.ScopeId.empty())
				{
					const auto Insertion = ResolveInsertion(O);
					std::unique_ptr<FStmt> Statement;
					auto Part = ParseDeclaration(P, O, nullptr, Bindings, Diagnostics, &Insertion.Context, &Statement);
					if (!Part) return false;
					auto& Alias = static_cast<FTypeAliasStmt&>(*Statement);
					const auto Mark = [&](auto& Declarations)
					{
						for (auto& Declaration : Declarations)
						{
							if (Declaration.Name == Alias.TypeName)
							{
								CheckFragmentMetadata(Declaration.DeclMeta, O.Metadata);
								Declaration.DeclMeta.StableId = "generated:" + O.GenerationId;
								ApplyMetadata(Declaration.DeclMeta, O.Metadata);
								Alias.DeclMeta = Declaration.DeclMeta;
							}
						}
					};
					Mark(Part->Classes);
					Mark(Part->Structs);
					Mark(Part->Interfaces);
					Mark(Part->Enums);
					Alias.DeclMeta.SourceRange = Statement->SourceRange;
					Alias.DeclMeta.StableId = "generated-statement:" + O.GenerationId;
					P.GeneratedBlockTypes.push_back({O.Scope.ScopeId, O.Scope.InsertionBeforeId, Alias.Name, Alias.TypeName,
					    Insertion.Anchor, Alias.DeclMeta, Insertion.AtEnd});
					MergeFragmentTypes(P, *Part);
				}
				else if (O.Kind == ECompileOperation::SetMetadata)
				{
					if (Target)
					{
						ApplyMetadata(*Target->Meta, O.Metadata);
					}

					else if (const auto* N = Session->Metadata->FindNode(O.TargetId);
					    N && N->Kind == "module")
					{
						auto Entry = Session->ModuleMetadata.try_emplace(N->ModuleId);
						// Package roots have no live document reconstruction during replay.
						if (Entry.second &&
						    std::any_of(P.PackageSourceModules.begin(), P.PackageSourceModules.end(),
						        [&](const auto& Module)
						{
							return Module.bFromPackage && Module.ModulePath == N->ModuleId;
						}))
						{
							FCompileMetadata Initial;
							Initial.Annotations = N->Annotations;
							Initial.Comments = N->Comments;
							ApplyMetadata(Entry.first->second, Initial);
							Entry.first->second.Documentation = N->Documentation;
						}
						ApplyMetadata(Entry.first->second, O.Metadata);
					}
					else
					{
						bool Found = false;
						WalkProgram(
						    P,
						    [&](FExpr& E)
						{
							if (E.DeclMeta.StableId == O.TargetId)
							{
								ApplyMetadata(E.DeclMeta, O.Metadata);
								Found = true;
							}
						},
						    [&](FStmt& S)
						{
							if (S.DeclMeta.StableId == O.TargetId)
							{
								ApplyMetadata(S.DeclMeta, O.Metadata);
								Found = true;
							}
						});
						if (!Found)
						{
							throw std::runtime_error("syntax metadata target became invalid");
						}
					}
				}
				else if (O.Kind == ECompileOperation::CompleteBody ||
				    O.Kind == ECompileOperation::ReplaceBody)
				{
					auto Text = std::make_shared<std::string>(O.Source);
					auto Label = std::make_shared<std::string>("veroscript-generated:" + O.GenerationId);
					P.LifetimeAnchors.push_back(Text);
					P.LifetimeAnchors.push_back(Label);
					P.GeneratedDocuments.push_back({*Label, *Text, O.GenerationId,
					    O.Origin.Range.Position.Begin, O.Origin.Range.Position.Begin});
					VeroScriptLexer Lexer(*Text, *Label);
					auto Tokens = Lexer.Tokenize();
					VeroScriptParser Parser(Tokens, Target->View.ModulePath, &P);
					Configure(Parser, Bindings, P);
					Parser.SetSourceText(*Text);
					FSourceDocument Doc;
					Doc.Id = *Label;
					Doc.ModuleId = Target->View.ModulePath;
					Doc.Text = *Text;
					Doc.Comments = CollectSourceComments(*Text, *Label, ESourceActivity::Active);
					MapGeneratedDocument(Doc, O, 0);
					Session->Metadata->Documents.push_back(std::move(Doc));
					auto* Owner = Find(Target->View.OwnerId);
					FDeferredSyntax Context;
					const auto RestoreOwner = [&](const auto& Declarations)
					{
						for (const auto& Declaration : Declarations)
						{
							if (Owner && Declaration.DeclMeta.StableId == Owner->View.Id)
							{
								if (Declaration.DeclarationContext)
								{
									Context = *Declaration.DeclarationContext;
								}
								Context.TypeOwner = Declaration.Name;
								Context.TypeScope = std::make_shared<FTypeDeclarationScope>(Declaration.Scope);
								Context.TypeAliasBoundaries.push_back(Context.Bindings ? Context.Bindings->Aliases.size() : 0);
							}
						}
					};
					RestoreOwner(P.Classes);
					RestoreOwner(P.Structs);
					RestoreOwner(P.Interfaces);
					Context.Namespace = Owner ? Owner->View.Name : Namespace(Target->View.Name);
					Context.ModulePath = Target->View.ModulePath;
					Context.TypeParameters = Owner ? Owner->View.TypeParameters : Target->View.TypeParameters;
					Context.FunctionTypeParameters = Context.TypeParameters;
					const std::vector<FFunctionParam> EmptyParameters;
					Context.FunctionOwner = CallableScopeName(Context.Namespace, Target->View.Name,
					    Target->Parameters ? *Target->Parameters : EmptyParameters, Target->View.bIsConst);
					Context.ParentScopeId = TypeScopeIdentity(Context.ModulePath, "function", Context.FunctionOwner);
					Context.ScopeId = TypeScopeIdentity(Context.ParentScopeId, "block", "generated");
					Parser.RestoreDeclarationContext(Context, true);
					Parser.SetAllowInternalBuiltins(Context.bAllowInternalBuiltins);
					auto Body = Parser.ParseBodyFragment(Context.Namespace, Context.TypeParameters, Target->Parameters);
					auto Fragment = Parser.TakeFragmentDeclarations();
					if (Lexer.HasError() || !Body || Parser.HasError())
					{
						auto E = Parser.GetError();
						if (Lexer.HasError())
						{
							E.Message = Lexer.GetErrorMessage();
							E.Location = FSourceLocation{*Label, 1, 1};
						}
						MapGeneratedDiagnostic(*Session->Metadata, E);
						Diagnostics.push_back(std::move(E));
						return false;
					}
					const auto PreviousBody = *Target->Body ? (*Target->Body)->SourceRange : FSourceRange{};
					if (*Target->Body)
					{
						const auto& PreviousRange = (*Target->Body)->SourceRange;
						const auto InPreviousBody = [&](const FSourceRange& R)
						{
							return R.SourceId == PreviousRange.SourceId && R.Begin >= PreviousRange.Begin &&
							    R.End <= PreviousRange.End;
						};
						// Package bootstrap nodes are normally retained. Invalidated body
						// nodes must be removed before that retention can restore them.
						auto& Nodes = Session->Metadata->Nodes;
						Nodes.erase(std::remove_if(Nodes.begin(), Nodes.end(),
						                [&](const auto& N)
						{
							return InPreviousBody(N.Range) ||
							    (!N.Origin.GenerationId.empty() && InPreviousBody(N.Origin.Range));
						}),
						    Nodes.end());
						for (auto& D : Session->Metadata->Documents)
						{
							for (auto& C : D.Comments)
							{
								if (C.OwnerId == Target->View.Id &&
								    C.Association == ECommentAssociation::Body && InPreviousBody(C.Range))
								{
									C.Activity = ESourceActivity::Removed;
								}
							}
						}
					}
					*Target->Body = std::move(Body);
					// Keep declaration documentation, but discard comments owned by
					// the old body before the generated document is collected.
					auto& Comments = Target->Meta->Comments;
					Comments.erase(std::remove_if(Comments.begin(), Comments.end(),
					                   [](const auto& C)
					{
						return C.Association == ECommentAssociation::Body;
					}),
					    Comments.end());
					ApplyMetadata(*Target->Meta, O.Metadata);
					if (!O.Metadata.Children.empty())
					{
						Session->PendingFragmentMetadata.push_back({*Label, 0, O.Metadata});
					}
					DiscardScopedDeclarations(P, PreviousBody);
					if (Fragment)
					{
						MergeFragmentTypes(P, *Fragment);
					}
				}
				else
				{
					auto* Owner = Find(O.Scope.OwnerId);
					if (Target && !Target->View.OwnerId.empty())
					{
						Owner = Find(Target->View.OwnerId);
					}

					if (Owner)
					{
						O.Scope.NamespacePath = Namespace(Owner->View.Name);
					}

					else if (Target)
					{
						O.Scope.NamespacePath = Namespace(Target->View.Name);
					}

					auto Part = ParseDeclaration(P, O, Owner ? &Owner->View : nullptr, Bindings, Diagnostics);
					if (!Part) return false;
					const auto ReplacedRange = Target ? Target->Meta->SourceRange : FSourceRange{};
					if (Target)
					{
						DiscardReplacedDeclarationChildren(*Session->Metadata, Target->View.Id);
					}

					if (Replay && !Owner)
					{
						auto Mark = [&](auto& Ds)
						{
							for (auto& D : Ds)
							{
								D.bPrecompiledPackage = D.TypeParams.empty();
							}
						};
						Mark(Part->Classes);
						Mark(Part->Structs);
						Mark(Part->Interfaces);
						Mark(Part->Functions);
						for (auto& E : Part->Enums)
						{
							E.bPrecompiledPackage = true;
						}

						for (auto& A : Part->TypeAliases)
						{
							A.bPrecompiledPackage = true;
						}

						for (auto& G : Part->TopLevelStatements)
						{
							if (G->Kind == EStmtKind::VarDecl)
							{
								static_cast<FVarDeclStmt&>(*G).bPrecompiledPackage = true;
							}
						}
					}
					if (Owner && Phase == ECompileProcessingPhase::Types)
					{
						const auto RemoveWrapper = [&](auto& Declarations)
						{
							const auto I = std::find_if(Declarations.begin(), Declarations.end(), [&](const auto& D)
							{
								return D.Name == Owner->View.Name;
							});
							if (I != Declarations.end())
							{
								std::size_t Members = I->Fields.size() + I->Methods.size();
								using T = std::decay_t<decltype(*I)>;
								if constexpr (!std::is_same_v<T, FInterfaceDecl>)
								{
									Members += I->Constructors.size() + (I->Destructor ? 1u : 0u);
								}
								if (Members != 0)
								{
									throw std::runtime_error("Types fragments cannot add ordinary members");
								}
								Declarations.erase(I);
							}
						};
						RemoveWrapper(Part->Classes);
						RemoveWrapper(Part->Structs);
						RemoveWrapper(Part->Interfaces);
						if (SelectFragmentRoots(*Part, Owner->View.Name) != 1)
						{
							throw std::runtime_error("nested type fragment must contain exactly one declaration");
						}
						if (HasFragmentRoot(Part->Classes, Owner->View.Name))
						{
							AddOrReplace(P.Classes, Part->Classes, Target, O);
						}
						else if (HasFragmentRoot(Part->Structs, Owner->View.Name))
						{
							AddOrReplace(P.Structs, Part->Structs, Target, O);
						}
						else if (HasFragmentRoot(Part->Interfaces, Owner->View.Name))
						{
							AddOrReplace(P.Interfaces, Part->Interfaces, Target, O);
						}
						else if (HasFragmentRoot(Part->Enums, Owner->View.Name))
						{
							AddOrReplace(P.Enums, Part->Enums, Target, O);
						}
						else if (HasFragmentRoot(Part->TypeAliases, Owner->View.Name))
						{
							AddOrReplace(P.TypeAliases, Part->TypeAliases, Target, O);
						}
						else
						{
							throw std::runtime_error("Types fragments require a type or alias");
						}
					}
					else if (Owner)
					{
						if (Owner->View.Target == EAnnotationTarget::Class)
						{
							ApplyMember(*static_cast<FClassDecl*>(Owner->Object), *Part, Target, O);
						}

						else if (Owner->View.Target == EAnnotationTarget::Struct)
						{
							ApplyMember(*static_cast<FStructDecl*>(Owner->Object), *Part, Target, O);
						}

						else if (Owner->View.Target == EAnnotationTarget::Interface)
						{
							ApplyMember(*static_cast<FInterfaceDecl*>(Owner->Object), *Part, Target, O);
						}

						else
						{
							throw std::runtime_error("declaration owner is not a type");
						}
					}
					else
					{
						const auto N = SelectFragmentRoots(*Part, {});
						if (N != 1)
						{
							throw std::runtime_error("fragment must contain exactly one declaration");
						}

						const bool Type = HasFragmentRoot(Part->Classes, {}) || HasFragmentRoot(Part->Structs, {}) ||
						    HasFragmentRoot(Part->Interfaces, {}) || HasFragmentRoot(Part->Enums, {}) || HasFragmentRoot(Part->TypeAliases, {});
						if (Type != (Phase == ECompileProcessingPhase::Types))
						{
							throw std::runtime_error("declaration kind does not match generation phase");
						}

						if (HasFragmentRoot(Part->Classes, {}))
						{
							AddOrReplace(P.Classes, Part->Classes, Target, O);
						}

						else if (HasFragmentRoot(Part->Structs, {}))
						{
							AddOrReplace(P.Structs, Part->Structs, Target, O);
						}

						else if (HasFragmentRoot(Part->Interfaces, {}))
						{
							AddOrReplace(P.Interfaces, Part->Interfaces, Target, O);
						}

						else if (HasFragmentRoot(Part->Enums, {}))
						{
							AddOrReplace(P.Enums, Part->Enums, Target, O);
						}

						else if (HasFragmentRoot(Part->TypeAliases, {}))
						{
							AddOrReplace(P.TypeAliases, Part->TypeAliases, Target, O);
						}

						else if (!Part->Functions.empty())
						{
							AddOrReplace(P.Functions, Part->Functions, Target, O);
						}

						else
						{
							auto& S = Part->TopLevelStatements.front();
							if (S->Kind != EStmtKind::VarDecl)
							{
								throw std::runtime_error("global fragment must be a variable declaration");
							}

							auto& G = static_cast<FVarDeclStmt&>(*S);
							CheckFragmentMetadata(G.DeclMeta, O.Metadata);
							if (Target)
							{
								if (Target->View.Target != EAnnotationTarget::GlobalVariable ||
								    G.Name != Target->View.Name)
								{
									throw std::runtime_error("invalid global replacement");
								}

								ApplyReplacementMetadata(G.DeclMeta, *Target->Meta, O.Metadata);
								bool Replaced = false;
								for (auto& Old : P.TopLevelStatements)
								{
									if (Old.get() == Target->Object)
									{
										Old = std::move(S);
										Replaced = true;
										break;
									}
									if (Old && Old->Kind == EStmtKind::VarDeclGroup)
									{
										for (auto& Entry : static_cast<FVarDeclGroupStmt&>(*Old).Declarations)
										{
											if (Entry.get() == Target->Object)
											{
												Entry.reset(static_cast<FVarDeclStmt*>(S.release()));
												Replaced = true;
												break;
											}
										}
									}

									if (Replaced)
									{
										break;
									}
								}
								if (!Replaced)
								{
									throw std::runtime_error("global replacement target became invalid");
								}
							}
							else
							{
								G.DeclMeta.StableId = "generated:" + O.GenerationId;
								ApplyMetadata(G.DeclMeta, O.Metadata);
								P.TopLevelStatements.push_back(std::move(S));
							}
						}
						for (auto& Ns : Part->Namespaces)
						{
							if (std::none_of(P.Namespaces.begin(), P.Namespaces.end(),
							        [&](const auto& X)
							{
								return X.Name == Ns.Name && X.ModulePath == Ns.ModulePath;
							}))
							{
								P.Namespaces.push_back(std::move(Ns));
							}
						}
					}
					if (Target)
					{
						DiscardScopedDeclarations(P, ReplacedRange);
					}
					MergeFragmentTypes(P, *Part);
				}
				if (Replay)
				{
					--Session->ReservedReplayOperations;
				}

				Session->Operations.push_back(std::move(O));
			}
			CollectProgramSourceMetadata(P);
			if (Phase == ECompileProcessingPhase::Bodies)
			{
				if (ApplyFragmentMetadata(P))
				{
					CollectProgramSourceMetadata(P);
				}
			}
			return true;
		}
		catch (const FAnalysisCancelled&)
		{
			throw;
		}
		catch (const std::exception& E)
		{
			FScriptError D;
			D.Message = std::string("compile processing failed: ") + E.what();
			Diagnostics.push_back(std::move(D));
			return false;
		}
	}

	bool ResolveOutlineDeclarationTypes(
	    FProgram& P, const IHostBindingCatalog* B, std::vector<FScriptError>& Diagnostics)
	{
		const auto S = P.CompileProcessingSession;
		if (!S ||
		    (!(S->Options.Registry && S->Options.Registry->HasPhase(ECompileProcessingPhase::Types)) &&
		        S->ReplayOperations.empty()))
		{
			return true;
		}
		bool OK = true;
		const FDeferredSyntax* TypeContext = nullptr;
		FParserLookupContext ParserLookups(P, B);
		const auto AddTemplateContext = [](FDeferredSyntax& Context, const auto& Declaration)
		{
			for (const auto& Name : Declaration.TypeParams)
			{
				if (std::find(Context.TypeParameters.begin(), Context.TypeParameters.end(), Name) == Context.TypeParameters.end())
				{
					Context.TypeParameters.push_back(Name);
				}
			}
			for (const auto& Parameter : Declaration.TemplateParameters.GetParameters())
			{
				if (Parameter.Kind == ETemplateParameterKind::Value)
				{
					Context.ValueParameters[Parameter.Name] = Parameter;
				}
			}
		};
		auto Type = [&](std::string& Name, const std::string& Module, const std::string& Ns,
		                const std::vector<std::string>& Params, FSourceLocation Location)
		{
			if (Name.empty() || Name == "auto" || Name == "void") return;
			// Internal names were already bound by the originating parser and are not source tokens.
			if (Name.find('$') != std::string::npos || Name.find('\x1F') != std::string::npos) return;
			S->Check();
			VeroScriptLexer L(Name, Location.FileLabel);
			L.SetInitialPosition(Location.Line, Location.Column);
			const auto Tokens = L.Tokenize();
			VeroScriptParser Parser(Tokens, Module, &P);
			ParserLookups.Attach(Parser);
			if (TypeContext)
			{
				Parser.RestoreDeclarationContext(*TypeContext, false);
			}
			auto Resolved = Parser.ParseTypeFragment(TypeContext ? TypeContext->Namespace : Ns,
			    TypeContext ? TypeContext->TypeParameters : Params);
			if (L.HasError() || Parser.HasError() || Resolved.empty())
			{
				auto Error = Parser.GetError();
				if (L.HasError())
				{
					Error.Message = L.GetErrorMessage();
				}
				Diagnostics.push_back(std::move(Error));
				OK = false;
			}
			else
			{
				Name = std::move(Resolved);
				if (auto Fragment = Parser.TakeFragmentDeclarations())
				{
					P.ScopedTypeUses.insert(P.ScopedTypeUses.end(), Fragment->ScopedTypeUses.begin(), Fragment->ScopedTypeUses.end());
				}
			}
		};
		auto Parameters = [&](auto& Values, const auto& Module, const auto& Ns, const auto& Params,
		                      FSourceLocation Location)
		{
			for (auto& Q : Values)
			{
				Type(Q.TypeName, Module, Ns, Params, Location);
			}
		};
		auto Types = [&](auto& Values)
		{
			for (auto& T : Values)
			{
				FDeferredSyntax Context = T.DeclarationContext ? *T.DeclarationContext : FDeferredSyntax{};
				Context.TypeOwner = T.Name;
				Context.TypeScope = std::make_shared<FTypeDeclarationScope>(T.Scope);
				Context.TypeAliasBoundaries.push_back(Context.Bindings ? Context.Bindings->Aliases.size() : 0);
				Context.ModulePath = T.ModulePath;
				Context.Namespace = T.Name;
				AddTemplateContext(Context, T);
				TypeContext = &Context;
				const auto Ns = Namespace(T.Name);
				for (auto& F : T.Fields)
				{
					Type(F.TypeName, T.ModulePath, Ns, T.TypeParams, F.Location);
				}

				for (auto& M : T.Methods)
				{
					auto MethodContext = Context;
					AddTemplateContext(MethodContext, M);
					TypeContext = &MethodContext;
					Type(M.ReturnType, T.ModulePath, Ns, T.TypeParams, M.Location);
					Parameters(M.Parameters, T.ModulePath, Ns, T.TypeParams, M.Location);
				}
				TypeContext = &Context;
				using TT = std::decay_t<decltype(T)>;
				if constexpr (!std::is_same_v<TT, FInterfaceDecl>)
				{
					for (auto& C : T.Constructors)
					{
						Parameters(C.Parameters, T.ModulePath, Ns, T.TypeParams, C.Location);
					}
				}
			}
			TypeContext = nullptr;
		};
		Types(P.Classes);
		Types(P.Structs);
		Types(P.Interfaces);
		for (auto& F : P.Functions)
		{
			FDeferredSyntax Context;
			Context.ModulePath = F.ModulePath;
			Context.Namespace = Namespace(F.Name);
			AddTemplateContext(Context, F);
			TypeContext = &Context;
			Type(F.ReturnType, F.ModulePath, Namespace(F.Name), F.TypeParams, F.Location);
			Parameters(F.Parameters, F.ModulePath, Namespace(F.Name), F.TypeParams, F.Location);
		}
		TypeContext = nullptr;
		for (auto& T : P.TypeAliases)
		{
			TypeContext = T.DeclarationContext.get();
			Type(T.TypeName, T.ModulePath, Namespace(T.Name), {}, T.Location);
		}
		TypeContext = nullptr;

		for (auto& Statement : P.TopLevelStatements)
		{
			if (Statement && Statement->Kind == EStmtKind::VarDecl)
			{
				auto& G = static_cast<FVarDeclStmt&>(*Statement);
				Type(G.TypeName, G.ModulePath, Namespace(G.Name), {}, G.Location);
			}
		}

		return OK;
	}
	bool ResolveDeferredProgram(
	    FProgram& P, const IHostBindingCatalog* B, std::vector<FScriptError>& Diagnostics, const bool OutlineOnly)
	{
		bool OK = true;
		std::vector<std::unique_ptr<FProgram>> Fragments;
		FParserLookupContext ParserLookups(P, B);
		auto Parse = [&](const FDeferredSyntax& D, bool Body)
		{
			if (P.CompileProcessingSession)
			{
				P.CompileProcessingSession->Check();
			}

			VeroScriptLexer L(D.Source, D.Range.SourceId);
			L.SetInitialPosition(D.Range.Position.Begin.Line, D.Range.Position.Begin.Column);
			auto Tokens = L.Tokenize();
			VeroScriptParser Parser(Tokens, D.ModulePath, &P);
			ParserLookups.Attach(Parser);
			Parser.SetAllowInternalBuiltins(D.bAllowInternalBuiltins);
			Parser.SetDeclarationOutline(OutlineOnly, D.Source);
			Parser.RestoreDeclarationContext(D, Body);
			Parser.SetSourceText(
			    D.Source, D.Range.Position.Begin.Line, D.Range.Position.Begin.Column, D.Range.Begin);
			std::unique_ptr<FExpr> E;
			std::unique_ptr<FBlockStmt> S;
			if (!L.HasError())
			{
				if (Body)
				{
					S = Parser.ParseBodyFragment(D.Namespace, D.TypeParameters);
				}
				else
				{
					E = Parser.ParseExpressionFragment(
					    D.Namespace, D.TypeParameters, D.InitializerType, D.bVariableInitializer);
				}
			}
			if (S)
			{
				S->DefinitionModulePath = D.ModulePath;
				S->DefinitionNamespace = D.Namespace;
			}
			if (L.HasError() || Parser.HasError() || (Body ? !S : !E))
			{
				FScriptError Error = Parser.GetError();
				if (L.HasError())
				{
					Error.Message = L.GetErrorMessage();
				}
				Diagnostics.push_back(std::move(Error));
				OK = false;
			}
			Fragments.push_back(Parser.TakeFragmentDeclarations());
			return std::make_pair(std::move(E), std::move(S));
		};
		// Replacement preserves object addresses used by AST traversal by moving the parsed payload into the placeholder.
		// Body placeholders contain no nested syntax; each fragment is parsed exactly once.
		auto Body = [&](auto& Ptr)
		{
			if (Ptr && Ptr->DeferredSyntax)
			{
				auto D = Ptr->DeferredSyntax;
				P.LifetimeAnchors.push_back(D);
				auto R = Parse(*D, true);
				if (R.second)
				{
					R.second->SourceRange = D->Range;
					Ptr = std::move(R.second);
				}
			}
		};
		auto Expr = [&](auto& Ptr)
		{
			if (OutlineOnly) return;
			if (Ptr && Ptr->DeferredSyntax)
			{
				auto D = Ptr->DeferredSyntax;
				P.LifetimeAnchors.push_back(D);
				auto R = Parse(*D, false);
				if (R.first)
				{
					R.first->SourceRange = D->Range;
					using T = std::decay_t<decltype(Ptr)>;
					Ptr = T(R.first.release());
				}
			}
		};
		auto Types = [&](auto& Ts)
		{
			for (auto& T : Ts)
			{
				for (auto& X : T.Fields)
				{
					Expr(X.DefaultValue);
				}

				for (auto& X : T.Methods)
				{
					Body(X.Body);
					for (auto& Q : X.Parameters)
					{
						Expr(Q.DefaultValue);
					}
				}
				using TT = std::decay_t<decltype(T)>;
				if constexpr (!std::is_same_v<TT, FInterfaceDecl>)
				{
					for (auto& X : T.Constructors)
					{
						Body(X.Body);
						for (auto& Q : X.Parameters)
						{
							Expr(Q.DefaultValue);
						}
						for (auto& C : X.BaseInits)
						{
							for (auto& A : C.Args)
							{
								Expr(A);
							}
						}
						for (auto& C : X.FieldInits)
						{
							for (auto& A : C.Args)
							{
								Expr(A);
							}
						}
					}
					if (T.Destructor)
					{
						Body(T.Destructor->Body);
					}
				}
				if constexpr (std::is_same_v<TT, FClassDecl>)
				{
					Body(T.ClassDefaults);
				}
			}
		};
		Types(P.Classes);
		Types(P.Structs);
		Types(P.Interfaces);
		for (auto& Enum : P.Enums)
		{
			for (auto& Member : Enum.Members)
			{
				Expr(Member.Initializer);
			}
		}
		for (auto& F : P.Functions)
		{
			Body(F.Body);
			for (auto& Q : F.Parameters)
			{
				Expr(Q.DefaultValue);
			}
		}
		for (auto& S : P.TopLevelStatements)
		{
			if (S)
			{
				if (S->DeferredSyntax)
				{
					auto D = S->DeferredSyntax;
					P.LifetimeAnchors.push_back(D);
					VeroScriptLexer L(D->Source, D->Range.SourceId);
					L.SetInitialPosition(D->Range.Position.Begin.Line, D->Range.Position.Begin.Column);
					auto Tokens = L.Tokenize();
					VeroScriptParser Parser(Tokens, D->ModulePath, &P);
					ParserLookups.Attach(Parser);
					Parser.SetAllowInternalBuiltins(D->bAllowInternalBuiltins);
					Parser.SetDeclarationOutline(OutlineOnly, D->Source);
					Parser.RestoreDeclarationContext(*D, false);
					Parser.SetSourceText(D->Source, D->Range.Position.Begin.Line,
					    D->Range.Position.Begin.Column, D->Range.Begin);
					auto Parsed = Parser.ParseStatementFragment(D->Namespace, D->TypeParameters);
					Fragments.push_back(Parser.TakeFragmentDeclarations());
					if (!L.HasError() && Parsed && !Parser.HasError())
					{
						Parsed->ModulePath = S->ModulePath;
						S = std::move(Parsed);
					}
					else
					{
						auto E = Parser.GetError();
						if (L.HasError())
						{
							E.Message = L.GetErrorMessage();
							E.Location = FSourceLocation{D->Range.SourceId, D->Range.Position.Begin.Line,
							    D->Range.Position.Begin.Column};
						}
						Diagnostics.push_back(std::move(E));
						OK = false;
					}
					continue;
				}
				if (S->Kind == EStmtKind::VarDecl)
				{
					Expr(static_cast<FVarDeclStmt&>(*S).Initializer);
				}

				else if (S->Kind == EStmtKind::Expr)
				{
					Expr(static_cast<FExprStmt&>(*S).Expression);
				}

				else if (S->Kind == EStmtKind::VarDeclGroup)
				{
					for (auto& G : static_cast<FVarDeclGroupStmt&>(*S).Declarations)
					{
						Expr(G->Initializer);
					}
				}
			}
		}

		if (OK)
		{
			const auto Merge = [&](auto& Target, auto& Source)
			{
				for (auto& Declaration : Source)
				{
					if (Declaration.Scope.Id.empty())
					{
						continue;
					}
					const auto Existing = std::find_if(Target.begin(), Target.end(), [&](const auto& Candidate)
					{
						return Candidate.Scope.Id == Declaration.Scope.Id;
					});
					if (Existing == Target.end())
					{
						Target.push_back(std::move(Declaration));
					}
					else if (OutlineOnly && Existing->DeclMeta.SourceRange.SourceId == Declaration.DeclMeta.SourceRange.SourceId)
					{
						Declaration.DeclMeta = Existing->DeclMeta;
						using T = std::decay_t<decltype(Declaration)>;
						if constexpr (!std::is_same_v<T, FEnumDecl> && !std::is_same_v<T, FTypeAliasDecl>)
						{
							for (auto& Field : Declaration.Fields)
							{
								for (const auto& Previous : Existing->Fields)
								{
									if (Previous.Name == Field.Name && Previous.Location.Line == Field.Location.Line)
									{
										Field.DeclMeta = Previous.DeclMeta;
									}
								}
							}
							for (auto& Method : Declaration.Methods)
							{
								for (const auto& Previous : Existing->Methods)
								{
									if (Previous.Name == Method.Name && Previous.Location.Line == Method.Location.Line && Previous.Location.Column == Method.Location.Column)
									{
										Method.DeclMeta = Previous.DeclMeta;
									}
								}
							}
						}
						*Existing = std::move(Declaration);
					}
				}
			};
			for (auto& Fragment : Fragments)
			{
				if (!Fragment)
				{
					continue;
				}
				Merge(P.Classes, Fragment->Classes);
				Merge(P.Structs, Fragment->Structs);
				Merge(P.Interfaces, Fragment->Interfaces);
				Merge(P.Enums, Fragment->Enums);
				Merge(P.TypeAliases, Fragment->TypeAliases);
				P.ScopedTypeUses.insert(P.ScopedTypeUses.end(),
				    Fragment->ScopedTypeUses.begin(), Fragment->ScopedTypeUses.end());
			}
			CollectProgramSourceMetadata(P);
			if (ApplyFragmentMetadata(P))
			{
				CollectProgramSourceMetadata(P);
			}
		}
		return OK;
	}
	void CollectProgramSourceMetadata(FProgram& P)
	{
		FSourceOffsetIndex Offsets;
		if (!P.CompileProcessingSession) return;
		auto& Meta = *P.CompileProcessingSession->Metadata;
		for (auto& D : Meta.Documents)
		{
			if (D.LineOffsets.empty())
			{
				D.LineOffsets.push_back(0);
				for (std::size_t I = 0; I < D.Text.size(); ++I)
				{
					if (D.Text[I] == '\n')
					{
						D.LineOffsets.push_back(I + 1);
					}
				}
			}
		}

		auto Decls = Collect(P);
		struct FLexicalDocument
		{
			const FSourceDocument* Document;
			std::vector<FToken> Tokens;
			std::vector<std::size_t> Starts;
		};
		std::unordered_set<std::string_view> NeededDocuments;
		for (const auto& D : Decls)
		{
			if (D.View.Target != EAnnotationTarget::Parameter &&
			    D.Meta->SourceRange.Begin == D.Meta->SourceRange.End)
			{
				NeededDocuments.insert(D.Meta->SourceRange.SourceId);
			}
		}
		std::vector<FLexicalDocument> Lexical;
		std::unordered_map<std::string_view, std::size_t> LexicalIndices;
		for (const auto& Doc : Meta.Documents)
		{
			if (Doc.Text.empty() || Doc.Id.find(":raw") != std::string::npos)
			{
				continue;
			}

			if (!NeededDocuments.count(Doc.Id))
			{
				continue;
			}

			VeroScriptLexer Lexer(Doc.Text, Doc.Id);
			FLexicalDocument L{&Doc, Lexer.Tokenize(), {}};
			if (Lexer.HasError())
			{
				continue;
			}

			std::vector<std::size_t> Lines{0};
			for (std::size_t I = 0; I < Doc.Text.size(); ++I)
			{
				if (Doc.Text[I] == '\n')
				{
					Lines.push_back(I + 1);
				}
			}

			for (const auto& T : L.Tokens)
			{
				const auto Line = static_cast<std::size_t>(T.Location.Line - 1);
				L.Starts.push_back(Line < Lines.size()
				        ? std::min(Doc.Text.size(), Lines[Line] + T.Location.Column - 1)
				        : Doc.Text.size());
			}
			LexicalIndices.emplace(Doc.Id, Lexical.size());
			Lexical.push_back(std::move(L));
		}
		auto CompleteRange = [&](FDecl& D)
		{
			auto& R = D.Meta->SourceRange;
			if (R.Begin != R.End || D.View.Target == EAnnotationTarget::Parameter) return;
			const auto Document = LexicalIndices.find(R.SourceId);
			if (Document == LexicalIndices.end()) return;
			const auto& L = Lexical[Document->second];
			auto I = static_cast<std::size_t>(
			    std::lower_bound(L.Starts.begin(), L.Starts.end(), R.Begin) - L.Starts.begin());
			const auto Begin = I;
			int Parens = 0, Braces = 0, Brackets = 0;
			bool SeenBrace = false;
			for (; I < L.Tokens.size() && L.Tokens[I].Kind != ETokenKind::EndOfFile; ++I)
			{
				const auto T = L.Tokens[I].Lexeme;
				if (T == "(")
				{
					++Parens;
				}
				else if (T == ")")
				{
					--Parens;
				}
				else if (T == "[")
				{
					++Brackets;
				}
				else if (T == "]")
				{
					--Brackets;
				}

				else if (T == "{")
				{
					++Braces;
					SeenBrace = true;
				}
				else if (T == "}")
				{
					if (!Braces)
					{
						break;
					}
					--Braces;
					if (!Braces && !Parens && !Brackets)
					{
						break;
					}
				}
				if (T == ";" && !Parens && !Braces && !Brackets)
				{
					break;
				}

				if (D.View.Target == EAnnotationTarget::EnumMember && (T == "," || T == "}"))
				{
					if (I > Begin)
					{
						--I;
					}
					break;
				}
			}
			if (I < L.Tokens.size())
			{
				R.End = std::min(L.Document->Text.size(), L.Starts[I] + L.Tokens[I].Lexeme.size());
				R.Position.End = {R.SourceId, L.Tokens[I].Location.Line, L.Tokens[I].Location.Column};
				for (char C : L.Tokens[I].Lexeme)
				{
					if (C == '\n')
					{
						++R.Position.End.Line;
						R.Position.End.Column = 1;
					}
					else
					{
						++R.Position.End.Column;
					}
				}
			}
			(void)SeenBrace;
		};
		std::set<std::string> PackageModules;
		std::unordered_map<std::string, const std::vector<FAnnotationDefinition>*> PackageDefinitions;
		for (const auto& M : P.PackageSourceModules)
		{
			if (M.bFromPackage)
			{
				PackageModules.insert(M.ModulePath);
				PackageDefinitions.emplace(
				    M.ModulePath, M.InitialSourceMetadata ? &M.InitialSourceMetadata->Definitions : nullptr);
			}
		}

		auto BindAnnotations = [&](FDeclMeta& M, const std::string& Module)
		{
			if (M.Annotations.empty()) return;
			const auto I = PackageDefinitions.find(Module);
			const auto* Definitions = I == PackageDefinitions.end()
			    ? &P.CompileProcessingSession->Options.AnnotationDefinitions
			    : I->second;
			if (!Definitions) return;
			for (auto& A : M.Annotations)
			{
				if (A.DefinitionId.empty())
				{
					const FAnnotationDefinition* Match = nullptr;
					for (const auto& D : *Definitions)
					{
						if (AnnotationNamesEqual(A.Name, D.Name, D.CaseSensitive))
						{
							if (Match)
							{
								Match = nullptr;
								break;
							}
							Match = &D;
						}
					}

					if (Match)
					{
						A.DefinitionId = Match->Id;
					}
				}
			}
		};
		for (auto& D : Decls)
		{
			CompleteRange(D);
			BindAnnotations(*D.Meta, D.View.ModulePath);
		}
		const auto HasExtent = [](const FSourceRange& Range)
		{
			return !Range.SourceId.empty() && Range.End > Range.Begin;
		};
		FDeclarationRangeIndex DeclarationRanges;
		for (std::size_t I = 0; I < Decls.size(); ++I)
		{
			if ((I & 255u) == 0u)
			{
				CheckAnalysisCancellation();
			}
			if (HasExtent(Decls[I].Meta->SourceRange))
			{
				DeclarationRanges.Add(Decls[I].Meta->SourceRange, I);
			}
		}
		DeclarationRanges.Build();
		// Package nodes retain producer descriptions and body metadata. Live nodes
		// are rebuilt from the current owned AST, never from a stale analysis view.
		auto PreviousNodes = std::move(Meta.Nodes);
		Meta.Nodes.clear();
		for (const auto& N : PreviousNodes)
		{
			if (PackageModules.count(N.ModuleId))
			{
				Meta.Nodes.push_back(N);
			}
		}

		std::unordered_map<std::string, std::size_t> NodeIndices;
		for (std::size_t I = 0; I < Meta.Nodes.size(); ++I)
		{
			NodeIndices.emplace(Meta.Nodes[I].Id, I);
		}

		auto InsertNode = [&](FSourceNodeMetadata N)
		{
			const auto I = NodeIndices.find(N.Id);
			if (I == NodeIndices.end())
			{
				NodeIndices.emplace(N.Id, Meta.Nodes.size());
				Meta.Nodes.push_back(std::move(N));
			}
			else
			{
				Meta.Nodes[I->second] = std::move(N);
			}
		};
		for (auto& D : Decls)
		{
			FSourceNodeMetadata N;
			N.Id = D.View.Id;
			N.OwnerId = D.View.OwnerId.empty() ? "module:" + D.View.ModulePath : D.View.OwnerId;
			N.ParentId = N.OwnerId;
			N.Kind = "declaration";
			N.Range = D.Meta->SourceRange;
			N.Name = D.View.Name;
			N.ModuleId = D.View.ModulePath;
			N.TypeName = D.View.TypeName;
			N.Target = static_cast<std::uint32_t>(D.View.Target);
			N.CommentRetention = D.Meta->CommentRetention;
			N.Annotations = D.Meta->Annotations;
			N.Comments = D.Meta->Comments;
			N.Documentation = D.Meta->Documentation;
			InsertNode(std::move(N));
		}
		std::map<std::string, std::size_t> Ordinals;
		std::unordered_map<const void*, const void*> Parents;
		std::unordered_map<const void*, std::string> NodeIds;
		std::unordered_map<const void*, const FDecl*> Declarations;
		std::unordered_map<const void*, const FDecl*> RootOwners;
		std::unordered_map<const void*, const FDecl*> StructuralOwners;
		for (const auto& D : Decls)
		{
			NodeIds[D.Object] = D.View.Id;
			Declarations.emplace(D.Object, &D);
		}

		std::unordered_map<std::string, std::vector<const FDecl*>> Bodies;
		for (const auto& D : Decls)
		{
			if (D.Body && *D.Body && HasExtent((*D.Body)->SourceRange))
			{
				Bodies[(*D.Body)->SourceRange.SourceId].push_back(&D);
			}
		}

		for (auto& Entry : Bodies)
		{
			std::sort(Entry.second.begin(), Entry.second.end(),
			    [](auto L, auto R)
			{
				return (*L->Body)->SourceRange.Begin < (*R->Body)->SourceRange.Begin;
			});
		}
		std::vector<const void*> OwnerPath;
		const auto FindStructuralOwner = [&](const void* Object)
		{
			const FDecl* Owner = nullptr;
			OwnerPath.clear();
			while (Object)
			{
				if (const auto Root = RootOwners.find(Object); Root != RootOwners.end())
				{
					Owner = Root->second;
					break;
				}
				if (const auto Declaration = Declarations.find(Object); Declaration != Declarations.end())
				{
					Owner = Declaration->second;
					break;
				}
				if (const auto Known = StructuralOwners.find(Object); Known != StructuralOwners.end())
				{
					Owner = Known->second;
					break;
				}
				OwnerPath.push_back(Object);
				if ((OwnerPath.size() & 255u) == 0u)
				{
					CheckAnalysisCancellation();
				}
				const auto Parent = Parents.find(Object);
				Object = Parent == Parents.end() ? nullptr : Parent->second;
			}
			for (const auto* Child : OwnerPath)
			{
				StructuralOwners.emplace(Child, Owner);
			}
			return Owner;
		};

		std::size_t VisitedNodes = 0;
		auto Node = [&](auto& N, std::string Kind)
		{
			if ((VisitedNodes++ & 255u) == 0u)
			{
				CheckAnalysisCancellation();
			}
			if (N.SourceRange.SourceId.empty())
			{
				N.SourceRange = RangeAt(P, N.Location, Offsets);
			}

			std::string Owner, Module;
			if constexpr (std::is_base_of_v<FStmt, std::decay_t<decltype(N)>>)
			{
				Module = N.ModulePath;
			}

			if (const auto Group = Bodies.find(N.SourceRange.SourceId);
			    HasExtent(N.SourceRange) && Group != Bodies.end())
			{
				const auto I =
				    std::upper_bound(Group->second.begin(), Group->second.end(), N.SourceRange.Begin,
				        [](auto Offset, auto D)
				{
					return Offset < (*D->Body)->SourceRange.Begin;
				});
				if (I != Group->second.begin())
				{
					const auto& D = **std::prev(I);
					const auto& R = (*D.Body)->SourceRange;
					if (R.End >= N.SourceRange.End)
					{
						Owner = D.View.Id;
						Module = D.View.ModulePath;
					}
				}
			}
			if (Owner.empty() && HasExtent(N.SourceRange))
			{
				if (const auto Index = DeclarationRanges.FindContaining(N.SourceRange))
				{
					const auto& D = Decls[*Index];
					Owner = D.View.Id;
					Module = D.View.ModulePath;
				}
			}
			// A location-only range has no byte containment evidence. In particular,
			// documentless parser clients leave every offset at zero. Preserve that
			// anchor and use the owned AST rather than guessing a spatial owner.
			if (Owner.empty() && !HasExtent(N.SourceRange))
			{
				if (const auto* StructuralOwner = FindStructuralOwner(&N))
				{
					Owner = StructuralOwner->View.Id;
					Module = StructuralOwner->View.ModulePath;
				}
			}

			if (Module.empty())
			{
				if (const auto* Doc = Meta.FindDocument(N.SourceRange.SourceId))
				{
					Module = Doc->ModuleId;
				}
			}

			if (Owner.empty() && (!Module.empty() || HasExtent(N.SourceRange)))
			{
				Owner = "module:" + Module;
			}

			std::string Parent = Owner;
			if (const auto I = Parents.find(&N); I != Parents.end())
			{
				if (const auto ID = NodeIds.find(I->second); ID != NodeIds.end())
				{
					Parent = ID->second;
				}
			}

			if (N.DeclMeta.StableId.empty())
			{
				const auto Prefix = "syntax:" + Owner + ":";
				const auto ParentPath = Parent == Owner ? std::string("root")
				    : Parent.rfind(Prefix, 0) == 0      ? Parent.substr(Prefix.size())
				                                        : Parent;
				const auto Path =
				    ParentPath + ":" + Kind + ":" + std::to_string(Ordinals[Parent + ":" + Kind]++);
				std::uint64_t Hash = 14695981039346656037ull;
				for (unsigned char C : Path)
				{
					Hash ^= C;
					Hash *= 1099511628211ull;
				}
				N.DeclMeta.StableId = "syntax:" + Owner + ":" + std::to_string(Hash);
			}
			NodeIds[&N] = N.DeclMeta.StableId;
			BindAnnotations(N.DeclMeta, Module);
			FSourceNodeMetadata V;
			V.Id = N.DeclMeta.StableId;
			V.OwnerId = Owner;
			V.ParentId = Parent;
			V.ModuleId = Module;
			V.Kind = std::move(Kind);
			V.CommentRetention = N.DeclMeta.CommentRetention;
			V.Range = N.SourceRange;
			V.Annotations = N.DeclMeta.Annotations;
			V.Comments = N.DeclMeta.Comments;
			V.Documentation = N.DeclMeta.Documentation;
			InsertNode(std::move(V));
		};
		WalkProgram(
		    P,
		    [&](FExpr& E)
		{
			Node(E, "expression:" + std::to_string(static_cast<unsigned>(E.Kind)));
		},
		    [&](FStmt& S)
		{
			if (S.Kind != EStmtKind::VarDecl || Declarations.find(&S) == Declarations.end())
			{
				Node(S, "statement:" + std::to_string(static_cast<unsigned>(S.Kind)));
			}
		},
		    &Parents,
		    [&](const void* Root, const void* Declaration)
		{
			if (const auto D = Declarations.find(Declaration); D != Declarations.end())
			{
				RootOwners.emplace(Root, D->second);
			}
		});
		std::unordered_map<std::string, FDecl*> DeclarationOwners;
		for (auto& D : Decls)
		{
			DeclarationOwners.emplace(D.View.Id, &D);
		}

		for (auto& Doc : Meta.Documents)
		{
			const bool Generated = Doc.Id.rfind("veroscript-generated:", 0) == 0;
			if (!Generated && (Doc.Id != Doc.ModuleId || PackageModules.count(Doc.ModuleId)))
			{
				continue;
			}

			FSourceNodeMetadata Root;
			Root.Id = "module:" + Doc.ModuleId;
			Root.Kind = "module";
			Root.ModuleId = Doc.ModuleId;
			if (const auto I = P.CompileProcessingSession->ModuleMetadata.find(Doc.ModuleId);
			    I != P.CompileProcessingSession->ModuleMetadata.end())
			{
				BindAnnotations(I->second, Doc.ModuleId);
				Root.Annotations = I->second.Annotations;
				Root.Comments = I->second.Comments;
				Root.Documentation = I->second.Documentation;
			}
			Root.Name = Doc.ModuleId;
			Root.Target = static_cast<std::uint32_t>(EAnnotationTarget::Module);
			Root.Range.SourceId = Doc.Id;
			Root.Range.End = Doc.Text.size();
			for (auto& C : Doc.Comments)
			{
				C.ModuleId = Doc.ModuleId;
				auto Origins = ResolveSourceOrigins(Meta, C.Range);
				if (!Origins.empty())
				{
					C.Origin = Origins.front();
				}

				if (C.Activity != ESourceActivity::Active)
				{
					continue;
				}

				auto Content = std::string_view(C.Text);
				const auto Tag = Content.find_first_not_of(" \t\r\n*");
				const bool FileDocumentation = (C.Kind == ESourceCommentKind::DocumentationLine ||
				                                   C.Kind == ESourceCommentKind::DocumentationBlock) &&
				    Tag != std::string_view::npos && Content.substr(Tag, 5) == "@file" &&
				    (Tag + 5 == Content.size() || Content[Tag + 5] == ' ' || Content[Tag + 5] == '\t' ||
				        Content[Tag + 5] == '\r' || Content[Tag + 5] == '\n');
				if (FileDocumentation)
				{
					C.OwnerId = Root.Id;
					C.Association = ECommentAssociation::Module;
					if (!P.CompileProcessingSession->ExplicitCommentTargets.count(Root.Id))
					{
						Root.Comments.push_back(C);
					}
					continue;
				}
				FDecl* Owner = nullptr;
				ECommentAssociation Association = ECommentAssociation::Module;
				std::size_t Width = SIZE_MAX;
				if (const auto I = DeclarationOwners.find(C.OwnerId); I != DeclarationOwners.end())
				{
					auto& D = *I->second;
					const bool ReplacedDeclaration =
					    D.Meta->SourceRange.SourceId.rfind("veroscript-generated:", 0) == 0 &&
					    D.Meta->SourceRange.SourceId != Doc.Id;
					const bool ReplacedBody = D.Body && *D.Body && (*D.Body)->SourceRange.SourceId != Doc.Id;
					if ((C.Association == ECommentAssociation::Body &&
					        (ReplacedDeclaration || ReplacedBody)) ||
					    (C.Association == ECommentAssociation::Scope && ReplacedDeclaration))
					{
						C.Activity = ESourceActivity::Removed;
						continue;
					}
					// Keep source comments attached to the same stable declaration
					// when its syntax moves into a replacement document.
					if (ReplacedDeclaration &&
					    (C.Association == ECommentAssociation::Leading ||
					        C.Association == ECommentAssociation::Trailing))
					{
						Owner = &D;
						Association = C.Association;
						Width = 0;
					}
				}
				for (auto& D : Decls)
				{
					if (D.Body && *D.Body)
					{
						const auto& R = (*D.Body)->SourceRange;
						if (R.SourceId == Doc.Id && R.Begin <= C.Range.Begin && R.End >= C.Range.End &&
						    R.End - R.Begin < Width)
						{
							Owner = &D;
							Association = ECommentAssociation::Body;
							Width = R.End - R.Begin;
						}
					}
				}

				if (!Owner)
				{
					for (auto& D : Decls)
					{
						if (D.Meta->SourceRange.SourceId == Doc.Id)
						{
							auto Start = D.Meta->SourceRange.Begin;
							for (const auto& A : D.Meta->Annotations)
							{
								if (A.Range.Begin.FileLabel == Doc.Id)
								{
									Start = std::min(Start,
									    Offsets.GetOffset(Doc,
									        {A.Range.Begin.FileLabel, A.Range.Begin.Line,
									            A.Range.Begin.Column}));
								}
							}

							if (C.Range.End <= Start)
							{
								auto Gap =
								    std::string_view(Doc.Text).substr(C.Range.End, Start - C.Range.End);
								bool Clear = Gap.find_first_not_of(" \t\r\n") == std::string_view::npos;
								// Consecutive preceding comments belong to the same nearest declaration.
								if (!Clear)
								{
									Clear = true;
									for (std::size_t I = C.Range.End; I < Start; ++I)
									{
										if (Doc.Text[I] == ' ' || Doc.Text[I] == '\t' ||
										    Doc.Text[I] == '\r' || Doc.Text[I] == '\n')
										{
											continue;
										}

										// Declaration locations follow export/template/access prefixes.
										// These validated prefixes do not separate leading comments.
										const auto Word = [&](std::string_view W)
										{
											return I + W.size() <= Start &&
											    std::string_view(Doc.Text).substr(I, W.size()) == W &&
											    (I + W.size() == Start || Doc.Text[I + W.size()] == ' ' ||
											        Doc.Text[I + W.size()] == '\t' ||
											        Doc.Text[I + W.size()] == '\r' ||
											        Doc.Text[I + W.size()] == '\n' ||
											        Doc.Text[I + W.size()] == ':' ||
											        Doc.Text[I + W.size()] == '<' ||
											        Doc.Text[I + W.size()] == '/');
										};
										std::size_t Prefix = 0;
										const auto SkipComment = [&](std::size_t& E)
										{
											for (const auto& Q : Doc.Comments)
											{
												if (Q.Range.Begin <= E && Q.Range.End > E)
												{
													E = Q.Range.End;
													return true;
												}
											}
											return false;
										};
										const auto SkipTrivia = [&](std::size_t& E)
										{
											while (E < Start)
											{
												if (Doc.Text[E] == ' ' || Doc.Text[E] == '\t' ||
												    Doc.Text[E] == '\r' || Doc.Text[E] == '\n')
												{
													++E;
												}
												else if (!SkipComment(E))
												{
													break;
												}
											}
										};
										const bool TypeHeader = D.View.Target == EAnnotationTarget::Class ||
										    D.View.Target == EAnnotationTarget::Struct ||
										    D.View.Target == EAnnotationTarget::Interface;
										for (const auto& W : {"public", "protected", "private"})
										{
											if ((TypeHeader || !D.View.OwnerId.empty()) && Word(W))
											{
												auto E = I + std::char_traits<char>::length(W);
												SkipTrivia(E);
												if (TypeHeader)
												{
													Prefix = std::char_traits<char>::length(W);
												}

												else if (E < Start && Doc.Text[E] == ':')
												{
													Prefix = E + 1 - I;
												}
											}
										}

										if (!Prefix)
										{
											for (const auto& W :
											    {std::pair<const char*, bool>{"export", D.View.bExported},
											        {"static", D.View.bIsStatic},
											        {"virtual", D.View.bVirtual},
											        {"explicit", D.View.bExplicit},
											        {"const", D.View.bIsConst},
											        {"mutable", D.View.bIsMutable}})
											{
												if (W.second && Word(W.first))
												{
													Prefix = std::char_traits<char>::length(W.first);
													break;
												}
											}
										}

										if (!Prefix && !D.View.TypeParameters.empty() && Word("template"))
										{
											auto E = I + 8;
											SkipTrivia(E);
											if (E < Start && Doc.Text[E] == '<')
											{
												int Depth = 0;
												for (; E < Start; ++E)
												{
													if (SkipComment(E))
													{
														--E;
														continue;
													}
													if (Doc.Text[E] == '<')
													{
														++Depth;
													}
													else if (Doc.Text[E] == '>' && !--Depth)
													{
														Prefix = E + 1 - I;
														break;
													}
												}
											}
										}
										if (Prefix)
										{
											I += Prefix - 1;
											continue;
										}
										bool InComment = false;
										for (const auto& Q : Doc.Comments)
										{
											if (Q.Range.Begin <= I && Q.Range.End > I)
											{
												I = Q.Range.End - 1;
												InComment = true;
												break;
											}
										}

										if (!InComment)
										{
											Clear = false;
											break;
										}
									}
								}
								bool Blank = false, LineHasContent = true;
								// A gap containing code cannot associate this comment with the
								// declaration. Avoid scanning its entire suffix for blank lines.
								if (Clear)
								{
									for (std::size_t I = C.Range.End; I < Start; ++I)
									{
										if (Doc.Text[I] == '\n')
										{
											if (!LineHasContent)
											{
												Blank = true;
											}
											LineHasContent = false;
										}
										else if (Doc.Text[I] != ' ' && Doc.Text[I] != '\t' &&
										    Doc.Text[I] != '\r')
										{
											LineHasContent = true;
											for (const auto& Q : Doc.Comments)
											{
												if (Q.Range.Begin == I)
												{
													I = Q.Range.End - 1;
													break;
												}
											}
										}
									}
								}

								if (Clear && !Blank && Start - C.Range.End < Width)
								{
									Owner = &D;
									Association = ECommentAssociation::Leading;
									Width = Start - C.Range.End;
								}
							}
							if (C.Range.Begin >= D.Meta->SourceRange.End &&
							    C.Range.Position.Begin.Line == D.Meta->SourceRange.Position.End.Line)
							{
								auto Gap = std::string_view(Doc.Text).substr(
								    D.Meta->SourceRange.End, C.Range.Begin - D.Meta->SourceRange.End);
								if (Gap.find_first_not_of(" \t\r") == std::string_view::npos &&
								    C.Range.Begin - D.Meta->SourceRange.End < Width)
								{
									Owner = &D;
									Association = ECommentAssociation::Trailing;
									Width = C.Range.Begin - D.Meta->SourceRange.End;
								}
							}
						}
					}
				}

				if (!Owner)
				{
					for (auto& D : Decls)
					{
						if (D.View.Target == EAnnotationTarget::Class ||
						    D.View.Target == EAnnotationTarget::Struct ||
						    D.View.Target == EAnnotationTarget::Interface ||
						    D.View.Target == EAnnotationTarget::Namespace)
						{
							const auto& R = D.Meta->SourceRange;
							if (R.SourceId == Doc.Id && R.Begin <= C.Range.Begin && R.End >= C.Range.End &&
							    R.End - R.Begin < Width)
							{
								Owner = &D;
								Association = ECommentAssociation::Scope;
								Width = R.End - R.Begin;
							}
						}
					}
				}

				C.OwnerId = Owner ? Owner->View.Id : Root.Id;
				C.Association = Association;
				if (Owner)
				{
					auto& Cs = Owner->Meta->Comments;
					if (Generated &&
					    (Association == ECommentAssociation::Leading ||
					        Association == ECommentAssociation::Trailing) &&
					    !P.CompileProcessingSession->ExplicitCommentTargets.count(Owner->View.Id))
					{
						Cs.erase(std::remove_if(Cs.begin(), Cs.end(),
						             [&](const auto& V)
						{
							return V.Range.SourceId != Doc.Id;
						}),
						    Cs.end());
					}

					if (!P.CompileProcessingSession->ExplicitCommentTargets.count(Owner->View.Id) &&
					    std::none_of(Cs.begin(), Cs.end(),
					        [&](const auto& V)
					{
						return V.Id == C.Id;
					}))
					{
						Cs.push_back(C);
					}

					Owner->Meta->Documentation = FormatSourceDocumentation(Cs);
					if (const auto I = NodeIndices.find(Owner->View.Id); I != NodeIndices.end())
					{
						auto& N = Meta.Nodes[I->second];
						N.Comments = Cs;
						N.Documentation = Owner->Meta->Documentation;
					}
					if (Association == ECommentAssociation::Body)
					{
						FSourceNodeMetadata V;
						V.Id = "comment-node:" + C.Id;
						V.OwnerId = Owner->View.Id;
						V.ParentId = Owner->View.Id;
						V.ModuleId = Doc.ModuleId;
						V.Kind = "comment";
						V.Range = C.Range;
						V.Comments = {C};
						InsertNode(std::move(V));
					}
				}
				else if (!P.CompileProcessingSession->ExplicitCommentTargets.count(Root.Id))
				{
					Root.Comments.push_back(C);
				}
			}
			// A generated fragment may contribute declaration comments, but must
			// not overwrite its module's original root metadata.
			if (!Generated)
			{
				Root.Documentation = FormatSourceDocumentation(Root.Comments);
				InsertNode(std::move(Root));
			}
		}
		for (auto& Entry : P.CompileProcessingSession->ModuleMetadata)
		{
			if (PackageModules.count(Entry.first))
			{
				BindAnnotations(Entry.second, Entry.first);
				if (const auto I = NodeIndices.find("module:" + Entry.first); I != NodeIndices.end())
				{
					auto& N = Meta.Nodes[I->second];
					N.Annotations = Entry.second.Annotations;
					N.Comments = Entry.second.Comments;
					N.Documentation = Entry.second.Documentation;
				}
			}
		}

		for (auto& N : Meta.Nodes)
		{
			(void)TryResolveFirstSourceOrigin(Meta, N.Range, N.Origin);
		}
		P.SourceMetadata = P.CompileProcessingSession->Metadata;
	}
	bool RestoreCompileProcessing(FProgram& P, std::vector<FScriptError>& Diagnostics)
	{
		if (!P.CompileProcessingSession)
		{
			P.CompileProcessingSession =
			    std::make_shared<FCompileProcessingSession>(FCompileProcessingOptions{});
		}

		auto& S = *P.CompileProcessingSession;
		const auto BudgetError = [&]()
		{
			FScriptError E;
			E.Message = "package compile generation budget exceeded";
			Diagnostics.push_back(std::move(E));
			return false;
		};
		if (S.Operations.size() > S.Options.MaxOperations || S.SourceBytes > S.Options.MaxSourceBytes)
		{
			return BudgetError();
		}
		std::set<std::string> Ids;
		for (const auto& Module : P.PackageSourceModules)
		{
			if (!Module.bFromPackage)
			{
				continue;
			}

			if (Module.InitialSourceMetadata)
			{
				for (const auto& D : Module.InitialSourceMetadata->Documents)
				{
					if (!S.Metadata->FindDocument(D.Id))
					{
						auto Document = D;
						if (Document.Id == Module.ModulePath && Document.Text.empty())
						{
							Document.Text = Module.Source;
						}
						S.Metadata->Documents.push_back(std::move(Document));
					}
				}

				for (const auto& D : Module.InitialSourceMetadata->Definitions)
				{
					if (const auto Existing = S.Metadata->FindDefinition(D.Id))
					{
						FSourceMetadataSnapshot L, R;
						L.Definitions = {*Existing};
						R.Definitions = {D};
						std::vector<std::uint8_t> A, B;
						std::string Error;
						if (!EncodeSourceMetadata(L, A, Error) || !EncodeSourceMetadata(R, B, Error) ||
						    A != B)
						{
							throw std::runtime_error("conflicting producer annotation description identity");
						}
					}
					else
					{
						S.Metadata->Definitions.push_back(D);
					}
				}

				for (const auto& N : Module.InitialSourceMetadata->Nodes)
				{
					if (!S.Metadata->FindNode(N.Id))
					{
						S.Metadata->Nodes.push_back(N);
					}
				}
			}
			for (const auto& O : Module.CompileGeneratedOperations)
			{
				if (O.Scope.ModuleId != Module.ModulePath || !Ids.insert(O.GenerationId).second)
				{
					FScriptError E;
					E.Message = "invalid or duplicate package processing operation";
					Diagnostics.push_back(std::move(E));
					return false;
				}
				if (S.Operations.size() + S.ReservedReplayOperations >= S.Options.MaxOperations ||
				    O.Source.size() > S.Options.MaxSourceBytes - S.SourceBytes)
				{
					return BudgetError();
				}

				++S.ReservedReplayOperations;
				S.SourceBytes += O.Source.size();
				if (O.Kind != ECompileOperation::EditSource)
				{
					S.ReplayOperations.push_back(O);
				}
			}
		}
		P.SourceMetadata = S.Metadata;
		// Restore the producer's original metadata before stage preconditions are
		// checked. Parent identities are assigned before their children/parameters.
		const bool HasPackageDeclarations = !P.PrecompiledPackageModules.empty() || std::any_of(P.PackageSourceModules.begin(), P.PackageSourceModules.end(), [](const auto& Module)
		{
			return Module.bFromPackage;
		});
		for (int Pass = 0; HasPackageDeclarations && Pass < 3; ++Pass)
		{
			auto Decls = Collect(P);
			for (auto& D : Decls)
			{
				if (D.View.bFromPackage)
				{
					const FSourceNodeMetadata* Match = nullptr;
					for (const auto& N : S.Metadata->Nodes)
					{
						if (N.Kind == "declaration" && N.ModuleId == D.View.ModulePath &&
						    N.Name == D.View.Name && N.Target == static_cast<std::uint32_t>(D.View.Target) &&
						    (D.View.OwnerId.empty() ? N.OwnerId == "module:" + D.View.ModulePath
						                            : N.OwnerId == D.View.OwnerId))
						{
							if (Match)
							{
								Match = nullptr;
								break;
							}
							Match = &N;
						}
					}

					if (Match)
					{
						FCompileMetadata M;
						M.Annotations = Match->Annotations;
						M.Comments = Match->Comments;
						ApplyMetadata(*D.Meta, M);
						D.Meta->StableId = Match->Id;
						D.Meta->SourceRange = Match->Range;
						D.Meta->CommentRetention = Match->CommentRetention;
						D.Meta->Documentation = Match->Documentation;
					}
				}
			}
		}
		return true;
	}
	void VisitProgramExpressions(FProgram& P, const std::function<void(FExpr&)>& Visitor)
	{
		WalkProgram(P, Visitor, [](FStmt&)
		{
		});
	}
	void VisitBlockNodes(FBlockStmt& Block, const std::function<void(FExpr&)>& Expr,
	    const std::function<void(FStmt&)>& Stmt)
	{
		auto* Pointer = &Block;
		WalkStmt(Pointer, Expr, Stmt);
	}
	void VisitExpressionNodes(FExpr& Expression, const std::function<void(FExpr&)>& Expr)
	{
		std::unique_ptr<FExpr, void (*)(FExpr*)> Pointer(&Expression, [](FExpr*)
		{
		});
		WalkExpr(Pointer, Expr, [](FStmt&)
		{
		});
	}
	void VisitBlockNodes(const FBlockStmt& Block, const std::function<void(const FExpr&)>& Expr,
	    const std::function<void(const FStmt&)>& Stmt)
	{
		auto* Pointer = const_cast<FBlockStmt*>(&Block);
		WalkStmt(Pointer, [&](FExpr& E)
		{
			if (Expr)
			{
				Expr(E);
			}
		}, [&](FStmt& S)
		{
			if (Stmt)
			{
				Stmt(S);
			}
		});
	}
	void VisitStatementNodes(const FStmt& Statement, const std::function<void(const FExpr&)>& Expr,
	    const std::function<void(const FStmt&)>& Stmt)
	{
		auto* Pointer = const_cast<FStmt*>(&Statement);
		WalkStmt(Pointer, [&](FExpr& E)
		{
			if (Expr)
			{
				Expr(E);
			}
		}, [&](FStmt& S)
		{
			if (Stmt)
			{
				Stmt(S);
			}
		});
	}
	void VisitExpressionNodes(const FExpr& Expression, const std::function<void(const FExpr&)>& Expr,
	    const std::function<void(const FStmt&)>& Stmt)
	{
		std::unique_ptr<FExpr, void (*)(FExpr*)> Pointer(const_cast<FExpr*>(&Expression), [](FExpr*)
		{
		});
		WalkExpr(Pointer, [&](FExpr& E)
		{
			if (Expr)
			{
				Expr(E);
			}
		}, [&](FStmt& S)
		{
			if (Stmt)
			{
				Stmt(S);
			}
		});
	}
	void VisitProgramNodes(FProgram& P, const std::function<void(FExpr&)>& Expr, const std::function<void(FStmt&)>& Stmt)
	{
		WalkProgram(P, Expr, Stmt);
	}
	void VisitProgramNodes(const FProgram& P, const std::function<void(const FExpr&)>& Expr, const std::function<void(const FStmt&)>& Stmt)
	{
		// WalkProgram only follows owning pointers. Const callbacks expose no
		// mutable declarations and do not materialize or alter deferred syntax.
		WalkProgram(const_cast<FProgram&>(P), [&](FExpr& E)
		{
			Expr(E);
		}, [&](FStmt& S)
		{
			Stmt(S);
		});
	}

	void FinalizeCompileProcessing(FProgram& P)
	{
		if (!P.CompileProcessingSession) return;
		auto S = P.CompileProcessingSession;
		auto PolicyFor = [&](const std::string& Module)
		{
			auto Policy = S->Options.CommentRetention;
			for (const auto& M : P.PackageSourceModules)
			{
				if (M.bFromPackage && M.ModulePath == Module && M.InitialSourceMetadata)
				{
					Policy = static_cast<ECommentRetention>(std::max(static_cast<unsigned>(Policy),
					    static_cast<unsigned>(M.InitialSourceMetadata->Retention)));
				}
			}

			return Policy;
		};
		auto TrimComments = [](std::vector<FSourceComment>& Cs, ECommentRetention Policy)
		{
			if (Policy == ECommentRetention::NoComments || Policy == ECommentRetention::Unavailable)
			{
				Cs.clear();
			}

			else if (Policy == ECommentRetention::Documentation)
			{
				Cs.erase(std::remove_if(Cs.begin(), Cs.end(),
				             [](const auto& C)
				{
					return C.Kind != ESourceCommentKind::DocumentationLine &&
					    C.Kind != ESourceCommentKind::DocumentationBlock;
				}),
				    Cs.end());
			}
		};
		// Documentation keeps the already computed fallback text even when the
		// ordinary comments that supplied it are removed from the final collection.
		std::unordered_map<std::string, std::string> Documentation;
		for (const auto& N : S->Metadata->Nodes)
		{
			if (PolicyFor(N.ModuleId) == ECommentRetention::Documentation && !N.Documentation.empty())
			{
				Documentation.emplace(N.Id, N.Documentation);
			}
		}

		// Trim the shared source collection first: reassociation must not restore
		// comments that the producer or current compilation explicitly removed.
		for (auto& D : S->Metadata->Documents)
		{
			TrimComments(D.Comments, PolicyFor(D.ModuleId));
		}

		auto Declarations = Collect(P);
		for (auto& D : Declarations)
		{
			const auto Policy = S->Metadata->FindDocument(D.Meta->SourceRange.SourceId)
			    ? PolicyFor(D.View.ModulePath)
			    : ECommentRetention::Unavailable;
			D.Meta->CommentRetention = Policy;
			TrimComments(D.Meta->Comments, Policy);
			if (Policy == ECommentRetention::NoComments || Policy == ECommentRetention::Unavailable)
			{
				D.Meta->Documentation.clear();
			}
		}
		CollectProgramSourceMetadata(P);
		for (auto& D : Declarations)
		{
			if (D.Meta->CommentRetention == ECommentRetention::Documentation)
			{
				if (const auto I = Documentation.find(D.Meta->StableId); I != Documentation.end())
				{
					D.Meta->Documentation = I->second;
				}
			}
		}

		S->Metadata->Retention = S->Options.CommentRetention;
		for (auto& D : S->Metadata->Documents)
		{
			D.Text.clear();
		}

		for (auto& Module : P.PackageSourceModules)
		{
			if (!Module.bFromPackage && Module.InitialSourceMetadata)
			{
				auto Initial = std::make_shared<FSourceMetadataSnapshot>(*Module.InitialSourceMetadata);
				for (auto& Document : Initial->Documents)
				{
					Document.Text.clear();
				}
				Module.InitialSourceMetadata = std::move(Initial);
			}
		}

		for (auto& N : S->Metadata->Nodes)
		{
			const auto Policy = S->Metadata->FindDocument(N.Range.SourceId) ? PolicyFor(N.ModuleId)
			                                                                : ECommentRetention::Unavailable;
			N.CommentRetention = Policy;
			TrimComments(N.Comments, Policy);
			if (Policy == ECommentRetention::NoComments || Policy == ECommentRetention::Unavailable)
			{
				N.Documentation.clear();
			}

			else if (Policy == ECommentRetention::Documentation)
			{
				if (const auto I = Documentation.find(N.Id); I != Documentation.end())
				{
					N.Documentation = I->second;
				}
			}
		}
		S->Metadata->Nodes.erase(std::remove_if(S->Metadata->Nodes.begin(), S->Metadata->Nodes.end(),
		                             [](const auto& N)
		{
			return N.Kind != "declaration" && N.Kind != "module" &&
			    N.Annotations.empty() && N.Comments.empty() &&
			    N.Documentation.empty();
		}),
		    S->Metadata->Nodes.end());
		P.CompileProcessingSession.reset();
	}
}

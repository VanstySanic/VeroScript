// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAstClone.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <type_traits>

namespace VeroScript
{
	bool VeroScriptCompiler::ResolveTemplateMethodReference(const FMemberAccessExpr &Reference,
	    const FMethodDecl *&Method, std::string &Owner,
	    std::string &FunctionType) const
	{
		Method = nullptr;
		if (!Reference.Object || !ActiveProgram)
		{
			return false;
		}
		const bool TypeReceiver =
		    Reference.Object->Kind == EExprKind::Identifier &&
		    IsTypeNameWithoutValueBinding(static_cast<const FIdentifierExpr &>(*Reference.Object).Name);
		std::string Receiver;
		if (TypeReceiver)
		{
			Receiver = QualifyTypeName(static_cast<const FIdentifierExpr &>(*Reference.Object).Name, CurrentModulePath);
		}
		else if (!TryResolveExprTypeName(*Reference.Object, Receiver))
		{
			return false;
		}
		const bool Wrapped = GenericTemplateBaseLinkage(Receiver) == "handle";
		if (Wrapped)
		{
			const auto Arguments = DecodeGenericConcreteTypes(Receiver);
			if (Arguments.size() != 1)
			{
				return false;
			}
			Receiver = Arguments.front();
		}
		std::vector<std::string> Parameters;
		std::string Return;
		const bool Contextual = Runtime::ParseFunctionType(ExpressionExpectedType, Parameters, Return);
		std::unordered_map<std::string, std::string> OwnerBindings;
		TryBuildGenericOwnerSubst(Receiver, OwnerBindings, Reference.Location);
		if (!Contextual)
		{
			bool Found = false;
			std::vector<std::pair<std::string, unsigned>> Pending{{Receiver, 0}};
			std::unordered_set<std::string> Visited;
			for (std::size_t Work = 0; Work < Pending.size(); ++Work)
			{
				CheckAnalysisCancellation();
				const auto Current = Pending[Work];
				if (!Visited.insert(Current.first).second)
				{
					continue;
				}
				if (Current.second >= 64 || Pending.size() > 4096)
				{
					return false;
				}
				const auto Base = GenericTemplateBaseLinkage(Current.first);
				const auto &Lookup = Base.empty() ? Current.first : Base;
				const auto Arity = Base.empty() ? 0 : DecodeGenericConcreteTypes(Current.first).size();
				std::unordered_map<std::string, std::string> Enclosing;
				if (!TryBuildGenericOwnerSubst(Current.first, Enclosing, Reference.Location))
				{
					return false;
				}
				const auto Find = [&](const auto &Declarations)
				{
					for (const auto &Declaration : Declarations)
					{
						const auto Linkage =
						    GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name);
						if (Linkage != Lookup || TemplateParameterNames(Declaration).size() != Arity)
						{
							continue;
						}
						bool HidesName = false;
						for (const auto &Candidate : Declaration.Methods)
						{
							if (Candidate.Name != Reference.Member)
							{
								continue;
							}
							HidesName = true;
							if (TemplateParameterNames(Candidate).empty())
							{
								continue;
							}
							auto Concrete = Reference.TypeArgs;
							if (!CompleteTemplateArguments(Candidate.TemplateParameters, Declaration.ModulePath,
							        Concrete, Reference.Location, false, &Enclosing))
							{
								continue;
							}
							auto Bindings = Enclosing;
							for (std::size_t Index = 0; Index < Concrete.size(); ++Index)
							{
								Bindings[TemplateParameterNames(Candidate)[Index]] = Concrete[Index];
							}
							const auto CandidateParameters =
							    CanonicalMethodParamTypeNames(Candidate, Declaration.ModulePath, Bindings);
							if (Found && CandidateParameters != Parameters)
							{
								return false;
							}
							Parameters = CandidateParameters;
							Found = true;
						}
						const auto AddBase = [&](const std::string &Type)
						{
							Pending.emplace_back(
							    QualifyTypeName(SubstituteTypeNameWith(Type, Enclosing), Declaration.ModulePath),
							    Current.second + 1);
						};
						using TDeclaration = std::decay_t<decltype(Declaration)>;
						if constexpr (!std::is_same_v<TDeclaration, FInterfaceDecl>)
						{
							for (const auto &Using : Declaration.UsingMembers)
							{
								if (Using.MemberName == Reference.Member)
								{
									AddBase(Using.BaseTypeName);
								}
							}
						}
						if (HidesName)
						{
							return true;
						}
						if constexpr (std::is_same_v<TDeclaration, FClassDecl>)
						{
							for (const auto &Parent : Declaration.BaseTypeNames)
							{
								AddBase(Parent);
							}
						}
						else if constexpr (std::is_same_v<TDeclaration, FStructDecl>)
						{
							if (!Declaration.BaseTypeName.empty())
							{
								AddBase(Declaration.BaseTypeName);
							}
						}
						if constexpr (std::is_same_v<TDeclaration, FInterfaceDecl>)
						{
							for (const auto &Parent : Declaration.BaseInterfaceNames)
							{
								AddBase(Parent);
							}
						}
						else
						{
							for (const auto &Parent : Declaration.InterfaceTypeNames)
							{
								AddBase(Parent);
							}
						}
					}
					return true;
				};
				if (!Find(ActiveProgram->Classes) || !Find(ActiveProgram->Structs) || !Find(ActiveProgram->Interfaces))
				{
					return false;
				}
			}
			if (!Found)
			{
				return false;
			}
		}
		if (!ResolveMethodForCall(Receiver, Reference.Member, Parameters, Owner, Method, Reference.Location, false,
		        false, nullptr, !TypeReceiver && IsConstAssignTarget(*Reference.Object),
		        &Reference.TypeArgs, Contextual ? &Return : nullptr) ||
		    !Method)
		{
			return false;
		}
		if (TypeReceiver && !Method->bIsStatic)
		{
			return false;
		}
		const auto Key = MethodSpecializationKeys.find(Method);
		if (Key == MethodSpecializationKeys.end())
		{
			return false;
		}
		const auto &Instance = MethodSpecializations.at(Key->second);
		auto SignatureParameters = CanonicalMethodParamTypeNames(*Method, Instance.ModulePath, {});
		for (auto &Parameter : SignatureParameters)
		{
			if (Parameter.size() >= 3 && Parameter.compare(Parameter.size() - 3, 3, "...") == 0)
			{
				Parameter = MakeDynamicArrayTypeName(Parameter.substr(0, Parameter.size() - 3));
			}
		}
		FunctionType = Runtime::MakeFunctionTypeName(SignatureParameters,
		    QualifyTypeName(Method->ReturnType, Instance.ModulePath));
		return !Contextual || FunctionType == ExpressionExpectedType;
	}

	bool VeroScriptCompiler::CompileTemplateMethodReference(const FMemberAccessExpr &Reference, FEmitter &Emitter)
	{
		const FMethodDecl *Method = nullptr;
		std::string Owner, FunctionType;
		if (!ResolveTemplateMethodReference(Reference, Method, Owner, FunctionType))
		{
			ReportError("cannot close template method reference: " + Reference.Member, Reference.Location);
			return false;
		}
		if (Method->bDeleted)
		{
			ReportError("use of deleted template method: " + Reference.Member, Reference.Location);
			return false;
		}
		if (!ValidateMemberAccess(Reference.Location, Owner, Reference.Member, true, Method->Parameters.size(),
		        Method) ||
		    !EnsureMethodSpecialization(*Method, Owner, Reference.Location))
		{
			return false;
		}
		const auto &Instance = MethodSpecializations.at(MethodSpecializationKeys.at(Method));
		const bool TypeReceiver =
		    Reference.Object->Kind == EExprKind::Identifier &&
		    IsTypeNameWithoutValueBinding(static_cast<const FIdentifierExpr &>(*Reference.Object).Name);
		std::string Target;
		bool Wrapped = false;
		const bool StaticTarget = TypeReceiver || Method->bIsStatic;
		if (Method->bIsStatic && !TypeReceiver)
		{
			if (!CompileExpression(*Reference.Object))
			{
				return false;
			}
			Emitter.Emit(EOpcode::Pop);
		}
		if (StaticTarget)
		{
			Target = Owner;
		}
		else
		{
			Target = "$template_receiver" + std::to_string(NextLambdaId++);
			std::string Type;
			if (!TryResolveExprTypeName(*Reference.Object, Type))
			{
				return false;
			}
			Wrapped = GenericTemplateBaseLinkage(Type) == "handle";
			const auto Slot = AllocateLocal(Target);
			Scopes.back().LocalTypes[Target] = Type;
			if (IsConstAssignTarget(*Reference.Object))
			{
				Scopes.back().LocalConsts.insert(Target);
			}
			if (!CompileExpression(*Reference.Object))
			{
				return false;
			}
			if (NeedsPassByValueCopy(Type))
			{
				EmitValueCopy(Emitter, Type);
			}
			Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
		}
		FLambdaExpr Lambda;
		Lambda.Location = Reference.Location;
		Lambda.CaptureKind = ELambdaCaptureKind::Explicit;
		Lambda.bMutable = true;
		if (!StaticTarget)
		{
			Lambda.Captures.push_back({Target, false, nullptr, Reference.Location});
		}
		std::vector<std::string> Parameters;
		Runtime::ParseFunctionType(FunctionType, Parameters, Lambda.ReturnType);
		std::vector<std::unique_ptr<FExpr>> Arguments;
		for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
		{
			FFunctionParam Parameter;
			Parameter.Name = "$argument" + std::to_string(Index);
			Parameter.TypeName = std::string(Runtime::StripReferenceTypeName(Parameters[Index]));
			Parameter.bIsRef = Runtime::IsReferenceTypeName(Parameters[Index]);
			Parameter.bIsConst = Runtime::IsConstReferenceTypeName(Parameters[Index]);
			std::unique_ptr<FExpr> Argument = std::make_unique<FIdentifierExpr>(Parameter.Name);
			if (Parameter.bIsRef)
			{
				Argument = std::make_unique<FRefExpr>(std::move(Argument));
			}
			Argument->Location = Reference.Location;
			Arguments.push_back(std::move(Argument));
			Lambda.Parameters.push_back(std::move(Parameter));
		}
		std::unique_ptr<FExpr> Receiver = std::make_unique<FIdentifierExpr>(Target);
		if (Wrapped)
		{
			Receiver = std::make_unique<FMemberAccessExpr>(std::move(Receiver), "value");
		}
		auto Call = std::make_unique<FMethodCallExpr>(std::move(Receiver), Reference.Member, std::move(Arguments));
		Call->Location = Reference.Location;
		Call->bExplicitTemplateArguments = true;
		for (const auto &Name : TemplateParameterNames(*Instance.Declaration))
		{
			Call->TypeArgs.push_back(Instance.Bindings.at(Name));
		}
		Lambda.Body = std::make_unique<FBlockStmt>();
		if (Lambda.ReturnType == "void")
		{
			Lambda.Body->Statements.push_back(std::make_unique<FExprStmt>(std::move(Call)));
		}
		else
		{
			Lambda.Body->Statements.push_back(std::make_unique<FReturnStmt>(std::move(Call)));
		}
		const bool Compiled = CompileLambdaExpression(Lambda, Emitter, true);
		if (!StaticTarget)
		{
			Emitter.Emit(EOpcode::ConstNull);
			Emitter.EmitU16Operand(EOpcode::StoreLocal, Scopes.back().Locals.at(Target));
		}
		return Compiled;
	}

	bool VeroScriptCompiler::ResolveTemplateFunctionReference(const FIdentifierExpr &Reference,
	    const FFunctionDecl *&Function,
	    std::vector<std::string> &Arguments,
	    std::string &FunctionType) const
	{
		Function = nullptr;
		// A template-id still follows unqualified value lookup. Explicit namespace
		// qualification (including ::f<T>) keeps its existing free-function lookup.
		if (!Reference.bStandardLibraryQualified && Reference.Name.find_first_of(".:\x1F") == std::string::npos)
		{
			std::uint16_t Slot = 0;
			if (ResolveLocal(Reference.Name, Slot) || !ResolveLocalStaticLinkageName(Reference.Name).empty() || !ResolveEnumConstantLinkageName(Reference.Name, CurrentModulePath).empty() || ResolveThisField(Reference.Name, Slot) || IsForbiddenRuntimeCapture(Reference.Name))
			{
				return false;
			}
		}
		if (!ActiveProgram)
		{
			return false;
		}
		std::vector<std::string> ExpectedParameters;
		std::string ExpectedReturn;
		const bool Contextual = Runtime::ParseFunctionType(ExpressionExpectedType, ExpectedParameters, ExpectedReturn);
		const auto Names = BuildFreeFunctionLookupNames(Reference.Name, CurrentModulePath);
		bool Local = false;
		for (const auto &Declaration : ActiveProgram->Functions)
		{
			if (Declaration.ModulePath == CurrentModulePath && !TemplateParameterNames(Declaration).empty() &&
			    std::find(Names.begin(), Names.end(), Declaration.Name) != Names.end())
			{
				Local = true;
			}
		}
		for (const auto &Declaration : ActiveProgram->Functions)
		{
			CheckAnalysisCancellation();
			if (TemplateParameterNames(Declaration).empty() ||
			    std::find(Names.begin(), Names.end(), Declaration.Name) == Names.end() ||
			    (Local && Declaration.ModulePath != CurrentModulePath) ||
			    !CanAccessSymbol(Declaration.ModulePath, Declaration.bExported, CurrentModulePath))
			{
				continue;
			}
			std::vector<std::string> Concrete = Reference.TypeArgs;
			if (Contextual)
			{
				FFunctionDecl Signature;
				Signature.Name = Declaration.Name;
				Signature.ModulePath = Declaration.ModulePath;
				Signature.TypeParams = Declaration.TypeParams;
				Signature.TypeParamDefaults = Declaration.TypeParamDefaults;
				Signature.TemplateParameters = Declaration.TemplateParameters;
				for (const auto &Parameter : Declaration.Parameters)
				{
					Signature.Parameters.push_back(CloneFunctionParameter(Parameter));
				}
				auto Inference = ExpectedParameters;
				if (IsVariadicParamList(Signature.Parameters))
				{
					Signature.Parameters.back().TypeName =
					    MakeDynamicArrayTypeName(Signature.Parameters.back().TypeName);
					Signature.Parameters.back().bIsVariadic = false;
				}
				FFunctionParam Result;
				Result.TypeName = Declaration.ReturnType;
				Signature.Parameters.push_back(std::move(Result));
				Inference.push_back(ExpectedReturn);
				if (!TryInferGenericFunctionTypeParams(Signature, Inference, Reference.TypeArgs, Concrete))
				{
					continue;
				}
			}
			else if (!Declaration.TemplateParameters.IsEmpty())
			{
				if (!CompleteTemplateArguments(Declaration.TemplateParameters, Declaration.ModulePath, Concrete,
				        Reference.Location))
				{
					continue;
				}
			}
			else if (Concrete.size() != TemplateParameterNames(Declaration).size())
			{
				continue;
			}
			if (!SatisfiesTypeParamBounds(TemplateParameterNames(Declaration), Declaration.Bounds, Concrete,
			        Declaration.ModulePath))
			{
				continue;
			}
			std::unordered_map<std::string, std::string> Bindings;
			for (std::size_t Index = 0; Index < Concrete.size(); ++Index)
			{
				Bindings.emplace(TemplateParameterNames(Declaration)[Index], Concrete[Index]);
			}
			auto Parameters = SpecializedFreeFunctionParamTypes(Declaration, Concrete);
			for (auto &Parameter : Parameters)
			{
				if (Parameter.size() >= 3 && Parameter.compare(Parameter.size() - 3, 3, "...") == 0)
				{
					Parameter = MakeDynamicArrayTypeName(Parameter.substr(0, Parameter.size() - 3));
				}
			}
			const auto CandidateType = Runtime::MakeFunctionTypeName(
			    Parameters,
			    QualifyTypeName(SubstituteTypeNameWith(Declaration.ReturnType, Bindings), Declaration.ModulePath));
			if (Contextual && CandidateType != ExpressionExpectedType)
			{
				continue;
			}
			if (Function)
			{
				return false;
			}
			Function = &Declaration;
			Arguments = std::move(Concrete);
			FunctionType = CandidateType;
		}
		return Function != nullptr;
	}

	bool VeroScriptCompiler::CompileTemplateFunctionReference(const FIdentifierExpr &Reference, FEmitter &Emitter)
	{
		const FFunctionDecl *Function = nullptr;
		std::vector<std::string> Arguments;
		std::string FunctionType;
		if (!ResolveTemplateFunctionReference(Reference, Function, Arguments, FunctionType))
		{
			ReportError("cannot close template function reference: " + Reference.Name, Reference.Location);
			return false;
		}
		if (Function->bDeleted)
		{
			ReportError("use of deleted template function: " + Reference.Name, Reference.Location);
			return false;
		}
		std::uint16_t Index = 0;
		if (!EnsureGenericInstantiation(*Function, Arguments, Index, Reference.Location))
		{
			return false;
		}
		Emitter.EmitMakeClosure(Index, 0);
		return true;
	}
} // namespace VeroScript

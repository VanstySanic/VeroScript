// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAstClone.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptMethodKey.h"

namespace VeroScript
{
	const FMethodDecl *VeroScriptCompiler::SpecializeMethodSignature(
		const FMethodDecl &Method, const std::string &Owner, const std::string &DeclModulePath,
		const std::unordered_map<std::string, std::string> &OwnerBindings,
		const std::vector<std::string> &ArgumentTypes, const std::vector<std::string> *ExplicitArguments,
		const std::string *ExpectedReturnType) const
	{
		if (TemplateParameterNames(Method).empty())
		{
			return ExplicitArguments ? nullptr : &Method;
		}
		CheckAnalysisCancellation();
		FFunctionDecl Signature;
		Signature.Name = Method.Name;
		Signature.ModulePath = DeclModulePath;
		Signature.TypeParams = Method.TypeParams;
		Signature.TypeParamDefaults = Method.TypeParamDefaults;
		Signature.TemplateParameters = Method.TemplateParameters;
		Signature.ReturnType = SubstituteTypeNameWith(Method.ReturnType, OwnerBindings);
		Signature.Bounds = Method.Bounds;
		for (auto &Bound : Signature.Bounds)
		{
			Bound.InterfaceTypeName = SubstituteTypeNameWith(Bound.InterfaceTypeName, OwnerBindings);
		}
		for (auto &Default : Signature.TypeParamDefaults)
		{
			Default = SubstituteTypeNameWith(Default, OwnerBindings);
		}
		for (const auto &Parameter : Method.Parameters)
		{
			auto Copy = CloneFunctionParameter(Parameter);
			Copy.TypeName = SubstituteTypeNameWith(Parameter.TypeName, OwnerBindings);
			Signature.Parameters.push_back(std::move(Copy));
		}
		auto InferenceArguments = ArgumentTypes;
		const bool VariadicReference = ExpectedReturnType && IsVariadicParamList(Signature.Parameters);
		std::string VariadicType;
		if (VariadicReference)
		{
			VariadicType = Signature.Parameters.back().TypeName;
			Signature.Parameters.back().TypeName = MakeDynamicArrayTypeName(VariadicType);
			Signature.Parameters.back().bIsVariadic = false;
		}
		if (ExpectedReturnType)
		{
			FFunctionParam Return;
			Return.TypeName = Signature.ReturnType;
			Signature.Parameters.push_back(std::move(Return));
			InferenceArguments.push_back(*ExpectedReturnType);
		}
		std::vector<std::string> Arguments;
		if (!TryInferGenericFunctionTypeParams(Signature, InferenceArguments,
											   ExplicitArguments ? *ExplicitArguments : std::vector<std::string>{},
											   Arguments, &OwnerBindings))
		{
			return nullptr;
		}
		if (ExpectedReturnType)
		{
			Signature.Parameters.pop_back();
		}
		if (VariadicReference)
		{
			Signature.Parameters.back().TypeName = std::move(VariadicType);
			Signature.Parameters.back().bIsVariadic = true;
		}

		auto Bindings = OwnerBindings;
		for (std::size_t Index = 0; Index < TemplateParameterNames(Signature).size(); ++Index)
		{
			Bindings[TemplateParameterNames(Signature)[Index]] = Arguments[Index];
		}
		std::vector<std::string> BoundNames, BoundArguments;
		for (const auto &Binding : Bindings)
		{
			BoundNames.push_back(Binding.first);
			BoundArguments.push_back(Binding.second);
		}
		if (!SatisfiesTypeParamBounds(BoundNames, Signature.Bounds, BoundArguments, DeclModulePath))
		{
			return nullptr;
		}

		auto SymbolicBindings = OwnerBindings;
		for (std::size_t Index = 0; Index < TemplateParameterNames(Method).size(); ++Index)
		{
			SymbolicBindings[TemplateParameterNames(Method)[Index]] = "__template_parameter_" + std::to_string(Index);
		}
		auto DeclarationPatterns = CanonicalMethodParamTypeNames(Method, DeclModulePath, SymbolicBindings);
		for (const auto &Parameter : Method.TemplateParameters.GetParameters())
		{
			DeclarationPatterns.push_back(
				Parameter.Kind == ETemplateParameterKind::Type
					? "$type"
					: "$value:" + QualifyTypeName(SubstituteTypeNameWith(Parameter.ValueTypeName, SymbolicBindings),
												  DeclModulePath));
		}
		const std::string DeclarationKey = Method.Name + "$template" + std::to_string(Arguments.size()) +
										   (Method.bIsConst ? "$const" : "") +
										   EncodeMethodParamSuffix(DeclarationPatterns);
		const std::string Key = MangleGenericInstanceName(Owner + "::" + DeclarationKey, Arguments);
		const auto Existing = MethodSpecializations.find(Key);
		if (Existing != MethodSpecializations.end())
		{
			return Existing->second.Signature.get();
		}
		if (MethodSpecializations.size() >= 4096)
		{
			ReportError("method template specialization limit exceeded", Method.Location);
			return nullptr;
		}
		FMethodSpecialization Instance;
		Instance.Declaration = &Method;
		Instance.ModulePath = DeclModulePath;
		Instance.Bindings = std::move(Bindings);
		Instance.Signature = std::make_unique<FMethodDecl>();
		auto &Closed = *Instance.Signature;
		Closed.TypeParams = Method.TypeParams;
		Closed.TypeParamDefaults = Method.TypeParamDefaults;
		Closed.TemplateParameters = Method.TemplateParameters;
		Closed.Name = MangleGenericInstanceName(DeclarationKey, Arguments);
		Closed.ReturnType = SubstituteTypeNameWith(Signature.ReturnType, Instance.Bindings);
		Closed.Parameters = std::move(Signature.Parameters);
		for (auto &Parameter : Closed.Parameters)
		{
			Parameter.TypeName = SubstituteTypeNameWith(Parameter.TypeName, Instance.Bindings);
		}
		Closed.Access = Method.Access;
		Closed.bIsStatic = Method.bIsStatic;
		Closed.bIsConst = Method.bIsConst;
		Closed.bConstexpr = Method.bConstexpr;
		Closed.bDeleted = Method.bDeleted;
		Closed.DeclMeta = Method.DeclMeta;
		Closed.Location = Method.Location;
		const FMethodDecl *Result = Instance.Signature.get();
		MethodSpecializations.emplace(Key, std::move(Instance));
		MethodSpecializationKeys.emplace(Result, Key);
		return Result;
	}

	bool VeroScriptCompiler::EnsureMethodSpecialization(const FMethodDecl &Method, const std::string &Owner,
														const FSourceLocation &Location)
	{
		const auto Key = MethodSpecializationKeys.find(&Method);
		if (Key == MethodSpecializationKeys.end())
		{
			return true;
		}
		const auto &Instance = MethodSpecializations.at(Key->second);
		const auto &Declaration = *Instance.Declaration;
		const std::string Name = MakeMethodFunctionName(
			Owner, Method.Name, CanonicalMethodParamTypeNames(Method, Instance.ModulePath, {}), Method.bIsConst);
		if (Module->FunctionIndices.count(Name) != 0)
		{
			// CompileFunction publishes its signature before the body, allowing recursion.
			return true;
		}
		if (!Declaration.Body)
		{
			ReportError("template method has no definition: " + Declaration.Name, Location);
			return false;
		}
		std::string Linkage;
		std::uint16_t OwnerIndex = static_cast<std::uint16_t>(-1);
		if (!EnsureStructTypeResolved(Owner, Linkage, &OwnerIndex, Location) &&
			!EnsureInterfaceTypeResolved(Owner, Linkage, &OwnerIndex, Location))
		{
			ReportError("template method owner could not be resolved: " + Owner, Location);
			return false;
		}
		if (InstantiatingGenerics.size() >= 64)
		{
			ReportError("template instantiation nesting limit exceeded", Location);
			return false;
		}
		InstantiatingGenerics.insert(Name);

		struct FInstantiationScope
		{
			~FInstantiationScope()
			{
				Instances.erase(Name);
			}

			std::unordered_set<std::string> &Instances;
			const std::string &Name;
		} InstantiationScope{InstantiatingGenerics, Name};

		if (IsInterfaceTypeName(Owner))
		{
			OwnerIndex = static_cast<std::uint16_t>(-1);
		}
		const auto PreviousBindings = ActiveTypeSubst;
		const auto PreviousInterface = CurrentInterfaceOwnerLinkage;
		ActiveTypeSubst = Instance.Bindings;
		CurrentInterfaceOwnerLinkage = IsInterfaceTypeName(Owner) ? Owner : std::string{};
		const bool Result =
			CompileFunction(Name, Declaration.ReturnType, Declaration.Parameters, *Declaration.Body, !Method.bIsStatic,
							Method.bIsStatic ? 0 : OwnerIndex, Instance.ModulePath, Declaration.Location, false,
							nullptr, Method.bIsConst, &Declaration.DeclMeta, nullptr, false, Declaration.bConstexpr);
		ActiveTypeSubst = PreviousBindings;
		CurrentInterfaceOwnerLinkage = PreviousInterface;
		return Result;
	}
} // namespace VeroScript

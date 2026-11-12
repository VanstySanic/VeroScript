// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptScopedTypeEnvironments.h"
#include "VeroScriptAst.h"
#include "VeroScriptAnalysisControl.h"

#include <algorithm>
#include <functional>
#include <unordered_map>

namespace VeroScript
{
	bool BindScopedTypeEnvironments(FProgram& Program, FScriptError& Error)
	{
		struct FEnvironment
		{
			const FTypeDeclarationScope* Scope;
			const std::vector<std::string>* Parameters;
			std::vector<FTypeParamBound>* Bounds;
			std::string Parent;
			unsigned State = 0;
		};
		std::unordered_map<std::string, FEnvironment> Environments;
		const auto Key = [](const std::string& Module, const std::string& Name)
		{
			return Module + '\x1F' + Name;
		};
		const auto Index = [&](auto& Declarations)
		{
			for (auto& Declaration : Declarations)
			{
				if (Declaration.Scope.Id.empty())
				{
					continue;
				}
				const auto Parent = Declaration.DeclarationContext && Declaration.DeclarationContext->TypeScope
										? Declaration.DeclarationContext->TypeScope->Id
										: std::string{};
				Environments.emplace(
					Declaration.Scope.Id,
					FEnvironment{&Declaration.Scope, &Declaration.TypeParams, &Declaration.Bounds, Parent});
			}
		};
		Index(Program.Classes);
		Index(Program.Structs);
		Index(Program.Interfaces);
		std::unordered_map<std::string, const std::vector<FTypeParamBound>*> Functions;
		for (const auto& Function : Program.Functions)
		{
			if (Function.Body && !Function.Body->CallableOwner.empty())
			{
				Functions.emplace(Key(Function.ModulePath, Function.Body->CallableOwner), &Function.Bounds);
			}
		}
		std::function<bool(FEnvironment&, std::size_t)> Bind;
		Bind = [&](FEnvironment& Environment, const std::size_t Depth)
		{
			CheckAnalysisCancellation();
			if (Environment.State == 2)
			{
				return true;
			}
			if (Environment.State == 1 || Depth >= 64)
			{
				Error.Message = "cyclic or excessive lexical type environment";
				return false;
			}
			Environment.State = 1;
			const auto Append = [&](const std::vector<FTypeParamBound>& Bounds)
			{
				for (const auto& Bound : Bounds)
				{
					if (std::find(Environment.Parameters->begin(), Environment.Parameters->end(),
								  Bound.TypeParam) == Environment.Parameters->end())
					{
						continue;
					}
					if (std::none_of(Environment.Bounds->begin(), Environment.Bounds->end(),
									 [&](const auto& Existing)
									 {
										 return Existing.TypeParam == Bound.TypeParam &&
												Existing.InterfaceTypeName == Bound.InterfaceTypeName;
									 }))
					{
						Environment.Bounds->push_back(Bound);
					}
				}
			};
			const auto& Scope = *Environment.Scope;
			const auto Parent = Environments.find(Environment.Parent);
			if (Parent != Environments.end())
			{
				if (!Bind(Parent->second, Depth + 1))
				{
					return false;
				}
				Append(*Parent->second.Bounds);
			}
			const auto Function = Functions.find(Key(Scope.ModulePath, Scope.OwnerFunctionName));
			if (Function != Functions.end())
			{
				Append(*Function->second);
			}
			Environment.State = 2;
			return true;
		};
		for (auto& Environment : Environments)
		{
			if (!Bind(Environment.second, 0))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript

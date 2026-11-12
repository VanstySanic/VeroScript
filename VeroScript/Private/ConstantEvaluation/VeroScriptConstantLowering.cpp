// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptConstantEvaluation.h"
#include "VeroScriptAstClone.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptConstantBranches.h"

#include <unordered_map>
#include <type_traits>
#include <algorithm>

namespace VeroScript
{
	std::unique_ptr<FProgram> CloneProgramWithConstantValues(
	    const FProgram &Program, const FBytecodeModule &Module, std::string &Error)
	{
		Error.clear();
		auto Copy = CloneProgram(Program);
		const auto LocationKey = [](const FSourceLocation &L)
		{
			return std::string(L.FileLabel) + ':' + std::to_string(L.Line) + ':' + std::to_string(L.Column);
		};
		std::unordered_multimap<std::string, const FBytecodeModule::FCompiledConstant *> Constants;
		std::unordered_multimap<std::string, const FBytecodeModule::FCompiledDeclarationType *> Types;
		std::unordered_map<std::string, bool> Branches;
		for (const auto &Value : Module.CompiledConstants)
		{
			Constants.emplace(LocationKey(Value.Location), &Value);
		}
		for (const auto &Type : Module.CompiledDeclarationTypes)
		{
			Types.emplace(LocationKey(Type.Location), &Type);
		}
		for (const auto &Branch : Module.CompiledConstantBranches)
		{
			const auto Inserted = Branches.emplace(Branch.Identity, Branch.bThen);
			if (!Inserted.second && Inserted.first->second != Branch.bThen)
			{
				Error = "instance-dependent if constexpr requires core AOT";
			}
		}
		FConstantBranchState ConditionalScopes(Program);
		for (const auto &Branch : Module.CompiledConstantBranches)
		{
			ConditionalScopes.Selections[Branch.Identity] = Branch.bThen;
		}
		const auto DiscardTypes = [&](auto &Declarations)
		{
			Declarations.erase(std::remove_if(Declarations.begin(), Declarations.end(),
			                       [&](const auto &Declaration)
			{
				return !ConditionalScopes.Includes(Declaration.Scope.ScopeId);
			}),
			    Declarations.end());
		};
		DiscardTypes(Copy->Classes);
		DiscardTypes(Copy->Structs);
		DiscardTypes(Copy->Interfaces);
		DiscardTypes(Copy->Enums);
		DiscardTypes(Copy->TypeAliases);
		std::unordered_multimap<std::string, const FBytecodeFunction *> Functions;
		for (const auto &Function : Module.Functions)
		{
			Functions.emplace(LocationKey(Function.Location), &Function);
		}
		const auto MatchesName = [](const std::string &Compiled, const std::string &Source)
		{
			if (Compiled == Source) return true;
			if (Compiled.size() <= Source.size()) return false;
			const auto Boundary = Compiled.size() - Source.size();
			return (Compiled[Boundary - 1] == '.' || Compiled[Boundary - 1] == ':' || Compiled[Boundary - 1] == '\x1F') &&
			    Compiled.compare(Boundary, Source.size(), Source) == 0;
		};
		const auto Lower = [&](const std::string &Name, const FSourceLocation &Location,
		                       std::unique_ptr<FExpr> &Initializer)
		{
			CheckAnalysisCancellation();
			const auto Range = Constants.equal_range(LocationKey(Location));
			for (auto I = Range.first; I != Range.second; ++I)
			{
				if (MatchesName(I->second->Name, Name))
				{
					auto Value = I->second->Value;
					if (Value.Type.bEnum)
					{
						for (const auto &Declaration : Copy->Enums)
						{
							const auto Identity = Declaration.bExported || Declaration.ModulePath.empty()
							    ? Declaration.Name
							    : Declaration.ModulePath + '\x1F' + Declaration.Name;
							if (Identity != Value.Type.Name)
							{
								continue;
							}
							// A source cast uses the source declaration spelling in its own module.
							Value.Type.Name = Declaration.Name;
							break;
						}
					}
					Initializer = MakeConstantExpression(Value, Location);
					return;
				}
			}
			Error = "compiled constexpr value is unavailable for source emission: " + Name;
		};
		const auto LowerArrayType = [](std::string &Written, const std::string &Resolved)
		{
			const auto Opening = Written.find('['), BoundOpening = Resolved.find('[');
			if (Written.empty() || BoundOpening == std::string::npos) return;
			Written = Opening == std::string::npos ? Resolved : Written.substr(0, Opening) + Resolved.substr(BoundOpening);
		};
		VisitProgramNodes(*Copy, [&](FExpr &Expression)
		{
			if (Expression.Kind != EExprKind::Lambda) return;
			const auto Range = Functions.equal_range(LocationKey(Expression.Location));
			for (auto I = Range.first; I != Range.second; ++I)
			{
				if (I->second->Name.rfind("$L", 0) == 0)
				{
					static_cast<FLambdaExpr &>(Expression).bCapturesThis.store(I->second->bIsMethod, std::memory_order_release);
				}
			}
		}, [&](FStmt &Statement)
		{
			if (Statement.Kind == EStmtKind::If && static_cast<FIfStmt &>(Statement).bConstexpr)
			{
				auto &If = static_cast<FIfStmt &>(Statement);
				const auto Branch = Branches.find(FConstantBranchState::Key(If));
				if (Branch == Branches.end())
				{
					Error = "compiled if constexpr selection is unavailable for source emission";
					return;
				}
				auto Selected = Branch->second ? std::move(If.ThenBranch) : std::move(If.ElseBranch);
				If.ThenBranch = Selected ? std::move(Selected) : std::make_unique<FBlockStmt>();
				If.ElseBranch.reset();
				If.Condition = std::make_unique<FBooleanLiteralExpr>(true);
				If.Condition->Location = If.Location;
				If.bConstexpr = false;
			}
			if (Statement.Kind != EStmtKind::VarDecl) return;
			auto &Declaration = static_cast<FVarDeclStmt &>(Statement);
			if (Declaration.bConstexpr)
			{
				Lower(Declaration.Name, Declaration.Location, Declaration.Initializer);
			}
			const auto Range = Types.equal_range(LocationKey(Declaration.Location));
			for (auto I = Range.first; I != Range.second; ++I)
			{
				if (MatchesName(I->second->Name, Declaration.Name))
				{
					LowerArrayType(Declaration.TypeName, I->second->TypeName);
					if (Declaration.Initializer && Declaration.Initializer->Kind == EExprKind::ArrayInit)
					{
						LowerArrayType(static_cast<FArrayInitExpr &>(*Declaration.Initializer).TypeName, I->second->TypeName);
					}
					break;
				}
			}
		});
		const auto LowerSignature = [&](auto &Declaration)
		{
			const auto Range = Functions.equal_range(LocationKey(Declaration.Location));
			for (auto Candidate = Range.first; Candidate != Range.second; ++Candidate)
			{
				const auto &Function = *Candidate->second;
				const auto Separator = Function.Name.find('\x1F');
				const auto Signature = Function.Name.find('#', Separator == std::string::npos ? 0 : Separator + 1);
				if (!MatchesName(Function.Name.substr(0, Signature), Declaration.Name))
				{
					continue;
				}
				std::vector<std::string> Parameters;
				std::string Return;
				if (!Runtime::ParseFunctionType(Function.ClosureTypeName, Parameters, Return))
				{
					continue;
				}
				const std::size_t Receiver = Function.bIsMethod && !Function.bIsStaticMethod ? 1 : 0;
				if (Parameters.size() != Declaration.Parameters.size() + Receiver)
				{
					continue;
				}
				LowerArrayType(Declaration.ReturnType, Return);
				for (std::size_t I = 0; I < Declaration.Parameters.size(); ++I)
				{
					LowerArrayType(Declaration.Parameters[I].TypeName, Parameters[I + Receiver]);
				}
				break;
			}
		};
		for (auto &Function : Copy->Functions)
		{
			LowerSignature(Function);
		}
		const auto LowerTypes = [&](auto &Declarations, const auto &CompiledTypes)
		{
			using FBoundType = typename std::decay_t<decltype(CompiledTypes)>::value_type;
			std::unordered_map<std::string, const FBoundType *> BoundTypes;
			for (const auto &Type : CompiledTypes)
			{
				BoundTypes.emplace(Type.Name, &Type);
			}
			for (auto &Declaration : Declarations)
			{
				auto Bound = BoundTypes.find(Declaration.ModulePath + '\x1F' + Declaration.Name);
				if (Bound == BoundTypes.end())
				{
					Bound = BoundTypes.find(Declaration.Name);
				}
				for (auto &Method : Declaration.Methods)
				{
					LowerSignature(Method);
				}
				for (auto &Field : Declaration.Fields)
				{
					if (Field.bConstexpr)
					{
						Lower(Field.Name, Field.Location, Field.DefaultValue);
					}
					if (Bound != BoundTypes.end())
					{
						const auto &Fields = Field.bIsStatic ? Bound->second->StaticFields : Bound->second->Fields;
						for (const auto &BoundField : Fields)
						{
							if (BoundField.Name == Field.Name)
							{
								LowerArrayType(Field.TypeName, BoundField.TypeName);
							}
						}
					}
				}
			}
		};
		LowerTypes(Copy->Classes, Module.Types);
		LowerTypes(Copy->Structs, Module.Types);
		LowerTypes(Copy->Interfaces, Module.Interfaces);
		for (auto &Declaration : Copy->Enums)
		{
			for (const auto &Enum : Module.Enums)
			{
				if (Enum.Name == Declaration.Name || Enum.Name == Declaration.ModulePath + '\x1F' + Declaration.Name)
				{
					for (auto &Member : Declaration.Members)
					{
						for (const auto &Value : Enum.Members)
						{
							if (Member.Name == Value.Name)
							{
								Member.Value = Value.Value;
							}
						}
					}
				}
			}
		}
		return Error.empty() ? std::move(Copy) : nullptr;
	}
}

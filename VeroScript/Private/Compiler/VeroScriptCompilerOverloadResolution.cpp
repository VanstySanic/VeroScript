// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptCompilerDeclarationLookup.h"
#include "VeroScriptCompilerVariantInternal.h"
#include "VeroScriptCompilerTemplateOrdering.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_set>

namespace VeroScript
{
	const FFunctionDecl* VeroScriptCompiler::FindFunctionDeclForReference(
	    const std::string& Name,
	    const std::string& ReferenceModulePath) const
	{
		if (!ActiveProgram)
		{
			return nullptr;
		}

		const FFunctionDecl* SelectedFunction = nullptr;
		const std::vector<std::string> LookupNames =
		    BuildFreeFunctionLookupNames(Name, ReferenceModulePath);
		for (const FFunctionDecl& Function : ActiveProgram->Functions)
		{
			if (std::find(LookupNames.begin(), LookupNames.end(), Function.Name) == LookupNames.end() || !CanAccessSymbol(Function.ModulePath, Function.bExported, ReferenceModulePath))
			{
				continue;
			}
			if (Function.ModulePath == ReferenceModulePath)
			{
				return &Function;
			}
			if (!SelectedFunction)
			{
				SelectedFunction = &Function;
			}
		}
		return SelectedFunction;
	}

	const FFunctionDecl* VeroScriptCompiler::FindGenericFreeFunctionDecl(
	    const std::string& Name,
	    const std::string& ReferenceModulePath,
	    const std::vector<std::string>& TypeArgs,
	    const std::vector<std::string>& ArgTypeNames,
	    bool* OutAmbiguous) const
	{
		if (OutAmbiguous)
		{
			*OutAmbiguous = false;
		}
		if (!ActiveProgram || Name.empty())
		{
			return nullptr;
		}
		std::vector<const FFunctionDecl*> Candidates;
		const std::vector<std::string> LookupNames =
		    BuildFreeFunctionLookupNames(Name, ReferenceModulePath);
		for (const FFunctionDecl& Function : ActiveProgram->Functions)
		{
			if (std::find(LookupNames.begin(), LookupNames.end(), Function.Name) == LookupNames.end() || TypeArgs.size() > TemplateParameterNames(Function).size() || TemplateParameterNames(Function).empty() || !ParamsAcceptArgCount(Function.Parameters, ArgTypeNames.size()) || !CanAccessSymbol(Function.ModulePath, Function.bExported, ReferenceModulePath))
			{
				continue;
			}
			Candidates.push_back(&Function);
		}
		const bool bHasLocal = std::any_of(
		    Candidates.begin(),
		    Candidates.end(),
		    [&](const FFunctionDecl* Function)
		{
			return Function->ModulePath == ReferenceModulePath;
		});
		if (bHasLocal)
		{
			Candidates.erase(
			    std::remove_if(
			        Candidates.begin(),
			        Candidates.end(),
			        [&](const FFunctionDecl* Function)
			{
				return Function->ModulePath != ReferenceModulePath;
			}),
			    Candidates.end());
		}
		struct FScoredCandidate
		{
			const FFunctionDecl* Function = nullptr;
			int Score = -1;
		};
		std::vector<FScoredCandidate> Scored;
		std::vector<FScoredCandidate> BoundRejected;
		for (const FFunctionDecl* const Function : Candidates)
		{
			std::vector<std::string> ConcreteTypes;
			if (!TryInferGenericFunctionTypeParams(
			        *Function, ArgTypeNames, TypeArgs, ConcreteTypes))
			{
				continue;
			}
			const bool bBoundsSatisfied = SatisfiesTypeParamBounds(
			    TemplateParameterNames(*Function),
			    Function->Bounds,
			    ConcreteTypes,
			    Function->ModulePath);
			const int Score = ScoreSpecializedGenericCall(
			    *Function, ArgTypeNames, ConcreteTypes);
			if (Score >= 0)
			{
				auto& Destination = bBoundsSatisfied
				    ? Scored
				    : BoundRejected;
				Destination.push_back(FScoredCandidate{
				    Function,
				    Score});
			}
		}
		// Constraints participate in viability. If every otherwise-compatible
		// candidate fails them, keep the best rejected declaration so the normal
		// instantiation path can report the precise failed `where` clause instead
		// of degrading to "unknown function".
		if (Scored.empty())
		{
			Scored = std::move(BoundRejected);
			if (Scored.empty())
			{
				return nullptr;
			}
		}
		CompilerDetail::DiscardBoxingFallbacks(Scored, [](const auto& C)
		{
			return C.Score;
		}, [&](const auto& C)
		{
			std::vector<std::string> Concrete;
			return TryInferGenericFunctionTypeParams(*C.Function, ArgTypeNames, TypeArgs, Concrete) && CompilerDetail::NeedsVariantBoxing(ArgTypeNames, SpecializedFreeFunctionParamTypes(*C.Function, Concrete));
		});

		const auto Original = Scored;
		Scored.erase(std::remove_if(Scored.begin(), Scored.end(), [&](const auto& Candidate)
		{
			std::vector<std::string> Concrete;
			TryInferGenericFunctionTypeParams(*Candidate.Function, ArgTypeNames, TypeArgs, Concrete);
			const auto Parameters = CompilerDetail::CallParameterTypes(ArgTypeNames, SpecializedFreeFunctionParamTypes(*Candidate.Function, Concrete),
			    [&](const auto& Argument, const auto& Parameter, int& Score)
			{
				return TryScoreArgParamTypes(Argument, Parameter, Score);
			},
			    [&](const auto& Element)
			{
				return MakeDynamicArrayTypeName(Element);
			});
			return std::any_of(Original.begin(), Original.end(), [&](const auto& Other)
			{
				if (Other.Function == Candidate.Function) return false;
				std::vector<std::string> OtherConcrete;
				TryInferGenericFunctionTypeParams(*Other.Function, ArgTypeNames, TypeArgs, OtherConcrete);
				const auto OtherParameters = CompilerDetail::CallParameterTypes(ArgTypeNames, SpecializedFreeFunctionParamTypes(*Other.Function, OtherConcrete),
				    [&](const auto& Argument, const auto& Parameter, int& Score)
				{
					return TryScoreArgParamTypes(Argument, Parameter, Score);
				},
				    [&](const auto& Element)
				{
					return MakeDynamicArrayTypeName(Element);
				});
				if (PreferOperatorReferenceOverload(ArgTypeNames, OtherParameters, Parameters)) return true;
				if (PreferOperatorReferenceOverload(ArgTypeNames, Parameters, OtherParameters)) return false;
				for (std::size_t Index = 0; Index < ArgTypeNames.size(); ++Index)
				{
					if (Index >= OtherParameters.size() || Index >= Parameters.size()) return false;
					int OtherScore = 0, Score = 0;
					if (!TryScoreArgParamTypes(ArgTypeNames[Index], OtherParameters[Index], OtherScore) || !TryScoreArgParamTypes(ArgTypeNames[Index], Parameters[Index], Score) || OtherScore != Score)
					{
						return false;
					}
				}
				return CompilerDetail::MoreSpecializedTemplatePattern(TemplateParameterNames(*Other.Function),
				    CompilerDetail::TemplateFunctionPatterns(*Other.Function), TemplateParameterNames(*Candidate.Function),
				    CompilerDetail::TemplateFunctionPatterns(*Candidate.Function),
				    [&](const auto& Pattern, const auto& Argument, const auto& Parameter, auto& Binding)
				{
					return TryMatchTypeParam(Pattern, Argument, Parameter, Binding);
				});
			});
		}),
		    Scored.end());
		if (Scored.size() == 1)
		{
			return Scored.front().Function;
		}
		if (OutAmbiguous)
		{
			*OutAmbiguous = true;
		}
		return nullptr;
	}

	bool VeroScriptCompiler::ResolveGenericFreeFunctionForCall(
	    const std::string& Name,
	    const std::vector<std::string>& TypeArgs,
	    const std::vector<std::string>& ArgTypeNames,
	    const FFunctionDecl*& OutFunction,
	    const FSourceLocation& Location,
	    const bool bExplicitTemplateArguments)
	{
		OutFunction = nullptr;
		if (!ActiveProgram)
		{
			return false;
		}
		if (TypeArgs.empty() && !bExplicitTemplateArguments)
		{
			if (const auto* Concrete = SelectFreeFunctionOverload(Name, ArgTypeNames);
			    Concrete && !CompilerDetail::NeedsVariantBoxing(ArgTypeNames, CanonicalParamTypeNames(Concrete->Parameters, Concrete->ModulePath, {})))
			{
				return false;
			}
		}
		if (!TypeArgs.empty() || bExplicitTemplateArguments)
		{
			bool bAmbiguous = false;
			OutFunction = FindGenericFreeFunctionDecl(
			    Name,
			    CurrentModulePath,
			    TypeArgs,
			    ArgTypeNames,
			    &bAmbiguous);
			if (!OutFunction)
			{
				if (bAmbiguous)
				{
					ReportErrorId("diag.ambiguous_generic_function_call", Location, {Name});
					return false;
				}
				const std::string TypeArgCount = std::to_string(TypeArgs.size());
				ReportErrorId("diag.no_generic_function", Location, {Name, TypeArgCount});
				return false;
			}
			return true;
		}

		std::vector<std::string> ConcreteTypes;
		bool bAmbiguous = false;
		OutFunction = SelectGenericFreeFunctionOverload(
		    Name, ArgTypeNames, ConcreteTypes, &bAmbiguous, true);
		if (OutFunction)
		{
			return true;
		}
		if (bAmbiguous)
		{
			ReportErrorId("diag.ambiguous_generic_function_call", Location, {Name});
		}
		return false;
	}

	bool VeroScriptCompiler::ResolveFunctionForCall(
	    const std::string& Name,
	    const std::vector<std::string>& ArgTypeNames,
	    const FFunctionDecl*& OutFunction,
	    const FSourceLocation& Location)
	{
		OutFunction = nullptr;
		bool bAmbiguous = false;
		OutFunction = SelectFreeFunctionOverload(Name, ArgTypeNames, &bAmbiguous);
		if (bAmbiguous)
		{
			ReportErrorId("diag.ambiguous_call_to", Location, {Name});
			return false;
		}
		if (OutFunction && OutFunction->bDeleted)
		{
			ReportErrorId("diag.use_of_deleted_declaration", Location, {Name});
			OutFunction = nullptr;
			return false;
		}
		return OutFunction != nullptr;
	}

	const FFunctionDecl* VeroScriptCompiler::SelectFreeFunctionOverload(
	    const std::string& Name,
	    const std::vector<std::string>& ArgTypeNames,
	    bool* OutAmbiguous,
	    const bool bAllowTypeMismatchCandidate) const
	{
		if (OutAmbiguous)
		{
			*OutAmbiguous = false;
		}
		if (!ActiveProgram)
		{
			return nullptr;
		}
		const std::size_t ArgCount = ArgTypeNames.size();
		const std::vector<std::string> LookupNames =
		    BuildFreeFunctionLookupNames(Name, CurrentModulePath);
		const auto ScoreCandidate =
		    [&](const FFunctionDecl& Function, bool& Boxing) -> int
		{
			const bool bNameMatch = std::any_of(
			    LookupNames.begin(),
			    LookupNames.end(),
			    [&](const std::string& LookupName)
			{
				return FreeFunctionNameMatches(LookupName, Function.Name);
			});
			if (!bNameMatch || !TemplateParameterNames(Function).empty() || !ParamsAcceptArgCount(Function.Parameters, ArgCount) || !CanAccessSymbol(Function.ModulePath, Function.bExported, CurrentModulePath))
			{
				return -1;
			}
			const std::vector<std::string> ParamTypes =
			    CanonicalParamTypeNames(Function.Parameters, Function.ModulePath, {});
			Boxing = CompilerDetail::NeedsVariantBoxing(ArgTypeNames, ParamTypes);
			const bool bVariadic = IsVariadicParamList(Function.Parameters);
			const std::size_t FixedCount = FixedParamCount(Function.Parameters);
			int Score = 0;
			bool bAnyTypedArg = false;
			bool bTypeMismatch = false;
			const auto ScorePair =
			    [&](const std::string& ArgTypeName, const std::string& ParamCanonical)
			{
				if (ArgTypeName.empty())
				{
					return;
				}
				bAnyTypedArg = true;
				int Contribution = 0;
				if (!TryScoreArgParamTypes(ArgTypeName, ParamCanonical, Contribution))
				{
					bTypeMismatch = true;
					return;
				}
				Score += Contribution;
			};
			const std::size_t FixedCompare = (std::min)(ArgCount, FixedCount);
			for (std::size_t Index = 0; Index < FixedCompare; ++Index)
			{
				ScorePair(ArgTypeNames[Index], ParamTypes[Index]);
			}
			if (bVariadic && ArgCount > FixedCount)
			{
				std::string ElementType = Function.Parameters.back().TypeName;
				ElementType = CanonicalTypeNameForCoercion(
				    QualifyTypeName(
				        NormalizeTypeAlias(ElementType, Function.ModulePath),
				        Function.ModulePath));
				const std::string ArrayType = MakeDynamicArrayTypeName(ElementType);
				if (ArgCount == FixedCount + 1)
				{
					const std::string& RestArg = ArgTypeNames[FixedCount];
					if (!RestArg.empty())
					{
						bAnyTypedArg = true;
						int Contribution = 0;
						if (TryScoreArgParamTypes(RestArg, ArrayType, Contribution) || TryScoreArgParamTypes(RestArg, ElementType, Contribution))
						{
							// Preserve historic strength: pack rest accepts element or array.
							Score += 2;
						}
						else
						{
							bTypeMismatch = true;
						}
					}
				}
				else
				{
					for (std::size_t Index = FixedCount; Index < ArgCount; ++Index)
					{
						ScorePair(ArgTypeNames[Index], ElementType);
					}
				}
			}
			if (bTypeMismatch)
			{
				return -2;
			}
			if (!bAnyTypedArg)
			{
				return 0;
			}
			return Score;
		};

		struct FScoredCandidate
		{
			int first;
			const FFunctionDecl* second;
			bool Boxing;
			FScoredCandidate(int Score, const FFunctionDecl* Function, bool NeedsBoxing) : first(Score), second(Function), Boxing(NeedsBoxing)
			{
			}
		};
		std::vector<FScoredCandidate> Scored;
		for (const FFunctionDecl& Function : ActiveProgram->Functions)
		{
			bool Boxing = false;
			const int Score = ScoreCandidate(Function, Boxing);
			if (Score >= 0 || Score == -2)
			{
				Scored.emplace_back(Score, &Function, Boxing);
			}
		}
		if (Scored.empty())
		{
			return nullptr;
		}
		CompilerDetail::DiscardBoxingFallbacks(Scored, [](const auto& C)
		{
			return C.first;
		}, [&](const auto& C)
		{
			return C.Boxing;
		});
		struct FUnifiedCandidate
		{
			const FFunctionDecl* Function;
			std::vector<std::string> Arguments, Parameters;
			int Score;
			bool Generic;
		};
		std::vector<FUnifiedCandidate> Unified;
		for (const auto& Candidate : Scored)
		{
			if (Candidate.first >= 0)
			{
				Unified.push_back({Candidate.second, {}, CanonicalParamTypeNames(Candidate.second->Parameters, Candidate.second->ModulePath, {}), Candidate.first, false});
			}
		}
		for (const auto& Function : ActiveProgram->Functions)
		{
			if (TemplateParameterNames(Function).empty() || !CanAccessSymbol(Function.ModulePath, Function.bExported, CurrentModulePath) || !std::any_of(LookupNames.begin(), LookupNames.end(), [&](const auto& Lookup)
			{
				return FreeFunctionNameMatches(Lookup, Function.Name);
			}))
			{
				continue;
			}
			std::vector<std::string> Arguments;
			if (!TryInferGenericFunctionTypeParams(Function, ArgTypeNames, Arguments) || !SatisfiesTypeParamBounds(TemplateParameterNames(Function), Function.Bounds, Arguments, Function.ModulePath))
			{
				continue;
			}
			const int Score = ScoreSpecializedGenericCall(Function, ArgTypeNames, Arguments);
			if (Score >= 0)
			{
				auto Parameters = SpecializedFreeFunctionParamTypes(Function, Arguments);
				Unified.push_back({&Function, std::move(Arguments), std::move(Parameters), Score, true});
			}
		}
		if (!Unified.empty())
		{
			CompilerDetail::DiscardBoxingFallbacks(Unified, [](const auto& Candidate)
			{
				return Candidate.Score;
			}, [&](const auto& Candidate)
			{
				return CompilerDetail::NeedsVariantBoxing(ArgTypeNames, Candidate.Parameters);
			});
			for (auto& Candidate : Unified)
			{
				Candidate.Parameters = CompilerDetail::CallParameterTypes(ArgTypeNames, std::move(Candidate.Parameters),
				    [&](const auto& Argument, const auto& Parameter, int& Score)
				{
					return TryScoreArgParamTypes(Argument, Parameter, Score);
				},
				    [&](const auto& Element)
				{
					return MakeDynamicArrayTypeName(Element);
				});
			}
			const auto Original = Unified;
			Unified.erase(std::remove_if(Unified.begin(), Unified.end(), [&](const auto& Candidate)
			{
				return std::any_of(Original.begin(), Original.end(), [&](const auto& Other)
				{
					if (Other.Function == Candidate.Function) return false;
					if (PreferOperatorReferenceOverload(ArgTypeNames, Other.Parameters, Candidate.Parameters)) return true;
					if (PreferOperatorReferenceOverload(ArgTypeNames, Candidate.Parameters, Other.Parameters)) return false;
					for (std::size_t Index = 0; Index < ArgTypeNames.size(); ++Index)
					{
						if (Index >= Candidate.Parameters.size() || Index >= Other.Parameters.size()) return false;
						int Left = 0, Right = 0;
						if (!TryScoreArgParamTypes(ArgTypeNames[Index], Other.Parameters[Index], Left) || !TryScoreArgParamTypes(ArgTypeNames[Index], Candidate.Parameters[Index], Right) || Left != Right)
						{
							return false;
						}
					}
					if (Other.Generic != Candidate.Generic) return !Other.Generic;
					return Other.Generic && CompilerDetail::MoreSpecializedTemplatePattern(TemplateParameterNames(*Other.Function), CompilerDetail::TemplateFunctionPatterns(*Other.Function), TemplateParameterNames(*Candidate.Function), CompilerDetail::TemplateFunctionPatterns(*Candidate.Function), [&](const auto& Pattern, const auto& Argument, const auto& Parameter, auto& Binding)
					{
						return TryMatchTypeParam(Pattern, Argument, Parameter, Binding);
					});
				});
			}),
			    Unified.end());
			if (Unified.size() != 1)
			{
				if (OutAmbiguous)
				{
					*OutAmbiguous = true;
				}
				return nullptr;
			}
			return Unified.front().Generic ? nullptr : Unified.front().Function;
		}
		int Best = std::numeric_limits<int>::min();
		for (const auto& Entry : Scored)
		{
			Best = std::max(Best, Entry.first);
		}
		if (Best >= 0)
		{
			const bool bCompareOperatorConversions = Name.rfind("operator", 0) == 0 && std::none_of(Scored.begin(), Scored.end(), [](const auto& Entry)
			{
				return Entry.first >= 0 && IsVariadicParamList(Entry.second->Parameters);
			});
			std::vector<const FFunctionDecl*> Ties;
			for (const auto& Entry : Scored)
			{
				if (Entry.first >= 0 && (bCompareOperatorConversions || Entry.first == Best))
				{
					Ties.push_back(Entry.second);
				}
			}
			const auto OriginalTies = Ties;
			Ties.erase(std::remove_if(Ties.begin(), Ties.end(), [&](const FFunctionDecl* Candidate)
			{
				const auto Params = CanonicalParamTypeNames(Candidate->Parameters, Candidate->ModulePath, {});
				return std::any_of(OriginalTies.begin(), OriginalTies.end(), [&](const FFunctionDecl* Other)
				{
					return Other != Candidate && PreferOperatorReferenceOverload(ArgTypeNames, CanonicalParamTypeNames(Other->Parameters, Other->ModulePath, {}), Params);
				});
			}),
			    Ties.end());
			if (Ties.size() > 1)
			{
				if (OutAmbiguous)
				{
					*OutAmbiguous = true;
				}
				return nullptr;
			}
			return Ties[0];
		}
		if (bAllowTypeMismatchCandidate && Best == -2)
		{
			std::vector<const FFunctionDecl*> Weak;
			for (const auto& Entry : Scored)
			{
				if (Entry.first == -2)
				{
					Weak.push_back(Entry.second);
				}
			}
			if (Weak.size() == 1)
			{
				return Weak[0];
			}
			// Multiple arity-matched but type-mismatched overloads (e.g. nullptr to A|B).
			if (OutAmbiguous)
			{
				*OutAmbiguous = true;
			}
			return nullptr;
		}
		return nullptr;
	}

	bool VeroScriptCompiler::TryInferGenericFunctionTypeParams(
	    const FFunctionDecl& Function,
	    const std::vector<std::string>& ArgTypeNames,
	    std::vector<std::string>& OutConcreteTypes) const
	{
		return TryInferGenericFunctionTypeParams(
		    Function, ArgTypeNames, {}, OutConcreteTypes);
	}

	bool VeroScriptCompiler::TryInferGenericFunctionTypeParams(
	    const FFunctionDecl& Function,
	    const std::vector<std::string>& ArgTypeNames,
	    const std::vector<std::string>& ExplicitTypeArgs,
	    std::vector<std::string>& OutConcreteTypes,
	    const std::unordered_map<std::string, std::string>* EnclosingBindings) const
	{
		OutConcreteTypes.clear();
		if (TemplateParameterNames(Function).empty() || ExplicitTypeArgs.size() > TemplateParameterNames(Function).size() || !ParamsAcceptArgCount(Function.Parameters, ArgTypeNames.size()))
		{
			return false;
		}
		OutConcreteTypes.resize(TemplateParameterNames(Function).size());
		std::vector<std::pair<std::size_t, std::string>> AdditionalValues;
		for (std::size_t Index = 0; Index < ExplicitTypeArgs.size(); ++Index)
		{
			const bool Value = !Function.TemplateParameters.IsEmpty() && Function.TemplateParameters.GetParameters()[Index].Kind == ETemplateParameterKind::Value;
			OutConcreteTypes[Index] = Value ? ExplicitTypeArgs[Index]
			                                : QualifyTypeName(SubstituteTypeName(ExplicitTypeArgs[Index]), CurrentModulePath);
			if (!Value && Function.TemplateParameters.IsEmpty())
			{
				OutConcreteTypes[Index] = CanonicalTypeNameForCoercion(OutConcreteTypes[Index]);
			}
			if (OutConcreteTypes[Index].empty())
			{
				return false;
			}
		}
		const bool bVariadic = IsVariadicParamList(Function.Parameters);
		const std::size_t FixedCount = FixedParamCount(Function.Parameters);
		const auto StripRefPrefix = [](const std::string& TypeName) -> std::string
		{
			if (TypeName.rfind("const lvalue ", 0) == 0) return TypeName.substr(13);
			if (TypeName.rfind("lvalue ", 0) == 0 || TypeName.rfind("rvalue ", 0) == 0)
			{
				return TypeName.substr(7);
			}
			if (TypeName.rfind("const ref ", 0) == 0)
			{
				return TypeName.substr(10);
			}
			if (TypeName.rfind("ref ", 0) == 0)
			{
				return TypeName.substr(4);
			}
			return TypeName;
		};
		for (std::size_t ArgumentIndex = 0; ArgumentIndex < ArgTypeNames.size(); ++ArgumentIndex)
		{
			const std::string& ArgTypeName = ArgTypeNames[ArgumentIndex];
			if (ArgTypeName.empty())
			{
				return false;
			}
			const bool bVariadicArgument = bVariadic && ArgumentIndex >= FixedCount;
			if (!bVariadicArgument && ArgumentIndex >= Function.Parameters.size())
			{
				return false;
			}
			const std::string ParamPattern = StripRefPrefix(
			    bVariadicArgument
			        ? Function.Parameters.back().TypeName
			        : Function.Parameters[ArgumentIndex].TypeName);
			const std::string ArgType = CanonicalTypeNameForCoercion(
			    StripRefPrefix(ArgTypeName));
			for (std::size_t TypeIndex = 0; TypeIndex < TemplateParameterNames(Function).size(); ++TypeIndex)
			{
				if (TypeIndex < ExplicitTypeArgs.size())
				{
					continue;
				}
				std::string Candidate;
				bool bMatched = false;
				if (bVariadicArgument && ArgTypeNames.size() == FixedCount + 1)
				{
					bMatched = TryMatchTypeParam(
					    MakeDynamicArrayTypeName(ParamPattern),
					    ArgType,
					    TemplateParameterNames(Function)[TypeIndex],
					    Candidate);
				}
				if (!bMatched)
				{
					bMatched = TryMatchTypeParam(
					    ParamPattern,
					    ArgType,
					    TemplateParameterNames(Function)[TypeIndex],
					    Candidate);
				}
				if (!bMatched)
				{
					continue;
				}
				Candidate = CanonicalTypeNameForCoercion(Candidate);
				if (OutConcreteTypes[TypeIndex].empty())
				{
					OutConcreteTypes[TypeIndex] = std::move(Candidate);
				}
				else if (OutConcreteTypes[TypeIndex] != Candidate)
				{
					if (Function.TemplateParameters.IsEmpty() || Function.TemplateParameters.GetParameters()[TypeIndex].Kind != ETemplateParameterKind::Value)
					{
						return false;
					}
					AdditionalValues.emplace_back(TypeIndex, std::move(Candidate));
				}
			}
		}
		if (!Function.TemplateParameters.IsEmpty())
		{
			if (!CompleteTemplateArguments(Function.TemplateParameters, Function.ModulePath,
			        OutConcreteTypes, Function.Location, false, EnclosingBindings, true))
			{
				return false;
			}
			for (const auto& Candidate : AdditionalValues)
			{
				FTemplateArgument Expected;
				if (!DecodeTemplateArgument(std::string_view(OutConcreteTypes[Candidate.first]).substr(7), Expected)) return false;
				auto Value = EvaluateTemplateSource(Candidate.second, CurrentModulePath, ActiveTypeSubst, Function.Location);
				if (!Value.Succeeded()) return false;
				Value = ConvertTemplateConstant(Value.Value, Expected.Value.Type);
				if (!Value.Succeeded() || Value.Value.Bits != Expected.Value.Bits) return false;
			}
			return true;
		}
		if (!TemplateParameterDefaults(Function).empty() && TemplateParameterDefaults(Function).size() != TemplateParameterNames(Function).size())
		{
			return false;
		}
		std::unordered_map<std::string, std::string> Subst;
		Subst.reserve(TemplateParameterNames(Function).size());
		for (std::size_t Index = 0; Index < TemplateParameterNames(Function).size(); ++Index)
		{
			std::string& ConcreteType = OutConcreteTypes[Index];
			if (ConcreteType.empty())
			{
				const std::string DefaultType =
				    Index < TemplateParameterDefaults(Function).size()
				    ? TemplateParameterDefaults(Function)[Index]
				    : std::string{};
				if (DefaultType.empty())
				{
					return false;
				}
				ConcreteType = CanonicalTypeNameForCoercion(
				    QualifyTypeName(
				        SubstituteTypeNameWith(
				            DefaultType, Subst),
				        Function.ModulePath));
				if (ConcreteType.empty())
				{
					return false;
				}
			}
			Subst.emplace(TemplateParameterNames(Function)[Index], ConcreteType);
		}
		return true;
	}

	int VeroScriptCompiler::ScoreSpecializedGenericCall(
	    const FFunctionDecl& Function,
	    const std::vector<std::string>& ArgTypeNames,
	    const std::vector<std::string>& ConcreteTypes) const
	{
		if (!ParamsAcceptArgCount(Function.Parameters, ArgTypeNames.size()))
		{
			return -1;
		}
		const std::vector<std::string> ParamTypes =
		    SpecializedFreeFunctionParamTypes(Function, ConcreteTypes);
		if (ParamTypes.size() != Function.Parameters.size())
		{
			return -1;
		}
		const bool bVariadic = IsVariadicParamList(Function.Parameters);
		const std::size_t FixedCount = FixedParamCount(Function.Parameters);
		int Score = 0;
		const auto AddScore = [&](const std::string& ArgType, const std::string& ParamType) -> bool
		{
			if (ArgType.empty())
			{
				return true;
			}
			int Contribution = 0;
			if (!TryScoreArgParamTypes(ArgType, ParamType, Contribution))
			{
				return false;
			}
			Score += Contribution;
			return true;
		};
		for (std::size_t Index = 0;
		    Index < (std::min)(ArgTypeNames.size(), FixedCount);
		    ++Index)
		{
			if (!AddScore(ArgTypeNames[Index], ParamTypes[Index]))
			{
				return -1;
			}
		}
		if (!bVariadic || ArgTypeNames.size() <= FixedCount)
		{
			return Score;
		}
		std::string ElementType = ParamTypes.back();
		if (ElementType.size() >= 3 && ElementType.compare(ElementType.size() - 3, 3, "...") == 0)
		{
			ElementType.resize(ElementType.size() - 3);
		}
		if (ArgTypeNames.size() == FixedCount + 1)
		{
			int ElementContribution = 0;
			int ArrayContribution = 0;
			const bool bElement = TryScoreArgParamTypes(
			    ArgTypeNames[FixedCount], ElementType, ElementContribution);
			const bool bArray = TryScoreArgParamTypes(
			    ArgTypeNames[FixedCount],
			    MakeDynamicArrayTypeName(ElementType),
			    ArrayContribution);
			if (!bElement && !bArray)
			{
				return -1;
			}
			return Score + (std::max)(ElementContribution, ArrayContribution);
		}
		for (std::size_t Index = FixedCount; Index < ArgTypeNames.size(); ++Index)
		{
			if (!AddScore(ArgTypeNames[Index], ElementType))
			{
				return -1;
			}
		}
		return Score;
	}

	bool VeroScriptCompiler::SatisfiesTypeParamBounds(
	    const std::vector<std::string>& TypeParams,
	    const std::vector<FTypeParamBound>& Bounds,
	    const std::vector<std::string>& ConcreteTypes,
	    const std::string& ModulePath) const
	{
		if (Bounds.empty())
		{
			return true;
		}
		if (TypeParams.size() != ConcreteTypes.size())
		{
			return false;
		}
		std::unordered_map<std::string, std::string> LocalSubst;
		LocalSubst.reserve(TypeParams.size());
		for (std::size_t Index = 0; Index < TypeParams.size(); ++Index)
		{
			LocalSubst[TypeParams[Index]] = ConcreteTypes[Index];
		}
		const std::string PreviousModulePath = CurrentModulePath;
		CurrentModulePath = ModulePath;
		const bool PreviousHasError = bHasError;
		const FScriptError PreviousError = Error;
		const std::size_t PreviousDiagCount = Diagnostics.size();
		const auto RestoreSilentState = [&]()
		{
			bHasError = PreviousHasError;
			Error = PreviousError;
			if (Diagnostics.size() > PreviousDiagCount)
			{
				Diagnostics.resize(PreviousDiagCount);
			}
		};
		bool bSatisfied = true;
		for (const FTypeParamBound& Bound : Bounds)
		{
			std::size_t ParamIndex = static_cast<std::size_t>(-1);
			for (std::size_t Index = 0; Index < TypeParams.size(); ++Index)
			{
				if (TypeParams[Index] == Bound.TypeParam)
				{
					ParamIndex = Index;
					break;
				}
			}
			if (ParamIndex == static_cast<std::size_t>(-1))
			{
				bSatisfied = false;
				break;
			}
			const std::string& Concrete = ConcreteTypes[ParamIndex];
			const std::string BoundAsWritten = SubstituteTypeNameWith(
			    Bound.InterfaceTypeName,
			    LocalSubst);
			std::string ResolvedIface;
			// Ensure may instantiate generic interfaces into Module (non-const).
			// Failures must not leak diagnostics — this helper is silent by contract.
			if (!const_cast<VeroScriptCompiler*>(this)->EnsureInterfaceTypeResolved(
			        QualifyTypeName(
			            NormalizeTypeAlias(BoundAsWritten, ModulePath),
			            ModulePath),
			        ResolvedIface,
			        nullptr,
			        {}))
			{
				RestoreSilentState();
				bSatisfied = false;
				break;
			}
			const std::string ConcreteCanonical = CanonicalTypeNameForCoercion(
			    QualifyTypeName(
			        NormalizeTypeAlias(Concrete, PreviousModulePath),
			        PreviousModulePath));
			const std::string InterfaceCanonical = CanonicalTypeNameForCoercion(ResolvedIface);
			if (!IsSubtypeOf(ConcreteCanonical, InterfaceCanonical))
			{
				RestoreSilentState();
				bSatisfied = false;
				break;
			}
		}
		CurrentModulePath = PreviousModulePath;
		RestoreSilentState();
		return bSatisfied;
	}

	const FFunctionDecl* VeroScriptCompiler::SelectGenericFreeFunctionOverload(
	    const std::string& Name,
	    const std::vector<std::string>& ArgTypeNames,
	    std::vector<std::string>& OutConcreteTypes,
	    bool* OutAmbiguous,
	    const bool bIncludeBoundRejected) const
	{
		OutConcreteTypes.clear();
		const auto* Function = FindGenericFreeFunctionDecl(Name, CurrentModulePath, {}, ArgTypeNames, OutAmbiguous);
		if (!Function || !TryInferGenericFunctionTypeParams(*Function, ArgTypeNames, OutConcreteTypes)) return nullptr;
		if (!bIncludeBoundRejected && !SatisfiesTypeParamBounds(TemplateParameterNames(*Function), Function->Bounds, OutConcreteTypes, Function->ModulePath))
		{
			return nullptr;
		}
		return Function;
	}

	bool VeroScriptCompiler::HasExtensionMethodCandidate(
	    const std::string& Name,
	    const std::vector<std::string>& TypeArgs,
	    const std::vector<std::string>& ArgTypeNames) const
	{
		bool bAmbiguous = false;
		if (!TypeArgs.empty())
		{
			return FindGenericFreeFunctionDecl(
			           Name,
			           CurrentModulePath,
			           TypeArgs,
			           ArgTypeNames,
			           &bAmbiguous) != nullptr ||
			    bAmbiguous;
		}

		if (SelectFreeFunctionOverload(
		        Name,
		        ArgTypeNames,
		        &bAmbiguous,
		        false) != nullptr ||
		    bAmbiguous)
		{
			return true;
		}

		std::vector<std::string> ConcreteTypes;
		return SelectGenericFreeFunctionOverload(
		           Name,
		           ArgTypeNames,
		           ConcreteTypes,
		           &bAmbiguous) != nullptr ||
		    bAmbiguous;
	}

	bool VeroScriptCompiler::ResolveFreeOperatorOverload(
	    const std::string& Name,
	    const std::vector<std::string>& ArgTypeNames,
	    const FFunctionDecl*& OutFunction,
	    std::vector<std::string>& OutGenericConcreteTypes,
	    bool* OutAmbiguous,
	    const bool bAllowTypeMismatchCandidate) const
	{
		OutFunction = nullptr;
		OutGenericConcreteTypes.clear();
		OutFunction = SelectFreeFunctionOverload(
		    Name,
		    ArgTypeNames,
		    OutAmbiguous,
		    bAllowTypeMismatchCandidate);
		if (OutFunction || (OutAmbiguous && *OutAmbiguous))
		{
			return OutFunction != nullptr;
		}
		OutFunction = SelectGenericFreeFunctionOverload(
		    Name,
		    ArgTypeNames,
		    OutGenericConcreteTypes,
		    OutAmbiguous);
		return OutFunction != nullptr;
	}

	std::vector<std::string> VeroScriptCompiler::SpecializedFreeFunctionParamTypes(
	    const FFunctionDecl& Function,
	    const std::vector<std::string>& GenericConcreteTypes) const
	{
		auto& Compiler = *const_cast<VeroScriptCompiler*>(this);
		FFunctionDeclarationLookupScope Lookup(Compiler.Scopes, Compiler.LocalTypeAliasScopes, Compiler.CurrentFunction,
		    Compiler.CurrentModulePath, Compiler.CurrentNamespacePrefix, Compiler.CurrentOwnerTypeLinkage,
		    Compiler.CurrentAccessFunctionLinkage, Compiler.CurrentEmitLocation, Function);

		std::vector<std::string> ParamTypes;
		if (TemplateParameterNames(Function).empty() || GenericConcreteTypes.size() != TemplateParameterNames(Function).size())
		{
			return CanonicalParamTypeNames(Function.Parameters, Function.ModulePath, {});
		}
		std::unordered_map<std::string, std::string> FunctionSubst;
		for (std::size_t TypeIndex = 0; TypeIndex < TemplateParameterNames(Function).size(); ++TypeIndex)
		{
			FunctionSubst.emplace(TemplateParameterNames(Function)[TypeIndex], GenericConcreteTypes[TypeIndex]);
		}
		// Keep encoding in sync with CanonicalParamTypeNames (ref / const ref / ...).
		return CanonicalParamTypeNames(Function.Parameters, Function.ModulePath, FunctionSubst);
	}

	bool VeroScriptCompiler::FreeFunctionNameMatches(
	    const std::string& RequestedName,
	    const std::string& DeclName)
	{
		return DeclName == RequestedName || (RequestedName.rfind("operator", 0) == 0 && TypeNameLeaf(DeclName) == RequestedName);
	}

	std::string VeroScriptCompiler::MakeFreeFunctionLinkageName(const FFunctionDecl& Function) const
	{
		const std::string Base =
		    GetSymbolLinkageName(Function.ModulePath, Function.bExported, Function.Name);
		if (!TemplateParameterNames(Function).empty())
		{
			return Base;
		}
		return MakeFreeFunctionName(
		    Base,
		    SpecializedFreeFunctionParamTypes(Function, {}));
	}

	bool VeroScriptCompiler::IsClassOrStructType(const std::string& TypeName) const
	{
		if (TypeName.empty() || !ActiveProgram || TypeName == "void" || Runtime::IsPrimitiveValueTypeName(TypeName))
		{
			return false;
		}
		if (IsStructType(TypeName))
		{
			return true;
		}
		if (Module)
		{
			std::string GenericBase;
			std::vector<std::string> GenericArgs;
			const std::string Qualified = SplitUserGenericTypeApp(TypeName, GenericBase, GenericArgs)
			    ? QualifyTypeName(TypeName, CurrentModulePath)
			    : TypeName;
			for (const auto Index : {FindRegisteredTypeIndex(TypeName), FindRegisteredTypeIndex(Qualified)})
			{
				if (Index < Module->Types.size() && Module->Types[Index].Kind == EScriptObjectKind::Class)
				{
					return true;
				}
			}
		}
		std::string LinkageName = ResolveTypeLinkageName(TypeName, CurrentModulePath);
		std::string GenericBase;
		std::vector<std::string> GenericArgs;
		if (LinkageName.empty() && SplitUserGenericTypeApp(TypeName, GenericBase, GenericArgs))
		{
			LinkageName = ResolveTypeLinkageName(GenericBase, CurrentModulePath);
		}
		if (LinkageName.empty())
		{
			const std::string TemplateBase = GenericTemplateBaseLinkage(TypeName);
			if (!TemplateBase.empty())
			{
				LinkageName = TemplateBase.find('\x1F') != std::string::npos
				    ? TemplateBase
				    : ResolveTypeLinkageName(TemplateBase, CurrentModulePath);
			}
		}
		if (LinkageName.empty())
		{
			return false;
		}
		const std::string TemplateBase = [&]() -> std::string
		{
			const std::string Base = GenericTemplateBaseLinkage(LinkageName);
			return Base.empty() ? LinkageName : Base;
		}();
		const std::size_t WantedArity = TypeParamArityFromLinkage(
		    FindGenericTypeMarker(TypeName) != std::string::npos ? TypeName : LinkageName);
		for (const auto Index : FindDeclarationCandidates(SourceClassIndices, TemplateBase))
		{
			const FClassDecl& ClassDecl = ActiveProgram->Classes[Index];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == TemplateBase && TemplateParameterNames(ClassDecl).size() == WantedArity)
			{
				return true;
			}
		}
		for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
		{
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == TemplateBase && TemplateParameterNames(ClassDecl).size() == WantedArity)
			{
				return true;
			}
			std::string AppBase;
			std::vector<std::string> AppArgs;
			if (SplitUserGenericTypeApp(TypeName, AppBase, AppArgs) && AppBase == ClassDecl.Name && AppArgs.size() == TemplateParameterNames(ClassDecl).size())
			{
				return true;
			}
		}
		return false;
	}

	bool VeroScriptCompiler::TypeSupportsOperatorOverload(const std::string& TypeName) const
	{
		if (IsClassOrStructType(TypeName) || IsInterfaceTypeName(TypeName))
		{
			return true;
		}
		if (!ActiveBindings)
		{
			return false;
		}
		const FHostTypeDesc* const HostType = ActiveBindings->FindType(TypeName);
		return HostType && (HostType->Kind == EHostTypeKind::Struct || HostType->Kind == EHostTypeKind::Class);
	}

	const char* VeroScriptCompiler::BinaryOperatorMethodName(const EBinaryOp Op) const
	{
		switch (Op)
		{
		case EBinaryOp::Add:
			return "operator+";
		case EBinaryOp::Subtract:
			return "operator-";
		case EBinaryOp::Multiply:
			return "operator*";
		case EBinaryOp::Divide:
			return "operator/";
		case EBinaryOp::Modulo:
			return "operator%";
		case EBinaryOp::Equal:
			return "operator==";
		case EBinaryOp::NotEqual:
			return "operator!=";
		case EBinaryOp::Less:
			return "operator<";
		case EBinaryOp::LessEqual:
			return "operator<=";
		case EBinaryOp::Greater:
			return "operator>";
		case EBinaryOp::GreaterEqual:
			return "operator>=";
		case EBinaryOp::LogicalAnd:
			return "operator&&";
		case EBinaryOp::LogicalOr:
			return "operator||";
		case EBinaryOp::BitAnd:
			return "operator&";
		case EBinaryOp::BitOr:
			return "operator|";
		case EBinaryOp::BitXor:
			return "operator^";
		case EBinaryOp::ShiftLeft:
			return "operator<<";
		case EBinaryOp::ShiftRight:
			return "operator>>";
		default:
			return nullptr;
		}
	}

	const char* VeroScriptCompiler::CompoundOperatorMethodName(const EBinaryOp Op) const
	{
		switch (Op)
		{
		case EBinaryOp::Add:
			return "operator+=";
		case EBinaryOp::Subtract:
			return "operator-=";
		case EBinaryOp::Multiply:
			return "operator*=";
		case EBinaryOp::Divide:
			return "operator/=";
		case EBinaryOp::Modulo:
			return "operator%=";
		default:
			return nullptr;
		}
	}

	const char* VeroScriptCompiler::UnaryOperatorMethodName(const EUnaryOp Op) const
	{
		switch (Op)
		{
		case EUnaryOp::Negate:
			return "operator-";
		case EUnaryOp::Not:
			return "operator!";
		case EUnaryOp::BitNot:
			return "operator~";
		default:
			return nullptr;
		}
	}

	std::optional<EBinaryOp> VeroScriptCompiler::MakeMirrorBinaryOp(const EBinaryOp Op) const
	{
		switch (Op)
		{
		case EBinaryOp::Greater:
			return EBinaryOp::Less;
		case EBinaryOp::Less:
			return EBinaryOp::Greater;
		case EBinaryOp::GreaterEqual:
			return EBinaryOp::LessEqual;
		case EBinaryOp::LessEqual:
			return EBinaryOp::GreaterEqual;
		default:
			return std::nullopt;
		}
	}

	std::string VeroScriptCompiler::ConversionOperatorMethodName(const std::string& TargetTypeName) const
	{
		return "operator conv " + TargetTypeName;
	}

}

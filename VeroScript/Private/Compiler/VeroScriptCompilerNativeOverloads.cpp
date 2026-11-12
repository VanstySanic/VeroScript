// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptCompilerVariantInternal.h"
#include "VeroScriptNumericBindings.h"

#include <algorithm>
#include <limits>

namespace VeroScript
{
	const FNativeFunction* VeroScriptCompiler::FindNativeForCall(const std::string& Name,
	    FExprArgumentView Arguments, std::uint16_t& OutIndex, const FSourceLocation& Location,
	    const bool bReportErrors, const int ReceiverMode, const bool bReceiverIsConst) const
	{
		OutIndex = 0xFFFF;
		if (!ActiveNatives) return nullptr;
		struct FCandidate
		{
			std::uint16_t Index;
			std::size_t First;
			bool Const = false;
		};
		std::vector<FCandidate> Candidates;
		bool FoundName = false;
		auto LookupNames = !Name.empty() && Name.front() == '\x1f'
		    ? std::vector<std::string>{Name}
		    : BuildFreeFunctionLookupNames(Name, CurrentModulePath);
		if (LookupNames.empty())
		{
			LookupNames.push_back(Name);
		}
		for (std::size_t I = 0; I < ActiveNatives->size() && I <= 0xFFFF; ++I)
		{
			const auto& N = (*ActiveNatives)[I];
			if (!std::any_of(LookupNames.begin(), LookupNames.end(), [&](const auto& Lookup)
			{
				return NativeNameMatchesLookup(N.Name, Lookup);
			}) ||
			    !CanAccessNativeBinding(N.Origin, !N.Name.empty() && N.Name.front() == '\x1f' ? N.HostTypeName : std::string{}))
			{
				continue;
			}
			FoundName = true;
			const bool Static = N.bHostStaticMethod || N.ParamTypes.empty() || N.ParamTypes[0] != ENativeType::HostObject;
			if (ReceiverMode == 1 && !Static)
			{
				continue;
			}
			bool Const = false;
			if (ReceiverMode >= 0 && !Static && ActiveBindings)
			{
				if (const auto* Owner = ActiveBindings->FindType(N.HostTypeName))
				{
					for (const auto& M : Owner->Members)
					{
						if (!M.bOperator && M.Kind == EHostMemberKind::Method && M.NativeName == N.Name)
						{
							Const = M.bConstMethod;
							break;
						}
					}
				}
				if (bReceiverIsConst && !Const)
				{
					continue;
				}
			}
			Candidates.push_back({static_cast<std::uint16_t>(I), ReceiverMode >= 0 && !Static ? 1u : 0u, Const});
		}
		// Keep the existing diagnostics and special intrinsic contracts for a
		// single signature. Overloaded names always go through typed selection.
		if (Candidates.size() == 1)
		{
			OutIndex = Candidates.front().Index;
			return &(*ActiveNatives)[OutIndex];
		}
		if (Candidates.empty())
		{
			if (FoundName && bReportErrors)
			{
				ReportErrorId("diag.no_matching_native_overload", Location,
				    {ReceiverMode >= 0 ? Name.substr(Name.find_last_of('.') + 1) : Name});
			}
			return nullptr;
		}
		std::vector<std::string> Types;
		for (const FExpr* Arg : Arguments)
		{
			std::string Type;
			if (!Arg || !TryResolveExprTypeName(UnwrapRefOperand(*Arg), Type))
			{
				Type.clear();
			}
			else if (Arg->Kind == EExprKind::Ref)
			{
				Type = EncodeRefCallArgType(UnwrapRefOperand(*Arg), std::move(Type));
			}
			Types.push_back(std::move(Type));
		}
		int BestScore = (std::numeric_limits<int>::min)();
		bool BestBoxing = true;
		std::vector<FCandidate> Best;
		for (const auto& C : Candidates)
		{
			const auto& N = (*ActiveNatives)[C.Index];
			if (N.ParamTypes.size() != Types.size() + C.First)
			{
				continue;
			}
			int Score = 0;
			std::vector<std::string> Params;
			bool Viable = true;
			for (std::size_t I = 0; I < Types.size(); ++I)
			{
				const auto P = I + C.First;
				std::string Type = ResolveNativeParamTypeName(N, P);
				const auto Flags = NativeParamFlagAt(N, P);
				int ReferencePenalty = 0;
				if (NativeParamFlagIsRef(Flags))
				{
					if (Types[I].rfind("ref ", 0) == 0 || Types[I].rfind("const ref ", 0) == 0)
					{
						if (NativeParamFlagIsMutableRef(Flags) && CanonicalTypeNameForCoercion(std::string(Runtime::StripReferenceTypeName(Types[I]))) != CanonicalTypeNameForCoercion(Type))
						{
							Viable = false;
							break;
						}
						Type = ((Flags & NativeParamFlagConstRef) ? "const ref " : "ref ") + Type;
					}
					else
					// Native value inputs may omit `ref` (no slot write-back).
					// Prefer an ordinary value signature when both are viable.
					{
						ReferencePenalty = NativeParamFlagIsMutableRef(Flags) ? 2 : 1;
					}
				}
				int Contribution = 0;
				if (Types[I].empty() || !Numerics::ValidateArgumentType(N, P, CanonicalTypeNameForCoercion(Types[I])) || !TryScoreArgParamTypes(Types[I], Type, Contribution))
				{
					Viable = false;
					break;
				}
				Params.push_back(std::move(Type));
				Score += Contribution - ReferencePenalty;
			}
			if (!Viable)
			{
				continue;
			}
			const bool Boxing = CompilerDetail::NeedsVariantBoxing(Types, Params);
			if (Best.empty() || (BestBoxing && !Boxing) || (BestBoxing == Boxing && Score > BestScore))
			{
				Best.assign(1, C);
				BestScore = Score;
				BestBoxing = Boxing;
			}
			else if (BestBoxing == Boxing && Score == BestScore)
			{
				Best.push_back(C);
			}
		}
		// Receiver constness breaks ties only between instance methods. A static
		// method has no receiver conversion and must not silently win that tie.
		if (!bReceiverIsConst && std::any_of(Best.begin(), Best.end(), [](const FCandidate& C)
		{
			return C.First != 0 && !C.Const;
		}))
		{
			Best.erase(std::remove_if(Best.begin(), Best.end(),
			               [](const FCandidate& C)
			{
				return C.First != 0 && C.Const;
			}),
			    Best.end());
		}
		if (Best.size() != 1)
		{
			OutIndex = 0xFFFF;
			if (bReportErrors && FoundName)
			{
				ReportErrorId(Best.empty() ? "diag.no_matching_native_overload" : "diag.ambiguous_call_to", Location,
				    {ReceiverMode >= 0 ? Name.substr(Name.find_last_of('.') + 1) : Name});
			}
			return nullptr;
		}
		OutIndex = Best.front().Index;
		return &(*ActiveNatives)[OutIndex];
	}
}

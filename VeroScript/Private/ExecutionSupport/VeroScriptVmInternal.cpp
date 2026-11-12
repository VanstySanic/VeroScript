// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmInternal.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptRuntime.h"
#include <limits>

namespace VeroScript::VmDetail
{
	namespace
	{
		bool RuntimeTypeNamesEqualImpl(const std::string_view Left,
			const std::string_view Right, const std::size_t Depth)
		{
			if (Depth > 256 || Left.empty() || Right.empty()) return false;
			if (Left == Right || CanonicalPrimitiveTypeName(Left) == CanonicalPrimitiveTypeName(Right))
			{
				return true;
			}

			for (const std::string_view Prefix : {"const ref ", "ref "})
			{
				if (Left.rfind(Prefix, 0) == 0 || Right.rfind(Prefix, 0) == 0)
				{
					return Left.rfind(Prefix, 0) == 0 && Right.rfind(Prefix, 0) == 0 &&
						RuntimeTypeNamesEqualImpl(
							Left.substr(Prefix.size()), Right.substr(Prefix.size()), Depth + 1);
				}
			}
			if (Runtime::IsFunctionTypeName(Left) && Runtime::IsFunctionTypeName(Right))
			{
				std::vector<std::string> LeftParams, RightParams;
				std::string LeftReturn, RightReturn;
				if (!Runtime::ParseFunctionType(Left, LeftParams, LeftReturn) ||
					!Runtime::ParseFunctionType(Right, RightParams, RightReturn) ||
					LeftParams.size() != RightParams.size() ||
					!RuntimeTypeNamesEqualImpl(LeftReturn, RightReturn, Depth + 1))
				{
					return false;
				}

				for (std::size_t I = 0; I < LeftParams.size(); ++I)
				{
					if (!RuntimeTypeNamesEqualImpl(LeftParams[I], RightParams[I], Depth + 1)) return false;
				}

				return true;
			}
			if (Runtime::IsNativeArrayTypeName(Left) && Runtime::IsNativeArrayTypeName(Right))
			{
				std::string LeftElement, RightElement;
				std::uint32_t LeftLength = 0, RightLength = 0;
				return Runtime::ParseNativeArrayType(Left, LeftElement, LeftLength) &&
					Runtime::ParseNativeArrayType(Right, RightElement, RightLength) && LeftLength == RightLength &&
					RuntimeTypeNamesEqualImpl(LeftElement, RightElement, Depth + 1);
			}
			// Only built-in value-container wrappers permit recursive primitive
			// aliases. A qualified generic script object remains an exact identity.
			for (const std::string_view Prefix : {"array<", "queue<", "priority_queue<", "stack<", "deque<",
				"multiset<", "ordered_multiset<"})
			{
				if (Left.rfind(Prefix, 0) == 0 && Right.rfind(Prefix, 0) == 0 &&
					Left.size() > Prefix.size() + 1 && Right.size() > Prefix.size() + 1 &&
					Left.back() == '>' && Right.back() == '>')
				{
					return RuntimeTypeNamesEqualImpl(
						Left.substr(Prefix.size(), Left.size() - Prefix.size() - 1),
						Right.substr(Prefix.size(), Right.size() - Prefix.size() - 1), Depth + 1);
				}
			}
			const bool bMaps = Runtime::IsMapTypeName(Left) && Runtime::IsMapTypeName(Right) &&
				Runtime::IsMultiMapTypeName(Left) == Runtime::IsMultiMapTypeName(Right);
			const bool bOrderedMaps = Runtime::IsOrderedMapTypeName(Left) && Runtime::IsOrderedMapTypeName(Right) &&
				Runtime::IsOrderedMultiMapTypeName(Left) == Runtime::IsOrderedMultiMapTypeName(Right);
			if (bMaps || bOrderedMaps)
			{
				std::string LeftKey, LeftValue, RightKey, RightValue;
				const auto Split = bMaps ? Runtime::SplitMapType : Runtime::SplitOrderedMapType;
				return Split(Left, LeftKey, LeftValue) && Split(Right, RightKey, RightValue) &&
					LeftKey == RightKey && RuntimeTypeNamesEqualImpl(LeftValue, RightValue, Depth + 1);
			}
			return false;
		}
	}

	bool RuntimeTypeNamesEqual(const std::string_view Left, const std::string_view Right)
	{
		return RuntimeTypeNamesEqualImpl(Left, Right, 0);
	}

	std::string ConversionDispatchLeaf(const FBytecodeFunction& Function)
	{
		const auto Separator = FindMethodLinkageSeparator(Function.Name);
		if (!Function.bIsMethod || Function.ParamCount != 0 || Separator == std::string::npos ||
			Function.Name.compare(Separator + 2, 14, "operator conv ") != 0)
		{
			return {};
		}

		std::vector<std::string> Parameters;
		std::string ReturnType;
		if (!Runtime::ParseFunctionType(Function.ClosureTypeName, Parameters, ReturnType) ||
			!Parameters.empty())
		{
			return {};
		}

		ReturnType = CanonicalPrimitiveTypeName(ReturnType);
		return "operator conv " + ReturnType + "#0" + (Function.bIsConstMethod ? "c" : "");
	}

	bool ConversionDispatchLeavesMatch(const std::string_view Left, const std::string_view Right)
	{
		constexpr std::string_view Prefix = "operator conv ";
		if (Left.rfind(Prefix, 0) != 0 || Right.rfind(Prefix, 0) != 0) return false;
		const bool bLeftConst = Left.back() == 'c';
		const bool bRightConst = Right.back() == 'c';
		if (bLeftConst != bRightConst) return false;
		const std::string_view Suffix = bLeftConst ? "#0c" : "#0";
		if (Left.size() <= Prefix.size() + Suffix.size() || Right.size() <= Prefix.size() + Suffix.size() ||
			Left.substr(Left.size() - Suffix.size()) != Suffix ||
			Right.substr(Right.size() - Suffix.size()) != Suffix)
		{
			return false;
		}

		return RuntimeTypeNamesEqual(Left.substr(Prefix.size(), Left.size() - Prefix.size() - Suffix.size()),
			Right.substr(Prefix.size(), Right.size() - Prefix.size() - Suffix.size()));
	}

	std::uint16_t FindVirtualConversionMethod(const FBytecodeModule& Module,
		const std::string_view OwnerName, const std::string_view DispatchLeaf)
	{
		const std::string Prefix = std::string(OwnerName) + "::operator conv ";
		for (std::size_t I = 0; I < Module.Functions.size(); ++I)
		{
			const auto& Function = Module.Functions[I];
			if (Function.bIsVirtual && Function.Name.rfind(Prefix, 0) == 0 &&
				ConversionDispatchLeavesMatch(ConversionDispatchLeaf(Function), DispatchLeaf))
			{
				return static_cast<std::uint16_t>(I);
			}
		}
		return std::numeric_limits<std::uint16_t>::max();
	}
}

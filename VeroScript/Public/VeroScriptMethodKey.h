// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	namespace MethodKeyDetail
	{
		inline bool IsInsideAnglePayload(
			const std::string_view LinkageName,
			const std::size_t PositionToTest) noexcept
		{
			std::size_t Depth = 0;
			for (std::size_t Position = 0; Position < PositionToTest; ++Position)
			{
				if (LinkageName[Position] == '<')
				{
					++Depth;
				}
				else if (LinkageName[Position] == '>' && Depth > 0
					&& (Position == 0 || LinkageName[Position - 1] != '-'))
				{
					--Depth;
				}
			}
			return Depth > 0;
		}

		inline bool IsInsideLengthEncodedPayload(
			const std::string_view LinkageName,
			const std::size_t PositionToTest) noexcept
		{
			for (std::size_t Marker = LinkageName.find('#');
				Marker != std::string_view::npos && Marker < PositionToTest;
				Marker = LinkageName.find('#', Marker + 1))
			{
				std::size_t Position = Marker + 1;
				while (Position < LinkageName.size())
				{
					const std::size_t LengthStart = Position;
					std::size_t TypeLength = 0;
					while (Position < LinkageName.size()
						&& LinkageName[Position] >= '0'
						&& LinkageName[Position] <= '9')
					{
						const std::size_t Digit = static_cast<std::size_t>(
							LinkageName[Position] - '0');
						if (TypeLength > (static_cast<std::size_t>(-1) - Digit) / 10)
						{
							break;
						}
						TypeLength = TypeLength * 10 + Digit;
						++Position;
					}
					if (Position == LengthStart
						|| Position >= LinkageName.size()
						|| LinkageName[Position] != ':')
					{
						break;
					}
					++Position;
					if (TypeLength > LinkageName.size() - Position)
					{
						break;
					}
					if (PositionToTest >= Position
						&& PositionToTest < Position + TypeLength)
					{
						return true;
					}
					Position += TypeLength;
				}
			}
			return false;
		}

		inline std::size_t FindGenericOwnerSeparator(
			const std::string_view LinkageName,
			const std::size_t GenericMarker) noexcept
		{
			std::size_t Position = GenericMarker + 1;
			bool bDecodedType = false;
			while (Position < LinkageName.size())
			{
				if (bDecodedType
					&& Position + 1 < LinkageName.size()
					&& LinkageName[Position] == ':'
					&& LinkageName[Position + 1] == ':')
				{
					const std::size_t MethodStart = Position + 2;
					if (MethodStart < LinkageName.size()
						&& (LinkageName[MethodStart] == '~'
							|| LinkageName.find('#', MethodStart) != std::string_view::npos))
					{
						return Position;
					}
					return std::string_view::npos;
				}

				const std::size_t LengthStart = Position;
				std::size_t TypeLength = 0;
				while (Position < LinkageName.size()
					&& LinkageName[Position] >= '0'
					&& LinkageName[Position] <= '9')
				{
					const std::size_t Digit =
						static_cast<std::size_t>(LinkageName[Position] - '0');
					if (TypeLength > (static_cast<std::size_t>(-1) - Digit) / 10)
					{
						return std::string_view::npos;
					}
					TypeLength = TypeLength * 10 + Digit;
					++Position;
				}
				if (Position == LengthStart
					|| Position >= LinkageName.size()
					|| LinkageName[Position] != ':')
				{
					return std::string_view::npos;
				}
				++Position;
				if (TypeLength > LinkageName.size() - Position)
				{
					return std::string_view::npos;
				}
				Position += TypeLength;
				bDecodedType = true;
			}
			return std::string_view::npos;
		}
	}

	// Start of the outer symbol identity, excluding a private module prefix.
	inline std::size_t FindLinkageIdentityStart(const std::string_view LinkageName) noexcept
	{
		const std::size_t UnitSeparator = LinkageName.find('\x1F');
		if (UnitSeparator == std::string_view::npos
			|| MethodKeyDetail::IsInsideAnglePayload(
				LinkageName, UnitSeparator)
			|| MethodKeyDetail::IsInsideLengthEncodedPayload(
				LinkageName, UnitSeparator))
		{
			return 0;
		}
		return UnitSeparator + 1;
	}

	// Marker for a length-mangled generic type, excluding any module-path hash.
	inline std::size_t FindGenericTypeMarker(const std::string_view TypeLinkageName) noexcept
	{
		for (std::size_t Marker = TypeLinkageName.find('#', FindLinkageIdentityStart(TypeLinkageName));
			Marker != std::string_view::npos; Marker = TypeLinkageName.find('#', Marker + 1))
		{
			if (!MethodKeyDetail::IsInsideAnglePayload(TypeLinkageName, Marker))
			{
				return Marker;
			}
		}
		return std::string_view::npos;
	}

	/**
	 * Locate the owner/method separator without inspecting the encoded parameter
	 * payload. A generic owner has its own length-prefixed `#...` section before
	 * the separator (`Box#3:int::Method#...`).
	 */
	inline std::size_t FindMethodLinkageSeparator(const std::string_view LinkageName) noexcept
	{
		const std::size_t IdentityStart = FindLinkageIdentityStart(LinkageName);
		const std::size_t FirstMarker = LinkageName.find('#', IdentityStart);
		if (FirstMarker == std::string_view::npos)
		{
			const std::size_t Separator = LinkageName.rfind("::");
			return Separator != std::string_view::npos && Separator >= IdentityStart
				? Separator
				: std::string_view::npos;
		}
		if (const std::size_t GenericSeparator =
				MethodKeyDetail::FindGenericOwnerSeparator(LinkageName, FirstMarker);
			GenericSeparator != std::string_view::npos)
		{
			return GenericSeparator;
		}
		const std::size_t Separator = LinkageName.rfind("::", FirstMarker);
		return Separator != std::string_view::npos && Separator >= IdentityStart
			? Separator
			: std::string_view::npos;
	}

	/**
	 * Find the function-signature marker after the resolved method separator.
	 * Free-function overload keys use the explicit `#fn#` tag. Generic free
	 * instantiations retain their historical `Name#types` representation.
	 */
	inline std::size_t FindFunctionSignatureMarker(const std::string_view LinkageName) noexcept
	{
		const std::size_t Separator = FindMethodLinkageSeparator(LinkageName);
		if (Separator != std::string_view::npos)
		{
			return LinkageName.find('#', Separator + 2);
		}
		const std::size_t IdentityStart = FindLinkageIdentityStart(LinkageName);
		if (const std::size_t FreeTag = LinkageName.find("#fn#", IdentityStart);
			FreeTag != std::string_view::npos)
		{
			return FreeTag;
		}
		return LinkageName.find('#', IdentityStart);
	}

	// Bare method leaf, excluding the owner and encoded signature.
	inline std::string_view MethodLinkageBareLeaf(const std::string_view LinkageName) noexcept
	{
		const std::size_t Signature = FindFunctionSignatureMarker(LinkageName);
		const std::size_t LeafEnd = Signature == std::string_view::npos
			? LinkageName.size()
			: Signature;
		const std::size_t Separator = FindMethodLinkageSeparator(LinkageName);
		if (Separator == std::string_view::npos || Separator + 2 >= LeafEnd)
		{
			return {};
		}
		return LinkageName.substr(Separator + 2, LeafEnd - (Separator + 2));
	}

	/**
	 * Match a full method linkage against either a bare leaf or its complete
	 * signature-bearing leaf. The owner separator is always resolved before '#'.
	 */
	inline bool MethodLinkageLeafMatches(
		const std::string_view LinkageName,
		const std::string_view WantedLeaf) noexcept
	{
		if (WantedLeaf.empty())
		{
			return false;
		}
		const std::size_t Separator = FindMethodLinkageSeparator(LinkageName);
		if (Separator == std::string_view::npos || Separator + 2 >= LinkageName.size())
		{
			return false;
		}
		const std::string_view EncodedLeaf = LinkageName.substr(Separator + 2);
		if (EncodedLeaf == WantedLeaf)
		{
			return true;
		}
		if (WantedLeaf.find('#') != std::string_view::npos
			|| EncodedLeaf.size() <= WantedLeaf.size()
			|| EncodedLeaf.compare(0, WantedLeaf.size(), WantedLeaf) != 0)
		{
			return false;
		}
		return EncodedLeaf[WantedLeaf.size()] == '#';
	}

	/**
	 * Method leaf / table suffix for param-type overloads.
	 * Empty -> "#0"; otherwise length-prefixed types: "#3:int6:string".
	 * CallMethod string pool stores "Name" + this suffix; VM looks up Owner::Name#....
	 */
	inline std::string EncodeMethodParamSuffix(const std::vector<std::string>& ParamTypes)
	{
		if (ParamTypes.empty())
		{
			return "#0";
		}
		std::string Result = "#";
		for (const std::string& TypeName : ParamTypes)
		{
			Result += std::to_string(TypeName.size());
			Result += ':';
			Result += TypeName;
		}
		return Result;
	}

	inline std::string MakeMethodFunctionName(
		const std::string& OwnerLinkage,
		const std::string& MethodName,
		const std::vector<std::string>& ParamTypes,
		const bool bIsConst = false)
	{
		std::string Result = OwnerLinkage + "::" + MethodName + EncodeMethodParamSuffix(ParamTypes);
		if (bIsConst)
		{
			Result += 'c';
		}
		return Result;
	}

	// Constructor table key - same param-type mangling as methods.
	inline std::string MakeConstructorFunctionName(
		const std::string& OwnerLinkage,
		const std::string& TypeSimpleName,
		const std::vector<std::string>& ParamTypes)
	{
		return MakeMethodFunctionName(OwnerLinkage, TypeSimpleName, ParamTypes, false);
	}

	// Destructor table key (`Owner::~Type`).
	inline std::string MakeDestructorFunctionName(
		const std::string& OwnerLinkage,
		const std::string& TypeSimpleName)
	{
		return OwnerLinkage + "::~" + TypeSimpleName;
	}

	// Legacy / builtin bare-name CallMethod fallback (`Owner::Name#N`).
	inline std::string MakeArityMethodFunctionName(
		const std::string& OwnerLinkage,
		const std::string& MethodName,
		const std::size_t Arity)
	{
		return OwnerLinkage + "::" + MethodName + "#" + std::to_string(Arity);
	}

	/**
	 * Free-function table key for param-type overloads.
	 * Uses "#fn" so keys never collide with generic instantiation mangling (`Name#3:int`).
	 */
	inline std::string MakeFreeFunctionName(
		const std::string& LinkageBase,
		const std::vector<std::string>& ParamTypes)
	{
		return LinkageBase + "#fn" + EncodeMethodParamSuffix(ParamTypes);
	}
}

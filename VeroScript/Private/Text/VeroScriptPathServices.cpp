// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTextInternal.h"
#include "VeroScriptHostBinding.h"

#include <stdexcept>

namespace VeroScript::StandardLibrary
{
	FServiceResult LexicalPath(std::string_view Path, EPathStyle Style, FLexicalPath& Output, const FServiceContext& C)
	{
		if (auto R = C.CheckText(Path.size()); !R) return R;
		if (Style == EPathStyle::Native)
		{
			#if defined(_WIN32)
			Style = EPathStyle::Windows;
			#else
			Style = EPathStyle::Posix;
			#endif
		}
		if (Style != EPathStyle::Windows && Style != EPathStyle::Posix) return {EServiceStatus::InvalidArgument, 0};
		const bool Windows = Style == EPathStyle::Windows;
		const auto Separator = [Windows](char C)
		{
			return C == '/' || (Windows && C == '\\');
		};
		if (Windows && Path.size() >= 4 && Separator(Path[0]) && Path[1] == '?' && Path[2] == '?' && Separator(Path[3]))
		{
			return {EServiceStatus::InvalidArgument, 0};
		}
		Detail::FWorkMeter Meter(C);
		for (std::size_t I = 0; I < Path.size(); ++I)
		{
			if (auto R = Meter.Add(); !R) return R;
			if (!Path[I]) return {EServiceStatus::InvalidArgument, I};
		}
		FLexicalPath Result;
		std::size_t Position = 0;
		bool RootDirectory = false;
		if (Windows && Path.size() >= 2 && Separator(Path[0]) && Separator(Path[1]))
		{
			Position = 2;
			const auto ServerBegin = Position;
			while (Position < Path.size() && !Separator(Path[Position]))
			{
				if (auto R = Meter.Add(); !R) return R;
				++Position;
			}
			const auto Server = Path.substr(ServerBegin, Position - ServerBegin);
			if (Server.empty() || Server == "." || Server == ".." || Server == "?") return {EServiceStatus::InvalidArgument, ServerBegin};
			while (Position < Path.size() && Separator(Path[Position]))
			{
				if (auto R = Meter.Add(); !R) return R;
				++Position;
			}
			const auto ShareBegin = Position;
			while (Position < Path.size() && !Separator(Path[Position]))
			{
				if (auto R = Meter.Add(); !R) return R;
				++Position;
			}
			const auto Share = Path.substr(ShareBegin, Position - ShareBegin);
			if (Share.empty() || Share == "." || Share == "..") return {EServiceStatus::InvalidArgument, ShareBegin};
			Result.Root = "//";
			if (auto R = Detail::Append(Result.Root, Server, C); !R) return R;
			Result.Root += '/';
			if (auto R = Detail::Append(Result.Root, Share, C); !R) return R;
			Result.Root += '/';
			Result.Absolute = RootDirectory = true;
		}
		else
		{
			const auto Letter = [](char C)
			{
				return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z');
			};
			if (Windows && Path.size() >= 2 && Letter(Path[0]) && Path[1] == ':')
			{
				Result.Root.assign(Path.data(), 2);
				Position = 2;
			}
			if (Position < Path.size() && Separator(Path[Position]))
			{
				RootDirectory = true;
				Result.Root += '/';
				Result.Absolute = !Windows || Position == 2;
			}
		}
		std::vector<std::string_view> Parts;
		bool DirectoryEnding = !Path.empty() && Separator(Path.back());
		while (Position < Path.size())
		{
			if (Separator(Path[Position]))
			{
				if (auto R = Meter.Add(); !R) return R;
				++Position;
				continue;
			}
			const auto Begin = Position;
			while (Position < Path.size() && !Separator(Path[Position]))
			{
				if (auto R = Meter.Add(); !R) return R;
				++Position;
			}
			const auto Part = Path.substr(Begin, Position - Begin);
			if (Part == ".")
			{
				if (Position == Path.size())
				{
					DirectoryEnding = true;
				}
			}
			else if (Part == "..")
			{
				if (!Parts.empty() && Parts.back() != "..")
				{
					Parts.pop_back();
					if (Position == Path.size())
					{
						DirectoryEnding = true;
					}
				}
				else if (!RootDirectory)
				{
					Parts.push_back(Part);
				}
			}
			else
			{
				Parts.push_back(Part);
			}
		}
		if (auto R = Meter.Flush(); !R) return R;
		if (auto R = C.CheckText(Result.Root.size()); !R) return R;
		if (auto R = Detail::Append(Result.Normal, Result.Root, C); !R) return R;
		for (std::size_t I = 0; I < Parts.size(); ++I)
		{
			if (I || RootDirectory)
			{
				if (!Result.Normal.empty() && Result.Normal.back() != '/')
				{
					Result.Normal += '/';
				}
			}
			if (auto R = Detail::Append(Result.Normal, Parts[I], C); !R) return R;
		}
		if (Result.Normal.empty())
		{
			Result.Normal = ".";
		}
		if (DirectoryEnding && !Parts.empty() && Parts.back() != ".." && Result.Normal.back() != '/')
		{
			Result.Normal += '/';
		}
		if (auto R = C.CheckText(Result.Normal.size()); !R) return R;
		Output = std::move(Result);
		return {};
	}

	const IHostBindingCatalog& GetServiceBindingCatalog()
	{
		const auto Build = []()
		{
			FHostBindingRegistry R;
			FHostTypeId Type;
			std::string Error;
			if (!R.RegisterType("std.filesystem.path_style", EHostTypeKind::Enum, Type, Error) || !R.SetEnumScoped(Type, true, Error))
			{
				throw std::logic_error(Error);
			}
			for (unsigned I = 0; I < 3; ++I)
			{
				FHostMemberDesc M;
				M.Kind = EHostMemberKind::EnumMember;
				M.Name = I == 0 ? "native" : I == 1 ? "posix"
				                                    : "windows";
				M.EnumValue = I;
				if (!R.RegisterMember(Type, std::move(M), Error))
				{
					throw std::logic_error(Error);
				}
			}
			return R;
		};
		class FServiceCatalog final : public IHostBindingCatalog
		{
		public:
			explicit FServiceCatalog(FHostBindingRegistry Registry) : Source(std::move(Registry)), Types(Source.GetTypes())
			{
				// Keep Core identities separate from host registrations and from the
				// existing memory/i18n reserved ranges.
				Types.front().Id.Value = 0xffffffb0u;
			}
			const std::vector<FNativeFunction>& GetNativeFunctions() const override
			{
				return Source.GetNativeFunctions();
			}
			const std::vector<FHostFunctionDesc>& GetFunctions() const override
			{
				return Source.GetFunctions();
			}
			const std::vector<FHostTypeDesc>& GetTypes() const override
			{
				return Types;
			}
			const FHostFunctionDesc* FindFunction(std::string_view) const override
			{
				return nullptr;
			}
			const FHostTypeDesc* FindType(std::string_view Name) const override
			{
				return Name == Types.front().Name ? &Types.front() : nullptr;
			}
			const FHostTypeDesc* FindType(FHostTypeId Id) const override
			{
				return Id == Types.front().Id ? &Types.front() : nullptr;
			}

		private:
			FHostBindingRegistry Source;
			std::vector<FHostTypeDesc> Types;
		};
		static const FServiceCatalog Catalog(Build());
		return Catalog;
	}
}

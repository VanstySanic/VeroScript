// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// IDNA rules adapted from python-idna 3.12; see ThirdParty/idna/LICENSE.
#include "SchemaIdna.h"
#include "SchemaSemantics.h"
#include "Unicode17Data.h"
#include "Unicode17Mapping.h"
#include "VeroScriptJsonRegexSymbols.h"
#include <libunicode.h>
#include <cstdlib>
#include <exception>
#include <limits>

namespace VeroScript::Json::Detail::Formats
{
	namespace U = Unicode17;
	using D = U::EDirection;
	template <std::size_t N>
	static bool In(std::uint32_t C, const std::uint64_t (&Table)[N])
	{
		const auto Key = (std::uint64_t(C) << 32) | 0xffffffffULL;
		const auto* I = std::upper_bound(Table,
			Table + N,
			Key,
			[](auto A, auto B)
			{
				FSchemaSemantics::Check();
				return A < B;
			});
		return I != Table && C < static_cast<std::uint32_t>(I[-1]);
	}
	template <std::size_t N>
	static std::uint8_t Tagged(std::uint32_t C, const U::FTaggedRange (&Table)[N])
	{
		const auto* I = std::upper_bound(Table,
			Table + N,
			C,
			[](auto A, const auto& B)
			{
				FSchemaSemantics::Check();
				return A < B.Start;
			});
		return I != Table && C < I[-1].End ? I[-1].Value : 0;
	}
	static D Direction(std::uint32_t C)
	{
		return static_cast<D>(Tagged(C, U::Bidi));
	}
	bool DecodeUtf8(std::string_view Text, std::vector<std::uint32_t>& Out)
	{
		for (std::size_t I = 0; I < Text.size();)
		{
			FSchemaSemantics::Check();
			const auto Lead = static_cast<unsigned char>(Text[I++]);
			std::uint32_t C = Lead;
			unsigned Count = 0;
			if (Lead < 0x80)
			{
			}
			else if (Lead >= 0xc2 && Lead <= 0xdf)
			{
				C = Lead & 31;
				Count = 1;
			}
			else if (Lead >= 0xe0 && Lead <= 0xef)
			{
				C = Lead & 15;
				Count = 2;
			}
			else if (Lead >= 0xf0 && Lead <= 0xf4)
			{
				C = Lead & 7;
				Count = 3;
			}
			else
			{
				return false;
			}
			if (Count > Text.size() - I)
			{
				return false;
			}
			for (unsigned K = 0; K < Count; ++K)
			{
				const auto Next = static_cast<unsigned char>(Text[I++]);
				if ((Next & 0xc0) != 0x80)
				{
					return false;
				}
				C = (C << 6) | (Next & 63);
			}
			if ((Count == 1 && C < 0x80) || (Count == 2 && C < 0x800) || (Count == 3 && C < 0x10000) || C > 0x10ffff ||
				(C >= 0xd800 && C <= 0xdfff))
			{
				return false;
			}
			Out.push_back(C);
		}
		return true;
	}
	struct alignas(std::max_align_t) FAllocation
	{
		std::size_t Size;
	};
	struct FNormalizationWork
	{
		std::size_t Bytes = 0;
		std::exception_ptr Failure;
	};
	static void* Reallocate(void* Opaque, void* Pointer, std::size_t Size) noexcept
	{
		auto& Work = *static_cast<FNormalizationWork*>(Opaque);
		auto* Old = Pointer ? static_cast<FAllocation*>(Pointer) - 1 : nullptr;
		if (!Size)
		{
			if (Old)
			{
				Work.Bytes -= Old->Size;
				std::free(Old);
			}
			return nullptr;
		}
		try
		{
			FSchemaSemantics::Check(Size + 1);
			const auto Before = Old ? Old->Size : 0;
			const auto Limit = std::min<std::uint64_t>(65536, FSchemaSemantics::Budget().Limits.OutputBytes);
			if (Size > Limit || Work.Bytes - Before > Limit - Size)
			{
				Fail(EError::Limit, "IDNA normalization memory limit exceeded");
			}
			auto* Value = static_cast<FAllocation*>(std::realloc(Old, sizeof(FAllocation) + Size));
			if (!Value)
			{
				throw std::bad_alloc();
			}
			Work.Bytes = Work.Bytes - Before + Size;
			Value->Size = Size;
			return Value + 1;
		}
		catch (...)
		{
			if (!Work.Failure)
			{
				Work.Failure = std::current_exception();
			}
			return nullptr;
		}
	}
	std::vector<std::uint32_t> Normalize(const std::vector<std::uint32_t>& Input)
	{
		if (Input.size() > 256)
		{
			Fail(EError::Limit, "IDNA label normalization limit exceeded");
		}
		FNormalizationWork Work;
		std::uint32_t* Output = nullptr;
		const auto Count =
			unicode_normalize(&Output, Input.data(), static_cast<int>(Input.size()), UNICODE_NFC, &Work, Reallocate);
		struct FOwner
		{
			std::uint32_t* Data;
			~FOwner()
			{
				if (Data)
				{
					std::free(reinterpret_cast<FAllocation*>(Data) - 1);
				}
			}
		} Owner{Output};
		if (Work.Failure)
		{
			std::rethrow_exception(Work.Failure);
		}
		if (Count < 0)
		{
			throw std::bad_alloc();
		}
		FSchemaSemantics::Check(static_cast<std::uint64_t>(Count) + 1);
		return Count ? std::vector<std::uint32_t>(Output, Output + Count) : std::vector<std::uint32_t>{};
	}
	static bool Bidi(const std::vector<std::uint32_t>& Label, bool Force)
	{
		if (!Force)
		{
			return true;
		}
		const auto First = Direction(Label.front());
		const bool Rtl = First == D::R || First == D::AL;
		if (!Rtl && First != D::L)
		{
			return false;
		}
		bool Ending = false;
		D Numbers = D::Other;
		for (auto C : Label)
		{
			const auto Dir = Direction(C);
			if (Dir != D::EN && Dir != D::ES && Dir != D::CS && Dir != D::ET && Dir != D::ON && Dir != D::BN &&
				Dir != D::NSM && (Rtl ? Dir != D::R && Dir != D::AL && Dir != D::AN : Dir != D::L))
			{
				return false;
			}
			if (Dir == D::EN || (Rtl ? Dir == D::R || Dir == D::AL || Dir == D::AN : Dir == D::L))
			{
				Ending = true;
			}
			else if (Dir != D::NSM)
			{
				Ending = false;
			}
			if (Rtl && (Dir == D::EN || Dir == D::AN))
			{
				if (Numbers != D::Other && Numbers != Dir)
				{
					return false;
				}
				Numbers = Dir;
			}
		}
		return Ending;
	}
	static bool Context(const std::vector<std::uint32_t>& L, std::size_t P)
	{
		const auto C = L[P];
		if (C == 0x200d || C == 0x200c)
		{
			if (P && In(L[P - 1], U::Virama))
			{
				return true;
			}
			if (C == 0x200d)
			{
				return false;
			}
			bool Left = false;
			for (std::size_t I = P; I;)
			{
				const auto J = Tagged(L[--I], U::Joining);
				if (J == 'T')
				{
					continue;
				}
				Left = J == 'L' || J == 'D';
				break;
			}
			if (!Left)
			{
				return false;
			}
			for (std::size_t I = P + 1; I < L.size(); ++I)
			{
				const auto J = Tagged(L[I], U::Joining);
				if (J == 'T')
				{
					continue;
				}
				return J == 'R' || J == 'D';
			}
			return false;
		}
		if (C == 0xb7)
		{
			return P && P + 1 < L.size() && L[P - 1] == 'l' && L[P + 1] == 'l';
		}
		if (C == 0x375)
		{
			return P + 1 < L.size() && In(L[P + 1], U::Greek);
		}
		if (C == 0x5f3 || C == 0x5f4)
		{
			return P && In(L[P - 1], U::Hebrew);
		}
		if (C == 0x30fb)
		{
			for (auto N : L)
			{
				if (N != 0x30fb && (In(N, U::Hiragana) || In(N, U::Katakana) || In(N, U::Han)))
				{
					return true;
				}
			}
			return false;
		}
		if (C >= 0x660 && C <= 0x669)
		{
			for (auto N : L)
			{
				if (N >= 0x6f0 && N <= 0x6f9)
				{
					return false;
				}
			}
			return true;
		}
		if (C >= 0x6f0 && C <= 0x6f9)
		{
			for (auto N : L)
			{
				if (N >= 0x660 && N <= 0x669)
				{
					return false;
				}
			}
			return true;
		}
		return false;
	}
	static bool CheckLabel(const std::vector<std::uint32_t>& L)
	{
		if (L.empty() || L.size() > 63 || L.front() == '-' || L.back() == '-' ||
			(L.size() >= 4 && L[2] == '-' && L[3] == '-') || In(L.front(), U::Marks))
		{
			return false;
		}
		if (std::any_of(L.begin(),
				L.end(),
				[](auto C)
				{
					return C >= 128;
				}) &&
			Normalize(L) != L)
		{
			return false;
		}
		for (std::size_t P = 0; P < L.size(); ++P)
		{
			if (!In(L[P], U::PVALID) && !((In(L[P], U::CONTEXTJ) || In(L[P], U::CONTEXTO)) && Context(L, P)))
			{
				return false;
			}
		}
		return true;
	}
	static unsigned Adapt(std::uint64_t Delta, std::uint64_t Points, bool First)
	{
		Delta = First ? Delta / 700 : Delta / 2;
		Delta += Delta / Points;
		unsigned K = 0;
		while (Delta > 455)
		{
			Delta /= 35;
			K += 36;
		}
		return K + static_cast<unsigned>(36 * Delta / (Delta + 38));
	}
	static int Digit(char C)
	{
		return C >= 'a' && C <= 'z' ? C - 'a' : C >= '0' && C <= '9' ? C - '0' + 26 : -1;
	}
	static char Code(std::uint64_t D)
	{
		return static_cast<char>(D < 26 ? 'a' + D : '0' + D - 26);
	}
	static bool DecodePunycode(std::string_view Text, std::vector<std::uint32_t>& Out)
	{
		std::size_t Pos = 0;
		const auto Dash = Text.rfind('-');
		if (Dash != std::string_view::npos)
		{
			if (!Dash)
			{
				return false;
			}
			for (; Pos < Dash; ++Pos)
			{
				if (static_cast<unsigned char>(Text[Pos]) >= 128)
				{
					return false;
				}
				else
				{
					Out.push_back(Text[Pos]);
				}
			}
			Pos++;
		}
		std::uint64_t N = 128, I = 0;
		unsigned Bias = 72;
		while (Pos < Text.size())
		{
			FSchemaSemantics::Check();
			const auto Old = I;
			std::uint64_t W = 1;
			for (unsigned K = 36;; K += 36)
			{
				if (Pos == Text.size())
				{
					return false;
				}
				const int D = Digit(Text[Pos++]);
				if (D < 0 || std::uint64_t(D) > (UINT64_MAX - I) / W)
				{
					return false;
				}
				I += D * W;
				const unsigned T = K <= Bias ? 1 : K >= Bias + 26 ? 26 : K - Bias;
				if (static_cast<unsigned>(D) < T)
				{
					break;
				}
				if (W > UINT64_MAX / (36 - T))
				{
					return false;
				}
				W *= 36 - T;
			}
			const auto Size = Out.size() + 1;
			Bias = Adapt(I - Old, Size, Old == 0);
			if (I / Size > 0x10ffff - N)
			{
				return false;
			}
			N += I / Size;
			I %= Size;
			if (N >= 0xd800 && N <= 0xdfff)
			{
				return false;
			}
			Out.insert(Out.begin() + static_cast<std::ptrdiff_t>(I), static_cast<std::uint32_t>(N));
			++I;
			if (Out.size() > 63)
			{
				return false;
			}
		}
		return std::any_of(Out.begin(),
			Out.end(),
			[](auto C)
			{
				return C >= 128;
			});
	}
	static bool EncodePunycode(const std::vector<std::uint32_t>& Input, std::string& Out)
	{
		for (auto C : Input)
		{
			if (C < 128)
			{
				Out.push_back(static_cast<char>(C));
			}
		}
		auto H = Out.size();
		const auto Basic = H;
		if (Basic)
		{
			Out.push_back('-');
		}
		std::uint64_t N = 128, Delta = 0;
		unsigned Bias = 72;
		while (H < Input.size())
		{
			FSchemaSemantics::Check(Input.size() + 1);
			auto M = std::uint32_t(0x10ffff);
			for (auto C : Input)
			{
				if (C >= N)
				{
					M = std::min(M, C);
				}
			}
			Delta += (M - N) * (H + 1);
			N = M;
			for (auto C : Input)
			{
				if (C < N)
				{
					++Delta;
				}
				if (C == N)
				{
					auto Q = Delta;
					for (unsigned K = 36;; K += 36)
					{
						const unsigned T = K <= Bias ? 1 : K >= Bias + 26 ? 26 : K - Bias;
						if (Q < T)
						{
							break;
						}
						Out.push_back(Code(T + (Q - T) % (36 - T)));
						Q = (Q - T) / (36 - T);
					}
					Out.push_back(Code(Q));
					Bias = Adapt(Delta, H + 1, H == Basic);
					Delta = 0;
					++H;
					if (Out.size() > 59)
					{
						return false;
					}
				}
			}
			++Delta;
			++N;
		}
		return Out.size() <= 59;
	}
	static bool HostnameMapped(std::string_view Text, bool International)
	{
		if (Text.empty() || Text.size() > 4096 || Text.back() == '.')
		{
			return false;
		}
		std::vector<std::vector<std::uint32_t>> Labels;
		std::size_t Offset = 0, Total = 0;
		bool Rtl = false;
		while (Offset < Text.size())
		{
			const auto Dot = Text.find('.', Offset);
			const auto End = Dot == std::string_view::npos ? Text.size() : Dot;
			const auto Part = Text.substr(Offset, End - Offset);
			if (Part.empty())
			{
				return false;
			}
			const bool Ascii = std::all_of(Part.begin(),
				Part.end(),
				[](unsigned char C)
				{
					return C < 128;
				});
			if (!International && !Ascii)
			{
				return false;
			}
			std::vector<std::uint32_t> Label;
			std::size_t Length = Part.size();
			if (Ascii)
			{
				if (Part.size() > 63)
				{
					return false;
				}
				std::string Lower(Part);
				for (auto& C : Lower)
				{
					if (C >= 'A' && C <= 'Z')
					{
						C = static_cast<char>(C + 32);
					}
				}
				if (Lower.substr(0, 4) == "xn--")
				{
					if (!DecodePunycode(std::string_view(Lower).substr(4), Label))
					{
						return false;
					}
					std::string Encoded;
					if (!EncodePunycode(Label, Encoded) || Encoded != std::string_view(Lower).substr(4))
					{
						return false;
					}
				}
				else
				{
					for (unsigned char C : Lower)
					{
						Label.push_back(C);
					}
				}
			}
			else
			{
				if (!DecodeUtf8(Part, Label) || Label.size() > 63)
				{
					return false;
				}
				std::string Encoded;
				if (!EncodePunycode(Label, Encoded))
				{
					return false;
				}
				Length = 4 + Encoded.size();
			}
			if (!CheckLabel(Label) || Length > 63)
			{
				return false;
			}
			for (auto C : Label)
			{
				const auto Dir = Direction(C);
				Rtl |= Dir == D::R || Dir == D::AL || Dir == D::AN;
			}
			Total += Length + (Labels.empty() ? 0 : 1);
			if (Total > 253)
			{
				return false;
			}
			Labels.push_back(std::move(Label));
			if (Dot == std::string_view::npos)
			{
				break;
			}
			Offset = End + 1;
		}
		for (const auto& Label : Labels)
		{
			if (!Bidi(Label, Rtl))
			{
				return false;
			}
		}
		return !Labels.empty();
	}
	static void EncodeUtf8(std::uint32_t C, std::string& Out)
	{
		if (C < 0x80)
		{
			Out.push_back(static_cast<char>(C));
		}
		else if (C < 0x800)
		{
			Out.push_back(static_cast<char>(0xc0 | (C >> 6)));
			Out.push_back(static_cast<char>(0x80 | (C & 63)));
		}
		else if (C < 0x10000)
		{
			Out.push_back(static_cast<char>(0xe0 | (C >> 12)));
			Out.push_back(static_cast<char>(0x80 | ((C >> 6) & 63)));
			Out.push_back(static_cast<char>(0x80 | (C & 63)));
		}
		else
		{
			Out.push_back(static_cast<char>(0xf0 | (C >> 18)));
			Out.push_back(static_cast<char>(0x80 | ((C >> 12) & 63)));
			Out.push_back(static_cast<char>(0x80 | ((C >> 6) & 63)));
			Out.push_back(static_cast<char>(0x80 | (C & 63)));
		}
	}
	bool Hostname(std::string_view Text, bool International)
	{
		if (!International)
		{
			return HostnameMapped(Text, false);
		}
		if (Text.size() > 4096)
		{
			Fail(EError::Limit, "IDNA mapping input limit exceeded");
		}
		std::vector<std::uint32_t> Input, Label;
		std::string Mapped;
		if (!DecodeUtf8(Text, Input))
		{
			return false;
		}
		auto Emit = [&](std::uint32_t C)
		{
			if (C == '.')
			{
				for (auto N : Normalize(Label))
				{
					EncodeUtf8(N, Mapped);
				}
				Mapped.push_back('.');
				Label.clear();
			}
			else
			{
				if (Label.size() >= 256)
				{
					Fail(EError::Limit, "IDNA mapping label limit exceeded");
				}
				Label.push_back(C);
			}
		};
		for (auto C : Input)
		{
			auto* I = std::upper_bound(std::begin(U::Mapping),
				std::end(U::Mapping),
				C,
				[](auto C, const auto& M)
				{
					FSchemaSemantics::Check();
					return C < M.Start;
				});
			if (I == std::begin(U::Mapping))
			{
				return false;
			}
			const auto& M = I[-1];
			if (M.Status == 'V' || M.Status == 'D')
			{
				Emit(C);
			}
			else if (M.Status == 'M')
			{
				for (std::size_t N = 0; N < M.Length; ++N)
				{
					Emit(U::MappingText[M.Offset + N]);
				}
			}
			else if (M.Status != 'I')
			{
				return false;
			}
		}
		for (auto C : Normalize(Label))
		{
			EncodeUtf8(C, Mapped);
		}
		return HostnameMapped(Mapped, true);
	}
} // namespace VeroScript::Json::Detail::Formats

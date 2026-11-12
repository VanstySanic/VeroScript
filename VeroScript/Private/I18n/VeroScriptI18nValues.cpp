// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptI18nInternal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace VeroScript::I18n::Detail
{
	namespace
	{
		const char* DiagnosticId(EI18nStatus Status)
		{
			static constexpr const char* Ids[] = {"diag.i18n_ok", "diag.i18n_invalid_argument",
			    "diag.i18n_invalid_configuration", "diag.i18n_invalid_state", "diag.i18n_missing_resolver",
			    "diag.i18n_missing_message", "diag.i18n_unsupported_format", "diag.i18n_missing_argument",
			    "diag.i18n_argument_type", "diag.i18n_numeric_conversion", "diag.i18n_invalid_pattern",
			    "diag.i18n_unsupported_option", "diag.i18n_provider_failure", "diag.i18n_resource_limit",
			    "diag.i18n_out_of_memory", "diag.i18n_interrupted", "diag.i18n_reentrant_operation"};
			const auto Index = static_cast<unsigned>(Status);
			return Index < sizeof(Ids) / sizeof(*Ids) ? Ids[Index] : "diag.i18n_provider_failure";
		}
	}
	bool Fail(FI18nError& E, EI18nStatus Status, std::string Message)
	{
		E = {};
		E.Status = Status;
		E.Diagnostic.MessageId = DiagnosticId(Status);
		E.Diagnostic.Message = "i18n: " + Message;
		E.Diagnostic.MessageArgs = {std::move(Message), "", "", "", ""};
		return false;
	}

	void Identify(FI18nError& E, std::string_view Domain, std::string_view Key, std::string_view Language,
	    std::string_view Argument)
	{
		E.Domain = Domain;
		E.Key = Key;
		E.Language = Language;
		if (!Argument.empty())
		{
			E.ArgumentName = Argument;
		}

		// A provider's diagnostic arguments belong to its own message schema.
		// Only the exact core diagnostic for this status uses our context slots.
		if (E.Diagnostic.MessageId != DiagnosticId(E.Status)) return;
		E.Diagnostic.MessageArgs.resize(5);
		E.Diagnostic.MessageArgs[1] = E.Domain;
		E.Diagnostic.MessageArgs[2] = E.Key;
		E.Diagnostic.MessageArgs[3] = E.Language;
		E.Diagnostic.MessageArgs[4] = E.ArgumentName;
	}

	bool ValidName(std::string_view S) noexcept
	{
		if (S.empty()) return false;
		for (std::size_t I = 0; I < S.size();)
		{
			const auto C = static_cast<unsigned char>(S[I++]);
			if (!C) return false;
			if (C < 128)
			{
				continue;
			}

			unsigned N;
			std::uint32_t Value, Minimum;
			if (C >= 0xc2 && C <= 0xdf)
			{
				N = 1;
				Value = C & 31;
				Minimum = 128;
			}
			else if (C >= 0xe0 && C <= 0xef)
			{
				N = 2;
				Value = C & 15;
				Minimum = 2048;
			}
			else if (C >= 0xf0 && C <= 0xf4)
			{
				N = 3;
				Value = C & 7;
				Minimum = 65536;
			}
			else
			{
				return false;
			}

			if (N > S.size() - I) return false;
			while (N--)
			{
				const auto B = static_cast<unsigned char>(S[I++]);
				if ((B & 0xc0) != 0x80) return false;
				Value = (Value << 6) | (B & 63);
			}
			if (Value < Minimum || Value > 0x10ffff || (Value >= 0xd800 && Value <= 0xdfff)) return false;
		}
		return true;
	}

	bool ValidFormatId(std::string_view S) noexcept
	{
		bool First = true;
		for (char C : S)
		{
			if (C == '.')
			{
				if (First) return false;
				First = true;
				continue;
			}
			if (First && (C < 'a' || C > 'z')) return false;
			if (!First && !((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_')) return false;
			First = false;
		}
		return !First;
	}

	bool CheckWork(FI18nWork Work, FI18nError& E, std::uint64_t Units)
	{
		return !Work.Check || Work.Check(Work.UserData, Units) ||
		    Fail(E, EI18nStatus::Interrupted, "operation interrupted");
	}
	bool CheckSize(std::size_t Size, FI18nWork Work, FI18nError& E)
	{
		return !Work.MaxOutputBytes || Size <= Work.MaxOutputBytes ||
		    Fail(E, EI18nStatus::ResourceLimit, "output limit exceeded");
	}
}

namespace VeroScript::I18n
{
	using namespace Detail;

	EI18nArgumentKind FI18nArgument::Kind() const noexcept
	{
		if (Custom) return EI18nArgumentKind::Custom;
		#define VS_I18N_KIND(T, K) \
			if (std::holds_alternative<T>(Scalar)) return EI18nArgumentKind::K
		VS_I18N_KIND(std::string, String);
		VS_I18N_KIND(bool, Bool);
		VS_I18N_KIND(std::int8_t, Int8);
		VS_I18N_KIND(std::uint8_t, UInt8);
		VS_I18N_KIND(std::int16_t, Int16);
		VS_I18N_KIND(std::uint16_t, UInt16);
		VS_I18N_KIND(std::int32_t, Int);
		VS_I18N_KIND(std::uint32_t, Unsigned);
		VS_I18N_KIND(std::int64_t, Int64);
		VS_I18N_KIND(std::uint64_t, UInt64);
		VS_I18N_KIND(float, Float);
		#undef VS_I18N_KIND
		return EI18nArgumentKind::Double;
	}

	bool MakeArgument(const FValue& V, FI18nArgument& Out, FI18nError& E)
	{
		return Boundary(E,
		    [&]()
		{
			#define VS_I18N_ARG(T) \
				if (const auto* P = std::get_if<T>(&V)) \
				{ \
					Out = FI18nArgument(*P); \
					return true; \
				}
			VS_I18N_ARG(std::string)
			VS_I18N_ARG(bool)
			VS_I18N_ARG(std::int8_t)
			VS_I18N_ARG(std::uint8_t)
			VS_I18N_ARG(std::int16_t)
			VS_I18N_ARG(std::uint16_t)
			VS_I18N_ARG(std::int32_t)
			VS_I18N_ARG(std::uint32_t)
			VS_I18N_ARG(std::int64_t)
			VS_I18N_ARG(std::uint64_t)
			VS_I18N_ARG(float)
			VS_I18N_ARG(double)
			#undef VS_I18N_ARG
			return Fail(E, EI18nStatus::ArgumentType, "expected an owned scalar argument");
		});
	}
	bool MakeCustomArgument(std::shared_ptr<const II18nCustomValue> V, FI18nArgument& Out, FI18nError& E)
	{
		return Boundary(E,
		    [&]()
		{
			if (!V || !ValidFormatId(V->Type().Name) || !V->BorrowValue())
			{
				return Fail(E, EI18nStatus::InvalidArgument, "invalid custom value descriptor");
			}

			FI18nArgument Next;
			Next.CustomOwner = V->Type().ModuleOwner;
			Next.Custom = std::move(V);
			Out = std::move(Next);
			return true;
		});
	}
	bool FI18nArgument::TryString(std::string& Out, FI18nError& E) const
	{
		return Boundary(E,
		    [&]()
		{
			if (!Custom)
			{
				if (const auto* P = std::get_if<std::string>(&Scalar))
				{
					Out = *P;
					return true;
				}
			}

			return Fail(E, EI18nStatus::ArgumentType, "expected string argument");
		});
	}

	namespace
	{
		bool Signed(const FValue& V, std::int64_t& Out) noexcept
		{
			#define VS_I18N_SIGNED(T) \
				if (const auto* P = std::get_if<T>(&V)) \
				{ \
					Out = *P; \
					return true; \
				}
			VS_I18N_SIGNED(std::int8_t)
			VS_I18N_SIGNED(std::int16_t)
			VS_I18N_SIGNED(std::int32_t)
			VS_I18N_SIGNED(std::int64_t)
			#undef VS_I18N_SIGNED
			return false;
		}
		bool Unsigned(const FValue& V, std::uint64_t& Out) noexcept
		{
			#define VS_I18N_UNSIGNED(T) \
				if (const auto* P = std::get_if<T>(&V)) \
				{ \
					Out = *P; \
					return true; \
				}
			VS_I18N_UNSIGNED(std::uint8_t)
			VS_I18N_UNSIGNED(std::uint16_t)
			VS_I18N_UNSIGNED(std::uint32_t)
			VS_I18N_UNSIGNED(std::uint64_t)
			#undef VS_I18N_UNSIGNED
			return false;
		}
		bool Floating(const FValue& V, double& Out) noexcept
		{
			if (const auto* P = std::get_if<float>(&V))
			{
				Out = *P;
				return true;
			}
			if (const auto* P = std::get_if<double>(&V))
			{
				Out = *P;
				return true;
			}
			return false;
		}
		bool FitsMantissa(std::uint64_t N, unsigned Digits) noexcept
		{
			if (!N) return true;
			while (!(N & 1))
			{
				N >>= 1;
			}

			unsigned Bits = 0;
			while (N)
			{
				++Bits;
				N >>= 1;
			}
			return Bits <= Digits;
		}
		template <class T>
		bool ReadFloating(const FI18nArgument& A, T& Out, FI18nError& E)
		{
			E = {};
			Out = T{};
			if (A.Kind() == EI18nArgumentKind::Custom)
			{
				return Fail(E, EI18nStatus::ArgumentType, "expected numeric argument");
			}
			const auto& V = A.ScalarValue();
			if (const auto* P = std::get_if<T>(&V))
			{
				Out = *P;
				return true;
			}
			std::int64_t S = 0;
			std::uint64_t U = 0;
			double D = 0;
			if (Signed(V, S))
			{
				const auto Magnitude =
				    S < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(S) : static_cast<std::uint64_t>(S);
				if (!FitsMantissa(Magnitude, std::numeric_limits<T>::digits))
				{
					return Fail(E, EI18nStatus::NumericConversion, "integer cannot be represented exactly");
				}
				Out = static_cast<T>(S);
				return true;
			}
			if (Unsigned(V, U))
			{
				if (!FitsMantissa(U, std::numeric_limits<T>::digits))
				{
					return Fail(E, EI18nStatus::NumericConversion, "integer cannot be represented exactly");
				}
				Out = static_cast<T>(U);
				return true;
			}
			if (!Floating(V, D)) return Fail(E, EI18nStatus::ArgumentType, "expected numeric argument");
			if (!std::isfinite(D) || D > std::numeric_limits<T>::max() || D < -std::numeric_limits<T>::max())
			{
				return Fail(
				    E, EI18nStatus::NumericConversion, "floating conversion is outside the exact range");
			}

			const T Result = static_cast<T>(D);
			if (static_cast<double>(Result) != D)
			{
				return Fail(
				    E, EI18nStatus::NumericConversion, "floating value cannot be represented exactly");
			}
			Out = Result;
			return true;
		}
		template <class T>
		bool EqualBits(T A, T B) noexcept
		{
			return std::memcmp(&A, &B, sizeof(T)) == 0;
		}
	}

	bool FI18nArgument::TryBool(bool& Out, FI18nError& E) const
	{
		E = {};
		Out = false;
		if (!Custom)
		{
			if (const auto* P = std::get_if<bool>(&Scalar))
			{
				Out = *P;
				return true;
			}
		}

		return Fail(E, EI18nStatus::ArgumentType, "expected bool argument");
	}
	bool FI18nArgument::TryInt64(std::int64_t& Out, FI18nError& E) const
	{
		E = {};
		Out = 0;
		std::uint64_t U = 0;
		double D = 0;
		if (Custom) return Fail(E, EI18nStatus::ArgumentType, "expected numeric argument");
		if (Signed(Scalar, Out)) return true;
		if (Unsigned(Scalar, U))
		{
			if (U > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
			{
				return Fail(E, EI18nStatus::NumericConversion, "unsigned value exceeds int64 range");
			}
			Out = static_cast<std::int64_t>(U);
			return true;
		}
		if (!Floating(Scalar, D)) return Fail(E, EI18nStatus::ArgumentType, "expected numeric argument");
		if (!std::isfinite(D) || std::trunc(D) != D || D < -0x1p63 || D >= 0x1p63)
		{
			return Fail(E, EI18nStatus::NumericConversion, "value is not representable as int64");
		}

		Out = static_cast<std::int64_t>(D);
		return true;
	}
	bool FI18nArgument::TryUInt64(std::uint64_t& Out, FI18nError& E) const
	{
		E = {};
		Out = 0;
		std::int64_t S = 0;
		double D = 0;
		if (Custom) return Fail(E, EI18nStatus::ArgumentType, "expected numeric argument");
		if (Unsigned(Scalar, Out)) return true;
		if (Signed(Scalar, S))
		{
			if (S < 0) return Fail(E, EI18nStatus::NumericConversion, "negative value cannot be unsigned");
			Out = static_cast<std::uint64_t>(S);
			return true;
		}
		if (!Floating(Scalar, D)) return Fail(E, EI18nStatus::ArgumentType, "expected numeric argument");
		if (!std::isfinite(D) || std::trunc(D) != D || D < 0 || D >= 0x1p64)
		{
			return Fail(E, EI18nStatus::NumericConversion, "value is not representable as uint64");
		}

		Out = static_cast<std::uint64_t>(D);
		return true;
	}
	bool FI18nArgument::TryFloat(float& Out, FI18nError& E) const
	{
		return ReadFloating(*this, Out, E);
	}
	bool FI18nArgument::TryDouble(double& Out, FI18nError& E) const
	{
		return ReadFloating(*this, Out, E);
	}
	bool FI18nArgument::TryCustomView(const FI18nCustomType& T, const void*& Out, FI18nError& E) const
	{
		E = {};
		Out = nullptr;
		if (!Custom || &Custom->Type() != &T)
		{
			return Fail(E, EI18nStatus::ArgumentType, "incompatible custom value protocol");
		}
		Out = Custom->BorrowValue();
		return true;
	}
	bool FI18nArgument::operator==(const FI18nArgument& B) const noexcept
	{
		if (Kind() != B.Kind()) return false;
		if (Custom)
		{
			return Custom == B.Custom || (&Custom->Type() == &B.Custom->Type() && Custom->Equals(*B.Custom));
		}
		if (const auto* P = std::get_if<float>(&Scalar)) return EqualBits(*P, std::get<float>(B.Scalar));
		if (const auto* P = std::get_if<double>(&Scalar)) return EqualBits(*P, std::get<double>(B.Scalar));
		std::int64_t S = 0, R = 0;
		std::uint64_t U = 0, V = 0;
		if (Signed(Scalar, S)) return Signed(B.Scalar, R) && S == R;
		if (Unsigned(Scalar, U)) return Unsigned(B.Scalar, V) && U == V;
		if (const auto* P = std::get_if<bool>(&Scalar)) return *P == std::get<bool>(B.Scalar);
		return std::get<std::string>(Scalar) == std::get<std::string>(B.Scalar);
	}

	bool FI18nArguments::Set(std::string Name, FI18nArgument Value, FI18nError& E)
	{
		return Boundary(E,
		    [&]()
		{
			if (!ValidName(Name)) return Fail(E, EI18nStatus::InvalidArgument, "invalid parameter name");
			auto Next = std::make_shared<FEntries>(Values());
			Next->insert_or_assign(std::move(Name), std::move(Value));
			if (Next->size() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
			{
				return Fail(E, EI18nStatus::ResourceLimit, "too many parameters");
			}
			Entries = std::move(Next);
			return true;
		});
	}
	bool FI18nArguments::Get(std::string_view Name, FI18nArgument& Out, FI18nError& E) const
	{
		return Boundary(E,
		    [&]()
		{
			if (!ValidName(Name)) return Fail(E, EI18nStatus::InvalidArgument, "invalid parameter name");
			const auto I = Values().find(Name);
			if (I == Values().end())
			{
				Fail(E, EI18nStatus::MissingArgument, "missing parameter: " + std::string(Name));
				Identify(E, {}, {}, {}, Name);
				return false;
			}
			Out = I->second;
			return true;
		});
	}
	bool FI18nArguments::Contains(std::string_view Name, bool& Out, FI18nError& E) const
	{
		E = {};
		Out = false;
		if (!ValidName(Name)) return Fail(E, EI18nStatus::InvalidArgument, "invalid parameter name");
		Out = Values().find(Name) != Values().end();
		return true;
	}
	bool FI18nArguments::Remove(std::string_view Name, FI18nError& E)
	{
		return Boundary(E,
		    [&]()
		{
			if (!ValidName(Name)) return Fail(E, EI18nStatus::InvalidArgument, "invalid parameter name");
			if (Values().find(Name) == Values().end()) return true;
			auto Next = std::make_shared<FEntries>(Values());
			Next->erase(std::string(Name));
			Entries = std::move(Next);
			return true;
		});
	}
	const FI18nArguments::FEntries& FI18nArguments::Values() const noexcept
	{
		static const FEntries Empty;
		return Entries ? *Entries : Empty;
	}
	std::int64_t FI18nArguments::Size() const noexcept
	{
		return static_cast<std::int64_t>(Values().size());
	}
	bool FI18nArguments::operator==(const FI18nArguments& B) const noexcept
	{
		return Entries == B.Entries || Values() == B.Values();
	}
	bool FI18nConfiguration::operator==(const FI18nConfiguration& B) const noexcept
	{
		return Language == B.Language && FallbackLanguages == B.FallbackLanguages &&
		    FormatLocale == B.FormatLocale && FormatOptions == B.FormatOptions;
	}

	struct FI18nText::FData
	{
		bool Literal = true;
		std::string Value, Domain, Key;
		FI18nArguments Params;
	};
	FI18nText::FI18nText() : FI18nText(std::string{})
	{
	}
	FI18nText::FI18nText(std::string Literal)
	{
		auto Next = std::make_shared<FData>();
		Next->Value = std::move(Literal);
		Data = std::move(Next);
	}
	bool FI18nText::IsLiteral() const noexcept
	{
		return !Data || Data->Literal;
	}
	const std::string& FI18nText::LiteralValue() const noexcept
	{
		static const std::string Empty;
		return Data ? Data->Value : Empty;
	}
	const std::string& FI18nText::Domain() const noexcept
	{
		static const std::string Empty;
		return Data ? Data->Domain : Empty;
	}
	const std::string& FI18nText::Key() const noexcept
	{
		static const std::string Empty;
		return Data ? Data->Key : Empty;
	}
	const FI18nArguments& FI18nText::Params() const noexcept
	{
		static const FI18nArguments Empty;
		return Data ? Data->Params : Empty;
	}
	bool FI18nText::Message(
	    std::string Domain, std::string Key, FI18nArguments Params, FI18nText& Out, FI18nError& E)
	{
		return Boundary(E,
		    [&]()
		{
			if (!ValidName(Domain) || !ValidName(Key))
			{
				return Fail(E, EI18nStatus::InvalidArgument, "invalid resource domain or key");
			}
			auto Next = std::make_shared<FData>();
			Next->Literal = false;
			Next->Domain = std::move(Domain);
			Next->Key = std::move(Key);
			Next->Params = std::move(Params);
			Out.Data = std::move(Next);
			return true;
		});
	}
	bool FI18nText::operator==(const FI18nText& B) const noexcept
	{
		return Data == B.Data ||
		    (IsLiteral() == B.IsLiteral() && LiteralValue() == B.LiteralValue() && Domain() == B.Domain() &&
		        Key() == B.Key() && Params() == B.Params());
	}
	bool FI18nVersion::operator==(const FI18nVersion& B) const noexcept
	{
		return ContextId == B.ContextId && ConfigurationRevision == B.ConfigurationRevision &&
		    ResourceRevision == B.ResourceRevision && FormatterRevision == B.FormatterRevision;
	}
}

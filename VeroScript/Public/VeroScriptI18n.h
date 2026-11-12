// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptValue.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace VeroScript
{
	struct FNativeFunction;
	class IHostBindingCatalog;
}

namespace VeroScript::I18n
{
	class FI18nArguments;
	class II18nResourceSnapshot;
	class II18nFormatterSnapshot;

	enum class EI18nStatus : std::uint8_t
	{
		Ok,
		InvalidArgument,
		InvalidConfiguration,
		InvalidState,
		MissingResolver,
		MissingMessage,
		UnsupportedFormat,
		MissingArgument,
		ArgumentType,
		NumericConversion,
		InvalidPattern,
		UnsupportedOption,
		ProviderFailure,
		ResourceLimit,
		OutOfMemory,
		Interrupted,
		ReentrantOperation
	};

	enum class EI18nArgumentKind : std::uint8_t
	{
		String,
		Bool,
		Int8,
		UInt8,
		Int16,
		UInt16,
		Int,
		Unsigned,
		Int64,
		UInt64,
		Float,
		Double,
		Custom
	};

	enum class EI18nLookupResult : std::uint8_t
	{
		Found,
		NotFound,
		Failed
	};

	struct FI18nError
	{
		EI18nStatus Status = EI18nStatus::Ok;
		FScriptError Diagnostic;
		std::string Domain, Key, Language, ArgumentName;
	};

	struct FI18nLimits
	{
		std::uint64_t MaxOutputBytes = 0;
	};

	// Borrowed only for a synchronous operation. Null Check is an explicit,
	// unrestricted native operation; script calls supply their current VM control.
	struct FI18nWork
	{
		bool (*Check)(void*, std::uint64_t WorkUnits) = nullptr;
		void* UserData = nullptr;
		std::uint64_t MaxOutputBytes = 0;
	};

	struct FI18nCustomType
	{
		std::string Name;
		std::shared_ptr<const void> ModuleOwner;
	};

	struct FI18nVersion
	{
		bool operator==(const FI18nVersion& Other) const noexcept;
		bool operator!=(const FI18nVersion& Other) const noexcept
		{
			return !(*this == Other);
		}

		std::uint64_t ContextId = 0, ConfigurationRevision = 0;
		std::uint64_t ResourceRevision = 0, FormatterRevision = 0;
	};

	struct FI18nLookupRequest
	{
		std::string_view Domain, Key, Language;
	};

	struct FI18nMessage
	{
		std::string Pattern, FormatId;
	};

	struct FI18nFormatRequest
	{
		std::string_view Pattern, FormatId;
		const FI18nArguments& Arguments;
		std::string_view RequestedLanguage, MatchedLanguage, FormatLocale;
		const FI18nArguments& Options;
	};

	struct FI18nProviders
	{
		FI18nProviders() = default;
		FI18nProviders(const FI18nProviders&) = default;
		FI18nProviders(FI18nProviders&&) = default;
		FI18nProviders& operator=(FI18nProviders Other) noexcept
		{
			ResourceOwner.swap(Other.ResourceOwner);
			FormatterOwner.swap(Other.FormatterOwner);
			Resources.swap(Other.Resources);
			Formatter.swap(Other.Formatter);
			return *this;
		}
		// Owners must outlive provider virtual destructors and foreign deleters.
		std::shared_ptr<const void> ResourceOwner, FormatterOwner;
		std::shared_ptr<const II18nResourceSnapshot> Resources;
		std::shared_ptr<const II18nFormatterSnapshot> Formatter;
	};

	struct FI18nServicesSnapshot : FI18nProviders
	{
		std::uint64_t ResourceRevision = 0, FormatterRevision = 0;
	};

	struct FI18nMessageTemplate
	{
		bool Valid = false;
		std::string Pattern, FormatId, MatchedLanguage;
		FI18nVersion Version;
	};

	struct FI18nResolvedText
	{
		bool Valid = false;
		std::string Value, MatchedLanguage;
		FI18nVersion Version;
	};

	// Implementations must own immutable data. Equals is pure, allocation-free,
	// thread-safe and non-throwing. Reuse one descriptor to establish compatibility.
	class II18nCustomValue
	{
	public:
		virtual ~II18nCustomValue() = default;
		// The immutable descriptor and borrowed payload outlive this owned value.
		virtual const FI18nCustomType& Type() const noexcept = 0;
		virtual bool Equals(const II18nCustomValue& Other) const noexcept = 0;
		virtual const void* BorrowValue() const noexcept = 0;
	};

	class FI18nArgument
	{
	public:
		FI18nArgument() = default;
		FI18nArgument(const FI18nArgument&) = default;
		FI18nArgument(FI18nArgument&&) = default;
		FI18nArgument& operator=(FI18nArgument Other)
		{
			Scalar.swap(Other.Scalar);
			CustomOwner.swap(Other.CustomOwner);
			Custom.swap(Other.Custom);
			return *this;
		}
		explicit FI18nArgument(std::string Value) : Scalar(std::move(Value))
		{
		}
		explicit FI18nArgument(const char* Value) : Scalar(std::string(Value ? Value : ""))
		{
		}
		template <class T, std::enable_if_t<std::is_same_v<T, bool> || std::is_same_v<T, std::int8_t> || std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::uint16_t> || std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t> || std::is_same_v<T, float> || std::is_same_v<T, double>, int> = 0>
		explicit FI18nArgument(T Value) : Scalar(Value)
		{
		}

		EI18nArgumentKind Kind() const noexcept;
		// Scalar alternatives are owned and restricted to the documented kinds.
		const FValue& ScalarValue() const noexcept
		{
			return Scalar;
		}
		bool TryString(std::string& Out, FI18nError& Error) const;
		bool TryBool(bool& Out, FI18nError& Error) const;
		bool TryInt64(std::int64_t& Out, FI18nError& Error) const;
		bool TryUInt64(std::uint64_t& Out, FI18nError& Error) const;
		bool TryFloat(float& Out, FI18nError& Error) const;
		bool TryDouble(double& Out, FI18nError& Error) const;
		// Borrowed until this argument is released; the descriptor must match by identity.
		bool TryCustomView(const FI18nCustomType& Expected, const void*& Out,
		    FI18nError& Error) const;
		bool operator==(const FI18nArgument& Other) const noexcept;
		bool operator!=(const FI18nArgument& Other) const noexcept
		{
			return !(*this == Other);
		}

	private:
		friend bool MakeCustomArgument(std::shared_ptr<const II18nCustomValue>,
		    FI18nArgument&, FI18nError&);
		FValue Scalar = std::string{};
		// Destroy the foreign value before releasing its module pin.
		std::shared_ptr<const void> CustomOwner;
		std::shared_ptr<const II18nCustomValue> Custom;
	};

	bool MakeArgument(const FValue& Value, FI18nArgument& Out, FI18nError& Error);
	bool MakeCustomArgument(std::shared_ptr<const II18nCustomValue> Value,
	    FI18nArgument& Out, FI18nError& Error);

	class FI18nArguments
	{
	public:
		using FEntries = std::map<std::string, FI18nArgument, std::less<>>;
		bool Set(std::string Name, FI18nArgument Value, FI18nError& Error);
		bool Get(std::string_view Name, FI18nArgument& Out, FI18nError& Error) const;
		bool Contains(std::string_view Name, bool& Out, FI18nError& Error) const;
		bool Remove(std::string_view Name, FI18nError& Error);
		void Clear() noexcept
		{
			Entries.reset();
		}
		const FEntries& Values() const noexcept;
		std::int64_t Size() const noexcept;
		bool operator==(const FI18nArguments& Other) const noexcept;
		bool operator!=(const FI18nArguments& Other) const noexcept
		{
			return !(*this == Other);
		}

	private:
		std::shared_ptr<const FEntries> Entries;
	};

	// FI18nConfiguration stores FI18nArguments by value and needs its complete definition.
	struct FI18nConfiguration
	{
		bool operator==(const FI18nConfiguration& Other) const noexcept;

		std::string Language = "en-US";
		std::vector<std::string> FallbackLanguages;
		// Empty means follow Language, including subsequent language changes.
		std::string FormatLocale;
		FI18nArguments FormatOptions;
	};

	bool NormalizeLanguageTag(std::string_view Input, std::string& Out, FI18nError& Error);
	bool NormalizeConfiguration(const FI18nConfiguration& Input,
	    FI18nConfiguration& Out, FI18nError& Error);

	class FI18nText
	{
	public:
		FI18nText();
		explicit FI18nText(std::string Literal);
		bool IsLiteral() const noexcept;
		const std::string& LiteralValue() const noexcept;
		const std::string& Domain() const noexcept;
		const std::string& Key() const noexcept;
		const FI18nArguments& Params() const noexcept;
		static bool Message(std::string Domain, std::string Key, FI18nArguments Params,
		    FI18nText& Out, FI18nError& Error);
		bool operator==(const FI18nText& Other) const noexcept;
		bool operator!=(const FI18nText& Other) const noexcept
		{
			return !(*this == Other);
		}

	private:
		struct FData;
		std::shared_ptr<const FData> Data;
	};

	// Provider views are immutable and safe for concurrent calls. Request views
	// and work services are borrowed until return and must never be retained.
	class II18nResourceSnapshot
	{
	public:
		virtual ~II18nResourceSnapshot() = default;
		virtual EI18nLookupResult Lookup(const FI18nLookupRequest& Request,
		    const FI18nWork& Work, FI18nMessage& OutMessage, FI18nError& OutError) const = 0;
	};
	class II18nFormatterSnapshot
	{
	public:
		virtual ~II18nFormatterSnapshot() = default;
		virtual bool SupportsFormat(std::string_view FormatId) const = 0;
		virtual bool Format(const FI18nFormatRequest& Request, const FI18nWork& Work,
		    std::string& OutText, FI18nError& OutError) const = 0;
	};

	class FI18nServices
	{
	public:
		FI18nServices();
		~FI18nServices();
		FI18nServices(const FI18nServices&) = delete;
		FI18nServices& operator=(const FI18nServices&) = delete;
		// Thread-safe publication/acquisition. Use a fresh view when contents change.
		// Partial publishes preserve the other latest view; identical views are a no-op.
		std::shared_ptr<const FI18nServicesSnapshot> Acquire() const;
		bool Publish(FI18nProviders Providers, FI18nError& Error);
		bool PublishResources(std::shared_ptr<const II18nResourceSnapshot> View,
		    FI18nError& Error, std::shared_ptr<const void> Owner = {});
		bool PublishFormatter(std::shared_ptr<const II18nFormatterSnapshot> View,
		    FI18nError& Error, std::shared_ptr<const void> Owner = {});

	private:
		struct FState;

		bool PublishImpl(FI18nProviders Providers, bool Resources, bool Formatter, FI18nError& Error);

		std::unique_ptr<FState> State;
	};

	class FI18nContext;
	class FI18nSnapshot
	{
	public:
		// Owns configuration and both provider views. Operations use only this state;
		// their current work/budget is supplied separately, including for old snapshots.
		bool IsValid() const noexcept
		{
			return static_cast<bool>(Data);
		}
		FI18nConfiguration Configuration() const;
		FI18nVersion Version() const noexcept;
		bool TryLookup(std::string_view Domain, std::string_view Key,
		    FI18nMessageTemplate& Out, FI18nError& Error, FI18nWork Work = {}) const;
		bool TryResolve(const FI18nText& Text, FI18nResolvedText& Out,
		    FI18nError& Error, FI18nWork Work = {}) const;
		bool TryFormat(std::string_view Pattern, const FI18nArguments& Args,
		    std::string& Out, FI18nError& Error, FI18nWork Work = {},
		    std::string_view FormatId = "vero.named") const;

	private:
		friend class FI18nContext;
		struct FData;

		bool LookupImpl(std::string_view Domain, std::string_view Key,
		    FI18nMessageTemplate& Out, FI18nError& Error, FI18nWork Work) const;
		bool FormatImpl(std::string_view Pattern, const FI18nArguments& Args,
		    std::string_view FormatId, std::string_view MatchedLanguage,
		    std::string& Out, FI18nError& Error, FI18nWork Work) const;

		std::shared_ptr<const FData> Data;
	};

	class FI18nContext
	{
	public:
		~FI18nContext();
		FI18nContext(const FI18nContext&) = delete;
		FI18nContext& operator=(const FI18nContext&) = delete;
		// Context methods are thread-safe; ordinary argument/config value mutation
		// requires caller synchronization. Returned configuration is a value copy.
		FI18nSnapshot CaptureSnapshot() const;
		FI18nConfiguration Configuration() const;
		bool Configure(const FI18nConfiguration& Config, FI18nError& Error);
		bool SetLanguage(std::string Language, FI18nError& Error);
		bool SetFallbackLanguages(std::vector<std::string> Languages, FI18nError& Error);
		bool SetFormatLocale(std::string Locale, FI18nError& Error);
		bool SetFormatOptions(FI18nArguments Options, FI18nError& Error);
		bool SetServices(std::shared_ptr<FI18nServices> Services, FI18nError& Error);
		bool ConfigureAndSetServices(const FI18nConfiguration& Config,
		    std::shared_ptr<FI18nServices> Services, FI18nError& Error);
		bool Clone(std::shared_ptr<FI18nContext>& Out, FI18nError& Error) const;

	private:
		friend bool CreateContext(const FI18nConfiguration&, std::shared_ptr<FI18nServices>,
		    std::shared_ptr<FI18nContext>&, FI18nError&);
		FI18nContext();
		struct FState;

		bool Update(const FI18nConfiguration& Config, std::shared_ptr<FI18nServices> Services,
		    int Field, bool ChangeServices, FI18nError& Error);

		std::unique_ptr<FState> State;
	};

	bool CreateContext(const FI18nConfiguration& Config, std::shared_ptr<FI18nServices> Services,
	    std::shared_ptr<FI18nContext>& Out, FI18nError& Error);
	bool TryFormatNamed(std::string_view Pattern, const FI18nArguments& Args,
	    std::string& Out, FI18nError& Error, FI18nWork Work = {});

	const IHostBindingCatalog& GetBindingCatalog();
	// Both directions validate the core type/protocol identity, not just a Payload cast.
	bool ToScriptValue(const FI18nText& Value, FValue& Out, FI18nError& Error);
	bool ToScriptValue(std::shared_ptr<FI18nContext> Value, FValue& Out, FI18nError& Error);
	bool FromScriptValue(const FValue& Value, FI18nText& Out, FI18nError& Error);
	bool FromScriptValue(const FValue& Value, std::shared_ptr<FI18nContext>& Out, FI18nError& Error);
	bool ToScriptValue(const FI18nArgument& Value, FValue& Out, FI18nError& Error);
	bool ToScriptValue(const FI18nArguments& Value, FValue& Out, FI18nError& Error);
	bool ToScriptValue(const FI18nConfiguration& Value, FValue& Out, FI18nError& Error);
	bool ToScriptValue(const FI18nVersion& Value, FValue& Out, FI18nError& Error);
	bool ToScriptValue(const FI18nMessageTemplate& Value, FValue& Out, FI18nError& Error);
	bool ToScriptValue(const FI18nResolvedText& Value, FValue& Out, FI18nError& Error);
	bool ToScriptValue(const FI18nSnapshot& Value, FValue& Out, FI18nError& Error);
	bool FromScriptValue(const FValue& Value, FI18nArgument& Out, FI18nError& Error);
	bool FromScriptValue(const FValue& Value, FI18nArguments& Out, FI18nError& Error);
	bool FromScriptValue(const FValue& Value, FI18nConfiguration& Out, FI18nError& Error);
	bool FromScriptValue(const FValue& Value, FI18nVersion& Out, FI18nError& Error);
	bool FromScriptValue(const FValue& Value, FI18nMessageTemplate& Out, FI18nError& Error);
	bool FromScriptValue(const FValue& Value, FI18nResolvedText& Out, FI18nError& Error);
	bool FromScriptValue(const FValue& Value, FI18nSnapshot& Out, FI18nError& Error);

	// Borrowed from the current native call, never retained by values or snapshots.
	class II18nCallServices
	{
	public:
		virtual ~II18nCallServices() = default;
		virtual std::shared_ptr<FI18nServices> GetI18nServices() const = 0;
		virtual FI18nWork GetI18nWork() = 0;
	};
}

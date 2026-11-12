// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VeroScript::Json
{
	enum class EKind : std::uint8_t
	{
		Null,
		Boolean,
		Number,
		String,
		Array,
		Object
	};

	enum class EError : std::uint8_t
	{
		None,
		Syntax,
		InvalidUtf8,
		DuplicateKey,
		Type,
		Range,
		Missing,
		Pointer,
		Patch,
		Limit,
		Cancelled,
		Allocation,
		State,
		Sink,
		Unsupported,
		Callback,
		Schema,
		Resource,
		Cycle
	};

	enum class EDuplicateKeys : std::uint8_t
	{
		Reject,
		KeepFirst,
		KeepLast,
		ReportAll
	};

	enum class EEvent : std::uint8_t
	{
		BeginArray,
		EndArray,
		BeginObject,
		EndObject,
		Key,
		Null,
		Boolean,
		Number,
		String
	};

	enum class EReadState : std::uint8_t
	{
		NeedInput,
		EventReady,
		Done,
		Failed
	};

	enum class ESinkState : std::uint8_t
	{
		Progress,
		NeedOutput,
		Failed
	};

	enum class EWriteState : std::uint8_t
	{
		Active,
		NeedOutput,
		Done,
		Failed
	};

	inline constexpr const char* Version = "0.1.1";

	struct FError
	{
		explicit operator bool() const noexcept;
		// Stable even if allocating the detailed error text failed.
		const char* Id() const noexcept;
		EError Code = EError::None;
		std::string MessageId;
		std::string Message;
		bool bHasSource = false;
		std::uint64_t ByteOffset = 0;
		std::uint64_t Line = 1;
		std::uint64_t Column = 1;
		std::string InstancePointer;
		std::int64_t PatchOperation = -1;
	};

	struct FLimits
	{
		std::uint64_t InputBytes = 64 * 1024 * 1024;
		std::uint64_t OutputBytes = 64 * 1024 * 1024;
		std::uint64_t TokenBytes = 16 * 1024 * 1024;
		std::uint64_t Nodes = 1000000;
		std::uint64_t Depth = 256;
		std::uint64_t NumberBytes = 4096;
		std::uint64_t Exponent = 100000;
		std::uint64_t WorkUnits = 64 * 1024 * 1024;
	};

	struct FWork
	{
		// Borrowed for this call only. False aborts before committing an output.
		bool (*Check)(void*, std::uint64_t) = nullptr;
		void* UserData = nullptr;
	};

	struct FParseOptions
	{
		FLimits Limits;
		EDuplicateKeys DuplicateKeys = EDuplicateKeys::Reject;
		bool bAllowLeadingBom = false;
	};

	struct FWriteOptions
	{
		FLimits Limits;
		bool bPretty = false;
		bool bSortKeys = false;
		std::uint32_t Indent = 2;
	};

	struct FEvent
	{
		EEvent Kind = EEvent::Null;
		// Decoded UTF-8 for keys/strings; the original token for numbers.
		std::string Text;
		bool Boolean = false;
	};

	struct FFeedResult
	{
		std::size_t Consumed = 0;
		EReadState State = EReadState::NeedInput;
	};

	struct FSinkResult
	{
		std::size_t Bytes = 0;
		ESinkState State = ESinkState::Progress;
	};

	using FSink = std::function<FSinkResult(std::string_view)>;

	namespace Detail
	{
		struct FNode;
		struct FAccess;
	} // namespace Detail

	/** Immutable shared nodes; each mutating wrapper commits a fresh root. */
	class FJsonValue
	{
		friend struct Detail::FAccess;

	public:
		FJsonValue() noexcept;
		explicit FJsonValue(bool Value);
		explicit FJsonValue(std::int64_t Value);
		explicit FJsonValue(std::uint64_t Value);
		static FJsonValue Array();
		static FJsonValue Object();
		static bool TryString(std::string_view Text, FJsonValue& Out, FError& Error);
		static bool TryNumber(std::string_view Text, FJsonValue& Out, FError& Error, const FLimits& Limits = {});
		static bool TryDouble(double Value, FJsonValue& Out, FError& Error);
		EKind Kind() const noexcept;
		bool TryBoolean(bool& Out, FError& Error) const;
		bool TryString(std::string& Out, FError& Error) const;
		bool TryNumberText(std::string& Out, FError& Error) const;
		bool TryInt64(std::int64_t& Out, FError& Error) const;
		bool TryUInt64(std::uint64_t& Out, FError& Error) const;
		bool TryDouble(double& Out, FError& Error, bool bExact = false) const;
		bool TrySize(std::int64_t& Out, FError& Error) const;
		bool TryAt(std::int64_t Index, FJsonValue& Out, FError& Error) const;
		bool TryGet(std::string_view Key, FJsonValue& Out, FError& Error) const;
		bool TryContains(std::string_view Key, bool& Out, FError& Error) const;
		bool TryKeys(std::vector<std::string>& Out, FError& Error) const;
		bool TrySet(std::string_view Key, const FJsonValue& Value, FError& Error);
		bool TryErase(std::string_view Key, FError& Error);
		bool TryInsert(std::int64_t Index, const FJsonValue& Value, FError& Error);
		bool TryReplace(std::int64_t Index, const FJsonValue& Value, FError& Error);
		bool TryErase(std::int64_t Index, FError& Error);
		bool TryPush(const FJsonValue& Value, FError& Error);
		bool TryEquals(
			const FJsonValue& Other, bool& Out, FError& Error, const FLimits& Limits = {}, FWork Work = {}) const;

	private:
		std::shared_ptr<const Detail::FNode> Node;
	};

	// FDocument follows FJsonValue because it embeds a complete value.
	struct FDocument
	{
		bool bHasValue = true;
		FJsonValue Value;
	};

	/** Shared mutable builder handle. Finish closes it; Reset starts another value. */
	class FBuilder
	{
	public:
		explicit FBuilder(EKind Kind = EKind::Array);
		bool TryPush(const FJsonValue& Value, FError& Error);
		bool TrySet(std::string Key, const FJsonValue& Value, FError& Error);
		bool TryReserve(std::int64_t Capacity, FError& Error);
		bool TryCapacity(std::int64_t& Out, FError& Error) const;
		bool TrySize(std::int64_t& Out, FError& Error) const;
		bool TryFinish(FJsonValue& Out, FError& Error);
		bool TryReset(EKind Kind, FError& Error);

	private:
		struct FState;
		std::shared_ptr<FState> State;
	};

	/** Parsed, reusable RFC 6901 path. The default value selects the root. */
	class FPointer
	{
	public:
		static bool TryCompile(std::string_view Text, FPointer& Out, FError& Error, bool bUriFragment = false,
			const FLimits& Limits = {}, FWork Work = {});
		bool TryResolve(const FDocument& Document, FJsonValue& Out, FError& Error, const FLimits& Limits = {},
			FWork Work = {}) const;

	private:
		std::vector<std::string> Tokens;
	};

	bool TryParse(
		std::string_view Text, FJsonValue& Out, FError& Error, const FParseOptions& Options = {}, FWork Work = {});
	bool TryWrite(
		const FJsonValue& Value, std::string& Out, FError& Error, const FWriteOptions& Options = {}, FWork Work = {});
	bool TryParsePointer(std::string_view Text, std::vector<std::string>& Out, FError& Error, bool bUriFragment = false,
		const FLimits& Limits = {}, FWork Work = {});
	bool TryResolve(const FDocument& Document, std::string_view Pointer, FJsonValue& Out, FError& Error,
		const FLimits& Limits = {}, FWork Work = {});
	bool TryPatch(const FDocument& Input, const FJsonValue& Patch, FDocument& Out, FError& Error,
		const FLimits& Limits = {}, FWork Work = {});
	bool TryMergePatch(const FJsonValue& Input, const FJsonValue& Patch, FJsonValue& Out, FError& Error,
		const FLimits& Limits = {}, FWork Work = {});

	/** Pull one event at a time. Unconsumed input remains owned by the caller. */
	class FReader
	{
	public:
		explicit FReader(FParseOptions Options = {});
		~FReader();
		FReader(FReader&&) noexcept;
		FReader& operator=(FReader&&) noexcept;
		FReader(const FReader&) = delete;
		FReader& operator=(const FReader&) = delete;
		FFeedResult Feed(std::string_view Input, bool bFinal, FEvent& Event, FError& Error, FWork Work = {});
		// A false visitor result pauses after that event; EventReady means paused.
		FFeedResult Visit(std::string_view Input, bool bFinal, const std::function<bool(const FEvent&)>& Visitor,
			FError& Error, FWork Work = {});
		void Reset();

	private:
		struct FState;
		std::unique_ptr<FState> State;
	};

	/** Event writes are transactional before acceptance. Resume never resubmits an event. */
	class FWriter
	{
	public:
		explicit FWriter(FSink Sink, FWriteOptions Options = {});
		~FWriter();
		FWriter(FWriter&&) noexcept;
		FWriter& operator=(FWriter&&) noexcept;
		FWriter(const FWriter&) = delete;
		FWriter& operator=(const FWriter&) = delete;
		EWriteState Write(const FEvent& Event, bool& bAccepted, FError& Error, FWork Work = {});
		EWriteState Resume(FError& Error, FWork Work = {});
		EWriteState Finish(FError& Error, FWork Work = {});

	private:
		struct FState;
		std::unique_ptr<FState> State;
	};
} // namespace VeroScript::Json

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJson.h"
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript::Json
{
	enum class EMappingDirection : std::uint8_t
	{
		Input,
		Output
	};
	struct FMappingOptions
	{
		FLimits Limits;
		bool bIgnoreUnknownFields = false;
		bool bMapAsPairs = false;
		bool bEnumsAsNumbers = false;
		bool bExactFloatingPoint = false;
	};
	struct FFieldOptions
	{
		bool bRequired = false;
		bool bIgnore = false;
		bool bSerialize = true;
		bool bDeserialize = true;
	};
	class FMappingContext;
	struct FTypeAdapter
	{
		FTypeAdapter() = default;
		FTypeAdapter(const FTypeAdapter&) = default;
		FTypeAdapter(FTypeAdapter&&) noexcept = default;
		FTypeAdapter& operator=(FTypeAdapter Other) noexcept
		{
			Name.swap(Other.Name);
			Owner.swap(Other.Owner);
			Encode.swap(Other.Encode);
			Decode.swap(Other.Decode);
			Schema.swap(Other.Schema);
			return *this;
		}
		std::string Name;
		std::shared_ptr<const void> Owner;
		std::function<bool(const void*, FMappingContext&, FJsonValue&)> Encode;
		std::function<bool(const FJsonValue&, FMappingContext&, std::shared_ptr<void>&)> Decode;
		std::function<bool(FMappingContext&, EMappingDirection, FJsonValue&)> Schema;
	};
	class FMappingRegistry
	{
		friend class FMappingContext;

	public:
		FMappingRegistry();
		// Registration is atomic; active calls retain an immutable registry snapshot.
		bool Register(std::type_index Type, FTypeAdapter Adapter, FError& Error, bool bReplace = false);

	private:
		struct FImpl;
		std::shared_ptr<FImpl> Impl;
	};

	template <class T>
	struct TMappingConverter
	{
		TMappingConverter() = default;
		TMappingConverter(const TMappingConverter&) = default;
		TMappingConverter(TMappingConverter&&) noexcept = default;
		TMappingConverter& operator=(TMappingConverter Other) noexcept
		{
			Name.swap(Other.Name);
			Owner.swap(Other.Owner);
			Encode.swap(Other.Encode);
			Decode.swap(Other.Decode);
			Schema.swap(Other.Schema);
			return *this;
		}
		std::string Name;
		std::shared_ptr<const void> Owner;
		std::function<bool(const T&, FMappingContext&, FJsonValue&)> Encode;
		std::function<bool(const FJsonValue&, FMappingContext&, std::shared_ptr<T>&)> Decode;
		std::function<bool(FMappingContext&, EMappingDirection, FJsonValue&)> Schema;
	};
	template <class T>
	bool RegisterConverter(FMappingRegistry&, TMappingConverter<T>, FError&, bool bReplace = false);

	class FMappingContext
	{
	public:
		FMappingContext(const FMappingRegistry&, const FMappingOptions&, FError&, FWork);
		~FMappingContext();
		FMappingContext(const FMappingContext&) = delete;
		FMappingContext& operator=(const FMappingContext&) = delete;
		const FMappingOptions& Options() const noexcept;
		FError& Error() noexcept;
		bool Fail(EError, const char* Message);
		bool Check(std::uint64_t Units = 1);
		bool EnterValue();
		bool CheckSegment(std::string_view);
		bool Locate(bool Result);
		bool ValidateJson(const FJsonValue&, bool bInput = false);
		class FPath
		{
		public:
			FPath(FMappingContext&, std::string_view Segment);
			~FPath();
			FPath(const FPath&) = delete;
			FPath& operator=(const FPath&) = delete;

		private:
			FMappingContext& Context;
			std::size_t Size;
		};
		template <class T>
		bool Encode(const T&, FJsonValue&);
		template <class T>
		bool Decode(const FJsonValue&, std::shared_ptr<T>&);
		template <class T>
		bool Schema(EMappingDirection, FJsonValue&);
		bool WrapSchema(const FJsonValue&, FJsonValue&);
		std::shared_ptr<const FTypeAdapter> Find(std::type_index) const;
		bool EncodeAdapter(const FTypeAdapter&, const void*, FJsonValue&);
		bool SchemaAdapter(std::type_index, const FTypeAdapter&, EMappingDirection, FJsonValue&);
		// Preserve converter code and factories for each decoded adapter result, including nested shared values.
		std::shared_ptr<void> RetainDecoded(std::shared_ptr<void> Value);

	private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};

	template <class T>
	bool TryEncode(
		const T&, FJsonValue& Out, FError&, const FMappingRegistry&, const FMappingOptions& = {}, FWork = {});
	// Construction and field assignment occur in a new object; failure leaves Out untouched.
	template <class T>
	bool TryDecode(const FJsonValue&, std::shared_ptr<T>& Out, FError&, const FMappingRegistry&,
		const FMappingOptions& = {}, FWork = {});
	template <class T>
	bool TryMappingSchema(
		EMappingDirection, FJsonValue& Out, FError&, const FMappingRegistry&, const FMappingOptions& = {}, FWork = {});

	template <class T>
	class TObjectMapping
	{
	public:
		using FFactory = std::function<std::shared_ptr<T>()>;
		explicit TObjectMapping(std::string Name, FFactory Factory = {}, std::shared_ptr<const void> Owner = {});
		TObjectMapping(const TObjectMapping&) = default;
		TObjectMapping(TObjectMapping&&) noexcept = default;
		TObjectMapping& operator=(TObjectMapping Other) noexcept
		{
			Name.swap(Other.Name);
			Owner.swap(Other.Owner);
			Factory.swap(Other.Factory);
			Fields.swap(Other.Fields);
			return *this;
		}
		template <class M, class B>
		TObjectMapping& Field(std::string Name, M B::* Member, FFieldOptions Options = {});
		template <class M>
		TObjectMapping& Property(std::string Name, std::function<M(const T&)> Getter, std::function<void(T&, M)> Setter,
			FFieldOptions Options = {});
		bool Register(FMappingRegistry&, FError&, bool bReplace = false) const;

	private:
		struct FField
		{
			std::string Name;
			FFieldOptions Options;
			std::function<bool(const T&, FMappingContext&, FJsonValue&)> Encode;
			std::function<bool(const FJsonValue&, FMappingContext&, std::function<void(T&)>&)> Prepare;
			std::function<bool(FMappingContext&, EMappingDirection, FJsonValue&)> Schema;
		};
		std::string Name;
		std::shared_ptr<const void> Owner;
		FFactory Factory;
		std::vector<FField> Fields;
	};

	template <class T>
	bool RegisterEnum(FMappingRegistry&, std::string Name, std::vector<std::pair<std::string, T>> Values, FError&,
		std::shared_ptr<const void> Owner = {});

	// A closed discriminator table. Wire format: {"kind":"tag","value":<derived payload>}.
	// Use shared_ptr<Base> for nullable values. Derived payloads cannot be JSON null.
	template <class Base>
	class TPolymorphicMapping
	{
		static_assert(std::is_polymorphic_v<Base>, "A polymorphic JSON base requires native RTTI");

	public:
		explicit TPolymorphicMapping(std::string Name, std::string Discriminator = "kind",
			std::string Payload = "value", std::shared_ptr<const void> Owner = {});
		TPolymorphicMapping(const TPolymorphicMapping&) = default;
		TPolymorphicMapping(TPolymorphicMapping&&) noexcept = default;
		TPolymorphicMapping& operator=(TPolymorphicMapping Other) noexcept
		{
			Name.swap(Other.Name);
			Discriminator.swap(Other.Discriminator);
			Payload.swap(Other.Payload);
			Owner.swap(Other.Owner);
			Cases.swap(Other.Cases);
			return *this;
		}
		// A foreign provider supplies an Owner for its RTTI and generated dispatch code.
		template <class Derived>
		TPolymorphicMapping& Add(std::string Tag, std::shared_ptr<const void> Owner = {});
		bool Register(FMappingRegistry&, FError&, bool bReplace = false) const;

	private:
		struct FCase
		{
			std::shared_ptr<const void> Owner;
			std::type_index Type{typeid(void)};
			std::string Tag;
			std::function<bool(const Base&, FMappingContext&, FJsonValue&)> Encode;
			std::function<bool(const FJsonValue&, FMappingContext&, std::shared_ptr<Base>&)> Decode;
			std::function<bool(FMappingContext&, EMappingDirection, FJsonValue&)> Schema;
		};
		std::string Name, Discriminator, Payload;
		std::shared_ptr<const void> Owner;
		std::vector<FCase> Cases;
	};
} // namespace VeroScript::Json

#include "VeroScriptJsonMapping.inl"
#include "VeroScriptJsonMappingPolymorphic.inl"

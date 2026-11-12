// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once
#include <algorithm>
#include <exception>
#include <stdexcept>

namespace VeroScript::Json::MappingDetail
{
	void Exception(FError&, EError, const char*) noexcept;
	template <class F>
	bool Guard(FError& Error, F&& Fn)
	{
		try
		{
			return Fn();
		}
		catch (const std::bad_alloc&)
		{
			Exception(Error, EError::Allocation, "Mapping allocation failed");
		}
		catch (const std::length_error&)
		{
			Exception(Error, EError::Limit, "Mapping capacity exceeded");
		}
		catch (const std::exception&)
		{
			Exception(Error, EError::Callback, "Mapping constructor, accessor or converter threw");
		}
		catch (...)
		{
			Exception(Error, EError::Callback, "Mapping callback threw");
		}
		return false;
	}
	template <class T>
	struct TSequence
	{
		static constexpr bool Value = false;
	};
	template <class T, class A>
	struct TSequence<std::vector<T, A>>
	{
		static constexpr bool Value = true;
		using Element = T;
	};
	template <class T, class A>
	struct TSequence<std::list<T, A>>
	{
		static constexpr bool Value = true;
		using Element = T;
	};
	template <class T, class A>
	struct TSequence<std::deque<T, A>>
	{
		static constexpr bool Value = true;
		using Element = T;
	};
	template <class T>
	struct TArray
	{
		static constexpr bool Value = false;
	};
	template <class T, std::size_t N>
	struct TArray<std::array<T, N>>
	{
		static constexpr bool Value = true;
		using Element = T;
		static constexpr std::size_t Size = N;
	};
	template <class T>
	struct TSet
	{
		static constexpr bool Value = false;
	};
	template <class T, class C, class A>
	struct TSet<std::set<T, C, A>>
	{
		static constexpr bool Value = true, Unique = true;
		using Element = T;
	};
	template <class T, class C, class A>
	struct TSet<std::multiset<T, C, A>>
	{
		static constexpr bool Value = true, Unique = false;
		using Element = T;
	};
	template <class T, class H, class E, class A>
	struct TSet<std::unordered_set<T, H, E, A>>
	{
		static constexpr bool Value = true, Unique = true;
		using Element = T;
	};
	template <class T, class H, class E, class A>
	struct TSet<std::unordered_multiset<T, H, E, A>>
	{
		static constexpr bool Value = true, Unique = false;
		using Element = T;
	};
	template <class T>
	struct TMap
	{
		static constexpr bool Value = false;
	};
	template <class K, class V, class C, class A>
	struct TMap<std::map<K, V, C, A>>
	{
		static constexpr bool Value = true, Unique = true;
		using Key = K;
		using Mapped = V;
	};
	template <class K, class V, class C, class A>
	struct TMap<std::multimap<K, V, C, A>>
	{
		static constexpr bool Value = true, Unique = false;
		using Key = K;
		using Mapped = V;
	};
	template <class K, class V, class H, class E, class A>
	struct TMap<std::unordered_map<K, V, H, E, A>>
	{
		static constexpr bool Value = true, Unique = true;
		using Key = K;
		using Mapped = V;
	};
	template <class K, class V, class H, class E, class A>
	struct TMap<std::unordered_multimap<K, V, H, E, A>>
	{
		static constexpr bool Value = true, Unique = false;
		using Key = K;
		using Mapped = V;
	};
	template <class T>
	struct TNullable
	{
		static constexpr bool Value = false;
	};
	template <class T>
	struct TNullable<std::optional<T>>
	{
		static constexpr bool Value = true, Shared = false;
		using Element = T;
	};
	template <class T>
	struct TNullable<std::shared_ptr<T>>
	{
		static constexpr bool Value = true, Shared = true;
		using Element = T;
	};
	template <class T>
	struct TNullable<std::unique_ptr<T>>
	{
		static constexpr bool Value = true, Shared = false;
		using Element = T;
	};

	inline bool Object(
		std::initializer_list<std::pair<std::string, FJsonValue>> Fields, FMappingContext& C, FJsonValue& Out)
	{
		FBuilder B(EKind::Object);
		for (const auto& P : Fields)
		{
			if (!B.TrySet(P.first, P.second, C.Error()))
			{
				return false;
			}
		}
		return B.TryFinish(Out, C.Error());
	}
	inline FJsonValue Text(const char* S)
	{
		FJsonValue V;
		FError E;
		if (!FJsonValue::TryString(S, V, E))
		{
			throw std::bad_alloc();
		}
		return V;
	}
	inline bool Type(const char* Name, FMappingContext& C, FJsonValue& Out)
	{
		return Object({{"type", Text(Name)}}, C, Out);
	}
	inline bool Size(const FJsonValue& V, EKind Kind, std::int64_t& N, FMappingContext& C)
	{
		return V.Kind() == Kind ? V.TrySize(N, C.Error()) : C.Fail(EError::Type, "Unexpected JSON container kind");
	}

	template <class T>
	bool Encode(FMappingContext& C, const T& V, FJsonValue& Out)
	{
		if constexpr (std::is_same_v<T, FJsonValue>)
		{
			if (!C.ValidateJson(V))
			{
				return false;
			}
			Out = V;
			return true;
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			Out = FJsonValue(V);
			return true;
		}
		else if constexpr (std::is_integral_v<T>)
		{
			static_assert(sizeof(T) <= 8, "An explicit converter is required for wider integers");
			if constexpr (std::is_signed_v<T>)
			{
				Out = FJsonValue(static_cast<std::int64_t>(V));
			}
			else
			{
				Out = FJsonValue(static_cast<std::uint64_t>(V));
			}
			return true;
		}
		else if constexpr (std::is_floating_point_v<T>)
		{
			if (!std::isfinite(V) || (C.Options().bExactFloatingPoint && static_cast<T>(static_cast<double>(V)) != V))
			{
				return C.Fail(EError::Range, "Floating point value cannot be represented");
			}
			return FJsonValue::TryDouble(static_cast<double>(V), Out, C.Error());
		}
		else if constexpr (std::is_same_v<T, std::string>)
		{
			return V.size() <= C.Options().Limits.TokenBytes && C.Check(V.size())
					   ? FJsonValue::TryString(V, Out, C.Error())
					   : C.Fail(EError::Limit, "String limit exceeded");
		}
		else if constexpr (TNullable<T>::Value)
		{
			if (!V)
			{
				Out = {};
				return true;
			}
			return C.Encode(*V, Out);
		}
		else if constexpr (TSequence<T>::Value || TArray<T>::Value || TSet<T>::Value)
		{
			FBuilder B;
			std::size_t I = 0;
			for (const auto& Item : V)
			{
				FMappingContext::FPath Path(C, std::to_string(I++));
				FJsonValue J;
				if constexpr (std::is_same_v<typename T::value_type, bool>)
				{
					if (!C.Encode(static_cast<bool>(Item), J))
					{
						return false;
					}
				}
				else if (!C.Encode(Item, J))
				{
					return false;
				}
				if (!B.TryPush(J, C.Error()))
				{
					return false;
				}
			}
			return B.TryFinish(Out, C.Error());
		}
		else if constexpr (TMap<T>::Value)
		{
			constexpr bool String = std::is_same_v<typename TMap<T>::Key, std::string>;
			const bool Pairs = C.Options().bMapAsPairs;
			if ((!String || !TMap<T>::Unique) && !Pairs)
			{
				return C.Fail(EError::Unsupported, "This map requires the key/value pair representation");
			}
			FBuilder B(Pairs ? EKind::Array : EKind::Object);
			std::size_t I = 0;
			for (const auto& Item : V)
			{
				FJsonValue J;
				if (Pairs)
				{
					FMappingContext::FPath Path(C, std::to_string(I++));
					FJsonValue K, M;
					{
						FMappingContext::FPath P(C, "key");
						if (!C.Encode(Item.first, K))
						{
							return false;
						}
					}
					{
						FMappingContext::FPath P(C, "value");
						if (!C.Encode(Item.second, M))
						{
							return false;
						}
					}
					if (!Object({{"key", K}, {"value", M}}, C, J) || !B.TryPush(J, C.Error()))
					{
						return false;
					}
				}
				else if constexpr (String)
				{
					FMappingContext::FPath Path(C, Item.first);
					if (!C.Encode(Item.second, J) || !B.TrySet(Item.first, J, C.Error()))
					{
						return false;
					}
				}
			}
			return B.TryFinish(Out, C.Error());
		}
		else
		{
			return C.Fail(EError::Unsupported, "Type has no registered JSON converter");
		}
	}

	template <class T>
	bool Decode(FMappingContext& C, const FJsonValue& V, std::shared_ptr<T>& Out)
	{
		if constexpr (std::is_same_v<T, FJsonValue>)
		{
			if (!C.ValidateJson(V))
			{
				return false;
			}
			Out = std::make_shared<T>(V);
			return true;
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			bool X;
			if (!V.TryBoolean(X, C.Error()))
			{
				return false;
			}
			Out = std::make_shared<T>(X);
			return true;
		}
		else if constexpr (std::is_integral_v<T>)
		{
			static_assert(sizeof(T) <= 8, "An explicit converter is required for wider integers");
			if constexpr (std::is_signed_v<T>)
			{
				std::int64_t X;
				if (!V.TryInt64(X, C.Error()))
				{
					return false;
				}
				if (X < std::numeric_limits<T>::min() || X > std::numeric_limits<T>::max())
				{
					return C.Fail(EError::Range, "Integer target range exceeded");
				}
				Out = std::make_shared<T>(static_cast<T>(X));
			}
			else
			{
				std::uint64_t X;
				if (!V.TryUInt64(X, C.Error()))
				{
					return false;
				}
				if (X > std::numeric_limits<T>::max())
				{
					return C.Fail(EError::Range, "Unsigned target range exceeded");
				}
				Out = std::make_shared<T>(static_cast<T>(X));
			}
			return true;
		}
		else if constexpr (std::is_floating_point_v<T>)
		{
			double X;
			if (!V.TryDouble(X, C.Error(), C.Options().bExactFloatingPoint))
			{
				return false;
			}
			if (X > std::numeric_limits<T>::max() || X < std::numeric_limits<T>::lowest())
			{
				return C.Fail(EError::Range, "Floating point target range exceeded");
			}
			const auto Y = static_cast<T>(X);
			if (!std::isfinite(Y) || (X != 0 && Y == 0) ||
				(C.Options().bExactFloatingPoint && static_cast<double>(Y) != X))
			{
				return C.Fail(EError::Range, "Inexact floating point conversion");
			}
			Out = std::make_shared<T>(Y);
			return true;
		}
		else if constexpr (std::is_same_v<T, std::string>)
		{
			std::string X;
			if (!V.TryString(X, C.Error()))
			{
				return false;
			}
			if (X.size() > C.Options().Limits.TokenBytes || !C.Check(X.size()))
			{
				return C.Fail(EError::Limit, "String limit exceeded");
			}
			Out = std::make_shared<T>(std::move(X));
			return true;
		}
		else if constexpr (TNullable<T>::Value)
		{
			if (V.Kind() == EKind::Null)
			{
				Out = std::make_shared<T>();
				return true;
			}
			using E = typename TNullable<T>::Element;
			std::shared_ptr<E> X;
			if (!C.Decode(V, X))
			{
				return false;
			}
			if constexpr (TNullable<T>::Shared)
			{
				Out = std::make_shared<T>(std::move(X));
			}
			else if constexpr (std::is_same_v<T, std::unique_ptr<E>>)
			{
				Out = std::make_shared<T>(std::make_unique<E>(std::move(*X)));
			}
			else
			{
				Out = std::make_shared<T>(std::move(*X));
			}
			return true;
		}
		else if constexpr (TSequence<T>::Value || TArray<T>::Value || TSet<T>::Value)
		{
			std::int64_t N;
			if (!Size(V, EKind::Array, N, C))
			{
				return false;
			}
			if constexpr (TArray<T>::Value)
			{
				if (static_cast<std::uint64_t>(N) != TArray<T>::Size)
				{
					return C.Fail(EError::Range, "Fixed array length mismatch");
				}
			}
			auto X = std::make_shared<T>();
			for (std::int64_t I = 0; I < N; ++I)
			{
				FMappingContext::FPath Path(C, std::to_string(I));
				FJsonValue J;
				std::shared_ptr<typename T::value_type> E;
				if (!V.TryAt(I, J, C.Error()) || !C.Decode(J, E))
				{
					return false;
				}
				if constexpr (TArray<T>::Value)
				{
					(*X)[static_cast<std::size_t>(I)] = std::move(*E);
				}
				else if constexpr (TSet<T>::Value)
				{
					if constexpr (TSet<T>::Unique)
					{
						if (X->find(*E) != X->end())
						{
							return C.Fail(EError::DuplicateKey, "Duplicate set element");
						}
					}
					X->insert(std::move(*E));
				}
				else
				{
					X->emplace_back(std::move(*E));
				}
			}
			Out = std::move(X);
			return true;
		}
		else if constexpr (TMap<T>::Value)
		{
			constexpr bool String = std::is_same_v<typename TMap<T>::Key, std::string>;
			const bool Pairs = C.Options().bMapAsPairs;
			if ((!String || !TMap<T>::Unique) && !Pairs)
			{
				return C.Fail(EError::Unsupported, "This map requires the key/value pair representation");
			}
			std::int64_t N;
			if (!Size(V, Pairs ? EKind::Array : EKind::Object, N, C))
			{
				return false;
			}
			auto X = std::make_shared<T>();
			std::vector<std::string> Keys;
			if (!Pairs && !V.TryKeys(Keys, C.Error()))
			{
				return false;
			}
			for (std::int64_t I = 0; I < N; ++I)
			{
				FMappingContext::FPath Path(C, Pairs ? std::to_string(I) : Keys[static_cast<std::size_t>(I)]);
				std::shared_ptr<typename TMap<T>::Key> Key;
				std::shared_ptr<typename TMap<T>::Mapped> Value;
				FJsonValue J;
				if (Pairs)
				{
					FJsonValue K, M;
					std::int64_t Count;
					if (!V.TryAt(I, J, C.Error()) || !Size(J, EKind::Object, Count, C))
					{
						return false;
					}
					if (Count != 2)
					{
						return C.Fail(EError::Type, "Map entry must contain exactly key and value");
					}
					{
						FMappingContext::FPath P(C, "key");
						if (!J.TryGet("key", K, C.Error()) || !C.Decode(K, Key))
						{
							return C.Locate(false);
						}
					}
					{
						FMappingContext::FPath P(C, "value");
						if (!J.TryGet("value", M, C.Error()) || !C.Decode(M, Value))
						{
							return C.Locate(false);
						}
					}
				}
				else if constexpr (String)
				{
					Key = std::make_shared<std::string>(Keys[static_cast<std::size_t>(I)]);
					if (!V.TryGet(*Key, J, C.Error()) || !C.Decode(J, Value))
					{
						return false;
					}
				}
				if constexpr (TMap<T>::Unique)
				{
					if (X->find(*Key) != X->end())
					{
						return C.Fail(EError::DuplicateKey, "Duplicate map key");
					}
				}
				X->emplace(std::move(*Key), std::move(*Value));
			}
			Out = std::move(X);
			return true;
		}
		else
		{
			return C.Fail(EError::Unsupported, "Type has no registered JSON converter");
		}
	}

	template <class T>
	bool Schema(FMappingContext& C, EMappingDirection Direction, FJsonValue& Out)
	{
		if constexpr (std::is_same_v<T, FJsonValue>)
		{
			Out = FJsonValue(true);
			return true;
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			return Type("boolean", C, Out);
		}
		else if constexpr (std::is_integral_v<T>)
		{
			FJsonValue Low, High;
			if constexpr (std::is_signed_v<T>)
			{
				Low = FJsonValue(static_cast<std::int64_t>(std::numeric_limits<T>::min()));
				High = FJsonValue(static_cast<std::int64_t>(std::numeric_limits<T>::max()));
			}
			else
			{
				Low = FJsonValue(std::uint64_t{0});
				High = FJsonValue(static_cast<std::uint64_t>(std::numeric_limits<T>::max()));
			}
			return Object({{"type", Text("integer")}, {"minimum", Low}, {"maximum", High}}, C, Out);
		}
		else if constexpr (std::is_floating_point_v<T>)
		{
			return Type("number", C, Out);
		}
		else if constexpr (std::is_same_v<T, std::string>)
		{
			return Type("string", C, Out);
		}
		else if constexpr (TNullable<T>::Value)
		{
			FJsonValue E, N, Any;
			FBuilder B;
			if (!C.template Schema<typename TNullable<T>::Element>(Direction, E) || !Type("null", C, N) ||
				!B.TryPush(E, C.Error()) || !B.TryPush(N, C.Error()) || !B.TryFinish(Any, C.Error()))
			{
				return false;
			}
			return Object({{"anyOf", Any}}, C, Out);
		}
		else if constexpr (TSequence<T>::Value || TArray<T>::Value || TSet<T>::Value)
		{
			FJsonValue E;
			if (!C.template Schema<typename T::value_type>(Direction, E))
			{
				return false;
			}
			if (!Object({{"type", Text("array")}, {"items", E}}, C, Out))
			{
				return false;
			}
			if constexpr (TArray<T>::Value)
			{
				return Out.TrySet("minItems", FJsonValue(static_cast<std::uint64_t>(TArray<T>::Size)), C.Error()) &&
					   Out.TrySet("maxItems", FJsonValue(static_cast<std::uint64_t>(TArray<T>::Size)), C.Error());
			}
			if constexpr (TSet<T>::Value)
			{
				if constexpr (TSet<T>::Unique)
				{
					return Out.TrySet("uniqueItems", FJsonValue(true), C.Error());
				}
			}
			return true;
		}
		else if constexpr (TMap<T>::Value)
		{
			constexpr bool String = std::is_same_v<typename TMap<T>::Key, std::string>;
			if (C.Options().bMapAsPairs)
			{
				FJsonValue K, V, P, E, R;
				FBuilder Required;
				if (!C.template Schema<typename TMap<T>::Key>(Direction, K) ||
					!C.template Schema<typename TMap<T>::Mapped>(Direction, V) ||
					!Object({{"key", K}, {"value", V}}, C, P) || !Required.TryPush(Text("key"), C.Error()) ||
					!Required.TryPush(Text("value"), C.Error()) || !Required.TryFinish(R, C.Error()))
				{
					return false;
				}
				if (!Object({{"type", Text("object")},
								{"properties", P},
								{"required", R},
								{"additionalProperties", FJsonValue(false)}},
						C,
						E))
				{
					return false;
				}
				return Object({{"type", Text("array")}, {"items", E}}, C, Out);
			}
			if constexpr (String && TMap<T>::Unique)
			{
				FJsonValue V;
				return C.template Schema<typename TMap<T>::Mapped>(Direction, V) &&
					   Object({{"type", Text("object")}, {"additionalProperties", V}}, C, Out);
			}
			return C.Fail(EError::Unsupported, "This map requires the key/value pair representation");
		}
		else
		{
			return C.Fail(EError::Unsupported, "Type has no Schema converter");
		}
	}
} // namespace VeroScript::Json::MappingDetail

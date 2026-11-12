// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

namespace VeroScript::Json
{
	template <class T>
	TObjectMapping<T>::TObjectMapping(std::string N, FFactory F, std::shared_ptr<const void> O)
		: Name(std::move(N)), Owner(std::move(O)), Factory(std::move(F))
	{
		if constexpr (std::is_default_constructible_v<T>)
		{
			if (!Factory)
			{
				Factory = []
				{
					return std::make_shared<T>();
				};
			}
		}
	}
	template <class T>
	template <class M, class B>
	TObjectMapping<T>& TObjectMapping<T>::Field(std::string N, M B::* Member, FFieldOptions O)
	{
		static_assert(std::is_base_of_v<B, T> || std::is_same_v<B, T>, "Field owner must be this type or its base");
		using U = std::remove_const_t<M>;
		FField F;
		F.Name = std::move(N);
		F.Options = O;
		F.Encode = [Member](const T& V, FMappingContext& C, FJsonValue& Out)
		{
			return C.Encode(V.*Member, Out);
		};
		if constexpr (std::is_assignable_v<decltype(std::declval<T&>().*Member), U&&>)
		{
			F.Prepare = [Member](const FJsonValue& V, FMappingContext& C, std::function<void(T&)>& Apply)
			{
				std::shared_ptr<U> Decoded;
				if (!C.Decode(V, Decoded))
				{
					return false;
				}
				Apply = [Member, Decoded = std::move(Decoded)](T& Target)
				{
					Target.*Member = std::move(*Decoded);
				};
				return true;
			};
		}
		F.Schema = [](FMappingContext& C, EMappingDirection D, FJsonValue& Out)
		{
			return C.template Schema<U>(D, Out);
		};
		Fields.push_back(std::move(F));
		return *this;
	}
	template <class T>
	template <class M>
	TObjectMapping<T>& TObjectMapping<T>::Property(
		std::string N, std::function<M(const T&)> Getter, std::function<void(T&, M)> Setter, FFieldOptions O)
	{
		FField F;
		F.Name = std::move(N);
		F.Options = O;
		if (Getter)
		{
			F.Encode = [Getter = std::move(Getter)](const T& V, FMappingContext& C, FJsonValue& Out)
			{
				return C.Encode(Getter(V), Out);
			};
		}
		if (Setter)
		{
			F.Prepare = [Setter = std::move(Setter)](
							const FJsonValue& V, FMappingContext& C, std::function<void(T&)>& Apply)
			{
				std::shared_ptr<M> Decoded;
				if (!C.Decode(V, Decoded))
				{
					return false;
				}
				Apply = [Setter, Decoded = std::move(Decoded)](T& Target)
				{
					Setter(Target, std::move(*Decoded));
				};
				return true;
			};
		}
		F.Schema = [](FMappingContext& C, EMappingDirection D, FJsonValue& Out)
		{
			return C.template Schema<M>(D, Out);
		};
		Fields.push_back(std::move(F));
		return *this;
	}
	template <class T>
	bool TObjectMapping<T>::Register(FMappingRegistry& Registry, FError& Error, bool Replace) const
	{
		Error = {};
		return MappingDetail::Guard(Error,
			[&]
			{
				auto Plan = std::make_shared<const std::vector<FField>>(Fields);
				auto Index = std::make_shared<std::unordered_map<std::string, std::size_t>>();
				for (std::size_t I = 0; I < Plan->size(); ++I)
				{
					const auto& F = (*Plan)[I];
					FJsonValue Check;
					if (!FJsonValue::TryString(F.Name, Check, Error))
					{
						return false;
					}
					if (!Index->emplace(F.Name, I).second)
					{
						MappingDetail::Exception(Error, EError::DuplicateKey, "Duplicate mapped field name");
						return false;
					}
					if ((F.Options.bRequired && (F.Options.bIgnore || !F.Options.bDeserialize)) ||
						(!F.Options.bIgnore &&
							((F.Options.bSerialize && !F.Encode) || (F.Options.bDeserialize && !F.Prepare))))
					{
						MappingDetail::Exception(
							Error, EError::Type, "Inconsistent JSON field direction or accessibility");
						return false;
					}
				}
				FTypeAdapter A;
				A.Name = Name;
				A.Owner = Owner;
				A.Encode = [Plan](const void* Pointer, FMappingContext& C, FJsonValue& Out)
				{
					FBuilder Builder(EKind::Object);
					const auto& V = *static_cast<const T*>(Pointer);
					for (const auto& F : *Plan)
					{
						if (F.Options.bIgnore || !F.Options.bSerialize)
						{
							continue;
						}
						FMappingContext::FPath Path(C, F.Name);
						FJsonValue J;
						if (!C.Locate(MappingDetail::Guard(C.Error(),
								[&]
								{
									return F.Encode(V, C, J);
								})) ||
							!Builder.TrySet(F.Name, J, C.Error()))
						{
							return false;
						}
					}
					return Builder.TryFinish(Out, C.Error());
				};
				if (Factory)
				{
					A.Decode = [Plan, Index, Factory = Factory](
								   const FJsonValue& V, FMappingContext& C, std::shared_ptr<void>& Out)
					{
						std::int64_t Size;
						if (!MappingDetail::Size(V, EKind::Object, Size, C))
						{
							return false;
						}
						std::vector<std::string> Keys;
						if (!V.TryKeys(Keys, C.Error()))
						{
							return false;
						}
						std::vector<bool> Seen(Plan->size(), false);
						std::vector<std::pair<std::size_t, std::function<void(T&)>>> Prepared;
						for (const auto& Key : Keys)
						{
							FMappingContext::FPath Path(C, Key);
							if (!C.Check(Key.size() + 1))
							{
								return false;
							}
							const auto It = Index->find(Key);
							if (It == Index->end())
							{
								if (C.Options().bIgnoreUnknownFields)
								{
									continue;
								}
								return C.Fail(EError::Missing, "Unknown JSON field");
							}
							const auto I = It->second;
							const auto& F = (*Plan)[I];
							Seen[I] = true;
							if (F.Options.bIgnore)
							{
								continue;
							}
							if (!F.Options.bDeserialize)
							{
								return C.Fail(EError::Type, "Output-only field cannot be deserialized");
							}
							FJsonValue J;
							std::function<void(T&)> Apply;
							if (!V.TryGet(Key, J, C.Error()) || !C.Locate(MappingDetail::Guard(C.Error(),
																	[&]
																	{
																		return F.Prepare(J, C, Apply);
																	})))
							{
								return false;
							}
							Prepared.emplace_back(I, std::move(Apply));
						}
						for (std::size_t I = 0; I < Plan->size(); ++I)
						{
							if ((*Plan)[I].Options.bRequired && !Seen[I])
							{
								FMappingContext::FPath Path(C, (*Plan)[I].Name);
								return C.Fail(EError::Missing, "Required JSON field missing");
							}
						}
						auto Value = Factory();
						if (!Value)
						{
							return C.Fail(EError::Callback, "Object factory returned no value");
						}
						for (auto& P : Prepared)
						{
							FMappingContext::FPath Path(C, (*Plan)[P.first].Name);
							if (!C.Locate(MappingDetail::Guard(C.Error(),
									[&]
									{
										P.second(*Value);
										return true;
									})))
							{
								return false;
							}
						}
						Out = std::move(Value);
						return true;
					};
				}
				A.Schema = [Plan](FMappingContext& C, EMappingDirection D, FJsonValue& Out)
				{
					FBuilder Properties(EKind::Object), Required;
					FJsonValue P, R;
					for (const auto& F : *Plan)
					{
						FJsonValue J;
						if (D == EMappingDirection::Output && (F.Options.bIgnore || !F.Options.bSerialize))
						{
							continue;
						}
						if (F.Options.bIgnore)
						{
							J = FJsonValue(true);
						}
						else if (D == EMappingDirection::Input && !F.Options.bDeserialize)
						{
							J = FJsonValue(false);
						}
						else if (!F.Schema(C, D, J))
						{
							return false;
						}
						if (!Properties.TrySet(F.Name, J, C.Error()))
						{
							return false;
						}
						if ((D == EMappingDirection::Output || F.Options.bRequired) && !F.Options.bIgnore)
						{
							FJsonValue N;
							if (!FJsonValue::TryString(F.Name, N, C.Error()) || !Required.TryPush(N, C.Error()))
							{
								return false;
							}
						}
					}
					if (!Properties.TryFinish(P, C.Error()) || !Required.TryFinish(R, C.Error()))
					{
						return false;
					}
					return MappingDetail::Object(
						{{"type", MappingDetail::Text("object")},
							{"properties", P},
							{"required", R},
							{"additionalProperties",
								FJsonValue(D == EMappingDirection::Input && C.Options().bIgnoreUnknownFields)}},
						C,
						Out);
				};
				return Registry.Register(typeid(T), std::move(A), Error, Replace);
			});
	}

	template <class T>
	bool RegisterEnum(FMappingRegistry& Registry, std::string Name, std::vector<std::pair<std::string, T>> Values,
		FError& Error, std::shared_ptr<const void> Owner)
	{
		static_assert(std::is_enum_v<T>, "RegisterEnum requires an enumeration type");
		Error = {};
		return MappingDetail::Guard(Error,
			[&]
			{
				std::unordered_set<std::string> Names;
				if (Values.empty())
				{
					MappingDetail::Exception(
						Error, EError::Type, "A JSON enumeration requires at least one named value");
					return false;
				}
				for (const auto& E : Values)
				{
					FJsonValue S;
					if (!FJsonValue::TryString(E.first, S, Error))
					{
						return false;
					}
					if (!Names.insert(E.first).second)
					{
						MappingDetail::Exception(Error, EError::DuplicateKey, "Duplicate enum name");
						return false;
					}
				}
				auto Entries = std::make_shared<const std::vector<std::pair<std::string, T>>>(std::move(Values));
				FTypeAdapter A;
				A.Name = std::move(Name);
				A.Owner = std::move(Owner);
				A.Encode = [Entries](const void* V, FMappingContext& C, FJsonValue& Out)
				{
					if (!C.Check(Entries->size()))
					{
						return false;
					}
					for (const auto& E : *Entries)
					{
						if (E.second == *static_cast<const T*>(V))
						{
							return C.Options().bEnumsAsNumbers
									   ? MappingDetail::Encode(C, static_cast<std::underlying_type_t<T>>(E.second), Out)
									   : FJsonValue::TryString(E.first, Out, C.Error());
						}
					}
					return C.Fail(EError::Range, "Enum value has no declared JSON name");
				};
				A.Decode = [Entries](const FJsonValue& V, FMappingContext& C, std::shared_ptr<void>& Out)
				{
					if (!C.Check(Entries->size()))
					{
						return false;
					}
					if (C.Options().bEnumsAsNumbers)
					{
						std::shared_ptr<std::underlying_type_t<T>> X;
						if (!MappingDetail::Decode(C, V, X))
						{
							return false;
						}
						for (const auto& E : *Entries)
						{
							if (static_cast<std::underlying_type_t<T>>(E.second) == *X)
							{
								Out = std::make_shared<T>(E.second);
								return true;
							}
						}
					}
					else
					{
						std::string X;
						if (!V.TryString(X, C.Error()))
						{
							return false;
						}
						for (const auto& E : *Entries)
						{
							if (E.first == X)
							{
								Out = std::make_shared<T>(E.second);
								return true;
							}
						}
					}
					return C.Fail(EError::Range, "Unknown JSON enumeration value");
				};
				A.Schema = [Entries](FMappingContext& C, EMappingDirection, FJsonValue& Out)
				{
					FBuilder B;
					FJsonValue J;
					std::unordered_set<std::underlying_type_t<T>> Numbers;
					for (const auto& E : *Entries)
					{
						if (C.Options().bEnumsAsNumbers)
						{
							if (!Numbers.insert(static_cast<std::underlying_type_t<T>>(E.second)).second)
							{
								continue;
							}
							if (!MappingDetail::Encode(C, static_cast<std::underlying_type_t<T>>(E.second), J))
							{
								return false;
							}
						}
						else if (!FJsonValue::TryString(E.first, J, C.Error()))
						{
							return false;
						}
						if (!B.TryPush(J, C.Error()))
						{
							return false;
						}
					}
					return B.TryFinish(J, C.Error()) && MappingDetail::Object({{"enum", J}}, C, Out);
				};
				return Registry.Register(typeid(T), std::move(A), Error);
			});
	}

} // namespace VeroScript::Json

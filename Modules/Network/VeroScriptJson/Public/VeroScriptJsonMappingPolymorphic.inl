// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

namespace VeroScript::Json
{
	template <class Base>
	TPolymorphicMapping<Base>::TPolymorphicMapping(
		std::string N, std::string D, std::string P, std::shared_ptr<const void> O)
		: Name(std::move(N)), Discriminator(std::move(D)), Payload(std::move(P)), Owner(std::move(O))
	{
	}
	template <class Base>
	template <class Derived>
	TPolymorphicMapping<Base>& TPolymorphicMapping<Base>::Add(std::string Tag, std::shared_ptr<const void> O)
	{
		static_assert(
			std::is_base_of_v<Base, Derived> && !std::is_same_v<Base, Derived>, "A case must be a proper derived type");
		static_assert(std::is_convertible_v<Derived*, Base*>, "A case requires an accessible unambiguous base");
		static_assert(!std::is_abstract_v<Derived>, "A polymorphic case must be concrete");
		FCase Item;
		Item.Owner = std::move(O);
		Item.Type = typeid(Derived);
		Item.Tag = std::move(Tag);
		Item.Encode = [](const Base& Value, FMappingContext& C, FJsonValue& Out)
		{
			const auto* Typed = dynamic_cast<const Derived*>(&Value);
			return Typed ? C.Encode(*Typed, Out)
						 : C.Fail(EError::Type, "Polymorphic value does not match its declared case");
		};
		Item.Decode = [](const FJsonValue& Value, FMappingContext& C, std::shared_ptr<Base>& Out)
		{
			std::shared_ptr<Derived> Typed;
			if (!C.Decode(Value, Typed))
			{
				return false;
			}
			if (!Typed || typeid(*Typed) != typeid(Derived))
			{
				return C.Fail(EError::Type, "Polymorphic factory returned a different runtime type");
			}
			Out = std::move(Typed);
			return true;
		};
		Item.Schema = [](FMappingContext& C, EMappingDirection D, FJsonValue& Out)
		{
			return C.template Schema<Derived>(D, Out);
		};
		Cases.push_back(std::move(Item));
		return *this;
	}
	template <class Base>
	bool TPolymorphicMapping<Base>::Register(FMappingRegistry& Registry, FError& Error, bool Replace) const
	{
		Error = {};
		return MappingDetail::Guard(Error,
			[&]
			{
				if (Name.empty() || Discriminator == Payload || Cases.empty() || Cases.size() > 256)
				{
					MappingDetail::Exception(
						Error, EError::Type, "A polymorphic table requires a name, distinct keys and 1..256 cases");
					return false;
				}
				FJsonValue Checked;
				if (!FJsonValue::TryString(Discriminator, Checked, Error) ||
					!FJsonValue::TryString(Payload, Checked, Error))
				{
					return false;
				}
				std::set<std::type_index> Types;
				std::set<std::string> Tags;
				for (const auto& Item : Cases)
				{
					if (!FJsonValue::TryString(Item.Tag, Checked, Error))
					{
						return false;
					}
					if (!Types.insert(Item.Type).second || !Tags.insert(Item.Tag).second)
					{
						MappingDetail::Exception(
							Error, EError::DuplicateKey, "Duplicate polymorphic case type or discriminator");
						return false;
					}
				}
				auto Plan = std::make_shared<const std::vector<FCase>>(Cases);
				TMappingConverter<Base> Converter;
				Converter.Name = Name;
				Converter.Owner = Owner;
				Converter.Encode = [Plan, D = Discriminator, P = Payload](
									   const Base& Value, FMappingContext& C, FJsonValue& Out)
				{
					if (!C.Check(Plan->size()))
					{
						return false;
					}
					const auto Actual = std::type_index(typeid(Value));
					const auto Item = std::find_if(Plan->begin(),
						Plan->end(),
						[&](const auto& Case)
						{
							return Case.Type == Actual;
						});
					if (Item == Plan->end())
					{
						return C.Fail(EError::Type, "Unregistered polymorphic runtime type");
					}
					FJsonValue Tag, Body;
					{
						FMappingContext::FPath Path(C, D);
						if (!C.Encode(Item->Tag, Tag))
						{
							return false;
						}
					}
					{
						FMappingContext::FPath Path(C, P);
						if (!Item->Encode(Value, C, Body))
						{
							return false;
						}
						if (Body.Kind() == EKind::Null)
						{
							return C.Fail(EError::Type, "A tagged polymorphic payload cannot be null");
						}
					}
					return MappingDetail::Object({{D, Tag}, {P, Body}}, C, Out);
				};
				Converter.Decode = [Plan, D = Discriminator, P = Payload](
									   const FJsonValue& Value, FMappingContext& C, std::shared_ptr<Base>& Out)
				{
					if (Value.Kind() != EKind::Object)
					{
						return C.Fail(EError::Type, "Expected a polymorphic JSON envelope");
					}
					if (!C.Check(Plan->size()))
					{
						return false;
					}
					std::vector<std::string> Keys;
					if (!Value.TryKeys(Keys, C.Error()))
					{
						return false;
					}
					for (const auto& Key : Keys)
					{
						FMappingContext::FPath Path(C, Key);
						if (!C.Check(Key.size() + 1))
						{
							return false;
						}
						if (Key != D && Key != P && !C.Options().bIgnoreUnknownFields)
						{
							return C.Fail(EError::Type, "Unknown polymorphic envelope field");
						}
					}
					FJsonValue Tag, Body;
					std::string Text;
					const FCase* Item = nullptr;
					{
						FMappingContext::FPath Path(C, D);
						bool Found = false;
						if (!C.Locate(Value.TryContains(D, Found, C.Error())))
						{
							return false;
						}
						if (!Found)
						{
							return C.Fail(EError::Type, "Missing polymorphic discriminator");
						}
						if (!C.Locate(Value.TryGet(D, Tag, C.Error())))
						{
							return false;
						}
						if (!C.Locate(Tag.TryString(Text, C.Error())))
						{
							return false;
						}
						for (const auto& Case : *Plan)
						{
							if (Case.Tag == Text)
							{
								Item = &Case;
								break;
							}
						}
						if (!Item)
						{
							return C.Fail(EError::Type, "Unknown polymorphic discriminator");
						}
					}
					{
						FMappingContext::FPath Path(C, P);
						bool Found = false;
						if (!C.Locate(Value.TryContains(P, Found, C.Error())))
						{
							return false;
						}
						if (Found && !C.Locate(Value.TryGet(P, Body, C.Error())))
						{
							return false;
						}
						if (!Found || Body.Kind() == EKind::Null)
						{
							return C.Fail(EError::Type, "Missing or null polymorphic payload");
						}
						std::shared_ptr<Base> Candidate;
						if (!Item->Decode(Body, C, Candidate))
						{
							return false;
						}
						Out = std::move(Candidate);
					}
					return true;
				};
				Converter.Schema = [Plan, D = Discriminator, P = Payload](
									   FMappingContext& C, EMappingDirection Direction, FJsonValue& Out)
				{
					FBuilder Choices;
					for (const auto& Item : *Plan)
					{
						if (!C.Check())
						{
							return false;
						}
						FJsonValue Tag, TagSchema, BodySchema, NonNull, NullType, PayloadSchema, Properties, Required,
							Branch;
						if (!FJsonValue::TryString(Item.Tag, Tag, C.Error()) ||
							!MappingDetail::Object({{"const", Tag}}, C, TagSchema) ||
							!Item.Schema(C, Direction, BodySchema) || !MappingDetail::Type("null", C, NullType) ||
							!MappingDetail::Object({{"not", NullType}}, C, NonNull))
						{
							return false;
						}
						FBuilder Both;
						FJsonValue Constraints;
						if (!Both.TryPush(BodySchema, C.Error()) || !Both.TryPush(NonNull, C.Error()) ||
							!Both.TryFinish(Constraints, C.Error()) ||
							!MappingDetail::Object({{"allOf", Constraints}}, C, PayloadSchema) ||
							!MappingDetail::Object({{D, TagSchema}, {P, PayloadSchema}}, C, Properties))
						{
							return false;
						}
						FJsonValue DiscriminatorKey, PayloadKey;
						FBuilder Names;
						if (!FJsonValue::TryString(D, DiscriminatorKey, C.Error()) ||
							!FJsonValue::TryString(P, PayloadKey, C.Error()) ||
							!Names.TryPush(DiscriminatorKey, C.Error()) || !Names.TryPush(PayloadKey, C.Error()) ||
							!Names.TryFinish(Required, C.Error()) ||
							!MappingDetail::Object({{"type", MappingDetail::Text("object")},
													   {"properties", Properties},
													   {"required", Required},
													   {"additionalProperties",
														   FJsonValue(Direction == EMappingDirection::Input &&
																	  C.Options().bIgnoreUnknownFields)}},
								C,
								Branch) ||
							!Choices.TryPush(Branch, C.Error()))
						{
							return false;
						}
					}
					FJsonValue Branches;
					return Choices.TryFinish(Branches, C.Error()) &&
						   MappingDetail::Object({{"oneOf", Branches}}, C, Out);
				};
				return RegisterConverter(Registry, std::move(Converter), Error, Replace);
			});
	}
} // namespace VeroScript::Json

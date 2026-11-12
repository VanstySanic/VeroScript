// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJsonMappingContainers.inl"
#include "VeroScriptJsonMappingObjects.inl"

namespace VeroScript::Json
{
	template <class T>
	bool RegisterConverter(FMappingRegistry& Registry, TMappingConverter<T> Converter, FError& Error, bool Replace)
	{
		Error = {};
		return MappingDetail::Guard(Error,
			[&]
			{
				FTypeAdapter A;
				A.Name = std::move(Converter.Name);
				A.Owner = std::move(Converter.Owner);
				A.Schema = std::move(Converter.Schema);
				if (Converter.Encode)
				{
					A.Encode = [F = std::move(Converter.Encode)](const void* Value, FMappingContext& C, FJsonValue& Out)
					{
						return F(*static_cast<const T*>(Value), C, Out);
					};
				}
				if (Converter.Decode)
				{
					A.Decode = [F = std::move(Converter.Decode)](
								   const FJsonValue& Value, FMappingContext& C, std::shared_ptr<void>& Out)
					{
						std::shared_ptr<T> Result;
						if (!F(Value, C, Result))
						{
							return false;
						}
						if (!Result)
						{
							return C.Fail(EError::Callback, "Converter returned no constructed value");
						}
						Out = std::move(Result);
						return true;
					};
				}
				return Registry.Register(typeid(T), std::move(A), Error, Replace);
			});
	}

	template <class T>
	bool FMappingContext::Encode(const T& V, FJsonValue& Out)
	{
		return Locate(MappingDetail::Guard(Error(),
			[&]
			{
				if (!EnterValue())
				{
					return false;
				}
				if (auto A = Find(typeid(T)))
				{
					return A->Encode ? EncodeAdapter(*A, &V, Out)
									 : Fail(EError::Unsupported, "Converter cannot encode this type");
				}
				return MappingDetail::Encode(*this, V, Out);
			}));
	}
	template <class T>
	bool FMappingContext::Decode(const FJsonValue& V, std::shared_ptr<T>& Out)
	{
		return Locate(MappingDetail::Guard(Error(),
			[&]
			{
				if (!EnterValue())
				{
					return false;
				}
				if (auto A = Find(typeid(T)))
				{
					if (!A->Decode)
					{
						return Fail(EError::Unsupported, "Converter cannot decode this type");
					}
					std::shared_ptr<void> Value;
					if (!A->Decode(V, *this, Value))
					{
						return false;
					}
					if (!Value)
					{
						return Fail(EError::Callback, "Converter returned no constructed value");
					}
					// Nested shared results may outlive the aggregate returned by TryDecode.
					Out = std::static_pointer_cast<T>(RetainDecoded(std::move(Value)));
					return true;
				}
				return MappingDetail::Decode(*this, V, Out);
			}));
	}
	template <class T>
	bool FMappingContext::Schema(EMappingDirection D, FJsonValue& Out)
	{
		return Locate(MappingDetail::Guard(Error(),
			[&]
			{
				if (!EnterValue())
				{
					return false;
				}
				if (auto A = Find(typeid(T)))
				{
					return SchemaAdapter(typeid(T), *A, D, Out);
				}
				return MappingDetail::Schema<T>(*this, D, Out);
			}));
	}
	template <class T>
	bool TryEncode(const T& Value, FJsonValue& Out, FError& Error, const FMappingRegistry& Registry,
		const FMappingOptions& Options, FWork Work)
	{
		Error = {};
		return MappingDetail::Guard(Error,
			[&]
			{
				FMappingContext C(Registry, Options, Error, Work);
				FJsonValue V;
				if (!C.Encode(Value, V) || !C.ValidateJson(V))
				{
					return false;
				}
				Out = std::move(V);
				return true;
			});
	}
	template <class T>
	bool TryDecode(const FJsonValue& Value, std::shared_ptr<T>& Out, FError& Error, const FMappingRegistry& Registry,
		const FMappingOptions& Options, FWork Work)
	{
		Error = {};
		return MappingDetail::Guard(Error,
			[&]
			{
				FMappingContext C(Registry, Options, Error, Work);
				std::shared_ptr<T> V;
				if (!C.ValidateJson(Value, true) || !C.Decode(Value, V))
				{
					return false;
				}
				Out = std::static_pointer_cast<T>(C.RetainDecoded(std::static_pointer_cast<void>(std::move(V))));
				return true;
			});
	}
	template <class T>
	bool TryMappingSchema(EMappingDirection D, FJsonValue& Out, FError& Error, const FMappingRegistry& Registry,
		const FMappingOptions& Options, FWork Work)
	{
		Error = {};
		return MappingDetail::Guard(Error,
			[&]
			{
				FMappingContext C(Registry, Options, Error, Work);
				FJsonValue V, R;
				if (!C.template Schema<T>(D, V) || !C.WrapSchema(V, R) || !C.ValidateJson(R))
				{
					return false;
				}
				Out = std::move(R);
				return true;
			});
	}
} // namespace VeroScript::Json

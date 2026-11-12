// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJsonMapping.h"
#include "JsonInternal.h"
#include <mutex>

namespace VeroScript::Json
{
	namespace MappingDetail
	{
		void Exception(FError& Error, EError Code, const char* Message) noexcept
		{
			if (!Error)
			{
				Detail::SetError(Error, Code, Message);
			}
		}
	} // namespace MappingDetail
	struct FRegisteredAdapter
	{
		// type_index borrows its type_info. A replacement must not unload the
		// provider that supplied the key still stored in this registry.
		std::shared_ptr<const void> TypeOwner;
		std::shared_ptr<const FTypeAdapter> Adapter;
	};
	struct FMappingRegistry::FImpl
	{
		std::mutex Mutex;
		std::unordered_map<std::type_index, FRegisteredAdapter> Types;
	};
	FMappingRegistry::FMappingRegistry() : Impl(std::make_shared<FImpl>())
	{
	}
	bool FMappingRegistry::Register(std::type_index Type, FTypeAdapter Adapter, FError& Error, bool Replace)
	{
		Error = {};
		return MappingDetail::Guard(Error,
			[&]
			{
				if (Adapter.Name.empty() || (!Adapter.Encode && !Adapter.Decode))
				{
					Detail::SetError(Error, EError::Type, "A converter requires a name and at least one direction");
					return false;
				}
				auto Value = std::make_shared<const FTypeAdapter>(std::move(Adapter));
				// Foreign callback/Owner destructors may register another converter.
				// Release the old adapter after unlocking the registry.
				std::shared_ptr<const FTypeAdapter> Previous;
				std::lock_guard<std::mutex> Lock(Impl->Mutex);
				auto It = Impl->Types.find(Type);
				if (It != Impl->Types.end())
				{
					if (!Replace)
					{
						Detail::SetError(Error, EError::State, "Type already has a JSON converter");
						return false;
					}
					Previous = std::move(It->second.Adapter);
					It->second.Adapter = std::move(Value);
				}
				else
				{
					if (Impl->Types.size() >= 4096)
					{
						Detail::SetError(Error, EError::Limit, "JSON converter registry limit exceeded");
						return false;
					}
					// Keep Value alive until after Lock even if emplace destroys its argument on failure.
					Impl->Types.emplace(Type, FRegisteredAdapter{Value->Owner, Value});
				}
				return true;
			});
	}
	struct FMappingContext::FImpl
	{
		struct FObjectKey
		{
			const void* Value;
			const FTypeAdapter* Adapter;
			bool operator==(const FObjectKey& Other) const noexcept
			{
				return Value == Other.Value && Adapter == Other.Adapter;
			}
		};
		struct FObjectHash
		{
			std::size_t operator()(const FObjectKey& K) const noexcept
			{
				return std::hash<const void*>{}(K.Value) ^ (std::hash<const void*>{}(K.Adapter) << 1);
			}
		};
		FMappingOptions Options;
		FError& Error;
		FWork Work;
		std::unordered_map<std::type_index, FRegisteredAdapter> Types;
		std::shared_ptr<const std::vector<std::shared_ptr<const FTypeAdapter>>> DecodedAdapters;
		std::unordered_set<FObjectKey, FObjectHash> Objects;
		std::map<std::pair<std::type_index, EMappingDirection>, std::size_t> SchemaIds;
		std::vector<FJsonValue> Definitions;
		std::string Pointer;
		std::uint64_t Used = 0, Nodes = 0, Depth = 0;
		FImpl(const FMappingOptions& O, FError& E, FWork W) : Options(O), Error(E), Work(W)
		{
		}
	};
	FMappingContext::FMappingContext(const FMappingRegistry& R, const FMappingOptions& O, FError& E, FWork W)
		: Impl(std::make_unique<FImpl>(O, E, W))
	{
		std::lock_guard<std::mutex> Lock(R.Impl->Mutex);
		Impl->Types = R.Impl->Types;
	}

	FMappingContext::~FMappingContext() = default;

	const FMappingOptions& FMappingContext::Options() const noexcept
	{
		return Impl->Options;
	}

	FError& FMappingContext::Error() noexcept
	{
		return Impl->Error;
	}

	bool FMappingContext::Fail(EError Code, const char* Message)
	{
		MappingDetail::Exception(Error(), Code, Message);
		return Locate(false);
	}

	bool FMappingContext::Check(std::uint64_t Units)
	{
		if (Error())
		{
			return false;
		}
		if (Impl->Depth > std::min<std::uint64_t>(Options().Limits.Depth, 256) ||
			Units > Options().Limits.WorkUnits - std::min(Impl->Used, Options().Limits.WorkUnits))
		{
			return Fail(EError::Limit, "JSON mapping depth, node or work limit exceeded");
		}
		Impl->Used += Units;
		if (Impl->Work.Check && !Impl->Work.Check(Impl->Work.UserData, Units))
		{
			return Fail(EError::Cancelled, "JSON mapping cancelled");
		}
		return true;
	}

	bool FMappingContext::EnterValue()
	{
		if (++Impl->Nodes > Options().Limits.Nodes)
		{
			return Fail(EError::Limit, "JSON mapping node limit exceeded");
		}
		return Check();
	}

	bool FMappingContext::CheckSegment(std::string_view Text)
	{
		if (Text.size() > Options().Limits.TokenBytes)
		{
			return Fail(EError::Limit, "JSON mapping key limit exceeded");
		}
		if (!Check(Text.size()))
		{
			return false;
		}
		try
		{
			Detail::ValidateUtf8(Text);
		}
		catch (const Detail::FFailure& Failure)
		{
			return Fail(Failure.Code, Failure.Message);
		}
		return true;
	}

	bool FMappingContext::Locate(bool Result)
	{
		if (!Result && !Error())
		{
			Detail::SetError(Error(), EError::Callback, "JSON converter failed without a diagnostic");
		}
		if (!Result && Error().InstancePointer.empty())
		{
			try
			{
				Error().InstancePointer = Impl->Pointer;
			}
			catch (...)
			{
			}
		}
		return Result;
	}

	bool FMappingContext::ValidateJson(const FJsonValue& Value, bool Input)
	{
		struct FVisit
		{
			const Detail::FNode* Node;
			std::size_t Index = 0;
			bool Entered = false;
		};
		std::vector<FVisit> Pending{{&Detail::FAccess::Get(Value)}};
		std::uint64_t Bytes = 0, Nodes = 0;
		const auto ByteLimit = Input ? Options().Limits.InputBytes : Options().Limits.OutputBytes;
		auto Add = [&](std::uint64_t N)
		{
			if (N > ByteLimit - std::min(Bytes, ByteLimit))
			{
				return Fail(EError::Limit, "JSON mapping byte limit exceeded");
			}
			Bytes += N;
			return true;
		};
		auto String = [&](std::string_view Text)
		{
			if (Text.size() > Options().Limits.TokenBytes || !Check(Text.size()))
			{
				return Fail(EError::Limit, "JSON mapping string limit exceeded");
			}
			if (!Add(2))
			{
				return false;
			}
			for (const unsigned char C : Text)
			{
				const auto N = C == '"' || C == '\\' || C == '\b' || C == '\f' || C == '\n' || C == '\r' || C == '\t'
								   ? 2u
							   : C < 0x20 ? 6u
										  : 1u;
				if (!Add(N))
				{
					return false;
				}
			}
			return true;
		};
		while (!Pending.empty())
		{
			auto& Frame = Pending.back();
			const auto& Node = *Frame.Node;
			if (!Frame.Entered)
			{
				if (Pending.size() > std::min<std::uint64_t>(Options().Limits.Depth, 256) ||
					++Nodes > Options().Limits.Nodes || !Check())
				{
					return Fail(EError::Limit, "JSON converter value exceeds limits");
				}
				Frame.Entered = true;
				if (Node.Kind == EKind::String)
				{
					if (!String(Node.Text))
					{
						return false;
					}
				}
				else if (Node.Kind == EKind::Number)
				{
					FJsonValue Checked;
					if (!FJsonValue::TryNumber(Node.Text, Checked, Error(), Options().Limits))
					{
						return Locate(false);
					}
					if (Node.Text.size() > Options().Limits.NumberBytes || !Check(Node.Text.size()) ||
						!Add(Node.Text.size()))
					{
						return Fail(EError::Limit, "JSON mapping number limit exceeded");
					}
				}
				else if (Node.Kind == EKind::Null)
				{
					if (!Add(4))
					{
						return false;
					}
				}
				else if (Node.Kind == EKind::Boolean)
				{
					if (!Add(Node.Boolean ? 4 : 5))
					{
						return false;
					}
				}
				else if (!Add(2))
				{
					return false;
				}
			}
			if (Frame.Index < Node.Array.size())
			{
				if (Frame.Index && !Add(1))
				{
					return false;
				}
				const auto* Next = &Detail::FAccess::Get(Node.Array[Frame.Index++]);
				Pending.push_back({Next});
			}
			else if (Frame.Index < Node.Object.size())
			{
				if ((Frame.Index && !Add(1)) || !Add(1))
				{
					return false;
				}
				const auto& Pair = Node.Object[Frame.Index++];
				if (!String(Pair.first))
				{
					return false;
				}
				Pending.push_back({&Detail::FAccess::Get(Pair.second)});
			}
			else
			{
				Pending.pop_back();
			}
		}
		return true;
	}

	FMappingContext::FPath::FPath(FMappingContext& C, std::string_view Segment)
		: Context(C), Size(C.Impl->Pointer.size())
	{
		++C.Impl->Depth;
		if (!C.CheckSegment(Segment))
		{
			return;
		}
		C.Impl->Pointer.push_back('/');
		for (char Ch : Segment)
		{
			if (Ch == '~')
			{
				C.Impl->Pointer += "~0";
			}
			else if (Ch == '/')
			{
				C.Impl->Pointer += "~1";
			}
			else
			{
				C.Impl->Pointer.push_back(Ch);
			}
		}
	}

	FMappingContext::FPath::~FPath()
	{
		Context.Impl->Pointer.resize(Size);
		--Context.Impl->Depth;
	}

	bool FMappingContext::WrapSchema(const FJsonValue& Root, FJsonValue& Out)
	{
		FBuilder Definitions(EKind::Object);
		for (std::size_t I = 0; I < Impl->Definitions.size(); ++I)
		{
			if (!Definitions.TrySet("t" + std::to_string(I), Impl->Definitions[I], Error()))
			{
				return false;
			}
		}
		FJsonValue Defs, Uri;
		if (!Definitions.TryFinish(Defs, Error()) ||
			!FJsonValue::TryString("https://json-schema.org/draft/2020-12/schema", Uri, Error()))
		{
			return false;
		}
		FJsonValue Result = Root;
		if (Result.Kind() != EKind::Object)
		{
			FBuilder All;
			FJsonValue Array;
			if (!All.TryPush(Root, Error()) || !All.TryFinish(Array, Error()) ||
				!MappingDetail::Object({{"allOf", Array}}, *this, Result))
			{
				return false;
			}
		}
		if (!Result.TrySet("$schema", Uri, Error()) || !Result.TrySet("$defs", Defs, Error()))
		{
			return false;
		}
		Out = std::move(Result);
		return true;
	}

	std::shared_ptr<const FTypeAdapter> FMappingContext::Find(std::type_index Type) const
	{
		const auto I = Impl->Types.find(Type);
		return I == Impl->Types.end() ? nullptr : I->second.Adapter;
	}

	bool FMappingContext::EncodeAdapter(const FTypeAdapter& Adapter, const void* Value, FJsonValue& Out)
	{
		const FImpl::FObjectKey Key{Value, &Adapter};
		if (!Impl->Objects.insert(Key).second)
		{
			return Fail(EError::Cycle, "Object cycle detected during JSON encoding");
		}
		struct FRelease
		{
			decltype(Impl->Objects)& Set;
			FImpl::FObjectKey Key;
			~FRelease()
			{
				Set.erase(Key);
			}
		} Release{Impl->Objects, Key};
		return Adapter.Encode(Value, *this, Out);
	}

	bool FMappingContext::SchemaAdapter(
		std::type_index Type, const FTypeAdapter& Adapter, EMappingDirection D, FJsonValue& Out)
	{
		if ((D == EMappingDirection::Input && !Adapter.Decode) || (D == EMappingDirection::Output && !Adapter.Encode))
		{
			return Fail(EError::Unsupported, "Converter does not support the requested Schema direction");
		}
		if (!Adapter.Schema)
		{
			return Fail(EError::Unsupported, "Converter has no declared Schema");
		}
		const auto Key = std::make_pair(Type, D);
		auto I = Impl->SchemaIds.find(Key);
		std::size_t Index;
		if (I != Impl->SchemaIds.end())
		{
			Index = I->second;
		}
		else
		{
			Index = Impl->Definitions.size();
			Impl->SchemaIds.emplace(Key, Index);
			Impl->Definitions.emplace_back();
			FJsonValue Definition;
			if (!Adapter.Schema(*this, D, Definition))
			{
				return false;
			}
			if (Definition.Kind() != EKind::Object && Definition.Kind() != EKind::Boolean)
			{
				return Fail(EError::Type, "Converter Schema must be an object or boolean");
			}
			Impl->Definitions[Index] = std::move(Definition);
		}
		FJsonValue Reference;
		return FJsonValue::TryString("#/$defs/t" + std::to_string(Index), Reference, Error()) &&
			   MappingDetail::Object({{"$ref", Reference}}, *this, Out);
	}

	std::shared_ptr<void> FMappingContext::RetainDecoded(std::shared_ptr<void> Value)
	{
		struct FOwned
		{
			// Callbacks and their code owners outlive the value's destructor/deleter.
			std::shared_ptr<const std::vector<std::shared_ptr<const FTypeAdapter>>> Adapters;
			std::shared_ptr<void> Value;
		};
		if (!Impl->DecodedAdapters)
		{
			auto Adapters = std::make_shared<std::vector<std::shared_ptr<const FTypeAdapter>>>();
			Adapters->reserve(Impl->Types.size());
			for (const auto& Entry : Impl->Types)
			{
				Adapters->push_back(Entry.second.Adapter);
			}
			Impl->DecodedAdapters = std::move(Adapters);
		}
		auto Owner = std::make_shared<FOwned>();
		Owner->Adapters = Impl->DecodedAdapters;
		Owner->Value = std::move(Value);
		return std::shared_ptr<void>(Owner, Owner->Value.get());
	}
} // namespace VeroScript::Json

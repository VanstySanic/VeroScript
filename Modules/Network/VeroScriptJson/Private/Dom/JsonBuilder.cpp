// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonInternal.h"

namespace VeroScript::Json
{
	using namespace Detail;

	struct FBuilder::FState
	{
		void Active() const
		{
			if (bClosed || (Node.Kind != EKind::Array && Node.Kind != EKind::Object))
			{
				Fail(EError::State, "Builder closed or has an invalid container kind");
			}
		}
		FNode Node;
		std::unordered_map<std::string, std::size_t> Keys;
		bool bClosed = false;
	};

	FBuilder::FBuilder(EKind Kind) : State(std::make_shared<FState>())
	{
		State->Node.Kind = Kind;
	}
	bool FBuilder::TryPush(const FJsonValue& Value, FError& Error)
	{
		return Guard(Error,
			[&]
			{
				State->Active();
				if (State->Node.Kind != EKind::Array)
				{
					Fail(EError::Type, "Expected array builder");
				}
				if (State->Node.Array.size() >= FLimits{}.Nodes || FAccess::Get(Value).Depth >= 256)
				{
					Fail(EError::Limit, "Builder node or depth limit exceeded");
				}
				State->Node.Array.push_back(Value);
			});
	}
	bool FBuilder::TrySet(std::string Key, const FJsonValue& Value, FError& Error)
	{
		return Guard(Error,
			[&]
			{
				State->Active();
				if (State->Node.Kind != EKind::Object)
				{
					Fail(EError::Type, "Expected object builder");
				}
				ValidateUtf8(Key);
				if (Key.size() > FLimits{}.TokenBytes || FAccess::Get(Value).Depth >= 256)
				{
					Fail(EError::Limit, "Builder key or depth limit exceeded");
				}
				auto It = State->Keys.find(Key);
				if (It != State->Keys.end())
				{
					State->Node.Object[It->second].second = Value;
					return;
				}
				if (State->Node.Object.size() >= FLimits{}.Nodes)
				{
					Fail(EError::Limit, "Builder node limit exceeded");
				}
				State->Node.Object.emplace_back(Key, Value);
				try
				{
					State->Keys.emplace(std::move(Key), State->Node.Object.size() - 1);
				}
				catch (...)
				{
					State->Node.Object.pop_back();
					throw;
				}
			});
	}
	bool FBuilder::TryReserve(std::int64_t Capacity, FError& Error)
	{
		return Guard(Error,
			[&]
			{
				State->Active();
				if (Capacity < 0 ||
					static_cast<std::uint64_t>(Capacity) > std::min<std::uint64_t>(FLimits{}.Nodes, SIZE_MAX))
				{
					Fail(EError::Range, "Invalid builder capacity");
				}
				if (State->Node.Kind == EKind::Array)
				{
					State->Node.Array.reserve(static_cast<std::size_t>(Capacity));
				}
				else
				{
					State->Node.Object.reserve(static_cast<std::size_t>(Capacity));
				}
			});
	}
	bool FBuilder::TryCapacity(std::int64_t& Out, FError& Error) const
	{
		return Guard(Error,
			[&]
			{
				State->Active();
				auto Capacity =
					State->Node.Kind == EKind::Array ? State->Node.Array.capacity() : State->Node.Object.capacity();
				if (Capacity > static_cast<std::uint64_t>(INT64_MAX))
				{
					Fail(EError::Range, "Builder capacity exceeds int64 range");
				}
				Out = static_cast<std::int64_t>(Capacity);
			});
	}
	bool FBuilder::TrySize(std::int64_t& Out, FError& Error) const
	{
		return Guard(Error,
			[&]
			{
				State->Active();
				Out = static_cast<std::int64_t>(
					State->Node.Kind == EKind::Array ? State->Node.Array.size() : State->Node.Object.size());
			});
	}
	bool FBuilder::TryFinish(FJsonValue& Out, FError& Error)
	{
		return Guard(Error,
			[&]
			{
				State->Active();
				auto Value = FAccess::Make(State->Node);
				State->Node = {};
				State->Keys.clear();
				State->bClosed = true;
				Out = std::move(Value);
			});
	}
	bool FBuilder::TryReset(EKind Kind, FError& Error)
	{
		return Guard(Error,
			[&]
			{
				if (Kind != EKind::Array && Kind != EKind::Object)
				{
					Fail(EError::Type, "Builder kind must be array or object");
				}
				State->Node = {};
				State->Node.Kind = Kind;
				State->Keys.clear();
				State->bClosed = false;
			});
	}
} // namespace VeroScript::Json

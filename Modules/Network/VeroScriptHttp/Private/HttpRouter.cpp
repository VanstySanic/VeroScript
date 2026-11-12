// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpRouter.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <stdexcept>

namespace VeroScript::Http
{
	namespace Detail
	{
		struct FRouteSegment
		{
			std::string Value;
			unsigned Kind = 3; // Literal, named parameter, trailing wildcard.
		};
		struct FPreparedRoute
		{
			FRoute Value;
			std::vector<FRouteSegment> Segments;
			std::vector<unsigned> Priority;
		};
		struct FRouteTable
		{
			FRouteLimits Limits;
			std::uint64_t Bytes = 0;
			std::vector<FPreparedRoute> Routes;
		};
	} // namespace Detail
	using Detail::FPreparedRoute;
	using Detail::FRouteTable;
	struct FRouter::FImpl
	{
		std::mutex Writer;
		std::shared_ptr<const FRouteTable> Table;
	};
	static bool Failure(Network::FError& Error, Network::EError Code, const char* Message)
	{
		Error.Code = Code;
		Error.Message = Message;
		return false;
	}
	template <class F>
	static bool Guard(Network::FError& Error, F&& Function)
	{
		Error = {};
		try
		{
			return Function();
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = Network::EError::Allocation;
		}
		catch (...)
		{
			Error.Code = Network::EError::Internal;
		}
		Error.Message.clear();
		return false;
	}
	static bool Token(std::string_view Value)
	{
		return !Value.empty() && std::all_of(Value.begin(),
									 Value.end(),
									 [](unsigned char C)
									 {
										 return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
												(C >= '0' && C <= '9') ||
												std::string_view("!#$%&'*+-.^_`|~").find(C) != std::string_view::npos;
									 });
	}
	static bool Identifier(std::string_view Value)
	{
		if (Value.empty() || (Value.front() >= '0' && Value.front() <= '9'))
		{
			return false;
		}
		return std::all_of(Value.begin(),
			Value.end(),
			[](unsigned char C)
			{
				return C == '_' || (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9');
			});
	}
	static bool Segments(std::string_view Path, const FRouteLimits& Limits, std::vector<std::string>& Out,
		Network::FError& Error, bool Pattern)
	{
		if (Path.empty() || Path.front() != '/' || Path.find_first_of("?#") != Path.npos ||
			std::any_of(Path.begin(),
				Path.end(),
				[](unsigned char C)
				{
					return C <= 0x20 || C == 0x7f;
				}))
		{
			return Failure(
				Error, Network::EError::InvalidArgument, "Expected an absolute route path without query or fragment");
		}
		for (std::size_t At = 1;;)
		{
			if (Out.size() >= Limits.MaxSegments)
			{
				return Failure(Error, Network::EError::Limit, "Route segment limit exceeded");
			}
			const auto End = Path.find('/', At);
			const auto Raw = Path.substr(At, End == Path.npos ? Path.size() - At : End - At);
			std::string Value;
			if (Pattern)
			{
				Value = Raw;
			}
			else if (!Network::TryPercentDecode(Raw, Value, Error))
			{
				return false;
			}
			Out.push_back(std::move(Value));
			if (End == Path.npos)
			{
				break;
			}
			At = End + 1;
		}
		return true;
	}
	static bool Prepare(FRoute Route, const FRouteLimits& Limits, FPreparedRoute& Out, Network::FError& Error)
	{
		if (Route.Method.size() > Limits.MaxMethodBytes || Route.Pattern.size() > Limits.MaxPatternBytes)
		{
			return Failure(Error, Network::EError::Limit, "Route method or pattern byte limit exceeded");
		}
		if (!Token(Route.Method))
		{
			return Failure(Error, Network::EError::InvalidArgument, "Invalid route method or pattern length");
		}
		std::vector<std::string> Parts;
		if (!Segments(Route.Pattern, Limits, Parts, Error, true))
		{
			return false;
		}
		FPreparedRoute Value;
		Value.Value = std::move(Route);
		std::set<std::string> Names;
		for (std::size_t I = 0; I < Parts.size(); ++I)
		{
			Detail::FRouteSegment Segment;
			const auto& Part = Parts[I];
			if (!Part.empty() && (Part.front() == ':' || Part.front() == '*'))
			{
				Segment.Kind = Part.front() == ':' ? 2 : 1;
				Segment.Value = Part.substr(1);
				if (!Identifier(Segment.Value) || !Names.insert(Segment.Value).second ||
					(Segment.Kind == 1 && I + 1 != Parts.size()))
				{
					return Failure(
						Error, Network::EError::InvalidArgument, "Invalid, repeated or nonterminal route parameter");
				}
			}
			else if (!Network::TryPercentDecode(Part, Segment.Value, Error))
			{
				return false;
			}
			Value.Priority.push_back(Segment.Kind);
			Value.Segments.push_back(std::move(Segment));
		}
		if (Value.Segments.back().Kind != 1)
		{
			Value.Priority.push_back(4);
		}
		Value.Priority.push_back(Value.Value.Method == "*" ? 0 : 1);
		Out = std::move(Value);
		return true;
	}
	static bool Conflict(const FPreparedRoute& A, const FPreparedRoute& B)
	{
		if (A.Value.Method != B.Value.Method || A.Priority != B.Priority || A.Segments.size() != B.Segments.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < A.Segments.size(); ++I)
		{
			if (A.Segments[I].Kind == 3 && A.Segments[I].Value != B.Segments[I].Value)
			{
				return false;
			}
		}
		return true;
	}
	FRouter::FRouter(FRouteLimits Limits) : Impl(std::make_shared<FImpl>())
	{
		if (!Limits.MaxRoutes || !Limits.MaxSegments || !Limits.MaxPatternBytes || !Limits.MaxTotalPatternBytes ||
			!Limits.MaxTargetBytes || !Limits.MaxMethodBytes)
		{
			throw std::invalid_argument("Route limits must be positive");
		}
		auto Table = std::make_shared<FRouteTable>();
		Table->Limits = Limits;
		Impl->Table = std::move(Table);
	}
	bool FRouter::TryAdd(FRoute Route, Network::FError& Error)
	{
		return Guard(Error,
			[&]
			{
				std::lock_guard<std::mutex> Lock(Impl->Writer);
				const auto Current = std::atomic_load(&Impl->Table);
				if (Current->Routes.size() >= Current->Limits.MaxRoutes)
				{
					return Failure(Error, Network::EError::Limit, "Route count limit exceeded");
				}
				const auto Bytes = static_cast<std::uint64_t>(Route.Pattern.size()) + Route.Method.size();
				if (Bytes > Current->Limits.MaxTotalPatternBytes -
								std::min(Current->Bytes, Current->Limits.MaxTotalPatternBytes))
				{
					return Failure(Error, Network::EError::Limit, "Total route pattern limit exceeded");
				}
				FPreparedRoute Prepared;
				if (!Prepare(std::move(Route), Current->Limits, Prepared, Error))
				{
					return false;
				}
				for (const auto& R : Current->Routes)
				{
					if (R.Value.Id == Prepared.Value.Id || Conflict(R, Prepared))
					{
						return Failure(
							Error, Network::EError::State, "Duplicate route identity or ambiguous route pattern");
					}
				}
				auto Next = std::make_shared<FRouteTable>(*Current);
				Next->Bytes += Bytes;
				Next->Routes.push_back(std::move(Prepared));
				std::sort(Next->Routes.begin(),
					Next->Routes.end(),
					[](const auto& A, const auto& B)
					{
						return A.Priority > B.Priority;
					});
				std::atomic_store(&Impl->Table, std::shared_ptr<const FRouteTable>(std::move(Next)));
				return true;
			});
	}
	bool FRouter::TryRemove(std::uint64_t Id, bool& Removed, Network::FError& Error)
	{
		return Guard(Error,
			[&]
			{
				std::lock_guard<std::mutex> Lock(Impl->Writer);
				const auto Current = std::atomic_load(&Impl->Table);
				const auto It = std::find_if(Current->Routes.begin(),
					Current->Routes.end(),
					[Id](const auto& R)
					{
						return R.Value.Id == Id;
					});
				if (It == Current->Routes.end())
				{
					Removed = false;
					return true;
				}
				auto Next = std::make_shared<FRouteTable>(*Current);
				Next->Bytes -= It->Value.Method.size() + It->Value.Pattern.size();
				Next->Routes.erase(Next->Routes.begin() + std::distance(Current->Routes.begin(), It));
				std::atomic_store(&Impl->Table, std::shared_ptr<const FRouteTable>(std::move(Next)));
				Removed = true;
				return true;
			});
	}
	FRouteSnapshot FRouter::Snapshot() const noexcept
	{
		FRouteSnapshot Result;
		Result.Table = std::atomic_load(&Impl->Table);
		return Result;
	}
	std::uint64_t FRouteSnapshot::Size() const noexcept
	{
		return Table ? Table->Routes.size() : 0;
	}
	bool FRouteSnapshot::TryMatch(
		std::string_view Method, std::string_view Target, FRouteMatch& Out, Network::FError& Error) const
	{
		return Guard(Error,
			[&]
			{
				if (!Table)
				{
					return Failure(Error, Network::EError::State, "Route snapshot is empty");
				}
				if (Method.size() > Table->Limits.MaxMethodBytes || Target.size() > Table->Limits.MaxTargetBytes)
				{
					return Failure(Error, Network::EError::Limit, "Route method or target byte limit exceeded");
				}
				if (std::any_of(Target.begin(),
						Target.end(),
						[](unsigned char C)
						{
							return C <= 0x20 || C == 0x7f;
						}))
				{
					return Failure(Error, Network::EError::InvalidArgument, "Invalid route request target character");
				}
				if (!Token(Method) || Method == "*" || Target.find('#') != Target.npos)
				{
					return Failure(Error, Network::EError::InvalidArgument, "Invalid route request method or target");
				}
				const auto At = Target.find('?');
				std::vector<std::string> Parts;
				if (!Segments(Target.substr(0, At), Table->Limits, Parts, Error, false))
				{
					return false;
				}
				FRouteMatch Result;
				Result.RawTarget = Target;
				for (const auto& Part : Parts)
				{
					Result.Path += '/';
					Result.Path += Network::PercentEncode(Part);
				}
				if (At != Target.npos)
				{
					const auto Query = Target.substr(At + 1);
					if (!Query.empty() && static_cast<std::uint64_t>(std::count(Query.begin(), Query.end(), '&')) + 1 >
											  Table->Limits.MaxQueryParameters)
					{
						return Failure(Error, Network::EError::Limit, "Route query parameter limit exceeded");
					}
					if (!Network::TryParseQuery(Query, Result.Query, Error))
					{
						return false;
					}
				}
				for (const auto& Route : Table->Routes)
				{
					if (Route.Value.Method != "*" && Route.Value.Method != Method)
					{
						continue;
					}
					const bool Wildcard = Route.Segments.back().Kind == 1;
					if ((!Wildcard && Route.Segments.size() != Parts.size()) ||
						(Wildcard && Parts.size() < Route.Segments.size()))
					{
						continue;
					}
					bool Matches = true;
					for (std::size_t I = 0; I < Route.Segments.size(); ++I)
					{
						if (Route.Segments[I].Kind == 3 && Route.Segments[I].Value != Parts[I])
						{
							Matches = false;
							break;
						}
					}
					if (!Matches)
					{
						continue;
					}
					for (std::size_t I = 0; I < Route.Segments.size(); ++I)
					{
						const auto& S = Route.Segments[I];
						if (S.Kind == 2)
						{
							Result.Parameters.emplace_back(S.Value, Parts[I]);
						}
						else if (S.Kind == 1)
						{
							std::string Tail = Parts[I];
							for (std::size_t J = I + 1; J < Parts.size(); ++J)
							{
								Tail += '/';
								Tail += Parts[J];
							}
							Result.Parameters.emplace_back(S.Value, std::move(Tail));
						}
					}
					Result.bFound = true;
					Result.Id = Route.Value.Id;
					break;
				}
				Out = std::move(Result);
				return true;
			});
	}
} // namespace VeroScript::Http

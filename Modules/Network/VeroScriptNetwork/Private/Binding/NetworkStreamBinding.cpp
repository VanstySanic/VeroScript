// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkBinding.h"

namespace VeroScript::NetworkBinding
{
	struct FPipeValue
	{
		FHostObjectHandle Context;
		Network::FBytePipe Pipe;
	};

	template <class TValue>
	static bool StreamQueries(FState& S, FHostTypeId Type, std::string& Error)
	{
		using T = ENativeType;
		for (const char* Name : {"state", "error_code", "error", "message_id", "buffered", "capacity"})
		{
			if (!Member(
					S,
					Type,
					Name,
					(std::string_view(Name) == "error" || std::string_view(Name) == "message_id") ? T::String
																								  : T::Int64,
					{},
					[Name](const auto& C, auto& O, auto&)
					{
						const auto& V = This<TValue>(C);
						if (std::string_view(Name) == "state")
						{
							O = static_cast<std::int64_t>(V.Last.State);
						}
						else if (std::string_view(Name) == "error_code")
						{
							O = static_cast<std::int64_t>(V.Last.Error.Code);
						}
						else if (std::string_view(Name) == "error")
						{
							O = V.Last.Error.Message;
						}
						else if (std::string_view(Name) == "message_id")
						{
							O = std::string(V.Last.Error.Id());
						}
						else
						{
							O = static_cast<std::int64_t>(
								std::string_view(Name) == "buffered" ? V.Value->BufferedBytes() : V.Value->Capacity());
						}
						return true;
					},
					Error))
			{
				return false;
			}
		}
		return true;
	}
	bool RegisterNetworkStreams(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Reader, Writer, Pipe;
		if (!Type(S, "Network::readable_stream", Reader, Error) || !Type(S, "Network::writable_stream", Writer, Error) ||
			!Type(S, "Network::byte_pipe", Pipe, Error) || !StreamQueries<FReadStreamValue>(S, Reader, Error) ||
			!StreamQueries<FWriteStreamValue>(S, Writer, Error))
		{
			return false;
		}
		if (!Member(
				S,
				Api,
				"create_byte_pipe",
				T::HostObject,
				{T::HostObject, T::Int64},
				[&S](const auto& C, auto& O, auto& E)
				{
					auto Owner = ContextArg(C);
					if (!Lease(S, Owner, E))
					{
						return false;
					}
					const auto Capacity = Count(Integer(C, 1));
					if (!Capacity || Capacity > 16 * 1024 * 1024)
					{
						E = "Stream capacity must be between 1 and 16777216";
						return false;
					}
					Network::FBytePipe Pipe;
					Network::FError Failure;
					if (!Network::TryCreateBytePipe(
							*static_cast<FContextValue*>(Owner->Payload)->Value, Capacity, Pipe, Failure))
					{
						E = "Cannot create byte pipe";
						return false;
					}
					return Make(S, "Network::byte_pipe", FPipeValue{Owner, std::move(Pipe)}, O, E);
				},
				Error,
				"Network::byte_pipe",
				{"Network::context", ""},
				true))
		{
			return false;
		}
		if (!Member(
				S,
				Pipe,
				"reader",
				T::HostObject,
				{},
				[&S](const auto& C, auto& O, auto& E)
				{
					auto& V = This<FPipeValue>(C);
					return Make(S, "Network::readable_stream", FReadStreamValue{V.Context, V.Pipe.Reader, {}}, O, E);
				},
				Error,
				"Network::readable_stream") ||
			!Member(
				S,
				Pipe,
				"writer",
				T::HostObject,
				{},
				[&S](const auto& C, auto& O, auto& E)
				{
					auto& V = This<FPipeValue>(C);
					return Make(S, "Network::writable_stream", FWriteStreamValue{V.Context, V.Pipe.Writer, {}}, O, E);
				},
				Error,
				"Network::writable_stream"))
		{
			return false;
		}
		return Member(
				   S,
				   Reader,
				   "read",
				   T::String,
				   {T::Int64},
				   [](const auto& C, auto& O, auto& E)
				   {
					   auto& V = This<FReadStreamValue>(C);
					   const auto Requested = Count(Integer(C, 0));
					   const auto N = std::min<std::uint64_t>({Requested, V.Value->Capacity(), 64 * 1024});
					   if (!CheckWork(C, N, E))
					   {
						   return false;
					   }
					   std::string Text(static_cast<std::size_t>(N), '\0');
					   V.Last = V.Value->Read(Text.data(), static_cast<std::int64_t>(N));
					   Text.resize(static_cast<std::size_t>(V.Last.Bytes));
					   O = std::move(Text);
					   return true;
				   },
				   Error,
				   "",
				   {},
				   false,
				   true) &&
			   Member(
				   S,
				   Reader,
				   "close",
				   T::Void,
				   {},
				   [](const auto& C, auto&, auto&)
				   {
					   This<FReadStreamValue>(C).Value->Cancel();
					   return true;
				   },
				   Error,
				   "",
				   {},
				   false,
				   true) &&
			   Member(
				   S,
				   Writer,
				   "write",
				   T::Int64,
				   {T::String},
				   [](const auto& C, auto& O, auto& E)
				   {
					   auto& V = This<FWriteStreamValue>(C);
					   const auto& Text = String(C, 0);
					   const auto N = std::min<std::size_t>(Text.size(), 64 * 1024);
					   if (!CheckWork(C, N, E))
					   {
						   return false;
					   }
					   V.Last = V.Value->Write(Text.data(), static_cast<std::int64_t>(N));
					   O = static_cast<std::int64_t>(V.Last.Bytes);
					   return true;
				   },
				   Error,
				   "",
				   {},
				   false,
				   true) &&
			   Member(
				   S,
				   Writer,
				   "finish",
				   T::Void,
				   {},
				   [](const auto& C, auto&, auto&)
				   {
					   This<FWriteStreamValue>(C).Value->Finish();
					   return true;
				   },
				   Error,
				   "",
				   {},
				   false,
				   true) &&
			   Member(
				   S,
				   Writer,
				   "fail",
				   T::Void,
				   {T::String},
				   [](const auto& C, auto&, auto&)
				   {
					   This<FWriteStreamValue>(C).Value->Fail({Network::EError::Callback, String(C, 0), 0});
					   return true;
				   },
				   Error,
				   "",
				   {},
				   false,
				   true);
	}
} // namespace VeroScript::NetworkBinding

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "NetworkTaskBinding.h"
#include "VeroScriptHttp.h"

namespace VeroScript::NetworkBinding
{
	using FStreamValue = FHandle<Http::FStreamingOperation>;
	bool RegisterHttpStreams(
		FState& S, FHostTypeId Client, FHostTypeId Response, FHostTypeId Request, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Stream;
		if (!Type(S, "http::stream_operation", Stream, Error))
		{
			return false;
		}
		for (const bool CountOnly : {true, false})
		{
			if (!Member(
					S,
					Response,
					CountOnly ? "trailer_count" : "trailer",
					CountOnly ? T::Int64 : T::String,
					CountOnly ? std::vector<T>{T::String} : std::vector<T>{T::String, T::Int64},
					[CountOnly](const auto& C, auto& O, auto& E)
					{
						const auto Values = This<FHttpResponseValue>(C).Value->Trailers.GetAll(String(C, 0));
						if (CountOnly)
						{
							O = static_cast<std::int64_t>(Values.size());
							return true;
						}
						const auto Index = Count(Integer(C, 1));
						if (Index >= Values.size())
						{
							E = "Trailer index is out of range";
							return false;
						}
						O = Values[static_cast<std::size_t>(Index)];
						return true;
					},
					Error))
			{
				return false;
			}
		}
		return Member(
				   S,
				   Request,
				   "set_upload",
				   T::Void,
				   {T::HostObject, T::Int64},
				   [](const auto& C, auto&, auto& E)
				   {
					   const auto Length = Integer(C, 1);
					   if (Length < -1)
					   {
						   E = "Upload length must be -1 or nonnegative";
						   return false;
					   }
					   auto& R = This<Http::FRequest>(C);
					   R.Body.clear();
					   R.Multipart.reset();
					   R.Upload = Object<FReadStreamValue>(C, 0).Value;
					   R.UploadFactory = {};
					   R.UploadLength = Length == -1 ? std::nullopt
													 : std::optional<std::uint64_t>(static_cast<std::uint64_t>(Length));
					   R.Headers.Erase("Content-Length");
					   R.Headers.Erase("Transfer-Encoding");
					   return true;
				   },
				   Error,
				   "",
				   {"Network::readable_stream", ""},
				   false,
				   true) &&
			   Member(
				   S,
				   Request,
				   "set_upload_limit",
				   T::Void,
				   {T::Int64},
				   [](const auto& C, auto&, auto&)
				   {
					   This<Http::FRequest>(C).MaxUploadBytes = Count(Integer(C, 0));
					   return true;
				   },
				   Error,
				   "",
				   {},
				   false,
				   true) &&
			   Member(
				   S,
				   Client,
				   "send_stream",
				   T::HostObject,
				   {T::HostObject, T::Int64},
				   [&S](const auto& C, auto& O, auto& E)
				   {
					   auto& V = This<FHandle<Http::FClient>>(C);
					   if (!Lease(S, V.Context, E))
					   {
						   return false;
					   }
					   return StartTask<Http::FStreamingOperation>(
						   S,
						   "http::stream_operation",
						   V.Context,
						   [&]
						   {
							   return V.Value->SendStream(Object<Http::FRequest>(C, 0), Count(Integer(C, 1)));
						   },
						   O,
						   E);
				   },
				   Error,
				   "http::stream_operation",
				   {"http::request", ""},
				   false,
				   true) &&
			   Member(
				   S,
				   Stream,
				   "operation",
				   T::HostObject,
				   {},
				   [&S](const auto& C, auto& O, auto& E)
				   {
					   const auto& V = This<FStreamValue>(C);
					   return Make(S, "http::operation", FOperationValue{V.Context, V.Value->Operation()}, O, E);
				   },
				   Error,
				   "http::operation") &&
			   Member(
				   S,
				   Stream,
				   "body",
				   T::HostObject,
				   {},
				   [&S](const auto& C, auto& O, auto& E)
				   {
					   const auto& V = This<FStreamValue>(C);
					   auto Body = V.Value->Body();
					   if (!Body)
					   {
						   E = "Stream operation has no body";
						   return false;
					   }
					   const auto& Service = static_cast<FContextValue*>(V.Context->Payload)->Services;
					   if (!Service)
					   {
						   E = "The Network extension does not provide the stream binding service";
						   return false;
					   }
					   return Service->MakeReadable(V.Context, std::move(Body), O, E);
				   },
				   Error,
				   "Network::readable_stream") &&
			   Member(
				   S,
				   Stream,
				   "head_ready",
				   T::Bool,
				   {},
				   [](const auto& C, auto& O, auto& E)
				   {
					   Http::FResponse Head;
					   Network::FError Failure;
					   O = This<FStreamValue>(C).Value->TryHead(Head, Failure);
					   E = Failure.Message;
					   return !Failure;
				   },
				   Error) &&
			   Member(
				   S,
				   Stream,
				   "head",
				   T::HostObject,
				   {},
				   [&S](const auto& C, auto& O, auto& E)
				   {
					   Http::FResponse Head;
					   Network::FError Failure;
					   if (!This<FStreamValue>(C).Value->TryHead(Head, Failure))
					   {
						   E = Failure ? Failure.Message : "Response head is not available";
						   return false;
					   }
					   return Make(S,
						   "http::response",
						   FHttpResponseValue{std::make_shared<const Http::FResponse>(std::move(Head))},
						   O,
						   E);
				   },
				   Error,
				   "http::response") &&
			   Member(
				   S,
				   Response,
				   "body_bytes",
				   T::Int64,
				   {},
				   [](const auto& C, auto& O, auto& E)
				   {
					   const auto Count = This<FHttpResponseValue>(C).Value->BodyBytes;
					   if (Count > static_cast<std::uint64_t>(INT64_MAX))
					   {
						   E = "HTTP byte count exceeds int64";
						   return false;
					   }
					   O = static_cast<std::int64_t>(Count);
					   return true;
				   },
				   Error);
	}
} // namespace VeroScript::NetworkBinding

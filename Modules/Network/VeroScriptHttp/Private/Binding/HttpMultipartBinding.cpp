// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttpMultipart.h"

namespace VeroScript::NetworkBinding
{
	struct FMultipartValue
	{
		FHostObjectHandle Context;
		std::shared_ptr<Http::FMultipartReader> Reader;
		Http::FMultipartFeedResult Last;
		Http::FMultipartEvent Event;
	};
	bool RegisterHttpMultipart(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId OptionsType, ReaderType, EventType;
		if (!ValueType<Http::FMultipartOptions>(S, "http::multipart_options", OptionsType, Error) ||
			!Type(S, "http::multipart_reader", ReaderType, Error) ||
			!Type(S, "http::multipart_event", EventType, Error))
		{
			return false;
		}
		if (!Member(
				S,
				Api,
				"make_multipart_options",
				T::HostObject,
				{},
				[&S](const auto&, auto& O, auto& E)
				{
					return Make(S, "http::multipart_options", Http::FMultipartOptions{}, O, E);
				},
				Error,
				"http::multipart_options",
				{},
				true))
		{
			return false;
		}
		const char* Names[] = {"max_parts",
			"max_header_bytes",
			"max_total_header_bytes",
			"max_part_body_bytes",
			"max_total_body_bytes",
			"max_input_bytes",
			"max_preamble_bytes",
			"max_epilogue_bytes"};
		for (unsigned I = 0; I < 8; ++I)
		{
			if (!Member(
					S,
					OptionsType,
					Names[I],
					T::Int64,
					{},
					[I](const auto& C, auto& O, auto& E)
					{
						const auto& V = This<Http::FMultipartOptions>(C);
						const std::uint64_t Values[] = {V.MaxParts,
							V.MaxHeaderBytes,
							V.MaxTotalHeaderBytes,
							V.MaxPartBodyBytes,
							V.MaxTotalBodyBytes,
							V.MaxInputBytes,
							V.MaxPreambleBytes,
							V.MaxEpilogueBytes};
						if (Values[I] > static_cast<std::uint64_t>(INT64_MAX))
						{
							E = "Multipart option exceeds int64";
							return false;
						}
						O = static_cast<std::int64_t>(Values[I]);
						return true;
					},
					Error))
			{
				return false;
			}
			const std::string Setter = std::string("set_") + Names[I];
			if (!Member(
					S,
					OptionsType,
					Setter.c_str(),
					T::Void,
					{T::Int64},
					[I](const auto& C, auto&, auto& E)
					{
						const auto Value = Count(Integer(C, 0));
						auto& V = This<Http::FMultipartOptions>(C);
						if (I == 0)
						{
							if (Value > UINT32_MAX)
							{
								E = "Multipart part count exceeds uint32";
								return false;
							}
							V.MaxParts = static_cast<std::uint32_t>(Value);
						}
						else
						{
							std::uint64_t* Fields[] = {&V.MaxHeaderBytes,
								&V.MaxTotalHeaderBytes,
								&V.MaxPartBodyBytes,
								&V.MaxTotalBodyBytes,
								&V.MaxInputBytes,
								&V.MaxPreambleBytes,
								&V.MaxEpilogueBytes};
							*Fields[I - 1] = Value;
						}
						return true;
					},
					Error,
					"",
					{},
					false,
					true))
			{
				return false;
			}
		}
		for (const bool WithOptions : {false, true})
		{
			if (!Member(
					S,
					Api,
					WithOptions ? "make_multipart_reader_with_options" : "make_multipart_reader",
					T::HostObject,
					WithOptions ? std::vector<T>{T::HostObject, T::String, T::HostObject}
								: std::vector<T>{T::HostObject, T::String},
					[&S, WithOptions](const auto& C, auto& O, auto& E)
					{
						auto Owner = ContextArg(C);
						if (!Lease(S, Owner, E))
						{
							return false;
						}
						auto W = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
						if (W.Check && !W.Check(W.UserData, String(C, 1).size()))
						{
							E = "Multipart initialization cancelled";
							return false;
						}
						std::shared_ptr<Http::FMultipartReader> Reader;
						Network::FError Failure;
						if (!Http::FMultipartReader::TryCreate(String(C, 1),
								Reader,
								Failure,
								WithOptions ? Object<Http::FMultipartOptions>(C, 2) : Http::FMultipartOptions{},
								&Context(Owner)))
						{
							E = Failure.Message;
							return false;
						}
						return Make(
							S, "http::multipart_reader", FMultipartValue{Owner, std::move(Reader), {}, {}}, O, E);
					},
					Error,
					"http::multipart_reader",
					WithOptions ? std::vector<std::string>{"Network::context", "", "http::multipart_options"}
								: std::vector<std::string>{"Network::context", ""},
					true))
			{
				return false;
			}
		}
		if (!Member(
				S,
				ReaderType,
				"feed",
				T::Int64,
				{T::String, T::Bool},
				[](const auto& C, auto& O, auto& E)
				{
					auto& V = This<FMultipartValue>(C);
					auto W = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
					V.Last = V.Reader->Feed(String(C, 0), std::get<bool>(C.Args[1]), V.Event, {W.Check, W.UserData});
					if (V.Reader->Error().Code == Network::EError::Cancelled)
					{
						E = "Multipart read cancelled";
						return false;
					}
					O = static_cast<std::int64_t>(V.Last.Consumed);
					return true;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				ReaderType,
				"state",
				T::Int64,
				{},
				[](const auto& C, auto& O, auto&)
				{
					O = static_cast<std::int64_t>(This<FMultipartValue>(C).Last.State);
					return true;
				},
				Error) ||
			!Member(
				S,
				ReaderType,
				"error_code",
				T::Int64,
				{},
				[](const auto& C, auto& O, auto&)
				{
					O = static_cast<std::int64_t>(This<FMultipartValue>(C).Reader->Error().Code);
					return true;
				},
				Error) ||
			!Member(
				S,
				ReaderType,
				"error",
				T::String,
				{},
				[](const auto& C, auto& O, auto&)
				{
					O = This<FMultipartValue>(C).Reader->Error().Message;
					return true;
				},
				Error) ||
			!Member(
				S,
				ReaderType,
				"message_id",
				T::String,
				{},
				[](const auto& C, auto& O, auto&)
				{
					O = std::string(This<FMultipartValue>(C).Reader->Error().Id());
					return true;
				},
				Error) ||
			!Member(
				S,
				ReaderType,
				"event",
				T::HostObject,
				{},
				[&S](const auto& C, auto& O, auto& E)
				{
					const auto& V = This<FMultipartValue>(C);
					if (V.Last.State != Http::EMultipartReadState::EventReady)
					{
						E = "Multipart reader has no current event";
						return false;
					}
					return Make(S, "http::multipart_event", V.Event, O, E);
				},
				Error,
				"http::multipart_event") ||
			!Member(
				S,
				EventType,
				"kind",
				T::Int64,
				{},
				[](const auto& C, auto& O, auto&)
				{
					O = static_cast<std::int64_t>(This<Http::FMultipartEvent>(C).Type);
					return true;
				},
				Error) ||
			!Member(
				S,
				EventType,
				"part_index",
				T::Int64,
				{},
				[](const auto& C, auto& O, auto& E)
				{
					const auto& V = This<Http::FMultipartEvent>(C);
					if (V.Type == Http::EMultipartEvent::End)
					{
						E = "Multipart end has no part index";
						return false;
					}
					if (V.PartIndex > static_cast<std::uint64_t>(INT64_MAX))
					{
						E = "Multipart part index exceeds int64";
						return false;
					}
					O = static_cast<std::int64_t>(V.PartIndex);
					return true;
				},
				Error) ||
			!Member(
				S,
				EventType,
				"data",
				T::String,
				{},
				[](const auto& C, auto& O, auto& E)
				{
					const auto& V = This<Http::FMultipartEvent>(C);
					if (!V.Data)
					{
						E = "Multipart event has no body data";
						return false;
					}
					O = BytesText(*V.Data);
					return true;
				},
				Error))
		{
			return false;
		}
		for (unsigned Field = 0; Field < 4; ++Field)
		{
			const char* Members[] = {"name", "has_filename", "filename", "content_type"};
			if (!Member(
					S,
					EventType,
					Members[Field],
					Field == 1 ? T::Bool : T::String,
					{},
					[Field](const auto& C, auto& O, auto& E)
					{
						const auto& H = This<Http::FMultipartEvent>(C).Head;
						if (!H)
						{
							E = "Multipart event has no part headers";
							return false;
						}
						if (Field == 0)
						{
							O = H->Name;
						}
						else if (Field == 1)
						{
							O = H->Filename.has_value();
						}
						else if (Field == 2)
						{
							if (!H->Filename)
							{
								E = "Multipart part has no filename";
								return false;
							}
							O = *H->Filename;
						}
						else
						{
							O = H->ContentType;
						}
						return true;
					},
					Error))
			{
				return false;
			}
		}
		for (const bool CountOnly : {true, false})
		{
			if (!Member(
					S,
					EventType,
					CountOnly ? "header_count" : "header",
					CountOnly ? T::Int64 : T::String,
					CountOnly ? std::vector<T>{T::String} : std::vector<T>{T::String, T::Int64},
					[CountOnly](const auto& C, auto& O, auto& E)
					{
						const auto& H = This<Http::FMultipartEvent>(C).Head;
						if (!H)
						{
							E = "Multipart event has no part headers";
							return false;
						}
						const auto Fields = H->Headers.GetAll(String(C, 0));
						if (CountOnly)
						{
							O = static_cast<std::int64_t>(Fields.size());
							return true;
						}
						const auto I = Count(Integer(C, 1));
						if (I >= Fields.size())
						{
							E = "Multipart header index is out of range";
							return false;
						}
						O = Fields[static_cast<std::size_t>(I)];
						return true;
					},
					Error))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::NetworkBinding

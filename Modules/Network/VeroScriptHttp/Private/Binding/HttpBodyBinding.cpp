// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttp.h"
#include "VeroScriptHttpFiles.h"

namespace VeroScript::NetworkBinding
{
	struct FScriptForm
	{
		std::vector<Network::FQueryParameter> Fields;
		std::uint64_t MaxEncodedBytes = 0;
	};

	bool RegisterHttpBody(FState& S, FHostTypeId Api, FHostTypeId Request, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Form;
		if (!ValueType<FScriptForm>(S, "http::form", Form, Error))
		{
			return false;
		}

		for (const unsigned SourceKind : {0u, 1u, 2u})
		{
			const bool bStream = SourceKind != 0, bPath = SourceKind == 2;
			for (const bool bFile : {false, true})
			{
				auto Types = bFile ? std::vector<T>{T::String, T::String, T::String} : std::vector<T>{T::String};
				const auto SourceIndex = Types.size();
				Types.push_back(bStream ? T::HostObject : T::String);
				std::vector<std::string> Names(Types.size());
				if (bStream)
				{
					Names.back() = bPath ? "Network::file_executor" : "Network::readable_stream";
					Types.push_back(bPath ? T::HostObject : T::Int64);
					Names.push_back(bPath ? "Network::file_read_options" : "");
				}
				if (!Member(
						S,
						Request,
						bPath	  ? (bFile ? "add_file_path" : "add_field_path")
						: bStream ? (bFile ? "add_file_stream" : "add_field_stream")
								  : (bFile ? "add_file" : "add_field"),
						T::Void,
						Types,
						[bFile, bStream, bPath, SourceIndex](const auto& C, auto&, auto& E)
						{
							auto& R = This<Http::FRequest>(C);
							if (!R.Body.empty() || !R.Multipart)
							{
								throw std::logic_error("Call begin_multipart before adding multipart fields");
							}
							if (R.Multipart->size() >= 256)
							{
								throw std::length_error("Multipart field limit exceeded");
							}
							std::uint64_t Remaining = 16 * 1024 * 1024;
							auto Charge = [&](std::size_t Count)
							{
								if (Count > Remaining)
								{
									throw std::length_error("Multipart byte limit exceeded");
								}
								Remaining -= Count;
							};
							for (const auto& Part : *R.Multipart)
							{
								Charge(Part.Name.size());
								Charge(Part.ContentType.size());
								Charge(Part.Filename ? Part.Filename->size() : 0);
								Charge(Part.Body.size());
							}
							for (std::uint32_t I = 0; I < SourceIndex + (bStream ? 0 : 1); ++I)
							{
								Charge(String(C, I).size());
							}
							Http::FMultipartPart Part;
							Part.Name = String(C, 0);
							if (bFile)
							{
								Part.Filename = String(C, 1);
								Part.ContentType = String(C, 2);
							}
							if (bPath)
							{
								Part.UploadFactory = Http::MakeFileUploadFactory(
									Object<FHandle<Network::FFileExecutor>>(C, SourceIndex).Value,
									Object<Network::FFileReadOptions>(C, SourceIndex + 1));
							}
							else if (bStream)
							{
								const auto Length = Integer(C, static_cast<std::uint32_t>(SourceIndex + 1));
								if (Length < -1)
								{
									E = "Multipart length must be -1 or nonnegative";
									return false;
								}
								Part.Upload =
									Object<FReadStreamValue>(C, static_cast<std::uint32_t>(SourceIndex)).Value;
								if (Length >= 0)
								{
									Part.UploadLength = static_cast<std::uint64_t>(Length);
								}
							}
							else
							{
								Part.Body = Bytes(String(C, static_cast<std::uint32_t>(SourceIndex)));
							}
							R.Multipart->push_back(std::move(Part));
							return true;
						},
						Error,
						"",
						Names,
						false,
						true))
				{
					return false;
				}
			}
		}

		return Member(
				   S,
				   Api,
				   "create_form",
				   T::HostObject,
				   {},
				   [&S](const auto&, auto& O, auto& E)
				   {
					   return Make(S, "http::form", FScriptForm{}, O, E);
				   },
				   Error,
				   "http::form",
				   {},
				   true) &&
			   Member(
				   S,
				   Form,
				   "add",
				   T::Void,
				   {T::String, T::String},
				   [](const auto& C, auto&, auto&)
				   {
					   auto& Form = This<FScriptForm>(C);
					   constexpr std::uint64_t Limit = 16 * 1024 * 1024;
					   if (Form.Fields.size() >= 256 || String(C, 0).size() > Limit || String(C, 1).size() > Limit)
					   {
						   throw std::length_error("Form field limit exceeded");
					   }
					   const auto Bytes = static_cast<std::uint64_t>(String(C, 0).size()) + String(C, 1).size();
					   const auto Remaining = Limit - Form.MaxEncodedBytes;
					   if (Remaining < 2 || Bytes > (Remaining - 2) / 3)
					   {
						   throw std::length_error("Form byte limit exceeded");
					   }
					   Form.Fields.push_back({String(C, 0), String(C, 1), true});
					   Form.MaxEncodedBytes += 3 * Bytes + 2;
					   return true;
				   },
				   Error,
				   "",
				   {},
				   false,
				   true) &&
			   Member(
				   S,
				   Form,
				   "encode",
				   T::String,
				   {},
				   [](const auto& C, auto& O, auto&)
				   {
					   O = Network::BuildQuery(This<FScriptForm>(C).Fields, true);
					   return true;
				   },
				   Error) &&
			   Member(
				   S,
				   Request,
				   "set_form",
				   T::Void,
				   {T::HostObject},
				   [](const auto& C, auto&, auto& E)
				   {
					   auto& R = This<Http::FRequest>(C);
					   auto Headers = R.Headers;
					   Network::FError Failure;
					   if (!Headers.TrySet("Content-Type", "application/x-www-form-urlencoded", Failure))
					   {
						   E = Failure.Message;
						   return false;
					   }
					   Headers.Erase("Content-Length");
					   Headers.Erase("Transfer-Encoding");
					   auto Body = Bytes(Network::BuildQuery(Object<FScriptForm>(C, 0).Fields, true));
					   R.Headers = std::move(Headers);
					   R.Body = std::move(Body);
					   R.Multipart.reset();
					   R.Upload.reset();
					   R.UploadFactory = {};
					   R.UploadLength.reset();
					   return true;
				   },
				   Error,
				   "",
				   {"http::form"},
				   false,
				   true) &&
			   Member(
				   S,
				   Request,
				   "begin_multipart",
				   T::Void,
				   {},
				   [](const auto& C, auto&, auto&)
				   {
					   auto& R = This<Http::FRequest>(C);
					   R.Body.clear();
					   R.Upload.reset();
					   R.UploadFactory = {};
					   R.UploadLength.reset();
					   R.Multipart.emplace();
					   R.Headers.Erase("Content-Type");
					   R.Headers.Erase("Content-Length");
					   R.Headers.Erase("Transfer-Encoding");
					   return true;
				   },
				   Error,
				   "",
				   {},
				   false,
				   true);
	}
} // namespace VeroScript::NetworkBinding

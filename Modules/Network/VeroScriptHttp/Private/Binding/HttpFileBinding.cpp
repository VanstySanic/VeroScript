// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "NetworkTaskBinding.h"
#include "VeroScriptHttpFiles.h"

namespace VeroScript::NetworkBinding
{
	struct FDownloadProgressValue
	{
		FHostObjectHandle Context;
		Http::FDownloadProgress Value;
	};
	bool RegisterHttpFiles(FState& S, FHostTypeId Api, FHostTypeId Client, FHostTypeId Request, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId OptionsType, TaskType, ProgressType;
		if (!ValueType<Http::FDownloadOptions>(S, "http::download_options", OptionsType, Error) ||
			!Type(S, "http::download_operation", TaskType, Error) ||
			!Type(S, "http::download_progress", ProgressType, Error))
		{
			return false;
		}
		if (!Member(
				S,
				Api,
				"make_download_options",
				T::HostObject,
				{T::String, T::String},
				[&S](const auto& C, auto& O, auto& E)
				{
					if (!CheckWork(C, String(C, 0).size() + String(C, 1).size(), E))
					{
						return false;
					}
					Http::FDownloadOptions V;
					V.File.Path = String(C, 0);
					V.File.BaseDirectory = String(C, 1);
					return Make(S, "http::download_options", std::move(V), O, E);
				},
				Error,
				"http::download_options",
				{},
				true) ||
			!Member(
				S,
				OptionsType,
				"set_file_options",
				T::Void,
				{T::HostObject},
				[](const auto& C, auto&, auto&)
				{
					This<Http::FDownloadOptions>(C).File = Object<Network::FFileWriteOptions>(C, 0);
					return true;
				},
				Error,
				"",
				{"Network::file_write_options"},
				false,
				true) ||
			!Member(
				S,
				OptionsType,
				"set_buffer_bytes",
				T::Void,
				{T::Int64},
				[](const auto& C, auto&, auto&)
				{
					This<Http::FDownloadOptions>(C).BufferBytes = Count(Integer(C, 0));
					return true;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				OptionsType,
				"set_resume",
				T::Void,
				{T::String, T::Int64, T::String, T::Bool},
				[](const auto& C, auto&, auto& E)
				{
					if (!CheckWork(C, String(C, 0).size() + String(C, 2).size(), E))
					{
						return false;
					}
					Http::FDownloadResume V;
					V.Offset = Count(Integer(C, 1));
					V.PartialPath = String(C, 0);
					V.Validator = String(C, 2);
					V.Kind = std::get<bool>(C.Args[3]) ? Http::EResumeValidator::LastModified
													   : Http::EResumeValidator::StrongETag;
					This<Http::FDownloadOptions>(C).Resume = std::move(V);
					return true;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				OptionsType,
				"clear_resume",
				T::Void,
				{},
				[](const auto& C, auto&, auto&)
				{
					This<Http::FDownloadOptions>(C).Resume.reset();
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
		if (!Member(
				S,
				Request,
				"set_upload_file",
				T::Void,
				{T::HostObject, T::HostObject},
				[](const auto& C, auto&, auto&)
				{
					auto Factory = Http::MakeFileUploadFactory(
						Object<FHandle<Network::FFileExecutor>>(C, 0).Value, Object<Network::FFileReadOptions>(C, 1));
					auto& R = This<Http::FRequest>(C);
					R.Body.clear();
					R.Upload.reset();
					R.UploadLength.reset();
					R.Multipart.reset();
					R.UploadFactory = std::move(Factory);
					R.Headers.Erase("Content-Length");
					R.Headers.Erase("Transfer-Encoding");
					return true;
				},
				Error,
				"",
				{"Network::file_executor", "Network::file_read_options"},
				false,
				true) ||
			!Member(
				S,
				Client,
				"download_file",
				T::HostObject,
				{T::HostObject, T::HostObject, T::HostObject},
				[&S](const auto& C, auto& O, auto& E)
				{
					auto& V = This<FHandle<Http::FClient>>(C);
					if (!Lease(S, V.Context, E))
					{
						return false;
					}
					return StartTask<Http::FDownloadOperation>(
						S,
						"http::download_operation",
						V.Context,
						[&]
						{
							return Http::DownloadFile(Context(V.Context),
								*V.Value,
								*Object<FHandle<Network::FFileExecutor>>(C, 0).Value,
								Object<Http::FRequest>(C, 1),
								Object<Http::FDownloadOptions>(C, 2));
						},
						O,
						E);
				},
				Error,
				"http::download_operation",
				{"Network::file_executor", "http::request", "http::download_options"},
				false,
				true) ||
			!Member(
				S,
				TaskType,
				"operation",
				T::HostObject,
				{},
				[&S](const auto& C, auto& O, auto& E)
				{
					const auto& V = This<FHandle<Http::FDownloadOperation>>(C);
					return Make(S, "http::operation", FOperationValue{V.Context, V.Value->Operation()}, O, E);
				},
				Error,
				"http::operation") ||
			!Member(
				S,
				TaskType,
				"progress",
				T::HostObject,
				{},
				[&S](const auto& C, auto& O, auto& E)
				{
					const auto& V = This<FHandle<Http::FDownloadOperation>>(C);
					return Make(
						S, "http::download_progress", FDownloadProgressValue{V.Context, V.Value->Progress()}, O, E);
				},
				Error,
				"http::download_progress") ||
			!Member(
				S,
				TaskType,
				"head_ready",
				T::Bool,
				{},
				[](const auto& C, auto& O, auto&)
				{
					O = static_cast<bool>(This<FHandle<Http::FDownloadOperation>>(C).Value->ResponseHead());
					return true;
				},
				Error))
		{
			return false;
		}
		for (const bool Final : {false, true})
		{
			if (!Member(
					S,
					TaskType,
					Final ? "response" : "head",
					T::HostObject,
					{},
					[&S, Final](const auto& C, auto& O, auto& E)
					{
						const auto& V = This<FHandle<Http::FDownloadOperation>>(C);
						std::shared_ptr<const Http::FResponse> Response;
						if (Final)
						{
							auto Result = NetworkBinding::Result<Http::FDownloadResult>(
								FOperationValue{V.Context, V.Value->Operation()});
							Response = std::shared_ptr<const Http::FResponse>(Result, &Result->Response);
						}
						else
						{
							Response = V.Value->ResponseHead();
						}
						if (!Response)
						{
							E = "Download response head is not ready";
							return false;
						}
						return Make(S, "http::response", FHttpResponseValue{std::move(Response)}, O, E);
					},
					Error,
					"http::response"))
			{
				return false;
			}
		}
		if (!Member(
				S,
				ProgressType,
				"file",
				T::HostObject,
				{},
				[](const auto& C, auto& O, auto& E)
				{
					const auto& V = This<FDownloadProgressValue>(C);
					const auto& Service = static_cast<FContextValue*>(V.Context->Payload)->Services;
					if (!Service)
					{
						E = "The Network extension does not provide its binding service";
						return false;
					}
					return Service->MakeFileProgress(V.Value.File, O, E);
				},
				Error,
				"Network::file_progress"))
		{
			return false;
		}
		for (const char* Name : {"received_bytes", "representation_bytes", "resumed", "already_complete"})
		{
			const std::string_view N = Name;
			if (!Member(
					S,
					ProgressType,
					Name,
					N == "resumed" || N == "already_complete" ? T::Bool : T::Int64,
					{},
					[N](const auto& C, auto& O, auto&)
					{
						const auto& V = This<FDownloadProgressValue>(C).Value;
						if (N == "resumed")
						{
							O = V.bResumed;
						}
						else if (N == "already_complete")
						{
							O = V.bAlreadyComplete;
						}
						else
						{
							const auto Value = N == "received_bytes" ? std::optional<std::uint64_t>(V.ReceivedBytes)
																	 : V.RepresentationBytes;
							if (Value && *Value > INT64_MAX)
							{
								throw std::range_error("Download count exceeds int64");
							}
							O = Value ? static_cast<std::int64_t>(*Value) : std::int64_t(-1);
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
} // namespace VeroScript::NetworkBinding

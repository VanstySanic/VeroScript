// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkBinding.h"
#include "NetworkTaskBinding.h"

namespace VeroScript::NetworkBinding
{
	static std::int64_t ScriptCount(std::uint64_t N)
	{
		if (N > INT64_MAX)
		{
			throw std::range_error("File count exceeds int64");
		}
		return static_cast<std::int64_t>(N);
	}
	bool RegisterNetworkFiles(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Executor, ReadOptions, WriteOptions, Task, Progress;
		if (!Type(S, "Network::file_executor", Executor, Error) || !Type(S, "Network::file_operation", Task, Error) ||
			!Type(S, "Network::file_progress", Progress, Error) ||
			!ValueType<Network::FFileReadOptions>(S, "Network::file_read_options", ReadOptions, Error) ||
			!ValueType<Network::FFileWriteOptions>(S, "Network::file_write_options", WriteOptions, Error))
		{
			return false;
		}
		for (const bool WithLimits : {false, true})
		{
			if (!Member(
					S,
					Api,
					WithLimits ? "create_file_executor_with_limits" : "create_file_executor",
					T::HostObject,
					WithLimits ? std::vector<T>{T::HostObject, T::Int64, T::Int64, T::Int64, T::Int64}
							   : std::vector<T>{T::HostObject},
					[&S, WithLimits](const auto& C, auto& O, auto& E)
					{
						auto Owner = ContextArg(C);
						if (!Lease(S, Owner, E))
						{
							return false;
						}
						Network::FFileExecutorOptions Options;
						if (WithLimits)
						{
							const auto Threads = Count(Integer(C, 1)), Pending = Count(Integer(C, 2));
							if (Threads > UINT32_MAX || Pending > UINT32_MAX)
							{
								throw std::range_error("File pool limits exceed uint32");
							}
							Options.WorkerThreads = static_cast<std::uint32_t>(Threads);
							Options.MaxPendingFiles = static_cast<std::uint32_t>(Pending);
							Options.BlockBytes = Count(Integer(C, 3));
							Options.PollInterval = std::chrono::milliseconds(Count(Integer(C, 4)));
						}
						return Make(S,
							"Network::file_executor",
							FHandle<Network::FFileExecutor>{
								Owner, std::make_shared<Network::FFileExecutor>(Context(Owner), Options)},
							O,
							E);
					},
					Error,
					"Network::file_executor",
					WithLimits ? std::vector<std::string>{"Network::context", "", "", "", ""}
							   : std::vector<std::string>{"Network::context"},
					true))
			{
				return false;
			}
		}
		for (const bool Read : {true, false})
		{
			const auto Id = Read ? ReadOptions : WriteOptions;
			if (!Member(
					S,
					Api,
					Read ? "make_file_read_options" : "make_file_write_options",
					T::HostObject,
					{T::String, T::String},
					[&S, Read](const auto& C, auto& O, auto& E)
					{
						if (!CheckWork(C, String(C, 0).size() + String(C, 1).size(), E))
						{
							return false;
						}
						if (Read)
						{
							Network::FFileReadOptions V;
							V.Path = String(C, 0);
							V.BaseDirectory = String(C, 1);
							return Make(S, "Network::file_read_options", std::move(V), O, E);
						}
						Network::FFileWriteOptions V;
						V.Path = String(C, 0);
						V.BaseDirectory = String(C, 1);
						return Make(S, "Network::file_write_options", std::move(V), O, E);
					},
					Error,
					Read ? "Network::file_read_options" : "Network::file_write_options",
					{},
					true) ||
				!Member(
					S,
					Id,
					"set_max_bytes",
					T::Void,
					{T::Int64},
					[Read](const auto& C, auto&, auto&)
					{
						if (Read)
						{
							This<Network::FFileReadOptions>(C).MaxBytes = Count(Integer(C, 0));
						}
						else
						{
							This<Network::FFileWriteOptions>(C).MaxBytes = Count(Integer(C, 0));
						}
						return true;
					},
					Error,
					"",
					{},
					false,
					true) ||
				!Member(
					S,
					Id,
					"set_timeout",
					T::Void,
					{T::Int64},
					[Read](const auto& C, auto&, auto&)
					{
						if (Read)
						{
							This<Network::FFileReadOptions>(C).Operation = Options(Integer(C, 0));
						}
						else
						{
							This<Network::FFileWriteOptions>(C).Operation = Options(Integer(C, 0));
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
		if (!Member(
				S,
				ReadOptions,
				"set_range",
				T::Void,
				{T::Int64, T::Int64},
				[](const auto& C, auto&, auto&)
				{
					const auto Length = Integer(C, 1);
					if (Length < -1)
					{
						throw std::invalid_argument("File read length must be -1 or nonnegative");
					}
					auto& V = This<Network::FFileReadOptions>(C);
					V.Offset = Count(Integer(C, 0));
					V.Length = Length == -1 ? std::nullopt : std::optional<std::uint64_t>(Length);
					return true;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				WriteOptions,
				"set_expected_length",
				T::Void,
				{T::Int64},
				[](const auto& C, auto&, auto&)
				{
					const auto Length = Integer(C, 0);
					if (Length < -1)
					{
						throw std::invalid_argument("File output length must be -1 or nonnegative");
					}
					This<Network::FFileWriteOptions>(C).ExpectedLength =
						Length == -1 ? std::nullopt : std::optional<std::uint64_t>(Length);
					return true;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				WriteOptions,
				"set_prefix",
				T::Void,
				{T::String, T::Int64},
				[](const auto& C, auto&, auto& E)
				{
					if (!CheckWork(C, String(C, 0).size(), E))
					{
						return false;
					}
					auto& V = This<Network::FFileWriteOptions>(C);
					const auto Length = Count(Integer(C, 1));
					V.PrefixPath = String(C, 0);
					V.PrefixLength = Length;
					return true;
				},
				Error,
				"",
				{},
				false,
				true) ||
			!Member(
				S,
				WriteOptions,
				"set_commit_options",
				T::Void,
				{T::Bool, T::Bool, T::Bool},
				[](const auto& C, auto&, auto&)
				{
					auto& V = This<Network::FFileWriteOptions>(C);
					V.bReplaceExisting = std::get<bool>(C.Args[0]);
					V.bKeepPartial = std::get<bool>(C.Args[1]);
					V.bFlushToDisk = std::get<bool>(C.Args[2]);
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
		for (const bool Read : {true, false})
		{
			if (!Member(
					S,
					Executor,
					Read ? "read" : "write",
					T::HostObject,
					Read ? std::vector<T>{T::HostObject} : std::vector<T>{T::HostObject, T::HostObject},
					[&S, Read](const auto& C, auto& O, auto& E)
					{
						auto& V = This<FHandle<Network::FFileExecutor>>(C);
						if (!Lease(S, V.Context, E))
						{
							return false;
						}
						return StartTask<Network::FFileOperation>(
							S,
							"Network::file_operation",
							V.Context,
							[&]
							{
								return Read ? V.Value->Read(Object<Network::FFileReadOptions>(C, 0))
											: V.Value->Write(Object<Network::FFileWriteOptions>(C, 0),
												  Object<FReadStreamValue>(C, 1).Value);
							},
							O,
							E);
					},
					Error,
					"Network::file_operation",
					Read ? std::vector<std::string>{"Network::file_read_options"}
						 : std::vector<std::string>{"Network::file_write_options", "Network::readable_stream"},
					false,
					true))
			{
				return false;
			}
		}
		if (!Member(
				S,
				Executor,
				"close",
				T::Void,
				{},
				[](const auto& C, auto&, auto&)
				{
					This<FHandle<Network::FFileExecutor>>(C).Value->Close();
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
		for (const unsigned Which : {0u, 1u, 2u})
		{
			if (!Member(
					S,
					Executor,
					Which == 0	 ? "pending_files"
					: Which == 1 ? "active_workers"
								 : "worker_threads",
					T::Int64,
					{},
					[Which](const auto& C, auto& O, auto&)
					{
						auto V = This<FHandle<Network::FFileExecutor>>(C).Value->Statistics();
						O = static_cast<std::int64_t>(Which == 0   ? V.PendingFiles
													  : Which == 1 ? V.ActiveWorkers
																   : V.WorkerThreads);
						return true;
					},
					Error))
			{
				return false;
			}
		}
		if (!Member(
				S,
				Task,
				"operation",
				T::HostObject,
				{},
				[&S](const auto& C, auto& O, auto& E)
				{
					const auto& V = This<FHandle<Network::FFileOperation>>(C);
					return Make(S, "Network::operation", FOperationValue{V.Context, V.Value->Operation()}, O, E);
				},
				Error,
				"Network::operation") ||
			!Member(
				S,
				Task,
				"body",
				T::HostObject,
				{},
				[&S](const auto& C, auto& O, auto& E)
				{
					const auto& V = This<FHandle<Network::FFileOperation>>(C);
					auto Reader = V.Value->Body();
					if (!Reader)
					{
						E = "File operation has no readable body";
						return false;
					}
					return Make(S, "Network::readable_stream", FReadStreamValue{V.Context, std::move(Reader), {}}, O, E);
				},
				Error,
				"Network::readable_stream") ||
			!Member(
				S,
				Task,
				"progress",
				T::HostObject,
				{},
				[&S](const auto& C, auto& O, auto& E)
				{
					return Make(S, "Network::file_progress", This<FHandle<Network::FFileOperation>>(C).Value->Progress(), O, E);
				},
				Error,
				"Network::file_progress"))
		{
			return false;
		}
		for (const char* Name : {"bytes",
				 "total_bytes",
				 "commit_state",
				 "physical_complete",
				 "cleanup_system_code",
				 "path",
				 "partial_path"})
		{
			const std::string_view N = Name;
			if (!Member(
					S,
					Progress,
					Name,
					N == "path" || N == "partial_path" ? T::String
					: N == "physical_complete"		   ? T::Bool
													   : T::Int64,
					{},
					[N](const auto& C, auto& O, auto& E)
					{
						const auto& V = This<Network::FFileProgress>(C);
						if (N == "path" || N == "partial_path")
						{
							const auto& P = N == "path" ? V.Path : V.PartialPath;
							if (!CheckWork(C, P.size(), E))
							{
								return false;
							}
							O = P;
						}
						else if (N == "physical_complete")
						{
							O = V.bPhysicalComplete;
						}
						else if (N == "bytes")
						{
							O = ScriptCount(V.Bytes);
						}
						else if (N == "total_bytes")
						{
							O = V.TotalBytes ? ScriptCount(*V.TotalBytes) : std::int64_t(-1);
						}
						else if (N == "commit_state")
						{
							O = static_cast<std::int64_t>(V.Commit);
						}
						else
						{
							O = static_cast<std::int64_t>(V.CleanupSystemCode);
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

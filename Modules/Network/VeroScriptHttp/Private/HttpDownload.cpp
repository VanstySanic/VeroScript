// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpFiles.h"
#include "NetworkProvider.h"
#include "NetworkStream.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace VeroScript::Http
{
	namespace N = Network::Detail;
	namespace
	{
		bool Number(std::string_view S, std::uint64_t& Out)
		{
			if (S.empty())
			{
				return false;
			}
			const auto R = std::from_chars(S.data(), S.data() + S.size(), Out);
			return R.ec == std::errc{} && R.ptr == S.data() + S.size();
		}
		bool StrongETag(std::string_view S)
		{
			return S.size() >= 2 && S.size() <= 8192 && S.front() == '"' && S.back() == '"' &&
				   std::all_of(S.begin() + 1,
					   S.end() - 1,
					   [](unsigned char C)
					   {
						   return C == 0x21 || (C >= 0x23 && C <= 0x7e) || C >= 0x80;
					   });
		}
		bool HttpDate(std::string_view S)
		{
			if (S.size() != 29 || S.substr(3, 2) != ", " || S[7] != ' ' || S[11] != ' ' || S[16] != ' ' ||
				S[19] != ':' || S[22] != ':' || S.substr(25) != " GMT")
			{
				return false;
			}
			const auto Week = std::string_view("MonTueWedThuFriSatSun").find(S.substr(0, 3));
			const auto Month = std::string_view("JanFebMarAprMayJunJulAugSepOctNovDec").find(S.substr(8, 3));
			std::uint64_t Day, Year, Hour, Minute, Second;
			if (Week == S.npos || Week % 3 || Month == S.npos || Month % 3 || !Number(S.substr(5, 2), Day) ||
				!Number(S.substr(12, 4), Year) || !Number(S.substr(17, 2), Hour) || !Number(S.substr(20, 2), Minute) ||
				!Number(S.substr(23, 2), Second) || Year < 1601 || Hour > 23 || Minute > 59 || Second > 60)
			{
				return false;
			}
			constexpr unsigned Days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
			const auto Leap = Month == 3 && Year % 4 == 0 && (Year % 100 != 0 || Year % 400 == 0);
			return Day > 0 && Day <= Days[Month / 3] + Leap;
		}
		bool Identity(const FHeaders& Headers)
		{
			const auto Values = Headers.GetAll("Content-Encoding");
			if (Values.empty())
			{
				return true;
			}
			constexpr std::string_view Expected = "identity";
			return Values.size() == 1 && Values[0].size() == Expected.size() &&
				   std::equal(Values[0].begin(),
					   Values[0].end(),
					   Expected.begin(),
					   [](unsigned char A, char B)
					   {
						   return (A >= 'A' && A <= 'Z' ? A + 32 : A) == B;
					   });
		}
		struct FRange
		{
			std::uint64_t Start = 0, End = 0, Total = 0;
			bool bUnsatisfied = false;
		};
		bool Range(std::string_view Text, FRange& Out)
		{
			constexpr std::string_view Prefix = "bytes ";
			if (Text.size() < Prefix.size() || !std::equal(Prefix.begin(),
												   Prefix.end(),
												   Text.begin(),
												   [](char A, unsigned char B)
												   {
													   return A == (B >= 'A' && B <= 'Z' ? B + 32 : B);
												   }))
			{
				return false;
			}
			Text.remove_prefix(Prefix.size());
			const auto Slash = Text.find('/');
			if (Slash == Text.npos || !Number(Text.substr(Slash + 1), Out.Total))
			{
				return false;
			}
			const auto Span = Text.substr(0, Slash);
			if (Span == "*")
			{
				Out.bUnsatisfied = true;
				return true;
			}
			const auto Dash = Span.find('-');
			return Dash != Span.npos && Number(Span.substr(0, Dash), Out.Start) &&
				   Number(Span.substr(Dash + 1), Out.End) && Out.Start <= Out.End && Out.End < Out.Total;
		}
	} // namespace
	namespace Detail
	{
		struct FDownloadInputs
		{
			explicit FDownloadInputs(std::shared_ptr<N::FContext> C) : Context(std::move(C))
			{
			}
			void Cancel(const Network::FOperation& O) noexcept
			{
				try
				{
					O.Cancel();
				}
				catch (const std::bad_alloc&)
				{
					N::Fault(Context, Network::EError::Allocation);
				}
				catch (...)
				{
					N::Fault(Context, Network::EError::Internal);
				}
			}
			void SetFile(Network::FOperation O)
			{
				bool Abort;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					Abort = Closed;
					if (!Closed)
					{
						File = O;
					}
				}
				if (Abort)
				{
					Cancel(O);
				}
			}
			void SetHttp(Network::FOperation O, std::shared_ptr<Network::FReadableStream> R)
			{
				bool Abort;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					Abort = Closed;
					if (!Closed)
					{
						Http = O;
						Reader = R;
					}
				}
				if (Abort)
				{
					Cancel(O);
					if (R)
					{
						R->Cancel();
					}
				}
			}
			void SetWriter(std::shared_ptr<Network::FWritableStream> W)
			{
				bool Abort;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					Abort = Closed;
					if (!Closed)
					{
						Writer = W;
					}
				}
				if (Abort)
				{
					W->Fail({Network::EError::Cancelled, {}, 0});
				}
			}
			void Finish(bool Success, const Network::FError& E) noexcept
			{
				Network::FOperation H, F;
				std::shared_ptr<Network::FReadableStream> R;
				std::shared_ptr<Network::FWritableStream> W;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					if (Closed)
					{
						return;
					}
					Closed = true;
					H = std::move(Http);
					F = std::move(File);
					R = std::move(Reader);
					W = std::move(Writer);
				}
				if (!Success)
				{
					Cancel(H);
					Cancel(F);
					if (R)
					{
						R->Cancel();
					}
					if (W)
					{
						W->Fail({E ? E.Code : Network::EError::Cancelled, {}, E.SystemCode});
					}
				}
			}
			std::shared_ptr<N::FContext> Context;
			std::mutex Mutex;
			Network::FOperation File, Http;
			std::shared_ptr<Network::FReadableStream> Reader;
			std::shared_ptr<Network::FWritableStream> Writer;
			bool Closed = false;
		};
		struct FDownloadState : std::enable_shared_from_this<FDownloadState>
		{
			explicit FDownloadState(std::shared_ptr<N::FContext> C) : Context(std::move(C)), Timer(N::Runtime(Context)->CreateTimer())
			{
			}
			~FDownloadState()
			{
				N::RemoveResource(Context, ResourceId);
			}
			bool Validate(const FResponse& H)
			{
				if (!Identity(H.Headers))
				{
					return Reject("File downloads require an identity representation");
				}
				const auto Lengths = H.Headers.GetAll("Content-Length");
				std::optional<std::uint64_t> Length;
				if (!Lengths.empty())
				{
					std::uint64_t Value;
					if (Lengths.size() != 1 || !Number(Lengths[0], Value))
					{
						return Reject("Invalid download Content-Length");
					}
					Length = Value;
				}
				const auto Ranges = H.Headers.GetAll("Content-Range");
				std::optional<std::uint64_t> Total;
				if (!Options.Resume)
				{
					if (H.Status != 200 || !Ranges.empty())
					{
						return Reject("A full download requires HTTP 200 without Content-Range");
					}
					ExpectedBody = Length;
					Total = Length;
				}
				else
				{
					const auto& Resume = *Options.Resume;
					const auto Validators =
						H.Headers.GetAll(Resume.Kind == EResumeValidator::StrongETag ? "ETag" : "Last-Modified");
					FRange R;
					if (Validators.size() != 1 || Validators[0] != Resume.Validator || Ranges.size() != 1 ||
						!Range(Ranges[0], R))
					{
						return Reject("Resume representation validator or range does not match");
					}
					if (H.Status == 206 && !R.bUnsatisfied && R.Start == Resume.Offset && R.End + 1 == R.Total)
					{
						ExpectedBody = R.Total - R.Start;
						if (Length && Length != ExpectedBody)
						{
							return Reject("Resume length does not match Content-Range");
						}
						Resumed = true;
					}
					else if (H.Status == 416 && R.bUnsatisfied && R.Total == Resume.Offset)
					{
						// An error entity is drained, not appended to the already complete representation.
						AlreadyComplete = true;
						Resumed = true;
					}
					else
					{
						return Reject(
							"Resume requires a matching 206 range or confirmed-complete 416; HTTP 200 is rejected");
					}
					Total = R.Total;
				}
				if (Total && *Total > Options.File.MaxBytes)
				{
					N::Fail(Op, Network::EError::Limit, "Download representation exceeds its file limit");
					return false;
				}
				if (Total && Options.File.ExpectedLength && *Total != *Options.File.ExpectedLength)
				{
					return Reject("Download representation length differs from its file contract");
				}
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					RepresentationBytes = Total;
				}
				return true;
			}
			bool Reject(const char* Text)
			{
				N::Fail(Op, Network::EError::Protocol, Text);
				return false;
			}
			void Fail(const Network::FError& E)
			{
				N::Fail(Op, E ? E.Code : Network::EError::Internal, E.Message, E.SystemCode);
			}
			void Stop() noexcept
			{
				if (Stopped.exchange(true))
				{
					return;
				}
				try
				{
					Timer->Cancel();
				}
				catch (...)
				{
				}
				if (Inputs)
				{
					Inputs->Finish(false, {Network::EError::Closed, {}, 0});
				}
				N::RemoveResource(Context, ResourceId);
			}
			void Wake() noexcept
			{
				if (Stopped || WakePosted.exchange(true))
				{
					return;
				}
				try
				{
					N::Post(Context,
						[Weak = weak_from_this()]
						{
							if (auto S = Weak.lock())
							{
								S->WakePosted = false;
								S->Tick();
							}
						});
				}
				catch (...)
				{
					WakePosted = false;
				} // An active download always retains its fallback timer.
			}
			void Arm()
			{
				if (Stopped || TimerPending)
				{
					return;
				}
				Timer->ExpiresAt(std::chrono::steady_clock::now() + std::chrono::milliseconds(2));
				Timer->AsyncWait(
					[Self = shared_from_this()](std::error_code E)
					{
						Self->TimerPending = false;
						if (!E)
						{
							Self->Tick();
						}
					});
				TimerPending = true;
			}
			void Pump()
			{
				if (N::Terminal(N::State(Op)))
				{
					if (File.Progress().bPhysicalComplete && Stream.Operation().IsComplete())
					{
						Stop();
					}
					return;
				}
				if (File.Operation().IsComplete() && File.Operation().Error())
				{
					Fail(File.Operation().Error());
					return;
				}
				if (!HaveHead)
				{
					const bool Complete = Stream.Operation().IsComplete();
					FResponse H;
					Network::FError E;
					if (!Stream.TryHead(H, E))
					{
						if (E)
						{
							Fail(E);
						}
						else if (Complete)
						{
							Reject("Download ended without a response head");
						}
						return;
					}
					std::uint64_t Bytes = sizeof(FResponse) + H.EffectiveUrl.capacity();
					for (const auto& Pair : H.Headers.Fields())
					{
						Bytes += sizeof(Pair) + Pair.first.capacity() + Pair.second.capacity();
					}
					auto Lease = N::ReserveBytes(Context, Bytes, Op);
					if (!Lease)
					{
						return;
					}
					auto Owned = N::OwnResult(Context, H, std::move(Lease));
					const auto* Value = static_cast<const FResponse*>(Owned->Data);
					{
						std::lock_guard<std::mutex> Lock(Mutex);
						Head = std::shared_ptr<const FResponse>(std::move(Owned), Value);
					}
					HaveHead = true;
					if (!Validate(H))
					{
						return;
					}
				}
				if (!Eof)
				{
					for (unsigned I = 0; I < 8; ++I)
					{
						if (Pending)
						{
							auto W = Writer->WriteAll(Buffer.data(), static_cast<std::int64_t>(Pending));
							if (W.State == Network::EStreamState::WouldBlock)
							{
								return;
							}
							if (W.State != Network::EStreamState::Progress || W.Bytes != Pending)
							{
								Fail(W.Error);
								return;
							}
							Pending = 0;
						}
						auto R = Stream.Body()->Read(Buffer.data(), static_cast<std::int64_t>(Buffer.size()));
						if (R.State == Network::EStreamState::WouldBlock)
						{
							return;
						}
						if (R.State == Network::EStreamState::Failed)
						{
							Fail(R.Error);
							return;
						}
						if (R.State == Network::EStreamState::End)
						{
							if (!AlreadyComplete && ExpectedBody && Received != *ExpectedBody)
							{
								Reject("Download range is truncated");
								return;
							}
							if (!Stream.Operation().IsComplete())
							{
								return;
							}
							if (Stream.Operation().Error())
							{
								Fail(Stream.Operation().Error());
								return;
							}
							if (!PrepareResult())
							{
								return;
							}
							Eof = true;
							Writer->Finish();
							break;
						}
						if (R.State != Network::EStreamState::Progress || !R.Bytes || R.Bytes > Buffer.size())
						{
							Reject("Invalid download stream progress");
							return;
						}
						if (!AlreadyComplete)
						{
							if (ExpectedBody && R.Bytes > *ExpectedBody - std::min(Received.load(), *ExpectedBody))
							{
								Reject("Download exceeds Content-Range");
								return;
							}
							Pending = static_cast<std::size_t>(R.Bytes);
						}
						Received.fetch_add(R.Bytes);
					}
				}
				if (Eof && File.Operation().IsComplete())
				{
					if (File.Operation().Error())
					{
						Fail(File.Operation().Error());
						return;
					}
					auto Result = File.Operation().Result<Network::FFileResult>();
					if (!Success || !Result)
					{
						Reject("Download result is unavailable");
						return;
					}
					auto& Value = *static_cast<FDownloadResult*>(Success->Data);
					Value.File.Bytes = Result->Bytes;
					Value.File.bCommitted = Result->bCommitted;
					N::Finish(Context, Op,
						Network::EOperationState::Succeeded,
						{},
						Success);
					Stop();
				}
			}
			bool PrepareResult()
			{
				auto Response = Stream.Operation().Result<FResponse>();
				if (!Response)
				{
					return Reject("Download response is unavailable");
				}
				FDownloadResult Value{*Response, {File.Progress().Path, 0, false}, Resumed, AlreadyComplete};
				const auto Limit = N::Options(Context).MaxRetainedBytes;
				std::uint64_t Bytes = sizeof(N::FOwnedResult) + sizeof(Value) + 256;
				auto Add = [&](std::uint64_t Amount)
				{
					if (Amount > Limit - std::min(Bytes, Limit))
					{
						return false;
					}
					Bytes += Amount;
					return true;
				};
				auto Headers = [&](const FHeaders& H)
				{
					if (!Add(H.Fields().capacity() * sizeof(H.Fields()[0])))
					{
						return false;
					}
					for (const auto& P : H.Fields())
					{
						if (!Add(P.first.capacity() + 1) || !Add(P.second.capacity() + 1))
						{
							return false;
						}
					}
					return true;
				};
				bool Fits = Bytes <= Limit && Add(Value.File.Path.capacity() + 1) &&
							Add(Value.Response.EffectiveUrl.capacity() + 1) && Add(Value.Response.Body.capacity()) &&
							Headers(Value.Response.Headers) && Headers(Value.Response.Trailers) &&
							Add(Value.Response.Attempts.capacity() * sizeof(FAttempt));
				for (const auto& A : Value.Response.Attempts)
				{
					Fits = Fits && Add(A.EffectiveUrl.capacity() + 1);
				}
				if (!Fits || !ResultMemory->TryResize(Bytes))
				{
					N::Fail(Op, Network::EError::Limit, "Download result metadata exceeds its budget");
					return false;
				}
				// Finish the input only after all retained result allocations succeed. EOF permits the file commit.
				Success = N::CreateTypedResult(Context, ResultMemory, std::move(Value));
				return true;
			}
			void Tick() noexcept
			{
				if (Stopped)
				{
					return;
				}
				try
				{
					Pump();
					Arm();
				}
				catch (const std::bad_alloc&)
				{
					N::Fault(Context, Network::EError::Allocation);
				}
				catch (...)
				{
					N::Fault(Context, Network::EError::Internal);
				}
			}
			std::shared_ptr<N::FContext> Context;
			std::shared_ptr<N::FOperation> Op;
			std::shared_ptr<FDownloadInputs> Inputs;
			std::shared_ptr<N::FByteLease> Memory, ResultMemory;
			std::shared_ptr<N::FOwnedResult> Success;
			FStreamingOperation Stream;
			Network::FFileOperation File;
			std::shared_ptr<Network::FWritableStream> Writer;
			FDownloadOptions Options;
			std::shared_ptr<Network::Backend::ITimer> Timer;
			std::vector<std::uint8_t> Buffer;
			std::mutex Mutex;
			std::shared_ptr<const FResponse> Head;
			std::optional<std::uint64_t> RepresentationBytes, ExpectedBody;
			std::atomic<std::uint64_t> Received{0};
			std::atomic<bool> Stopped{false}, WakePosted{false}, Resumed{false}, AlreadyComplete{false};
			std::uint64_t ResourceId = 0;
			std::size_t Pending = 0;
			bool HaveHead = false, Eof = false, TimerPending = false;
		};
	} // namespace Detail
	FDownloadOperation DownloadFile(
		Network::FContext& Context, FClient& Client, Network::FFileExecutor& Files, FRequest Request, FDownloadOptions Options)
	{
		FDownloadOperation Result;
		auto C = N::FAccess::Context(Context);
		auto Op = N::Begin(C, Request.Operation);
		Result.Op = N::FAccess::Wrap(Op);
		if (N::Terminal(N::State(Op)))
		{
			return Result;
		}
		struct FSetupGuard
		{
			FDownloadOperation& Result;
			bool Committed = false;
			~FSetupGuard()
			{
				if (!Committed && Result.State)
				{
					Result.State->Stop();
				}
			}
		} Guard{Result};
		try
		{
			if (!Client.BelongsTo(Context) || !Files.BelongsTo(Context) || Request.Method != "GET" ||
				!Request.Body.empty() || Request.Upload || Request.UploadFactory || Request.UploadLength ||
				Request.Multipart || !Options.File.PrefixPath.empty() || Options.File.PrefixLength ||
				Options.BufferBytes < 16384 || Options.BufferBytes > N::Options(C).MaxBufferBytes ||
				!Request.Headers.GetAll("Range").empty() || !Request.Headers.GetAll("If-Range").empty() ||
				!Request.Retry.MaxAttempts || Request.Retry.MaxAttempts > 32 ||
				Request.MaxHeaderBytes > N::Options(C).MaxBufferBytes || Options.File.Path.size() > 1024 * 1024 ||
				Options.File.BaseDirectory.size() > 1024 * 1024 ||
				(Options.Resume && Options.Resume->PartialPath.size() > 1024 * 1024))
			{
				N::Fail(Op, Network::EError::InvalidArgument, "Invalid file download contexts, request or buffers");
				return Result;
			}
			Network::FError E;
			Request.bDecodeContent = false;
			if (!Request.Headers.TrySet("Accept-Encoding", "identity", E))
			{
				N::Fail(Op, E.Code, E.Message);
				return Result;
			}
			if (Options.Resume)
			{
				const auto& R = *Options.Resume;
				if (R.PartialPath.empty() ||
					(R.Kind != EResumeValidator::StrongETag && R.Kind != EResumeValidator::LastModified) ||
					!(R.Kind == EResumeValidator::StrongETag ? StrongETag(R.Validator) : HttpDate(R.Validator)))
				{
					N::Fail(Op,
						Network::EError::InvalidArgument,
						"Resume requires a partial file and strong ETag or HTTP Last-Modified date");
					return Result;
				}
				Options.File.PrefixPath = R.PartialPath;
				Options.File.PrefixLength = R.Offset;
				if (!Request.Headers.TrySet("Range", "bytes=" + std::to_string(R.Offset) + "-", E) ||
					!Request.Headers.TrySet("If-Range", R.Validator, E))
				{
					N::Fail(Op, E.Code, E.Message);
					return Result;
				}
			}
			auto S = std::make_shared<Detail::FDownloadState>(C);
			if (!S->Timer)
			{
				N::Fail(Op, Network::EError::Unsupported, "HTTP download backend did not provide a timer");
				return Result;
			}
			Result.State = S;
			S->Op = Op;
			S->Inputs = std::make_shared<Detail::FDownloadInputs>(C);
			N::ObserveTerminal(Op,
				[Inputs = S->Inputs](auto State, const auto& E)
				{
					Inputs->Finish(State == Network::EOperationState::Succeeded, E);
				});
			if (N::Terminal(N::State(Op)))
			{
				return Result;
			}
			S->Options = Options;
			const auto Block = std::min<std::uint64_t>(65536, Options.BufferBytes);
			const auto Limit = N::Options(C).MaxRetainedBytes;
			std::uint64_t Memory = sizeof(Detail::FDownloadState) + sizeof(Detail::FDownloadInputs) + Block;
			auto Add = [&](std::uint64_t Amount)
			{
				if (Amount > Limit - std::min(Memory, Limit))
				{
					return false;
				}
				Memory += Amount;
				return true;
			};
			if (Memory > Limit || !Add(Options.File.Path.capacity()) || !Add(Options.File.BaseDirectory.capacity()) ||
				!Add(Options.File.PrefixPath.capacity()) ||
				(Options.Resume &&
					(!Add(Options.Resume->PartialPath.capacity()) || !Add(Options.Resume->Validator.capacity()))) ||
				Request.MaxHeaderBytes > Limit / 8 || sizeof(FAttempt) + 64 > Limit ||
				Request.Url.size() > Limit - (sizeof(FAttempt) + 64))
			{
				N::Fail(Op, Network::EError::Limit, "Download metadata exceeds its budget");
				return Result;
			}
			auto ResultBytes = Request.MaxHeaderBytes * 8;
			const auto AttemptBytes = sizeof(FAttempt) + Request.Url.size() + 64;
			if (Request.Retry.MaxAttempts > (Limit - ResultBytes) / AttemptBytes)
			{
				N::Fail(Op, Network::EError::Limit, "Download result budget exceeded");
				return Result;
			}
			ResultBytes += Request.Retry.MaxAttempts * AttemptBytes;
			S->Memory = N::ReserveBytes(C, Memory, Op);
			S->ResultMemory = N::ReserveBytes(C, ResultBytes, Op);
			if (!S->Memory || !S->ResultMemory)
			{
				return Result;
			}
			S->Buffer.resize(static_cast<std::size_t>(Block));
			Network::FBytePipe Pipe;
			if (!Network::TryCreateBytePipe(Context, Options.BufferBytes, Pipe, E))
			{
				N::Fail(Op, E.Code, E.Message);
				return Result;
			}
			S->Writer = Pipe.Writer;
			S->Inputs->SetWriter(S->Writer);
			Options.File.Operation.Timeout = std::nullopt;
			S->File = Files.Write(std::move(Options.File), Pipe.Reader);
			S->Inputs->SetFile(S->File.Operation());
			S->Stream = Client.SendStream(std::move(Request), Options.BufferBytes);
			S->Inputs->SetHttp(S->Stream.Operation(), S->Stream.Body());
			S->ResourceId = N::AddResource(C,
				[Weak = std::weak_ptr<Detail::FDownloadState>(S)]
				{
					if (auto P = Weak.lock())
					{
						P->Stop();
					}
				});
			if (!S->ResourceId)
			{
				N::Fail(Op, Network::EError::Limit, "Download resource limit exceeded");
			}
			if (auto Reader = S->Stream.Body())
			{
				N::FStreamAccess::OnReadable(*Reader,
					[Weak = std::weak_ptr<Detail::FDownloadState>(S)]
					{
						if (auto P = Weak.lock())
						{
							P->Wake();
						}
					});
			}
			N::FStreamAccess::OnWritable(*S->Writer,
				[Weak = std::weak_ptr<Detail::FDownloadState>(S)]
				{
					if (auto P = Weak.lock())
					{
						P->Wake();
					}
				});
			N::Post(C,
				[S]
				{
					S->Tick();
				});
			Guard.Committed = true;
		}
		catch (const std::bad_alloc&)
		{
			N::Fail(Op, Network::EError::Allocation, "Download allocation failed");
		}
		catch (const std::length_error&)
		{
			N::Fail(Op, Network::EError::Limit, "Download metadata exceeds its limits");
		}
		return Result;
	}
	FUploadFactory MakeFileUploadFactory(std::shared_ptr<Network::FFileExecutor> Files, Network::FFileReadOptions Options)
	{
		if (!Files)
		{
			throw std::invalid_argument("File upload requires an executor");
		}
		Options.Operation.Timeout = std::nullopt;
		return [Files = std::move(Files), Options = std::move(Options)](std::uint32_t, Network::FError& E)
		{
			auto Source = Files->Read(Options);
			if (Source.Operation().IsComplete() && Source.Operation().Error())
			{
				E = Source.Operation().Error();
				return FBodySource{};
			}
			E = {};
			return FBodySource{Source.Body(), Options.Length && *Options.Length ? Options.Length : std::nullopt};
		};
	}
	const Network::FOperation& FDownloadOperation::Operation() const noexcept
	{
		return Op;
	}
	FDownloadProgress FDownloadOperation::Progress() const
	{
		FDownloadProgress R;
		if (!State)
		{
			R.File.bPhysicalComplete = Op.IsComplete();
			return R;
		}
		R.File = State->File.Progress();
		R.ReceivedBytes = State->Received;
		R.bResumed = State->Resumed;
		R.bAlreadyComplete = State->AlreadyComplete;
		std::lock_guard<std::mutex> Lock(State->Mutex);
		R.RepresentationBytes = State->RepresentationBytes;
		return R;
	}
	std::shared_ptr<const FResponse> FDownloadOperation::ResponseHead() const
	{
		if (!State)
		{
			return {};
		}
		std::lock_guard<std::mutex> Lock(State->Mutex);
		return State->Head;
	}
} // namespace VeroScript::Http

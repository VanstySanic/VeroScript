// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpBackend.h"
#include "NetworkProvider.h"
#include "NetworkStream.h"
#include "HttpClientError.h"
#include "HttpClientHistory.h"
#include <curl/curl.h>
#include <algorithm>
#include <climits>
#include <map>
#include <limits>
#include <stdexcept>
#include <charconv>
#include <ctime>
#include <cstring>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace VeroScript::Http
{
	namespace N = Network::Detail;
	namespace Detail
	{

	} // namespace Detail

	template <class T>
	static bool FitsCurlLong(T Value)
	{
		if constexpr (std::numeric_limits<T>::digits <= std::numeric_limits<long>::digits)
		{
			return true;
		}
		else
		{
			return Value <= static_cast<T>(LONG_MAX);
		}
	}

	static std::shared_ptr<N::FByteLease> ReserveRequest(
	    const std::shared_ptr<N::FContext>& Context, const FRequest& Request, const std::shared_ptr<N::FOperation>& Op)
	{
		auto Lease = N::ReserveBytes(
		    Context, static_cast<std::uint64_t>(Request.Retry.Statuses.capacity()) * sizeof(std::uint32_t), Op);
		if (!Lease)
		{
			return {};
		}
		auto Add = [&](std::uint64_t Bytes)
		{
			if (Bytes > N::Options(Context).MaxRetainedBytes - std::min(Lease->Size(), N::Options(Context).MaxRetainedBytes) ||
			    !Lease->TryResize(Lease->Size() + Bytes))
			{
				N::Fail(Op, Network::EError::Limit, "Context payload byte limit exceeded");
				return false;
			}
			return true;
		};
		for (const auto Bytes : {Request.Url.capacity(),
		         Request.Method.capacity(),
		         Request.Proxy.capacity(),
		         Request.CaFile.capacity(),
		         Request.Body.capacity()})
		{
			if (!Add(Bytes))
			{
				return {};
			}
		}
		for (const auto& Field : Request.Headers.Fields())
		{
			// Both the request snapshot and curl's owned slist retain these bytes.
			if (!Add(Field.first.capacity()) || !Add(Field.second.capacity()) || !Add(Field.first.size()) ||
			    !Add(Field.second.size()))
			{
				return {};
			}
		}
		if (Request.Multipart)
		{
			for (const auto& Part : *Request.Multipart)
			{
				// curl_mime_data owns a copy in addition to the request snapshot.
				if (!Add(Part.Body.capacity()) || !Add(Part.Body.size()) || !Add(Part.Name.capacity()) ||
				    !Add(Part.ContentType.capacity()) || (Part.Filename && !Add(Part.Filename->capacity())))
				{
					return {};
				}
			}
		}
		return Lease;
	}

	struct FCurlHttpClient : Backend::IClient, std::enable_shared_from_this<FCurlHttpClient>
	{
		struct FFlow
		{
			FFlow() : Headers(0), Body(0), Upload(0)
			{
			}
			std::uint64_t Headers, Body, Upload;
		};
		struct FTransfer;
		struct FMimeUpload
		{
			FTransfer* Transfer = nullptr;
			std::size_t Index = 0;
			std::uint64_t ReadBytes = 0, Admission = 0;
		};
		struct FInputClaims
		{
			std::vector<std::shared_ptr<Network::FReadableStream>> Readers;
			bool bCommitted = false;
			~FInputClaims()
			{
				Rollback();
			}
			void Rollback() noexcept
			{
				if (!bCommitted)
				{
					for (const auto& Reader : Readers)
					{
						Reader->Cancel();
					}
					bCommitted = true;
				}
			}
		};
		// One terminal observer per transfer. Its source slots are replaced on native rewinds.
		// Cancellation can run on a caller thread and never reads the mutable request record.
		struct FInputSources
		{
			explicit FInputSources(std::size_t Count) : Readers(Count)
			{
			}
			void Set(std::size_t Index, const std::shared_ptr<Network::FReadableStream>& Source)
			{
				bool Cancelled;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					Cancelled = bClosed;
					if (!Cancelled)
					{
						Readers[Index] = Source;
					}
				}
				if (Cancelled)
				{
					Source->Cancel();
				}
			}
			void Cancel() noexcept
			{
				std::vector<std::weak_ptr<Network::FReadableStream>> Previous;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					if (bClosed)
					{
						return;
					}
					bClosed = true;
					Previous.swap(Readers);
				}
				for (const auto& Weak : Previous)
				{
					if (auto Reader = Weak.lock())
					{
						Reader->Cancel();
					}
				}
			}
			std::mutex Mutex;
			std::vector<std::weak_ptr<Network::FReadableStream>> Readers;
			bool bClosed = false;
		};
		struct FTransfer
		{
			~FTransfer()
			{
				Detail::FAttemptHistoryAccess::End(Request.History, Operation, Attempt);
				if (Request.Upload)
				{
					Request.Upload->Cancel();
				}
				if (Request.Multipart)
				{
					for (const auto& Part : *Request.Multipart)
					{
						if (Part.Upload)
						{
							Part.Upload->Cancel();
						}
					}
				}
				if (Output)
				{
					if (N::State(Operation) == Network::EOperationState::Succeeded)
					{
						Output->Finish();
					}
					else
					{
						Network::FError Error;
						try
						{
							Error = N::FAccess::Wrap(Operation).Error();
						}
						catch (...)
						{
							Error.Code = Network::EError::Allocation;
						}
						if (!Error)
						{
							Error.Code = Network::EError::Closed;
						}
						Output->Fail(std::move(Error));
					}
				}
				if (Easy)
				{
					curl_easy_cleanup(Easy);
				}
				curl_slist_free_all(HeaderList);
				curl_mime_free(Mime);
			}
			CURL* Easy = nullptr;
			bool bInMulti = false;
			bool bRecorded = false;
			std::uint32_t Attempt = 1;
			FFlow Total;
			std::uint64_t AttemptBodyBytes = 0;
			std::uint32_t Redirects = 0;
			std::vector<FAttempt> Previous;
			std::shared_ptr<N::FByteLease> HistoryMemory;
			std::shared_ptr<N::FByteLease> TraceMemory;
			FAttemptDetails Trace;
			std::size_t NextTraceRedirect = 0;
			std::uint64_t FollowedTraceRedirects = 0;
			std::shared_ptr<Network::Backend::ITimer> RetryTimer;
			FCurlHttpClient* Client = nullptr;
			curl_slist* HeaderList = nullptr;
			curl_mime* Mime = nullptr;
			std::vector<std::unique_ptr<FMimeUpload>> MimeUploads;
			std::uint64_t MimeBodyBytes = 0, MimeAdmission = 0;
			std::shared_ptr<N::FOperation> Operation;
			std::shared_ptr<FInputSources> Inputs;
			std::shared_ptr<N::FByteLease> RequestMemory, ResponseMemory;
			FRequest Request;
			FResponse Response;
			Network::FError CallbackError;
			std::uint64_t HeaderBytes = 0;
			bool bHeadComplete = false;
			bool bRedirectBody = false;
			bool bDownloadPaused = false;
			bool bUploadPaused = false;
			std::atomic<bool> ResumeQueued{false};
			std::atomic<unsigned> ResumeDirections{0};
			std::uint64_t UploadedBytes = 0;
			std::shared_ptr<Network::FWritableStream> Output;
			std::shared_ptr<Detail::FHttpStreamState> Head;
			char ErrorBuffer[CURL_ERROR_SIZE]{};
			bool ReserveResponse(std::uint64_t Capacity, std::uint64_t Headers)
			{
				const auto Limit = N::Options(Client->Context).MaxRetainedBytes;
				const auto Copies = Head ? 2u : 1u;
				if (Headers > Limit / Copies || Capacity > Limit - Headers * Copies ||
				    !ResponseMemory->TryResize(Capacity + Headers * Copies))
				{
					CallbackError.Code = Network::EError::Limit;
					return false;
				}
				return true;
			}
		};

		struct FWatch
		{
			FWatch(const std::shared_ptr<N::FContext>& Context, std::uintptr_t Handle, std::error_code& Error)
			    : Socket(N::Runtime(Context)->WatchSocket(Handle, Error))
			{
			}
			~FWatch()
			{
				if (Socket)
				{
					Socket->Release();
				}
			}
			std::shared_ptr<Network::Backend::ISocketWatch> Socket;
			std::uint64_t Generation = 0;
			int Interest = CURL_POLL_NONE;
		};

		explicit FCurlHttpClient(std::shared_ptr<N::FContext> C, FClientOptions O)
		    : Context(std::move(C)), Configuration(std::move(O)), Timer(N::Runtime(Context)->CreateTimer())
		{
			const auto Total =
			    Configuration.MaxConnections ? Configuration.MaxConnections : N::Options(Context).MaxConnections;
			if (!Timer)
			{
				throw Detail::FClientCreationError(Network::EError::Unsupported);
			}
			if (!Total || Total > N::Options(Context).MaxConnections || !FitsCurlLong(Total) ||
			    !Configuration.MaxConnectionsPerHost || !FitsCurlLong(Configuration.MaxConnectionsPerHost) ||
			    !FitsCurlLong(Configuration.MaxIdleConnections) || !Configuration.MaxPendingRequests ||
			    (Configuration.bCookies && (!Configuration.MaxCookieBytes || Configuration.MaxCookieBytes > N::Options(Context).MaxBufferBytes)) ||
			    Configuration.MaxConnectionIdleTime.count() < 0 ||
			    Configuration.MaxConnectionIdleTime.count() > LONG_MAX)
			{
				throw std::invalid_argument("Invalid HTTP client options");
			}
			if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
			{
				throw std::bad_alloc();
			}
			try
			{
				Multi = curl_multi_init();
				if (!Multi)
				{
					throw std::bad_alloc();
				}
				auto Set = [&](CURLMoption Option, auto Value)
				{
					const auto Code = curl_multi_setopt(Multi, Option, Value);
					if (Code != CURLM_OK)
					{
						throw std::runtime_error(curl_multi_strerror(Code));
					}
				};
				Set(CURLMOPT_SOCKETFUNCTION, SocketCallback);
				Set(CURLMOPT_SOCKETDATA, this);
				Set(CURLMOPT_TIMERFUNCTION, TimerCallback);
				Set(CURLMOPT_TIMERDATA, this);
				Set(CURLMOPT_MAX_TOTAL_CONNECTIONS, static_cast<long>(Total));
				Set(CURLMOPT_MAX_HOST_CONNECTIONS, static_cast<long>(Configuration.MaxConnectionsPerHost));
				Set(CURLMOPT_MAXCONNECTS, static_cast<long>(Configuration.MaxIdleConnections));
				if (Configuration.bCookies)
				{
					Network::FError BudgetError;
					CookieMemory = N::ReserveBytes(Context, Configuration.MaxCookieBytes, BudgetError);
					if (!CookieMemory)
					{
						throw Detail::FClientCreationError(BudgetError.Code);
					}
					CookieShare = curl_share_init();
					if (!CookieShare)
					{
						throw std::bad_alloc();
					}
					if (curl_share_setopt(CookieShare, CURLSHOPT_USERDATA, &CookieMutex) != CURLSHE_OK ||
					    curl_share_setopt(CookieShare, CURLSHOPT_LOCKFUNC, CookieLock) != CURLSHE_OK ||
					    curl_share_setopt(CookieShare, CURLSHOPT_UNLOCKFUNC, CookieUnlock) != CURLSHE_OK ||
					    curl_share_setopt(CookieShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != CURLSHE_OK)
					{
						throw std::runtime_error("Cannot initialize the HTTP cookie store");
					}
				}
			}
			catch (...)
			{
				if (CookieShare)
				{
					curl_share_cleanup(CookieShare);
				}
				if (Multi)
				{
					curl_multi_cleanup(Multi);
				}
				curl_global_cleanup();
				throw;
			}
		}
		~FCurlHttpClient()
		{
			if (Multi)
			{
				curl_multi_cleanup(Multi);
			}
			Transfers.clear();
			if (CookieShare)
			{
				curl_share_cleanup(CookieShare);
			}
			curl_global_cleanup();
		}
		static void CookieLock(CURL*, curl_lock_data, curl_lock_access, void* State)
		{
			static_cast<std::mutex*>(State)->lock();
		}
		static void CookieUnlock(CURL*, curl_lock_data, void* State)
		{
			static_cast<std::mutex*>(State)->unlock();
		}

		#include "HttpClientCookies.inl"

		void BindInputSource(const std::shared_ptr<FTransfer>& T, const std::shared_ptr<Network::FReadableStream>& Source,
		    std::size_t Index = 0)
		{
			T->Inputs->Set(Index, Source);
			N::FStreamAccess::OnReadable(*Source,
			    [Weak = weak_from_this(), Transfer = std::weak_ptr<FTransfer>(T)]
			{
				if (auto Self = Weak.lock())
				{
					if (auto Value = Transfer.lock())
					{
						Self->QueueResume(Value, 2);
					}
				}
			});
		}

		#include "HttpClientMultipart.inl"

		bool AdmitUpload(FTransfer& T)
		{
			std::uint64_t Bytes = T.Request.Body.size();
			if (T.Request.Multipart)
			{
				Bytes = 0;
				for (const auto& Part : *T.Request.Multipart)
				{
					if (Part.Body.size() > T.Request.MaxUploadBytes - std::min(Bytes, T.Request.MaxUploadBytes))
					{
						T.CallbackError = {Network::EError::Limit, "Multipart upload byte limit exceeded", 0};
						return false;
					}
					Bytes += Part.Body.size();
				}
			}
			if (Bytes > T.Request.MaxUploadBytes ||
			    Bytes > T.Request.MaxTotalUploadBytes - std::min(T.Total.Upload, T.Request.MaxTotalUploadBytes))
			{
				T.CallbackError = {Network::EError::Limit, "HTTP total upload byte limit exceeded", 0};
				return false;
			}
			T.Total.Upload += Bytes;
			T.MimeBodyBytes = Bytes;
			++T.MimeAdmission;
			return true;
		}
		static int SeekUpload(void* User, curl_off_t Offset, int Origin) noexcept
		{
			auto& T = *static_cast<FTransfer*>(User);
			if (Offset || Origin != SEEK_SET || N::Terminal(N::State(T.Operation)))
			{
				return CURL_SEEKFUNC_CANTSEEK;
			}
			try
			{
				Network::FError Error;
				auto Source = T.Request.UploadFactory(T.Attempt, Error);
				if (Error || !Source.Stream || Source.Length != T.Request.UploadLength ||
				    !N::FStreamAccess::ClaimReader(*Source.Stream))
				{
					T.CallbackError = Error ? std::move(Error)
					                        : Network::FError{Network::EError::State,
					                              "Upload factory returned an incompatible or consumed rewind source",
					                              0};
					return CURL_SEEKFUNC_FAIL;
				}
				T.Request.Upload->Cancel();
				T.Request.Upload = std::move(Source.Stream);
				T.UploadedBytes = 0;
				T.Client->BindUploadSource(T);
				return CURL_SEEKFUNC_OK;
			}
			catch (const std::bad_alloc&)
			{
				T.CallbackError.Code = Network::EError::Allocation;
			}
			catch (...)
			{
				T.CallbackError.Code = Network::EError::Callback;
			}
			return CURL_SEEKFUNC_FAIL;
		}
		void BindUploadSource(FTransfer& T)
		{
			// The callback is renewed after a factory rewind; the old source no longer wakes this transfer.
			auto It = Transfers.find(T.Easy);
			if (It == Transfers.end())
			{
				return;
			}
			BindInputSource(It->second, T.Request.Upload);
		}

		static std::size_t ReadCallback(char* Data, std::size_t Size, std::size_t Count, void* User) noexcept
		{
			auto& T = *static_cast<FTransfer*>(User);
			if ((Size && Count > SIZE_MAX / Size) || N::Terminal(N::State(T.Operation)))
			{
				return CURL_READFUNC_ABORT;
			}
			const auto Capacity = Size * Count;
			if (!Capacity)
			{
				return 0;
			}
			try
			{
				const auto Remaining =
				    std::min(T.Request.MaxUploadBytes - std::min(T.UploadedBytes, T.Request.MaxUploadBytes),
				        T.Request.MaxTotalUploadBytes - std::min(T.Total.Upload, T.Request.MaxTotalUploadBytes));
				const auto Amount =
				    std::min<std::uint64_t>({Capacity, Remaining, static_cast<std::uint64_t>(INT64_MAX)});
				char Extra;
				const auto Result =
				    T.Request.Upload->Read(Amount ? Data : &Extra, static_cast<std::int64_t>(Amount ? Amount : 1));
				if (Result.State == Network::EStreamState::WouldBlock)
				{
					T.bUploadPaused = true;
					return CURL_READFUNC_PAUSE;
				}
				if (Result.State == Network::EStreamState::Failed)
				{
					T.CallbackError = Result.Error;
					return CURL_READFUNC_ABORT;
				}
				if (!Amount && Result.Bytes)
				{
					T.CallbackError.Code = Network::EError::Limit;
					T.CallbackError.Message = "Upload byte limit exceeded";
					return CURL_READFUNC_ABORT;
				}
				if (Result.State == Network::EStreamState::End && T.Request.UploadLength &&
				    T.UploadedBytes != *T.Request.UploadLength)
				{
					T.CallbackError.Code = Network::EError::Protocol;
					T.CallbackError.Message = "Upload ended before its declared length";
					return CURL_READFUNC_ABORT;
				}
				T.UploadedBytes += Result.Bytes;
				T.Total.Upload += Result.Bytes;
				return static_cast<std::size_t>(Result.Bytes);
			}
			catch (...)
			{
				T.CallbackError.Code = Network::EError::Allocation;
				return CURL_READFUNC_ABORT;
			}
		}

		static std::size_t BodyCallback(char* Data, std::size_t Size, std::size_t Count, void* User) noexcept
		{
			auto& T = *static_cast<FTransfer*>(User);
			if (Size && Count > SIZE_MAX / Size)
			{
				return 0;
			}
			auto Bytes = Size * Count;
			try
			{
				if (N::Terminal(N::State(T.Operation)))
				{
					return 0;
				}
				if (Bytes > T.Request.MaxTotalBodyBytes - std::min(T.Total.Body, T.Request.MaxTotalBodyBytes))
				{
					T.CallbackError = {Network::EError::Limit, "HTTP total body byte limit exceeded", 0};
					return 0;
				}
				if (T.bRedirectBody)
				{
					T.Total.Body += Bytes;
					T.AttemptBodyBytes += Bytes;
					return Bytes;
				}
				if (Bytes > T.Request.MaxBodyBytes - std::min(T.Response.BodyBytes, T.Request.MaxBodyBytes))
				{
					T.CallbackError = {Network::EError::Limit, "HTTP body limit exceeded", 0};
					return 0;
				}
				if (T.Output)
				{
					auto Result = T.Output->WriteAll(Data, static_cast<std::int64_t>(Bytes));
					if (Result.State == Network::EStreamState::WouldBlock)
					{
						T.bDownloadPaused = true;
						return CURL_WRITEFUNC_PAUSE;
					}
					if (Result.State != Network::EStreamState::Progress)
					{
						T.CallbackError = std::move(Result.Error);
						return 0;
					}
				}
				else
				{
					const auto Needed = T.Response.Body.size() + Bytes;
					if (Needed > T.Response.Body.capacity())
					{
						const auto Current = T.Response.Body.capacity();
						const auto Limit = static_cast<std::size_t>(T.Request.MaxBodyBytes);
						const auto Growth = Current > Limit / 2 ? Limit : std::max<std::size_t>(16384, Current * 2);
						const auto Capacity = std::min(Limit, std::max(Needed, Growth));
						if (!T.ReserveResponse(Capacity, T.HeaderBytes))
						{
							return 0;
						}
						T.Response.Body.reserve(Capacity);
					}
					T.Response.Body.insert(T.Response.Body.end(), Data, Data + Bytes);
				}
				T.Response.BodyBytes += Bytes;
				T.Total.Body += Bytes;
				T.AttemptBodyBytes += Bytes;
				return Bytes;
			}
			catch (...)
			{
				T.CallbackError.Code = Network::EError::Allocation;
				return 0;
			}
		}

		#include "HttpClientTrace.inl"

		static std::size_t HeaderCallback(char* Data, std::size_t Size, std::size_t Count, void* User) noexcept
		{
			auto& T = *static_cast<FTransfer*>(User);
			if (Size && Count > SIZE_MAX / Size)
			{
				return 0;
			}
			auto Bytes = Size * Count;
			try
			{
				if (N::Terminal(N::State(T.Operation)))
				{
					return 0;
				}
				if (Bytes > T.Request.MaxHeaderBytes - std::min(T.HeaderBytes, T.Request.MaxHeaderBytes))
				{
					T.CallbackError = {Network::EError::Limit, "HTTP header limit exceeded", 0};
					return 0;
				}
				if (Bytes > T.Request.MaxTotalHeaderBytes - std::min(T.Total.Headers, T.Request.MaxTotalHeaderBytes))
				{
					T.CallbackError = {Network::EError::Limit, "HTTP total header byte limit exceeded", 0};
					return 0;
				}
				T.Total.Headers += Bytes;
				T.HeaderBytes += Bytes;
				if (!T.ReserveResponse(T.Response.Body.capacity(), T.HeaderBytes))
				{
					return 0;
				}
				std::string_view Line(Data, Bytes);
				if (Line.substr(0, 5) == "HTTP/")
				{
					T.Response.Headers = {};
					T.Response.Trailers = {};
					T.Response.Body.clear();
					T.Response.BodyBytes = 0;
					T.bHeadComplete = false;
					T.bRedirectBody = false;
					return Bytes;
				}
				if (Line == "\r\n" || Line == "\n")
				{
					if (!T.bHeadComplete)
					{
						long Status = 0;
						curl_easy_getinfo(T.Easy, CURLINFO_RESPONSE_CODE, &Status);
						if (!T.Client->RecordHead(T, static_cast<std::uint32_t>(Status)))
						{
							return 0;
						}
						const auto Locations = T.Response.Headers.GetAll("Location");
						T.bRedirectBody = T.Request.bFollowRedirects && Status >= 300 && Status < 400 &&
						    std::any_of(
						        Locations.begin(),
						        Locations.end(),
						        [](const auto& Value)
						{
							return !Value.empty();
						});

						if (T.bRedirectBody && T.Redirects++ < T.Request.MaxRedirects && !T.Request.Upload)
						{
							char* Method = nullptr;
							curl_easy_getinfo(T.Easy, CURLINFO_EFFECTIVE_METHOD, &Method);
							const bool Rewrite = Status == 303 || ((Status == 301 || Status == 302) && Method && std::string_view(Method) == "POST");
							if (!Rewrite && !T.Client->AdmitUpload(T))
							{
								return 0;
							}
						}
						if (T.Head && Status >= 200 && !T.bRedirectBody)
						{
							T.Client->Describe(T);
							T.Response.Details = T.Client->TraceSnapshot(T, false);
							if (!T.Response.Details)
							{
								return 0;
							}
							std::lock_guard<std::mutex> Lock(T.Head->Mutex);
							T.Head->Retention = T.ResponseMemory;
							T.Head->Head = T.Response;
						}
					}
					T.bHeadComplete = true;
					return Bytes;
				}
				auto Colon = Line.find(':');
				if (Colon == std::string_view::npos)
				{
					return 0;
				}
				auto Value = Line.substr(Colon + 1);
				while (!Value.empty() && (Value.front() == ' ' || Value.front() == '\t'))
				{
					Value.remove_prefix(1);
				}
				while (!Value.empty() &&
				    (Value.back() == '\r' || Value.back() == '\n' || Value.back() == ' ' || Value.back() == '\t'))
				{
					Value.remove_suffix(1);
				}
				auto& Headers = T.bHeadComplete ? T.Response.Trailers : T.Response.Headers;
				return Headers.TryAdd(std::string(Line.substr(0, Colon)), std::string(Value), T.CallbackError) ? Bytes
				                                                                                               : 0;
			}
			catch (...)
			{
				T.CallbackError.Code = Network::EError::Allocation;
				return 0;
			}
		}

		void Arm(curl_socket_t Fd, const std::shared_ptr<FWatch>& Watch, bool bRead)
		{
			auto Generation = Watch->Generation;
			Watch->Socket->AsyncWait(bRead,
			    [Weak = weak_from_this(), Watch, Fd, Generation, bRead](std::error_code Error)
			{
				auto Self = Weak.lock();
				if (!Self || Error || Self->Closed || Generation != Watch->Generation)
				{
					return;
				}
				Self->Action(Fd, bRead ? CURL_CSELECT_IN : CURL_CSELECT_OUT);
				if (!Self->Closed && Generation == Watch->Generation &&
				    (Watch->Interest == CURL_POLL_INOUT ||
				        Watch->Interest == (bRead ? CURL_POLL_IN : CURL_POLL_OUT)))
				{
					Self->Arm(Fd, Watch, bRead);
				}
			});
		}

		static int SocketCallback(CURL*, curl_socket_t Fd, int Interest, void* User, void*) noexcept
		{
			auto& Self = *static_cast<FCurlHttpClient*>(User);
			try
			{
				auto It = Self.Watches.find(Fd);
				if (It != Self.Watches.end())
				{
					++It->second->Generation;

					It->second->Socket->Cancel();
					if (Interest == CURL_POLL_REMOVE)
					{
						It->second->Socket->Release();
						Self.Watches.erase(It);
						return 0;
					}
				}
				else if (Interest != CURL_POLL_REMOVE)
				{
					std::error_code Error;
					auto Watch = std::make_shared<FWatch>(Self.Context, static_cast<std::uintptr_t>(Fd), Error);
					if (Error || !Watch->Socket)
					{
						return -1;
					}
					It = Self.Watches.emplace(Fd, std::move(Watch)).first;
				}
				else
				{
					return 0;
				}
				It->second->Interest = Interest;
				if (Interest == CURL_POLL_IN || Interest == CURL_POLL_INOUT)
				{
					Self.Arm(Fd, It->second, true);
				}
				if (Interest == CURL_POLL_OUT || Interest == CURL_POLL_INOUT)
				{
					Self.Arm(Fd, It->second, false);
				}
				return 0;
			}
			catch (...)
			{
				return -1;
			}
		}

		static int CloseSystemSocket(curl_socket_t Socket) noexcept
		{
			#ifdef _WIN32
			return ::closesocket(Socket);
			#else
			return ::close(Socket);
			#endif
		}

		static curl_socket_t OpenSocket(void* User, curlsocktype Purpose, curl_sockaddr* Address) noexcept
		{
			auto& Transfer = *static_cast<FTransfer*>(User);
			auto& Self = *Transfer.Client;
			std::uint64_t Reservation = 0;
			curl_socket_t Socket = CURL_SOCKET_BAD;
			try
			{
				if (Purpose != CURLSOCKTYPE_IPCXN || Self.Closed ||
				    (Address->family != AF_INET && Address->family != AF_INET6))
				{
					return CURL_SOCKET_BAD;
				}
				Reservation = N::AddResource(Self.Context,
				    [Weak = Self.weak_from_this()]
				{
					if (auto Client = Weak.lock())
					{
						Client->CloseOnIo();
					}
				});
				if (!Reservation)
				{
					Transfer.CallbackError.Code = Network::EError::Limit;
					Transfer.CallbackError.Message = "HTTP connection quota exceeded";
					return CURL_SOCKET_BAD;
				}
				Socket = ::socket(Address->family, Address->socktype, Address->protocol);
				if (Socket == CURL_SOCKET_BAD)
				{
					N::RemoveResource(Self.Context, Reservation);
					return CURL_SOCKET_BAD;
				}
				Self.SocketResources.emplace(Socket, Reservation);
				return Socket;
			}
			catch (...)
			{
				N::RemoveResource(Self.Context, Reservation);
				if (Socket != CURL_SOCKET_BAD)
				{
					CloseSystemSocket(Socket);
				}
				Transfer.CallbackError.Code = Network::EError::Allocation;
				return CURL_SOCKET_BAD;
			}
		}

		static int CloseSocket(void* User, curl_socket_t Socket) noexcept
		{
			auto& Self = *static_cast<FCurlHttpClient*>(User);
			const auto Watch = Self.Watches.find(Socket);
			if (Watch != Self.Watches.end())
			{
				++Watch->second->Generation;

				Watch->second->Socket->Cancel();
				Watch->second->Socket->Release();
				Self.Watches.erase(Watch);
			}
			const auto Reservation = Self.SocketResources.find(Socket);
			if (Reservation != Self.SocketResources.end())
			{
				N::RemoveResource(Self.Context, Reservation->second);
				Self.SocketResources.erase(Reservation);
			}
			return CloseSystemSocket(Socket);
		}

		static int TimerCallback(CURLM*, long Timeout, void* User) noexcept
		{
			auto& Self = *static_cast<FCurlHttpClient*>(User);
			try
			{
				Self.Timer->Cancel();
				if (Timeout < 0 || Self.Closed)
				{
					return 0;
				}
				Self.Timer->ExpiresAt(std::chrono::steady_clock::now() + std::chrono::milliseconds(Timeout));
				Self.Timer->AsyncWait(
				    [Weak = Self.weak_from_this()](std::error_code Error)
				{
					if (auto Locked = Weak.lock(); Locked && !Error && !Locked->Closed)
					{
						Locked->Action(CURL_SOCKET_TIMEOUT, 0);
					}
				});
				return 0;
			}
			catch (...)
			{
				return -1;
			}
		}

		void Remove(CURL* Easy)
		{
			auto It = Transfers.find(Easy);
			if (It == Transfers.end())
			{
				return;
			}
			if (It->second->RetryTimer)
			{
				It->second->RetryTimer->Cancel();
			}
			if (It->second->bInMulti)
			{
				curl_multi_remove_handle(Multi, Easy);
			}
			Transfers.erase(It);
		}

		void Describe(FTransfer& T)
		{
			long Status = 0, Protocol = 0;
			char* EffectiveUrl = nullptr;
			curl_easy_getinfo(T.Easy, CURLINFO_RESPONSE_CODE, &Status);
			curl_easy_getinfo(T.Easy, CURLINFO_HTTP_VERSION, &Protocol);
			curl_easy_getinfo(T.Easy, CURLINFO_EFFECTIVE_URL, &EffectiveUrl);
			T.Response.Status = static_cast<std::uint32_t>(Status);
			T.Response.ProtocolMajor = Protocol == CURL_HTTP_VERSION_2_0 ? 2 : Protocol == CURL_HTTP_VERSION_3 ? 3
			                                                                                                   : 1;
			T.Response.EffectiveUrl = EffectiveUrl ? EffectiveUrl : "";
		}

		void Resume(const std::shared_ptr<FTransfer>& T)
		{
			T->ResumeQueued = false;
			const auto Directions = T->ResumeDirections.exchange(0);
			if (Closed || !Transfers.count(T->Easy) || (!T->bDownloadPaused && !T->bUploadPaused))
			{
				return;
			}
			// pause(CONT) can synchronously replay BodyCallback; clear the flag first.
			if (Directions & 1)
			{
				T->bDownloadPaused = false;
			}
			if (Directions & 2)
			{
				T->bUploadPaused = false;
			}
			const auto Mask = (T->bDownloadPaused ? CURLPAUSE_RECV : 0) | (T->bUploadPaused ? CURLPAUSE_SEND : 0);
			const auto Code = curl_easy_pause(T->Easy, Mask);
			if (Code != CURLE_OK)
			{
				N::Fail(T->Operation, Network::EError::Read, curl_easy_strerror(Code), Code);
				Remove(T->Easy);
				return;
			}
			Action(CURL_SOCKET_TIMEOUT, 0);
		}

		void QueueResume(const std::shared_ptr<FTransfer>& Value, unsigned Direction)
		{
			Value->ResumeDirections.fetch_or(Direction);
			if (Value->ResumeQueued.exchange(true))
			{
				return;
			}
			try
			{
				N::Post(Context,
				    [Weak = weak_from_this(), Transfer = std::weak_ptr<FTransfer>(Value)]
				{
					if (auto Client = Weak.lock())
					{
						if (auto T = Transfer.lock())
						{
							Client->Resume(T);
						}
					}
				});
			}
			catch (...)
			{
				Value->ResumeQueued = false;
				N::Fault(Context, Network::EError::Allocation);
			}
		}

		#include "HttpClientRetry.inl"

		void Action(curl_socket_t Fd, int Events)
		{
			int Running = 0;
			auto Code = curl_multi_socket_action(Multi, Fd, Events, &Running);
			if (Code != CURLM_OK)
			{
				CloseOnIo();
				return;
			}
			int Remaining = 0;
			while (auto* Message = curl_multi_info_read(Multi, &Remaining))
			{
				if (Message->msg != CURLMSG_DONE)
				{
					continue;
				}
				auto It = Transfers.find(Message->easy_handle);
				if (It == Transfers.end())
				{
					continue;
				}
				auto T = It->second;
				auto Result = Message->data.result;
				Describe(*T);
				if (CookieShare)
				{
					FCookies Snapshot;
					std::shared_ptr<N::FByteLease> Memory;
					Network::FError Failure;
					if (!CookieSnapshot(T->Easy, Snapshot, Memory, Failure))
					{
						if (Failure.Code == Network::EError::Limit)
						{
							curl_easy_setopt(T->Easy, CURLOPT_COOKIELIST, "ALL");
						}
						T->CallbackError = std::move(Failure);
					}
				}
				if (!RecordAttempt(*T, Result) || N::Terminal(N::State(T->Operation)))
				{
					Remove(T->Easy);
					continue;
				}
				if (Retry(T, Result))
				{
					continue;
				}
				Remove(T->Easy);
				if (T->CallbackError)
				{
					N::Fail(T->Operation, T->CallbackError.Code, T->CallbackError.Message, T->CallbackError.SystemCode);
				}
				else if (Result != CURLE_OK)
				{
					N::Fail(T->Operation,
					    Detail::TransportError(Result),
					    T->ErrorBuffer[0] ? T->ErrorBuffer : curl_easy_strerror(Result),
					    Result);
				}
				else if (T->Request.bRequireHttp2 && T->Response.ProtocolMajor != 2)
				{
					N::Fail(T->Operation, Network::EError::Unsupported, "Peer did not negotiate HTTP/2");
				}
				else
				{
					T->Response.bComplete = true;
					T->Response.Attempts = std::move(T->Previous);
					auto Retention =
					    std::make_shared<std::pair<std::shared_ptr<N::FByteLease>, std::shared_ptr<N::FByteLease>>>(
					        T->ResponseMemory, T->HistoryMemory);
					auto Value = N::OwnResult(Context, std::move(T->Response), std::move(Retention));
					// Publish EOF before a successful operation can wake its consumer.
					// The terminal observer runs after publication and can race with Wait().
					if (T->Output)
					{
						T->Output->Finish();
					}
					N::Finish(Context, T->Operation, Network::EOperationState::Succeeded, {}, std::move(Value));
				}
			}
		}

		void Start(FRequest Request, const std::shared_ptr<N::FOperation>& Op,
		    std::shared_ptr<N::FByteLease> RequestMemory, std::shared_ptr<Network::FWritableStream> Output = {},
		    std::shared_ptr<Detail::FHttpStreamState> Head = {}, std::uint32_t Attempt = 1,
		    std::vector<FAttempt> Previous = {}, std::shared_ptr<N::FByteLease> History = {}, FFlow Accumulated = {})
		{
			if (N::Terminal(N::State(Op)))
			{
				return;
			}
			if (Closed)
			{
				N::Fail(Op, Network::EError::Closed, "HTTP client closed");
				return;
			}
			if (Transfers.size() >= Configuration.MaxPendingRequests)
			{
				N::Fail(Op, Network::EError::QueueFull, "HTTP client pending request limit exceeded");
				return;
			}
			const auto& Policy = Request.Retry;
			const bool Idempotent = Request.Method == "GET" || Request.Method == "HEAD" ||
			    Request.Method == "OPTIONS" || Request.Method == "PUT" ||
			    Request.Method == "DELETE";
			if (!Policy.MaxAttempts || Policy.MaxAttempts > 32 || Policy.Delay.count() < 0 ||
			    Policy.MaxDelay.count() < 0 || Policy.Delay > Policy.MaxDelay ||
			    Policy.MaxDelay > std::chrono::hours(24) || Policy.Statuses.size() > 64 ||
			    (Policy.MaxAttempts > 1 && (!Policy.bAllowReplay || (!Idempotent && !Policy.bAllowUnsafeMethods) || (Request.Upload && !Request.UploadFactory))))
			{
				N::Fail(Op, Network::EError::InvalidArgument, "Invalid HTTP retry or body replay policy");
				return;
			}
			for (const auto Status : Policy.Statuses)
			{
				if (Status < 100 || Status > 599)
				{
					N::Fail(Op, Network::EError::InvalidArgument, "Invalid retry status");
					return;
				}
			}

			if (Request.UploadFactory)
			{
				if (Attempt == 1 && Request.Upload)
				{
					N::Fail(Op, Network::EError::InvalidArgument, "Upload source and factory are mutually exclusive");
					return;
				}
				Network::FError Failure;
				try
				{
					auto Source = Request.UploadFactory(Attempt, Failure);
					if (Failure || !Source.Stream)
					{
						N::Fail(Op,
						    Failure ? Failure.Code : Network::EError::Callback,
						    Failure ? Failure.Message : "Upload factory returned no source",
						    Failure ? Failure.SystemCode : 0);
						return;
					}
					Request.Upload = std::move(Source.Stream);
					Request.UploadLength = Source.Length;
				}
				catch (const std::bad_alloc&)
				{
					N::Fail(Op, Network::EError::Allocation, "Upload factory allocation failed");
					return;
				}
				catch (...)
				{
					N::Fail(Op, Network::EError::Callback, "Upload factory threw");
					return;
				}
			}
			Network::FUrl Url;
			Network::FError Error;
			if (!Network::FUrl::TryParse(Request.Url, Url, Error) || (Url.Scheme != "http" && Url.Scheme != "https"))
			{
				N::Fail(Op, Network::EError::InvalidArgument, "Expected an HTTP or HTTPS URL");
				return;
			}
			FHeaders MethodCheck;
			if (!MethodCheck.TryAdd(Request.Method, "", Error) || Request.ConnectTimeout.count() <= 0 ||
			    Request.ConnectTimeout.count() > LONG_MAX || !FitsCurlLong(Request.MaxRedirects) ||
			    Request.MaxHeaderBytes > N::Options(Context).MaxBufferBytes ||
			    (!Output && (Request.MaxBodyBytes > N::Options(Context).MaxBufferBytes || Request.MaxBodyBytes > Network::FBytes{}.max_size())) ||
			    Request.Body.size() > N::Options(Context).MaxBufferBytes ||
			    Request.Body.size() > static_cast<std::uint64_t>(INT64_MAX) ||
			    Request.Proxy.find('\0') != std::string::npos || Request.CaFile.find('\0') != std::string::npos)
			{
				N::Fail(Op, Network::EError::InvalidArgument, "Invalid method, timeout or buffer limits");
				return;
			}
			if (Request.bRequireHttp2 && !(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_HTTP2))
			{
				N::Fail(Op, Network::EError::Unsupported, "This curl backend does not provide HTTP/2");
				return;
			}
			if (!Request.Headers.GetAll("Content-Length").empty() ||
			    !Request.Headers.GetAll("Transfer-Encoding").empty() || (Request.UploadLength && !Request.Upload) ||
			    (Request.Upload &&
			        (!Request.Body.empty() || Request.Multipart || Request.Method == "HEAD" ||
			            (Request.bFollowRedirects && !Request.UploadFactory) ||
			            (Request.UploadLength && (*Request.UploadLength > static_cast<std::uint64_t>(INT64_MAX) || *Request.UploadLength > Request.MaxUploadBytes)))))
			{
				N::Fail(Op,
				    Network::EError::InvalidArgument,
				    "Body framing is backend-managed; upload streams are exclusive, single-use bodies");
				return;
			}
			if (Request.Multipart)
			{
				if (!Request.Body.empty() || Request.Method == "HEAD" || Request.Multipart->size() > 256 ||
				    !Request.Headers.GetAll("Content-Type").empty() ||
				    !Request.Headers.GetAll("Content-Length").empty() ||
				    !Request.Headers.GetAll("Transfer-Encoding").empty())
				{
					N::Fail(Op,
					    Network::EError::InvalidArgument,
					    "Multipart requires an exclusive body and backend-managed framing headers");
					return;
				}
				std::uint64_t Total = 0;
				for (const auto& Part : *Request.Multipart)
				{
					const auto Clean = [](std::string_view Text)
					{
						return std::none_of(Text.begin(),
						    Text.end(),
						    [](unsigned char C)
						{
							return C < 0x20 || C == 0x7f;
						});
					};
					if (!Clean(Part.Name) || !Clean(Part.ContentType) || (Part.Filename && !Clean(*Part.Filename)) ||
					    Part.Body.size() > static_cast<std::uint64_t>(INT64_MAX))
					{
						N::Fail(Op, Network::EError::InvalidArgument, "Invalid multipart field");
						return;
					}
					for (const auto Bytes : {Part.Body.size(),
					         Part.Name.size(),
					         Part.ContentType.size(),
					         Part.Filename ? Part.Filename->size() : std::size_t{0}})
					{
						if (Bytes > N::Options(Context).MaxBufferBytes - Total)
						{
							N::Fail(Op, Network::EError::Limit, "Multipart payload limit exceeded");
							return;
						}
						Total += Bytes;
					}
				}
			}
			FInputClaims InputClaims;
			if (!PrepareMultipart(Request, Attempt, Op, InputClaims))
			{
				return;
			}
			if (Request.Upload)
			{
				InputClaims.Readers.reserve(InputClaims.Readers.size() + 1);
				if (!N::FStreamAccess::ClaimReader(*Request.Upload))
				{
					InputClaims.Rollback();
					N::Fail(Op, Network::EError::State, "Upload stream has already been consumed or cancelled");
					return;
				}
				InputClaims.Readers.push_back(Request.Upload);
			}
			auto T = std::make_shared<FTransfer>();
			T->Client = this;
			T->Attempt = Attempt;
			T->Total = Accumulated;
			T->Previous = std::move(Previous);
			T->HistoryMemory =
			    History ? std::move(History)
			            : N::ReserveBytes(
			                  Context, static_cast<std::uint64_t>(Request.Retry.MaxAttempts) * sizeof(FAttempt), Op);
			if (!T->HistoryMemory)
			{
				return;
			}
			T->Previous.reserve(Request.Retry.MaxAttempts);
			T->Operation = Op;
			T->RequestMemory = std::move(RequestMemory);
			T->ResponseMemory = N::ReserveBytes(Context, 0, Op);
			if (!T->ResponseMemory)
			{
				return;
			}
			T->Output = std::move(Output);
			T->Head = std::move(Head);
			T->Request = std::move(Request);
			if (!Detail::FAttemptHistoryAccess::Begin(T->Request.History, Op, Attempt))
			{
				if (!N::Terminal(N::State(Op)))
				{
					N::Fail(Op, Network::EError::Internal, "Cannot activate HTTP history");
				}
				return;
			}
			if (!T->Request.Upload && !AdmitUpload(*T))
			{
				N::Fail(Op, T->CallbackError.Code, T->CallbackError.Message, T->CallbackError.SystemCode);
				return;
			}
			if (T->Output)
			{
				N::FStreamAccess::OnWritable(*T->Output,
				    [Weak = weak_from_this(), Transfer = std::weak_ptr<FTransfer>(T)]
				{
					auto Self = Weak.lock();
					auto Value = Transfer.lock();
					if (Self && Value)
					{
						Self->QueueResume(Value, 1);
					}
				});
			}
			const bool HasInputs =
			    T->Request.Upload || (T->Request.Multipart && std::any_of(T->Request.Multipart->begin(), T->Request.Multipart->end(), [](const auto& Part)
			{
				return static_cast<bool>(Part.Upload);
			}));
			if (HasInputs)
			{
				T->Inputs = std::make_shared<FInputSources>(T->Request.Multipart ? T->Request.Multipart->size() : 1);
				N::ObserveTerminal(Op,
				    [Inputs = T->Inputs, Output = std::weak_ptr<Network::FWritableStream>(T->Output)](
				        auto State, const auto& Failure)
				{
					Inputs->Cancel();
					if (auto Writer = Output.lock())
					{
						if (State == Network::EOperationState::Succeeded)
						{
							Writer->Finish();
						}
						else
						{
							Network::FError E;
							E.Code = Failure.Code;
							E.SystemCode = Failure.SystemCode;
							try
							{
								E.Message = Failure.Message;
							}
							catch (...)
							{
							}
							Writer->Fail(std::move(E));
						}
					}
				});
				if (T->Request.Upload)
				{
					BindInputSource(T, T->Request.Upload);
				}
				if (T->Request.Multipart)
				{
					for (std::size_t I = 0; I < T->Request.Multipart->size(); ++I)
					{
						if (const auto& Source = (*T->Request.Multipart)[I].Upload)
						{
							BindInputSource(T, Source, I);
						}
					}
				}
			}

			T->Easy = curl_easy_init();
			if (!T->Easy)
			{
				N::Fail(Op, Network::EError::Limit, "Cannot allocate HTTP request");
				return;
			}
			bool bConfigured = true;
			auto Set = [&](CURLoption Option, auto Value)
			{
				bConfigured = (curl_easy_setopt(T->Easy, Option, Value) == CURLE_OK) && bConfigured;
			};
			Set(CURLOPT_URL, T->Request.Url.c_str());
			Set(CURLOPT_NOSIGNAL, 1L);
			Set(CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
			Set(CURLOPT_OPENSOCKETFUNCTION, OpenSocket);
			Set(CURLOPT_OPENSOCKETDATA, T.get());
			Set(CURLOPT_CLOSESOCKETFUNCTION, CloseSocket);
			Set(CURLOPT_CLOSESOCKETDATA, this);
			if (!Configuration.bInheritProxyEnvironment || !T->Request.Proxy.empty())
			{
				Set(CURLOPT_PROXY, T->Request.Proxy.c_str());
			}
			Set(CURLOPT_MAXAGE_CONN, static_cast<long>(Configuration.MaxConnectionIdleTime.count()));
			if (CookieShare)
			{
				Set(CURLOPT_SHARE, CookieShare);
				Set(CURLOPT_COOKIEFILE, "");
			}
			if (!T->Request.CaFile.empty())
			{
				Set(CURLOPT_CAINFO, T->Request.CaFile.c_str());
			}
			Set(CURLOPT_PROTOCOLS_STR, "http,https");
			Set(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
			Set(CURLOPT_ERRORBUFFER, T->ErrorBuffer);
			Set(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(T->Request.ConnectTimeout.count()));
			Set(CURLOPT_FOLLOWLOCATION, T->Request.bFollowRedirects ? static_cast<long>(CURLFOLLOW_OBEYCODE) : 0L);
			Set(CURLOPT_MAXREDIRS, static_cast<long>(T->Request.MaxRedirects));
			Set(CURLOPT_HTTP_VERSION,
			    static_cast<long>(T->Request.bRequireHttp2 ? CURL_HTTP_VERSION_2TLS : CURL_HTTP_VERSION_1_1));
			if (T->Request.bDecodeContent)
			{
				Set(CURLOPT_ACCEPT_ENCODING, "");
			}
			if (T->Request.Upload)
			{
				Set(CURLOPT_UPLOAD, 1L);
				Set(CURLOPT_CUSTOMREQUEST, T->Request.Method.c_str());
				Set(CURLOPT_READFUNCTION, ReadCallback);
				Set(CURLOPT_READDATA, T.get());
				if (T->Request.UploadFactory)
				{
					Set(CURLOPT_SEEKFUNCTION, SeekUpload);
					Set(CURLOPT_SEEKDATA, T.get());
				}
				Set(CURLOPT_INFILESIZE_LARGE,
				    T->Request.UploadLength ? static_cast<curl_off_t>(*T->Request.UploadLength)
				                            : static_cast<curl_off_t>(-1));
			}
			else if (T->Request.Method == "HEAD")
			{
				Set(CURLOPT_NOBODY, 1L);
			}
			else if (T->Request.Multipart)
			{
				T->Mime = curl_mime_init(T->Easy);
				if (!T->Mime)
				{
					N::Fail(Op, Network::EError::Allocation, "Cannot allocate multipart body");
					return;
				}
				for (const auto& Part : *T->Request.Multipart)
				{
					auto* MimePart = curl_mime_addpart(T->Mime);
					if (!MimePart)
					{
						N::Fail(Op, Network::EError::Allocation, "Cannot allocate multipart field");
						return;
					}
					bConfigured = (curl_mime_name(MimePart, Part.Name.c_str()) == CURLE_OK) && bConfigured;
					if (Part.Upload)
					{
						auto Source = std::make_unique<FMimeUpload>();
						Source->Transfer = T.get();
						Source->Index = static_cast<std::size_t>(&Part - T->Request.Multipart->data());
						const auto Length = Part.UploadLength ? static_cast<curl_off_t>(*Part.UploadLength)
						                                      : static_cast<curl_off_t>(-1);
						auto* State = Source.get();
						T->MimeUploads.push_back(std::move(Source));
						bConfigured =
						    (curl_mime_data_cb(MimePart, Length, ReadMime, SeekMime, nullptr, State) == CURLE_OK) &&
						    bConfigured;
					}
					else
					{
						bConfigured = (curl_mime_data(MimePart,
						                   Part.Body.empty() ? "" : reinterpret_cast<const char*>(Part.Body.data()),
						                   Part.Body.size()) == CURLE_OK) &&
						    bConfigured;
					}
					if (Part.Filename)
					{
						bConfigured = (curl_mime_filename(MimePart, Part.Filename->c_str()) == CURLE_OK) && bConfigured;
					}
					if (!Part.ContentType.empty())
					{
						bConfigured = (curl_mime_type(MimePart, Part.ContentType.c_str()) == CURLE_OK) && bConfigured;
					}
				}
				Set(CURLOPT_MIMEPOST, T->Mime);
			}
			else if (T->Request.Method == "POST" || !T->Request.Body.empty())
			{
				Set(CURLOPT_POSTFIELDS,
				    T->Request.Body.empty() ? "" : reinterpret_cast<const char*>(T->Request.Body.data()));
				Set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(T->Request.Body.size()));
			}
			if (T->Request.Method != "GET" && T->Request.Method != "POST" && T->Request.Method != "HEAD")
			{
				Set(CURLOPT_CUSTOMREQUEST, T->Request.Method.c_str());
			}
			if (T->Request.Method == "GET" && (!T->Request.Body.empty() || T->Request.Multipart))
			{
				Set(CURLOPT_CUSTOMREQUEST, "GET");
			}
			for (const auto& Pair : T->Request.Headers.Fields())
			{
				auto Line = Pair.first + (Pair.second.empty() ? ";" : ": " + Pair.second);
				auto* Appended = curl_slist_append(T->HeaderList, Line.c_str());
				if (!Appended)
				{
					N::Fail(Op, Network::EError::Limit, "Cannot allocate HTTP headers");
					return;
				}
				T->HeaderList = Appended;
			}
			Set(CURLOPT_HTTPHEADER, T->HeaderList);
			Set(CURLOPT_WRITEFUNCTION, BodyCallback);
			Set(CURLOPT_WRITEDATA, T.get());
			Set(CURLOPT_HEADERFUNCTION, HeaderCallback);
			Set(CURLOPT_HEADERDATA, T.get());
			if (!bConfigured)
			{
				N::Fail(Op, Network::EError::Unsupported, "curl rejected a request option");
				return;
			}
			N::SetAbort(Op,
			    [Weak = weak_from_this(), Transfer = std::weak_ptr<FTransfer>(T)]
			{
				if (auto Self = Weak.lock())
				{
					if (auto Pending = Transfer.lock())
					{
						auto It = Self->Transfers.find(Pending->Easy);
						if (It != Self->Transfers.end() && It->second == Pending)
						{
							Self->RecordAborted(*Pending);
							Self->Remove(Pending->Easy);
						}
					}
				}
			});
			if (N::Terminal(N::State(Op)))
			{
				return;
			}
			N::Running(Op);
			Transfers.emplace(T->Easy, T);
			if (curl_multi_add_handle(Multi, T->Easy) != CURLM_OK)
			{
				Transfers.erase(T->Easy);
				N::Fail(Op, Network::EError::Protocol, "Cannot submit HTTP transfer");
			}
			else
			{
				T->bInMulti = true;
				InputClaims.bCommitted = true;
			}
		}

		void CloseOnIo()
		{
			if (Closed)
			{
				return;
			}
			Closed = true;
			N::RemoveResource(Context, ResourceId);
			Timer->Cancel();
			while (!Transfers.empty())
			{
				auto T = Transfers.begin()->second;
				N::Fail(T->Operation, Network::EError::Closed, "HTTP client closed");
				RecordAborted(*T);
				Remove(T->Easy);
			}
			curl_multi_cleanup(Multi);
			Multi = nullptr;
			for (const auto& Pair : Watches)
			{
				++Pair.second->Generation;

				Pair.second->Socket->Cancel();
				Pair.second->Socket->Release();
			}
			Watches.clear();
		}

		std::shared_ptr<N::FContext> Context;
		FClientOptions Configuration;
		CURLSH* CookieShare = nullptr;
		std::shared_ptr<N::FByteLease> CookieMemory;
		std::mutex CookieMutex;
		std::shared_ptr<Network::Backend::ITimer> Timer;
		CURLM* Multi = nullptr;
		std::map<CURL*, std::shared_ptr<FTransfer>> Transfers;
		std::map<curl_socket_t, std::shared_ptr<FWatch>> Watches;
		std::map<curl_socket_t, std::uint64_t> SocketResources;
		bool Closed = false;
		std::uint64_t ResourceId = 0;

		void Dispose() noexcept override;
		Network::FOperation Send(FRequest Request) override;
		Network::FOperation Cookies(Network::FOperationOptions Options) override;
		Network::FOperation ClearCookies(Network::FOperationOptions Options) override;
		Network::FOperation ImportCookies(FCookies Cookies, Network::FOperationOptions Options) override;
		FStreamingOperation SendStream(FRequest Request, std::uint64_t BufferBytes) override;
		FCapabilities Capabilities() const override;
		bool BelongsTo(const Network::FContext& Context) const noexcept override;
		void RequestClose() override;
	};

	void FCurlHttpClient::Dispose() noexcept
	{
		N::OnDestroy(Context,
		    [this]
		{
			RequestClose();
		});
	}

	Network::FOperation FCurlHttpClient::Send(FRequest Request)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Request.Operation);
		if (!Detail::FAttemptHistoryAccess::Bind(Request.History, P->Context, Op))
		{
			N::Fail(Op, Network::EError::State, "HTTP history is already in use");
			Request.History.reset();
		}
		if (!N::Terminal(N::State(Op)))
		{
			auto Retention = ReserveRequest(P->Context, Request, Op);
			if (!Retention)
			{
				return N::FAccess::Wrap(Op);
			}
			N::Post(P->Context,
			    [P, Op, Retention, Request = std::move(Request)]() mutable
			{
				P->Start(std::move(Request), Op, std::move(Retention));
			});
		}
		return N::FAccess::Wrap(Op);
	}

	Network::FOperation FCurlHttpClient::Cookies(Network::FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (!N::Terminal(N::State(Op)))
		{
			N::Post(P->Context,
			    [P, Op]
			{
				P->CookieCommand(Op, 0, {});
			});
		}
		return N::FAccess::Wrap(Op);
	}
	Network::FOperation FCurlHttpClient::ClearCookies(Network::FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (!N::Terminal(N::State(Op)))
		{
			N::Post(P->Context,
			    [P, Op]
			{
				P->CookieCommand(Op, 1, {});
			});
		}
		return N::FAccess::Wrap(Op);
	}
	Network::FOperation FCurlHttpClient::ImportCookies(FCookies Cookies, Network::FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (N::Terminal(N::State(Op)))
		{
			return N::FAccess::Wrap(Op);
		}
		std::uint64_t Size = static_cast<std::uint64_t>(Cookies.Records.capacity()) * sizeof(std::string);
		if (Size > P->Configuration.MaxCookieBytes)
		{
			N::Fail(Op, Network::EError::Limit, "Cookie import byte limit exceeded");
			return N::FAccess::Wrap(Op);
		}
		for (const auto& R : Cookies.Records)
		{
			const auto Bytes = R.capacity() + 32;
			if (Bytes > P->Configuration.MaxCookieBytes - std::min(Size, P->Configuration.MaxCookieBytes))
			{
				N::Fail(Op, Network::EError::Limit, "Cookie import byte limit exceeded");
				return N::FAccess::Wrap(Op);
			}
			Size += Bytes;
		}
		auto Memory = N::ReserveBytes(P->Context, Size, Op);
		if (Memory)
		{
			N::Post(P->Context,
			    [P, Op, Memory, Cookies = std::move(Cookies)]() mutable
			{
				P->CookieCommand(Op, 2, std::move(Cookies));
			});
		}
		return N::FAccess::Wrap(Op);
	}

	FStreamingOperation FCurlHttpClient::SendStream(FRequest Request, std::uint64_t BufferBytes)
	{
		FStreamingOperation Result;
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Request.Operation);
		if (!Detail::FAttemptHistoryAccess::Bind(Request.History, P->Context, Op))
		{
			N::Fail(Op, Network::EError::State, "HTTP history is already in use");
			Request.History.reset();
		}
		auto Head = std::make_shared<Detail::FHttpStreamState>();
		Result = Backend::FAccess::Stream(N::FAccess::Wrap(Op), {}, Head);
		if (N::Terminal(N::State(Op)))
		{
			return Result;
		}
		Network::FBytePipe Pipe;
		Network::FError Error;
		if (BufferBytes < CURL_MAX_WRITE_SIZE || BufferBytes > N::Options(P->Context).MaxBufferBytes)
		{
			N::Fail(Op, Network::EError::InvalidArgument, "Invalid HTTP stream buffer capacity");
			return Result;
		}
		auto BufferRetention = N::ReserveBytes(P->Context, BufferBytes, Op);
		if (!BufferRetention)
		{
			return Result;
		}
		if (!N::FStreamAccess::CreatePipe(P->Context, BufferBytes, Pipe, Error, std::move(BufferRetention)))
		{
			N::Fail(Op, Error.Code, "Cannot allocate HTTP stream buffer");
			return Result;
		}
		auto Retention = ReserveRequest(P->Context, Request, Op);
		if (!Retention)
		{
			return Result;
		}
		Result = Backend::FAccess::Stream(N::FAccess::Wrap(Op), Pipe.Reader, Head);
		N::ObserveTerminal(Op,
		    [Weak = std::weak_ptr<Network::FWritableStream>(Pipe.Writer)](auto State, const auto& Failure)
		{
			if (auto Writer = Weak.lock())
			{
				if (State == Network::EOperationState::Succeeded)
				{
					Writer->Finish();
				}
				else
				{
					Network::FError Error;
					Error.Code = Failure.Code;
					Error.SystemCode = Failure.SystemCode;
					try
					{
						Error.Message = Failure.Message;
					}
					catch (...)
					{
					}
					Writer->Fail(std::move(Error));
				}
			}
		});
		N::FStreamAccess::OnCancel(*Pipe.Reader,
		    [Weak = std::weak_ptr<N::FOperation>(Op)]
		{
			if (auto Pending = Weak.lock())
			{
				N::FAccess::Wrap(std::move(Pending)).Cancel();
			}
		});
		N::Post(P->Context,
		    [P,
		        Op,
		        Retention,
		        Request = std::move(Request),
		        Output = std::move(Pipe.Writer),
		        Head]() mutable
		{
			P->Start(std::move(Request), Op, std::move(Retention), Output, std::move(Head));
			if (N::Terminal(N::State(Op)) && N::State(Op) != Network::EOperationState::Succeeded)
			{
				Output->Fail(N::FAccess::Wrap(Op).Error());
			}
		});
		return Result;
	}

	FCapabilities FCurlHttpClient::Capabilities() const
	{
		const auto* Info = curl_version_info(CURLVERSION_NOW);
		return {static_cast<bool>(Info->features & CURL_VERSION_SSL),
		    static_cast<bool>(Info->features & CURL_VERSION_HTTP2),
		    Info->version};
	}
	bool FCurlHttpClient::BelongsTo(const Network::FContext& Context) const noexcept
	{
		if (std::string_view(Context.GetFoundationIdentity()) != Network::Backend::FoundationIdentity()) return false;
		return this->Context == N::FAccess::Context(const_cast<Network::FContext&>(Context));
	}

	void FCurlHttpClient::RequestClose()
	{
		auto P = shared_from_this();
		N::Post(P->Context,
		    [P]
		{
			P->CloseOnIo();
		});
	}

	std::shared_ptr<Backend::IClient> CreateCurlClient(Network::FContext& Context, FClientOptions Options)
	{
		auto P = std::make_shared<FCurlHttpClient>(N::FAccess::Context(Context), std::move(Options));
		P->ResourceId = N::AddResource(P->Context, [Weak = std::weak_ptr<FCurlHttpClient>(P)]
		{
			if (auto Self = Weak.lock())
			{
				Self->CloseOnIo();
			}
		});
		if (!P->ResourceId)
		{
			throw Detail::FClientCreationError(N::IsClosed(P->Context) ? Network::EError::Closed : Network::EError::Limit);
		}
		return P;
	}

} // namespace VeroScript::Http

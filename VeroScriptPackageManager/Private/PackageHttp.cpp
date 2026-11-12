// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInternal.h"
#include <algorithm>
#include <climits>
#include <curl/curl.h>
#include <fstream>
#include <thread>

namespace VeroScript::Packages::Detail
{
	std::string EscapeUrl(const std::string& S)
	{
		static const char* H = "0123456789ABCDEF";
		std::string R;
		for (unsigned char C : S)
		{
			if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' ||
				C == '_' || C == '.' || C == '~')
			{
				R += char(C);
			}
			else
			{
				R += '%';
				R += H[C >> 4];
				R += H[C & 15];
			}
		}
		return R;
	}
	std::string SafeUrl(const std::string& S)
	{
		auto P = S.find_first_of("?#");
		auto R = S.substr(0, P);
		auto At = R.find('@');
		auto Start = R.find("://");
		if (At != std::string::npos && Start != std::string::npos)
		{
			R.erase(Start + 3, At - Start - 2);
		}
		return R;
	}
	struct FUrl
	{
		FUrl(const std::string& S, bool Http)
		{
			if (!Handle || curl_url_set(Handle, CURLUPART_URL, S.c_str(), 0) != CURLUE_OK)
			{
				Fail("invalid-url", "invalid URL");
			}
			const auto Get = [&](CURLUPart P)
			{
				char* V = nullptr;
				auto C = curl_url_get(Handle, P, &V, 0);
				std::string R = C == CURLUE_OK ? V : "";
				curl_free(V);
				return R;
			};
			Scheme = Get(CURLUPART_SCHEME);
			Host = Get(CURLUPART_HOST);
			Port = Get(CURLUPART_PORT);
			Path = Get(CURLUPART_PATH);
			Normalized = Get(CURLUPART_URL);
			if (Port.empty())
			{
				Port = Scheme == "https" ? "443" : "80";
			}
			if ((Scheme != "https" && (!Http || Scheme != "http")) || !Get(CURLUPART_USER).empty() ||
				!Get(CURLUPART_PASSWORD).empty() || !Get(CURLUPART_FRAGMENT).empty())
			{
				Fail("url-policy", "URLs require HTTPS, no user/password or fragments");
			}
		}
		~FUrl() = default;
		FUrl(const FUrl&) = delete;
		bool HasSafeCredentialPath() const
		{
			// URL parsing removes ordinary dot segments, but an encoded separator
			// can hide one until the server decodes its route. Never authorize that
			// ambiguous path, even when the raw spelling matches a credential scope.
			char* Raw = nullptr;
			auto Result = curl_url_get(Handle, CURLUPART_PATH, &Raw, CURLU_URLDECODE);
			std::unique_ptr<char, decltype(&curl_free)> Decoded(Raw, curl_free);
			if (Result != CURLUE_OK || !Raw)
			{
				return false;
			}
			std::string_view DecodedPath(Raw);
			for (std::size_t Begin = 0; Begin < DecodedPath.size();)
			{
				auto End = DecodedPath.find_first_of("/\\", Begin);
				auto Segment = DecodedPath.substr(Begin, End == std::string_view::npos ? End : End - Begin);
				if (Segment == "." || Segment == "..")
				{
					return false;
				}
				if (End == std::string_view::npos)
				{
					break;
				}
				Begin = End + 1;
			}
			return true;
		}

		std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> Owner{curl_url(), curl_url_cleanup};
		CURLU* Handle = Owner.get();
		std::string Scheme, Host, Port, Path, Normalized;
	};
	class FCurlTransport final : public IHttpTransport
	{
	public:
		//~ Begin IHttpTransport Interface
		bool AllowsPublicMetadataCache() const noexcept override
		{
			return true;
		}

		bool Send(const FHttpRequest& Request, FHttpResponse& Out, FDiagnostic& E) const override
		{
			try
			{
				Out = {};
				E = {};
				FUrl Origin(Request.Url, Request.AllowHttp);
				auto Scope = Origin.Path.substr(0, Origin.Path.rfind('/') + 1);
				std::string Url = Request.Url;
				for (unsigned Redirects = 0; Redirects <= 5; ++Redirects)
				{
					FUrl Current(Url, Request.AllowHttp);
					if (Origin.Scheme == "https" && Current.Scheme != "https")
					{
						Fail("redirect-policy", "HTTPS downgrade redirect is forbidden");
					}
					if (Request.CheckCancelled)
					{
						Request.CheckCancelled();
					}
					FTransfer T{Request};
					CURL* Raw = curl_easy_init();
					if (!Raw)
					{
						Fail("http-init", "cannot create HTTP request");
					}
					std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> C(Raw, curl_easy_cleanup);
					curl_easy_setopt(Raw, CURLOPT_URL, Current.Normalized.c_str());
					curl_easy_setopt(Raw, CURLOPT_PROTOCOLS_STR, Request.AllowHttp ? "http,https" : "https");
					curl_easy_setopt(Raw, CURLOPT_FOLLOWLOCATION, 0L);
					curl_easy_setopt(Raw, CURLOPT_NOSIGNAL, 1L);
					curl_easy_setopt(
						Raw, CURLOPT_CONNECTTIMEOUT_MS,
						static_cast<long>(std::min<std::uint64_t>(10000, Request.TimeoutMilliseconds)));
					curl_easy_setopt(
						Raw, CURLOPT_TIMEOUT_MS,
						static_cast<long>(std::min<std::uint64_t>(Request.TimeoutMilliseconds, LONG_MAX)));
					curl_easy_setopt(Raw, CURLOPT_SSL_VERIFYPEER, 1L);
					curl_easy_setopt(Raw, CURLOPT_SSL_VERIFYHOST, 2L);
					curl_easy_setopt(Raw, CURLOPT_USERAGENT, "VeroScriptPackageManager/0.1.1");
					if (!Request.Proxy.empty())
					{
						curl_easy_setopt(Raw, CURLOPT_PROXY, Request.Proxy.c_str());
					}
					if (!Request.CaFile.empty())
					{
						curl_easy_setopt(Raw, CURLOPT_CAINFO, Request.CaFile.c_str());
					}
					curl_easy_setopt(Raw, CURLOPT_WRITEFUNCTION, FTransfer::Write);
					curl_easy_setopt(Raw, CURLOPT_WRITEDATA, &T);
					curl_easy_setopt(Raw, CURLOPT_HEADERFUNCTION, FTransfer::Header);
					curl_easy_setopt(Raw, CURLOPT_HEADERDATA, &T);
					curl_easy_setopt(Raw, CURLOPT_NOPROGRESS, 0L);
					curl_easy_setopt(Raw, CURLOPT_XFERINFOFUNCTION, FTransfer::Progress);
					curl_easy_setopt(Raw, CURLOPT_XFERINFODATA, &T);
					std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> OwnedHeaders(
						nullptr, curl_slist_free_all);
					const bool SameScope = Origin.Scheme == Current.Scheme && Origin.Host == Current.Host &&
						Origin.Port == Current.Port && Current.Path.rfind(Scope, 0) == 0 &&
						(Redirects == 0 || Current.Path.find('%') == std::string::npos);
					const auto AddHeader = [&](const std::string& Name, const std::string& Value)
					{
						if (Name.empty() ||
							!std::all_of(Name.begin(), Name.end(),
								[](unsigned char C)
								{
									return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
										(C >= '0' && C <= '9') ||
										std::string("!#$%&'*+-.^_`|~").find(char(C)) != std::string::npos;
								}) ||
							std::any_of(Value.begin(), Value.end(),
								[](unsigned char C)
								{
									return (C < 32 && C != '\t') || C == 127;
								}))
						{
							Fail("http-header", "invalid header characters");
						}
						auto Added = curl_slist_append(OwnedHeaders.get(), (Name + ": " + Value).c_str());
						if (!Added)
						{
							Fail("http-memory", "cannot allocate request headers");
						}
						OwnedHeaders.release();
						OwnedHeaders.reset(Added);
					};
					for (const auto& H : Request.Headers)
					{
						auto K = H.first;
						std::transform(K.begin(), K.end(), K.begin(),
							[](unsigned char X)
							{
								return char(std::tolower(X));
							});
						if ((K == "authorization" && Request.Credential) ||
							(!SameScope && K != "content-type" && K != "accept" && K != "user-agent"))
						{
							continue;
						}
						AddHeader(H.first, H.second);
					}
					if (SameScope && Request.Credential && Current.HasSafeCredentialPath())
					{
						auto Token = Request.Credential(Current.Normalized);
						if (!Token.empty())
						{
							AddHeader("Authorization", Token);
						}
					}
					curl_easy_setopt(Raw, CURLOPT_HTTPHEADER, OwnedHeaders.get());
					if (!Request.BodyFile.empty())
					{
						auto P = U8Path(Request.BodyFile);
						NoLinks(P);
						T.Upload.open(P, std::ios::binary);
						if (!T.Upload)
						{
							Fail("upload-read", "cannot open upload file");
						}
						curl_easy_setopt(Raw, CURLOPT_UPLOAD, 1L);
						curl_easy_setopt(Raw, CURLOPT_READFUNCTION, FTransfer::ReadUpload);
						curl_easy_setopt(Raw, CURLOPT_READDATA, &T);
						curl_easy_setopt(Raw, CURLOPT_INFILESIZE_LARGE,
										 static_cast<curl_off_t>(fs::file_size(P)));
						curl_easy_setopt(Raw, CURLOPT_CUSTOMREQUEST, Request.Method.c_str());
					}
					else if (Request.Method != "GET")
					{
						curl_easy_setopt(Raw, CURLOPT_CUSTOMREQUEST, Request.Method.c_str());
						curl_easy_setopt(Raw, CURLOPT_POSTFIELDS, Request.Body.data());
						curl_easy_setopt(Raw, CURLOPT_POSTFIELDSIZE_LARGE,
										 static_cast<curl_off_t>(Request.Body.size()));
					}
					auto Code = curl_easy_perform(Raw);
					if (T.Error)
					{
						std::rethrow_exception(T.Error);
					}
					if (Code != CURLE_OK)
					{
						Fail("http-transfer", curl_easy_strerror(Code));
					}
					if (T.Response.Status >= 300 && T.Response.Status < 400 && T.Response.Status != 304)
					{
						if (Redirects == 5)
						{
							Fail("redirect-limit", "too many HTTP redirects");
						}
						auto L = T.Response.Headers.find("location");
						if (L == T.Response.Headers.end())
						{
							Fail("redirect-invalid", "redirect has no location");
						}
						if (Request.Method != "GET" && T.Response.Status != 307 && T.Response.Status != 308)
						{
							Fail("redirect-method", "upload redirects must preserve request method");
						}
						if (curl_url_set(Current.Handle, CURLUPART_URL, L->second.c_str(), 0) != CURLUE_OK)
						{
							Fail("redirect-invalid", "invalid redirect location");
						}
						char* Next = nullptr;
						if (curl_url_get(Current.Handle, CURLUPART_URL, &Next, 0) != CURLUE_OK)
						{
							Fail("redirect-invalid", "cannot resolve redirect URL");
						}
						Url = Next;
						curl_free(Next);
						continue;
					}
					Out = std::move(T.Response);
					return true;
				}
				return false;
			}
			catch (const FFailure& X)
			{
				E = X.Diagnostic;
				Out = {};
				return false;
			}
			catch (const std::exception& X)
			{
				E.Code = "http-error";
				E.Message = X.what();
				Out = {};
				return false;
			}
		}
		//~ End IHttpTransport Interface

		FCurlTransport()
		{
			static const auto Init = curl_global_init(CURL_GLOBAL_DEFAULT);
			if (Init != CURLE_OK)
			{
				Fail("http-init", "libcurl initialization failed");
			}
		}

	private:
		struct FTransfer
		{
			static size_t Write(char* Data, size_t Size, size_t Count, void* P)
			{
				auto& T = *static_cast<FTransfer*>(P);
				try
				{
					if (T.Request.CheckCancelled)
					{
						T.Request.CheckCancelled();
					}
					if (Size && Count > std::numeric_limits<size_t>::max() / Size)
					{
						Fail("http-limit", "HTTP size overflow");
					}
					auto N = Size * Count;
					if (N > T.Request.MaxBytes - T.Bytes)
					{
						Fail("http-limit", "HTTP response exceeds its budget");
					}
					T.Bytes += N;
					if (T.Response.Status >= 300 && T.Response.Status < 400)
					{
						return N;
					}
					if (T.Request.DataSink)
					{
						T.Request.DataSink(Data, N);
					}
					else
					{
						T.Response.Body.append(Data, N);
					}
					return N;
				}
				catch (...)
				{
					T.Error = std::current_exception();
					return 0;
				}
			}

			static size_t Header(char* Data, size_t Size, size_t Count, void* P)
			{
				auto& T = *static_cast<FTransfer*>(P);
				try
				{
					if (Size && Count > std::numeric_limits<size_t>::max() / Size)
					{
						Fail("http-header", "header size overflow");
					}
					auto N = Size * Count;
					if (N > (1u << 20) - T.HeaderBytes)
					{
						Fail("http-header", "aggregate HTTP headers exceed budget");
					}
					T.HeaderBytes += N;
					if (N > 65536)
					{
						Fail("http-header", "HTTP header is too large");
					}
					std::string S(Data, N);
					if (S.rfind("HTTP/", 0) == 0)
					{
						auto Space = S.find(' ');
						T.Response.Status = static_cast<unsigned>(std::stoul(S.substr(Space + 1, 3)));
						T.Response.Headers.clear();
					}
					else
					{
						auto Colon = S.find(':');
						if (Colon != std::string::npos)
						{
							auto K = S.substr(0, Colon);
							std::transform(K.begin(), K.end(), K.begin(),
								[](unsigned char C)
								{
									return char(std::tolower(C));
								});
							// HTTP optional whitespace includes SP and HTAB. Locate both
							// ends once instead of shifting the value for every leading SP.
							const auto Begin = S.find_first_not_of(" \t", Colon + 1);
							const auto End = S.find_last_not_of(" \t\r\n");
							auto V = Begin == std::string::npos || End < Begin ? std::string{}
								: S.substr(Begin, End - Begin + 1);
							if (T.Response.Headers.size() >= 256)
							{
								Fail("http-header", "too many HTTP headers");
							}
							T.Response.Headers[K] = std::move(V);
						}
					}
					return N;
				}
				catch (...)
				{
					T.Error = std::current_exception();
					return 0;
				}
			}

			static int Progress(void* P, curl_off_t Total, curl_off_t Now, curl_off_t, curl_off_t)
			{
				auto& T = *static_cast<FTransfer*>(P);
				try
				{
					if (T.Request.CheckCancelled)
					{
						T.Request.CheckCancelled();
					}
					if (T.Request.Progress)
					{
						T.Request.Progress(static_cast<std::uint64_t>(Now),
										   static_cast<std::uint64_t>(Total));
					}
					return 0;
				}
				catch (...)
				{
					T.Error = std::current_exception();
					return 1;
				}
			}

			static size_t ReadUpload(char* Data, size_t Size, size_t Count, void* P)
			{
				auto& T = *static_cast<FTransfer*>(P);
				try
				{
					if (T.Request.CheckCancelled)
					{
						T.Request.CheckCancelled();
					}
					T.Upload.read(Data, static_cast<std::streamsize>(Size * Count));
					return static_cast<size_t>(T.Upload.gcount());
				}
				catch (...)
				{
					T.Error = std::current_exception();
					return CURL_READFUNC_ABORT;
				}
			}

			const FHttpRequest& Request;

			FHttpResponse Response;

			std::uint64_t Bytes = 0, HeaderBytes = 0;

			std::exception_ptr Error;

			std::ifstream Upload;
		};
	};
} // namespace VeroScript::Packages::Detail
namespace VeroScript::Packages
{
	std::shared_ptr<const IHttpTransport> CreateHttpTransport()
	{
		return std::make_shared<Detail::FCurlTransport>();
	}
} // namespace VeroScript::Packages

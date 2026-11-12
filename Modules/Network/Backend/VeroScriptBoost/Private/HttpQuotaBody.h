// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkProvider.h"
#include <boost/beast/http.hpp>
#include <algorithm>
#include <stdexcept>

namespace VeroScript::Http::Detail
{
	// BodyReader backed by the public byte vector. Reserve quota before growing
	// either known-length or chunked request bodies; do not preallocate a limit.
	struct FQuotaBody
	{
		struct value_type
		{
			std::shared_ptr<Network::Detail::FByteLease> Retention;
			Network::FBytes Bytes;
			std::uint64_t Limit = 0;
		};
		class reader
		{
		public:
			template <bool Request, class Fields>
			reader(boost::beast::http::header<Request, Fields>&, value_type& Body) : Body(Body)
			{
			}
			void init(const boost::optional<std::uint64_t>& Length, Network::Detail::FSystemError& Error)
			{
				Error = {};
				if (Length && *Length > Body.Limit)
				{
					Error = boost::beast::http::error::body_limit;
				}
			}
			template <class Buffers>
			std::size_t put(const Buffers& Input, Network::Detail::FSystemError& Error)
			{
				Error = {};
				const auto Count = boost::beast::buffer_bytes(Input);
				const auto Size = Body.Bytes.size();
				if (Count > Body.Limit - std::min<std::uint64_t>(Size, Body.Limit) ||
					Count > Body.Bytes.max_size() - Size)
				{
					Error = boost::beast::http::error::body_limit;
					return 0;
				}
				const auto Needed = Size + Count;
				try
				{
					if (Needed > Body.Bytes.capacity())
					{
						const auto Limit = static_cast<std::size_t>(Body.Limit);
						const auto Current = Body.Bytes.capacity();
						const auto Growth = Current > Limit / 2 ? Limit : std::max<std::size_t>(16384, Current * 2);
						const auto Capacity = std::min(Limit, std::max(Needed, Growth));
						if (!Body.Retention || !Body.Retention->TryResize(Capacity))
						{
							Error = boost::beast::http::error::buffer_overflow;
							return 0;
						}
						Body.Bytes.reserve(Capacity);
					}
					Body.Bytes.resize(Needed);
					return Count ? Network::Detail::Asio::buffer_copy(
									   Network::Detail::Asio::buffer(Body.Bytes.data() + Size, Count), Input)
								 : 0;
				}
				catch (const std::bad_alloc&)
				{
					Error = boost::system::errc::make_error_code(boost::system::errc::not_enough_memory);
				}
				catch (const std::length_error&)
				{
					Error = boost::beast::http::error::buffer_overflow;
				}
				return 0;
			}
			void finish(Network::Detail::FSystemError& Error)
			{
				Error = {};
			}

		private:
			value_type& Body;
		};
	};
} // namespace VeroScript::Http::Detail

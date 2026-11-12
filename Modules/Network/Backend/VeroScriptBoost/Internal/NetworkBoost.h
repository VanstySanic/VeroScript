// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

// Resolve Boost.Config's generated include paths before renaming its namespace.
// Only the scalar configuration aliases below are declared by that header.
#include <boost/config.hpp>

namespace veroscript_network_boost
{
	#if defined(BOOST_HAS_LONG_LONG)
	using ::boost::long_long_type;
	using ::boost::ulong_long_type;
	#endif
	#if defined(BOOST_HAS_INT128)
	using ::boost::int128_type;
	using ::boost::uint128_type;
	#endif
	#if defined(BOOST_HAS_FLOAT128)
	using ::boost::float128_type;
	#endif
} // namespace veroscript_network_boost

// This header is forced into private backend translation units only.
#define boost veroscript_network_boost

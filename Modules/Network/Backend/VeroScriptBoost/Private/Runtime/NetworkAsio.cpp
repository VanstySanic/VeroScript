// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Keep Asio's scheduler, reactor and thread-local state in one runtime shared by
// all network providers, including providers loaded as separate native modules.
#include <boost/asio/impl/src.hpp>
#if VEROSCRIPT_NETWORK_TLS
#include <boost/asio/ssl/impl/src.hpp>
#endif

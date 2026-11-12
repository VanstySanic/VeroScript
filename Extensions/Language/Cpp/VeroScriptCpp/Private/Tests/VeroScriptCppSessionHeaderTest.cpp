// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSession.h"

#include <cstddef>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<VeroScript::VeroScriptSession>);
static_assert(std::is_aggregate_v<VeroScript::FSessionJitCacheLimits>);
static_assert(std::is_aggregate_v<VeroScript::FSessionJitCacheStats>);
static_assert(std::is_same_v<
	decltype(std::declval<const VeroScript::VeroScriptSession&>().GetJitCacheLimits()),
	VeroScript::FSessionJitCacheLimits>);
static_assert(std::is_same_v<
	decltype(std::declval<const VeroScript::VeroScriptSession&>().GetJitCacheStats()),
	VeroScript::FSessionJitCacheStats>);

constexpr VeroScript::FSessionJitCacheLimits DefaultSessionJitLimits;
constexpr VeroScript::FSessionJitCacheStats DefaultSessionJitStats;
static_assert(DefaultSessionJitLimits.MaxNativeBytes == 8u * 1024u * 1024u);
static_assert(DefaultSessionJitLimits.MaxRetainedPayloadBytes == 32u * 1024u * 1024u);
static_assert(DefaultSessionJitStats.CompileAttempts == 0);
static_assert(DefaultSessionJitStats.RetainedPayloadBytes == 0);
static_assert(DefaultSessionJitStats.NativeBytes == 0);
static_assert(!DefaultSessionJitStats.bResourceLimitReached);
static_assert(std::is_abstract_v<VeroScript::ISessionDebugBridge>);
static_assert(std::is_same_v<
	decltype(std::declval<const VeroScript::VeroScriptSession&>().GetDebugProgram()),
	VeroScript::FSessionDebugProgramView>);

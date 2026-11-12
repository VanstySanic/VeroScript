// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpRouter.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace H = VeroScript::Http;
namespace N = VeroScript::Network;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #X); \
		} \
	} \
	while (false)
static H::FRouteMatch Match(const H::FRouteSnapshot& Table, std::string_view Method, std::string_view Target)
{
	H::FRouteMatch Result;
	N::FError Error;
	CHECK(Table.TryMatch(Method, Target, Result, Error));
	CHECK(!Error);
	return Result;
}
int main()
{
	try
	{
		H::FRouter Router;
		N::FError Error;
		CHECK(Router.TryAdd({1, "GET", "/users/:id"}, Error));
		CHECK(Router.TryAdd({2, "GET", "/users/me"}, Error));
		CHECK(Router.TryAdd({3, "GET", "/users/*tail"}, Error));
		CHECK(Router.TryAdd({4, "POST", "/users/:id"}, Error));
		CHECK(Router.TryAdd({5, "*", "/users/:id"}, Error));
		CHECK(!Router.TryAdd({6, "GET", "/users/:other"}, Error) && Error.Code == N::EError::State);
		CHECK(!Router.TryAdd({6, "GET", "/users/%6de"}, Error));
		CHECK(!Router.TryAdd({7, "GET", "/:id/:id"}, Error));
		CHECK(!Router.TryAdd({7, "GET", "/a/*tail/b"}, Error));
		CHECK(!Router.TryAdd({7, "GET", "/a?b"}, Error));
		CHECK(!Router.TryAdd({7, "GET", "/a%"}, Error));
		const auto Old = Router.Snapshot();
		CHECK(Old.Size() == 5);
		CHECK(Match(Old, "GET", "/users/me").Id == 2);
		CHECK(Match(Old, "GET", "/users/17").Id == 1);
		CHECK(Match(Old, "POST", "/users/17").Id == 4);
		CHECK(Match(Old, "PATCH", "/users/17").Id == 5);
		CHECK(Match(Old, "GET", "/users/a/b").Id == 3);
		CHECK(!Match(Old, "GET", "/users").bFound);
		auto Result = Match(Old, "GET", "/users/a%2Fb?key&key=&key=x%2By&key=a+b");
		CHECK(Result.Parameters.size() == 1 && Result.Parameters[0].first == "id" &&
		    Result.Parameters[0].second == "a/b");
		CHECK(Result.Path == "/users/a%2Fb" && Result.Query.size() == 4 && !Result.Query[0].bHasEquals &&
		    Result.Query[1].bHasEquals);
		CHECK(Result.Query[2].Value == "x+y" && Result.Query[3].Value == "a+b");
		CHECK(Match(Old, "GET", "/users/%252F").Parameters[0].second == "%2F");
		CHECK(Match(Old, "GET", "/users/%E4%B8%AD%E6%96%87").Parameters[0].second == "\xe4\xb8\xad\xe6\x96\x87");
		CHECK(Match(Old, "GET", "/users/a//").Parameters[0].second == "a//");
		Result.Id = 99;
		CHECK(!Old.TryMatch("GET", "/users/%zz", Result, Error) && Result.Id == 99);
		CHECK(!Old.TryMatch("GET", "/users/a#fragment", Result, Error) && Result.Id == 99);
		bool Removed = false;
		CHECK(Router.TryRemove(2, Removed, Error) && Removed);
		CHECK(Match(Router.Snapshot(), "GET", "/users/me").Id == 1 && Match(Old, "GET", "/users/me").Id == 2);
		CHECK(Router.TryRemove(100, Removed, Error) && !Removed);
		H::FRouteLimits Limits;
		Limits.MaxRoutes = 1;
		Limits.MaxQueryParameters = 1;
		Limits.MaxTargetBytes = 16;
		H::FRouter Small(Limits);
		CHECK(Small.TryAdd({1, "GET", "/"}, Error));
		CHECK(!Small.TryAdd({2, "GET", "/other"}, Error) && Error.Code == N::EError::Limit);
		CHECK(!Small.Snapshot().TryMatch("GET", "/?a&b", Result, Error) && Error.Code == N::EError::Limit);
		CHECK(!Small.Snapshot().TryMatch("GET", "/abcdefghijklmnop", Result, Error));
		std::atomic<bool> Done{false}, Good{true};
		std::thread Reader(
		    [&]
		{
			while (!Done)
			{
				H::FRouteMatch R;
				N::FError E;
				if (!Old.TryMatch("GET", "/users/me", R, E) || R.Id != 2)
				{
					Good = false;
				}
				const auto Live = Router.Snapshot();
				if (!Live.TryMatch("GET", "/users/1", R, E) || R.Id != 1)
				{
					Good = false;
				}
			}
		});
		for (unsigned I = 0; I < 128; ++I)
		{
			if (!Router.TryAdd({100 + I, "GET", "/route-" + std::to_string(I)}, Error))
			{
				Good = false;
			}
		}
		Done = true;
		Reader.join();
		CHECK(Good);
		std::cout << "HTTP route snapshots, matching and conflict tests passed\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
}

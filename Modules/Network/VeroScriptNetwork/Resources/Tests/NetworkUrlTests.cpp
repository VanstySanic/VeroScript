// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNetwork.h"
#include <iostream>
#include <utility>

int main()
{
	using namespace VeroScript::Network;
	// RFC 3986 section 5.4; authority-only network URLs use our explicit '/' path.
	const std::pair<std::string_view, std::string_view> Cases[] = {{"g", "http://a/b/c/g"},
		{"./g", "http://a/b/c/g"},
		{"g/", "http://a/b/c/g/"},
		{"/g", "http://a/g"},
		{"//g", "http://g/"},
		{"?y", "http://a/b/c/d;p?y"},
		{"g?y", "http://a/b/c/g?y"},
		{"#s", "http://a/b/c/d;p?q#s"},
		{"g#s", "http://a/b/c/g#s"},
		{"g?y#s", "http://a/b/c/g?y#s"},
		{";x", "http://a/b/c/;x"},
		{"g;x", "http://a/b/c/g;x"},
		{"g;x?y#s", "http://a/b/c/g;x?y#s"},
		{"", "http://a/b/c/d;p?q"},
		{".", "http://a/b/c/"},
		{"./", "http://a/b/c/"},
		{"..", "http://a/b/"},
		{"../", "http://a/b/"},
		{"../g", "http://a/b/g"},
		{"../..", "http://a/"},
		{"../../", "http://a/"},
		{"../../g", "http://a/g"},
		{"../../../g", "http://a/g"},
		{"../../../../g", "http://a/g"},
		{"/./g", "http://a/g"},
		{"/../g", "http://a/g"},
		{"g.", "http://a/b/c/g."},
		{".g", "http://a/b/c/.g"},
		{"g..", "http://a/b/c/g.."},
		{"..g", "http://a/b/c/..g"},
		{"./../g", "http://a/b/g"},
		{"./g/.", "http://a/b/c/g/"},
		{"g/./h", "http://a/b/c/g/h"},
		{"g/../h", "http://a/b/c/h"},
		{"g;x=1/./y", "http://a/b/c/g;x=1/y"},
		{"g;x=1/../y", "http://a/b/c/y"},
		{"g?y/./x", "http://a/b/c/g?y/./x"},
		{"g?y/../x", "http://a/b/c/g?y/../x"},
		{"g#s/./x", "http://a/b/c/g#s/./x"},
		{"g#s/../x", "http://a/b/c/g#s/../x"},
		{"%2e/%2E%2E/g", "http://a/b/c/%2e/%2E%2E/g"},
		{"g//h", "http://a/b/c/g//h"},
		{"?", "http://a/b/c/d;p?"},
		{"#", "http://a/b/c/d;p?q#"},
		{"?#", "http://a/b/c/d;p?#"},
		{"https://B:8443/a/../z?x#f", "https://b:8443/z?x#f"},
		{"//[::1]:8443/a/./b", "http://[::1]:8443/a/b"}};
	FError Error;
	FUrl Out;
	for (const std::string_view Host : {"::", "::1", "2001:db8::1", "1:2:3:4:5:6:7:8", "::ffff:192.0.2.1", "1:2:3:4:5:6:192.0.2.1"})
	{
		if (!FUrl::TryParse("http://[" + std::string(Host) + "]/", Out, Error) || !Out.bIpv6) return 8;
	}
	for (const std::string_view Host : {"", ":", ":::1", "1::2::3", "1:2:3", "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7:8::", "::ffff:256.0.2.1", "::ffff:192.0.2.", "::ffff:019.0.2.1"})
	{
		const auto Previous = Out.String();
		if (FUrl::TryParse("http://[" + std::string(Host) + "]/", Out, Error) || !Error || Out.String() != Previous) return 9;
	}
	for (const auto& Case : Cases)
	{
		if (!FUrl::TryResolve("http://a/b/c/d;p?q#old", Case.first, Out, Error) || Error || Out.String() != Case.second)
		{
			std::cerr << "Resolution failed: " << Case.first << " -> " << Out.String() << " " << Error.Message << '\n';
			return 1;
		}
	}
	for (const std::string_view Invalid : {"g:h", "http:g", "//", "//a:0", "a%xx/../ok", "bad\\/../ok", "%/../ok"})
	{
		const auto Before = Out.String();
		if (FUrl::TryResolve("http://a/b/", Invalid, Out, Error) || !Error || Out.String() != Before)
		{
			return 2;
		}
	}
	if (!FUrl::TryResolve("https://a", "b", Out, Error) || Out.String() != "https://a/b")
	{
		return 3;
	}
	if (!FUrl::TryResolve("https://a/?old", "", Out, Error) || !Out.bHasQuery || Out.Query != "old")
	{
		return 4;
	}
	const auto Before = Out.String();
	if (FUrl::TryResolve("relative/base", "b", Out, Error) || Out.String() != Before)
	{
		return 5;
	}
	for (unsigned Code = 0; Code <= static_cast<unsigned>(EError::Internal); ++Code)
	{
		FError Failure;
		Failure.Code = static_cast<EError>(Code);
		const std::string Id = Failure.Id();
		if (Code == 0 ? !Id.empty() : Id.rfind("network.", 0) != 0 || Id == "network.unknown")
		{
			return 6;
		}
	}
	FError Unknown;
	Unknown.Code = static_cast<EError>(255);
	if (std::string(Unknown.Id()) != "network.unknown")
	{
		return 7;
	}
	std::cout << "Network URL resolution and stable diagnostic IDs passed\n";
	return 0;
}

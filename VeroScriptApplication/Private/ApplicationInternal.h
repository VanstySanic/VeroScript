// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptArchive.h"
#include "VeroScriptPath.h"
#include "json/json.hpp"

#include <filesystem>
#include <set>
#include <stdexcept>

namespace VeroScript::Application::Detail
{
	using J = nlohmann::json;
	namespace fs = std::filesystem;
	struct FFailure : std::runtime_error
	{
		FFailure(std::string Code, std::string Message, std::string Path = {});
		FDiagnostic Diagnostic;
	};
	[[noreturn]] void Fail(const std::string&, const std::string&, const std::string& = {});
	void Check(const FLimits&);
	J Parse(std::string_view, const FLimits&);
	void Keys(const J&, std::initializer_list<const char*>, const std::string&);
	std::uint64_t Count(const J&, const std::string&);
	std::string Fold(std::string);
	bool Under(const fs::path&, const fs::path&);
	void Path(const std::string&);
	void Physical(const fs::path&, bool Missing = false);
	fs::path TreePath(const fs::path&);
	std::vector<std::uint8_t> ReadFile(const fs::path&, const FLimits&);
	void WriteFile(const fs::path&, const std::vector<std::uint8_t>&);
	// The caller exclusively creates Root. Each logical directory must then
	// create a new physical directory instead of merging with another spelling.
	class FContentTreeWriter
	{
	public:
		explicit FContentTreeWriter(fs::path InRoot) : Root(std::move(InRoot))
		{
		}
		void Write(const std::string& Relative, const std::vector<std::uint8_t>&);

	private:
		void Directory(const fs::path& Relative);
		fs::path Root;
		std::set<std::string> Directories;
	};
	std::vector<std::uint8_t> Bytes(std::string_view);
}

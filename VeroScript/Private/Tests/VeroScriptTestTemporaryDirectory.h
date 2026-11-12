// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace VeroScript::Tests
{
	class FScopedTemporaryDirectory final
	{
	public:
		explicit FScopedTemporaryDirectory(const std::string_view Prefix)
		{
			std::error_code Error;
			const std::filesystem::path TemporaryRoot =
				std::filesystem::temp_directory_path(Error);
			if (Error)
			{
				return;
			}

			const auto Stamp = std::chrono::high_resolution_clock::now()
				.time_since_epoch()
								   .count();
			for (std::size_t Attempt = 0; Attempt < 1024; ++Attempt)
			{
				const std::filesystem::path Candidate = TemporaryRoot
					/ (std::string(Prefix) + std::to_string(Stamp)
						+ "-" + std::to_string(Attempt));
				Error.clear();
				if (std::filesystem::create_directory(Candidate, Error))
				{
					Directory = Candidate;
					return;
				}
				if (Error && Error != std::errc::file_exists)
				{
					return;
				}
			}
		}

		~FScopedTemporaryDirectory() noexcept
		{
			try
			{
				if (Directory.empty())
				{
					return;
				}
				std::error_code Error;
				std::filesystem::remove_all(Directory, Error);
			}
			catch (...)
			{
			}
		}

		FScopedTemporaryDirectory(const FScopedTemporaryDirectory&) = delete;
		FScopedTemporaryDirectory& operator=(const FScopedTemporaryDirectory&) = delete;

		explicit operator bool() const noexcept
		{
			return !Directory.empty();
		}

		const std::filesystem::path& Get() const noexcept
		{
			return Directory;
		}

	private:
		std::filesystem::path Directory;
	};
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// standard_namespace.vs - recommended std:: spellings
// Run from this directory: veroscript standard_namespace.vs

import "core.vs";

void main()
{
	std::array<int> values = {2, 3, 5};
	std::map<string, int> scores = {"alpha": 7, "beta": 11};
	std::optional<int> selected = std::optional<int>(scores["beta"]);
	std::readonly<int> publishedScore = std::readonly<int>(selected.get());
	// Explicit conversions either produce the requested value or, with
	// try_cast, an optional value that can be queried without an error path.
	double exactScore = cast<double>(scores["beta"]);
	std::optional<int> narrowed = try_cast<int>(exactScore);

	values.push(selected.value_or(0) + narrowed.value_or(0));
	const std::array<int> explicitlyUnlocked = {13};
	const_cast<std::array<int>>(explicitlyUnlocked).push(publishedScore.get());
	std::print("values: {}, {}, {}, {}", values[0], values[1], values[2], values[3]);
	std::print(std::format("selected score: {}", selected.get()));
	std::print("published score: {}", publishedScore.get());
	std::print("explicitly unlocked size: {}", std::len(explicitlyUnlocked));
	std::print("larger score: {}", std::max(scores["alpha"], scores["beta"]));
	std::print("square root: {}", std::sqrt(81.0));
	std::print("rounded power: {}", std::round(std::pow(2.0, 3.0)));
	std::print("finite logarithm: {}", std::isfinite(std::log(1.0)));
	std::print("trimmed: {}", std::strings::trim("  ready  "));
	std::print("last separator: {}", std::strings::last_find("a/b/c", "/"));
	std::print("file: {}", std::filesystem::basename("assets/data.txt"));
	std::print("current path is a directory: {}", std::filesystem::is_directory("."));
	std::print("elapsed clock is available: {}", std::chrono::now() > 0.0);
	std::print("monotonic clock is available: {}", std::chrono::steady_now() > 0.0);
	std::console::write("console output: ");
	std::console::flush();
	std::print("ready");
	std::print("interactive terminal: {}", std::console::is_interactive());
}

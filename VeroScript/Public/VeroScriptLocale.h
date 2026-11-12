// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Locale
{
	/**
	 * Select the best loaded catalog for a BCP 47 language tag. Tags from OS
	 * locale APIs (for example `ja_JP.UTF-8`) are normalized before matching.
	 * Exact matches win, followed by progressively broader and same-language
	 * matches. Returns false when no catalog for the requested language exists.
	 */
	bool TrySetLanguageFromTag(std::string_view Tag);
	std::string GetLanguageTag();
	std::vector<std::string> GetAvailableLanguageTags();
	// VERO_LANG overrides the OS/user locale; unsupported locales fall back to English.
	void InitializeFromEnvironment();
	// A module may add its own Resources/Localization catalogs when initialization
	// fell back to a source checkout. ModulePath is relative to the source module.
	// Explicit and deployed catalogs determine their own complete resource set.
	void InitializeModuleFromEnvironment(std::string_view ModulePath);

	/**
	 * Replace the process-wide catalog from a root containing
	 * one qualified canonical BCP 47 directory per language (`en-US`, `zh-TW`, `ja-JP`, ...).
	 * Culture directories and JSON files are discovered automatically. Files are
	 * loaded recursively in relative-path order. en-US is authoritative; missing
	 * translated files/keys fall back to en-US. Extra translated files/keys,
	 * non-canonical/duplicate culture directories, and duplicate message ids are rejected.
	 */
	bool LoadCatalogDirectory(std::string_view Directory, std::string* OutError = nullptr);

	/**
	 * Merge a catalog tree into the process-wide active catalogs
	 * (per-module / plugin locale).
	 * Unlike LoadCatalogDirectory (which replaces), this augments the current catalogs so
	 * multiple modules can each contribute their own keys. Existing keys are preserved
	 * unless bOverride is true. Ensures the base catalog is loaded first. Merged entries
	 * remain available for delayed diagnostic rendering after a module loader is destroyed.
	 * Public locale operations are thread-safe.
	 */
	bool MergeCatalogDirectory(
		std::string_view Directory,
		bool bOverride = false,
		std::string* OutError = nullptr);

	// Active-language catalog + {0}.. args.
	std::string TranslateId(
		std::string_view MessageId,
		const std::vector<std::string_view>& Args = {});

	// English (en-US catalog) template + args - stored on FScriptError::Message.
	std::string FormatEnglish(
		std::string_view MessageId,
		const std::vector<std::string_view>& Args = {});

	// Legacy: translate a canonical English diagnostic / prefix+suffix.
	std::string Translate(std::string_view EnglishMessage);

	// Locale-aware rendering of a diagnostic (prefers MessageId).
	std::string FormatDiagnosticMessage(
		std::string_view MessageId,
		const std::vector<std::string>& MessageArgs,
		std::string_view EnglishFallback);

	/**
	 * Resolve a rendered English diagnostic back to MessageId + args using the en-US catalog.
	 * Supports exact matches and `{0}`..`{9}` template matches (most specific wins).
	 */
	bool TryResolveEnglish(
		std::string_view EnglishMessage,
		std::string& OutMessageId,
		std::vector<std::string>& OutArgs);
}

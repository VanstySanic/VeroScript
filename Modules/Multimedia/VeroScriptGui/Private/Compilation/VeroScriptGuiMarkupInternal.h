// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiMarkup.h"
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>

namespace VeroScript::Gui
{
	class FMarkupLocator
	{
	public:
		explicit FMarkupLocator(const FMarkupSource& Source);
		FMarkupLocation At(std::size_t Offset);

	private:
		const FMarkupSource& Source;
		FMarkupLocation Position;
	};

	enum class ESelectorState : std::uint8_t
	{
		Root,
		Hover,
		Active,
		Checked,
		ReadOnly,
		Focus,
		FocusWithin,
		Disabled
	};

	struct FMarkupAttribute
	{
		std::string Name, Value;
		FMarkupLocation Location;
		FMarkupLocation ValueLocation;
		std::vector<FMarkupTextSpan> ValueSpans;
	};

	struct FMarkupElement
	{
		std::string Name, Text;
		FMarkupLocation Location, TextLocation;
		std::vector<FMarkupAttribute> Attributes;
		std::vector<FMarkupElement> Children;
		std::vector<FMarkupTextSpan> TextSpans;
	};

	struct FSelectorPart
	{
		std::string Type, Id;
		std::vector<std::string> Classes;
		std::vector<ESelectorState> States;
		// Relation to the previous (ancestor) part: '>' or ' '.
		char Relation = ' ';
	};

	struct FStyleSelector
	{
		std::vector<FSelectorPart> Parts;
		std::array<std::size_t, 3> Specificity{};
	};

	struct FStyleDeclaration
	{
		std::string Name, Value;
		FMarkupLocation Location;
		FResourceResolver ResolveResource;
		// Immutable literal builtin values are parsed with the sheet, shared safely
		// by contexts. Variables, resource resolution and custom codecs stay dynamic.
		std::optional<std::pair<EProperty, FPropertyValue>> Literal;
	};

	struct FStyleRule
	{
		std::vector<FStyleSelector> Selectors;
		std::vector<FStyleDeclaration> Declarations;
	};

	struct FStyleSheetData
	{
		FMarkupSource Source;
		std::vector<FStyleRule> Rules;
	};

	struct FMarkupUnit
	{
		FMarkupSource Source;
		FMarkupElement Root;
		std::string Type, Mark, Base, DefaultId;
		std::vector<std::shared_ptr<const FStyleSheetData>> Styles;
		std::vector<std::string> Imports;
		FMarkupScript Script;
		bool bComponent = false, bHasScript = false;
	};

	struct FMarkupData
	{
		std::string Entry;
		std::map<std::string, FMarkupUnit> Units;
		std::map<std::string, std::string> Components;
		std::map<std::string, FMarkupType> Types;
		std::vector<FMarkupSource> Dependencies;
		FMarkupCompileOptions Options;
		bool bDeferSchema = false;
	};

	struct FMarkupRuntime : std::enable_shared_from_this<FMarkupRuntime>
	{
		struct FScope;
		struct FNode
		{
			std::weak_ptr<FWidget> Widget;
			std::string Type, Mark;
			std::vector<std::shared_ptr<FScope>> Scopes;
			std::map<std::uint64_t, std::pair<std::string, std::string>> ScopeTypes;
		};
		struct FScope
		{
			std::weak_ptr<FWidget> Root;
			std::vector<std::size_t> Nodes;
			std::vector<std::shared_ptr<const FStyleSheetData>> Styles;
			std::map<std::string, std::weak_ptr<FWidget>> Names;
			std::shared_ptr<IMarkupScriptContext> Script;
			std::uint64_t Id = 0;
			bool bPage = false;
		};

		bool Apply(FContext& Context, std::uint64_t Round, std::string& Error);
		static std::shared_ptr<FMarkupRuntime> BuildScene(const std::vector<std::shared_ptr<FWidget>>& Widgets,
														  std::shared_ptr<const FStyleSheet> Theme = {},
														  std::shared_ptr<FMarkupRuntime> Previous = {});
		bool SynchronizeScripts(FContext& Context, std::string& Error);
		bool ValidateFactoryWidget(const FWidget& Widget, std::string& Error) const;
		void Attach(const std::shared_ptr<FWidget>& Widget, const std::string& Type, const std::string& Mark,
					const std::shared_ptr<FScope>& Scope);
		bool CheckId(const FWidget& Widget, const std::string& Id, std::string& Error) const;
		void IdChanged(const std::shared_ptr<FWidget>& Widget, const std::string& Previous);
		std::shared_ptr<FWidget> Find(const std::shared_ptr<FWidget>& Root, const std::string& Id) const;
		bool Match(const FStyleSelector& Selector, const FNode& Node, const FScope& Scope, FContext& Context) const;
		bool MatchPart(const FSelectorPart& Part, const FNode& Node, const FScope& Scope, FContext& Context) const;

		std::vector<FNode> Nodes;
		std::unordered_map<const FWidget*, std::size_t> NodeIndices;
		std::vector<std::shared_ptr<FScope>> Scopes;
		std::shared_ptr<const FStyleSheetData> Theme;
		std::thread::id Thread = std::this_thread::get_id();
		std::weak_ptr<FContext> LastContext;
		std::weak_ptr<FContext> LastScriptContext;
		std::uint64_t LastScriptFrame = 0;
		std::uint64_t LastRound = 0;
		bool bCompiledStyle = false;

		// Cache only scene structure and selector winners. Value decoding and
		// property evaluation still run each round, including user callbacks.
		struct FSceneSource
		{
			std::weak_ptr<FWidget> Widget, Parent;
			std::weak_ptr<FMarkupRuntime> Markup;
			std::uint64_t NameScope = 0;
			std::vector<std::tuple<std::uint64_t, std::string, std::string>> Memberships;
			std::map<std::uint64_t, std::vector<std::shared_ptr<const FStyleSheet>>> Sheets;
		};
		struct FSelectorSource
		{
			std::string Id;
			std::vector<std::string> Classes;
			std::array<std::uint64_t, 4> Eligibility{};
			std::uint64_t StyleMutation = 0;
			EControlKind Kind = EControlKind::None;
			bool bChecked = false, bReadOnly = false, bOpen = false, bContentVisible = false;
		};
		struct FWinner
		{
			std::tuple<std::size_t, std::array<std::size_t, 3>, std::size_t> Priority;
			const FStyleDeclaration* Declaration = nullptr;
		};
		bool MatchesScene(const std::vector<std::shared_ptr<FWidget>>& Widgets,
						  const std::shared_ptr<const FStyleSheet>& ActiveTheme) const;
		std::vector<FSceneSource> SceneSources;
		std::vector<FSelectorSource> SelectorSources;
		std::vector<std::map<std::string, FWinner>> SelectorWinners;
		std::vector<std::uint64_t> SelectorInput;
		bool bSelectorCache = false;
		bool bEmptyStylesCleared = false;
	};

	FMarkupLocation MarkupLocation(const FMarkupSource& Source, std::size_t Offset);
	bool ValidateMarkupUtf8(const FMarkupSource& Source, FMarkupDiagnostic& Diagnostic);
	bool MarkupFail(FMarkupDiagnostic& Diagnostic, const FMarkupLocation& Location, std::string Message);
	std::string TrimMarkup(std::string_view Value);
	bool IsMarkupName(std::string_view Value);
	bool ParseMarkup(const FMarkupSource& Source, const FMarkupCompileOptions& Options, FMarkupElement& Root,
					 FMarkupDiagnostic& Diagnostic);
	bool ParseStyleSheet(const FMarkupSource& Source, FStyleSheetData& Result, FMarkupDiagnostic& Diagnostic,
						 bool bValidateProperties = true);
	bool ParseMarkupProperty(std::string_view Name, std::string_view Value, EProperty& Property, FPropertyValue& Parsed,
							 bool bStyle, std::string& Error);
	bool FindMarkupProperty(std::string_view Name, EProperty& Property, bool bStyle, std::string& Error);
	bool FindMarkupEvent(std::string_view Name, Platform::EEventType& Event);
	bool CompileMarkupUnit(FMarkupData& Data, const FMarkupSource& Source, std::set<std::string>& Visiting,
						   FMarkupDiagnostic& Diagnostic);
	bool ValidateMarkupGraph(const FMarkupData& Data, FMarkupDiagnostic& Diagnostic);
	std::shared_ptr<FWidget> CreateMarkup(const std::shared_ptr<const FMarkupData>& Data,
										  const FMarkupScriptFactory& Scripts,
										  std::shared_ptr<IMarkupScriptContext> BindingContext,
										  FMarkupDiagnostic& Diagnostic);
} // namespace VeroScript::Gui

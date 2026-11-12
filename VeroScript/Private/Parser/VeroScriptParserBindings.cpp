// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParserBindings.h"
#include "VeroScriptParser.h"
#include "VeroScriptAst.h"
#include "VeroScriptAnalysisControl.h"

#include <stdexcept>
#include <algorithm>
#include <type_traits>

namespace VeroScript
{
	const FParserBindingRecord& FParserBindingArena::Get(const std::size_t Index) const
	{
		if (Index < BaseCount)
		{
			if (!Base)
			{
				throw std::invalid_argument("invalid binding arena");
			}
			return Base->Get(Index);
		}
		return Records.at(Index - BaseCount);
	}

	FParserBindingStore::FParserBindingStore(const FParserBindingsSnapshot* Source)
		: Arena(std::make_shared<FParserBindingArena>())
	{
		if (Source)
		{
			Arena->Base = Source->Arena;
			Arena->BaseCount = Source->RecordCount;
			Arena->Depth = Source->Arena ? Source->Arena->Depth + 1 : 0;
			if (Arena->Depth >= 64)
			{
				throw std::invalid_argument("binding snapshot nesting limit exceeded");
			}
			Aliases = Source->Aliases;
			Runtime = Source->Runtime;
			AliasOrder = Source->AliasOrder;
			RuntimeOrder = Source->RuntimeOrder;
			NextOrder = Source->NextOrder;
		}
	}

	void FParserBindingStore::Add(std::vector<std::size_t>& Heads, const std::string& Name,
								  const std::string& Type)
	{
		if (Heads.empty())
		{
			throw std::logic_error("missing parser binding scope");
		}
		const auto Previous = Heads.back();
		Heads.back() = Arena->BaseCount + Arena->Records.size();
		Arena->Records.push_back({Name, Type, Previous});
	}

	std::shared_ptr<const FParserBindingsSnapshot> FParserBindingStore::Capture() const
	{
		auto Result = std::make_shared<FParserBindingsSnapshot>();
		Result->Arena = Arena;
		Result->RecordCount = Arena->BaseCount + Arena->Records.size();
		Result->Aliases = Aliases;
		Result->Runtime = Runtime;
		Result->AliasOrder = AliasOrder;
		Result->RuntimeOrder = RuntimeOrder;
		Result->NextOrder = NextOrder;
		return Result;
	}

	bool FParserBindingStore::IsRuntimeName(
		const std::string& Name, const std::vector<std::unordered_map<std::string, std::string>>& TypeScopes,
		const std::vector<std::unordered_set<std::string>>& ValueScopes, const std::size_t TypeScopeOrder) const
	{
		const auto Find = [&](const auto& Scopes, const auto& Order)
		{
			for (std::size_t I = Scopes.size(); I > 0; --I)
			{
				if (Scopes[I - 1].count(Name))
				{
					return Order[I - 1];
				}
			}
			return std::size_t{0};
		};
		return Find(ValueScopes, RuntimeOrder) > std::max(TypeScopeOrder, Find(TypeScopes, AliasOrder));
	}

	void VeroScriptParser::PushAliasScope()
	{
		if (!BindingStore)
		{
			BindingStore = std::make_shared<FParserBindingStore>();
		}
		LocalTypeAliasScopes.emplace_back();
		BindingStore->Aliases.push_back(EmptyBindingHead);
		BindingStore->AliasOrder.push_back(++BindingStore->NextOrder);
	}

	void VeroScriptParser::PopAliasScope()
	{
		LocalTypeAliasScopes.pop_back();
		BindingStore->Aliases.pop_back();
		BindingStore->AliasOrder.pop_back();
	}

	bool VeroScriptParser::AddAliasBinding(const std::string& Name, const std::string& Type)
	{
		if (!BlockScopes.empty() && !RuntimeBindingScopes.empty() && RuntimeBindingScopes.back().count(Name) != 0)
		{
			ReportErrorAt(Peek(), "type or alias conflicts with a local variable: " + Name);
			return false;
		}
		if (!LocalTypeAliasScopes.back().emplace(Name, Type).second)
		{
			return false;
		}
		BindingStore->Add(BindingStore->Aliases, Name, Type);
		return true;
	}

	void VeroScriptParser::PushRuntimeScope()
	{
		if (!BindingStore)
		{
			BindingStore = std::make_shared<FParserBindingStore>();
		}
		RuntimeBindingScopes.emplace_back();
		BindingStore->Runtime.push_back(EmptyBindingHead);
		BindingStore->RuntimeOrder.push_back(++BindingStore->NextOrder);
	}

	void VeroScriptParser::PopRuntimeScope()
	{
		RuntimeBindingScopes.pop_back();
		BindingStore->Runtime.pop_back();
		BindingStore->RuntimeOrder.pop_back();
	}

	void VeroScriptParser::AddRuntimeBinding(const std::string& Name)
	{
		if (Name.empty())
		{
			return;
		}
		if (ActiveValueParams.count(Name) != 0)
		{
			ReportErrorAt(Peek(), "a runtime variable cannot shadow a template value parameter: " + Name);
			return;
		}
		if (!BlockScopes.empty() && !LocalTypeAliasScopes.empty() && LocalTypeAliasScopes.back().count(Name) != 0)
		{
			ReportErrorAt(Peek(), "variable conflicts with a local type or alias: " + Name);
			return;
		}
		if (RuntimeBindingScopes.empty() || !RuntimeBindingScopes.back().insert(Name).second)
		{
			return;
		}
		BindingStore->Add(BindingStore->Runtime, Name);
	}

	std::shared_ptr<const FParserBindingsSnapshot> VeroScriptParser::CaptureBindings() const
	{
		if (!BindingStore)
		{
			BindingStore = std::make_shared<FParserBindingStore>();
		}
		return BindingStore->Capture();
	}

	void VeroScriptParser::RestoreBindings(const FDeferredSyntax& Context)
	{
		BindingStore = std::make_shared<FParserBindingStore>(Context.Bindings.get());
		LocalTypeAliasScopes.clear();
		RuntimeBindingScopes.clear();
		const auto Restore = [&](const auto& Heads, auto& Scopes)
		{
			for (auto Index : Heads)
			{
				Scopes.emplace_back();
				while (Index != EmptyBindingHead)
				{
					CheckAnalysisCancellation();
					const auto& Entry = BindingStore->Arena->Get(Index);
					if constexpr (std::is_same_v<std::decay_t<decltype(Scopes)>, decltype(LocalTypeAliasScopes)>)
					{
						Scopes.back().emplace(Entry.Name, Entry.Type);
					}
					else
					{
						Scopes.back().insert(Entry.Name);
					}
					if (Entry.Previous != EmptyBindingHead && Entry.Previous >= Index)
					{
						throw std::invalid_argument("invalid binding snapshot chain");
					}
					Index = Entry.Previous;
				}
			}
		};
		Restore(BindingStore->Aliases, LocalTypeAliasScopes);
		Restore(BindingStore->Runtime, RuntimeBindingScopes);
	}
} // namespace VeroScript

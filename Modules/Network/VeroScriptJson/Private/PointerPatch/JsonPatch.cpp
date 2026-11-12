// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonInternal.h"

namespace VeroScript::Json
{
	using namespace Detail;

	static std::vector<std::string> Pointer(std::string_view Text, bool bFragment, FBudget& Budget)
	{
		Budget.Check(0);
		if (Text.size() > Budget.Limits.InputBytes)
		{
			Fail(EError::Limit, "Pointer byte limit exceeded");
		}
		std::string Decoded;
		if (bFragment)
		{
			if (Text.empty() || Text.front() != '#')
			{
				Fail(EError::Pointer, "URI fragment must start with #");
			}
			Text.remove_prefix(1);
			auto Hex = [](char C) -> unsigned
			{
				if (C >= '0' && C <= '9')
				{
					return static_cast<unsigned>(C - '0');
				}
				if (C >= 'a' && C <= 'f')
				{
					return static_cast<unsigned>(C - 'a' + 10);
				}
				if (C >= 'A' && C <= 'F')
				{
					return static_cast<unsigned>(C - 'A' + 10);
				}
				Fail(EError::Pointer, "Invalid percent escape");
			};
			std::size_t CheckAt = 0;
			for (std::size_t I = 0; I < Text.size(); ++I)
			{
				if (I >= CheckAt)
				{
					const auto Count = std::min<std::size_t>(256, Text.size() - I);
					Budget.Check(Count);
					CheckAt = I + Count;
				}
				if (Text[I] == '%')
				{
					if (Text.size() - I < 3)
					{
						Fail(EError::Pointer, "Incomplete percent escape");
					}
					Decoded += static_cast<char>((Hex(Text[I + 1]) << 4) | Hex(Text[I + 2]));
					I += 2;
				}
				else
				{
					Decoded += Text[I];
				}
			}
			Text = Decoded;
		}
		ValidateUtf8(Text);
		std::vector<std::string> Tokens;
		if (Text.empty())
		{
			return Tokens;
		}
		if (Text.front() != '/')
		{
			Fail(EError::Pointer, "JSON Pointer must be empty or start with /");
		}
		Tokens.emplace_back();
		const auto MaxDepth = std::min<std::uint64_t>(256, Budget.Limits.Depth);
		if (!MaxDepth)
		{
			Fail(EError::Limit, "Pointer nesting limit exceeded");
		}
		std::size_t CheckAt = 1;
		for (std::size_t I = 1; I < Text.size(); ++I)
		{
			if (I >= CheckAt)
			{
				const auto Count = std::min<std::size_t>(256, Text.size() - I);
				Budget.Check(Count);
				CheckAt = I + Count;
			}
			char C = Text[I];
			if (C == '/')
			{
				if (Tokens.size() >= MaxDepth)
				{
					Fail(EError::Limit, "Pointer nesting limit exceeded");
				}
				Tokens.emplace_back();
			}
			else if (C == '~')
			{
				if (++I == Text.size() || (Text[I] != '0' && Text[I] != '1'))
				{
					Fail(EError::Pointer, "Invalid JSON Pointer escape");
				}
				Tokens.back() += Text[I] == '0' ? '~' : '/';
			}
			else
			{
				Tokens.back() += C;
			}
			if (Tokens.back().size() > Budget.Limits.TokenBytes)
			{
				Fail(EError::Limit, "Pointer token byte limit exceeded");
			}
		}
		return Tokens;
	}

	bool TryParsePointer(std::string_view Text, std::vector<std::string>& Out, FError& Error, bool bUriFragment,
		const FLimits& Limits, FWork Work)
	{
		return Guard(Error,
			[&]
			{
				FBudget Budget(Limits, Work);
				Out = Pointer(Text, bUriFragment, Budget);
			});
	}

	static std::size_t Index(std::string_view Token, std::size_t Size, bool bAdd)
	{
		if (bAdd && Token == "-")
		{
			return Size;
		}
		if (Token.empty() || (Token.size() > 1 && Token.front() == '0'))
		{
			Fail(EError::Pointer, "Invalid array index");
		}
		std::uint64_t Number = 0;
		auto Result = std::from_chars(Token.data(), Token.data() + Token.size(), Number);
		if (Result.ec != std::errc{} || Result.ptr != Token.data() + Token.size() || Number > Size ||
			(!bAdd && Number == Size))
		{
			Fail(EError::Pointer, "Array index outside range");
		}
		return static_cast<std::size_t>(Number);
	}

	static FJsonValue Resolve(const FDocument& Document, const std::vector<std::string>& Tokens, FBudget& Budget)
	{
		if (Tokens.size() > std::min<std::uint64_t>(256, Budget.Limits.Depth))
		{
			Fail(EError::Limit, "Pointer nesting limit exceeded");
		}
		if (!Document.bHasValue)
		{
			Fail(EError::Missing, "Document has no root value");
		}
		auto Current = Document.Value;
		for (const auto& Token : Tokens)
		{
			if (Token.size() > Budget.Limits.TokenBytes)
			{
				Fail(EError::Limit, "Pointer token byte limit exceeded");
			}
			Budget.Check(1 + Token.size());
			const auto& N = FAccess::Get(Current);
			if (N.Kind == EKind::Array)
			{
				Current = N.Array[Index(Token, N.Array.size(), false)];
			}
			else if (N.Kind == EKind::Object)
			{
				auto I = FindMember(N, Token);
				if (I == N.Object.size())
				{
					Fail(EError::Missing, "Pointer member does not exist");
				}
				Current = N.Object[I].second;
			}
			else
			{
				Fail(EError::Pointer, "Cannot traverse scalar value");
			}
		}
		return Current;
	}

	bool FPointer::TryCompile(
		std::string_view Text, FPointer& Out, FError& Error, bool bUriFragment, const FLimits& Limits, FWork Work)
	{
		return Guard(Error,
			[&]
			{
				FBudget Budget(Limits, Work);
				auto Tokens = Pointer(Text, bUriFragment, Budget);
				Out.Tokens = std::move(Tokens);
			});
	}

	bool FPointer::TryResolve(
		const FDocument& Document, FJsonValue& Out, FError& Error, const FLimits& Limits, FWork Work) const
	{
		return Guard(Error,
			[&]
			{
				FBudget Budget(Limits, Work);
				Budget.Check(0);
				Out = Resolve(Document, Tokens, Budget);
			});
	}

	bool TryResolve(const FDocument& Document, std::string_view Text, FJsonValue& Out, FError& Error,
		const FLimits& Limits, FWork Work)
	{
		return Guard(Error,
			[&]
			{
				FBudget Budget(Limits, Work);
				Out = Resolve(Document, Pointer(Text, false, Budget), Budget);
			});
	}

	enum class EModify
	{
		Add,
		Remove,
		Replace
	};

	static FJsonValue Modify(const FJsonValue& Root, const std::vector<std::string>& Tokens, std::size_t Depth,
		EModify Mode, const FJsonValue& Value, FBudget& Budget)
	{
		const auto& Source = FAccess::Get(Root);
		Budget.Check(1 + Source.Array.size() + Source.Object.size());
		auto Node = Source;
		bool bLeaf = Depth + 1 == Tokens.size();
		const auto& Token = Tokens[Depth];
		if (Node.Kind == EKind::Array)
		{
			auto I = Index(Token, Node.Array.size(), bLeaf && Mode == EModify::Add);
			if (!bLeaf)
			{
				Node.Array[I] = Modify(Node.Array[I], Tokens, Depth + 1, Mode, Value, Budget);
			}
			else if (Mode == EModify::Add)
			{
				Node.Array.insert(Node.Array.begin() + I, Value);
			}
			else if (Mode == EModify::Remove)
			{
				Node.Array.erase(Node.Array.begin() + I);
			}
			else
			{
				Node.Array[I] = Value;
			}
		}
		else if (Node.Kind == EKind::Object)
		{
			auto It = Node.Object.begin() + FindMember(Node, Token);
			if (It == Node.Object.end())
			{
				if (!bLeaf || Mode != EModify::Add)
				{
					Fail(EError::Missing, "Patch member does not exist");
				}
				Node.Object.emplace_back(Token, Value);
				Node.ObjectIndex.reset();
			}
			else if (!bLeaf)
			{
				It->second = Modify(It->second, Tokens, Depth + 1, Mode, Value, Budget);
			}
			else if (Mode == EModify::Remove)
			{
				Node.Object.erase(It);
				Node.ObjectIndex.reset();
			}
			else
			{
				It->second = Value;
			}
		}
		else
		{
			Fail(EError::Pointer, "Cannot modify a child of a scalar");
		}
		return FAccess::Make(std::move(Node));
	}

	static void ModifyDocument(FDocument& Document, const std::vector<std::string>& Tokens, EModify Mode,
		const FJsonValue& Value, FBudget& Budget)
	{
		if (Tokens.empty())
		{
			if (Mode != EModify::Add && !Document.bHasValue)
			{
				Fail(EError::Missing, "Document has no root value");
			}
			Document.bHasValue = Mode != EModify::Remove;
			Document.Value = Document.bHasValue ? Value : FJsonValue{};
		}
		else
		{
			if (!Document.bHasValue)
			{
				Fail(EError::Missing, "Document has no root value");
			}
			Document.Value = Modify(Document.Value, Tokens, 0, Mode, Value, Budget);
		}
	}

	static const FJsonValue& Member(const FJsonValue& Object, std::string_view Name)
	{
		const auto& N = FAccess::Get(Object);
		if (N.Kind != EKind::Object)
		{
			Fail(EError::Patch, "Patch operation must be an object");
		}
		auto It = N.Object.begin() + FindMember(N, Name);
		if (It == N.Object.end())
		{
			Fail(EError::Patch, "Missing required Patch operation member");
		}
		return It->second;
	}

	static std::string StringMember(const FJsonValue& Object, std::string_view Name)
	{
		const auto& N = FAccess::Get(Member(Object, Name));
		if (N.Kind != EKind::String)
		{
			Fail(EError::Patch, "Patch path, from and op must be strings");
		}
		return N.Text;
	}

	bool TryPatch(const FDocument& Input, const FJsonValue& Patch, FDocument& Out, FError& Error, const FLimits& Limits,
		FWork Work)
	{
		std::int64_t Operation = -1;
		bool Success = Guard(Error,
			[&]
			{
				if (Patch.Kind() != EKind::Array)
				{
					Fail(EError::Patch, "Patch must be an array");
				}
				FBudget Budget(Limits, Work);
				auto Result = Input;
				for (const auto& Op : FAccess::Get(Patch).Array)
				{
					++Operation;
					Budget.Check();
					auto Name = StringMember(Op, "op");
					auto Path = Pointer(StringMember(Op, "path"), false, Budget);
					if (Name == "add" || Name == "replace")
					{
						ModifyDocument(
							Result, Path, Name == "add" ? EModify::Add : EModify::Replace, Member(Op, "value"), Budget);
					}
					else if (Name == "remove")
					{
						ModifyDocument(Result, Path, EModify::Remove, {}, Budget);
					}
					else if (Name == "test")
					{
						if (!Equal(Resolve(Result, Path, Budget), Member(Op, "value"), Budget))
						{
							Fail(EError::Patch, "Patch test failed");
						}
					}
					else if (Name == "copy" || Name == "move")
					{
						auto From = Pointer(StringMember(Op, "from"), false, Budget);
						auto Value = Resolve(Result, From, Budget);
						if (Name == "move")
						{
							if (From == Path)
							{
								continue;
							}
							if (Path.size() > From.size() && std::equal(From.begin(), From.end(), Path.begin()))
							{
								Fail(EError::Patch, "Cannot move a value into its descendant");
							}
							ModifyDocument(Result, From, EModify::Remove, {}, Budget);
						}
						ModifyDocument(Result, Path, EModify::Add, Value, Budget);
					}
					else
					{
						Fail(EError::Patch, "Unknown Patch operation");
					}
				}
				if (Result.bHasValue && FAccess::Get(Result.Value).Depth > Limits.Depth)
				{
					Fail(EError::Limit, "Patched document depth limit exceeded");
				}
				Out = std::move(Result);
			});
		if (!Success)
		{
			Error.PatchOperation = Operation;
		}
		return Success;
	}

	static FJsonValue Merge(const FJsonValue& Input, const FJsonValue& Patch, FBudget& Budget, std::uint64_t Depth)
	{
		Budget.Check();
		if (Depth >= Budget.Limits.Depth)
		{
			Fail(EError::Limit, "Merge Patch depth limit exceeded");
		}
		if (Patch.Kind() != EKind::Object)
		{
			return Patch;
		}
		FNode Result;
		Result.Kind = EKind::Object;
		const auto& Original = FAccess::Get(Input);
		const auto& Changes = FAccess::Get(Patch);
		for (const auto& Pair : Original.Object)
		{
			Budget.Check(1 + Pair.first.size());
			auto I = FindMember(Changes, Pair.first);
			if (I == Changes.Object.size())
			{
				Result.Object.push_back(Pair);
			}
			else if (Changes.Object[I].second.Kind() != EKind::Null)
			{
				Result.Object.emplace_back(Pair.first, Merge(Pair.second, Changes.Object[I].second, Budget, Depth + 1));
			}
		}
		for (const auto& Pair : Changes.Object)
		{
			Budget.Check(1 + Pair.first.size());
			if (FindMember(Original, Pair.first) == Original.Object.size() && Pair.second.Kind() != EKind::Null)
			{
				Result.Object.emplace_back(Pair.first, Merge({}, Pair.second, Budget, Depth + 1));
			}
		}
		return FAccess::Make(std::move(Result));
	}

	bool TryMergePatch(const FJsonValue& Input, const FJsonValue& Patch, FJsonValue& Out, FError& Error,
		const FLimits& Limits, FWork Work)
	{
		return Guard(Error,
			[&]
			{
				FBudget Budget(Limits, Work);
				auto Result = Merge(Input, Patch, Budget, 0);
				if (FAccess::Get(Result).Depth > Limits.Depth)
				{
					Fail(EError::Limit, "Merged document depth limit exceeded");
				}
				Out = std::move(Result);
			});
	}
} // namespace VeroScript::Json

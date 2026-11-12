// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompilerAnalysis.h"
#include "VeroScriptAst.h"
#include "VeroScriptRuntime.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VeroScript::CompilerDetail
{
	bool BlockContainsLocalFallthrough(const FBlockStmt& Block);

	bool StatementContainsLocalFallthrough(const FStmt& Statement)
	{
		switch (Statement.Kind)
		{
		case EStmtKind::Fallthrough:
			return true;
		case EStmtKind::Switch:
			// Nested switch validates its own fallthrough rules.
			return false;
		case EStmtKind::Block:
			return BlockContainsLocalFallthrough(static_cast<const FBlockStmt&>(Statement));
		case EStmtKind::If:
			{
				const FIfStmt& IfStmt = static_cast<const FIfStmt&>(Statement);
				if (IfStmt.ThenBranch && BlockContainsLocalFallthrough(*IfStmt.ThenBranch))
				{
					return true;
				}
				if (IfStmt.ElseBranch && BlockContainsLocalFallthrough(*IfStmt.ElseBranch))
				{
					return true;
				}
				return false;
			}
		case EStmtKind::While:
			{
				const FWhileStmt& WhileStmt = static_cast<const FWhileStmt&>(Statement);
				return WhileStmt.Body && BlockContainsLocalFallthrough(*WhileStmt.Body);
			}
		case EStmtKind::DoWhile:
			{
				const FDoWhileStmt& DoWhileStmt = static_cast<const FDoWhileStmt&>(Statement);
				return DoWhileStmt.Body && BlockContainsLocalFallthrough(*DoWhileStmt.Body);
			}
		case EStmtKind::For:
			{
				const FForStmt& ForStmt = static_cast<const FForStmt&>(Statement);
				if (ForStmt.Init
					&& StatementContainsLocalFallthrough(*ForStmt.Init))
				{
					return true;
				}
				return ForStmt.Body && BlockContainsLocalFallthrough(*ForStmt.Body);
			}
		case EStmtKind::ForEach:
			{
				const FForEachStmt& ForEachStmt = static_cast<const FForEachStmt&>(Statement);
				return ForEachStmt.Body && BlockContainsLocalFallthrough(*ForEachStmt.Body);
			}
		case EStmtKind::Try:
			{
				const FTryStmt& TryStmt = static_cast<const FTryStmt&>(Statement);
				if (TryStmt.TryBlock && BlockContainsLocalFallthrough(*TryStmt.TryBlock))
				{
					return true;
				}
				for (const FCatchClause& Clause : TryStmt.Catches)
				{
					if (Clause.Body && BlockContainsLocalFallthrough(*Clause.Body))
					{
						return true;
					}
				}
				return TryStmt.FinallyBlock
					&& BlockContainsLocalFallthrough(*TryStmt.FinallyBlock);
			}
		default:
			return false;
		}
	}

	bool BlockContainsLocalFallthrough(const FBlockStmt& Block)
	{
		for (const std::unique_ptr<FStmt>& Statement : Block.Statements)
		{
			if (Statement && StatementContainsLocalFallthrough(*Statement))
			{
				return true;
			}
		}
		return false;
	}

	bool BlockAlwaysReturns(const FBlockStmt& Block);
	bool BlockContainsEscapingControl(
		const FBlockStmt& Block,
		bool bBreakEscapes,
		bool bContinueEscapes);

	bool StatementContainsEscapingControl(
		const FStmt& Statement,
		const bool bBreakEscapes,
		const bool bContinueEscapes)
	{
		switch (Statement.Kind)
		{
		case EStmtKind::Break:
			return bBreakEscapes;
		case EStmtKind::Continue:
			return bContinueEscapes;
		case EStmtKind::Goto:
			return true;
		case EStmtKind::Block:
			return BlockContainsEscapingControl(
				static_cast<const FBlockStmt&>(Statement),
				bBreakEscapes,
				bContinueEscapes);
		case EStmtKind::If:
			{
				const FIfStmt& IfStmt = static_cast<const FIfStmt&>(Statement);
				return (IfStmt.ThenBranch && BlockContainsEscapingControl(
						*IfStmt.ThenBranch, bBreakEscapes, bContinueEscapes))
					|| (IfStmt.ElseBranch && BlockContainsEscapingControl(
						*IfStmt.ElseBranch, bBreakEscapes, bContinueEscapes));
			}
		case EStmtKind::While:
			{
				const FWhileStmt& WhileStmt = static_cast<const FWhileStmt&>(Statement);
				return WhileStmt.Body
					&& BlockContainsEscapingControl(*WhileStmt.Body, false, false);
			}
		case EStmtKind::DoWhile:
			{
				const FDoWhileStmt& DoWhileStmt = static_cast<const FDoWhileStmt&>(Statement);
				return DoWhileStmt.Body
					&& BlockContainsEscapingControl(*DoWhileStmt.Body, false, false);
			}
		case EStmtKind::For:
			{
				const FForStmt& ForStmt = static_cast<const FForStmt&>(Statement);
				return ForStmt.Body
					&& BlockContainsEscapingControl(*ForStmt.Body, false, false);
			}
		case EStmtKind::ForEach:
			{
				const FForEachStmt& ForEachStmt = static_cast<const FForEachStmt&>(Statement);
				return ForEachStmt.Body
					&& BlockContainsEscapingControl(*ForEachStmt.Body, false, false);
			}
		case EStmtKind::Switch:
			{
				const FSwitchStmt& SwitchStmt = static_cast<const FSwitchStmt&>(Statement);
				for (const FSwitchCase& Case : SwitchStmt.Cases)
				{
					if (Case.Body && BlockContainsEscapingControl(
							*Case.Body, false, bContinueEscapes))
					{
						return true;
					}
				}
				return false;
			}
		case EStmtKind::Try:
			{
				const FTryStmt& TryStmt = static_cast<const FTryStmt&>(Statement);
				if (TryStmt.TryBlock && BlockContainsEscapingControl(
						*TryStmt.TryBlock, bBreakEscapes, bContinueEscapes))
				{
					return true;
				}
				for (const FCatchClause& Clause : TryStmt.Catches)
				{
					if (Clause.Body && BlockContainsEscapingControl(
							*Clause.Body, bBreakEscapes, bContinueEscapes))
					{
						return true;
					}
				}
				return TryStmt.FinallyBlock && BlockContainsEscapingControl(
					*TryStmt.FinallyBlock, bBreakEscapes, bContinueEscapes);
			}
		default:
			return false;
		}
	}

	bool BlockContainsEscapingControl(
		const FBlockStmt& Block,
		const bool bBreakEscapes,
		const bool bContinueEscapes)
	{
		for (const std::unique_ptr<FStmt>& Statement : Block.Statements)
		{
			if (Statement && StatementContainsEscapingControl(
					*Statement, bBreakEscapes, bContinueEscapes))
			{
				return true;
			}
		}
		return false;
	}

	bool SwitchAlwaysReturns(const FSwitchStmt& Switch)
	{
		if (Switch.Cases.empty())
		{
			return false;
		}
		bool bHasDefault = false;
		for (const FSwitchCase& Case : Switch.Cases)
		{
			if (Case.bIsDefault)
			{
				bHasDefault = true;
			}
			if (!Case.Body || BlockContainsLocalFallthrough(*Case.Body))
			{
				return false;
			}
			if (!BlockAlwaysReturns(*Case.Body))
			{
				return false;
			}
		}
		return bHasDefault;
	}

	bool StatementAlwaysReturns(const FStmt& Statement)
	{
		switch (Statement.Kind)
		{
		case EStmtKind::Return:
		case EStmtKind::Throw:
			return true;
		case EStmtKind::Block:
			return BlockAlwaysReturns(static_cast<const FBlockStmt&>(Statement));
		case EStmtKind::If:
			{
				const FIfStmt& IfStmt = static_cast<const FIfStmt&>(Statement);
				return IfStmt.ThenBranch
					&& IfStmt.ElseBranch
					&& BlockAlwaysReturns(*IfStmt.ThenBranch)
					&& BlockAlwaysReturns(*IfStmt.ElseBranch);
			}
		case EStmtKind::DoWhile:
			{
				// Unlike pre-test loops, a do-loop always enters its body once.
				const FDoWhileStmt& DoWhileStmt = static_cast<const FDoWhileStmt&>(Statement);
				return DoWhileStmt.Body
					&& !BlockContainsEscapingControl(*DoWhileStmt.Body, true, true)
					&& BlockAlwaysReturns(*DoWhileStmt.Body);
			}
		case EStmtKind::Switch:
			return SwitchAlwaysReturns(static_cast<const FSwitchStmt&>(Statement));
		case EStmtKind::Try:
			{
				const FTryStmt& TryStmt = static_cast<const FTryStmt&>(Statement);
				if (TryStmt.FinallyBlock && BlockAlwaysReturns(*TryStmt.FinallyBlock))
				{
					return true;
				}
				if (!TryStmt.TryBlock || !BlockAlwaysReturns(*TryStmt.TryBlock))
				{
					return false;
				}
				for (const FCatchClause& Clause : TryStmt.Catches)
				{
					if (!Clause.Body || !BlockAlwaysReturns(*Clause.Body))
					{
						return false;
					}
				}
				return true;
			}
		default:
			return false;
		}
	}

	bool BlockAlwaysReturns(const FBlockStmt& Block)
	{
		for (const std::unique_ptr<FStmt>& Statement : Block.Statements)
		{
			if (Statement && StatementAlwaysReturns(*Statement))
			{
				return true;
			}
		}
		return false;
	}

	bool TypeNameMentionsAuto(const std::string& TypeName)
	{
		if (TypeName.empty())
		{
			return false;
		}
		if (TypeName == "auto")
		{
			return true;
		}
		if (TypeName.rfind("const ref ", 0) == 0)
		{
			return TypeNameMentionsAuto(TypeName.substr(10));
		}
		if (TypeName.rfind("ref ", 0) == 0)
		{
			return TypeNameMentionsAuto(TypeName.substr(4));
		}
		if (Runtime::IsFunctionTypeName(TypeName))
		{
			std::vector<std::string> ParamTypes;
			std::string ReturnType;
			if (!Runtime::ParseFunctionType(TypeName, ParamTypes, ReturnType))
			{
				return false;
			}
			for (const std::string& ParamType : ParamTypes)
			{
				if (TypeNameMentionsAuto(ParamType))
				{
					return true;
				}
			}
			return TypeNameMentionsAuto(ReturnType);
		}
		if (Runtime::IsDynamicArrayTypeName(TypeName))
		{
			std::string ElementType;
			return Runtime::ParseDynamicArrayElementType(TypeName, ElementType)
				&& TypeNameMentionsAuto(ElementType);
		}
		if (Runtime::IsMapTypeName(TypeName))
		{
			std::string KeyType;
			std::string ValueType;
			return Runtime::ParseMapType(TypeName, KeyType, ValueType)
				&& (TypeNameMentionsAuto(KeyType) || TypeNameMentionsAuto(ValueType));
		}
		if (Runtime::IsOrderedMapTypeName(TypeName))
		{
			std::string KeyType;
			std::string ValueType;
			return Runtime::ParseOrderedMapType(TypeName, KeyType, ValueType)
				&& (TypeNameMentionsAuto(KeyType) || TypeNameMentionsAuto(ValueType));
		}
		if (Runtime::IsSetTypeName(TypeName))
		{
			std::string ElementType;
			return Runtime::ParseSetElementType(TypeName, ElementType)
				&& TypeNameMentionsAuto(ElementType);
		}
		if (Runtime::IsMultiSetTypeName(TypeName)
			|| Runtime::IsOrderedMultiSetTypeName(TypeName))
		{
			std::string ElementType;
			return Runtime::ParseMultiSetElementType(TypeName, ElementType)
				&& TypeNameMentionsAuto(ElementType);
		}
		if (Runtime::IsQueueTypeName(TypeName))
		{
			std::string ElementType;
			return Runtime::ParseQueueElementType(TypeName, ElementType)
				&& TypeNameMentionsAuto(ElementType);
		}
		if (Runtime::IsStackTypeName(TypeName))
		{
			std::string ElementType;
			return Runtime::ParseStackElementType(TypeName, ElementType)
				&& TypeNameMentionsAuto(ElementType);
		}
		if (Runtime::IsDequeTypeName(TypeName))
		{
			std::string ElementType;
			return Runtime::ParseDequeElementType(TypeName, ElementType)
				&& TypeNameMentionsAuto(ElementType);
		}
		if (Runtime::IsChannelTypeName(TypeName))
		{
			std::string ElementType;
			return Runtime::ParseChannelElementType(TypeName, ElementType)
				&& TypeNameMentionsAuto(ElementType);
		}
		if (Runtime::IsNativeArrayTypeName(TypeName))
		{
			std::string ElementType;
			std::uint32_t Length = 0;
			return Runtime::ParseNativeArrayType(TypeName, ElementType, Length)
				&& TypeNameMentionsAuto(ElementType);
		}
		// Soft generics / user apps: scan type-argument text for an `auto` leaf.
		const std::size_t Open = TypeName.find('<');
		if (Open != std::string::npos && TypeName.back() == '>')
		{
			std::size_t Index = Open + 1;
			const std::size_t End = TypeName.size() - 1;
			int Depth = 0;
			int ParenthesisDepth = 0;
			std::size_t ArgStart = Index;
			while (Index <= End)
			{
				const bool bAtEnd = Index == End;
				const char Char = bAtEnd ? ',' : TypeName[Index];
				if (!bAtEnd)
				{
					if (Char == '<')
					{
						++Depth;
					}
					else if (Char == '>'
						&& (Index == 0 || TypeName[Index - 1] != '-'))
					{
						--Depth;
					}
					else if (Char == '(')
					{
						++ParenthesisDepth;
					}
					else if (Char == ')' && ParenthesisDepth > 0)
					{
						--ParenthesisDepth;
					}
				}
				if ((Char == ',' && Depth == 0 && ParenthesisDepth == 0) || bAtEnd)
				{
					std::string Arg = TypeName.substr(ArgStart, Index - ArgStart);
					while (!Arg.empty() && (Arg.front() == ' ' || Arg.front() == '\t'))
					{
						Arg.erase(Arg.begin());
					}
					while (!Arg.empty() && (Arg.back() == ' ' || Arg.back() == '\t'))
					{
						Arg.pop_back();
					}
					if (TypeNameMentionsAuto(Arg))
					{
						return true;
					}
					ArgStart = Index + 1;
				}
				if (bAtEnd)
				{
					break;
				}
				++Index;
			}
		}
		return false;
	}
}

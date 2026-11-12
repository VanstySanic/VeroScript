// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPreprocessor.h"
#include "Parser/VeroScriptParserDetail.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptImportSyntax.h"
#include "VeroScriptPath.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace VeroScript
{
	namespace
	{
		struct FPreprocessLimit
		{
			const char* Message;
			FPreprocessSourcePos Position;
		};

		thread_local FPreprocessBudget* ActiveBudget = nullptr;
		thread_local FPreprocessSourcePos BudgetPosition;

		class FPreprocessBudgetScope
		{
		public:
			explicit FPreprocessBudgetScope(FPreprocessBudget& Budget)
			    : Previous(ActiveBudget), Position(BudgetPosition)
			{
				ActiveBudget = &Budget;
				BudgetPosition = {};
			}
			~FPreprocessBudgetScope()
			{
				ActiveBudget = Previous;
				BudgetPosition = Position;
			}

		private:
			FPreprocessBudget* Previous;
			FPreprocessSourcePos Position;
		};

		void ChargePreprocessWork(const std::size_t Units)
		{
			if (!ActiveBudget) return;
			if (ActiveBudget->Work > ActiveBudget->MaxWork || Units > ActiveBudget->MaxWork - ActiveBudget->Work)
			{
				throw FPreprocessLimit{"macro expansion work limit exceeded", BudgetPosition};
			}
			ActiveBudget->Work += Units;
		}

		void CheckExpansionSize(const std::size_t Current, const std::size_t Added)
		{
			if (ActiveBudget && (Current > ActiveBudget->MaxOutputBytes || Added > ActiveBudget->MaxOutputBytes - Current))
			{
				throw FPreprocessLimit{"preprocessed output limit exceeded", BudgetPosition};
			}
			ChargePreprocessWork(Added);
		}

		void AppendExpansion(std::string& Output, const std::string_view Text)
		{
			CheckExpansionSize(Output.size(), Text.size());
			Output.append(Text.data(), Text.size());
		}
		void AppendExpansion(std::string& Output, const char* Data, const std::size_t Count)
		{
			AppendExpansion(Output, std::string_view(Data, Count));
		}
		void AppendExpansion(std::string& Output, const char Character)
		{
			CheckExpansionSize(Output.size(), 1);
			Output.push_back(Character);
		}

		bool IsIdentStart(const char Character)
		{
			return (Character >= 'A' && Character <= 'Z')
				|| (Character >= 'a' && Character <= 'z')
				|| Character == '_';
		}

		bool IsIdentContinue(const char Character)
		{
			return IsIdentStart(Character)
				|| (Character >= '0' && Character <= '9');
		}

		void SkipHorizontalSpace(const std::string_view Line, std::size_t& Index)
		{
			while (Index < Line.size()
				&& (Line[Index] == ' ' || Line[Index] == '\t' || Line[Index] == '\r'))
			{
				++Index;
			}
		}

		void TrimTrailingHorizontalSpace(std::string& Text)
		{
			while (!Text.empty()
				&& (Text.back() == ' ' || Text.back() == '\t' || Text.back() == '\r'))
			{
				Text.pop_back();
			}
		}

		bool ParseIdentifier(
			const std::string_view Line,
			std::size_t& Index,
			std::string& OutName)
		{
			if (Index >= Line.size() || !IsIdentStart(Line[Index]))
			{
				return false;
			}
			const std::size_t Begin = Index;
			++Index;
			while (Index < Line.size() && IsIdentContinue(Line[Index]))
			{
				++Index;
			}
			OutName.assign(Line.data() + Begin, Index - Begin);
			return true;
		}

		constexpr unsigned long long IfIntMax =
			static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max());

		bool AccumulateIfDigit(
			unsigned long long& Parsed,
			const unsigned long long Base,
			const unsigned long long Digit)
		{
			if (Base == 0 || Digit >= Base)
			{
				return false;
			}
			if (Parsed > IfIntMax / Base)
			{
				return false;
			}
			const unsigned long long Scaled = Parsed * Base;
			if (Scaled > IfIntMax - Digit)
			{
				return false;
			}
			Parsed = Scaled + Digit;
			return true;
		}

		bool CheckedAddInt64(const std::int64_t Left, const std::int64_t Right, std::int64_t& Out)
		{
			if (Right > 0
				&& Left > std::numeric_limits<std::int64_t>::max() - Right)
			{
				return false;
			}
			if (Right < 0
				&& Left < std::numeric_limits<std::int64_t>::min() - Right)
			{
				return false;
			}
			Out = Left + Right;
			return true;
		}

		bool CheckedSubInt64(const std::int64_t Left, const std::int64_t Right, std::int64_t& Out)
		{
			if (Right > 0
				&& Left < std::numeric_limits<std::int64_t>::min() + Right)
			{
				return false;
			}
			if (Right < 0
				&& Left > std::numeric_limits<std::int64_t>::max() + Right)
			{
				return false;
			}
			Out = Left - Right;
			return true;
		}

		bool CheckedMulInt64(const std::int64_t Left, const std::int64_t Right, std::int64_t& Out)
		{
			if (Left == 0 || Right == 0)
			{
				Out = 0;
				return true;
			}
			if (Left == -1 && Right == std::numeric_limits<std::int64_t>::min())
			{
				return false;
			}
			if (Right == -1 && Left == std::numeric_limits<std::int64_t>::min())
			{
				return false;
			}
			if (Left > 0)
			{
				if (Right > 0)
				{
					if (Left > std::numeric_limits<std::int64_t>::max() / Right)
					{
						return false;
					}
				}
				else if (Right < std::numeric_limits<std::int64_t>::min() / Left)
				{
					return false;
				}
			}
			else
			{
				if (Right > 0)
				{
					if (Left < std::numeric_limits<std::int64_t>::min() / Right)
					{
						return false;
					}
				}
				else if (Left < std::numeric_limits<std::int64_t>::max() / Right)
				{
					return false;
				}
			}
			Out = Left * Right;
			return true;
		}

		struct FCondFrame
		{
			bool bParentActive = true;
			bool bBranchActive = true;
			bool bBranchTaken = false;
			bool bElseSeen = false;
			std::uint32_t OpenLine = 1;
		};

		struct FLogicalPiece
		{
			std::uint32_t PhysLine = 1;
			std::uint32_t PhysColStart = 1;
			std::size_t LogicalBegin = 0;
			std::size_t LogicalEnd = 0;
		};

		struct FMacro
		{
			enum class EBuiltin : std::uint8_t
			{
				None,
				File,
				Line,
			};

			const std::string &Replacement() const
			{
				return Definition ? Definition->Body : Body;
			}

			const std::vector<std::string> &Arguments() const
			{
				return Definition ? Definition->Parameters : Parameters;
			}

			bool bFunctionLike = false;

			// True when the parameter list ends with `...` (`__VA_ARGS__`).
			bool bVariadic = false;

			// Named parameters only (excludes `...`).
			std::vector<std::string> Parameters;

			std::string Body;

			EBuiltin Builtin = EBuiltin::None;

			std::shared_ptr<const FPublicMacro> Definition;

			bool bExported = false;

			bool bImported = false;
		};

		using FMacroMap = std::unordered_map<std::string, FMacro>;

		constexpr const char* VaArgsName = "__VA_ARGS__";
		constexpr const char* VaOptName = "__VA_OPT__";
		constexpr const char* FileMacroName = "__FILE__";
		constexpr const char* LineMacroName = "__LINE__";
		/** Caps nested ExpandFragment / function-like invocations (stack safety).
		 * Object-like chains cost ~1 per hop; nested `F(F(...))` ~2 per level. */
		constexpr int MaxMacroExpansionDepth = 512;

		bool IsReservedPreprocessorIdentifier(const std::string_view Name)
		{
			return Name == VaArgsName || Name == VaOptName
				|| Name == FileMacroName || Name == LineMacroName;
		}

		struct FMacroExpansionContext
		{
			std::uint32_t ResolveLine(const std::size_t Offset) const
			{
				FPreprocessSourcePos Position{Line, 1};
				if (Pieces)
				{
					for (const auto& Piece : *Pieces)
					{
						if (Offset >= Piece.LogicalBegin && Offset < Piece.LogicalEnd)
						{
							Position = {Piece.PhysLine,
								Piece.PhysColStart + static_cast<std::uint32_t>(Offset - Piece.LogicalBegin)};
							break;
						}
					}
				}

				return Mapper && *Mapper ? (*Mapper)(Position).Line : Position.Line;
			}

			FMacroExpansionContext AtOffset(const std::size_t Offset) const
			{
				return FMacroExpansionContext{FileLiteral, ResolveLine(Offset), nullptr};
			}

			std::string_view FileLiteral;
			std::uint32_t Line = 1;
			const std::vector<FLogicalPiece>* Pieces = nullptr;
			const std::function<FPreprocessSourcePos(FPreprocessSourcePos)>* Mapper = nullptr;

		};

		std::string MakeFileMacroLiteral(const std::string_view LogicalPath)
		{
			const std::string_view Value = LogicalPath.empty()
				? std::string_view("<memory>")
				: LogicalPath;
			std::string Out;
			CheckExpansionSize(0, Value.size());
			Out.reserve(Value.size() + 2);
			AppendExpansion(Out, '"');
			for (const char Character : Value)
			{
				switch (Character)
				{
				case '\\':
					{
						// Normalize path separators so the same logical path has a stable
						// spelling on Windows and Unix hosts.
						AppendExpansion(Out, '/');
						break;
					}
				case '"':
					{
						AppendExpansion(Out, "\\\"");
						break;
					}
				case '\n':
					{
						AppendExpansion(Out, "\\n");
						break;
					}
				case '\r':
					{
						AppendExpansion(Out, "\\r");
						break;
					}
				case '\t':
					{
						AppendExpansion(Out, "\\t");
						break;
					}
				default:
					{
						AppendExpansion(Out, Character);
						break;
					}
				}
			}
			AppendExpansion(Out, '"');
			return Out;
		}

		int FindMacroParamIndex(
			const FMacro& Macro,
			const std::string_view Name)
		{
			if (Macro.bVariadic && Name == VaArgsName)
			{
				return static_cast<int>(Macro.Arguments().size());
			}
			for (std::size_t Index = 0; Index < Macro.Arguments().size(); ++Index)
			{
				if (Macro.Arguments()[Index] == Name)
				{
					return static_cast<int>(Index);
				}
			}
			return -1;
		}

		std::string JoinMacroArgs(
			const std::vector<std::string>& Args,
			const std::size_t Begin,
			const std::size_t End)
		{
			std::string Joined;
			for (std::size_t Index = Begin; Index < End; ++Index)
			{
				if (Index > Begin)
				{
					AppendExpansion(Joined, ", ");
				}
				AppendExpansion(Joined, Args[Index]);
			}
			return Joined;
		}

		/**
		 * Bind collected call arguments into named slots (+ optional __VA_ARGS__).
		 * Empty `()` rules:
		 * - Non-variadic, one named param: one empty argument (`F()` → empty `x`).
		 * - Variadic with named params: all named slots empty and `__VA_ARGS__`
		 *   empty (`#define F(a, ...) ...` may be invoked as `F()`).
		 */
		bool BindMacroArguments(
			const std::string& MacroName,
			const FMacro& Macro,
			std::vector<std::string> Collected,
			std::vector<std::string>& OutRawArgs,
			std::string& OutError)
		{
			OutRawArgs.clear();
			const std::size_t NamedCount = Macro.Arguments().size();
			if (!Macro.bVariadic)
			{
				if (Collected.empty() && NamedCount == 1)
				{
					Collected.emplace_back();
				}
				if (Collected.size() != NamedCount)
				{
					OutError = "macro '" + MacroName + "' requires "
						+ std::to_string(NamedCount) + " argument(s), got "
						+ std::to_string(Collected.size());
					return false;
				}
				OutRawArgs = std::move(Collected);
				return true;
			}
			if (Collected.empty() && NamedCount > 0)
			{
				Collected.assign(NamedCount, std::string{});
			}
			if (Collected.size() < NamedCount)
			{
				OutError = "macro '" + MacroName + "' requires at least "
					+ std::to_string(NamedCount) + " argument(s), got "
					+ std::to_string(Collected.size());
				return false;
			}
			OutRawArgs.reserve(NamedCount + 1);
			for (std::size_t Index = 0; Index < NamedCount; ++Index)
			{
				OutRawArgs.push_back(std::move(Collected[Index]));
			}
			OutRawArgs.push_back(JoinMacroArgs(Collected, NamedCount, Collected.size()));
			return true;
		}

		std::string StringizeMacroArg(const std::string_view Arg)
		{
			std::size_t Begin = 0;
			std::size_t End = Arg.size();
			while (Begin < End
				&& (Arg[Begin] == ' ' || Arg[Begin] == '\t' || Arg[Begin] == '\r'))
			{
				++Begin;
			}
			while (End > Begin
				&& (Arg[End - 1] == ' ' || Arg[End - 1] == '\t' || Arg[End - 1] == '\r'))
			{
				--End;
			}
			std::string Out;
			AppendExpansion(Out, '"');
			bool bPendingSpace = false;
			for (std::size_t Index = Begin; Index < End; ++Index)
			{
				const char Character = Arg[Index];
				if (Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n')
				{
					bPendingSpace = true;
					continue;
				}
				if (bPendingSpace)
				{
					AppendExpansion(Out, ' ');
					bPendingSpace = false;
				}
				if (Character == '\\' || Character == '"')
				{
					AppendExpansion(Out, '\\');
				}
				AppendExpansion(Out, Character);
			}
			AppendExpansion(Out, '"');
			return Out;
		}

		bool SkipStringLiteral(const std::string_view Text, std::size_t& Index)
		{
			if (Index >= Text.size())
			{
				return false;
			}
			if (Text[Index] == 'L' && Index + 1 < Text.size() && Text[Index + 1] == '"')
			{
				Index += 2;
			}
			else if (Text[Index] == '"')
			{
				++Index;
			}
			else
			{
				return false;
			}
			while (Index < Text.size())
			{
				const char Character = Text[Index++];
				if (Character == '\\' && Index < Text.size())
				{
					++Index;
					continue;
				}
				if (Character == '"')
				{
					return true;
				}
			}
			return false;
		}

		bool SkipCharacterLiteral(const std::string_view Text, std::size_t& Index)
		{
			if (Index >= Text.size())
			{
				return false;
			}
			if (Text[Index] == 'L' && Index + 1 < Text.size() && Text[Index + 1] == '\'')
			{
				Index += 2;
			}
			else if (Text[Index] == '\'')
			{
				++Index;
			}
			else
			{
				return false;
			}
			while (Index < Text.size())
			{
				const char Character = Text[Index++];
				if (Character == '\\' && Index < Text.size())
				{
					++Index;
					continue;
				}
				if (Character == '\'')
				{
					return true;
				}
			}
			return false;
		}

		bool VariadicArgumentHasTokens(const std::string_view Argument)
		{
			return std::any_of(
				Argument.begin(),
				Argument.end(),
				[](const char Character)
				{
					return Character != ' ' && Character != '\t'
							&& Character != '\r' && Character != '\n';
				});
		}

		bool FindVaOptClose(
			const std::string_view Text,
			const std::size_t OpenIndex,
			std::size_t& OutCloseIndex,
			std::string& OutError)
		{
			if (OpenIndex >= Text.size() || Text[OpenIndex] != '(')
			{
				OutError = "__VA_OPT__ must be followed by '('";
				return false;
			}
			int Depth = 1;
			std::size_t Index = OpenIndex + 1;
			while (Index < Text.size())
			{
				const char Character = Text[Index];
				if (Character == '"'
					|| (Character == 'L' && Index + 1 < Text.size()
						&& Text[Index + 1] == '"'))
				{
					if (!SkipStringLiteral(Text, Index))
					{
						OutError = "unterminated string in __VA_OPT__";
						return false;
					}
					continue;
				}
				if (Character == '\''
					|| (Character == 'L' && Index + 1 < Text.size()
						&& Text[Index + 1] == '\''))
				{
					if (!SkipCharacterLiteral(Text, Index))
					{
						OutError = "unterminated character literal in __VA_OPT__";
						return false;
					}
					continue;
				}
				if (Character == '/' && Index + 1 < Text.size())
				{
					if (Text[Index + 1] == '/')
					{
						break;
					}
					if (Text[Index + 1] == '*')
					{
						const std::size_t End = Text.find("*/", Index + 2);
						if (End == std::string_view::npos)
						{
							OutError = "unterminated block comment in __VA_OPT__";
							return false;
						}
						Index = End + 2;
						continue;
					}
				}
				if (Character == '(')
				{
					++Depth;
				}
				else if (Character == ')')
				{
					--Depth;
					if (Depth == 0)
					{
						OutCloseIndex = Index;
						return true;
					}
				}
				++Index;
			}
			OutError = "unterminated __VA_OPT__(...)";
			return false;
		}

		bool VaOptHasBoundaryTokenPaste(const std::string_view Content)
		{
			bool bSawToken = false;
			bool bFirstIsPaste = false;
			bool bLastIsPaste = false;
			std::size_t Index = 0;
			while (Index < Content.size())
			{
				const char Character = Content[Index];
				if (Character == ' ' || Character == '\t'
					|| Character == '\r' || Character == '\n')
				{
					++Index;
					continue;
				}
				if (Character == '/' && Index + 1 < Content.size())
				{
					if (Content[Index + 1] == '/')
					{
						break;
					}
					if (Content[Index + 1] == '*')
					{
						const std::size_t End = Content.find("*/", Index + 2);
						if (End == std::string_view::npos)
						{
							break;
						}
						Index = End + 2;
						continue;
					}
				}

				const bool bIsPaste = Character == '#'
					&& Index + 1 < Content.size() && Content[Index + 1] == '#';
				if (!bSawToken)
				{
					bFirstIsPaste = bIsPaste;
					bSawToken = true;
				}
				bLastIsPaste = bIsPaste;
				if (bIsPaste)
				{
					Index += 2;
				}
				else if (IsIdentStart(Character))
				{
					++Index;
					while (Index < Content.size() && IsIdentContinue(Content[Index]))
					{
						++Index;
					}
				}
				else if (Character == '"'
					|| (Character == 'L' && Index + 1 < Content.size()
						&& Content[Index + 1] == '"'))
				{
					if (!SkipStringLiteral(Content, Index))
					{
						break;
					}
				}
				else if (Character == '\''
					|| (Character == 'L' && Index + 1 < Content.size()
						&& Content[Index + 1] == '\''))
				{
					if (!SkipCharacterLiteral(Content, Index))
					{
						break;
					}
				}
				else
				{
					++Index;
				}
			}
			return bSawToken && (bFirstIsPaste || bLastIsPaste);
		}

		bool SubstituteResolvedMacroBody(
			const FMacro& Macro,
			std::string_view Body,
			const std::vector<std::string>& RawArgs,
			const std::vector<std::string>& ExpandedArgs,
			std::string& Out,
			std::string& OutError);

		bool ApplyTokenPastes(
			std::string_view Text,
			std::string& Out,
			std::string& OutError);

		bool ResolveVaOpt(
			const FMacro& Macro,
			const std::string_view Body,
			const bool bHasVariadicTokens,
			std::string& Out,
			std::string& OutError,
			const std::vector<std::string>* const RawArgs = nullptr,
			const std::vector<std::string>* const ExpandedArgs = nullptr,
			const int Depth = 0)
		{
			if (Depth >= MaxMacroExpansionDepth)
			{
				OutError = "__VA_OPT__ nested too deeply";
				return false;
			}
			Out.clear();
			CheckExpansionSize(0, Body.size());
			Out.reserve(Body.size());
			std::size_t Index = 0;
			while (Index < Body.size())
			{
				const char Character = Body[Index];
				if (Character == '"'
					|| (Character == 'L' && Index + 1 < Body.size()
						&& Body[Index + 1] == '"'))
				{
					const std::size_t Begin = Index;
					if (!SkipStringLiteral(Body, Index))
					{
						OutError = "unterminated string in macro body";
						return false;
					}
					AppendExpansion(Out, Body.data() + Begin, Index - Begin);
					continue;
				}
				if (Character == '\''
					|| (Character == 'L' && Index + 1 < Body.size()
						&& Body[Index + 1] == '\''))
				{
					const std::size_t Begin = Index;
					if (!SkipCharacterLiteral(Body, Index))
					{
						OutError = "unterminated character literal in macro body";
						return false;
					}
					AppendExpansion(Out, Body.data() + Begin, Index - Begin);
					continue;
				}
				if (Character == '/' && Index + 1 < Body.size())
				{
					if (Body[Index + 1] == '/')
					{
						AppendExpansion(Out, Body.data() + Index, Body.size() - Index);
						return true;
					}
					if (Body[Index + 1] == '*')
					{
						const std::size_t End = Body.find("*/", Index + 2);
						if (End == std::string_view::npos)
						{
							OutError = "unterminated block comment in macro body";
							return false;
						}
						AppendExpansion(Out, Body.data() + Index, End + 2 - Index);
						Index = End + 2;
						continue;
					}
				}
				if (!IsIdentStart(Character))
				{
					AppendExpansion(Out, Character);
					++Index;
					continue;
				}

				const std::size_t NameBegin = Index;
				std::string Name;
				if (!ParseIdentifier(Body, Index, Name))
				{
					OutError = "internal preprocessor identifier error";
					return false;
				}
				if (Name != VaOptName)
				{
					AppendExpansion(Out, Body.data() + NameBegin, Index - NameBegin);
					continue;
				}
				if (Depth > 0)
				{
					OutError = "__VA_OPT__ cannot be nested";
					return false;
				}
				if (!Macro.bFunctionLike || !Macro.bVariadic)
				{
					OutError = "__VA_OPT__ is only valid in a variadic macro";
					return false;
				}

				std::size_t OpenIndex = Index;
				for (;;)
				{
					while (OpenIndex < Body.size()
						&& (Body[OpenIndex] == ' ' || Body[OpenIndex] == '\t'
							|| Body[OpenIndex] == '\r' || Body[OpenIndex] == '\n'))
					{
						++OpenIndex;
					}
					if (OpenIndex + 1 < Body.size()
						&& Body[OpenIndex] == '/' && Body[OpenIndex + 1] == '*')
					{
						const std::size_t End = Body.find("*/", OpenIndex + 2);
						if (End == std::string_view::npos)
						{
							OutError = "unterminated block comment after __VA_OPT__";
							return false;
						}
						OpenIndex = End + 2;
						continue;
					}
					break;
				}
				if (OpenIndex >= Body.size() || Body[OpenIndex] != '(')
				{
					OutError = "__VA_OPT__ must be followed by '('";
					return false;
				}
				std::size_t CloseIndex = 0;
				if (!FindVaOptClose(Body, OpenIndex, CloseIndex, OutError))
				{
					return false;
				}
				const std::string_view Content =
					Body.substr(OpenIndex + 1, CloseIndex - OpenIndex - 1);
				if (VaOptHasBoundaryTokenPaste(Content))
				{
					OutError = "__VA_OPT__ content cannot begin or end with '##'";
					return false;
				}
				std::string ResolvedContent;
				if (!ResolveVaOpt(
						Macro,
						Content,
						bHasVariadicTokens,
						ResolvedContent,
						OutError,
						RawArgs,
						ExpandedArgs,
						Depth + 1))
				{
					return false;
				}

				// Like a macro parameter, __VA_OPT__(...) may be the operand of
				// stringization. Resolve and substitute its replacement list before
				// producing the string literal; the outer substitution pass then sees
				// an ordinary literal and cannot accidentally interpret its contents.
				std::size_t HashEnd = Out.size();
				while (HashEnd > 0
					&& (Out[HashEnd - 1] == ' ' || Out[HashEnd - 1] == '\t'
						|| Out[HashEnd - 1] == '\r' || Out[HashEnd - 1] == '\n'))
				{
					--HashEnd;
				}
				const bool bStringize = HashEnd > 0 && Out[HashEnd - 1] == '#'
					&& !(HashEnd > 1 && Out[HashEnd - 2] == '#');
				if (bStringize)
				{
					Out.erase(HashEnd - 1);
					std::string SubstitutedContent;
					if (bHasVariadicTokens
						&& (RawArgs == nullptr || ExpandedArgs == nullptr))
					{
						OutError = "internal preprocessor __VA_OPT__ argument error";
						return false;
					}
					if (bHasVariadicTokens
						&& !SubstituteResolvedMacroBody(
							Macro,
							ResolvedContent,
							*RawArgs,
							*ExpandedArgs,
							SubstitutedContent,
							OutError))
					{
						return false;
					}
					std::string PastedContent;
					if (!ApplyTokenPastes(SubstitutedContent, PastedContent, OutError))
					{
						return false;
					}
					AppendExpansion(Out, StringizeMacroArg(PastedContent));
				}
				else if (bHasVariadicTokens)
				{
					AppendExpansion(Out, ResolvedContent);
				}
				Index = CloseIndex + 1;
			}
			return true;
		}

		bool CollectMacroArguments(
			const std::string_view Text,
			std::size_t& Index,
			std::vector<std::string>& OutArgs,
			std::string& OutError)
		{
			OutArgs.clear();
			if (Index >= Text.size() || Text[Index] != '(')
			{
				OutError = "expected '(' after function-like macro";
				return false;
			}
			++Index;
			SkipHorizontalSpace(Text, Index);
			if (Index < Text.size() && Text[Index] == ')')
			{
				++Index;
				return true;
			}
			std::string Current;
			int Depth = 0;
			while (Index < Text.size())
			{
				const char Character = Text[Index];
				if (Character == '"'
					|| (Character == 'L' && Index + 1 < Text.size() && Text[Index + 1] == '"'))
				{
					const std::size_t Begin = Index;
					if (!SkipStringLiteral(Text, Index))
					{
						OutError = "unterminated string in macro arguments";
						return false;
					}
					AppendExpansion(Current, Text.data() + Begin, Index - Begin);
					continue;
				}
				if (Character == '\''
					|| (Character == 'L' && Index + 1 < Text.size()
						&& Text[Index + 1] == '\''))
				{
					const std::size_t Begin = Index;
					if (!SkipCharacterLiteral(Text, Index))
					{
						OutError = "unterminated character literal in macro arguments";
						return false;
					}
					AppendExpansion(Current, Text.data() + Begin, Index - Begin);
					continue;
				}
				if (Character == '/' && Index + 1 < Text.size())
				{
					if (Text[Index + 1] == '/')
					{
						// Line comment: remainder of this fragment is not argument text
						// (same as C phase 3). Usually hides the closing `)` → unterminated.
						Index = Text.size();
						break;
					}
					if (Text[Index + 1] == '*')
					{
						Index += 2;
						const std::size_t End = Text.find("*/", Index);
						if (End == std::string_view::npos)
						{
							OutError = "unterminated block comment in macro arguments";
							return false;
						}
						Index = End + 2;
						if (Current.empty()
							|| (Current.back() != ' ' && Current.back() != '\t'))
						{
							AppendExpansion(Current, ' ');
						}
						continue;
					}
				}
				if (Character == '(')
				{
					++Depth;
					AppendExpansion(Current, Character);
					++Index;
					continue;
				}
				if (Character == ')')
				{
					if (Depth == 0)
					{
						TrimTrailingHorizontalSpace(Current);
						OutArgs.push_back(std::move(Current));
						++Index;
						return true;
					}
					--Depth;
					AppendExpansion(Current, Character);
					++Index;
					continue;
				}
				if (Character == ',' && Depth == 0)
				{
					TrimTrailingHorizontalSpace(Current);
					OutArgs.push_back(std::move(Current));
					Current.clear();
					++Index;
					SkipHorizontalSpace(Text, Index);
					continue;
				}
				AppendExpansion(Current, Character);
				++Index;
			}
			OutError = "unterminated function-like macro invocation";
			return false;
		}

		bool OutputEndsWithTokenPaste(const std::string& Text)
		{
			std::size_t End = Text.size();
			while (End > 0
				&& (Text[End - 1] == ' ' || Text[End - 1] == '\t' || Text[End - 1] == '\r'))
			{
				--End;
			}
			return End >= 2 && Text[End - 2] == '#' && Text[End - 1] == '#';
		}

		bool LooksLikeTokenPasteAhead(const std::string_view Text, std::size_t Index)
		{
			SkipHorizontalSpace(Text, Index);
			return Index + 1 < Text.size() && Text[Index] == '#' && Text[Index + 1] == '#';
		}

		bool SubstituteResolvedMacroBody(
			const FMacro& Macro,
			const std::string_view Body,
			const std::vector<std::string>& RawArgs,
			const std::vector<std::string>& ExpandedArgs,
			std::string& Out,
			std::string& OutError)
		{
			Out.clear();
			std::size_t Index = 0;
			bool bInBlockComment = false;
			const auto TrimTrailingPasteOp = [&]()
			{
				while (!Out.empty()
					&& (Out.back() == ' ' || Out.back() == '\t' || Out.back() == '\r'))
				{
					Out.pop_back();
				}
				if (Out.size() >= 2 && Out[Out.size() - 2] == '#' && Out.back() == '#')
				{
					Out.pop_back();
					Out.pop_back();
				}
				while (!Out.empty()
					&& (Out.back() == ' ' || Out.back() == '\t' || Out.back() == '\r'))
				{
					Out.pop_back();
				}
			};
			const auto RemoveTrailingCommaAfterEmptyPaste = [&]()
			{
				// GCC / common extension: `, ## __VA_ARGS__` drops the comma when empty.
				if (!Out.empty() && Out.back() == ',')
				{
					Out.pop_back();
					while (!Out.empty()
						&& (Out.back() == ' ' || Out.back() == '\t' || Out.back() == '\r'))
					{
						Out.pop_back();
					}
				}
			};
			while (Index < Body.size())
			{
				if (bInBlockComment)
				{
					const std::size_t End = Body.find("*/", Index);
					if (End == std::string_view::npos)
					{
						AppendExpansion(Out, Body.data() + Index, Body.size() - Index);
						return true;
					}
					AppendExpansion(Out, Body.data() + Index, End + 2 - Index);
					Index = End + 2;
					bInBlockComment = false;
					continue;
				}
				const char Character = Body[Index];
				if (Character == '"'
					|| (Character == 'L' && Index + 1 < Body.size() && Body[Index + 1] == '"'))
				{
					const std::size_t Begin = Index;
					if (!SkipStringLiteral(Body, Index))
					{
						OutError = "unterminated string in macro body";
						return false;
					}
					AppendExpansion(Out, Body.data() + Begin, Index - Begin);
					continue;
				}
				if (Character == '\''
					|| (Character == 'L' && Index + 1 < Body.size()
						&& Body[Index + 1] == '\''))
				{
					const std::size_t Begin = Index;
					if (!SkipCharacterLiteral(Body, Index))
					{
						OutError = "unterminated character literal in macro body";
						return false;
					}
					AppendExpansion(Out, Body.data() + Begin, Index - Begin);
					continue;
				}
				if (Character == '/' && Index + 1 < Body.size())
				{
					if (Body[Index + 1] == '/')
					{
						AppendExpansion(Out, Body.data() + Index, Body.size() - Index);
						return true;
					}
					if (Body[Index + 1] == '*')
					{
						AppendExpansion(Out, '/');
						AppendExpansion(Out, '*');
						Index += 2;
						bInBlockComment = true;
						continue;
					}
				}
				if (Character == '#' && Index + 1 < Body.size() && Body[Index + 1] == '#')
				{
					AppendExpansion(Out, '#');
					AppendExpansion(Out, '#');
					Index += 2;
					continue;
				}
				if (Character == '#')
				{
					++Index;
					SkipHorizontalSpace(Body, Index);
					std::string ParamName;
					if (!ParseIdentifier(Body, Index, ParamName))
					{
						OutError = "'#' is not followed by a macro parameter";
						return false;
					}
					if (ParamName == VaArgsName && !Macro.bVariadic)
					{
						OutError = "__VA_ARGS__ is only valid in a variadic macro";
						return false;
					}
					const int ParamIndex = FindMacroParamIndex(Macro, ParamName);
					if (ParamIndex < 0)
					{
						OutError = "'#' is not followed by a macro parameter";
						return false;
					}
					AppendExpansion(Out, StringizeMacroArg(RawArgs[static_cast<std::size_t>(ParamIndex)]));
					continue;
				}
				if (IsIdentStart(Character))
				{
					std::string Name;
					if (!ParseIdentifier(Body, Index, Name))
					{
						OutError = "internal preprocessor identifier error";
						return false;
					}
					if (Name == VaArgsName && !Macro.bVariadic)
					{
						OutError = "__VA_ARGS__ is only valid in a variadic macro";
						return false;
					}
					const int ParamIndex = FindMacroParamIndex(Macro, Name);
					if (ParamIndex >= 0)
					{
						const std::size_t Slot = static_cast<std::size_t>(ParamIndex);
						const bool bPasteBefore = OutputEndsWithTokenPaste(Out);
						const bool bPasteAfter = LooksLikeTokenPasteAhead(Body, Index);
						const bool bRaw = bPasteBefore || bPasteAfter;
						const std::string& Replacement =
							bRaw ? RawArgs[Slot] : ExpandedArgs[Slot];
						if (Replacement.empty() && (bPasteBefore || bPasteAfter))
						{
							// Empty placemarker: drop adjacent ## (both sides if
							// `x ## EMPTY ## y`), and a preceding comma for
							// `, ## __VA_ARGS__`.
							if (bPasteBefore)
							{
								TrimTrailingPasteOp();
								RemoveTrailingCommaAfterEmptyPaste();
							}
							if (bPasteAfter)
							{
								SkipHorizontalSpace(Body, Index);
								Index += 2;
								SkipHorizontalSpace(Body, Index);
							}
						}
						else
						{
							AppendExpansion(Out, Replacement);
						}
					}
					else
					{
						AppendExpansion(Out, Name);
					}
					continue;
				}
				AppendExpansion(Out, Character);
				++Index;
			}
			return true;
		}

		bool SubstituteFunctionMacroBody(
			const FMacro& Macro,
			const std::vector<std::string>& RawArgs,
			const std::vector<std::string>& ExpandedArgs,
			std::string& Out,
			std::string& OutError)
		{
			Out.clear();
			// Match standard va-opt substitution: emptiness is determined after the
			// variadic argument has undergone ordinary macro expansion. For example,
			// `F(EMPTY)` takes the empty branch when EMPTY expands to no tokens.
			const bool bHasVariadicTokens =
				Macro.bVariadic && ExpandedArgs.size() > Macro.Arguments().size() &&
				VariadicArgumentHasTokens(ExpandedArgs[Macro.Arguments().size()]);
			std::string ResolvedBody;
			if (!ResolveVaOpt(Macro, Macro.Replacement(), bHasVariadicTokens, ResolvedBody,
							  OutError, &RawArgs, &ExpandedArgs))
			{
				return false;
			}
			return SubstituteResolvedMacroBody(
				Macro, ResolvedBody, RawArgs, ExpandedArgs, Out, OutError);
		}

		bool ApplyTokenPastes(
			const std::string_view Text,
			std::string& Out,
			std::string& OutError)
		{
			Out.clear();
			std::size_t Index = 0;
			bool bInBlockComment = false;
			while (Index < Text.size())
			{
				if (bInBlockComment)
				{
					const std::size_t End = Text.find("*/", Index);
					if (End == std::string_view::npos)
					{
						AppendExpansion(Out, Text.data() + Index, Text.size() - Index);
						return true;
					}
					AppendExpansion(Out, Text.data() + Index, End + 2 - Index);
					Index = End + 2;
					bInBlockComment = false;
					continue;
				}
				const char Character = Text[Index];
				if (Character == '"'
					|| (Character == 'L' && Index + 1 < Text.size() && Text[Index + 1] == '"'))
				{
					const std::size_t Begin = Index;
					if (!SkipStringLiteral(Text, Index))
					{
						OutError = "unterminated string in macro token paste";
						return false;
					}
					AppendExpansion(Out, Text.data() + Begin, Index - Begin);
					continue;
				}
				if (Character == '\''
					|| (Character == 'L' && Index + 1 < Text.size()
						&& Text[Index + 1] == '\''))
				{
					const std::size_t Begin = Index;
					if (!SkipCharacterLiteral(Text, Index))
					{
						OutError = "unterminated character literal in macro token paste";
						return false;
					}
					AppendExpansion(Out, Text.data() + Begin, Index - Begin);
					continue;
				}
				if (Character == '/' && Index + 1 < Text.size())
				{
					if (Text[Index + 1] == '/')
					{
						AppendExpansion(Out, Text.data() + Index, Text.size() - Index);
						return true;
					}
					if (Text[Index + 1] == '*')
					{
						AppendExpansion(Out, '/');
						AppendExpansion(Out, '*');
						Index += 2;
						bInBlockComment = true;
						continue;
					}
				}
				if (Character == '#' && Index + 1 < Text.size() && Text[Index + 1] == '#')
				{
					while (!Out.empty()
						&& (Out.back() == ' ' || Out.back() == '\t' || Out.back() == '\r'))
					{
						Out.pop_back();
					}
					Index += 2;
					SkipHorizontalSpace(Text, Index);
					continue;
				}
				AppendExpansion(Out, Character);
				++Index;
			}
			return true;
		}

		bool ExpandFragment(
			const std::string_view Text,
			const FMacroMap& Macros,
			std::unordered_set<std::string>& Expanding,
			bool& bInBlockComment,
			std::string& Out,
			std::string& OutError,
			const FMacroExpansionContext& Context,
			const int Depth,
			std::vector<std::size_t>* const OutToIn = nullptr);

		bool ExpandFunctionMacroInvocation(
			const std::string& Name,
			const FMacro& Macro,
			const std::string_view Text,
			std::size_t& Index,
			const FMacroMap& Macros,
			std::unordered_set<std::string>& Expanding,
			std::string& OutExpanded,
			std::string& OutError,
			const FMacroExpansionContext& Context,
			const int Depth)
		{
			if (Depth >= MaxMacroExpansionDepth)
			{
				OutError = "macro expansion nested too deeply";
				return false;
			}
			std::vector<std::string> Collected;
			if (!CollectMacroArguments(Text, Index, Collected, OutError))
			{
				return false;
			}
			std::vector<std::string> RawArgs;
			if (!BindMacroArguments(Name, Macro, std::move(Collected), RawArgs, OutError))
			{
				return false;
			}
			std::vector<std::string> ExpandedArgs;
			ExpandedArgs.reserve(RawArgs.size());
			for (const std::string& Arg : RawArgs)
			{
				std::string ExpandedArg;
				bool bArgComment = false;
				if (!ExpandFragment(
						Arg,
						Macros,
						Expanding,
						bArgComment,
						ExpandedArg,
						OutError,
						Context,
						Depth + 1,
						nullptr))
				{
					return false;
				}
				if (bArgComment)
				{
					OutError = "unterminated block comment in macro argument";
					return false;
				}
				ExpandedArgs.push_back(std::move(ExpandedArg));
			}
			std::string Substituted;
			if (!SubstituteFunctionMacroBody(
					Macro, RawArgs, ExpandedArgs, Substituted, OutError))
			{
				return false;
			}
			std::string Pasted;
			if (!ApplyTokenPastes(Substituted, Pasted, OutError))
			{
				return false;
			}
			Expanding.insert(Name);
			bool bBodyComment = false;
			const bool bOk = ExpandFragment(
				Pasted,
				Macros,
				Expanding,
				bBodyComment,
				OutExpanded,
				OutError,
				Context,
				Depth + 1,
				nullptr);
			Expanding.erase(Name);
			return bOk;
		}

		bool ExpandFragment(
			const std::string_view Text,
			const FMacroMap& Macros,
			std::unordered_set<std::string>& Expanding,
			bool& bInBlockComment,
			std::string& Out,
			std::string& OutError,
			const FMacroExpansionContext& Context,
			const int Depth,
			std::vector<std::size_t>* const OutToIn)
		{
			CheckAnalysisCancellation();
			if (Depth >= MaxMacroExpansionDepth)
			{
				OutError = "macro expansion nested too deeply";
				return false;
			}
			Out.clear();
			CheckExpansionSize(0, Text.size());
			Out.reserve(Text.size());
			if (OutToIn)
			{
				OutToIn->clear();
				OutToIn->reserve(Text.size());
			}
			const auto EmitAt = [&](const char Char, const std::size_t InIndex)
			{
				AppendExpansion(Out, Char);
				if (OutToIn)
				{
					OutToIn->push_back(InIndex);
				}
			};
			const auto EmitRange = [&](const std::size_t Begin, const std::size_t End)
			{
				for (std::size_t I = Begin; I < End; ++I)
				{
					EmitAt(Text[I], I);
				}
			};
			const auto EmitMappedSpan = [&](const std::string_view Span, const std::size_t InIndex)
			{
				for (std::size_t I = 0; I < Span.size(); ++I)
				{
					EmitAt(Span[I], InIndex);
				}
			};
			std::size_t Index = 0;
			/**
			 * After a macro expands to a single identifier that names a function-like
			 * macro, consume a following `(` in the *source* (C rescan glue).
			 * Enables `#define max MAX` / `#define MAX(a,b) ...` + `max(1,2)`, and
			 * `#define F() G` / `#define G() 1` + `F()()`.
			 *
			 * JustExpandedName (and each name glued in this chain) is not re-glued:
			 * `#define F() F` + `F()()` leaves the trailing `()` (painted-name rule).
			 */
			const auto TrimIsSingleIdentifier =
				[](const std::string_view Span, std::string& OutName) -> bool
				{
					std::size_t Cursor = 0;
					SkipHorizontalSpace(Span, Cursor);
					if (Cursor >= Span.size() || !IsIdentStart(Span[Cursor]))
					{
						return false;
					}
					if (!ParseIdentifier(Span, Cursor, OutName))
					{
						return false;
					}
					SkipHorizontalSpace(Span, Cursor);
					return Cursor == Span.size();
				};
			const auto GlueTrailingFunctionLikeCalls =
				[&](std::string& ExpandedBody,
					const std::string& JustExpandedName,
					const FMacroExpansionContext& InvocationContext) -> bool
				{
					std::unordered_set<std::string> NoReglue;
					NoReglue.insert(JustExpandedName);
					for (;;)
					{
						std::string Ident;
						if (!TrimIsSingleIdentifier(ExpandedBody, Ident)
								|| Expanding.find(Ident) != Expanding.end()
								|| NoReglue.find(Ident) != NoReglue.end())
						{
							return true;
						}
						const auto Found = Macros.find(Ident);
						if (Found == Macros.end() || !Found->second.bFunctionLike)
						{
							return true;
						}
						const std::size_t SavedIndex = Index;
						SkipHorizontalSpace(Text, Index);
						if (Index >= Text.size() || Text[Index] != '(')
						{
							Index = SavedIndex;
							return true;
						}
						std::string Glued;
						if (!ExpandFunctionMacroInvocation(
									Ident,
									Found->second,
									Text,
									Index,
									Macros,
									Expanding,
									Glued,
									OutError,
									InvocationContext,
									Depth + 1))
						{
							return false;
						}
						NoReglue.insert(Ident);
						ExpandedBody = std::move(Glued);
					}
				};

			while (Index < Text.size())
			{
				if ((Index & 1023u) == 0)
				{
					CheckAnalysisCancellation();
				}
				if (bInBlockComment)
				{
					const std::size_t End = Text.find("*/", Index);
					if (End == std::string_view::npos)
					{
						EmitRange(Index, Text.size());
						return true;
					}
					EmitRange(Index, End + 2);
					Index = End + 2;
					bInBlockComment = false;
					continue;
				}

				const char Character = Text[Index];
				if (Character == '"'
					|| (Character == 'L'
						&& Index + 1 < Text.size()
						&& Text[Index + 1] == '"'))
				{
					if (Character == 'L')
					{
						EmitAt('L', Index);
						++Index;
					}
					EmitAt('"', Index);
					++Index;
					while (Index < Text.size())
					{
						const char QuoteChar = Text[Index];
						EmitAt(QuoteChar, Index);
						++Index;
						if (QuoteChar == '\\' && Index < Text.size())
						{
							EmitAt(Text[Index], Index);
							++Index;
							continue;
						}
						if (QuoteChar == '"')
						{
							break;
						}
					}
					continue;
				}

				if (Character == '\''
					|| (Character == 'L'
						&& Index + 1 < Text.size()
						&& Text[Index + 1] == '\''))
				{
					const std::size_t Begin = Index;
					if (!SkipCharacterLiteral(Text, Index))
					{
						OutError = "unterminated character literal in macro expansion";
						return false;
					}
					EmitRange(Begin, Index);
					continue;
				}

				if (Character == '/' && Index + 1 < Text.size())
				{
					if (Text[Index + 1] == '/')
					{
						EmitRange(Index, Text.size());
						return true;
					}
					if (Text[Index + 1] == '*')
					{
						EmitAt('/', Index);
						EmitAt('*', Index + 1);
						Index += 2;
						bInBlockComment = true;
						continue;
					}
				}

				if (IsIdentStart(Character))
				{
					const std::size_t NameStart = Index;
					std::string Name;
					if (!ParseIdentifier(Text, Index, Name))
					{
						OutError = "internal preprocessor identifier error";
						return false;
					}
					const auto Found = Macros.find(Name);
					if (Found != Macros.end() && Expanding.find(Name) == Expanding.end())
					{
						const FMacroExpansionContext InvocationContext =
							Context.AtOffset(NameStart);
						const FMacro& Macro = Found->second;
						if (Macro.Builtin != FMacro::EBuiltin::None)
						{
							const std::string Replacement =
								Macro.Builtin == FMacro::EBuiltin::File
								? std::string(Context.FileLiteral)
								: std::to_string(InvocationContext.Line);
							EmitMappedSpan(Replacement, NameStart);
							continue;
						}
						if (Macro.bFunctionLike)
						{
							const std::size_t AfterName = Index;
							SkipHorizontalSpace(Text, Index);
							if (Index < Text.size() && Text[Index] == '(')
							{
								std::string ExpandedBody;
								if (!ExpandFunctionMacroInvocation(
										Name,
										Macro,
										Text,
										Index,
										Macros,
										Expanding,
									ExpandedBody,
									OutError,
									InvocationContext,
									Depth + 1))
								{
									return false;
								}
								if (!GlueTrailingFunctionLikeCalls(
										ExpandedBody, Name, InvocationContext))
								{
									return false;
								}
								EmitMappedSpan(ExpandedBody, NameStart);
								continue;
							}
							Index = AfterName;
							for (std::size_t I = 0; I < Name.size(); ++I)
							{
								EmitAt(Name[I], NameStart + I);
							}
							continue;
						}
						Expanding.insert(Name);
						std::string ExpandedBody;
						bool bBodyComment = false;
						if (!ExpandFragment(Macro.Replacement(), Macros, Expanding, bBodyComment,
											ExpandedBody, OutError, InvocationContext, Depth + 1,
											nullptr))
						{
							Expanding.erase(Name);
							return false;
						}
						Expanding.erase(Name);
						if (!GlueTrailingFunctionLikeCalls(
								ExpandedBody, Name, InvocationContext))
						{
							return false;
						}
						EmitMappedSpan(ExpandedBody, NameStart);
					}
					else
					{
						for (std::size_t I = 0; I < Name.size(); ++I)
						{
							EmitAt(Name[I], NameStart + I);
						}
					}
					continue;
				}

				EmitAt(Character, Index);
				++Index;
			}
			return true;
		}

		/**
		 * Collect directive payload from Line[Begin..), replacing comments with spaces.
		 * A line comment ends the payload. An unclosed slash-star block-comment opener
		 * ends the payload and sets bInBlockComment.
		 * Strings are preserved verbatim.
		 */
		bool EndsWithLineContinuation(const std::string_view Line)
		{
			std::size_t End = Line.size();
			if (End > 0 && Line[End - 1] == '\r')
			{
				--End;
			}
			return End > 0 && Line[End - 1] == '\\';
		}

		std::string_view WithoutTrailingContinuation(const std::string_view Line)
		{
			std::size_t End = Line.size();
			if (End > 0 && Line[End - 1] == '\r')
			{
				--End;
			}
			if (End > 0 && Line[End - 1] == '\\')
			{
				--End;
			}
			return Line.substr(0, End);
		}

		void CollectDirectivePayload(
			const std::string_view Line,
			const std::size_t Begin,
			std::string& OutPayload,
			bool& bInBlockComment)
		{
			OutPayload.clear();
			std::size_t Index = Begin;
			while (Index < Line.size() && Line[Index] != '\r')
			{
				if (Line[Index] == '"'
					|| (Line[Index] == 'L'
						&& Index + 1 < Line.size()
						&& Line[Index + 1] == '"'))
				{
					if (Line[Index] == 'L')
					{
						AppendExpansion(OutPayload, 'L');
						++Index;
					}
					AppendExpansion(OutPayload, '"');
					++Index;
					while (Index < Line.size() && Line[Index] != '\r')
					{
						const char QuoteChar = Line[Index];
						AppendExpansion(OutPayload, QuoteChar);
						++Index;
						if (QuoteChar == '\\' && Index < Line.size() && Line[Index] != '\r')
						{
							AppendExpansion(OutPayload, Line[Index]);
							++Index;
							continue;
						}
						if (QuoteChar == '"')
						{
							break;
						}
					}
					continue;
				}
				if (Index + 1 < Line.size() && Line[Index] == '/' && Line[Index + 1] == '/')
				{
					break;
				}
				if (Index + 1 < Line.size() && Line[Index] == '/' && Line[Index + 1] == '*')
				{
					Index += 2;
					bool bClosed = false;
					while (Index < Line.size() && Line[Index] != '\r')
					{
						if (Index + 1 < Line.size()
							&& Line[Index] == '*'
							&& Line[Index + 1] == '/')
						{
							Index += 2;
							AppendExpansion(OutPayload, ' ');
							bClosed = true;
							break;
						}
						++Index;
					}
					if (!bClosed)
					{
						bInBlockComment = true;
						break;
					}
					continue;
				}
				AppendExpansion(OutPayload, Line[Index]);
				++Index;
			}
			TrimTrailingHorizontalSpace(OutPayload);
		}

		// Replace defined(NAME) / defined NAME with 0/1 before macro expand.
		bool RewriteDefinedOperators(
			const std::string_view Expr,
			const FMacroMap& Macros,
			std::string& Out,
			std::string& OutError)
		{
			Out.clear();
			std::size_t Index = 0;
			while (Index < Expr.size())
			{
				if (IsIdentStart(Expr[Index]))
				{
					std::string Name;
					const std::size_t NameBegin = Index;
					if (!ParseIdentifier(Expr, Index, Name))
					{
						OutError = "invalid #if expression";
						return false;
					}
					if (Name == "defined")
					{
						SkipHorizontalSpace(Expr, Index);
						std::string Target;
						if (Index < Expr.size() && Expr[Index] == '(')
						{
							++Index;
							SkipHorizontalSpace(Expr, Index);
							if (!ParseIdentifier(Expr, Index, Target))
							{
								OutError = "defined() requires an identifier";
								return false;
							}
							SkipHorizontalSpace(Expr, Index);
							if (Index >= Expr.size() || Expr[Index] != ')')
							{
								OutError = "missing ')' after defined(";
								return false;
							}
							++Index;
						}
						else if (!ParseIdentifier(Expr, Index, Target))
						{
							OutError = "defined requires an identifier";
							return false;
						}
						AppendExpansion(Out, Macros.find(Target) != Macros.end() ? '1' : '0');
						continue;
					}
					AppendExpansion(Out, Expr.data() + NameBegin, Index - NameBegin);
					continue;
				}
				AppendExpansion(Out, Expr[Index]);
				++Index;
			}
			return true;
		}

		struct FIfParser
		{
			bool EnterExpressionDepth()
			{
				constexpr int MaxIfExpressionDepth = 256;
				if (++ExpressionDepth <= MaxIfExpressionDepth)
				{
					return true;
				}
				--ExpressionDepth;
				return Fail("#if expression nested too deeply");
			}

			void LeaveExpressionDepth()
			{
				if (ExpressionDepth > 0)
				{
					--ExpressionDepth;
				}
			}

			void Skip()
			{
				SkipHorizontalSpace(Text, Index);
			}

			bool Fail(const char* Message)
			{
				Error = Message;
				return false;
			}

			bool SoftArithmeticFail(const char* Message, std::int64_t& OutValue)
			{
				if (EvalSkipDepth > 0)
				{
					OutValue = 0;
					return true;
				}
				return Fail(Message);
			}

			bool ParseInt(std::int64_t& OutValue)
			{
				Skip();
				if (Index >= Text.size() || Text[Index] < '0' || Text[Index] > '9')
				{
					return Fail("expected integer in #if expression");
				}
				unsigned long long Parsed = 0;
				if (Index + 1 < Text.size()
					&& Text[Index] == '0'
					&& (Text[Index + 1] == 'x' || Text[Index + 1] == 'X'))
				{
					Index += 2;
					if (Index >= Text.size() || !std::isxdigit(static_cast<unsigned char>(Text[Index])))
					{
						return Fail("invalid hex integer in #if expression");
					}
					while (Index < Text.size()
						&& std::isxdigit(static_cast<unsigned char>(Text[Index])))
					{
						const char DigitChar = Text[Index++];
						unsigned long long Digit = 0;
						if (DigitChar >= '0' && DigitChar <= '9')
						{
							Digit = static_cast<unsigned long long>(DigitChar - '0');
						}
						else if (DigitChar >= 'a' && DigitChar <= 'f')
						{
							Digit = static_cast<unsigned long long>(10 + DigitChar - 'a');
						}
						else
						{
							Digit = static_cast<unsigned long long>(10 + DigitChar - 'A');
						}
						if (!AccumulateIfDigit(Parsed, 16ull, Digit))
						{
							return Fail("#if integer constant out of range");
						}
					}
				}
				else if (Text[Index] == '0')
				{
					// Octal (C preprocessor); lone 0 is zero.
					++Index;
					while (Index < Text.size() && Text[Index] >= '0' && Text[Index] <= '7')
					{
						const unsigned long long Digit =
							static_cast<unsigned long long>(Text[Index] - '0');
						if (!AccumulateIfDigit(Parsed, 8ull, Digit))
						{
							return Fail("#if integer constant out of range");
						}
						++Index;
					}
					if (Index < Text.size() && Text[Index] >= '8' && Text[Index] <= '9')
					{
						return Fail("invalid octal integer in #if expression");
					}
				}
				else
				{
					while (Index < Text.size() && Text[Index] >= '0' && Text[Index] <= '9')
					{
						const unsigned long long Digit =
							static_cast<unsigned long long>(Text[Index] - '0');
						if (!AccumulateIfDigit(Parsed, 10ull, Digit))
						{
							return Fail("#if integer constant out of range");
						}
						++Index;
					}
				}
				// Optional integer suffixes (ignored).
				while (Index < Text.size()
					&& (Text[Index] == 'u' || Text[Index] == 'U'
						|| Text[Index] == 'l' || Text[Index] == 'L'))
				{
					++Index;
				}
				OutValue = static_cast<std::int64_t>(Parsed);
				return true;
			}

			bool ParsePrimary(std::int64_t& OutValue)
			{
				if (!EnterExpressionDepth())
				{
					return false;
				}
				struct FDepthGuard
				{
					~FDepthGuard()
					{
						Parser.LeaveExpressionDepth();
					}

					FIfParser& Parser;

				} DepthGuard{*this};
				Skip();
				if (Index >= Text.size())
				{
					return Fail("unexpected end of #if expression");
				}
				if (Text[Index] == '(')
				{
					++Index;
					if (!ParseExpr(OutValue))
					{
						return false;
					}
					Skip();
					if (Index >= Text.size() || Text[Index] != ')')
					{
						return Fail("missing ')' in #if expression");
					}
					++Index;
					return true;
				}
				if (IsIdentStart(Text[Index]))
				{
					std::string Name;
					if (!ParseIdentifier(Text, Index, Name))
					{
						return Fail("invalid identifier in #if expression");
					}
					// Undefined / leftover identifiers are 0 (C preprocessor rule).
					OutValue = 0;
					return true;
				}
				return ParseInt(OutValue);
			}

			bool ParseUnary(std::int64_t& OutValue)
			{
				if (!EnterExpressionDepth())
				{
					return false;
				}
				struct FDepthGuard
				{
					~FDepthGuard()
					{
						Parser.LeaveExpressionDepth();
					}

					FIfParser& Parser;

				} DepthGuard{*this};
				Skip();
				if (Index < Text.size())
				{
					if (Text[Index] == '+')
					{
						++Index;
						return ParseUnary(OutValue);
					}
					if (Text[Index] == '-')
					{
						++Index;
						std::int64_t Value = 0;
						if (!ParseUnary(Value))
						{
							return false;
						}
						if (Value == std::numeric_limits<std::int64_t>::min())
						{
							if (EvalSkipDepth > 0)
							{
								OutValue = 0;
								return true;
							}
							return Fail("negation overflow in #if expression");
						}
						OutValue = -Value;
						return true;
					}
					if (Text[Index] == '!')
					{
						++Index;
						std::int64_t Value = 0;
						if (!ParseUnary(Value))
						{
							return false;
						}
						OutValue = Value == 0 ? 1 : 0;
						return true;
					}
					if (Text[Index] == '~')
					{
						++Index;
						std::int64_t Value = 0;
						if (!ParseUnary(Value))
						{
							return false;
						}
						OutValue = ~Value;
						return true;
					}
				}
				return ParsePrimary(OutValue);
			}

			bool ParseMul(std::int64_t& OutValue)
			{
				if (!ParseUnary(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index >= Text.size())
					{
						return true;
					}
					const char Op = Text[Index];
					if (Op != '*' && Op != '/' && Op != '%')
					{
						return true;
					}
					++Index;
					std::int64_t Right = 0;
					if (!ParseUnary(Right))
					{
						return false;
					}
					if (Op == '*')
					{
						std::int64_t Product = 0;
						if (!CheckedMulInt64(OutValue, Right, Product))
						{
							return SoftArithmeticFail("multiplication overflow in #if expression", OutValue);
						}
						OutValue = Product;
					}
					else if (Right == 0)
					{
						return SoftArithmeticFail("division by zero in #if expression", OutValue);
					}
					else if (OutValue == std::numeric_limits<std::int64_t>::min()
						&& Right == -1)
					{
						return SoftArithmeticFail("division overflow in #if expression", OutValue);
					}
					else if (Op == '/')
					{
						OutValue /= Right;
					}
					else
					{
						OutValue %= Right;
					}
				}
			}

			bool ParseAdd(std::int64_t& OutValue)
			{
				if (!ParseMul(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index >= Text.size() || (Text[Index] != '+' && Text[Index] != '-'))
					{
						return true;
					}
					const char Op = Text[Index++];
					std::int64_t Right = 0;
					if (!ParseMul(Right))
					{
						return false;
					}
					std::int64_t Result = 0;
					const bool bOk = Op == '+'
						? CheckedAddInt64(OutValue, Right, Result)
						: CheckedSubInt64(OutValue, Right, Result);
					if (!bOk)
					{
						return SoftArithmeticFail(
							Op == '+'
							? "addition overflow in #if expression"
							: "subtraction overflow in #if expression",
							OutValue);
					}
					OutValue = Result;
				}
			}

			bool ParseShift(std::int64_t& OutValue)
			{
				if (!ParseAdd(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index + 1 >= Text.size())
					{
						return true;
					}
					if (Text[Index] == '<' && Text[Index + 1] == '<')
					{
						Index += 2;
						std::int64_t Right = 0;
						if (!ParseAdd(Right))
						{
							return false;
						}
						if (Right < 0 || Right >= 64)
						{
							return SoftArithmeticFail(
								"shift count out of range in #if expression",
								OutValue);
						}
						if (OutValue < 0)
						{
							return SoftArithmeticFail(
								"left shift of negative value in #if expression",
								OutValue);
						}
						if (Right > 0
							&& OutValue > (std::numeric_limits<std::int64_t>::max() >> Right))
						{
							return SoftArithmeticFail(
								"left shift overflow in #if expression",
								OutValue);
						}
						OutValue <<= Right;
					}
					else if (Text[Index] == '>' && Text[Index + 1] == '>')
					{
						Index += 2;
						std::int64_t Right = 0;
						if (!ParseAdd(Right))
						{
							return false;
						}
						if (Right < 0 || Right >= 64)
						{
							return SoftArithmeticFail(
								"shift count out of range in #if expression",
								OutValue);
						}
						OutValue >>= Right;
					}
					else
					{
						return true;
					}
				}
			}

			bool ParseRel(std::int64_t& OutValue)
			{
				if (!ParseShift(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index >= Text.size())
					{
						return true;
					}
					int Op = 0; // 1< 2<= 3> 4>=
					if (Text[Index] == '<' && (Index + 1 >= Text.size() || Text[Index + 1] != '<'))
					{
						if (Index + 1 < Text.size() && Text[Index + 1] == '=')
						{
							Op = 2;
							Index += 2;
						}
						else
						{
							Op = 1;
							++Index;
						}
					}
					else if (Text[Index] == '>' && (Index + 1 >= Text.size() || Text[Index + 1] != '>'))
					{
						if (Index + 1 < Text.size() && Text[Index + 1] == '=')
						{
							Op = 4;
							Index += 2;
						}
						else
						{
							Op = 3;
							++Index;
						}
					}
					else
					{
						return true;
					}
					std::int64_t Right = 0;
					if (!ParseShift(Right))
					{
						return false;
					}
					if (Op == 1)
					{
						OutValue = OutValue < Right ? 1 : 0;
					}
					else if (Op == 2)
					{
						OutValue = OutValue <= Right ? 1 : 0;
					}
					else if (Op == 3)
					{
						OutValue = OutValue > Right ? 1 : 0;
					}
					else
					{
						OutValue = OutValue >= Right ? 1 : 0;
					}
				}
			}

			bool ParseEq(std::int64_t& OutValue)
			{
				if (!ParseRel(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index + 1 >= Text.size())
					{
						return true;
					}
					int Op = 0;
					if (Text[Index] == '=' && Text[Index + 1] == '=')
					{
						Op = 1;
						Index += 2;
					}
					else if (Text[Index] == '!' && Text[Index + 1] == '=')
					{
						Op = 2;
						Index += 2;
					}
					else
					{
						return true;
					}
					std::int64_t Right = 0;
					if (!ParseRel(Right))
					{
						return false;
					}
					OutValue = Op == 1
						? (OutValue == Right ? 1 : 0)
						: (OutValue != Right ? 1 : 0);
				}
			}

			bool ParseBitAnd(std::int64_t& OutValue)
			{
				if (!ParseEq(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index >= Text.size() || Text[Index] != '&'
						|| (Index + 1 < Text.size() && Text[Index + 1] == '&'))
					{
						return true;
					}
					++Index;
					std::int64_t Right = 0;
					if (!ParseEq(Right))
					{
						return false;
					}
					OutValue &= Right;
				}
			}

			bool ParseBitXor(std::int64_t& OutValue)
			{
				if (!ParseBitAnd(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index >= Text.size() || Text[Index] != '^')
					{
						return true;
					}
					++Index;
					std::int64_t Right = 0;
					if (!ParseBitAnd(Right))
					{
						return false;
					}
					OutValue ^= Right;
				}
			}

			bool ParseBitOr(std::int64_t& OutValue)
			{
				if (!ParseBitXor(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index >= Text.size() || Text[Index] != '|'
						|| (Index + 1 < Text.size() && Text[Index + 1] == '|'))
					{
						return true;
					}
					++Index;
					std::int64_t Right = 0;
					if (!ParseBitXor(Right))
					{
						return false;
					}
					OutValue |= Right;
				}
			}

			bool ParseAnd(std::int64_t& OutValue)
			{
				if (!ParseBitOr(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index + 1 >= Text.size() || Text[Index] != '&' || Text[Index + 1] != '&')
					{
						return true;
					}
					Index += 2;
					if (OutValue == 0)
					{
						++EvalSkipDepth;
						std::int64_t Right = 0;
						const bool bOk = ParseBitOr(Right);
						--EvalSkipDepth;
						if (!bOk)
						{
							return false;
						}
						continue;
					}
					std::int64_t Right = 0;
					if (!ParseBitOr(Right))
					{
						return false;
					}
					OutValue = Right != 0 ? 1 : 0;
				}
			}

			bool ParseOr(std::int64_t& OutValue)
			{
				if (!ParseAnd(OutValue))
				{
					return false;
				}
				for (;;)
				{
					Skip();
					if (Index + 1 >= Text.size() || Text[Index] != '|' || Text[Index + 1] != '|')
					{
						return true;
					}
					Index += 2;
					if (OutValue != 0)
					{
						++EvalSkipDepth;
						std::int64_t Right = 0;
						const bool bOk = ParseAnd(Right);
						--EvalSkipDepth;
						if (!bOk)
						{
							return false;
						}
						OutValue = 1;
						continue;
					}
					std::int64_t Right = 0;
					if (!ParseAnd(Right))
					{
						return false;
					}
					OutValue = Right != 0 ? 1 : 0;
				}
			}

			bool ParseCond(std::int64_t& OutValue)
			{
				if (!EnterExpressionDepth())
				{
					return false;
				}
				struct FDepthGuard
				{
					~FDepthGuard()
					{
						Parser.LeaveExpressionDepth();
					}

					FIfParser& Parser;

				} DepthGuard{*this};
				if (!ParseOr(OutValue))
				{
					return false;
				}
				Skip();
				if (Index >= Text.size() || Text[Index] != '?')
				{
					return true;
				}
				++Index;
				if (OutValue != 0)
				{
					std::int64_t ThenValue = 0;
					if (!ParseCond(ThenValue))
					{
						return false;
					}
					Skip();
					if (Index >= Text.size() || Text[Index] != ':')
					{
						return Fail("missing ':' in #if ?: expression");
					}
					++Index;
					++EvalSkipDepth;
					std::int64_t ElseValue = 0;
					const bool bOk = ParseCond(ElseValue);
					--EvalSkipDepth;
					if (!bOk)
					{
						return false;
					}
					OutValue = ThenValue;
					return true;
				}
				++EvalSkipDepth;
				std::int64_t ThenValue = 0;
				const bool bThenOk = ParseCond(ThenValue);
				--EvalSkipDepth;
				if (!bThenOk)
				{
					return false;
				}
				Skip();
				if (Index >= Text.size() || Text[Index] != ':')
				{
					return Fail("missing ':' in #if ?: expression");
				}
				++Index;
				std::int64_t ElseValue = 0;
				if (!ParseCond(ElseValue))
				{
					return false;
				}
				OutValue = ElseValue;
				return true;
			}

			bool ParseExpr(std::int64_t& OutValue)
			{
				return ParseCond(OutValue);
			}

			bool ParseAll(std::int64_t& OutValue)
			{
				if (!ParseExpr(OutValue))
				{
					return false;
				}
				Skip();
				if (Index < Text.size() && Text[Index] != '\r')
				{
					return Fail("unexpected tokens in #if expression");
				}
				return true;
			}

			std::string_view Text;
			std::size_t Index = 0;
			std::string Error;
			// When > 0, arithmetic side effects (e.g. div-by-zero) are ignored for short-circuit.
			int EvalSkipDepth = 0;
			int ExpressionDepth = 0;

		};

		// Replace line/block comments with spaces before #if parsing.
		bool StripCommentsToSpace(
			const std::string_view Text,
			std::string& Out,
			std::string& OutError)
		{
			Out.clear();
			CheckExpansionSize(0, Text.size());
			Out.reserve(Text.size());
			std::size_t Index = 0;
			bool bInBlockComment = false;
			while (Index < Text.size())
			{
				if (bInBlockComment)
				{
					if (Index + 1 < Text.size() && Text[Index] == '*' && Text[Index + 1] == '/')
					{
						AppendExpansion(Out, ' ');
						AppendExpansion(Out, ' ');
						Index += 2;
						bInBlockComment = false;
						continue;
					}
					AppendExpansion(Out, ' ');
					++Index;
					continue;
				}
				if (Text[Index] == '"'
					|| (Text[Index] == 'L'
						&& Index + 1 < Text.size()
						&& Text[Index + 1] == '"'))
				{
					if (Text[Index] == 'L')
					{
						AppendExpansion(Out, 'L');
						++Index;
					}
					AppendExpansion(Out, '"');
					++Index;
					while (Index < Text.size())
					{
						const char QuoteChar = Text[Index];
						AppendExpansion(Out, QuoteChar);
						++Index;
						if (QuoteChar == '\\' && Index < Text.size())
						{
							AppendExpansion(Out, Text[Index]);
							++Index;
							continue;
						}
						if (QuoteChar == '"')
						{
							break;
						}
					}
					continue;
				}
				if (Index + 1 < Text.size() && Text[Index] == '/' && Text[Index + 1] == '/')
				{
					while (Index < Text.size())
					{
						AppendExpansion(Out, ' ');
						++Index;
					}
					break;
				}
				if (Index + 1 < Text.size() && Text[Index] == '/' && Text[Index + 1] == '*')
				{
					AppendExpansion(Out, ' ');
					AppendExpansion(Out, ' ');
					Index += 2;
					bInBlockComment = true;
					continue;
				}
				AppendExpansion(Out, Text[Index]);
				++Index;
			}
			if (bInBlockComment)
			{
				OutError = "unterminated block comment in #if expression";
				return false;
			}
			return true;
		}

		/**
		 * Parse/evaluate a #if/#elif expression.
		 * When bNeedValue is false (skipped branch), still require a well-formed
		 * expression but ignore arithmetic traps (div-by-zero, shift range, …).
		 */
		bool EvaluateIfExpression(
			const std::string_view Expr,
			const FMacroMap& Macros,
			bool& OutTruthy,
			std::string& OutError,
			const FMacroExpansionContext& Context,
			const bool bNeedValue = true)
		{
			OutTruthy = false;
			std::string AfterDefined;
			if (!RewriteDefinedOperators(Expr, Macros, AfterDefined, OutError))
			{
				return false;
			}
			std::string Expanded;
			std::unordered_set<std::string> Expanding;
			bool bInBlockComment = false;
			if (!ExpandFragment(
					AfterDefined,
					Macros,
					Expanding,
					bInBlockComment,
					Expanded,
					OutError,
					Context,
					0))
			{
				return false;
			}
			if (bInBlockComment)
			{
				OutError = "unterminated block comment in #if expression";
				return false;
			}
			std::string Clean;
			if (!StripCommentsToSpace(Expanded, Clean, OutError))
			{
				return false;
			}
			FIfParser Parser;
			Parser.Text = Clean;
			if (!bNeedValue)
			{
				Parser.EvalSkipDepth = 1;
			}
			std::int64_t Value = 0;
			if (!Parser.ParseAll(Value))
			{
				OutError = Parser.Error.empty() ? "invalid #if expression" : Parser.Error;
				return false;
			}
			OutTruthy = bNeedValue && Value != 0;
			return true;
		}

		void ScanInactiveLineForComments(const std::string_view Line, bool& bInBlockComment)
		{
			std::size_t Scan = 0;
			while (Scan < Line.size())
			{
				if (bInBlockComment)
				{
					const std::size_t End = Line.find("*/", Scan);
					if (End == std::string_view::npos)
					{
						return;
					}
					Scan = End + 2;
					bInBlockComment = false;
					continue;
				}
				if (Line[Scan] == '"'
					|| (Line[Scan] == 'L' && Scan + 1 < Line.size() && Line[Scan + 1] == '"'))
				{
					if (Line[Scan] == 'L')
					{
						++Scan;
					}
					++Scan;
					while (Scan < Line.size())
					{
						const char QuoteChar = Line[Scan++];
						if (QuoteChar == '\\' && Scan < Line.size())
						{
							++Scan;
							continue;
						}
						if (QuoteChar == '"')
						{
							break;
						}
					}
					continue;
				}
				if (Scan + 1 < Line.size() && Line[Scan] == '/' && Line[Scan + 1] == '/')
				{
					return;
				}
				if (Scan + 1 < Line.size() && Line[Scan] == '/' && Line[Scan + 1] == '*')
				{
					Scan += 2;
					bInBlockComment = true;
					continue;
				}
				++Scan;
			}
		}
	}

	bool ParseDefineSpec(
		const std::string_view Spec,
		std::string& OutName,
		std::string& OutBody,
		std::string& OutError)
	{
		OutName.clear();
		OutBody.clear();
		OutError.clear();
		std::size_t Index = 0;
		SkipHorizontalSpace(Spec, Index);
		if (!ParseIdentifier(Spec, Index, OutName))
		{
			OutError = "invalid define name";
			return false;
		}
		if (IsReservedPreprocessorIdentifier(OutName))
		{
			OutError = "reserved preprocessor identifier cannot be defined: " + OutName;
			return false;
		}
		if (Index < Spec.size() && Spec[Index] == '(')
		{
			OutError = "function-like macros are not supported";
			return false;
		}
		if (Index < Spec.size() && Spec[Index] == '=')
		{
			++Index;
			OutBody.assign(Spec.data() + Index, Spec.size() - Index);
			TrimTrailingHorizontalSpace(OutBody);
			return true;
		}
		SkipHorizontalSpace(Spec, Index);
		if (Index < Spec.size())
		{
			OutError = "use NAME or NAME=value for -D / define specs";
			return false;
		}
		return true;
	}

	bool BuildDefineMap(
		const std::vector<std::string_view>& Specs,
		std::unordered_map<std::string, std::string>& OutDefines,
		std::string& OutError)
	{
		OutDefines.clear();
		OutError.clear();
		for (const std::string_view Spec : Specs)
		{
			if (Spec.empty())
			{
				continue;
			}
			std::string Name;
			std::string Body;
			if (!ParseDefineSpec(Spec, Name, Body, OutError))
			{
				return false;
			}
			OutDefines[std::move(Name)] = std::move(Body);
		}
		return true;
	}

	std::vector<std::string> NormalizeDefineList(const std::vector<std::string_view>& Defines)
	{
		std::unordered_map<std::string, std::string> Map;
		std::string Error;
		if (!BuildDefineMap(Defines, Map, Error))
		{
			std::unordered_set<std::string> Unique;
			for (const std::string_view Define : Defines)
			{
				if (!Define.empty())
				{
					Unique.emplace(Define);
				}
			}
			std::vector<std::string> Sorted(Unique.begin(), Unique.end());
			std::sort(Sorted.begin(), Sorted.end());
			return Sorted;
		}
		std::vector<std::string> Sorted;
		Sorted.reserve(Map.size());
		for (const auto& Entry : Map)
		{
			if (Entry.second.empty())
			{
				Sorted.push_back(Entry.first);
			}
			else
			{
				Sorted.push_back(Entry.first + "=" + Entry.second);
			}
		}
		std::sort(Sorted.begin(), Sorted.end());
		return Sorted;
	}

	bool ValidatePredefinedMacros(
		const std::vector<FPredefinedMacro>& Macros,
		std::string& OutError)
	{
		OutError.clear();
		std::unordered_set<std::string> Names;
		for (const FPredefinedMacro& Macro : Macros)
		{
			std::size_t NameEnd = 0;
			std::string ParsedName;
			if (!ParseIdentifier(Macro.Name, NameEnd, ParsedName)
				|| NameEnd != Macro.Name.size() || ParsedName != Macro.Name)
			{
				OutError = "invalid predefined macro name '" + Macro.Name + "'";
				return false;
			}
			if (!Names.insert(Macro.Name).second)
			{
				OutError = "duplicate predefined macro '" + Macro.Name + "'";
				return false;
			}
			std::unordered_set<std::string> Parameters;
			for (const std::string& Parameter : Macro.Parameters)
			{
				std::size_t ParamEnd = 0;
				std::string ParsedParameter;
				if (!ParseIdentifier(Parameter, ParamEnd, ParsedParameter)
					|| ParamEnd != Parameter.size() || ParsedParameter != Parameter)
				{
					OutError = "invalid parameter '" + Parameter
						+ "' in predefined macro '" + Macro.Name + "'";
					return false;
				}
				if (IsReservedPreprocessorIdentifier(Parameter))
				{
					OutError = Parameter
						+ " cannot be a named parameter in predefined macro '"
						+ Macro.Name + "'";
					return false;
				}
				if (!Parameters.insert(Parameter).second)
				{
					OutError = "duplicate parameter '" + Parameter
						+ "' in predefined macro '" + Macro.Name + "'";
					return false;
				}
			}
			FMacro ValidationMacro;
			ValidationMacro.bFunctionLike = true;
			ValidationMacro.bVariadic = Macro.bVariadic;
			ValidationMacro.Parameters = Macro.Parameters;
			ValidationMacro.Body = Macro.Body;
			std::string IgnoredResolvedBody;
			if (!ResolveVaOpt(
					ValidationMacro,
					ValidationMacro.Body,
					false,
					IgnoredResolvedBody,
					OutError))
			{
				OutError += " in predefined macro '" + Macro.Name + "'";
				return false;
			}
			if (IsReservedPreprocessorIdentifier(Macro.Name))
			{
				OutError = "reserved preprocessor identifier cannot be predefined: "
					+ Macro.Name;
				return false;
			}
			bool bInString = false;
			bool bInCharacter = false;
			bool bEscape = false;
			bool bInBlockComment = false;
			for (std::size_t Index = 0; Index < Macro.Body.size();)
			{
				const char Char = Macro.Body[Index];
				if (bInBlockComment)
				{
					if (Char == '*' && Index + 1 < Macro.Body.size()
						&& Macro.Body[Index + 1] == '/')
					{
						bInBlockComment = false;
						Index += 2;
					}
					else
					{
						++Index;
					}
					continue;
				}
				if (bInString || bInCharacter)
				{
					if (bEscape)
					{
						bEscape = false;
					}
					else if (Char == '\\')
					{
						bEscape = true;
					}
					else if ((bInString && Char == '"') || (bInCharacter && Char == '\''))
					{
						bInString = false;
						bInCharacter = false;
					}
					++Index;
					continue;
				}
				if (Char == '/' && Index + 1 < Macro.Body.size())
				{
					if (Macro.Body[Index + 1] == '/')
					{
						break;
					}
					if (Macro.Body[Index + 1] == '*')
					{
						bInBlockComment = true;
						Index += 2;
						continue;
					}
				}
				if (Char == '"')
				{
					bInString = true;
					++Index;
					continue;
				}
				if (Char == '\'')
				{
					bInCharacter = true;
					++Index;
					continue;
				}
				if (Char == '#')
				{
					if (Index + 1 < Macro.Body.size() && Macro.Body[Index + 1] == '#')
					{
						std::size_t Left = Index;
						while (Left > 0
							&& (Macro.Body[Left - 1] == ' ' || Macro.Body[Left - 1] == '\t'
								|| Macro.Body[Left - 1] == '\r'))
						{
							--Left;
						}
						std::size_t Right = Index + 2;
						SkipHorizontalSpace(Macro.Body, Right);
						if (Left == 0 || Right >= Macro.Body.size())
						{
							OutError = "token paste requires operands in predefined macro '"
								+ Macro.Name + "'";
							return false;
						}
						Index += 2;
						continue;
					}
					std::size_t ParameterIndex = Index + 1;
					SkipHorizontalSpace(Macro.Body, ParameterIndex);
					std::string ParameterName;
					if (!ParseIdentifier(Macro.Body, ParameterIndex, ParameterName)
						|| (Parameters.find(ParameterName) == Parameters.end()
							&& !(Macro.bVariadic
								&& (ParameterName == VaArgsName
									|| ParameterName == VaOptName))))
					{
						OutError = "stringize must be followed by a macro parameter in '"
							+ Macro.Name + "'";
						return false;
					}
					Index = ParameterIndex;
					continue;
				}
				if (!IsIdentStart(Macro.Body[Index]))
				{
					++Index;
					continue;
				}
				std::string Identifier;
				if (!ParseIdentifier(Macro.Body, Index, Identifier))
				{
					++Index;
					continue;
				}
				if (Identifier == VaArgsName && !Macro.bVariadic)
				{
					OutError = "__VA_ARGS__ requires bVariadic in predefined macro '"
						+ Macro.Name + "'";
					return false;
				}
			}
		}
		return true;
	}

	bool ParsePredefinedMacroSpec(
		const std::string_view Spec,
		FPredefinedMacro& OutMacro,
		std::string& OutError)
	{
		OutMacro = {};
		OutError.clear();
		std::size_t Index = 0;
		SkipHorizontalSpace(Spec, Index);
		if (!ParseIdentifier(Spec, Index, OutMacro.Name)
			|| Index >= Spec.size() || Spec[Index] != '(')
		{
			OutError = "predefined macro must use NAME(parameters)=body";
			return false;
		}
		++Index;
		SkipHorizontalSpace(Spec, Index);
		if (Index >= Spec.size() || Spec[Index] != ')')
		{
			for (;;)
			{
				if (Index + 2 < Spec.size()
					&& Spec[Index] == '.' && Spec[Index + 1] == '.'
					&& Spec[Index + 2] == '.')
				{
					OutMacro.bVariadic = true;
					Index += 3;
					SkipHorizontalSpace(Spec, Index);
					break;
				}
				std::string Parameter;
				if (!ParseIdentifier(Spec, Index, Parameter))
				{
					OutError = "predefined macro parameter must be an identifier or ...";
					return false;
				}
				OutMacro.Parameters.push_back(std::move(Parameter));
				SkipHorizontalSpace(Spec, Index);
				if (Index < Spec.size() && Spec[Index] == ',')
				{
					++Index;
					SkipHorizontalSpace(Spec, Index);
					continue;
				}
				break;
			}
		}
		if (Index >= Spec.size() || Spec[Index] != ')')
		{
			OutError = "missing ')' in predefined macro specification";
			return false;
		}
		++Index;
		SkipHorizontalSpace(Spec, Index);
		if (Index >= Spec.size() || Spec[Index] != '=')
		{
			OutError = "predefined macro specification requires '=body'";
			return false;
		}
		++Index;
		OutMacro.Body.assign(Spec.data() + Index, Spec.size() - Index);
		OutMacro.SourceName = "<command-line>";
		return ValidatePredefinedMacros({OutMacro}, OutError);
	}

	std::vector<FPredefinedMacro> NormalizePredefinedMacros(
		const std::vector<FPredefinedMacro>& Macros)
	{
		std::vector<FPredefinedMacro> Result = Macros;
		for (FPredefinedMacro& Macro : Result)
		{
			if (Macro.SourceName.empty())
			{
				Macro.SourceName = "<host>";
			}
		}
		std::sort(
			Result.begin(),
			Result.end(),
			[](const FPredefinedMacro& Left, const FPredefinedMacro& Right)
			{
				return Left.Name < Right.Name;
			});
		return Result;
	}

	struct FPreprocessorSession::FImpl
	{
		struct FObserver
		{
			std::string Token;
			FPreprocessSourcePos Position;
			FModuleImportHeadState Head;
			bool bString = false, bRawString = false;
			bool bEscape = false, bLineComment = false, bBlockComment = false;
			char Last = 0;
			char Quote = '"';
		};

		struct FStreamFrame
		{
			std::string Text, Name;
			std::size_t Index = 0, OriginOffset = 0, OutputStart = 0;
			bool bRoot = false;
			std::unordered_set<std::string> NoReglue;
			FObserver Before;
		};

		bool Observe(char Char, const FPreprocessSourcePos &Position);
		bool ExpandStream();
		bool Install(std::string Path, const std::vector<FPublicMacro> &Definitions,
					 std::shared_ptr<const void> Owner, bool bImplicit = false);
		bool Init(const std::unordered_map<std::string, std::string> &InitialDefines,
				  const std::vector<FPredefinedMacro> &PredefinedMacros)
		{
			const auto Hash = [&](std::string_view Text)
			{
				for (unsigned char C : Text)
				{
					Revision ^= C;
					Revision *= 1099511628211ull;
				}
				Revision ^= 0xff;
				Revision *= 1099511628211ull;
			};
			Hash(Input);
			std::vector<std::string> Names;
			for (const auto& Entry : InitialDefines)
			{
				Names.push_back(Entry.first);
			}

			std::sort(Names.begin(), Names.end());
			for (const auto &Name : Names)
			{
				Hash(Name);
				Hash(InitialDefines.at(Name));
			}
			for (const auto &Macro : NormalizePredefinedMacros(PredefinedMacros))
			{
				Hash(Macro.Name);
				Hash(Macro.Body);
				Hash(Macro.bVariadic ? "1" : "0");
				for (const auto& P : Macro.Parameters)
				{
					Hash(P);
				}
			}
			if (!ValidatePredefinedMacros(PredefinedMacros, OutError))
			{
				return false;
			}

			FileLiteral = MakeFileMacroLiteral(Context.LogicalPath);
			Macros.reserve(InitialDefines.size() + PredefinedMacros.size() + 2);
			FMacro FileMacro;
			FileMacro.Builtin = FMacro::EBuiltin::File;
			Macros.emplace(FileMacroName, std::move(FileMacro));
			FMacro LineMacro;
			LineMacro.Builtin = FMacro::EBuiltin::Line;
			Macros.emplace(LineMacroName, std::move(LineMacro));
			for (const auto &Entry : InitialDefines)
			{
				std::size_t NameEnd = 0;
				std::string ParsedName;
				if (!ParseIdentifier(Entry.first, NameEnd, ParsedName) ||
					NameEnd != Entry.first.size() || ParsedName != Entry.first)
				{
					OutError = "invalid initial define name '" + Entry.first + "'";
					return false;
				}
				if (IsReservedPreprocessorIdentifier(Entry.first))
				{
					OutError = "reserved preprocessor identifier cannot be defined: " + Entry.first;
					return false;
				}
				FMacro Macro;
				Macro.Body = Entry.second;
				Macros.emplace(Entry.first, std::move(Macro));
			}
			ProtectedPredefinedMacros.reserve(PredefinedMacros.size() + 2);
			ProtectedPredefinedMacros.insert(FileMacroName);
			ProtectedPredefinedMacros.insert(LineMacroName);
			if (OutLineMap)
			{
				const auto AddBuiltinMacro = [&](const std::string &Name, const std::string &Body)
				{
					FPreprocessMacroDef Visible;
					Visible.Name = Name;
					Visible.Body = Body;
					Visible.bActive = true;
					Visible.bPredefined = true;
					Visible.SourceName = "<builtin>";
					Visible.Line = 0;
					Visible.Column = 0;
					Visible.DirectiveStartLine = 0;
					Visible.DirectiveEndLine = 0;
					OutLineMap->ActiveMacros.push_back(std::move(Visible));
				};
				AddBuiltinMacro(FileMacroName, FileLiteral);
				AddBuiltinMacro(LineMacroName, "<current-line>");
			}
			for (const FPredefinedMacro &Predefined : PredefinedMacros)
			{
				if (Macros.find(Predefined.Name) != Macros.end())
				{
					OutError = "predefined macro '" + Predefined.Name +
							   "' conflicts with an initial object-like define";
					return false;
				}
				FMacro Macro;
				Macro.bFunctionLike = true;
				Macro.bVariadic = Predefined.bVariadic;
				Macro.Parameters = Predefined.Parameters;
				Macro.Body = Predefined.Body;
				Macros.emplace(Predefined.Name, std::move(Macro));
				ProtectedPredefinedMacros.insert(Predefined.Name);
				if (OutLineMap)
				{
					FPreprocessMacroDef Visible;
					Visible.Name = Predefined.Name;
					Visible.Body = Predefined.Body;
					Visible.bFunctionLike = true;
					Visible.bVariadic = Predefined.bVariadic;
					Visible.bActive = true;
					Visible.bPredefined = true;
					Visible.SourceName =
						Predefined.SourceName.empty() ? "<host>" : Predefined.SourceName;
					Visible.Parameters = Predefined.Parameters;
					Visible.Line = 0;
					Visible.Column = 0;
					Visible.DirectiveStartLine = 0;
					Visible.DirectiveEndLine = 0;
					OutLineMap->ActiveMacros.push_back(std::move(Visible));
				}
			}
			ActivatedEvents = Map.ActiveMacros.size();
			if (Input.size() >= 3 && static_cast<unsigned char>(Input[0]) == 0xef &&
				static_cast<unsigned char>(Input[1]) == 0xbb && static_cast<unsigned char>(Input[2]) == 0xbf)
			{
				Index = 3;
			}

			return true;
		}
		bool Run()
		{
			const std::string_view Source(Input);
			const auto IsActive = [&]() -> bool
			{
				for (const FCondFrame &Frame : Stack)
				{
					if (!Frame.bBranchActive)
					{
						return false;
					}
				}
				return true;
			};

			const auto MarkPhysicalLines = [&](const std::uint32_t StartLine,
											   const std::uint32_t Count, const bool bActiveCode)
			{
				if (!OutLineMap || Count == 0)
				{
					return;
				}
				const std::size_t Need = static_cast<std::size_t>(StartLine) + Count;
				if (OutLineMap->PhysicalLineActiveCode.size() < Need)
				{
					OutLineMap->PhysicalLineActiveCode.resize(Need, false);
				}
				for (std::uint32_t Offset = 0; Offset < Count; ++Offset)
				{
					OutLineMap->PhysicalLineActiveCode[StartLine + Offset] = bActiveCode;
				}
			};

			const auto LogicalOffsetToPhysical =
				[](const std::vector<FLogicalPiece> &Pieces, const std::uint32_t FallbackLine,
				   const std::size_t LogicalOffset, FPreprocessSourcePos &OutPos)
			{
				for (const FLogicalPiece &Piece : Pieces)
				{
					if (LogicalOffset >= Piece.LogicalBegin && LogicalOffset < Piece.LogicalEnd)
					{
						OutPos.Line = Piece.PhysLine;
						OutPos.Column =
							Piece.PhysColStart +
							static_cast<std::uint32_t>(LogicalOffset - Piece.LogicalBegin);
						return;
					}
				}
				OutPos.Line = FallbackLine;
				OutPos.Column = static_cast<std::uint32_t>(LogicalOffset) + 1;
			};

			const auto EmitMappedChar = [&](const char Char, const FPreprocessSourcePos &Pos)
			{
				BudgetPosition = Pos;
				AppendExpansion(OutSource, Char);
				if (!Observe(Char, Pos))
				{
					return;
				}

				if (OutLineMap)
				{
					OutLineMap->OutByteToPhysical.push_back(Pos);
				}
			};

			const auto EmitMappedNewline = [&](const std::uint32_t PhysLine)
			{
				FPreprocessSourcePos Pos;
				Pos.Line = PhysLine;
				Pos.Column = 1;
				EmitMappedChar('\n', Pos);
			};

			while (Index <= Source.size())
			{
				CheckAnalysisCancellation();
				if (bLinePending)
				{
					if (!ExpandStream())
					{
						return false;
					}

					bLinePending = false;
					if (bHasNewline)
					{
						EmitMappedNewline(LogicalStartLine + PhysicalCount - 1);
					}

					LineNumber += PhysicalCount;
					if (!bHasNewline)
					{
						break;
					}
				}
				LogicalLine.clear();
				PhysicalCount = 0;
				bool bLastPhysicalHasNewline = false;
				LogicalStartLine = LineNumber;
				Pieces.clear();

				for (;;)
				{
					if (Index > Source.size())
					{
						break;
					}
					if (Index == Source.size())
					{
						if (PhysicalCount == 0)
						{
							break;
						}
						OutError = "backslash-newline at end of file";
						if (OutErrorLine)
						{
							*OutErrorLine = LogicalStartLine;
						}
						return false;
					}

					const std::size_t LineBegin = Index;
					while (Index < Source.size() && Source[Index] != '\n')
					{
						++Index;
					}
					const bool bPhysicalHasNewline = Index < Source.size() && Source[Index] == '\n';
					const std::string_view Physical(Source.data() + LineBegin, Index - LineBegin);
					if (bPhysicalHasNewline)
					{
						++Index;
					}
					++PhysicalCount;
					bLastPhysicalHasNewline = bPhysicalHasNewline;
					const std::uint32_t PhysLine = LogicalStartLine + PhysicalCount - 1;

					if (EndsWithLineContinuation(Physical))
					{
						if (!bPhysicalHasNewline)
						{
							OutError = "backslash-newline at end of file";
							if (OutErrorLine)
							{
								*OutErrorLine = LogicalStartLine;
							}
							return false;
						}
						const std::string_view Piece = WithoutTrailingContinuation(Physical);
						FLogicalPiece MapPiece;
						MapPiece.PhysLine = PhysLine;
						MapPiece.PhysColStart = 1;
						MapPiece.LogicalBegin = LogicalLine.size();
						AppendExpansion(LogicalLine, Piece.data(), Piece.size());
						MapPiece.LogicalEnd = LogicalLine.size();
						Pieces.push_back(MapPiece);
						continue;
					}

					FLogicalPiece MapPiece;
					MapPiece.PhysLine = PhysLine;
					MapPiece.PhysColStart = 1;
					MapPiece.LogicalBegin = LogicalLine.size();
					AppendExpansion(LogicalLine, Physical.data(), Physical.size());
					MapPiece.LogicalEnd = LogicalLine.size();
					Pieces.push_back(MapPiece);
					break;
				}

				if (PhysicalCount == 0)
				{
					break;
				}

				const std::string_view Line(LogicalLine);
				bHasNewline = bLastPhysicalHasNewline;
				const FMacroExpansionContext ExpansionContext{FileLiteral, LogicalStartLine,
															  &Pieces,&Context.MapSourcePosition};
				const auto FailLine = [&](const std::string &Message) -> bool
				{
					OutError = Message;
					if (OutErrorLine)
					{
						*OutErrorLine = LogicalStartLine;
					}
					return false;
				};

				std::size_t Cursor = 0;
				SkipHorizontalSpace(Line, Cursor);
				const bool bDirective =
					!bInBlockComment && Cursor < Line.size() && Line[Cursor] == '#';
				MarkPhysicalLines(LogicalStartLine, PhysicalCount, !bDirective && IsActive());
				if (bDirective)
				{
					// bDirective already requires !bInBlockComment; any comment opened
					// below comes from this directive's payload (unclosed /*).
					++Cursor;
					SkipHorizontalSpace(Line, Cursor);
					std::string Directive;
					const std::size_t DirectiveLogicalOffset = Cursor;
					if (!ParseIdentifier(Line, Cursor, Directive))
					{
						return FailLine("invalid preprocessor directive");
					}
					SkipHorizontalSpace(Line, Cursor);

					if (Directive == "include")
					{
						// `#include "module.vs"` is deliberately a spelling alias for
						// `export import "module.vs";`. It does not perform textual inclusion,
						// expand macros in the path, or support C/C++ angle-bracket form.
						if (Cursor >= Line.size() || Line[Cursor] != '"')
						{
							return FailLine("#include expects a quoted module path");
						}
						const std::size_t LiteralBegin = Cursor;
						++Cursor;
						bool bEscaped = false;
						while (Cursor < Line.size())
						{
							const char Char = Line[Cursor++];
							if (bEscaped)
							{
								bEscaped = false;
								continue;
							}
							if (Char == '\\')
							{
								bEscaped = true;
								continue;
							}
							if (Char == '"')
							{
								break;
							}
						}
						if (Cursor > Line.size() || Line[Cursor - 1] != '"' || bEscaped)
						{
							return FailLine("unterminated quoted module path in #include");
						}
						const std::size_t LiteralEnd = Cursor;
						if (LiteralEnd == LiteralBegin + 2)
						{
							return FailLine("#include module path must not be empty");
						}
						std::string Trailing;
						CollectDirectivePayload(Line, Cursor, Trailing, bInBlockComment);
						if (!Trailing.empty())
						{
							return FailLine("unexpected tokens after #include module path");
						}

						if (IsActive())
						{
							const std::string_view ImportKeywords = "export import";
							for (std::size_t Offset = 0; Offset < ImportKeywords.size(); ++Offset)
							{
								FPreprocessSourcePos Pos;
								LogicalOffsetToPhysical(Pieces, LogicalStartLine,
														DirectiveLogicalOffset +
															std::min(Offset, Directive.size() - 1),
														Pos);
								EmitMappedChar(ImportKeywords[Offset], Pos);
							}
							FPreprocessSourcePos SpacePos;
							LogicalOffsetToPhysical(Pieces, LogicalStartLine, LiteralBegin,
													SpacePos);
							EmitMappedChar(' ', SpacePos);
							for (std::size_t Offset = LiteralBegin; Offset < LiteralEnd; ++Offset)
							{
								FPreprocessSourcePos Pos;
								LogicalOffsetToPhysical(Pieces, LogicalStartLine, Offset, Pos);
								EmitMappedChar(Line[Offset], Pos);
							}
							FPreprocessSourcePos SemicolonPos;
							LogicalOffsetToPhysical(Pieces, LogicalStartLine, LiteralEnd - 1,
													SemicolonPos);
							EmitMappedChar(';', SemicolonPos);
						}
					}
					else if (Directive == "define" || Directive == "define_export" || Directive == "undef")
					{
						std::string Name;
						const std::size_t NameLogicalOffset = Cursor;
						if (!ParseIdentifier(Line, Cursor, Name))
						{
							return FailLine("#" + Directive + " requires an identifier");
						}
						if (IsReservedPreprocessorIdentifier(Name))
						{
							return FailLine(
								"reserved preprocessor identifier cannot be used with #" +
								Directive + ": " + Name);
						}
						if (Directive != "undef")
						{
							++DefinitionOrdinal;
						}

						if (Directive == "define_export")
						{
							Result.bContainsExportDirective = true;
						}

						FMacro ParsedMacro;
						if (Directive != "undef")
						{
							if (Cursor < Line.size() && Line[Cursor] == '(')
							{
								ParsedMacro.bFunctionLike = true;
								++Cursor;
								SkipHorizontalSpace(Line, Cursor);
								if (Cursor >= Line.size() || Line[Cursor] != ')')
								{
									for (;;)
									{
										if (Cursor + 2 < Line.size() && Line[Cursor] == '.' &&
											Line[Cursor + 1] == '.' && Line[Cursor + 2] == '.')
										{
											ParsedMacro.bVariadic = true;
											Cursor += 3;
											SkipHorizontalSpace(Line, Cursor);
											break;
										}
										std::string ParamName;
										if (!ParseIdentifier(Line, Cursor, ParamName))
										{
											return FailLine("function-like macro parameter must be "
															"an identifier");
										}
										if (IsReservedPreprocessorIdentifier(ParamName))
										{
											return FailLine(
												ParamName +
												" cannot be used as a macro parameter name");
										}
										for (const std::string &Existing : ParsedMacro.Parameters)
										{
											if (Existing == ParamName)
											{
												return FailLine("duplicate macro parameter '" +
																ParamName + "'");
											}
										}
										ParsedMacro.Parameters.push_back(std::move(ParamName));
										SkipHorizontalSpace(Line, Cursor);
										if (Cursor < Line.size() && Line[Cursor] == ',')
										{
											++Cursor;
											SkipHorizontalSpace(Line, Cursor);
											continue;
										}
										break;
									}
									if (Cursor >= Line.size() || Line[Cursor] != ')')
									{
										return FailLine("missing ')' in macro parameter list");
									}
								}
								++Cursor;
							}
							SkipHorizontalSpace(Line, Cursor);
							CollectDirectivePayload(Line, Cursor, ParsedMacro.Body,
													bInBlockComment);
							std::string IgnoredResolvedBody;
							std::string VaOptError;
							if (!ResolveVaOpt(ParsedMacro, ParsedMacro.Body, false,
											  IgnoredResolvedBody, VaOptError))
							{
								return FailLine(VaOptError);
							}
						}
						else
						{
							std::string Trailing;
							CollectDirectivePayload(Line, Cursor, Trailing, bInBlockComment);
							if (!Trailing.empty())
							{
								return FailLine("unexpected tokens after #undef");
							}
						}
						if (IsActive())
						{
							if (Directive != "undef")
							{
								if (const auto Existing = Macros.find(Name);
									Existing != Macros.end() && Existing->second.bImported)
								{
									return FailLine(
										"imported macro must be #undef'd before redefinition: " + Name);
								}

								if (ProtectedPredefinedMacros.find(Name) != ProtectedPredefinedMacros.end())
								{
									return FailLine(
										"predefined macro '" + Name +
										"' must be #undef'd before it can be redefined");
								}
								if (OutLineMap)
								{
									FPreprocessMacroDef Macro;
									Macro.Name = Name;
									Macro.Body = ParsedMacro.Body;
									Macro.bFunctionLike = ParsedMacro.bFunctionLike;
									Macro.bVariadic = ParsedMacro.bVariadic;
									Macro.bActive = true;
									Macro.Parameters = ParsedMacro.Parameters;
									Macro.DirectiveStartLine = LogicalStartLine;
									Macro.DirectiveEndLine = LogicalStartLine + PhysicalCount - 1;
									Macro.Line = LogicalStartLine;
									Macro.Column =
										static_cast<std::uint32_t>(NameLogicalOffset) + 1;
									for (const FLogicalPiece &Piece : Pieces)
									{
										if (NameLogicalOffset >= Piece.LogicalBegin &&
											NameLogicalOffset < Piece.LogicalEnd)
										{
											Macro.Line = Piece.PhysLine;
											Macro.Column =
												Piece.PhysColStart +
												static_cast<std::uint32_t>(NameLogicalOffset -
																		   Piece.LogicalBegin);
											break;
										}
									}
									OutLineMap->ActiveMacros.push_back(std::move(Macro));
								}

								if (Directive == "define_export")
								{
									auto Definition = std::make_shared<FPublicMacro>();
									Definition->Name = Name;
									Definition->Body = std::move(ParsedMacro.Body);
									Definition->bFunctionLike = ParsedMacro.bFunctionLike;
									Definition->bVariadic = ParsedMacro.bVariadic;
									Definition->Parameters = std::move(ParsedMacro.Parameters);
									Definition->OriginModule = PathUtil::IdentityKey(
										Context.LogicalPath.empty() ? "<memory>"
																	: Context.LogicalPath);
									Definition->Revision = Revision;
									Definition->DefinitionOrdinal = DefinitionOrdinal;
									Definition->SourceName = Context.LogicalPath;
									FPreprocessSourcePos DefinitionPos;
									LogicalOffsetToPhysical(Pieces, LogicalStartLine,
															NameLogicalOffset, DefinitionPos);
									Definition->Line = DefinitionPos.Line;
									Definition->Column = DefinitionPos.Column;
									Definition->DirectiveStartLine = LogicalStartLine;
									Definition->DirectiveEndLine =
										LogicalStartLine + PhysicalCount - 1;
									ParsedMacro.Definition = std::move(Definition);
									ParsedMacro.bExported = Directive == "define_export";
									if (ParsedMacro.bExported &&
										!ValidatePublicMacros({*ParsedMacro.Definition}, OutError))
									{
										return false;
									}
								}
								Macros[Name] = std::move(ParsedMacro);
							}
							else
							{
								ProtectedPredefinedMacros.erase(Name);
								if (OutLineMap)
								{
									FPreprocessMacroDef Tombstone;
									Tombstone.Name = Name;
									Tombstone.bActive = false;
									Tombstone.DirectiveStartLine = LogicalStartLine;
									Tombstone.DirectiveEndLine =
										LogicalStartLine + PhysicalCount - 1;
									Tombstone.Line = LogicalStartLine;
									Tombstone.Column =
										static_cast<std::uint32_t>(NameLogicalOffset) + 1;
									for (const FLogicalPiece &Piece : Pieces)
									{
										if (NameLogicalOffset >= Piece.LogicalBegin &&
											NameLogicalOffset < Piece.LogicalEnd)
										{
											Tombstone.Line = Piece.PhysLine;
											Tombstone.Column =
												Piece.PhysColStart +
												static_cast<std::uint32_t>(NameLogicalOffset -
																		   Piece.LogicalBegin);
											break;
										}
									}
									OutLineMap->ActiveMacros.push_back(std::move(Tombstone));
								}
								if (const auto Existing = Macros.find(Name);
									Existing != Macros.end() && Existing->second.bImported)
								{
									--ImportedMacroCount;
									ImportedMacroBytes -= PublicMacroEncodedSize(*Existing->second.Definition);
								}
								Macros.erase(Name);
							}
						}
					}
					else if (Directive == "ifdef" || Directive == "ifndef")
					{
						std::string Name;
						if (!ParseIdentifier(Line, Cursor, Name))
						{
							return FailLine("#" + Directive + " requires an identifier");
						}
						std::string Trailing;
						CollectDirectivePayload(Line, Cursor, Trailing, bInBlockComment);
						if (!Trailing.empty())
						{
							return FailLine("unexpected tokens after #" + Directive);
						}
						const bool bParentActive = IsActive();
						const bool bDefined = Macros.find(Name) != Macros.end();
						const bool bWant = Directive == "ifdef" ? bDefined : !bDefined;
						FCondFrame Frame;
						Frame.bParentActive = bParentActive;
						Frame.bBranchActive = bParentActive && bWant;
						Frame.bBranchTaken = Frame.bBranchActive;
						Frame.bElseSeen = false;
						Frame.OpenLine = LogicalStartLine;
						Stack.push_back(Frame);
					}
					else if (Directive == "if" || Directive == "elif")
					{
						std::string Expr;
						CollectDirectivePayload(Line, Cursor, Expr, bInBlockComment);
						if (Expr.empty())
						{
							return FailLine("empty #" + Directive + " expression");
						}
						if (Directive == "elif")
						{
							if (Stack.empty())
							{
								return FailLine("#elif without matching #if/#ifdef/#ifndef");
							}
							FCondFrame &Frame = Stack.back();
							if (Frame.bElseSeen)
							{
								return FailLine("#elif after #else");
							}
							bool bTruthy = false;
							const bool bNeedValue = Frame.bParentActive && !Frame.bBranchTaken;
							if (!EvaluateIfExpression(Expr, Macros, bTruthy, OutError,
													  ExpansionContext, bNeedValue))
							{
								if (OutErrorLine)
								{
									*OutErrorLine = LogicalStartLine;
								}
								return false;
							}
							if (bNeedValue)
							{
								Frame.bBranchActive = bTruthy;
								if (bTruthy)
								{
									Frame.bBranchTaken = true;
								}
							}
							else
							{
								Frame.bBranchActive = false;
							}
						}
						else
						{
							const bool bParentActive = IsActive();
							bool bTruthy = false;
							if (!EvaluateIfExpression(Expr, Macros, bTruthy, OutError,
													  ExpansionContext, bParentActive))
							{
								if (OutErrorLine)
								{
									*OutErrorLine = LogicalStartLine;
								}
								return false;
							}
							FCondFrame Frame;
							Frame.bParentActive = bParentActive;
							Frame.bBranchActive = bParentActive && bTruthy;
							Frame.bBranchTaken = Frame.bBranchActive;
							Frame.bElseSeen = false;
							Frame.OpenLine = LogicalStartLine;
							Stack.push_back(Frame);
						}
					}
					else if (Directive == "else")
					{
						std::string Trailing;
						CollectDirectivePayload(Line, Cursor, Trailing, bInBlockComment);
						if (!Trailing.empty())
						{
							return FailLine("unexpected tokens after #else");
						}
						if (Stack.empty())
						{
							return FailLine("#else without matching #if/#ifdef/#ifndef");
						}
						FCondFrame &Frame = Stack.back();
						if (Frame.bElseSeen)
						{
							return FailLine("duplicate #else in conditional block");
						}
						Frame.bElseSeen = true;
						Frame.bBranchActive = Frame.bParentActive && !Frame.bBranchTaken;
						if (Frame.bBranchActive)
						{
							Frame.bBranchTaken = true;
						}
					}
					else if (Directive == "endif")
					{
						std::string Trailing;
						CollectDirectivePayload(Line, Cursor, Trailing, bInBlockComment);
						if (!Trailing.empty())
						{
							return FailLine("unexpected tokens after #endif");
						}
						if (Stack.empty())
						{
							return FailLine("#endif without matching #if/#ifdef/#ifndef");
						}
						Stack.pop_back();
					}
					else if (Directive == "error")
					{
						std::string Message;
						CollectDirectivePayload(Line, Cursor, Message, bInBlockComment);
						if (IsActive())
						{
							return FailLine(Message.empty() ? "#error" : "#error: " + Message);
						}
					}
					else
					{
						return FailLine("unsupported preprocessor directive: #" + Directive);
					}

					if (!OutError.empty())
					{
						return false;
					}

					// Directive payload excludes an unclosed `/*`. Only emit it when the
					// current branch is active: inactive lines are blanked, so emitting
					// `/*` without a matching `*/` would comment-out the rest of the file.
					// Inactive branches still set bInBlockComment so `#` inside the comment
					// is not treated as a directive (ScanInactiveLineForComments closes it).
					if (bInBlockComment && IsActive())
					{
						FPreprocessSourcePos Pos;
						Pos.Line = LogicalStartLine;
						Pos.Column = 1;
						EmitMappedChar('/', Pos);
						Pos.Column = 2;
						EmitMappedChar('*', Pos);
					}
					if (bHasNewline || Index >= Source.size())
					{
						EmitMappedNewline(LogicalStartLine);
					}
				}
				else if (IsActive())
				{
					FStreamFrame Frame;
					Frame.Text = LogicalLine;
					Frame.bRoot = true;
					Stream.push_back(std::move(Frame));
					bLinePending = true;
					continue;
				}
				else
				{
					ScanInactiveLineForComments(Line, bInBlockComment);
					if (bHasNewline || Index >= Source.size())
					{
						EmitMappedNewline(LogicalStartLine);
					}
				}

				if (OutLineMap)
				{
					for (; ActivatedEvents < OutLineMap->ActiveMacros.size(); ++ActivatedEvents)
					{
						auto &Event = OutLineMap->ActiveMacros[ActivatedEvents];
						Event.ActivationOffset = OutSource.size();
						Event.ActivationLine = LogicalStartLine + PhysicalCount;
						Event.ActivationColumn = 1;
						Event.EventSequence = ActivatedEvents;
						Event.bPublic = Macros.count(Event.Name) && Macros.at(Event.Name).bExported;
					}
				}
				if (bWaiting)
				{
					LineNumber += PhysicalCount;
					return false;
				}
				// No padding: `\`+newline is deleted (true join), like C phase 2.

				if (!bHasNewline)
				{
					break;
				}
				LineNumber += PhysicalCount;
			}

			if (!Stack.empty())
			{
				OutError = "unterminated #if/#ifdef/#ifndef (missing #endif)";
				if (OutErrorLine)
				{
					*OutErrorLine = Stack.back().OpenLine;
				}
				return false;
			}
			if (OutLineMap)
			{
				OutLineMap->PreprocessedSource = OutSource;
			}
			for (const auto& Entry : Macros)
			{
				if (Entry.second.bExported)
				{
					Result.PublicMacros.push_back(*Entry.second.Definition);
				}
			}

			std::sort(Result.PublicMacros.begin(), Result.PublicMacros.end(),
				[](const auto& A, const auto& B)
				{
					return A.Name < B.Name;
				});
			if (!ValidatePublicMacros(Result.PublicMacros, OutError))
			{
				Result.PublicMacros.clear();
				return false;
			}
			bComplete = true;
			Result.bComplete = true;
			Result.bModuleImportsResolved =
				Context.Mode == EPreprocessMode::Module || Result.Imports.empty();
			return true;
		}

		std::string Input, OutSource, OutError, FileLiteral;
		FPreprocessContext Context;
		FPreprocessResult Result;
		FPreprocessLineMap Map;
		FPreprocessLineMap *OutLineMap = nullptr;
		std::uint32_t ErrorLineValue = 0;
		std::uint32_t ErrorColumnValue = 1;
		std::uint32_t *OutErrorLine = &ErrorLineValue;
		FMacroMap Macros;
		std::unordered_set<std::string> ProtectedPredefinedMacros, Expanding;
		std::vector<FCondFrame> Stack;
		std::uint64_t Revision = 14695981039346656037ull, DefinitionOrdinal = 0;
		bool bInBlockComment = false, bComplete = false, bWaiting = false, bLinePending = false;
		bool bStarted = false;
		std::uint32_t LineNumber = 1, LogicalStartLine = 1, PhysicalCount = 0;
		std::size_t Index = 0, ActivatedEvents = 0;
		std::size_t ImportedMacroCount = 0, ImportedMacroBytes = 4;
		std::string LogicalLine;
		std::vector<FLogicalPiece> Pieces;
		bool bHasNewline = false;
		FPreprocessImport Import;
		FObserver Observer;

		std::vector<FStreamFrame> Stream;
	};

	std::size_t PublicMacroEncodedSize(const FPublicMacro &M)
	{
		std::size_t Size =
			4 + M.Name.size() + 2 + 4 + 4 + M.Body.size() + 4 + M.OriginModule.size() + 16 + 1;
		for (const auto& P : M.Parameters)
		{
			Size += 4 + P.size();
		}

		if (!M.SourceName.empty())
		{
			Size += 4 + M.SourceName.size() + 16;
		}

		return Size;
	}

	bool SamePublicMacroDefinition(const FPublicMacro &A, const FPublicMacro &B)
	{
		return A.Name == B.Name && A.bFunctionLike == B.bFunctionLike &&
			   A.bVariadic == B.bVariadic && A.Parameters == B.Parameters && A.Body == B.Body &&
			   A.OriginModule == B.OriginModule && A.Revision == B.Revision &&
			   A.DefinitionOrdinal == B.DefinitionOrdinal;
	}

	bool ValidatePublicMacros(const std::vector<FPublicMacro> &Definitions, std::string &OutError)
	{
		const auto Fail = [&](const char *Message)
		{
			OutError = Message;
			return false;
		};
		if (Definitions.size() > MaxPublicMacroCount)
		{
			return Fail("public macro count limit exceeded");
		}

		std::size_t Bytes = 4;
		std::string_view Previous;
		for (const auto &M : Definitions)
		{
			CheckAnalysisCancellation();
			std::size_t End = 0;
			std::string Name;
			if (!ParseIdentifier(M.Name, End, Name) || End != M.Name.size() ||
				IsReservedPreprocessorIdentifier(M.Name) || (!Previous.empty() && Previous >= M.Name))
			{
				return Fail("invalid, duplicate or unsorted public macro name");
			}

			Previous = M.Name;
			if (M.Parameters.size() > MaxPublicMacroParameters || M.Body.size() > MaxPublicMacroBodyBytes)
			{
				return Fail("public macro definition limit exceeded");
			}

			if (!M.bFunctionLike && (M.bVariadic || !M.Parameters.empty()))
			{
				return Fail("invalid object-like public macro");
			}

			if (M.Body.find_first_of("\r\n\0", 0, 3) != std::string::npos || M.OriginModule.empty() ||
				M.OriginModule.find('\0') != std::string::npos || M.DefinitionOrdinal == 0 ||
				M.SourceName.find('\0') != std::string::npos)
			{
				return Fail("invalid public macro body or origin");
			}

			if (!M.SourceName.empty() &&
				(!M.Line || !M.Column || !M.DirectiveStartLine || M.DirectiveStartLine > M.Line ||
					M.Line > M.DirectiveEndLine))
			{
				return Fail("invalid public macro source location");
			}

			FPredefinedMacro Validation{M.Name, M.Parameters, M.bVariadic, M.Body, "<macro>"};
			if (M.bFunctionLike)
			{
				if (!ValidatePredefinedMacros({Validation}, OutError)) return false;
			}
			else
			{
				// Object replacements can contain a literal '#', e.g. a two-stage
				// stringization argument. Do not validate them as host function macros.
				FMacro ObjectMacro;
				ObjectMacro.Body = M.Body;
				std::string Ignored;
				if (!ResolveVaOpt(ObjectMacro, M.Body, false, Ignored, OutError)) return false;
			}
			const auto Size = PublicMacroEncodedSize(M);
			if (Size > MaxPublicMacroTableBytes - Bytes)
			{
				return Fail("public macro table byte limit exceeded");
			}

			Bytes += Size;
		}
		return true;
	}

	bool FPreprocessorSession::FImpl::Observe(char C, const FPreprocessSourcePos &Pos)
	{
		auto &O = Observer;
		if (O.bLineComment)
		{
			if (C == '\n')
			{
				O.bLineComment = false;
			}

			return true;
		}
		if (O.bBlockComment)
		{
			if (O.Last == '*' && C == '/')
			{
				O.bBlockComment = false;
				O.Last = 0;
			}
			else
			{
				O.Last = C;
			}

			return true;
		}
		if (O.bString)
		{
			O.Token += C;
			if (O.bEscape)
			{
				O.bEscape = false;
				return true;
			}
			if (C == '\\' && !O.bRawString)
			{
				O.bEscape = true;
				return true;
			}
			if (C != O.Quote)
			{
				return true;
			}

			O.bString = false;
			if (O.Head.ExpectsPath() && O.Quote == '"')
			{
				Import = {};
				Import.bReExport = O.Head.IsReExport();
				Import.Position = O.Position;
				Import.ActivationOffset = OutSource.size();
				Import.ActivationPosition = {Pos.Line, Pos.Column + 1};
				if (!ParserDetail::DecodeModuleImportPath(O.Token, Import.Path, OutError))
				{
					if (OutError.empty())
					{
						OutError = "import module path must be nonempty and contain no NUL";
					}

					ErrorLineValue = O.Position.Line;
					ErrorColumnValue = O.Position.Column;
					return false;
				}
				Result.Imports.push_back(Import);
				Result.bModuleImportsResolved = false;
				if (Context.Mode == EPreprocessMode::Module)
				{
					bWaiting = true;
				}
			}
			O.Head.Consume(O.Token, O.Quote == '"');
			O.Token.clear();
			return true;
		}
		if (O.Last == '/' && (C == '/' || C == '*'))
		{
			O.bLineComment = C == '/';
			O.bBlockComment = C == '*';
			O.Last = 0;
			return true;
		}
		if (O.Last == '/')
		{
			// The slash was an operator rather than a comment opener.
			O.Head.Consume("/");
			O.Last = 0;
		}
		if (C == '"' && O.Token == "r")
		{
			O.bString = O.bRawString = true;
			O.Quote = C;
			O.Token += C;
			return true;
		}
		if (!O.Token.empty() && IsIdentContinue(C))
		{
			O.Token += C;
			return true;
		}
		if (!O.Token.empty())
		{
			O.Head.Consume(O.Token);
			O.Token.clear();
		}
		if (IsIdentStart(C))
		{
			O.Token += C;
			if (!O.Head.ExpectsPath() && !O.Head.IsReExport())
			{
				O.Position = Pos;
			}

			return true;
		}
		if (C == '"' || C == '\'')
		{
			O.bString = true;
			O.bRawString = false;
			O.Quote = C;
			O.Token = C;
			return true;
		}
		if (C == '/' || (O.Last == '/' && C == '*'))
		{
			O.Last = C;
			return true;
		}
		O.Last = 0;
		if (C == ' ' || C == '\t' || C == '\r' || C == '\n')
		{
			return true;
		}

		O.Head.Consume(std::string_view(&C, 1));
		return true;
	}

	bool FPreprocessorSession::FImpl::Install(std::string Path,
											  const std::vector<FPublicMacro> &Definitions,
											  std::shared_ptr<const void> Owner, bool bImplicit)
	{
		if (!ValidatePublicMacros(Definitions, OutError))
		{
			return false;
		}
		FPreprocessBudgetScope BudgetScope(*Context.Budget);
		BudgetPosition = Import.Position;
		try
		{
			for (const auto& Definition : Definitions)
			{
				ChargePreprocessWork(PublicMacroEncodedSize(Definition));
			}
		}
		catch (const FPreprocessLimit& Limit)
		{
			OutError = Limit.Message;
			ErrorLineValue = Limit.Position.Line;
			ErrorColumnValue = Limit.Position.Column;
			OutSource.clear();
			Map = {};
			Result.PublicMacros.clear();
			Result.Imports.clear();
			bWaiting = false;
			return false;
		}

		// Keep the active budget incrementally: visiting every prior import here
		// makes independent module imports quadratic in the number of definitions.
		std::size_t Count = ImportedMacroCount, Bytes = ImportedMacroBytes;
		for (const auto &D : Definitions)
		{
			const auto Found = Macros.find(D.Name);
			if (Found != Macros.end())
			{
				if (!Found->second.bImported ||
					!SamePublicMacroDefinition(*Found->second.Definition, D))
				{
					const auto CurrentOrigin = Found->second.Definition
						? Found->second.Definition->OriginModule
						: Context.LogicalPath;
					OutError = "conflicting imported macro: " + D.Name + " (current: " +
						CurrentOrigin + "; imported: " + D.OriginModule + ")";
					ErrorLineValue = Import.Position.Line;
					return false;
				}
			}
			else
			{
				++Count;
				Bytes += PublicMacroEncodedSize(D);
			}
		}
		if (Count > MaxPublicMacroCount || Bytes > MaxPublicMacroTableBytes)
		{
			OutError = "imported macro budget exceeded";
			return false;
		}
		// All semantic checks precede mutation. Allocation failure invalidates this session.
		for (const auto &D : Definitions)
		{
			auto Found = Macros.find(D.Name);
			if (Found == Macros.end())
			{
				FMacro M;
				M.bFunctionLike = D.bFunctionLike;
				M.bVariadic = D.bVariadic;
				M.Definition = Owner ? std::shared_ptr<const FPublicMacro>(Owner, &D)
									 : std::make_shared<const FPublicMacro>(D);
				M.bImported = true;
				Found = Macros.emplace(D.Name, std::move(M)).first;
			}
			Found->second.bExported |= Import.bReExport;
			if (OutLineMap)
			{
				FPreprocessMacroDef E;
				E.Name = D.Name;
				E.Body = D.Body;
				E.Parameters = D.Parameters;
				E.bFunctionLike = D.bFunctionLike;
				E.bVariadic = D.bVariadic;
				E.bImported = true;
				E.bPublic = Found->second.bExported;
				E.SourceName = D.SourceName.empty() ? D.OriginModule : D.SourceName;
				E.Line = D.Line;
				E.Column = D.Column;
				E.DirectiveStartLine = D.DirectiveStartLine;
				E.DirectiveEndLine = D.DirectiveEndLine;
				E.ActivationOffset = Import.ActivationOffset;
				E.ActivationLine = Import.ActivationPosition.Line;
				E.ActivationColumn = Import.ActivationPosition.Column;
				E.EventSequence = Map.ActiveMacros.size();
				Map.ActiveMacros.push_back(std::move(E));
			}
		}
		ActivatedEvents = Map.ActiveMacros.size();
		ImportedMacroCount = Count;
		ImportedMacroBytes = Bytes;
		if (!bImplicit)
		{
			Result.Imports.back().ResolvedPath = std::move(Path);
			bWaiting = false;
		}
		return true;
	}

	bool FPreprocessorSession::FImpl::ExpandStream()
	{
		const auto PositionAt = [&](std::size_t Offset)
		{
			FPreprocessSourcePos P{LogicalStartLine, static_cast<std::uint32_t>(Offset) + 1};
			for (const auto& Piece : Pieces)
			{
				if (Offset >= Piece.LogicalBegin && Offset < Piece.LogicalEnd)
				{
					P.Line = Piece.PhysLine;
					P.Column = Piece.PhysColStart +
							   static_cast<std::uint32_t>(Offset - Piece.LogicalBegin);
					break;
				}
			}

			return Context.MapSourcePosition ? Context.MapSourcePosition(P) : P;
		};
		const auto Emit = [&](char C, std::size_t Offset)
		{
			const auto P = PositionAt(Offset);
			BudgetPosition = P;
			AppendExpansion(OutSource, C);
			if (OutLineMap)
			{
				Map.OutByteToPhysical.push_back(P);
			}

			return Observe(C, P);
		};
		const auto PrepareFunction = [&](const std::string &Name, const FMacro &M,
										 std::string_view Text, std::size_t &Cursor,
										 const FMacroExpansionContext &C, std::string &Body)
		{
			std::vector<std::string> Collected, Raw, Expanded;
			if (!CollectMacroArguments(Text, Cursor, Collected, OutError) ||
				!BindMacroArguments(Name, M, std::move(Collected), Raw, OutError))
			{
				return false;
			}

			for (const auto& A : Raw)
			{
				std::string E;
				bool Comment = false;
				if (!ExpandFragment(A, Macros, Expanding, Comment, E, OutError, C,
						static_cast<int>(Stream.size()), nullptr))
				{
					return false;
				}

				if (Comment)
				{
					OutError = "unterminated block comment in macro argument";
					return false;
				}
				Expanded.push_back(std::move(E));
			}
			std::string Substituted;
			return SubstituteFunctionMacroBody(M, Raw, Expanded, Substituted, OutError) &&
				   ApplyTokenPastes(Substituted, Body, OutError);
		};
		while (!Stream.empty())
		{
			CheckAnalysisCancellation();
			auto &F = Stream.back();
			if (F.Index == F.Text.size())
			{
				if (!F.Name.empty())
				{
					std::string_view Span(OutSource.data() + F.OutputStart,
										  OutSource.size() - F.OutputStart);
					std::size_t I = 0;
					SkipHorizontalSpace(Span, I);
					std::string Alias;
					bool Single = ParseIdentifier(Span, I, Alias);
					SkipHorizontalSpace(Span, I);
					Single &= I == Span.size();
					const auto M = Macros.find(Alias);
					if (Single && M != Macros.end() && M->second.bFunctionLike &&
						!Expanding.count(Alias) && !F.NoReglue.count(Alias) && Stream.size() > 1)
					{
						auto &Parent = Stream[Stream.size() - 2];
						std::size_t Cursor = Parent.Index;
						SkipHorizontalSpace(Parent.Text, Cursor);
						if (Cursor < Parent.Text.size() && Parent.Text[Cursor] == '(')
						{
							std::string Body;
							const auto P = PositionAt(F.OriginOffset);
							if (!PrepareFunction(Alias, M->second, Parent.Text, Cursor,
									{FileLiteral, P.Line, nullptr}, Body))
							{
								return false;
							}

							Parent.Index = Cursor;
							OutSource.resize(F.OutputStart);
							if (OutLineMap)
							{
								Map.OutByteToPhysical.resize(F.OutputStart);
							}

							Observer = F.Before;
							Expanding.erase(F.Name);
							Expanding.insert(Alias);
							F.Name = Alias;
							F.NoReglue.insert(Alias);
							F.Text = std::move(Body);
							F.Index = 0;
							continue;
						}
					}
					Expanding.erase(F.Name);
				}
				Stream.pop_back();
				continue;
			}
			const std::size_t Start = F.Index;
			const std::size_t Offset = F.bRoot ? Start : F.OriginOffset;
			const char C = F.Text[F.Index];
			if (!bInBlockComment && IsIdentStart(C) &&
				!(C == 'r' && Start + 1 < F.Text.size() && F.Text[Start + 1] == '"'))
			{
				std::string Name;
				ParseIdentifier(F.Text, F.Index, Name);
				const auto It = Macros.find(Name);
				if (It != Macros.end() && !Expanding.count(Name))
				{
					const auto &M = It->second;
					if (M.Builtin != FMacro::EBuiltin::None)
					{
						const auto Value = M.Builtin == FMacro::EBuiltin::File
											   ? FileLiteral
											   : std::to_string(PositionAt(Offset).Line);
						for (char B : Value)
						{
							if (!Emit(B, Offset))
							{
								return false;
							}
						}

						if (bWaiting)
						{
							return false;
						}

						continue;
					}
					BudgetPosition = PositionAt(Offset);
					CheckExpansionSize(0, M.Replacement().size());
					std::string Body = M.Replacement();
					bool Invoke = !M.bFunctionLike;
					if (M.bFunctionLike)
					{
						std::size_t Cursor = F.Index;
						SkipHorizontalSpace(F.Text, Cursor);
						if (Cursor < F.Text.size() && F.Text[Cursor] == '(')
						{
							const auto P = PositionAt(Offset);
							if (!PrepareFunction(
									Name, M, F.Text, Cursor, {FileLiteral, P.Line, nullptr}, Body))
							{
								return false;
							}

							F.Index = Cursor;
							Invoke = true;
						}
					}
					if (Invoke)
					{
						if (Stream.size() >= MaxMacroExpansionDepth)
						{
							OutError = "macro expansion nested too deeply";
							return false;
						}
						FStreamFrame Child;
						Child.Text = std::move(Body);
						Child.Name = Name;
						Child.NoReglue.insert(Name);
						Child.OriginOffset = Offset;
						Child.OutputStart = OutSource.size();
						Child.Before = Observer;
						Expanding.insert(Name);
						Stream.push_back(std::move(Child));
						continue;
					}
				}
				for (std::size_t I = Start; I < F.Index; ++I)
				{
					if (!Emit(F.Text[I], F.bRoot ? I : Offset))
					{
						return false;
					}
				}

			}
			else
			{
				// Emit literals/comments without scanning their identifiers as macros.
				std::size_t End = Start + 1;
				if (bInBlockComment)
				{
					const auto Close = F.Text.find("*/", Start);
					End = Close == std::string::npos ? F.Text.size() : Close + 2;
					if (Close != std::string::npos)
					{
						bInBlockComment = false;
					}

				}
				else if (C == '/' && End < F.Text.size() && F.Text[End] == '/')
				{
					End = F.Text.size();
				}

				else if (C == '/' && End < F.Text.size() && F.Text[End] == '*')
				{
					End = Start + 2;
					bInBlockComment = true;
				}
				else if (C == '"' || C == '\'')
				{
					const bool Raw = C == '"' && Start > 0 && F.Text[Start - 1] == 'r' &&
									 (Start < 2 || !IsIdentContinue(F.Text[Start - 2]));
					while (End < F.Text.size())
					{
						const char B = F.Text[End++];
						if (!Raw && B == '\\' && End < F.Text.size())
						{
							++End;
						}

						else if (B == C)
						{
							break;
						}
					}
				}
				while (F.Index < End)
				{
					const auto I = F.Index++;
					if (!Emit(F.Text[I], F.bRoot ? I : Offset))
					{
						return false;
					}

					if (bWaiting)
					{
						return false;
					}
				}
			}
			if (bWaiting)
			{
				return false;
			}
		}
		return true;
	}

	FPreprocessorSession::FPreprocessorSession(
		std::string S, const std::unordered_map<std::string, std::string> &D,
		const std::vector<FPredefinedMacro> &M, FPreprocessContext C, bool bMap)
		: Impl(std::make_unique<FImpl>())
	{
		Impl->Input = std::move(S);
		Impl->Context = std::move(C);
		if (!Impl->Context.Budget)
		{
			Impl->Context.Budget = std::make_shared<FPreprocessBudget>();
		}
		if (bMap)
		{
			Impl->OutLineMap = &Impl->Map;
		}

		Impl->Init(D, M);
		FPreprocessBudgetScope BudgetScope(*Impl->Context.Budget);
		try
		{
			ChargePreprocessWork(Impl->Input.size());
		}
		catch (const FPreprocessLimit& Limit)
		{
			Impl->OutError = Limit.Message;
			Impl->ErrorLineValue = Limit.Position.Line;
			Impl->ErrorColumnValue = Limit.Position.Column;
		}
	}
	FPreprocessorSession::~FPreprocessorSession() = default;
	EPreprocessStep FPreprocessorSession::Advance()
	{
		FAnalysisStage Stage("preprocess");
		Impl->bStarted = true;
		if (!Impl->OutError.empty())
		{
			FillFromEnglishMessage(
				Impl->Result.Diagnostic, Impl->OutError,
				{Impl->Context.LogicalPath, Impl->ErrorLineValue ? Impl->ErrorLineValue : 1,
				 Impl->ErrorColumnValue});
			Impl->Result.Diagnostic.bPhysicalLocation = true;
			return EPreprocessStep::Error;
		}
		if (Impl->bWaiting)
		{
			return EPreprocessStep::NeedModule;
		}

		FPreprocessBudgetScope BudgetScope(*Impl->Context.Budget);
		bool bComplete = Impl->bComplete;
		try
		{
			bComplete = bComplete || Impl->Run();
		}
		catch (const FPreprocessLimit& Limit)
		{
			Impl->OutError = Limit.Message;
			Impl->ErrorLineValue = Limit.Position.Line;
			Impl->ErrorColumnValue = Limit.Position.Column;
			Impl->OutSource.clear();
			Impl->Map = {};
			Impl->Result.PublicMacros.clear();
			Impl->Result.Imports.clear();
			Impl->bWaiting = false;
		}
		if (bComplete)
		{
			return EPreprocessStep::Complete;
		}

		if (Impl->ErrorLineValue == 0 && !Impl->OutError.empty())
		{
			Impl->ErrorLineValue = Impl->LogicalStartLine;
		}

		if (!Impl->OutError.empty())
		{
			FillFromEnglishMessage(Impl->Result.Diagnostic, Impl->OutError,
								   {Impl->Context.LogicalPath, Impl->ErrorLineValue,
									Impl->ErrorColumnValue});
			Impl->Result.Diagnostic.bPhysicalLocation = true;
		}
		return Impl->bWaiting && Impl->OutError.empty() ? EPreprocessStep::NeedModule
														: EPreprocessStep::Error;
	}
	const FPreprocessImport &FPreprocessorSession::PendingImport() const
	{
		return Impl->Import;
	}
	bool FPreprocessorSession::ResumeImport(std::string P, const std::vector<FPublicMacro> &D,
											std::shared_ptr<const void> Owner)
	{
		if (!Impl->bWaiting || !Impl->OutError.empty())
		{
			return false;
		}

		const auto EventCount = Impl->Map.ActiveMacros.size();
		try
		{
			if (Impl->Install(std::move(P), D, std::move(Owner)))
			{
				return true;
			}

		}
		catch (const std::bad_alloc&)
		{
			Impl->bWaiting = false;
			Impl->Map.ActiveMacros.resize(EventCount);
			Impl->Result.PublicMacros.clear();
			Impl->OutError = "out of memory";
		}
		Impl->ErrorLineValue = Impl->Import.Position.Line;
		Impl->ErrorColumnValue = Impl->Import.Position.Column;
		FillFromEnglishMessage(Impl->Result.Diagnostic, Impl->OutError,
			{Impl->Context.LogicalPath, Impl->ErrorLineValue, Impl->ErrorColumnValue});
		Impl->Result.Diagnostic.bPhysicalLocation = true;
		return false;
	}
	bool FPreprocessorSession::ImportImplicitMacros(const std::vector<FPublicMacro>& Macros,
		std::shared_ptr<const void> Owner)
	{
		if (Impl->bStarted || !Impl->OutError.empty()) return false;
		Impl->Import = {};
		Impl->Import.ActivationPosition = {1, 1};
		const auto EventCount = Impl->Map.ActiveMacros.size();
		try
		{
			if (Impl->Install({}, Macros, std::move(Owner), true)) return true;
		}
		catch (const std::bad_alloc&)
		{
			Impl->Map.ActiveMacros.resize(EventCount);
			Impl->Result.PublicMacros.clear();
			Impl->OutError = "out of memory";
		}
		Impl->ErrorLineValue = Impl->ErrorColumnValue = 1;
		FillFromEnglishMessage(Impl->Result.Diagnostic, Impl->OutError,
			{Impl->Context.LogicalPath, 1, 1});
		Impl->Result.Diagnostic.bPhysicalLocation = true;
		return false;
	}
	const std::string &FPreprocessorSession::Source() const
	{
		return Impl->OutSource;
	}
	const std::string &FPreprocessorSession::Error() const
	{
		return Impl->OutError;
	}
	std::uint32_t FPreprocessorSession::ErrorLine() const
	{
		return Impl->ErrorLineValue;
	}
	const FPreprocessResult &FPreprocessorSession::Result() const
	{
		return Impl->Result;
	}
	const FPreprocessLineMap &FPreprocessorSession::LineMap() const
	{
		return Impl->Map;
	}

	bool PreprocessSource(std::string_view Source,
						  const std::unordered_map<std::string, std::string> &InitialDefines,
						  const std::vector<FPredefinedMacro> &PredefinedMacros,
						  const FPreprocessContext &Context, std::string &OutSource,
						  std::string &OutError, std::uint32_t *OutErrorLine,
						  FPreprocessLineMap *OutLineMap)
	{
		OutError.clear();
		if (Context.Result)
		{
			*Context.Result = {};
		}

		FPreprocessorSession Session(
			std::string(Source), InitialDefines, PredefinedMacros, Context, OutLineMap != nullptr);
		bool Success = false;
		for (;;)
		{
			const auto Step = Session.Advance();
			if (Step == EPreprocessStep::Complete)
			{
				Success = true;
				break;
			}
			if (Step == EPreprocessStep::Error)
			{
				break;
			}

			auto Import = Session.PendingImport();
			std::vector<FPublicMacro> Macros;
			if (!Context.ResolveModule)
			{
				OutError = "module preprocessing requires an import resolver";
				break;
			}
			if (!Context.ResolveModule(Import, Macros, OutError) ||
				!Session.ResumeImport(Import.ResolvedPath, Macros))
			{
				break;
			}
		}
		OutSource = Session.Source();
		if (OutError.empty() || !Session.Error().empty())
		{
			OutError = Session.Error();
		}

		if (OutErrorLine)
		{
			*OutErrorLine = Success
				? 0
				: (Session.ErrorLine() ? Session.ErrorLine() : Session.PendingImport().Position.Line);
		}

		if (OutLineMap)
		{
			*OutLineMap = Session.LineMap();
			OutLineMap->PreprocessedSource = OutSource;
		}
		if (Context.Result)
		{
			*Context.Result = Session.Result();
			if (!Success)
			{
				Context.Result->PublicMacros.clear();
				if (Context.Result->Diagnostic.Message.empty())
				{
					FillFromEnglishMessage(Context.Result->Diagnostic, OutError,
						{Context.LogicalPath,
						 Session.ErrorLine() ? Session.ErrorLine()
										 : Session.PendingImport().Position.Line,
						 Session.PendingImport().Position.Column});
					Context.Result->Diagnostic.bPhysicalLocation = true;
				}
			}
		}
		return Success;
	}

	bool PreprocessSource(
		const std::string_view Source,
		const std::unordered_map<std::string, std::string>& InitialDefines,
		const std::vector<FPredefinedMacro>& PredefinedMacros,
		std::string& OutSource,
		std::string& OutError,
		std::uint32_t* const OutErrorLine,
		FPreprocessLineMap* const OutLineMap)
	{
		static const FPreprocessContext MemoryContext;
		return PreprocessSource(
			Source,
			InitialDefines,
			PredefinedMacros,
			MemoryContext,
			OutSource,
			OutError,
			OutErrorLine,
			OutLineMap);
	}

	bool PreprocessSource(
		const std::string_view Source,
		const std::unordered_map<std::string, std::string>& InitialDefines,
		std::string& OutSource,
		std::string& OutError,
		std::uint32_t* const OutErrorLine,
		FPreprocessLineMap* const OutLineMap)
	{
		static const std::vector<FPredefinedMacro> Empty;
		return PreprocessSource(
			Source,
			InitialDefines,
			Empty,
			OutSource,
			OutError,
			OutErrorLine,
			OutLineMap);
	}

	bool MapPreprocessedToPhysical(
		const FPreprocessLineMap& Map,
		const std::uint32_t PreprocessedLine,
		const std::uint32_t PreprocessedColumn,
		std::uint32_t& OutPhysicalLine,
		std::uint32_t& OutPhysicalColumn)
	{
		if (Map.OutByteToPhysical.empty() || PreprocessedLine == 0 || PreprocessedColumn == 0)
		{
			return false;
		}
		std::uint32_t CurrentLine = 1;
		std::uint32_t LineStart = 0;
		for (std::size_t Index = 0; Index < Map.OutByteToPhysical.size(); ++Index)
		{
			if (CurrentLine == PreprocessedLine)
			{
				const std::uint32_t Column =
					static_cast<std::uint32_t>(Index - LineStart) + 1;
				if (Column == PreprocessedColumn)
				{
					OutPhysicalLine = Map.OutByteToPhysical[Index].Line;
					OutPhysicalColumn = Map.OutByteToPhysical[Index].Column;
					return true;
				}
			}
			if (Index < Map.PreprocessedSource.size()
				&& Map.PreprocessedSource[Index] == '\n')
			{
				++CurrentLine;
				LineStart = static_cast<std::uint32_t>(Index) + 1;
			}
		}
		return false;
	}

	bool MapPhysicalToPreprocessed(
		const FPreprocessLineMap& Map,
		const std::uint32_t PhysicalLine,
		const std::uint32_t PhysicalColumn,
		std::uint32_t& OutPreprocessedLine,
		std::uint32_t& OutPreprocessedColumn)
	{
		if (Map.OutByteToPhysical.empty() || PhysicalLine == 0 || PhysicalColumn == 0)
		{
			return false;
		}

		std::size_t BestIndex = Map.OutByteToPhysical.size();
		std::uint32_t BestDistance = UINT32_MAX;
		for (std::size_t Index = 0; Index < Map.OutByteToPhysical.size(); ++Index)
		{
			const FPreprocessSourcePos& Pos = Map.OutByteToPhysical[Index];
			if (Pos.Line != PhysicalLine)
			{
				continue;
			}
			if (Pos.Column == PhysicalColumn)
			{
				BestIndex = Index;
				break;
			}
			if (Pos.Column <= PhysicalColumn)
			{
				const std::uint32_t Distance = PhysicalColumn - Pos.Column;
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					BestIndex = Index;
				}
			}
		}
		if (BestIndex >= Map.OutByteToPhysical.size())
		{
			return false;
		}

		std::uint32_t CurrentLine = 1;
		std::uint32_t LineStart = 0;
		for (std::size_t Index = 0; Index <= BestIndex; ++Index)
		{
			if (Index == BestIndex)
			{
				OutPreprocessedLine = CurrentLine;
				OutPreprocessedColumn =
					static_cast<std::uint32_t>(Index - LineStart) + 1;
				return true;
			}
			if (Index < Map.PreprocessedSource.size()
				&& Map.PreprocessedSource[Index] == '\n')
			{
				++CurrentLine;
				LineStart = static_cast<std::uint32_t>(Index) + 1;
			}
		}
		return false;
	}

	bool MapPhysicalLineToPreprocessedLine(
		const FPreprocessLineMap& Map,
		const std::uint32_t PhysicalLine,
		std::uint32_t& OutPreprocessedLine)
	{
		if (Map.OutByteToPhysical.empty() || PhysicalLine == 0)
		{
			return false;
		}
		for (std::size_t Index = 0; Index < Map.OutByteToPhysical.size(); ++Index)
		{
			if (Map.OutByteToPhysical[Index].Line != PhysicalLine)
			{
				continue;
			}
			std::uint32_t CurrentLine = 1;
			for (std::size_t Byte = 0; Byte <= Index; ++Byte)
			{
				if (Byte == Index)
				{
					OutPreprocessedLine = CurrentLine;
					return true;
				}
				if (Byte < Map.PreprocessedSource.size()
					&& Map.PreprocessedSource[Byte] == '\n')
				{
					++CurrentLine;
				}
			}
		}
		return false;
	}

	bool MapPreprocessedLineToPhysicalLine(
		const FPreprocessLineMap& Map,
		const std::uint32_t PreprocessedLine,
		std::uint32_t& OutPhysicalLine)
	{
		if (Map.OutByteToPhysical.empty() || PreprocessedLine == 0)
		{
			return false;
		}
		std::uint32_t CurrentLine = 1;
		for (std::size_t Index = 0; Index < Map.OutByteToPhysical.size(); ++Index)
		{
			if (CurrentLine == PreprocessedLine)
			{
				OutPhysicalLine = Map.OutByteToPhysical[Index].Line;
				return true;
			}
			if (Index < Map.PreprocessedSource.size()
				&& Map.PreprocessedSource[Index] == '\n')
			{
				++CurrentLine;
			}
		}
		return false;
	}
}

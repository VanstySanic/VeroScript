// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSoftBuiltins.h"
#include "VeroScriptQueryLexicalSnapshot.h"
#include "VeroScriptAnalysisControl.h"

#include "VeroScriptStandardNamespaceMetadata.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptLexer.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptNamespaceLookup.h"
#include "VeroScriptTypeHierarchy.h"
#include "VeroScriptSourceMetadataBuiltins.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		void StripExtensionCvRef(std::string& Type)
		{
			if (Type.rfind("const ref ", 0) == 0)
			{
				Type.erase(0, 10);
			}
			else if (Type.rfind("ref ", 0) == 0)
			{
				Type.erase(0, 4);
			}
			if (Type.rfind("const ", 0) == 0)
			{
				Type.erase(0, 6);
			}
			while (!Type.empty() && Type.back() == '&')
			{
				Type.pop_back();
			}
		}

		bool SplitExtensionGenericType(
			const std::string& Type,
			std::string& OutBase,
			std::vector<std::string>& OutArgs)
		{
			OutBase.clear();
			OutArgs.clear();
			const std::size_t Open = Type.find('<');
			if (Open == std::string::npos || Type.empty() || Type.back() != '>')
			{
				return false;
			}
			OutBase = Type.substr(0, Open);
			std::size_t Start = Open + 1;
			int Depth = 0;
			for (std::size_t Index = Start; Index + 1 < Type.size(); ++Index)
			{
				if (Type[Index] == '<')
				{
					++Depth;
				}
				else if (Type[Index] == '>')
				{
					--Depth;
				}
				else if (Type[Index] == ',' && Depth == 0)
				{
					OutArgs.push_back(Type.substr(Start, Index - Start));
					Start = Index + 1;
				}
			}
			OutArgs.push_back(Type.substr(Start, Type.size() - Start - 1));
			return !OutBase.empty()
				&& std::all_of(
					OutArgs.begin(),
					OutArgs.end(),
					[](const std::string& Arg)
					{
						return !Arg.empty();
					});
		}

		bool MatchExtensionReceiverType(
			const FFunctionDecl& Function,
			std::string ReceiverType,
			std::unordered_map<std::string, std::string>& OutTypeBindings)
		{
			OutTypeBindings.clear();
			if (Function.Parameters.empty() || ReceiverType.empty())
			{
				return false;
			}
			std::string Pattern = Function.Parameters.front().TypeName;
			StripExtensionCvRef(Pattern);
			StripExtensionCvRef(ReceiverType);
			const std::function<bool(const std::string&, const std::string&)> Match =
				[&](const std::string& PatternType, const std::string& ActualType) -> bool
				{
					if (PatternType == ActualType)
					{
						return true;
					}
					if (std::find(
								Function.TypeParams.begin(),
								Function.TypeParams.end(),
								PatternType) != Function.TypeParams.end())
					{
						const auto Existing = OutTypeBindings.find(PatternType);
						if (Existing == OutTypeBindings.end())
						{
							OutTypeBindings.emplace(PatternType, ActualType);
							return true;
						}
						return Existing->second == ActualType;
					}
					std::string PatternBase;
					std::string ActualBase;
					std::vector<std::string> PatternArgs;
					std::vector<std::string> ActualArgs;
					if (!SplitExtensionGenericType(PatternType, PatternBase, PatternArgs)
							|| !SplitExtensionGenericType(ActualType, ActualBase, ActualArgs)
							|| PatternBase != ActualBase
							|| PatternArgs.size() != ActualArgs.size())
					{
						return false;
					}
					for (std::size_t Index = 0; Index < PatternArgs.size(); ++Index)
					{
						if (!Match(PatternArgs[Index], ActualArgs[Index]))
						{
							return false;
						}
					}
					return true;
				};
			return Match(Pattern, ReceiverType);
		}

		std::string SubstituteExtensionTypeParams(
			const std::string& TypeName,
			const std::unordered_map<std::string, std::string>& TypeBindings)
		{
			std::string Result;
			Result.reserve(TypeName.size());
			for (std::size_t Index = 0; Index < TypeName.size();)
			{
				const unsigned char Character = static_cast<unsigned char>(TypeName[Index]);
				if (!(std::isalpha(Character) || Character == '_'))
				{
					Result.push_back(TypeName[Index++]);
					continue;
				}
				const std::size_t Begin = Index++;
				while (Index < TypeName.size())
				{
					const unsigned char Next = static_cast<unsigned char>(TypeName[Index]);
					if (!(std::isalnum(Next) || Next == '_'))
					{
						break;
					}
					++Index;
				}
				const std::string Token = TypeName.substr(Begin, Index - Begin);
				const auto Binding = TypeBindings.find(Token);
				Result += Binding == TypeBindings.end() ? Token : Binding->second;
			}
			return Result;
		}

		bool PrefixMatchesLabel(const std::string_view Label, const std::string_view Prefix)
		{
			return FuzzyIdentifierPrefixMatches(Label, Prefix);
		}

		void AddMethod(
			std::unordered_map<std::string, FInheritedMemberCompletion>& Out,
			const std::string_view Name,
			const std::string_view Detail,
			const std::string_view Prefix)
		{
			if (!PrefixMatchesLabel(Name, Prefix))
			{
				return;
			}
			// Never overwrite hierarchy / user members with soft stubs.
			if (Out.find(std::string(Name)) != Out.end())
			{
				return;
			}
			FInheritedMemberCompletion Member;
			Member.Kind = EDocumentSymbolKind::Method;
			Member.Label.assign(Name.data(), Name.size());
			Member.Detail.assign(Detail.data(), Detail.size());
			Member.bSoftBuiltin = true;
			Out.emplace(Member.Label, std::move(Member));
		}

		void MarkSoft(FDocumentSymbol& Symbol)
		{
			Symbol.bSoftBuiltin = true;
			Symbol.FileLabel.clear();
			Symbol.StartLine = 0;
			Symbol.StartColumn = 0;
			Symbol.EndLine = 0;
			Symbol.EndColumn = 0;
			for (FDocumentSymbol& Child : Symbol.Children)
			{
				MarkSoft(Child);
			}
		}

		bool HasTopLevelFunction(
			const FProgram& Program,
			const std::vector<FDocumentSymbol>& Symbols,
			const char* Name)
		{
			for (const FFunctionDecl& Function : Program.Functions)
			{
				if (Function.Name == Name)
				{
					return true;
				}
			}
			for (const FDocumentSymbol& Symbol : Symbols)
			{
				if (Symbol.Kind == EDocumentSymbolKind::Function && Symbol.Name == Name)
				{
					return true;
				}
			}
			return false;
		}

		bool IsHostFormatFamily(const std::string_view Name)
		{
			return Name == "print" || Name == "printf"
				|| Name == "format" || Name == "formatf";
		}

		void AddFunctionSymbol(
			const FProgram& Program,
			std::vector<FDocumentSymbol>& Out,
			const char* Name,
			const char* Detail,
			const char* Documentation = nullptr)
		{
			// Always expose compiler-provided varargs for the public formatting
			// family, even when string.vs contributes an array<string> wrapper.
			if (!IsHostFormatFamily(Name) && HasTopLevelFunction(Program, Out, Name))
			{
				return;
			}
			FDocumentSymbol Symbol;
			Symbol.Kind = EDocumentSymbolKind::Function;
			Symbol.Name = Name;
			Symbol.Detail = Detail;
			if (Documentation)
			{
				Symbol.Documentation = Documentation;
			}
			Symbol.bExported = true;
			MarkSoft(Symbol);
			Out.push_back(std::move(Symbol));
		}

		FDocumentSymbol MakeField(const char* Name, const char* Detail)
		{
			FDocumentSymbol Field;
			Field.Kind = EDocumentSymbolKind::Field;
			Field.Name = Name;
			Field.Detail = Detail;
			Field.bSoftBuiltin = true;
			Field.StartLine = 0;
			Field.StartColumn = 0;
			Field.EndLine = 0;
			Field.EndColumn = 0;
			return Field;
		}

		FDocumentSymbol MakeCtor(const char* TypeName, const char* Detail)
		{
			FDocumentSymbol Ctor;
			Ctor.Kind = EDocumentSymbolKind::Constructor;
			Ctor.Name = TypeName;
			Ctor.Detail = Detail;
			Ctor.bSoftBuiltin = true;
			Ctor.StartLine = 0;
			Ctor.StartColumn = 0;
			Ctor.EndLine = 0;
			Ctor.EndColumn = 0;
			return Ctor;
		}

		FDocumentSymbol MakeMethodSymbol(const char* Name, const char* Detail)
		{
			FDocumentSymbol Method;
			Method.Kind = EDocumentSymbolKind::Method;
			Method.Name = Name;
			Method.Detail = Detail;
			Method.bSoftBuiltin = true;
			return Method;
		}

		std::string_view StripTypeDecorations(const std::string_view TypeName)
		{
			std::string_view Simple = TypeName.substr(
				FindLinkageIdentityStart(TypeName));
			// Qualifiers (module linkage, namespace path) only ever precede the
			// generic argument list; separators inside `<...>` or after the mangled
			// `#` belong to the args (`delegate<ns.Foo>` / `ns.Box#7:ns.Color` must
			// keep their head intact).
			const std::size_t Hash = Simple.find('#');
			const std::size_t Open = Simple.find('<');
			const std::string_view Head = Simple.substr(0, Open < Hash ? Open : Hash);
			std::size_t QualifierEnd = 0;
			if (const std::size_t UnitSep = Head.rfind('\x1F');
				UnitSep != std::string_view::npos)
			{
				QualifierEnd = UnitSep + 1;
			}
			if (const std::size_t ColonSep = Head.rfind("::");
				ColonSep != std::string_view::npos && ColonSep + 2 > QualifierEnd)
			{
				QualifierEnd = ColonSep + 2;
			}
			if (const std::size_t DotSep = Head.rfind('.');
				DotSep != std::string_view::npos && DotSep + 1 > QualifierEnd)
			{
				QualifierEnd = DotSep + 1;
			}
			return Hash == std::string_view::npos
				? Simple.substr(QualifierEnd)
				: Simple.substr(QualifierEnd, Hash - QualifierEnd);
		}

		bool SplitTypeApp(
			const std::string_view TypeName,
			std::string& OutHead,
			std::vector<std::string>& OutArgs)
		{
			OutHead.clear();
			OutArgs.clear();
			const std::size_t Open = TypeName.find('<');
			if (Open == std::string_view::npos || TypeName.empty() || TypeName.back() != '>')
			{
				OutHead.assign(TypeName.data(), TypeName.size());
				return false;
			}
			OutHead = std::string(TypeName.substr(0, Open));
			int Depth = 0;
			int ParenthesisDepth = 0;
			std::size_t ArgStart = Open + 1;
			for (std::size_t Index = Open + 1; Index + 1 < TypeName.size(); ++Index)
			{
				const char Char = TypeName[Index];
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
				else if (Char == ',' && Depth == 0 && ParenthesisDepth == 0)
				{
					std::string Arg(TypeName.substr(ArgStart, Index - ArgStart));
					while (!Arg.empty() && std::isspace(static_cast<unsigned char>(Arg.front())))
					{
						Arg.erase(Arg.begin());
					}
					while (!Arg.empty() && std::isspace(static_cast<unsigned char>(Arg.back())))
					{
						Arg.pop_back();
					}
					if (!Arg.empty())
					{
						OutArgs.push_back(std::move(Arg));
					}
					ArgStart = Index + 1;
				}
			}
			std::string Last(TypeName.substr(ArgStart, TypeName.size() - 1 - ArgStart));
			while (!Last.empty() && std::isspace(static_cast<unsigned char>(Last.front())))
			{
				Last.erase(Last.begin());
			}
			while (!Last.empty() && std::isspace(static_cast<unsigned char>(Last.back())))
			{
				Last.pop_back();
			}
			if (!Last.empty())
			{
				OutArgs.push_back(std::move(Last));
			}
			return !OutArgs.empty();
		}

		bool IsDelegateTypeName(const std::string_view TypeName)
		{
			std::string Head;
			std::vector<std::string> Args;
			(void)SplitTypeApp(StripTypeDecorations(TypeName), Head, Args);
			return Head == "delegate";
		}

		bool IsMulticastDelegateTypeName(const std::string_view TypeName)
		{
			if (Runtime::IsMulticastDelegateTypeName(TypeName))
			{
				return true;
			}
			std::string Head;
			std::vector<std::string> Args;
			(void)SplitTypeApp(StripTypeDecorations(TypeName), Head, Args);
			return Head == "multicast_delegate";
		}

		bool ProgramHasUserTypeNamed(const FProgram& Program, const char* Name)
		{
			for (const FClassDecl& ClassDecl : Program.Classes)
			{
				if (ClassDecl.Name == Name)
				{
					return true;
				}
			}
			for (const FStructDecl& StructDecl : Program.Structs)
			{
				if (StructDecl.Name == Name)
				{
					return true;
				}
			}
			return false;
		}

		bool CanUseSoftDelegateMembers(const FProgram* Program, const std::string_view TypeName)
		{
			// A leaf spelling alone cannot identify a standard delegate. In
			// particular, custom::delegate<T> belongs to the user's namespace.
			std::string Head;
			std::vector<std::string> Args;
			(void)SplitTypeApp(TypeName, Head, Args);
			Head = FormatNamespaceQualifiedName(Head);
			if (Head.rfind("::", 0) == 0)
			{
				Head.erase(0, 2);
			}
			if (Head == "std::delegate" || Head == "std::multicast_delegate")
			{
				Head.erase(0, 5);
			}
			if (!Program || (Head != "delegate" && Head != "multicast_delegate")
				|| (Head == "delegate" && (Args.empty() || Args.size() > 4))
				|| (Head == "multicast_delegate" && Args.size() > 3))
			{
				return false;
			}
			const auto HasDeclaration = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					CheckAnalysisCancellation();
					if (Declaration.Name != Head || Args.size() > TemplateParameterNames(Declaration).size()
						|| (Declaration.ModulePath != Program->EntryModulePath && !Declaration.bExported
							&& Program->ProjectVisibleModulePaths.find(Declaration.ModulePath)
								== Program->ProjectVisibleModulePaths.end()))
					{
						continue;
					}
					bool bAcceptsArity = true;
					for (std::size_t Index = Args.size(); Index < TemplateParameterNames(Declaration).size(); ++Index)
					{
						if (Index >= TemplateParameterDefaults(Declaration).size()
							|| TemplateParameterDefaults(Declaration)[Index].empty())
						{
							bAcceptsArity = false;
							break;
						}
					}
					if (bAcceptsArity)
					{
						return true;
					}
				}
				return false;
			};
			return !HasDeclaration(Program->Classes) && !HasDeclaration(Program->Structs)
				&& !HasDeclaration(Program->Interfaces);
		}

		bool ProgramHasUserTypeArity(
			const FProgram& Program,
			const std::string_view Name,
			const std::size_t Arity)
		{
			for (const FClassDecl& Decl : Program.Classes)
			{
				if (Decl.Name == Name && TemplateParameterNames(Decl).size() == Arity)
				{
					return true;
				}
			}
			for (const FStructDecl& Decl : Program.Structs)
			{
				if (Decl.Name == Name && TemplateParameterNames(Decl).size() == Arity)
				{
					return true;
				}
			}
			return false;
		}

		std::string SubstituteDelegatePlaceholders(
			const std::string_view OwnerTypeName,
			const std::string_view DetailTemplate)
		{
			std::string Head;
			std::vector<std::string> Args;
			(void)SplitTypeApp(StripTypeDecorations(OwnerTypeName), Head, Args);
			std::string R;
			std::string A;
			std::string B;
			std::string C;
			if (Head == "delegate")
			{
				if (!Args.empty())
				{
					R = Args[0];
				}
				if (Args.size() > 1)
				{
					A = Args[1];
				}
				if (Args.size() > 2)
				{
					B = Args[2];
				}
				if (Args.size() > 3)
				{
					C = Args[3];
				}
			}
			else if (IsMulticastDelegateTypeName(OwnerTypeName))
			{
				if (!Args.empty())
				{
					A = Args[0];
				}
				if (Args.size() > 1)
				{
					B = Args[1];
				}
				if (Args.size() > 2)
				{
					C = Args[2];
				}
			}
			else
			{
				return std::string(DetailTemplate);
			}

			const auto IsIdentChar = [](const char C)
			{
				return std::isalnum(static_cast<unsigned char>(C)) != 0 || C == '_';
			};
			std::string Out;
			Out.reserve(DetailTemplate.size() + 8);
			for (std::size_t Index = 0; Index < DetailTemplate.size(); ++Index)
			{
				const char Char = DetailTemplate[Index];
				const bool bWordStart = Index == 0 || !IsIdentChar(DetailTemplate[Index - 1]);
				const bool bWordEnd = Index + 1 >= DetailTemplate.size()
					|| !IsIdentChar(DetailTemplate[Index + 1]);
				if (bWordStart && bWordEnd && Char == 'R')
				{
					Out += R.empty() ? "R" : R;
					continue;
				}
				if (bWordStart && bWordEnd && Char == 'A')
				{
					Out += A.empty() ? "A" : A;
					continue;
				}
				if (bWordStart && bWordEnd && Char == 'B')
				{
					Out += B.empty() ? "B" : B;
					continue;
				}
				if (bWordStart && bWordEnd && Char == 'C')
				{
					Out += C.empty() ? "C" : C;
					continue;
				}
				Out.push_back(Char);
			}
			return Out;
		}

		void AppendSoftMulticastDelegateMembers(
			const std::string_view TypeName,
			const std::string_view Prefix,
			const std::size_t Arity,
			std::unordered_map<std::string, FInheritedMemberCompletion>& OutMembersByLabel)
		{
			const auto Subst = [TypeName](const char* Detail)
			{
				return SubstituteDelegatePlaceholders(TypeName, Detail);
			};
			if (Arity == 0)
			{
				AddMethod(OutMembersByLabel, "add", Subst("void (fn()->void h)"), Prefix);
				AddMethod(OutMembersByLabel, "remove", Subst("void (fn()->void h)"), Prefix);
				AddMethod(OutMembersByLabel, "broadcast", "void ()", Prefix);
			}
			else if (Arity == 1)
			{
				AddMethod(OutMembersByLabel, "add", Subst("void (fn(A)->void h)"), Prefix);
				AddMethod(OutMembersByLabel, "remove", Subst("void (fn(A)->void h)"), Prefix);
				AddMethod(OutMembersByLabel, "broadcast", Subst("void (A value)"), Prefix);
			}
			else if (Arity == 2)
			{
				AddMethod(OutMembersByLabel, "add", Subst("void (fn(A, B)->void h)"), Prefix);
				AddMethod(OutMembersByLabel, "remove", Subst("void (fn(A, B)->void h)"), Prefix);
				AddMethod(OutMembersByLabel, "broadcast", Subst("void (A a, B b)"), Prefix);
			}
			else
			{
				AddMethod(OutMembersByLabel, "add", Subst("void (fn(A, B, C)->void h)"), Prefix);
				AddMethod(OutMembersByLabel, "remove", Subst("void (fn(A, B, C)->void h)"), Prefix);
				AddMethod(OutMembersByLabel, "broadcast", Subst("void (A a, B b, C c)"), Prefix);
			}
			AddMethod(OutMembersByLabel, "count", "int64 ()", Prefix);
			AddMethod(OutMembersByLabel, "clear", "void ()", Prefix);
		}

		void AppendSoftDelegateMembers(
			const std::string_view TypeName,
			const std::string_view Prefix,
			const std::size_t Arity,
			std::unordered_map<std::string, FInheritedMemberCompletion>& OutMembersByLabel)
		{
			const auto Subst = [TypeName](const char* Detail)
			{
				return SubstituteDelegatePlaceholders(TypeName, Detail);
			};
			if (Arity == 0)
			{
				AddMethod(OutMembersByLabel, "bind", Subst("void (fn()->R h)"), Prefix);
				AddMethod(OutMembersByLabel, "execute", Subst("R ()"), Prefix);
				AddMethod(OutMembersByLabel, "execute_if_bound", "bool ()", Prefix);
			}
			else if (Arity == 1)
			{
				AddMethod(OutMembersByLabel, "bind", Subst("void (fn(A)->R h)"), Prefix);
				AddMethod(OutMembersByLabel, "execute", Subst("R (A a)"), Prefix);
				AddMethod(OutMembersByLabel, "execute_if_bound", Subst("bool (A a)"), Prefix);
			}
			else if (Arity == 2)
			{
				AddMethod(OutMembersByLabel, "bind", Subst("void (fn(A, B)->R h)"), Prefix);
				AddMethod(OutMembersByLabel, "execute", Subst("R (A a, B b)"), Prefix);
				AddMethod(OutMembersByLabel, "execute_if_bound", Subst("bool (A a, B b)"), Prefix);
			}
			else
			{
				AddMethod(OutMembersByLabel, "bind", Subst("void (fn(A, B, C)->R h)"), Prefix);
				AddMethod(OutMembersByLabel, "execute", Subst("R (A a, B b, C c)"), Prefix);
				AddMethod(OutMembersByLabel, "execute_if_bound", Subst("bool (A a, B b, C c)"), Prefix);
			}
			AddMethod(OutMembersByLabel, "unbind", "void ()", Prefix);
			AddMethod(OutMembersByLabel, "is_bound", "bool ()", Prefix);
		}
	}

	std::string FindActiveNamespaceAt(
		const std::string_view Source,
		const std::string_view FileLabel,
		const FTextPosition Position)
	{
		if (ActiveQueryLexicalSnapshot && ActiveQueryLexicalSnapshot->Matches(Source, FileLabel))
		{
			return ActiveQueryLexicalSnapshot->NamespaceAt(Position);
		}
		FQueryLexicalSnapshot Snapshot(Source, FileLabel);
		return Snapshot.NamespaceAt(Position);
	}

	std::string ExtensionMethodLeafName(const std::string& Name)
	{
		const std::size_t Separator = Name.find_last_of('.');
		return Separator == std::string::npos ? Name : Name.substr(Separator + 1);
	}

	bool ExtensionFunctionIsVisible(
		const FProgram& Program,
		const FFunctionDecl& Function,
		const std::string_view ReferenceModulePath,
		const std::string_view ActiveNamespace,
		const FTextPosition ReferencePosition,
		const VeroScript::IHostBindingCatalog* const Bindings)
	{
		if (!ModuleVisibility::CanAccessSymbol(
				Program,
				Function.ModulePath,
				Function.bExported,
				std::string(ReferenceModulePath)))
		{
			return false;
		}

		const std::string Leaf = ExtensionMethodLeafName(Function.Name);
		const auto IsAccessibleFunctionNamed = [&](const std::string& Name)
		{
			return std::any_of(
				Program.Functions.begin(),
				Program.Functions.end(),
				[&](const FFunctionDecl& Candidate)
				{
					return Candidate.Name == Name
							&& ModuleVisibility::CanAccessSymbol(
								Program,
								Candidate.ModulePath,
								Candidate.bExported,
								std::string(ReferenceModulePath));
				});
		};

		// Match compiler lookup exactly: the first lexical namespace containing
		// any declaration owns the whole overload set, even if no overload later
		// matches the receiver. Only when every lexical scope is empty do using-
		// directives contribute their combined overload set.
		std::string Scope(ActiveNamespace);
		while (!Scope.empty())
		{
			const std::string Candidate = Scope + "." + Leaf;
			if (IsAccessibleFunctionNamed(Candidate))
			{
				return Function.Name == Candidate;
			}
			const std::size_t Dot = Scope.rfind('.');
			Scope = Dot == std::string::npos ? std::string{} : Scope.substr(0, Dot);
		}
		if (IsAccessibleFunctionNamed(Leaf))
		{
			return Function.Name == Leaf;
		}

		const std::size_t Separator = Function.Name.rfind('.');
		if (Separator == std::string::npos)
		{
			return false;
		}
		const std::string FunctionNamespace = Function.Name.substr(0, Separator);

		FNamespaceLookup Names(Program);
		if (Bindings)
		{
			const auto Add = [&](const auto &Values)
			{
				for (const auto &Value : Values)
				{
					Names.Add(Value.Name, Value.Origin.ModulePath, Value.Origin.bExported, false);
				}
			};
			Add(Bindings->GetNativeFunctions());
			Add(Bindings->GetTypes());
			Add(Bindings->GetTypeAliases());
			Add(Bindings->GetGenericTypeTemplates());
		}
		ModuleVisibility::FImportIndex Imports(Program.Imports);
		const auto NamespaceExists = [&](const std::string& Name)
		{
			return StandardLibrary::IsNamespace(Name) || Names.Contains(Program, Name, std::string(ReferenceModulePath), &Imports);
		};

		for (const FUsingNamespaceDecl& Directive : Program.UsingNamespaces)
		{
			if (Directive.ModulePath != ReferenceModulePath
				|| (!Directive.ScopeNamespace.empty()
					&& ActiveNamespace != Directive.ScopeNamespace
					&& ActiveNamespace.rfind(Directive.ScopeNamespace + ".", 0) != 0)
				|| ReferencePosition.Line < Directive.Location.Line
				|| (ReferencePosition.Line == Directive.Location.Line
					&& ReferencePosition.Column < Directive.Location.Column))
			{
				continue;
			}

			for (const auto& Target : BindNamespacePath(Directive.NamespaceName, Directive.ScopeNamespace, NamespaceExists))
			{
				if (Target == FunctionNamespace) return true;
			}
		}
		return false;
	}

	bool ExtensionReceiverTypeMatches(
		const FFunctionDecl& Function,
		std::string ReceiverType)
	{
		std::unordered_map<std::string, std::string> TypeBindings;
		return MatchExtensionReceiverType(Function, std::move(ReceiverType), TypeBindings);
	}

	bool ResolveExtensionMethodReturnType(
		const FFunctionDecl& Function,
		std::string ReceiverType,
		std::string& OutReturnType)
	{
		std::unordered_map<std::string, std::string> TypeBindings;
		if (!MatchExtensionReceiverType(Function, std::move(ReceiverType), TypeBindings))
		{
			return false;
		}
		OutReturnType = SubstituteExtensionTypeParams(Function.ReturnType, TypeBindings);
		return !OutReturnType.empty();
	}

	std::string FormatExtensionMethodDetail(const FFunctionDecl& Function)
	{
		std::string Detail = "extension " + Function.ReturnType + " "
			+ ExtensionMethodLeafName(Function.Name);
		if (!Function.TypeParams.empty())
		{
			Detail.push_back('<');
			for (std::size_t Index = 0; Index < Function.TypeParams.size(); ++Index)
			{
				if (Index > 0)
				{
					Detail += ", ";
				}
				Detail += Function.TypeParams[Index];
			}
			Detail.push_back('>');
		}
		Detail.push_back('(');
		for (std::size_t Index = 1; Index < Function.Parameters.size(); ++Index)
		{
			if (Index > 1)
			{
				Detail += ", ";
			}
			const FFunctionParam& Param = Function.Parameters[Index];
			if (Param.bIsConst)
			{
				Detail += "const ";
			}
			Detail += Param.TypeName;
			if (Param.bIsRef)
			{
				Detail += "&";
			}
			if (Param.bIsVariadic)
			{
				Detail += "...";
			}
			if (!Param.Name.empty())
			{
				Detail += " " + Param.Name;
			}
		}
		Detail += ")";
		return Detail;
	}

	bool ProgramHasUserPairArity2(const FProgram& Program)
	{
		for (const FStructDecl& StructDecl : Program.Structs)
		{
			if (StructDecl.Name == "pair" && TemplateParameterNames(StructDecl).size() == 2)
			{
				return true;
			}
		}
		for (const FClassDecl& ClassDecl : Program.Classes)
		{
			if (ClassDecl.Name == "pair" && TemplateParameterNames(ClassDecl).size() == 2)
			{
				return true;
			}
		}
		return false;
	}

	bool ProgramHasUserHandleArity1(const FProgram& Program)
	{
		for (const FClassDecl& ClassDecl : Program.Classes)
		{
			if (ClassDecl.Name == "handle" && TemplateParameterNames(ClassDecl).size() == 1)
			{
				return true;
			}
		}
		for (const FStructDecl& StructDecl : Program.Structs)
		{
			if (StructDecl.Name == "handle" && TemplateParameterNames(StructDecl).size() == 1)
			{
				return true;
			}
		}
		return false;
	}

	std::string SubstituteSoftTypeParams(
		const std::string_view OwnerTypeName,
		const std::string_view DetailTemplate)
	{
		std::string T;
		std::string U;
		std::string Key;
		std::string Value;
		if (Runtime::IsPairTypeName(OwnerTypeName)
			&& Runtime::ParsePairType(OwnerTypeName, T, U))
		{
			Key = T;
			Value = U;
		}
		else if (Runtime::ParseDynamicArrayElementType(OwnerTypeName, T)
			|| Runtime::ParseDequeElementType(OwnerTypeName, T)
			|| Runtime::ParseSetElementType(OwnerTypeName, T)
			|| Runtime::ParseMultiSetElementType(OwnerTypeName, T)
			|| Runtime::ParseQueueElementType(OwnerTypeName, T)
			|| Runtime::ParseStackElementType(OwnerTypeName, T)
			|| Runtime::ParseChannelElementType(OwnerTypeName, T))
		{
			U.clear();
		}
		else if (Runtime::SplitMapType(OwnerTypeName, Key, Value)
			|| Runtime::SplitOrderedMapType(OwnerTypeName, Key, Value))
		{
			// Use raw declared map arguments here. ParseMapType intentionally turns a
			// multimap's V into array<V> for expression/index inference; completion
			// templates below add that grouping exactly where the API returns it.
			T = Key;
			U = Value;
		}
		else if (OwnerTypeName.size() >= 8
			&& (OwnerTypeName.compare(0, 7, "handle<") == 0
				|| OwnerTypeName.compare(0, 7, "shared<") == 0)
			&& OwnerTypeName.back() == '>')
		{
			T = std::string(OwnerTypeName.substr(7, OwnerTypeName.size() - 8));
		}
		else if (IsDelegateTypeName(OwnerTypeName)
			|| IsMulticastDelegateTypeName(OwnerTypeName))
		{
			return SubstituteDelegatePlaceholders(OwnerTypeName, DetailTemplate);
		}
		else
		{
			std::string GenericBase;
			std::vector<std::string> GenericArgs;
			const bool bGeneric = SplitTypeApp(OwnerTypeName, GenericBase, GenericArgs);
			const std::string_view Leaf = StripTypeDecorations(GenericBase);
			if (bGeneric
				&& GenericArgs.size() == 1
				&& (Leaf == "field_info" || Leaf == "method_info"
					|| Leaf == "task" || Leaf == "task_group"
					|| Leaf == "optional" || Leaf == "readonly" || Leaf == "weak" || Leaf == "type"
					|| Leaf == "handle" || Leaf == "shared"))
			{
				T = std::move(GenericArgs.front());
			}
			else
			{
				return std::string(DetailTemplate);
			}
		}

		std::string Out;
		Out.reserve(DetailTemplate.size() + 8);
		for (std::size_t Index = 0; Index < DetailTemplate.size(); ++Index)
		{
			const char Char = DetailTemplate[Index];
			const auto IsIdentChar = [](const char C)
			{
				return std::isalnum(static_cast<unsigned char>(C)) != 0 || C == '_';
			};
			const bool bWordStart = Index == 0 || !IsIdentChar(DetailTemplate[Index - 1]);
			const bool bWordEnd = Index + 1 >= DetailTemplate.size()
				|| !IsIdentChar(DetailTemplate[Index + 1]);
			if (bWordStart && bWordEnd && Char == 'T')
			{
				Out += T.empty() ? "T" : T;
				continue;
			}
			if (bWordStart && bWordEnd && Char == 'U')
			{
				Out += U.empty() ? "U" : U;
				continue;
			}
			if (bWordStart && bWordEnd && Char == 'K')
			{
				Out += Key.empty() ? (T.empty() ? "K" : T) : Key;
				continue;
			}
			if (bWordStart && bWordEnd && Char == 'V')
			{
				Out += Value.empty() ? (U.empty() ? "V" : U) : Value;
				continue;
			}
			Out.push_back(Char);
		}
		return Out;
	}

	void AppendStandardNamespaceSymbols(std::vector<FDocumentSymbol>& OutSymbols)
	{
		for (const char* NamespaceName : {"std", "std.memory", "std.chrono", "std.io", "std.filesystem",
			"std.strings", "std.concurrent", "std.algorithm", "std.numeric", "std.random",
			"std.environment", "std.this_thread", "std.console", "std.process"})
		{
			CheckAnalysisCancellation();
			const FStandardNamespaceMetadata* const Metadata = FindStandardNamespaceMetadata(NamespaceName);
			if (!Metadata)
			{
				continue;
			}
			FDocumentSymbol StandardNamespace;
			StandardNamespace.Kind = EDocumentSymbolKind::Namespace;
			StandardNamespace.Name.assign(Metadata->QualifiedName);
			StandardNamespace.Detail = FormatStandardNamespaceDetail(*Metadata);
			StandardNamespace.Documentation = FormatStandardNamespaceDocumentation(*Metadata);
			StandardNamespace.bExported = true;
			MarkSoft(StandardNamespace);
			OutSymbols.push_back(std::move(StandardNamespace));
		}
	}

	void AppendSoftBuiltinSymbols(
		const FProgram& Program,
		std::vector<FDocumentSymbol>& OutSymbols)
	{
		if (!ProgramHasUserPairArity2(Program))
		{
			FDocumentSymbol Pair;
			Pair.Kind = EDocumentSymbolKind::Struct;
			Pair.Name = "pair<T, U>";
			Pair.Detail = "soft builtin";
			Pair.bExported = true;
			Pair.Children.push_back(MakeField("first", "T"));
			Pair.Children.push_back(MakeField("second", "U"));
			Pair.Children.push_back(MakeCtor("pair", "()"));
			Pair.Children.push_back(MakeCtor("pair", "(T first, U second)"));
			MarkSoft(Pair);
			OutSymbols.push_back(std::move(Pair));
		}

		if (!ProgramHasUserHandleArity1(Program))
		{
			FDocumentSymbol HandleSymbol;
			HandleSymbol.Kind = EDocumentSymbolKind::Class;
			HandleSymbol.Name = "handle<T>";
			HandleSymbol.Detail = "soft builtin";
			HandleSymbol.bExported = true;
			HandleSymbol.Children.push_back(MakeField("value", "T"));
			HandleSymbol.Children.push_back(MakeCtor("handle", "()"));
			HandleSymbol.Children.push_back(MakeCtor("handle", "(T v)"));
			MarkSoft(HandleSymbol);
			OutSymbols.push_back(std::move(HandleSymbol));
		}

		if (!ProgramHasUserTypeArity(Program, "optional", 1))
		{
			FDocumentSymbol Optional;
			Optional.Kind = EDocumentSymbolKind::Struct;
			Optional.Name = "optional<T>";
			Optional.Detail = "soft builtin value type";
			Optional.bExported = true;
			Optional.Children.push_back(MakeCtor("optional", "()"));
			Optional.Children.push_back(MakeCtor("optional", "(T value)"));
			Optional.Children.push_back(MakeMethodSymbol("has_value", "bool () const"));
			Optional.Children.push_back(MakeMethodSymbol("get", "T () const"));
			Optional.Children.push_back(MakeMethodSymbol("value_or", "T (T fallback) const"));
			Optional.Children.push_back(MakeMethodSymbol("reset", "void ()"));
			MarkSoft(Optional);
			OutSymbols.push_back(std::move(Optional));
		}

		if (!ProgramHasUserTypeArity(Program, "readonly", 1))
		{
			FDocumentSymbol Readonly;
			Readonly.Kind = EDocumentSymbolKind::Struct;
			Readonly.Name = "readonly<T>";
			Readonly.Detail = "soft builtin read-only value wrapper";
			Readonly.bExported = true;
			Readonly.Children.push_back(MakeCtor("readonly", "(T value)"));
			Readonly.Children.push_back(MakeMethodSymbol("get", "const T& () const"));
			MarkSoft(Readonly);
			OutSymbols.push_back(std::move(Readonly));
		}

		if (!ProgramHasUserTypeArity(Program, "module_info", 0))
		{
			FDocumentSymbol ModuleInfo;
			ModuleInfo.Kind = EDocumentSymbolKind::Struct;
			ModuleInfo.Name = "module_info";
			ModuleInfo.Detail = "soft builtin immutable module reflection descriptor";
			ModuleInfo.bExported = true;
			ModuleInfo.Children.push_back(MakeCtor("module_info", "()"));
			ModuleInfo.Children.push_back(MakeMethodSymbol("path", "string () const"));
			ModuleInfo.Children.push_back(MakeMethodSymbol("function_count", "int () const"));
			ModuleInfo.Children.push_back(MakeMethodSymbol("global_count", "int () const"));
			ModuleInfo.Children.push_back(MakeMethodSymbol("type_count", "int () const"));
			ModuleInfo.Children.push_back(MakeMethodSymbol("type_name", "string (int index) const"));
			MarkSoft(ModuleInfo);
			OutSymbols.push_back(std::move(ModuleInfo));
		}

		if (!ProgramHasUserTypeArity(Program, "function_info", 0))
		{
			FDocumentSymbol FunctionInfo;
			FunctionInfo.Kind = EDocumentSymbolKind::Struct;
			FunctionInfo.Name = "function_info";
			FunctionInfo.Detail = "soft builtin immutable free-function reflection descriptor";
			FunctionInfo.bExported = true;
			FunctionInfo.Children.push_back(MakeCtor("function_info", "(int index)"));
			for (const auto& [Name, Detail] : std::initializer_list<std::pair<const char*, const char*>>{
					 {"name", "string () const"}, {"linkage_name", "string () const"},
					 {"module_path", "string () const"}, {"return_type", "string () const"},
					 {"parameter_count", "int () const"}, {"parameter_type", "string (int index) const"},
					 {"parameter_name", "string (int index) const"}, {"parameter_is_ref", "bool (int index) const"},
					 {"parameter_is_const", "bool (int index) const"}, {"is_deleted", "bool () const"},
					 {"is_exported", "bool () const"}, {"metadata_count", "int () const"},
					 {"metadata_key", "string (int index) const"}, {"metadata_value", "string (int index) const"},
					 {"has_metadata", "bool (string key) const"}, {"metadata", "string (string key) const"},
					 {"has_annotation", "bool (string name) const"}})
			{
				FunctionInfo.Children.push_back(MakeMethodSymbol(Name, Detail));
			}
			MarkSoft(FunctionInfo);
			OutSymbols.push_back(std::move(FunctionInfo));
		}

		if (!ProgramHasUserTypeArity(Program, "global_info", 0))
		{
			FDocumentSymbol GlobalInfo;
			GlobalInfo.Kind = EDocumentSymbolKind::Struct;
			GlobalInfo.Name = "global_info";
			GlobalInfo.Detail = "soft builtin immutable global-variable reflection descriptor";
			GlobalInfo.bExported = true;
			GlobalInfo.Children.push_back(MakeCtor("global_info", "(int index)"));
			for (const auto& [Name, Detail] : std::initializer_list<std::pair<const char*, const char*>>{
					 {"name", "string () const"}, {"linkage_name", "string () const"},
					 {"module_path", "string () const"}, {"declared_type", "string () const"},
					 {"runtime_type", "string () const"}, {"is_const", "bool () const"},
					 {"is_exported", "bool () const"}, {"metadata_count", "int () const"},
					 {"metadata_key", "string (int index) const"}, {"metadata_value", "string (int index) const"},
					 {"has_metadata", "bool (string key) const"}, {"metadata", "string (string key) const"},
					 {"has_annotation", "bool (string name) const"}})
			{
				GlobalInfo.Children.push_back(MakeMethodSymbol(Name, Detail));
			}
			MarkSoft(GlobalInfo);
			OutSymbols.push_back(std::move(GlobalInfo));
		}

		if (!ProgramHasUserTypeArity(Program, "enum_member_info", 1))
		{
			FDocumentSymbol EnumMemberInfo;
			EnumMemberInfo.Kind = EDocumentSymbolKind::Struct;
			EnumMemberInfo.Name = "enum_member_info<T>";
			EnumMemberInfo.Detail = "soft builtin immutable enum-member reflection descriptor";
			EnumMemberInfo.bExported = true;
			EnumMemberInfo.Children.push_back(MakeCtor("enum_member_info", "(string owner, int index)"));
			for (const auto& [Name, Detail] : std::initializer_list<std::pair<const char*, const char*>>{
					 {"name", "string () const"}, {"value", "int64 () const"},
					 {"metadata_count", "int () const"}, {"metadata_key", "string (int index) const"},
					 {"metadata_value", "string (int index) const"}, {"has_metadata", "bool (string key) const"},
					 {"metadata", "string (string key) const"}, {"has_annotation", "bool (string name) const"}})
			{
				EnumMemberInfo.Children.push_back(MakeMethodSymbol(Name, Detail));
			}
			MarkSoft(EnumMemberInfo);
			OutSymbols.push_back(std::move(EnumMemberInfo));
		}

		if (!ProgramHasUserTypeArity(Program, "type", 1))
		{
			FDocumentSymbol Type;
			Type.Kind = EDocumentSymbolKind::Struct;
			Type.Name = "type<T>";
			Type.Detail = "soft builtin strongly typed type token";
			Type.bExported = true;
			Type.Children.push_back(MakeField("name", "string"));
			Type.Children.push_back(MakeField("qualified_name", "string"));
			Type.Children.push_back(MakeCtor("type", "()"));
			Type.Children.push_back(MakeMethodSymbol("kind", "string () const"));
			Type.Children.push_back(MakeMethodSymbol("is_abstract", "bool () const"));
			Type.Children.push_back(MakeMethodSymbol("base_count", "int () const"));
			Type.Children.push_back(MakeMethodSymbol("base_name", "string (int index) const"));
			Type.Children.push_back(MakeMethodSymbol("field_count", "int () const"));
			Type.Children.push_back(MakeMethodSymbol("method_count", "int () const"));
			Type.Children.push_back(MakeMethodSymbol("metadata_count", "int () const"));
			Type.Children.push_back(MakeMethodSymbol("metadata_key", "string (int index) const"));
			Type.Children.push_back(MakeMethodSymbol("metadata_value", "string (int index) const"));
			Type.Children.push_back(MakeMethodSymbol("has_metadata", "bool (string key) const"));
			Type.Children.push_back(MakeMethodSymbol("metadata", "string (string key) const"));
			Type.Children.push_back(MakeMethodSymbol("has_annotation", "bool (string name) const"));
			Type.Children.push_back(MakeMethodSymbol("enum_member_count", "int () const"));
			Type.Children.push_back(MakeMethodSymbol("enum_member_name", "string (int index) const"));
			Type.Children.push_back(MakeMethodSymbol("enum_member_value", "int64 (int index) const"));
			Type.Children.push_back(MakeMethodSymbol("enum_underlying_type", "string () const"));
			Type.Children.push_back(MakeMethodSymbol("is_scoped_enum", "bool () const"));
			MarkSoft(Type);
			OutSymbols.push_back(std::move(Type));
		}

		if (!ProgramHasUserTypeArity(Program, "field_info", 1))
		{
			FDocumentSymbol FieldInfo;
			FieldInfo.Kind = EDocumentSymbolKind::Struct;
			FieldInfo.Name = "field_info<T>";
			FieldInfo.Detail = "soft builtin immutable field reflection descriptor";
			FieldInfo.bExported = true;
			FieldInfo.Children.push_back(MakeCtor("field_info", "(string owner, int index)"));
			FieldInfo.Children.push_back(MakeMethodSymbol("name", "string () const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("declared_type", "string () const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("runtime_type", "string () const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("is_static", "bool () const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("is_mutable", "bool () const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("is_declared_on_type", "bool () const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("get", "V get<V>(T receiver) const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("set", "void set<V>(T receiver, V value) const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("metadata_count", "int () const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("metadata_key", "string (int index) const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("metadata_value", "string (int index) const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("has_metadata", "bool (string key) const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("metadata", "string (string key) const"));
			FieldInfo.Children.push_back(MakeMethodSymbol("has_annotation", "bool (string name) const"));
			MarkSoft(FieldInfo);
			OutSymbols.push_back(std::move(FieldInfo));
		}

		if (!ProgramHasUserTypeArity(Program, "method_info", 1))
		{
			FDocumentSymbol MethodInfo;
			MethodInfo.Kind = EDocumentSymbolKind::Struct;
			MethodInfo.Name = "method_info<T>";
			MethodInfo.Detail = "soft builtin immutable method reflection descriptor";
			MethodInfo.bExported = true;
			MethodInfo.Children.push_back(MakeCtor("method_info", "(string owner, int index)"));
			for (const auto& [Name, Detail] : std::initializer_list<std::pair<const char*, const char*>>{
					 {"name", "string () const"}, {"return_type", "string () const"},
					 {"parameter_count", "int () const"}, {"parameter_type", "string (int index) const"},
					 {"parameter_name", "string (int index) const"}, {"parameter_is_ref", "bool (int index) const"},
					 {"parameter_is_const", "bool (int index) const"}, {"is_static", "bool () const"},
					 {"is_virtual", "bool () const"}, {"is_abstract", "bool () const"},
					 {"is_const", "bool () const"}, {"is_deleted", "bool () const"},
					 {"is_constructor", "bool () const"}, {"is_destructor", "bool () const"},
					 {"metadata_count", "int () const"}, {"metadata_key", "string (int index) const"},
					 {"metadata_value", "string (int index) const"},
					 {"has_metadata", "bool (string key) const"}, {"metadata", "string (string key) const"},
					 {"has_annotation", "bool (string name) const"}})
			{
				MethodInfo.Children.push_back(MakeMethodSymbol(Name, Detail));
			}
			MethodInfo.Children.push_back(MakeMethodSymbol("invoke", "R invoke<R>(T receiver, args...) const"));
			MethodInfo.Children.push_back(MakeMethodSymbol("invoke_static", "R invoke_static<R>(args...) const"));
			MarkSoft(MethodInfo);
			OutSymbols.push_back(std::move(MethodInfo));
		}

		if (!ProgramHasUserTypeArity(Program, "weak", 1))
		{
			FDocumentSymbol Weak;
			Weak.Kind = EDocumentSymbolKind::Struct;
			Weak.Name = "weak<T>";
			Weak.Detail = "soft builtin non-owning reference value";
			Weak.bExported = true;
			Weak.Children.push_back(MakeCtor("weak", "()"));
			Weak.Children.push_back(MakeCtor("weak", "(T target)"));
			Weak.Children.push_back(MakeMethodSymbol("lock", "T () const"));
			Weak.Children.push_back(MakeMethodSymbol("expired", "bool () const"));
			Weak.Children.push_back(MakeMethodSymbol("reset", "void ()"));
			MarkSoft(Weak);
			OutSymbols.push_back(std::move(Weak));
		}

		if (!ProgramHasUserTypeArity(Program, "task", 1))
		{
			FDocumentSymbol Task;
			Task.Kind = EDocumentSymbolKind::Class;
			Task.Name = "task<T>";
			Task.Detail = "concurrent.vs typed asynchronous result";
			Task.bExported = true;
			Task.Children.push_back(MakeCtor("task", "(int id, channel<T> output)"));
			Task.Children.push_back(MakeMethodSymbol("valid", "bool () const"));
			Task.Children.push_back(MakeMethodSymbol("done", "bool () const"));
			Task.Children.push_back(MakeMethodSymbol("await", "T ()"));
			Task.Children.push_back(MakeMethodSymbol("await_for", "optional<T> (int timeout_ms)"));
			Task.Children.push_back(MakeMethodSymbol("cancel", "void ()"));
			MarkSoft(Task);
			OutSymbols.push_back(std::move(Task));
		}

		if (!ProgramHasUserTypeArity(Program, "task_group", 1))
		{
			FDocumentSymbol TaskGroup;
			TaskGroup.Kind = EDocumentSymbolKind::Class;
			TaskGroup.Name = "task_group<T>";
			TaskGroup.Detail = "concurrent.vs scoped homogeneous task collection";
			TaskGroup.bExported = true;
			TaskGroup.Children.push_back(MakeCtor("task_group", "()"));
			TaskGroup.Children.push_back(MakeMethodSymbol("add", "void (task<T> work)"));
			TaskGroup.Children.push_back(MakeMethodSymbol("size", "int64 () const"));
			TaskGroup.Children.push_back(MakeMethodSymbol("await_all", "array<T> ()"));
			TaskGroup.Children.push_back(MakeMethodSymbol("cancel_all", "void ()"));
			MarkSoft(TaskGroup);
			OutSymbols.push_back(std::move(TaskGroup));
		}

		for (const std::pair<const char*, const char*> Alias : {
				 std::pair<const char*, const char*>{"mutex", "channel<bool>"},
				std::pair<const char*, const char*>{"counting_semaphore", "channel<bool>"}})
		{
			if (ProgramHasUserTypeArity(Program, Alias.first, 0))
			{
				continue;
			}
			FDocumentSymbol SynchronizationType;
			SynchronizationType.Kind = EDocumentSymbolKind::TypeAlias;
			SynchronizationType.Name = Alias.first;
			SynchronizationType.Detail =
				std::string("concurrent.vs synchronized capability alias of ")
				+ Alias.second;
			SynchronizationType.bExported = true;
			MarkSoft(SynchronizationType);
			OutSymbols.push_back(std::move(SynchronizationType));
		}

		if (!ProgramHasUserTypeNamed(Program, "delegate"))
		{
			FDocumentSymbol Delegate;
			Delegate.Kind = EDocumentSymbolKind::Class;
			Delegate.Name = "delegate";
			Delegate.bArityOverloadedEventType = true;
			Delegate.Detail = "soft builtin (delegate.vs; delegate<R> / <R, A> / <R, A, B> / <R, A, B, C>)";
			Delegate.bExported = true;
			// Methods come from AppendSoftContainerMembers (arity-specialized).
			Delegate.Children.push_back(MakeCtor("delegate", "()"));
			MarkSoft(Delegate);
			OutSymbols.push_back(std::move(Delegate));
		}

		if (!ProgramHasUserTypeNamed(Program, "multicast_delegate"))
		{
			FDocumentSymbol Multicast;
			Multicast.Kind = EDocumentSymbolKind::Class;
			Multicast.Name = "multicast_delegate";
			Multicast.bArityOverloadedEventType = true;
			Multicast.Detail = "soft builtin (delegate.vs; multicast_delegate / <A> / <A, B> / <A, B, C>)";
			Multicast.bExported = true;
			// Methods come from AppendSoftContainerMembers (arity-specialized).
			Multicast.Children.push_back(MakeCtor("multicast_delegate", "()"));
			MarkSoft(Multicast);
			OutSymbols.push_back(std::move(Multicast));
		}

		// One symbol identity and one completion candidate. Show the overloads
		// as separate source-facing signatures instead of a `|` joined detail.
		FDocumentSymbol VariantType;
		VariantType.Kind = EDocumentSymbolKind::Struct;
		VariantType.Name = "variant";
		VariantType.Detail = "builtin dynamic value box; extract using cast<T> or try_cast<T>";
		VariantType.bExported = true;
		VariantType.Children.push_back(MakeCtor("variant", "()"));
		VariantType.Children.push_back(MakeCtor("variant", "(T value)"));
		MarkSoft(VariantType);
		OutSymbols.push_back(std::move(VariantType));
		AddFunctionSymbol(Program, OutSymbols, "variant_has_value", "bool (variant value)");
		AddFunctionSymbol(Program, OutSymbols, "variant_type_name", "string (variant value)");
		AddFunctionSymbol(Program, OutSymbols, "variant_is", "bool variant_is<T>(variant value)");
		AddFunctionSymbol(Program, OutSymbols, "print",
			"void print(any value);\nvoid print(string fmt, ...);");
		AddFunctionSymbol(Program, OutSymbols, "eprint", "void (any value)");
		AddFunctionSymbol(Program, OutSymbols, "format", "string (string fmt, ...)");
		AddFunctionSymbol(Program, OutSymbols, "printf", "void (string fmt, ...)");
		AddFunctionSymbol(Program, OutSymbols, "formatf", "string (string fmt, ...)");
		AddFunctionSymbol(Program, OutSymbols, "len", "int64 (any value)");
		AddFunctionSymbol(
			Program,
			OutSymbols,
			"assert",
			"void (bool condition, string message = \"\")");
		AddFunctionSymbol(
			Program,
			OutSymbols,
			"cast",
			"T cast<T> (any value)",
			"Explicit checked conversion. A failed runtime reference or numeric conversion reports an error.");
		AddFunctionSymbol(
			Program,
			OutSymbols,
			"try_cast",
			"optional<T> try_cast<T> (any value)",
			"Non-throwing checked conversion; an incompatible value returns an empty optional<T>.");
		AddFunctionSymbol(
			Program,
			OutSymbols,
			"const_cast",
			"T const_cast<T> (const T value)",
			"Explicitly removes the compile-time const access capability without changing the value type.");
		AddFunctionSymbol(Program, OutSymbols, "to_string", "string (any value)");
		AddFunctionSymbol(Program, OutSymbols, "to_int", "int (any value)");
		AddFunctionSymbol(Program, OutSymbols, "to_int64", "int64 (any value)");
		AddFunctionSymbol(Program, OutSymbols, "to_float", "float (any value)");
		AddFunctionSymbol(Program, OutSymbols, "to_double", "double (any value)");
		AddFunctionSymbol(Program, OutSymbols, "to_unsigned", "unsigned (any value)");
		AddFunctionSymbol(Program, OutSymbols, "to_uint64", "uint64 (any value)");
		AddFunctionSymbol(Program, OutSymbols, "to_wstring", "wstring (any value)");
		AddFunctionSymbol(Program, OutSymbols, "abs", "number (number value)");
		AddFunctionSymbol(Program, OutSymbols, "min", "number (number a, number b)");
		AddFunctionSymbol(Program, OutSymbols, "max", "number (number a, number b)");
		AddFunctionSymbol(Program, OutSymbols, "range", "array<int> (int64 n)");
		AddFunctionSymbol(Program, OutSymbols, "sleep", "void (int ms)");
		AddFunctionSymbol(Program, OutSymbols, "exit", "void (int code)");
		AddFunctionSymbol(Program, OutSymbols, "getenv", "string (string name)");
		AddFunctionSymbol(Program, OutSymbols, "setenv", "void (string name, string value)");
		AddFunctionSymbol(Program, OutSymbols, "now", "double ()");
		AddFunctionSymbol(Program, OutSymbols, "steady_now", "double ()");
		AddFunctionSymbol(Program, OutSymbols, "console_write", "void (any value)",
			"Write one value to standard output without adding a newline. Use std::console::flush() to show pending output.");
		AddFunctionSymbol(Program, OutSymbols, "console_error", "void (any value)",
			"Write one value to standard error without adding a newline.");
		AddFunctionSymbol(Program, OutSymbols, "console_flush", "void ()",
			"Flush pending standard output to the host, including newline-free progress text.");
		AddFunctionSymbol(Program, OutSymbols, "console_clear", "bool ()",
			"Clear the visible output window and move the cursor to its origin; scrollback is retained. "
			"Return false when unsupported or unsuccessful. Plain text sinks receive no terminal control codes.");
		AddFunctionSymbol(Program, OutSymbols, "console_is_interactive", "bool ()",
			"Query whether both standard input and standard output are interactive. This query has no side effects.");
		AddFunctionSymbol(Program, OutSymbols, "console_supports_color", "bool ()",
			"Query whether output supports color without changing terminal state. Interactive input is not required.");
		AddFunctionSymbol(Program, OutSymbols, "console_supports_cursor", "bool ()",
			"Query whether output supports cursor control without changing terminal state. Interactive input is not required.");
		AddFunctionSymbol(Program, OutSymbols, "console_supports_clear", "bool ()",
			"Query whether output supports clearing the window or line without changing terminal state. Interactive input is not required.");
		AddFunctionSymbol(Program, OutSymbols, "console_is_output_terminal", "bool ()",
			"Query whether standard output is a terminal; standard input may be redirected. This query has no side effects.");
		AddFunctionSymbol(Program, OutSymbols, "console_set_color", "bool (string foreground, string background)",
			"Set both foreground and background colors. Names are black, red, green, yellow, blue, magenta, cyan, white, "
			"their bright_ variants, or default. Return false when unsupported or unsuccessful; invalid names cause an error "
			"even on plain text sinks. Modified style is restored at the execution boundary.");
		AddFunctionSymbol(Program, OutSymbols, "console_reset_style", "bool ()",
			"Reset terminal style. Return false when unsupported or unsuccessful; plain text sinks receive no terminal control codes.");
		AddFunctionSymbol(Program, OutSymbols, "console_clear_line", "bool (string mode)",
			"Clear after, before, or all of the current line, including the cursor character, without moving the cursor. "
			"Return false when unsupported or unsuccessful; invalid mode names cause an error even on plain text sinks.");
		AddFunctionSymbol(Program, OutSymbols, "console_move_cursor", "bool (int column, int row)",
			"Move to a zero-based column and row in the visible window. Coordinates must be integers in 0..32766; "
			"invalid types or ranges cause an error. Return false outside known window dimensions, when unsupported, or on failure.");
		AddFunctionSymbol(Program, OutSymbols, "console_set_cursor_visible", "bool (bool visible)",
			"Show or hide the terminal cursor. Return false when unsupported or unsuccessful. "
			"Modified cursor visibility is restored at the execution boundary.");
		AddFunctionSymbol(Program, OutSymbols, "console_width", "int ()",
			"Query the visible output window width in columns; return 0 when unknown. This query has no side effects.");
		AddFunctionSymbol(Program, OutSymbols, "console_height", "int ()",
			"Query the visible output window height in rows; return 0 when unknown. This query has no side effects.");
		AddFunctionSymbol(Program, OutSymbols, "__std_read_line", "string ()");
		AddFunctionSymbol(
			Program,
			OutSymbols,
			"__std_pause",
			"void ()",
			"Wait until one input line is submitted, then discard it. "
			"End-of-file returns immediately.");
		AddFunctionSymbol(Program, OutSymbols, "__std_read_int", "int ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_read_double", "double ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_read_string", "string ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_read_bool", "bool ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_read_token", "string ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_read_file", "string (string path)");
		AddFunctionSymbol(Program, OutSymbols, "__std_try_read_line", "optional<string> ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_try_read_token", "optional<string> ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_read_bytes", "std::memory::buffer (string path, int64 max_bytes)", "Omit max_bytes for host limits; zero permits only an empty file.");
		for (const char* Name : {"__std_write_bytes", "__std_append_bytes"})
		{
			AddFunctionSymbol(Program, OutSymbols, Name, "void (string path, std::memory::buffer source)", "Also accepts memory view and readonly_view.");
		}
		AddFunctionSymbol(Program, OutSymbols, "try_getenv", "optional<string> (string name)");
		AddFunctionSymbol(Program, OutSymbols, "unsetenv", "void (string name)");
		AddFunctionSymbol(Program, OutSymbols, "__std_write_file", "void (string path, string content)");
		AddFunctionSymbol(Program, OutSymbols, "__std_append_file", "void (string path, string content)");
		AddFunctionSymbol(Program, OutSymbols, "__std_delete_file", "bool (string path)");
		AddFunctionSymbol(Program, OutSymbols, "__std_exists", "bool (string path)");
		AddFunctionSymbol(Program, OutSymbols, "__std_is_file", "bool (string path)");
		AddFunctionSymbol(Program, OutSymbols, "__std_is_directory", "bool (string path)");
		AddFunctionSymbol(Program, OutSymbols, "__std_remove_dir", "bool (string path)");
		AddFunctionSymbol(Program, OutSymbols, "__std_list_dir", "array<string> (string path)");
		AddFunctionSymbol(Program, OutSymbols, "__std_mkdir", "void (string path)");
		AddFunctionSymbol(Program, OutSymbols, "__std_cwd", "string ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_chdir", "void (string path)");

		for (const char* Name : {
				 "sqrt", "sin", "cos", "tan", "floor", "ceil", "round", "trunc",
				"exp", "log", "log2", "asin", "acos"})
		{
			const std::string InternalName = std::string("__std_") + Name;
			AddFunctionSymbol(Program, OutSymbols, InternalName.c_str(), "double (double value)");
		}
		AddFunctionSymbol(Program, OutSymbols, "__std_pow", "double (double base, double exponent)");
		AddFunctionSymbol(Program, OutSymbols, "__std_atan2", "double (double y, double x)");
		AddFunctionSymbol(Program, OutSymbols, "__std_isfinite", "bool (double value)");
		AddFunctionSymbol(Program, OutSymbols, "__std_isnan", "bool (double value)");
		AddFunctionSymbol(Program, OutSymbols, "__std_pi", "double ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_e", "double ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_clamp", "T (T value, T low, T high)");
		AddFunctionSymbol(Program, OutSymbols, "__std_clamp_int", "int (int value, int low, int high)");

		AddFunctionSymbol(Program, OutSymbols, "__std_substring", "string (string value, int64 start, int64 count)");
		AddFunctionSymbol(Program, OutSymbols, "__std_find", "int64 (string value, string needle)");
		AddFunctionSymbol(Program, OutSymbols, "__std_char_code", "int (string value, int index)");
		for (const char* Name : {"contains", "starts_with", "ends_with"})
		{
			const std::string InternalName = std::string("__std_") + Name;
			AddFunctionSymbol(Program, OutSymbols, InternalName.c_str(), "bool (string value, string part)");
		}
		for (const char* Name : {"trim_left", "trim_right", "trim", "to_lower", "to_upper"})
		{
			const std::string InternalName = std::string("__std_") + Name;
			AddFunctionSymbol(Program, OutSymbols, InternalName.c_str(), "string (string value)");
		}
		AddFunctionSymbol(Program, OutSymbols, "__std_split", "array<string> (string value, string separator)");
		AddFunctionSymbol(Program, OutSymbols, "__std_join", "string (array<string> parts, string separator)");
		AddFunctionSymbol(Program, OutSymbols, "__std_replace", "string (string value, string from, string to)");
		AddFunctionSymbol(Program, OutSymbols, "__std_last_find", "int64 (string value, string needle)");
		AddFunctionSymbol(Program, OutSymbols, "__std_repeat", "string (string value, int64 count)");
		AddFunctionSymbol(Program, OutSymbols, "__std_parse<T>", "T (string text)", "Optional integer radix 2..36; parsing consumes the whole trimmed token.");
		AddFunctionSymbol(Program, OutSymbols, "__std_try_parse<T>", "bool (string text, ref T result)", "Optional integer radix 2..36; format/range failure preserves result.");
		AddFunctionSymbol(Program, OutSymbols, "__std_encode_utf8", "std::memory::buffer (string text)");
		AddFunctionSymbol(Program, OutSymbols, "__std_decode_utf8", "string (std::memory::buffer bytes)", "Also accepts memory view and readonly_view; invalid UTF-8 is an error.");
		AddFunctionSymbol(Program, OutSymbols, "__std_is_valid_utf8", "bool (string text)", "Also accepts memory buffer, view and readonly_view.");
		AddFunctionSymbol(Program, OutSymbols, "__std_remove_prefix", "string (string value, string prefix)");
		AddFunctionSymbol(Program, OutSymbols, "__std_remove_suffix", "string (string value, string suffix)");

		for (const char* Name : {"basename", "dirname", "extension"})
		{
			const std::string InternalName = std::string("__std_") + Name;
			AddFunctionSymbol(Program, OutSymbols, InternalName.c_str(), "string (string path)");
		}
		AddFunctionSymbol(Program, OutSymbols, "__std_join_path", "string (string left, string right)");
		AddFunctionSymbol(Program, OutSymbols, "__std_change_extension", "string (string path, string extension)");
		AddFunctionSymbol(Program, OutSymbols, "__std_path_is_absolute", "bool (string path, std::filesystem::path_style style = std::filesystem::path_style::native)");
		AddFunctionSymbol(Program, OutSymbols, "__std_path_root", "string (string path, std::filesystem::path_style style = std::filesystem::path_style::native)");
		AddFunctionSymbol(Program, OutSymbols, "__std_path_normalize", "string (string path, std::filesystem::path_style style = std::filesystem::path_style::native)");

		// concurrent.vs wrappers (when import not visible to the indexer).
		AddFunctionSymbol(Program, OutSymbols, "__std_channel_send", "void (channel<T> ch, T value)");
		AddFunctionSymbol(Program, OutSymbols, "__std_channel_recv", "T (channel<T> ch)");
		AddFunctionSymbol(Program, OutSymbols, "__std_channel_close", "void (channel<T> ch)");
		AddFunctionSymbol(Program, OutSymbols, "__std_make_channel", "channel<T> (int64 capacity = 0)");
		AddFunctionSymbol(Program, OutSymbols, "send", "void (channel<T> ch, T value)");
		AddFunctionSymbol(Program, OutSymbols, "recv", "T (channel<T> ch)");
		AddFunctionSymbol(Program, OutSymbols, "close", "void (channel<T> ch)");
		AddFunctionSymbol(Program, OutSymbols, "try_send", "bool (channel<T> ch, T value)");
		AddFunctionSymbol(Program, OutSymbols, "send_for", "bool (channel<T> ch, T value, int timeout_ms)");
		AddFunctionSymbol(Program, OutSymbols, "try_recv", "optional<T> (channel<T> ch)");
		AddFunctionSymbol(Program, OutSymbols, "recv_for", "optional<T> (channel<T> ch, int timeout_ms)");
		AddFunctionSymbol(Program, OutSymbols, "is_closed", "bool (channel<T> ch)");
		AddFunctionSymbol(Program, OutSymbols, "pending", "int (channel<T> ch)");
		AddFunctionSymbol(Program, OutSymbols, "capacity", "int64 (channel<T> ch)");
		// Soft fallback only; stdlib overloads are channel<int|string|bool|double>.
		AddFunctionSymbol(Program, OutSymbols, "__std_spawn", "int (string name, channel<T> ch)",
			"Also accepts a callable and zero or more transferable value arguments.");
		AddFunctionSymbol(Program, OutSymbols, "__std_spawn_task", "task<T> (string name)",
			"Also accepts a callable and zero or more transferable value arguments.");
		AddFunctionSymbol(Program, OutSymbols, "__std_join_task", "void (int task_id)");
		AddFunctionSymbol(Program, OutSymbols, "__std_cancel_task", "void (int task_id)");
		AddFunctionSymbol(Program, OutSymbols, "__std_cancel_requested", "bool ()");
		AddFunctionSymbol(Program, OutSymbols, "__std_when_all", "array<T> (array<task<T>> tasks)");
		AddFunctionSymbol(Program, OutSymbols, "__std_cancel_all", "void (array<task<T>> tasks)");
		AddFunctionSymbol(Program, OutSymbols, "__std_make_mutex", "mutex ()");
		AddFunctionSymbol(Program, OutSymbols, "mutex_lock", "void (mutex value)");
		AddFunctionSymbol(Program, OutSymbols, "mutex_try_lock", "bool (mutex value)");
		AddFunctionSymbol(Program, OutSymbols, "__std_mutex_lock_for", "bool (mutex value, int timeout_ms)");
		AddFunctionSymbol(Program, OutSymbols, "mutex_unlock", "void (mutex value)");
		AddFunctionSymbol(Program, OutSymbols, "__std_make_semaphore", "counting_semaphore (int initial, int maximum)");
		AddFunctionSymbol(Program, OutSymbols, "semaphore_acquire", "void (counting_semaphore value)");
		AddFunctionSymbol(Program, OutSymbols, "semaphore_try_acquire", "bool (counting_semaphore value)");
		AddFunctionSymbol(Program, OutSymbols, "__std_semaphore_acquire_for", "bool (counting_semaphore value, int timeout_ms)");
		AddFunctionSymbol(Program, OutSymbols, "semaphore_release", "void (counting_semaphore value)");
		AddFunctionSymbol(Program, OutSymbols, "__std_make_synchronized", "channel<T> (T initial)");
		AddFunctionSymbol(Program, OutSymbols, "synchronized_load", "T (channel<T> value)");
		AddFunctionSymbol(Program, OutSymbols, "synchronized_store", "void (channel<T> value, T desired)");
		AddFunctionSymbol(Program, OutSymbols, "synchronized_exchange", "T (channel<T> value, T desired)");
		AddFunctionSymbol(Program, OutSymbols, "synchronized_fetch_add", "T (channel<T> value, T delta)");

		// Source-backed algorithm/numeric/random modules. These fallbacks preserve
		// useful completion, hover, and signature help before the document has a
		// resolvable import; compilation still enforces the normal module import.
		AddFunctionSymbol(Program, OutSymbols, "algorithm_sort", "void (array<T> values)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_reverse", "void (array<T> values)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_find_index", "int64 (array<T> values, T value)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_count", "int64 (array<T> values, T value)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_lower_bound", "int64 (array<T> values, T value)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_binary_search", "bool (array<T> values, T value)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_fill", "void (array<T> values, T value)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_copy", "array<T> (array<T> values)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_contains", "bool (array<T> values, T value)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_equal", "bool (array<T> left, array<T> right)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_is_sorted", "bool (array<T> values)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_min_index", "int64 (array<T> values)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_max_index", "int64 (array<T> values)");
		AddFunctionSymbol(Program, OutSymbols, "__std_algorithm_rotate", "void (array<T> values, int64 middle)");
		AddFunctionSymbol(Program, OutSymbols, "algorithm_shuffle", "void (array<T> values, array<unsigned int> engine)");
		AddFunctionSymbol(Program, OutSymbols, "numeric_accumulate", "T (array<T> values, T initial)");
		AddFunctionSymbol(Program, OutSymbols, "numeric_sum", "T (array<T> values)");
		AddFunctionSymbol(Program, OutSymbols, "numeric_average", "double (array<T> values)");
		AddFunctionSymbol(Program, OutSymbols, "numeric_accumulate_as<R,T>", "R (array<T> values, R initial)");
		AddFunctionSymbol(Program, OutSymbols, "numeric_sum_as<R,T>", "R (array<T> values)");
		AddFunctionSymbol(Program, OutSymbols, "random_make_engine", "array<unsigned int> (unsigned int seed_value)");
		AddFunctionSymbol(Program, OutSymbols, "random_seed", "void (array<unsigned int> engine, unsigned int seed_value)");
		AddFunctionSymbol(Program, OutSymbols, "random_next_uint", "unsigned int (array<unsigned int> engine)");
		AddFunctionSymbol(Program, OutSymbols, "random_next_bounded_uint", "unsigned int (array<unsigned int> engine, unsigned int upper_exclusive)");
		AddFunctionSymbol(Program, OutSymbols, "random_next_int", "int (array<unsigned int> engine, int minimum, int maximum_exclusive)");
		AddFunctionSymbol(Program, OutSymbols, "random_next_double", "double (array<unsigned int> engine)");
		AddFunctionSymbol(Program, OutSymbols, "random_next_double_range", "double (array<unsigned int> engine, double minimum, double maximum_exclusive)");
		AddFunctionSymbol(Program, OutSymbols, "random_next_bool", "bool (array<unsigned int> engine)");

		AddFunctionSymbol(Program, OutSymbols, "__memory_available", "bool ()");
		AddFunctionSymbol(Program, OutSymbols, "__memory_address", "uint64 (ref T value)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_object_id", "uint64 (T object)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_object_address", "uint64 (T object)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_data_address", "uint64 (region bytes)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_copy",
			"void (region destination, int64 offset, region source, int64 source_offset, int64 count)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_fill",
			"void (region destination, int64 offset, int64 count, uint8 value)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_equal",
			"bool (region left, int64 offset, region right, int64 right_offset, int64 count)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_clone", "std::memory::buffer (region bytes)");
		AddFunctionSymbol(
			Program, OutSymbols, "__memory_from_array", "std::memory::buffer (array<uint8> bytes)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_to_array", "array<uint8> (region bytes)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_read<T>",
			"T (region bytes, int64 offset, std::memory::byte_order order = "
			"std::memory::byte_order::little)");
		AddFunctionSymbol(Program, OutSymbols, "__memory_write<T>",
			"void (region bytes, int64 offset, T value, std::memory::byte_order order = "
			"std::memory::byte_order::little)");

		// The script-level standard namespace is a zero-cost spelling layer over
		// canonical builtins and imported stdlib declarations. Publish qualified
		// soft symbols so completion/hover sees exactly the same surface as the
		// parser, without inventing a second runtime type or function identity.
		// Namespace entries are first-class symbols rather than synthetic import
		// entries.  Keep one tooling-owned metadata record per path so hover,
		// completion, semantic tokens, and navigation agree on their identity.
		AppendStandardNamespaceSymbols(OutSymbols);

		const auto FindCanonicalSymbol = [&OutSymbols](
			const std::string_view CanonicalName,
			const EDocumentSymbolKind WantedKind) -> const FDocumentSymbol*
		{
			for (const FDocumentSymbol& Symbol : OutSymbols)
			{
				if (Symbol.Kind != WantedKind)
				{
					continue;
				}
				const std::size_t Generic = Symbol.Name.find('<');
				const std::size_t BaseLength =
					Generic == std::string::npos ? Symbol.Name.size() : Generic;
				const std::string_view Base(Symbol.Name.data(), BaseLength);
				if (Base == CanonicalName)
				{
					return &Symbol;
				}
			}
			return nullptr;
		};
		const auto FormatQualifiedStandardFunctionDetail = [](
			const std::string_view QualifiedName,
			const std::string_view CanonicalName,
			const std::string_view CanonicalDetail)
		{
			const std::string DisplayName = FormatNamespaceQualifiedName(QualifiedName);
			const auto Trim = [](const std::string_view Text)
			{
				const std::size_t Begin = Text.find_first_not_of(" \t\r");
				return Begin == std::string_view::npos ? std::string_view{}
					: Text.substr(Begin, Text.find_last_not_of(" \t\r") - Begin + 1);
			};
			const std::size_t Generic = CanonicalName.find('<');
			const std::string_view BaseName = CanonicalName.substr(0, Generic);
			const std::string_view TypeParams = Generic == std::string_view::npos
				? std::string_view{} : CanonicalName.substr(Generic);
			std::vector<std::string> Lines;
			for (std::size_t Begin = 0; Begin < CanonicalDetail.size();)
			{
				const std::size_t End = CanonicalDetail.find('\n', Begin);
				std::string_view Line = Trim(CanonicalDetail.substr(Begin,
					End == std::string_view::npos ? CanonicalDetail.size() - Begin : End - Begin));
				Begin = End == std::string_view::npos ? CanonicalDetail.size() : End + 1;
				while (!Line.empty() && Line.back() == ';')
				{
					Line = Trim(Line.substr(0, Line.size() - 1));
				}
				if (Line.empty())
				{
					continue;
				}

				VeroScriptLexer Lexer(Line, "<standard-signature>");
				const std::vector<FToken>& Tokens = Lexer.Tokenize();
				std::size_t OpenParen = std::string_view::npos;
				std::size_t NameIndex = 0;
				int ParenDepth = 0;
				int AngleDepth = 0;
				int BracketDepth = 0;
				for (std::size_t Index = 0; !Lexer.HasError() && Index < Tokens.size(); ++Index)
				{
					const std::string_view Token = Tokens[Index].Lexeme;
					if (Token == "(")
					{
						if (Index > 0 && ParenDepth == 0 && AngleDepth == 0 && BracketDepth == 0)
						{
							NameIndex = Index - 1;
							// A named generic signature may end in f<T> before '('.
							if (Tokens[NameIndex].Lexeme == ">" || Tokens[NameIndex].Lexeme == ">>")
							{
								int Depth = Tokens[NameIndex].Lexeme == ">>" ? 2 : 1;
								while (NameIndex > 0 && Depth > 0)
								{
									const std::string_view Previous = Tokens[--NameIndex].Lexeme;
									if (Previous == ">")
									{
										++Depth;
									}
									else if (Previous == ">>")
									{
										Depth += 2;
									}
									else if (Previous == "<")
									{
										--Depth;
									}
								}
								if (NameIndex > 0)
								{
									--NameIndex;
								}
							}
							// Parentheses in a callable return type are not parameters
							// of the standard function itself.
							if (Tokens[NameIndex].Lexeme != "fn" && Tokens[NameIndex].Lexeme != "function")
							{
								OpenParen = Tokens[Index].Location.Column - 1;
								break;
							}
						}
						++ParenDepth;
					}
					else if (Token == ")")
					{
						--ParenDepth;
					}
					else if (Token == "<")
					{
						++AngleDepth;
					}
					else if (Token == ">")
					{
						--AngleDepth;
					}
					else if (Token == ">>")
					{
						AngleDepth -= 2;
					}
					else if (Token == "[")
					{
						++BracketDepth;
					}
					else if (Token == "]")
					{
						--BracketDepth;
					}
				}
				std::string Formatted(Line);
				if (OpenParen != std::string_view::npos)
				{
					std::string_view Prefix = Trim(Line.substr(0, OpenParen));
					std::string_view Generics = TypeParams;
					const FToken& Name = Tokens[NameIndex];
					if (Name.Lexeme == BaseName && Name.Location.Column > 1)
					{
						Prefix = Trim(Line.substr(0, Name.Location.Column - 1));
						const std::size_t NameEnd = Name.Location.Column - 1 + Name.Lexeme.size();
						const std::string_view WrittenGenerics = Trim(Line.substr(NameEnd, OpenParen - NameEnd));
						if (!WrittenGenerics.empty())
						{
							Generics = WrittenGenerics;
						}
					}
					Formatted = std::string(Prefix) + (Prefix.empty() ? "" : " ")
						+ DisplayName + std::string(Generics) + std::string(Line.substr(OpenParen)) + ';';
				}
				if (std::find(Lines.begin(), Lines.end(), Formatted) == Lines.end())
				{
					Lines.push_back(std::move(Formatted));
				}
			}
			std::string Result;
			for (const std::string& Line : Lines)
			{
				if (!Result.empty())
				{
					Result.push_back('\n');
				}
				Result += Line;
			}
			return Result.empty() ? "standard function " + DisplayName : Result;
		};

		for (const char* Name : {"source_node_info", "annotation_info", "annotation_argument_info", "comment_info", "annotation_definition_info"})
		{
			if (ProgramHasUserTypeArity(Program, Name, 0))
			{
				continue;
			}

			FDocumentSymbol Symbol;
			Symbol.Kind = EDocumentSymbolKind::Struct;
			Symbol.Name = Name;
			Symbol.Detail = "soft builtin immutable source metadata descriptor";
			Symbol.Documentation = "An immutable source metadata snapshot. Check is_valid() before querying an optional record.";
			Symbol.bExported = true;
			Symbol.Children.push_back(MakeCtor(Name, "()"));
			for (const auto& [Method, Signature] : SourceMetadataBuiltinMethods(Name))
			{
				Symbol.Children.push_back(MakeMethodSymbol(Method, Signature));
			}

			MarkSoft(Symbol);
			OutSymbols.push_back(std::move(Symbol));
		}
		for (auto& Symbol : OutSymbols)
		{
			if (!Symbol.bSoftBuiltin ||
				(Symbol.Kind != EDocumentSymbolKind::Struct && Symbol.Kind != EDocumentSymbolKind::Class))
			{
				continue;
			}

			const auto Head = std::string_view(Symbol.Name).substr(0, Symbol.Name.find('<'));
			for (const auto& [Method, Signature] : SourceMetadataBuiltinMethods(Head))
			{
				if (std::none_of(Symbol.Children.begin(), Symbol.Children.end(),
						[Method](const auto& Child)
						{
							return Child.Name == Method;
						}))
				{
					Symbol.Children.push_back(MakeMethodSymbol(Method, Signature));
				}
			}
		}
		std::size_t AliasCount = 0;
		const StandardLibrary::FNameAlias* TypeAliases =
			StandardLibrary::GetTypeAliases(AliasCount);
		for (std::size_t Index = 0; Index < AliasCount; ++Index)
		{
			const StandardLibrary::FNameAlias& Entry = TypeAliases[Index];
			// Identity entries reserve runtime types; their binding descriptors
			// supply the actual kind and members. A synthetic alias would hide them.
			if (Entry.QualifiedName == Entry.CanonicalName)
			{
				continue;
			}
			FDocumentSymbol Alias;
			for (const EDocumentSymbolKind Kind : {
					 EDocumentSymbolKind::Struct,
					EDocumentSymbolKind::Class,
					EDocumentSymbolKind::Interface,
					EDocumentSymbolKind::Enum,
					EDocumentSymbolKind::TypeAlias})
			{
				if (const FDocumentSymbol* const Canonical =
						FindCanonicalSymbol(Entry.CanonicalName, Kind))
				{
					Alias = *Canonical;
					break;
				}
			}
			if (Alias.Name.empty())
			{
				Alias.Kind = EDocumentSymbolKind::TypeAlias;
				Alias.Detail = "standard type alias of " + std::string(Entry.CanonicalName);
			}
			else
			{
				Alias.Detail = std::string(Entry.QualifiedName == "shared" ? "builtin alias of " : "standard alias of ")
					+ std::string(Entry.CanonicalName)
					+ (Alias.Detail.empty() ? std::string{} : " - " + Alias.Detail);
			}
			// Keep the canonical formal parameters on type aliases so member
			// specialization also works when no script AST declaration exists.
			const std::size_t Generic = Alias.Name.find('<');
			const std::string GenericSuffix = Generic == std::string::npos
				? std::string{} : Alias.Name.substr(Generic);
			Alias.Name.assign(Entry.QualifiedName);
			Alias.Name += GenericSuffix;
			Alias.bExported = true;
			MarkSoft(Alias);
			OutSymbols.push_back(std::move(Alias));
		}

		const std::size_t CanonicalFunctionSymbolCount = OutSymbols.size();
		const StandardLibrary::FNameAlias* FunctionAliases =
			StandardLibrary::GetFunctionAliases(AliasCount);
		for (std::size_t Index = 0; Index < AliasCount; ++Index)
		{
			const StandardLibrary::FNameAlias& Entry = FunctionAliases[Index];
			bool bAdded = false;
			for (std::size_t SymbolIndex = 0;
				SymbolIndex < CanonicalFunctionSymbolCount;
				++SymbolIndex)
			{
				const FDocumentSymbol& Canonical = OutSymbols[SymbolIndex];
				if (Canonical.Kind != EDocumentSymbolKind::Function)
				{
					continue;
				}
				const std::size_t Generic = Canonical.Name.find('<');
				const std::string_view Base(
					Canonical.Name.data(),
					Generic == std::string::npos ? Canonical.Name.size() : Generic);
				if (Base != Entry.CanonicalName)
				{
					continue;
				}
				FDocumentSymbol Alias = Canonical;
				Alias.Name.assign(Entry.QualifiedName);
				if (Entry.QualifiedName == "std.concurrent.spawn")
				{
					Alias.Detail = "int std::concurrent::spawn(string name, channel<T> ch);\n"
						"int std::concurrent::spawn(fn(args...)->void callable, args...);";
				}
				else if (Entry.QualifiedName == "std.concurrent.spawn_task")
				{
					Alias.Detail = "task<T> std::concurrent::spawn_task<T>(string name);\n"
						"task<T> std::concurrent::spawn_task<T>(fn(args...)->T callable, args...);";
				}
				else
				{
					Alias.Detail = FormatQualifiedStandardFunctionDetail(
						Entry.QualifiedName, Canonical.Name, Canonical.Detail);
				}
				Alias.bExported = true;
				MarkSoft(Alias);
				OutSymbols.push_back(std::move(Alias));
				bAdded = true;
			}
			if (!bAdded)
			{
				FDocumentSymbol Alias;
				Alias.Kind = EDocumentSymbolKind::Function;
				Alias.Detail = "standard function "
					+ FormatNamespaceQualifiedName(Entry.QualifiedName)
					+ "\nstandard alias of " + std::string(Entry.CanonicalName);
				Alias.Name.assign(Entry.QualifiedName);
				Alias.bExported = true;
				MarkSoft(Alias);
				OutSymbols.push_back(std::move(Alias));
			}
		}

		// Qualified aliases above keep their copied signatures. Remove unqualified
		// fallbacks for namespace-owned APIs and hide reserved implementation
		// linkages exported by source stdlib modules.
		OutSymbols.erase(
			std::remove_if(
				OutSymbols.begin(),
				OutSymbols.end(),
				[](const FDocumentSymbol& Symbol)
				{
					return Symbol.Kind == EDocumentSymbolKind::Function
								&& (Symbol.Name.rfind("__std_", 0) == 0
									|| (Symbol.bSoftBuiltin
										&& !StandardLibrary::RequiredNamespaceForFunction(
											Symbol.Name).empty()));
				}),
			OutSymbols.end());
	}

	void AppendHostAnnotationSymbols(
		const VeroScript::IHostBindingCatalog* const Bindings,
		std::vector<FDocumentSymbol>& OutSymbols)
	{
		if (!Bindings)
		{
			return;
		}
		for (const VeroScript::FHostAnnotationDesc& Annotation : Bindings->GetAnnotations())
		{
			if (Annotation.Name.empty())
			{
				continue;
			}
			FDocumentSymbol Symbol;
			Symbol.Kind = EDocumentSymbolKind::Annotation;
			Symbol.Name = Annotation.Name;
			Symbol.Detail =
				Annotation.Detail.empty() ? std::string("annotation") : Annotation.Detail;
			Symbol.Documentation = Annotation.Documentation;
			Symbol.bExported = true;
			MarkSoft(Symbol);
			OutSymbols.push_back(std::move(Symbol));
		}
	}

	bool IsAnnotationSymbol(const FDocumentSymbol& Symbol)
	{
		return Symbol.Kind == EDocumentSymbolKind::Annotation
			&& Symbol.bSoftBuiltin;
	}

	void AppendSoftContainerMembers(
		const std::string_view TypeName,
		const std::string_view Prefix,
		std::unordered_map<std::string, FInheritedMemberCompletion>& OutMembersByLabel,
		const FProgram* const Program)
	{
		const auto Subst = [TypeName](const char* Detail)
		{
			return SubstituteSoftTypeParams(TypeName, Detail);
		};

		if (Runtime::IsDynamicArrayTypeName(TypeName))
		{
			AddMethod(OutMembersByLabel, "push", std::string(TypeName) + Subst(" (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "reserve", std::string(TypeName) + " (int64 capacity)", Prefix);
			AddMethod(OutMembersByLabel, "capacity", "int64 () const", Prefix);
			AddMethod(OutMembersByLabel, "clear", std::string(TypeName) + " ()", Prefix);
			AddMethod(OutMembersByLabel, "remove", Subst("bool (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "remove_at", std::string(TypeName) + " (int index)", Prefix);
			AddMethod(OutMembersByLabel, "operator[]", Subst("T (int64 index)"), Prefix);
			return;
		}
		if (Runtime::IsMapTypeName(TypeName) || Runtime::IsOrderedMapTypeName(TypeName))
		{
			const bool bMultiMap = Runtime::IsMultiMapTypeName(TypeName)
				|| Runtime::IsOrderedMultiMapTypeName(TypeName);
			if (bMultiMap)
			{
				AddMethod(
					OutMembersByLabel,
					"add",
					std::string(TypeName) + Subst(" (K key, V value)"),
					Prefix);
			}
			AddMethod(OutMembersByLabel, "has", Subst("bool (K key)"), Prefix);
			AddMethod(OutMembersByLabel, "remove", Subst("bool (K key)"), Prefix);
			AddMethod(OutMembersByLabel, "reserve", std::string(TypeName) + " (int64 capacity)", Prefix);
			AddMethod(OutMembersByLabel, "clear", std::string(TypeName) + " ()", Prefix);
			AddMethod(OutMembersByLabel, "keys", Subst("array<K> ()"), Prefix);
			AddMethod(
				OutMembersByLabel,
				"values",
				Subst(bMultiMap ? "array<array<V>> ()" : "array<V> ()"),
				Prefix);
			AddMethod(
				OutMembersByLabel,
				"entries",
				Subst(bMultiMap
					? "array<pair<K, array<V>>> ()"
					: "array<pair<K, V>> ()"),
				Prefix);
			AddMethod(
				OutMembersByLabel,
				"operator[]",
				Subst(bMultiMap ? "array<V> (K key)" : "V (K key)"),
				Prefix);
			return;
		}
		if (Runtime::IsSetTypeName(TypeName))
		{
			AddMethod(OutMembersByLabel, "has", Subst("bool (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "add", std::string(TypeName) + Subst(" (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "remove", Subst("bool (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "reserve", std::string(TypeName) + " (int64 capacity)", Prefix);
			AddMethod(OutMembersByLabel, "clear", std::string(TypeName) + " ()", Prefix);
			AddMethod(OutMembersByLabel, "values", Subst("array<T> ()"), Prefix);
			return;
		}
		if (Runtime::IsMultiSetTypeName(TypeName)
			|| Runtime::IsOrderedMultiSetTypeName(TypeName))
		{
			AddMethod(OutMembersByLabel, "add", std::string(TypeName) + Subst(" (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "has", Subst("bool (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "count", Subst("int64 (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "remove", Subst("bool (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "reserve", std::string(TypeName) + " (int64 capacity)", Prefix);
			AddMethod(OutMembersByLabel, "clear", std::string(TypeName) + " ()", Prefix);
			AddMethod(OutMembersByLabel, "values", Subst("array<T> ()"), Prefix);
			return;
		}
		if (Runtime::IsQueueTypeName(TypeName))
		{
			AddMethod(OutMembersByLabel, "push", std::string(TypeName) + Subst(" (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "pop", Subst("T ()"), Prefix);
			AddMethod(
				OutMembersByLabel,
				Runtime::IsPriorityQueueTypeName(TypeName) ? "top" : "front",
				Subst("T ()"),
				Prefix);
			AddMethod(OutMembersByLabel, "clear", std::string(TypeName) + " ()", Prefix);
			AddMethod(OutMembersByLabel, "values", Subst("array<T> ()"), Prefix);
			return;
		}
		if (Runtime::IsStackTypeName(TypeName))
		{
			AddMethod(OutMembersByLabel, "push", std::string(TypeName) + Subst(" (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "pop", Subst("T ()"), Prefix);
			AddMethod(OutMembersByLabel, "top", Subst("T ()"), Prefix);
			AddMethod(OutMembersByLabel, "clear", std::string(TypeName) + " ()", Prefix);
			AddMethod(OutMembersByLabel, "values", Subst("array<T> ()"), Prefix);
			return;
		}
		if (Runtime::IsDequeTypeName(TypeName))
		{
			AddMethod(OutMembersByLabel, "push_back", std::string(TypeName) + Subst(" (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "push_front", std::string(TypeName) + Subst(" (T value)"), Prefix);
			AddMethod(OutMembersByLabel, "pop_back", Subst("T ()"), Prefix);
			AddMethod(OutMembersByLabel, "pop_front", Subst("T ()"), Prefix);
			AddMethod(OutMembersByLabel, "front", Subst("T ()"), Prefix);
			AddMethod(OutMembersByLabel, "back", Subst("T ()"), Prefix);
			AddMethod(OutMembersByLabel, "clear", std::string(TypeName) + " ()", Prefix);
			AddMethod(OutMembersByLabel, "values", Subst("array<T> ()"), Prefix);
			AddMethod(OutMembersByLabel, "operator[]", Subst("T (int64 index)"), Prefix);
			return;
		}
		const std::string_view SoftTypeName = StripTypeDecorations(TypeName);
		const auto MetadataHead = StandardLibrary::ResolveTypeName(SoftTypeName.substr(0, SoftTypeName.find('<')));
		const std::size_t MetadataArity = SoftTypeName.find('<') == std::string_view::npos ? 0 : 1;
		if (!Program || !ProgramHasUserTypeArity(*Program, MetadataHead, MetadataArity))
		{
			for (const auto& [Method, Signature] : SourceMetadataBuiltinMethods(MetadataHead))
			{
				AddMethod(OutMembersByLabel, Method, Signature, Prefix);
			}
		}

		if (SoftTypeName == "module_info")
		{
			AddMethod(OutMembersByLabel, "path", "string () const", Prefix);
			AddMethod(OutMembersByLabel, "function_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "global_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "type_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "type_name", "string (int index) const", Prefix);
			return;
		}
		if (SoftTypeName == "function_info")
		{
			for (const auto& [Name, Detail] : std::initializer_list<std::pair<const char*, const char*>>{
					 {"name", "string () const"}, {"linkage_name", "string () const"},
					 {"module_path", "string () const"}, {"return_type", "string () const"},
					 {"parameter_count", "int () const"}, {"parameter_type", "string (int index) const"},
					 {"parameter_name", "string (int index) const"}, {"parameter_is_ref", "bool (int index) const"},
					 {"parameter_is_const", "bool (int index) const"}, {"is_deleted", "bool () const"},
					 {"is_exported", "bool () const"}, {"metadata_count", "int () const"},
					 {"metadata_key", "string (int index) const"}, {"metadata_value", "string (int index) const"},
					 {"has_metadata", "bool (string key) const"}, {"metadata", "string (string key) const"},
					 {"has_annotation", "bool (string name) const"}})
			{
				AddMethod(OutMembersByLabel, Name, Detail, Prefix);
			}
			return;
		}
		if (SoftTypeName == "global_info")
		{
			for (const auto& [Name, Detail] : std::initializer_list<std::pair<const char*, const char*>>{
					 {"name", "string () const"}, {"linkage_name", "string () const"},
					 {"module_path", "string () const"}, {"declared_type", "string () const"},
					 {"runtime_type", "string () const"}, {"is_const", "bool () const"},
					 {"is_exported", "bool () const"}, {"metadata_count", "int () const"},
					 {"metadata_key", "string (int index) const"}, {"metadata_value", "string (int index) const"},
					 {"has_metadata", "bool (string key) const"}, {"metadata", "string (string key) const"},
					 {"has_annotation", "bool (string name) const"}})
			{
				AddMethod(OutMembersByLabel, Name, Detail, Prefix);
			}
			return;
		}
		std::string SoftHead;
		std::vector<std::string> SoftArgs;
		if (SplitTypeApp(SoftTypeName, SoftHead, SoftArgs)
			&& SoftHead == "optional" && SoftArgs.size() == 1)
		{
			AddMethod(OutMembersByLabel, "has_value", "bool () const", Prefix);
			AddMethod(OutMembersByLabel, "get", Subst("T () const"), Prefix);
			AddMethod(
				OutMembersByLabel, "value_or", Subst("T (T fallback) const"), Prefix);
			AddMethod(OutMembersByLabel, "reset", "void ()", Prefix);
			return;
		}
		if (SoftHead == "weak" && SoftArgs.size() == 1)
		{
			AddMethod(OutMembersByLabel, "lock", Subst("T () const"), Prefix);
			AddMethod(OutMembersByLabel, "expired", "bool () const", Prefix);
			AddMethod(OutMembersByLabel, "reset", "void ()", Prefix);
			return;
		}
		if (SoftHead == "readonly" && SoftArgs.size() == 1)
		{
			AddMethod(OutMembersByLabel, "get", Subst("const T& () const"), Prefix);
			return;
		}
		if (SoftHead == "type" && SoftArgs.size() == 1)
		{
			AddMethod(OutMembersByLabel, "kind", "string () const", Prefix);
			AddMethod(OutMembersByLabel, "is_abstract", "bool () const", Prefix);
			AddMethod(OutMembersByLabel, "base_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "base_name", "string (int index) const", Prefix);
			AddMethod(OutMembersByLabel, "field_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "method_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "metadata_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "metadata_key", "string (int index) const", Prefix);
			AddMethod(OutMembersByLabel, "metadata_value", "string (int index) const", Prefix);
			AddMethod(OutMembersByLabel, "has_metadata", "bool (string key) const", Prefix);
			AddMethod(OutMembersByLabel, "metadata", "string (string key) const", Prefix);
			AddMethod(OutMembersByLabel, "has_annotation", "bool (string name) const", Prefix);
			AddMethod(OutMembersByLabel, "enum_member_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "enum_member_name", "string (int index) const", Prefix);
			AddMethod(OutMembersByLabel, "enum_member_value", "int64 (int index) const", Prefix);
			AddMethod(OutMembersByLabel, "enum_underlying_type", "string () const", Prefix);
			AddMethod(OutMembersByLabel, "is_scoped_enum", "bool () const", Prefix);
			return;
		}
		if (SoftHead == "field_info" && SoftArgs.size() == 1)
		{
			AddMethod(OutMembersByLabel, "name", "string () const", Prefix);
			AddMethod(OutMembersByLabel, "declared_type", "string () const", Prefix);
			AddMethod(OutMembersByLabel, "runtime_type", "string () const", Prefix);
			AddMethod(OutMembersByLabel, "is_static", "bool () const", Prefix);
			AddMethod(OutMembersByLabel, "is_mutable", "bool () const", Prefix);
			AddMethod(OutMembersByLabel, "is_declared_on_type", "bool () const", Prefix);
			AddMethod(OutMembersByLabel, "get", Subst("V get<V>(T receiver) const"), Prefix);
			AddMethod(OutMembersByLabel, "set", Subst("void set<V>(T receiver, V value) const"), Prefix);
			AddMethod(OutMembersByLabel, "metadata_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "metadata_key", "string (int index) const", Prefix);
			AddMethod(OutMembersByLabel, "metadata_value", "string (int index) const", Prefix);
			AddMethod(OutMembersByLabel, "has_metadata", "bool (string key) const", Prefix);
			AddMethod(OutMembersByLabel, "metadata", "string (string key) const", Prefix);
			AddMethod(OutMembersByLabel, "has_annotation", "bool (string name) const", Prefix);
			return;
		}
		if (SoftHead == "method_info" && SoftArgs.size() == 1)
		{
			for (const auto& [Name, Detail] : std::initializer_list<std::pair<const char*, const char*>>{
					 {"name", "string () const"}, {"return_type", "string () const"},
					 {"parameter_count", "int () const"}, {"parameter_type", "string (int index) const"},
					 {"parameter_name", "string (int index) const"}, {"parameter_is_ref", "bool (int index) const"},
					 {"parameter_is_const", "bool (int index) const"}, {"is_static", "bool () const"},
					 {"is_virtual", "bool () const"}, {"is_abstract", "bool () const"},
					 {"is_const", "bool () const"}, {"is_deleted", "bool () const"},
					 {"is_constructor", "bool () const"}, {"is_destructor", "bool () const"},
					 {"metadata_count", "int () const"}, {"metadata_key", "string (int index) const"},
					 {"metadata_value", "string (int index) const"},
					 {"has_metadata", "bool (string key) const"}, {"metadata", "string (string key) const"},
					 {"has_annotation", "bool (string name) const"}})
			{
				AddMethod(OutMembersByLabel, Name, Detail, Prefix);
			}
			AddMethod(OutMembersByLabel, "invoke", Subst("R invoke<R>(T receiver, args...) const"), Prefix);
			AddMethod(OutMembersByLabel, "invoke_static", "R invoke_static<R>(args...) const", Prefix);
			return;
		}
		if (SoftHead == "enum_member_info" && SoftArgs.size() == 1)
		{
			AddMethod(OutMembersByLabel, "name", "string () const", Prefix);
			AddMethod(OutMembersByLabel, "value", "int64 () const", Prefix);
			AddMethod(OutMembersByLabel, "metadata_count", "int () const", Prefix);
			AddMethod(OutMembersByLabel, "metadata_key", "string (int index) const", Prefix);
			AddMethod(OutMembersByLabel, "metadata_value", "string (int index) const", Prefix);
			AddMethod(OutMembersByLabel, "has_metadata", "bool (string key) const", Prefix);
			AddMethod(OutMembersByLabel, "metadata", "string (string key) const", Prefix);
			AddMethod(OutMembersByLabel, "has_annotation", "bool (string name) const", Prefix);
			return;
		}
		if (SoftHead == "task" && SoftArgs.size() == 1)
		{
			AddMethod(OutMembersByLabel, "valid", "bool () const", Prefix);
			AddMethod(OutMembersByLabel, "done", "bool () const", Prefix);
			AddMethod(OutMembersByLabel, "await", Subst("T ()"), Prefix);
			AddMethod(OutMembersByLabel, "await_for", Subst("optional<T> (int timeout_ms)"), Prefix);
			AddMethod(OutMembersByLabel, "cancel", "void ()", Prefix);
			return;
		}
		if (SoftHead == "task_group" && SoftArgs.size() == 1)
		{
			AddMethod(OutMembersByLabel, "add", Subst("void (task<T> work)"), Prefix);
			AddMethod(OutMembersByLabel, "size", "int64 () const", Prefix);
			AddMethod(OutMembersByLabel, "await_all", Subst("array<T> ()"), Prefix);
			AddMethod(OutMembersByLabel, "cancel_all", "void ()", Prefix);
			return;
		}
		if (IsMulticastDelegateTypeName(TypeName))
		{
			if (!CanUseSoftDelegateMembers(Program, TypeName))
			{
				return;
			}
			std::string Head;
			std::vector<std::string> Args;
			(void)SplitTypeApp(StripTypeDecorations(TypeName), Head, Args);
			AppendSoftMulticastDelegateMembers(TypeName, Prefix, Args.size(), OutMembersByLabel);
			return;
		}
		if (IsDelegateTypeName(TypeName))
		{
			if (!CanUseSoftDelegateMembers(Program, TypeName))
			{
				return;
			}
			std::string Head;
			std::vector<std::string> Args;
			(void)SplitTypeApp(StripTypeDecorations(TypeName), Head, Args);
			const std::size_t Arity = Args.empty() ? 0 : Args.size() - 1;
			AppendSoftDelegateMembers(TypeName, Prefix, Arity, OutMembersByLabel);
			return;
		}
		// channel / pair: no soft methods (pair fields come from soft pair / hierarchy).
	}

	bool ResolveContainerIndexElementType(
		const std::string_view OwnerTypeName,
		std::string& OutElementType)
	{
		std::string Element;
		std::uint32_t Length = 0;
		if (Runtime::ParseNativeArrayType(OwnerTypeName, Element, Length)
			|| Runtime::ParseDynamicArrayElementType(OwnerTypeName, Element)
			|| Runtime::ParseDequeElementType(OwnerTypeName, Element))
		{
			OutElementType = std::move(Element);
			return true;
		}
		std::string Key;
		std::string Value;
		if (Runtime::ParseMapType(OwnerTypeName, Key, Value)
			|| Runtime::ParseOrderedMapType(OwnerTypeName, Key, Value))
		{
			OutElementType = std::move(Value);
			return true;
		}
		return false;
	}

	bool InferForeachBindingTypes(
		const std::string_view CollectionTypeName,
		const bool bHasKeyBinding,
		std::string& InOutKeyTypeName,
		std::string& InOutElementTypeName)
	{
		if (InOutKeyTypeName != "auto" && InOutElementTypeName != "auto")
		{
			return false;
		}

		std::string Key;
		std::string Value;
		if (Runtime::ParseMapType(CollectionTypeName, Key, Value)
			|| Runtime::ParseOrderedMapType(CollectionTypeName, Key, Value))
		{
			bool bChanged = false;
			if (bHasKeyBinding && InOutKeyTypeName == "auto")
			{
				InOutKeyTypeName = std::move(Key);
				bChanged = true;
			}
			if (InOutElementTypeName == "auto")
			{
				InOutElementTypeName = std::move(Value);
				bChanged = true;
			}
			return bChanged;
		}

		if (InOutElementTypeName != "auto")
		{
			return false;
		}

		std::string Element;
		std::uint32_t Length = 0;
		if (Runtime::IsScriptArgsArrayTypeName(CollectionTypeName))
		{
			InOutElementTypeName = "string";
			return true;
		}
		if (Runtime::ParseNativeArrayType(CollectionTypeName, Element, Length)
			|| Runtime::ParseDynamicArrayElementType(CollectionTypeName, Element)
			|| Runtime::ParseDequeElementType(CollectionTypeName, Element)
			|| Runtime::ParseSetElementType(CollectionTypeName, Element)
			|| Runtime::ParseMultiSetElementType(CollectionTypeName, Element)
			|| Runtime::ParseQueueElementType(CollectionTypeName, Element)
			|| Runtime::ParseStackElementType(CollectionTypeName, Element))
		{
			InOutElementTypeName = std::move(Element);
			return true;
		}
		return false;
	}

	bool ResolveSoftContainerMethodReturnType(
		const std::string_view OwnerTypeName,
		const std::string_view MethodName,
		std::string& OutReturnType,
		const FProgram* const Program)
	{
		const auto Owner = StripTypeDecorations(OwnerTypeName);
		const auto MetadataHead = StandardLibrary::ResolveTypeName(Owner.substr(0, Owner.find('<')));
		const std::size_t Arity = Owner.find('<') == std::string_view::npos ? 0 : 1;
		if (!Program || !ProgramHasUserTypeArity(*Program, MetadataHead, Arity))
		{
			for (const auto& [Name, Signature] : SourceMetadataBuiltinMethods(MetadataHead))
			{
				if (MethodName == Name)
				{
					const auto ReturnEnd = std::string_view(Signature).find(" (");
					OutReturnType.assign(Signature, ReturnEnd);
					return true;
				}
			}
		}
		if (Runtime::IsDynamicArrayTypeName(OwnerTypeName))
		{
			if (MethodName == "push" || MethodName == "reserve"
				|| MethodName == "clear" || MethodName == "remove_at")
			{
				OutReturnType.assign(OwnerTypeName.data(), OwnerTypeName.size());
				return true;
			}
			if (MethodName == "capacity")
			{
				OutReturnType = "int64";
				return true;
			}
			if (MethodName == "remove")
			{
				OutReturnType = "bool";
				return true;
			}
			return false;
		}
		if (Runtime::IsMapTypeName(OwnerTypeName) || Runtime::IsOrderedMapTypeName(OwnerTypeName))
		{
			std::string Key;
			std::string Value;
			const bool bParsed = Runtime::IsMapTypeName(OwnerTypeName)
				? Runtime::ParseMapType(OwnerTypeName, Key, Value)
				: Runtime::ParseOrderedMapType(OwnerTypeName, Key, Value);
			if (!bParsed)
			{
				return false;
			}
			if (MethodName == "has" || MethodName == "remove")
			{
				OutReturnType = "bool";
				return true;
			}
			if (MethodName == "clear" || MethodName == "reserve")
			{
				OutReturnType.assign(OwnerTypeName.data(), OwnerTypeName.size());
				return true;
			}
			if (MethodName == "add"
				&& (Runtime::IsMultiMapTypeName(OwnerTypeName)
					|| Runtime::IsOrderedMultiMapTypeName(OwnerTypeName)))
			{
				OutReturnType.assign(OwnerTypeName.data(), OwnerTypeName.size());
				return true;
			}
			if (MethodName == "keys")
			{
				OutReturnType = "array<" + Key + ">";
				return true;
			}
			if (MethodName == "values")
			{
				OutReturnType = "array<" + Value + ">";
				return true;
			}
			if (MethodName == "entries")
			{
				OutReturnType = "array<" + Runtime::MakePairTypeName(Key, Value) + ">";
				return true;
			}
			return false;
		}
		if (Runtime::IsSetTypeName(OwnerTypeName))
		{
			std::string Element;
			if (!Runtime::ParseSetElementType(OwnerTypeName, Element))
			{
				return false;
			}
			if (MethodName == "has" || MethodName == "remove")
			{
				OutReturnType = "bool";
				return true;
			}
			if (MethodName == "add" || MethodName == "reserve" || MethodName == "clear")
			{
				OutReturnType.assign(OwnerTypeName.data(), OwnerTypeName.size());
				return true;
			}
			if (MethodName == "values")
			{
				OutReturnType = "array<" + Element + ">";
				return true;
			}
			return false;
		}
		if (Runtime::IsMultiSetTypeName(OwnerTypeName)
			|| Runtime::IsOrderedMultiSetTypeName(OwnerTypeName))
		{
			std::string Element;
			if (!Runtime::ParseMultiSetElementType(OwnerTypeName, Element))
			{
				return false;
			}
			if (MethodName == "has" || MethodName == "remove")
			{
				OutReturnType = "bool";
				return true;
			}
			if (MethodName == "count")
			{
				OutReturnType = "int64";
				return true;
			}
			if (MethodName == "add" || MethodName == "reserve" || MethodName == "clear")
			{
				OutReturnType.assign(OwnerTypeName.data(), OwnerTypeName.size());
				return true;
			}
			if (MethodName == "values")
			{
				OutReturnType = "array<" + Element + ">";
				return true;
			}
			return false;
		}
		if (Runtime::IsQueueTypeName(OwnerTypeName))
		{
			if (MethodName == "push" || MethodName == "clear")
			{
				OutReturnType.assign(OwnerTypeName.data(), OwnerTypeName.size());
				return true;
			}
			if (MethodName == "pop"
				|| (MethodName == "front"
					&& !Runtime::IsPriorityQueueTypeName(OwnerTypeName))
				|| (MethodName == "top"
					&& Runtime::IsPriorityQueueTypeName(OwnerTypeName)))
			{
				return Runtime::ParseQueueElementType(OwnerTypeName, OutReturnType);
			}
			if (MethodName == "values")
			{
				std::string Element;
				if (!Runtime::ParseQueueElementType(OwnerTypeName, Element))
				{
					return false;
				}
				OutReturnType = "array<" + Element + ">";
				return true;
			}
			return false;
		}
		if (Runtime::IsStackTypeName(OwnerTypeName))
		{
			if (MethodName == "push" || MethodName == "clear")
			{
				OutReturnType.assign(OwnerTypeName.data(), OwnerTypeName.size());
				return true;
			}
			if (MethodName == "pop" || MethodName == "top")
			{
				return Runtime::ParseStackElementType(OwnerTypeName, OutReturnType);
			}
			if (MethodName == "values")
			{
				std::string Element;
				if (!Runtime::ParseStackElementType(OwnerTypeName, Element))
				{
					return false;
				}
				OutReturnType = "array<" + Element + ">";
				return true;
			}
			return false;
		}
		if (Runtime::IsDequeTypeName(OwnerTypeName))
		{
			if (MethodName == "push_back"
				|| MethodName == "push_front"
				|| MethodName == "clear")
			{
				OutReturnType.assign(OwnerTypeName.data(), OwnerTypeName.size());
				return true;
			}
			if (MethodName == "pop_back"
				|| MethodName == "pop_front"
				|| MethodName == "front"
				|| MethodName == "back")
			{
				return Runtime::ParseDequeElementType(OwnerTypeName, OutReturnType);
			}
			if (MethodName == "values")
			{
				std::string Element;
				if (!Runtime::ParseDequeElementType(OwnerTypeName, Element))
				{
					return false;
				}
				OutReturnType = "array<" + Element + ">";
				return true;
			}
			return false;
		}
		if (IsMulticastDelegateTypeName(OwnerTypeName))
		{
			if (Program && !CanUseSoftDelegateMembers(Program, OwnerTypeName))
			{
				return false;
			}
			if (MethodName == "count")
			{
				OutReturnType = "int64";
				return true;
			}
			return false;
		}
		if (IsDelegateTypeName(OwnerTypeName))
		{
			if (Program && !CanUseSoftDelegateMembers(Program, OwnerTypeName))
			{
				return false;
			}
			if (MethodName == "is_bound" || MethodName == "execute_if_bound")
			{
				OutReturnType = "bool";
				return true;
			}
			if (MethodName == "execute")
			{
				std::string Head;
				std::vector<std::string> Args;
				if (!SplitTypeApp(StripTypeDecorations(OwnerTypeName), Head, Args) || Args.empty())
				{
					return false;
				}
				OutReturnType = Args[0];
				return true;
			}
			return false;
		}
		return false;
	}

	bool IsSoftMulticastDelegateTypeName(const std::string_view TypeName)
	{
		return IsMulticastDelegateTypeName(TypeName);
	}
}

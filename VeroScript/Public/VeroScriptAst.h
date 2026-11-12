// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptMacro.h"
#include "VeroScriptExternalDependency.h"
#include "VeroScriptCompileProcessing.h"
#include "VeroScriptPreprocessor.h"

// Low-level compiler AST declarations. Runtime embedders should normally use
// VeroScriptCompile.h instead of depending on this representation directly.

#include "VeroScriptTypes.h"
#include "VeroScriptTypeScope.h"
#include "VeroScriptTemplate.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace VeroScript
{
	struct FBytecodeModule;
	struct FTypeParamBound;
	class IScriptCompileExtensionTask;

	enum class EExprKind : std::uint8_t
	{
		IntegerLiteral,
		FloatLiteral,
		StringLiteral,
		WStringLiteral,
		BooleanLiteral,
		NullptrLiteral,
		This,
		Identifier,
		Unary,
		Binary,
		Assign,
		Call,
		MemberAccess,
		MethodCall,
		IndexAccess,
		ArrayInit,
		MapInit,
		Conditional,
		Ref,
		Lambda,
		CharacterLiteral,
	};

	enum class EUnaryOp : std::uint8_t
	{
		Negate,
		Not,
		BitNot,
	};

	enum class EBinaryOp : std::uint8_t
	{
		Add,
		Subtract,
		Multiply,
		Divide,
		Modulo,
		Equal,
		NotEqual,
		Less,
		LessEqual,
		Greater,
		GreaterEqual,
		LogicalAnd,
		LogicalOr,
		BitAnd,
		BitOr,
		BitXor,
		ShiftLeft,
		ShiftRight,
		In,
	};

	enum class EStmtKind : std::uint8_t
	{
		Expr,
		Block,
		VarDecl,
		TypeAlias,
		Return,
		If,
		While,
		For,
		ForEach,
		Switch,
		Break,
		Continue,
		Fallthrough,
		Goto,
		Label,
		Try,
		Throw,
		DoWhile,
		// A comma-separated local declaration group. Kept at the end so existing
		// public enum values remain stable for AST tooling.
		VarDeclGroup,
	};

	/**

	 * How outer locals are captured for a lambda literal.

	 */
	enum class ELambdaCaptureKind : std::uint8_t
	{
		// `fn(...)` / `[=](...)`: auto by-value capture of used outer locals.
		Implicit,
		// `[&](...)`: auto by-reference capture of used outer locals.
		Reference,
		// `[](...)`: no outer captures allowed.
		None,
		// `[a, &b](...)`: no default; body may only use listed names.
		Explicit,
	};

	struct FDeferredSyntax
	{
		std::shared_ptr<const FTypeDeclarationScope> TypeScope;
		std::string TypeOwner;
		std::string ScopeId;
		std::string FunctionOwner;
		std::string ParentScopeId;
		std::vector<std::string> FunctionTypeParameters;
		FTemplateParameterList CallableTemplateParameters;
		std::shared_ptr<const struct FParserBindingsSnapshot> Bindings;
		std::vector<std::size_t> TypeAliasBoundaries;
		std::string ModulePath;
		std::string Source, Namespace;
		std::string InitializerType;
		bool bVariableInitializer = false;
		std::vector<std::string> TypeParameters;
		std::unordered_map<std::string, FTemplateParameter> ValueParameters;
		FSourceRange Range;
		bool bAllowInternalBuiltins = false;
	};
	struct FExpr
	{
		virtual ~FExpr() = default;
		explicit FExpr(const EExprKind InKind) : Kind(InKind)
		{
		}

		const EExprKind Kind;
		FSourceLocation Location = {};
		FSourceRange SourceRange;
		FDeclMeta DeclMeta;
		std::shared_ptr<const FDeferredSyntax> DeferredSyntax;
	};

	struct FIntegerLiteralExpr final : FExpr
	{
		explicit FIntegerLiteralExpr(const std::int32_t InValue)
			: FExpr(EExprKind::IntegerLiteral)
			, Value(InValue)
			, bUnsigned(false)
		{
		}

		explicit FIntegerLiteralExpr(const std::uint32_t InUnsignedValue, const bool bInUnsigned)
			: FExpr(EExprKind::IntegerLiteral)
			, UnsignedValue(InUnsignedValue)
			, bUnsigned(bInUnsigned)
		{
		}

		explicit FIntegerLiteralExpr(const std::int64_t InValue)
			: FExpr(EExprKind::IntegerLiteral)
			, Value64(InValue)
			, bUnsigned(false)
			, bIs64(true)
		{
		}

		explicit FIntegerLiteralExpr(const std::uint64_t InUnsignedValue, const bool bInUnsigned)
			: FExpr(EExprKind::IntegerLiteral)
			, UnsignedValue64(InUnsignedValue)
			, bUnsigned(bInUnsigned)
			, bIs64(true)
		{
		}

		std::int32_t Value = 0;
		std::uint32_t UnsignedValue = 0;
		std::int64_t Value64 = 0;
		std::uint64_t UnsignedValue64 = 0;
		bool bUnsigned = false;
		bool bIs64 = false;
	};

	struct FCharacterLiteralExpr final : FExpr
	{
		explicit FCharacterLiteralExpr(const std::int8_t InValue)
			: FExpr(EExprKind::CharacterLiteral)
			, Value(InValue)
		{
		}

		std::int8_t Value = 0;
	};

	struct FFloatLiteralExpr final : FExpr
	{
		explicit FFloatLiteralExpr(const double InValue, const bool bInFloat32 = false)
			: FExpr(EExprKind::FloatLiteral)
			, Value(InValue)
			, bFloat32(bInFloat32)
		{
		}

		double Value = 0.0;
		bool bFloat32 = false;
	};

	struct FStringLiteralExpr final : FExpr
	{
		explicit FStringLiteralExpr(std::string InValue)
			: FExpr(EExprKind::StringLiteral)
			, Value(std::move(InValue))
		{
		}

		std::string Value;
	};

	struct FWStringLiteralExpr final : FExpr
	{
		explicit FWStringLiteralExpr(std::u16string InValue)
			: FExpr(EExprKind::WStringLiteral)
			, Value(std::move(InValue))
		{
		}

		std::u16string Value;
	};

	struct FBooleanLiteralExpr final : FExpr
	{
		explicit FBooleanLiteralExpr(const bool InValue)
			: FExpr(EExprKind::BooleanLiteral)
			, Value(InValue)
		{
		}

		bool Value = false;
	};

	struct FNullptrLiteralExpr final : FExpr
	{
		FNullptrLiteralExpr() : FExpr(EExprKind::NullptrLiteral)
		{
		}
	};

	struct FThisExpr final : FExpr
	{
		FThisExpr() : FExpr(EExprKind::This)
		{
		}
	};

	struct FIdentifierExpr final : FExpr
	{
		explicit FIdentifierExpr(std::string InName)
			: FExpr(EExprKind::Identifier)
			, Name(std::move(InName))
		{
		}

		std::string Name;
		// Parser-only provenance used to enforce namespace-only standard builtins.
		bool bStandardLibraryQualified = false;
		bool bExplicitTemplateArguments = false;
		std::vector<std::string> TypeArgs;
	};

	struct FUnaryExpr final : FExpr
	{
		FUnaryExpr(const EUnaryOp InOp, std::unique_ptr<FExpr> InOperand)
			: FExpr(EExprKind::Unary)
			, Op(InOp)
			, Operand(std::move(InOperand))
		{
		}

		EUnaryOp Op = EUnaryOp::Negate;
		std::unique_ptr<FExpr> Operand;
	};

	struct FBinaryExpr final : FExpr
	{
		FBinaryExpr(
			const EBinaryOp InOp,
			std::unique_ptr<FExpr> InLeft,
			std::unique_ptr<FExpr> InRight)
			: FExpr(EExprKind::Binary)
			, Op(InOp)
			, Left(std::move(InLeft))
			, Right(std::move(InRight))
		{
		}

		EBinaryOp Op = EBinaryOp::Add;
		std::unique_ptr<FExpr> Left;
		std::unique_ptr<FExpr> Right;
	};

	struct FAssignExpr final : FExpr
	{
		FAssignExpr(std::unique_ptr<FExpr> InTarget, std::unique_ptr<FExpr> InValue)
			: FExpr(EExprKind::Assign)
			, Target(std::move(InTarget))
			, Value(std::move(InValue))
		{
		}

		FAssignExpr(
			std::unique_ptr<FExpr> InTarget,
			std::unique_ptr<FExpr> InValue,
			const EBinaryOp InCompoundOp,
			const bool bInFromPrefixIncDec = false,
			const bool bInFromPostfixIncDec = false)
			: FExpr(EExprKind::Assign)
			, Target(std::move(InTarget))
			, Value(std::move(InValue))
			, bIsCompound(true)
			, CompoundOp(InCompoundOp)
			, bFromPrefixIncDec(bInFromPrefixIncDec)
			, bFromPostfixIncDec(bInFromPostfixIncDec)
		{
		}

		std::unique_ptr<FExpr> Target;
		std::unique_ptr<FExpr> Value;
		bool bIsCompound = false;
		EBinaryOp CompoundOp = EBinaryOp::Add;
		// True when desugared from prefix ++/-- (allows unsigned unit promotion without weakening D9 for `u += 1`).
		bool bFromPrefixIncDec = false;
		// True when desugared from postfix ++/-- (expression yields value before update).
		bool bFromPostfixIncDec = false;
	};

	struct FConditionalExpr final : FExpr
	{
		FConditionalExpr(
			std::unique_ptr<FExpr> InCondition,
			std::unique_ptr<FExpr> InThenExpr,
			std::unique_ptr<FExpr> InElseExpr)
			: FExpr(EExprKind::Conditional)
			, Condition(std::move(InCondition))
			, ThenExpr(std::move(InThenExpr))
			, ElseExpr(std::move(InElseExpr))
		{
		}

		std::unique_ptr<FExpr> Condition;
		std::unique_ptr<FExpr> ThenExpr;
		std::unique_ptr<FExpr> ElseExpr;
	};

	struct FRefExpr final : FExpr
	{
		explicit FRefExpr(std::unique_ptr<FExpr> InOperand)
			: FExpr(EExprKind::Ref)
			, Operand(std::move(InOperand))
		{
		}

		std::unique_ptr<FExpr> Operand;
	};

	/**
	 * Non-owning argument sequence used by compiler-only call lowering. It can
	 * prepend one receiver expression without cloning or moving the source AST.
	 */
	// FCallExpr and FMethodCallExpr store this argument view by value.
	class FExprArgumentView
	{
	public:
		class FIterator
		{
		public:
			FIterator(const FExprArgumentView* InView, const std::size_t InIndex)
				: View(InView), Index(InIndex)
			{
			}

			const FExpr* operator*() const
			{
				return (*View)[Index];
			}
			FIterator& operator++()
			{
				++Index;
				return *this;
			}
			bool operator!=(const FIterator& Other) const
			{
				return View != Other.View || Index != Other.Index;
			}

		private:
			const FExprArgumentView* View = nullptr;
			std::size_t Index = 0;
		};

		FExprArgumentView() = default;
		explicit FExprArgumentView(const std::vector<const FExpr *> *InViews) : Views(InViews)
		{
		}
		FExprArgumentView(
			const FExpr *InLeading,
			const std::vector<std::unique_ptr<FExpr>> *InArguments)
			: Leading(InLeading), Arguments(InArguments)
		{
		}

		std::size_t size() const
		{
			return Views ? Views->size() : (Leading ? 1u : 0u) + (Arguments ? Arguments->size() : 0u);
		}

		bool empty() const
		{
			return size() == 0;
		}
		const FExpr* front() const
		{
			return (*this)[0];
		}
		bool IsBound() const
		{
			return Views != nullptr || Leading != nullptr || Arguments != nullptr;
		}
		bool HasLeadingArgument() const
		{
			return Leading != nullptr;
		}
		const std::vector<std::unique_ptr<FExpr>>* GetStorage() const
		{
			return Arguments;
		}

		const FExpr* operator[](const std::size_t Index) const
		{
			if (Views) return (*Views)[Index];
			if (Leading)
			{
				return Index == 0 ? Leading : (*Arguments)[Index - 1].get();
			}
			return (*Arguments)[Index].get();
		}

		FIterator begin() const
		{
			return FIterator(this, 0);
		}
		FIterator end() const
		{
			return FIterator(this, size());
		}

	private:
		const std::vector<const FExpr*>* Views = nullptr;
		const FExpr* Leading = nullptr;
		const std::vector<std::unique_ptr<FExpr>>* Arguments = nullptr;
	};

	struct FCallExpr final : FExpr
	{
		FCallExpr(
			std::string InCallee,
			std::vector<std::unique_ptr<FExpr>> InArguments,
			std::vector<std::string> InTypeArgs = {})
			: FExpr(EExprKind::Call)
			, Callee(std::move(InCallee))
			, Arguments(std::move(InArguments))
			, TypeArgs(std::move(InTypeArgs))
			, bExplicitTemplateArguments(!TypeArgs.empty())
		{
		}

		// Compiler-only lowering can temporarily view another call node's arguments
		// without cloning expression trees. The source node owns them throughout.
		void BorrowArgumentsFrom(
			const std::vector<std::unique_ptr<FExpr>>& InArguments)
		{
			BorrowedArguments = FExprArgumentView(nullptr, &InArguments);
		}

		void BorrowArgumentsFrom(const FExprArgumentView InArguments)
		{
			BorrowedArguments = InArguments;
		}

		void BorrowLeadingArgument(const FExpr& InArgument)
		{
			BorrowedLeadingArgument = &InArgument;
		}

		FExprArgumentView GetArguments() const
		{
			const FExprArgumentView Base = BorrowedArguments.IsBound()
				? BorrowedArguments
				: FExprArgumentView(nullptr, &Arguments);
			if (!BorrowedLeadingArgument)
			{
				return Base;
			}
			// Current lowerings never compose two leading arguments.
			return FExprArgumentView(BorrowedLeadingArgument, Base.GetStorage());
		}

		// Named callee (builtins / free functions). Empty when CalleeExpr is set.
		std::string Callee;
		// Expression callee (lambda, call result, member, index, ...).
		std::unique_ptr<FExpr> CalleeExpr;
		std::vector<std::unique_ptr<FExpr>> Arguments;
		std::vector<std::string> TypeArgs;
		bool bExplicitTemplateArguments = false;
		// Parser provenance used to distinguish an explicitly qualified standard
		// function from an ordinary identifier with the same spelling. The
		// compiler must still let locals and user functions shadow namespace-only
		// builtins.
		bool bStandardLibraryQualified = false;
		// Trusted standard-library/compiler roots may use the canonical builtin
		// spelling directly. Ordinary source never receives this capability.
		bool bImplementationBuiltinAccess = false;
		// Compiler-only UFCS lowering (`value.f(args)` -> `f(value, args)`).
		// The implicit receiver may bind the first ref parameter without source `ref`.
		bool bImplicitReceiverArgument = false;
		bool bImplicitReceiverRefBinding = false;

	private:
		const FExpr* BorrowedLeadingArgument = nullptr;
		FExprArgumentView BorrowedArguments;
	};

	struct FMemberAccessExpr final : FExpr
	{
		FMemberAccessExpr(std::unique_ptr<FExpr> InObject, std::string InMember)
			: FExpr(EExprKind::MemberAccess)
			, Object(std::move(InObject))
			, Member(std::move(InMember))
		{
		}

		std::unique_ptr<FExpr> Object;
		std::string Member;
		bool bExplicitTemplateArguments = false;
		std::vector<std::string> TypeArgs;
	};

	struct FMethodCallExpr final : FExpr
	{
		FMethodCallExpr(
			std::unique_ptr<FExpr> InObject,
			std::string InMethod,
			std::vector<std::unique_ptr<FExpr>> InArguments)
			: FExpr(EExprKind::MethodCall)
			, Object(std::move(InObject))
			, Method(std::move(InMethod))
			, Arguments(std::move(InArguments))
		{
		}

		/**
		 * Temporarily view another call expression's arguments without cloning
		 * them. The compiler uses this only while lowering an unqualified
		 * `Method(...)` call to the existing `this.Method(...)` path; the source
		 * expression always outlives the borrowed view.
		 */
		void BorrowArgumentsFrom(const FExprArgumentView InArguments)
		{
			BorrowedArguments = InArguments;
		}

		FExprArgumentView GetArguments() const
		{
			return BorrowedArguments.IsBound()
				? BorrowedArguments
				: FExprArgumentView(nullptr, &Arguments);
		}

		std::unique_ptr<FExpr> Object;
		std::string Method;
		std::vector<std::unique_ptr<FExpr>> Arguments;
		// Explicit type or value arguments on a qualified free, instance, or static call.
		std::vector<std::string> TypeArgs;
		bool bExplicitTemplateArguments = false;

	private:
		FExprArgumentView BorrowedArguments;
	};

	struct FIndexAccessExpr final : FExpr
	{
		FIndexAccessExpr(std::unique_ptr<FExpr> InObject, std::unique_ptr<FExpr> InIndex)
			: FExpr(EExprKind::IndexAccess)
			, Object(std::move(InObject))
			, Index(std::move(InIndex))
		{
		}

		std::unique_ptr<FExpr> Object;
		std::unique_ptr<FExpr> Index;
	};

	struct FArrayInitExpr final : FExpr
	{
		explicit FArrayInitExpr(
			std::vector<std::unique_ptr<FExpr>> InElements,
			std::string InTypeName = {})
			: FExpr(EExprKind::ArrayInit)
			, Elements(std::move(InElements))
			, TypeName(std::move(InTypeName))
		{
		}

		std::vector<std::unique_ptr<FExpr>> Elements;
		// Non-empty only for an expression-level `Type{...}` initializer. A bare
		// declaration initializer `{...}` keeps using its surrounding target type.
		std::string TypeName;
	};

	struct FMapInitEntry
	{
		std::unique_ptr<FExpr> Key;
		std::unique_ptr<FExpr> Value;
	};

	struct FMapInitExpr final : FExpr
	{
		explicit FMapInitExpr(
			std::vector<FMapInitEntry> InEntries,
			std::string InTypeName = {})
			: FExpr(EExprKind::MapInit)
			, Entries(std::move(InEntries))
			, TypeName(std::move(InTypeName))
		{
		}

		std::vector<FMapInitEntry> Entries;
		std::string TypeName;
	};

	struct FStmt
	{
		std::string ScopeId;
		std::shared_ptr<const FDeferredSyntax> DeclarationContext;
		virtual ~FStmt() = default;
		explicit FStmt(const EStmtKind InKind) : Kind(InKind)
		{
		}

		const EStmtKind Kind;
		FSourceLocation Location = {};
		std::string ModulePath;
		FSourceRange SourceRange;
		FDeclMeta DeclMeta;
		std::shared_ptr<const FDeferredSyntax> DeferredSyntax;
	};

	struct FExprStmt final : FStmt
	{
		explicit FExprStmt(std::unique_ptr<FExpr> InExpression)
			: FStmt(EStmtKind::Expr)
			, Expression(std::move(InExpression))
		{
		}

		std::unique_ptr<FExpr> Expression;
	};

	struct FBlockStmt final : FStmt
	{
		FBlockStmt() : FStmt(EStmtKind::Block)
		{
		}

		std::vector<std::unique_ptr<FStmt>> Statements;
		std::string CallableOwner;
		std::string ParentScopeId;
		std::shared_ptr<const FDeferredSyntax> BodyContext;
		std::shared_ptr<const FDeferredSyntax> EndContext;
		// Supplemental definitions retain their own lexical scope and parameter
		// names while the declaration remains the canonical signature/owner.
		std::string DefinitionModulePath, DefinitionNamespace;
		std::vector<std::string> DefinitionParameterNames;
		std::unordered_map<std::string, std::string> DefinitionTemplateAliases;
	};

	struct FVarDeclStmt final : FStmt
	{
		FVarDeclStmt(std::string InType, std::string InName, std::unique_ptr<FExpr> InInitializer)
			: FStmt(EStmtKind::VarDecl)
			, TypeName(std::move(InType))
			, Name(std::move(InName))
			, Initializer(std::move(InInitializer))
		{
		}

		std::string TypeName;
		std::string Name;
		bool bExported = false;
		bool bIsConst = false;
		// Local alias declaration (`T& name = lvalue` / `const T& name = value`).
		bool bIsRef = false;
		bool bIsLocalStatic = false;
		// Declaration-only symbol loaded from a compiled binary library.
		bool bPrecompiledPackage = false;
		std::unique_ptr<FExpr> Initializer;
		std::string InitializerSource;
		bool bConstexpr = false;
	};

	/**
	 * Same-type declarations written as one source statement, for example
	 * `int first = 1, second = first + 1;`. The declarations compile in source
	 * order within the containing scope; this wrapper does not introduce one.
	 */
	struct FVarDeclGroupStmt final : FStmt
	{
		explicit FVarDeclGroupStmt(std::vector<std::unique_ptr<FVarDeclStmt>> InDeclarations)
			: FStmt(EStmtKind::VarDeclGroup)
			, Declarations(std::move(InDeclarations))
		{
		}

		std::vector<std::unique_ptr<FVarDeclStmt>> Declarations;
	};

	/**

	 * Local `using` / `typedef Alias = Type;` (compile-time only; no bytecode).

	 */
	struct FTypeAliasStmt final : FStmt
	{
		bool bTypeDefinition = false;
		FTypeAliasStmt(std::string InName, std::string InTypeName)
			: FStmt(EStmtKind::TypeAlias)
			, Name(std::move(InName))
			, TypeName(std::move(InTypeName))
		{
		}

		std::string Name;
		std::string TypeName;
	};

	struct FReturnStmt final : FStmt
	{
		explicit FReturnStmt(std::unique_ptr<FExpr> InValue)
			: FStmt(EStmtKind::Return)
			, Value(std::move(InValue))
		{
		}

		std::unique_ptr<FExpr> Value;
	};

	struct FCatchClause
	{
		std::string TypeName;
		std::string Name;
		bool bCatchAll = false;
		bool bIsConst = false;
		bool bIsRef = false;
		FSourceLocation Location = {};
		FSourceLocation NameLocation = {};
		std::unique_ptr<FBlockStmt> Body;
	};

	struct FTryStmt final : FStmt
	{
		FTryStmt() : FStmt(EStmtKind::Try)
		{
		}

		std::unique_ptr<FBlockStmt> TryBlock;
		std::vector<FCatchClause> Catches;
		std::unique_ptr<FBlockStmt> FinallyBlock;
	};

	struct FThrowStmt final : FStmt
	{
		explicit FThrowStmt(std::unique_ptr<FExpr> InValue)
			: FStmt(EStmtKind::Throw)
			, Value(std::move(InValue))
		{
		}

		// Empty only for `throw;`, which rethrows the current caught value.
		std::unique_ptr<FExpr> Value;
	};

	struct FIfStmt final : FStmt
	{
		// Select a branch during compilation, after enclosing template substitution.
		bool bConstexpr = false;
		FIfStmt(
			std::unique_ptr<FExpr> InCondition,
			std::unique_ptr<FBlockStmt> InThenBranch,
			std::unique_ptr<FBlockStmt> InElseBranch)
			: FStmt(EStmtKind::If)
			, Condition(std::move(InCondition))
			, ThenBranch(std::move(InThenBranch))
			, ElseBranch(std::move(InElseBranch))
		{
		}

		std::unique_ptr<FExpr> Condition;
		std::unique_ptr<FBlockStmt> ThenBranch;
		std::unique_ptr<FBlockStmt> ElseBranch;
	};

	struct FWhileStmt final : FStmt
	{
		FWhileStmt(std::unique_ptr<FExpr> InCondition, std::unique_ptr<FBlockStmt> InBody)
			: FStmt(EStmtKind::While)
			, Condition(std::move(InCondition))
			, Body(std::move(InBody))
		{
		}

		std::unique_ptr<FExpr> Condition;
		std::unique_ptr<FBlockStmt> Body;
	};

	struct FDoWhileStmt final : FStmt
	{
		FDoWhileStmt(std::unique_ptr<FBlockStmt> InBody, std::unique_ptr<FExpr> InCondition)
			: FStmt(EStmtKind::DoWhile)
			, Body(std::move(InBody))
			, Condition(std::move(InCondition))
		{
		}

		std::unique_ptr<FBlockStmt> Body;
		std::unique_ptr<FExpr> Condition;
	};

	struct FForStmt final : FStmt
	{
		FForStmt(
			std::unique_ptr<FStmt> InInit,
			std::unique_ptr<FExpr> InCondition,
			std::unique_ptr<FExpr> InStep,
			std::unique_ptr<FBlockStmt> InBody)
			: FForStmt(
				std::move(InInit),
				  {},
				std::move(InCondition),
				std::move(InStep),
				  {},
				std::move(InBody))
		{
		}

		FForStmt(
			std::unique_ptr<FStmt> InInit,
			std::vector<std::unique_ptr<FExpr>> InInitExpressions,
			std::unique_ptr<FExpr> InCondition,
			std::unique_ptr<FExpr> InStep,
			std::vector<std::unique_ptr<FExpr>> InStepExpressions,
			std::unique_ptr<FBlockStmt> InBody)
			: FStmt(EStmtKind::For)
			, Init(std::move(InInit))
			, Condition(std::move(InCondition))
			, Step(std::move(InStep))
			, Body(std::move(InBody))
			, InitExpressions(std::move(InInitExpressions))
			, StepExpressions(std::move(InStepExpressions))
		{
		}

		// Keep the original fields in their original order. Additional classic-for
		// expressions live in appended members so existing source consumers can
		// keep using Init and Step without migration.
		std::unique_ptr<FStmt> Init;
		std::unique_ptr<FExpr> Condition;
		std::unique_ptr<FExpr> Step;
		std::unique_ptr<FBlockStmt> Body;
		std::vector<std::unique_ptr<FExpr>> InitExpressions;
		std::vector<std::unique_ptr<FExpr>> StepExpressions;
	};

	struct FForEachStmt final : FStmt
	{
		FForEachStmt(
			std::string InElementTypeName,
			std::string InElementName,
			std::unique_ptr<FExpr> InCollection,
			std::unique_ptr<FBlockStmt> InBody,
			const bool bInIsConst = false,
			const bool bInIsRef = false)
			: FStmt(EStmtKind::ForEach)
			, ElementTypeName(std::move(InElementTypeName))
			, ElementName(std::move(InElementName))
			, bIsConst(bInIsConst)
			, bIsRef(bInIsRef)
			, Collection(std::move(InCollection))
			, Body(std::move(InBody))
		{
		}

		// Value / single-binding type (`V` or `pair` or `auto`).
		std::string ElementTypeName;
		std::string ElementName;
		bool bIsConst = false;
		// `for (ref T x : xs)` or destructure value `ref V` / `auto& [k,v]`.
		bool bIsRef = false;
		/**
		 * Map destructure: `for ((K k, ref V v) : m)` / `for (auto& [k, v] : m)`.
		 * Key is always a by-value copy; value uses Element* + bIsRef.
		 */
		bool bHasKeyBinding = false;
		std::string KeyTypeName;
		std::string KeyName;
		bool bKeyIsConst = false;
		std::unique_ptr<FExpr> Collection;
		std::unique_ptr<FBlockStmt> Body;
	};

	struct FSwitchCase
	{
		std::vector<std::unique_ptr<FExpr>> Labels;
		bool bIsDefault = false;
		std::unique_ptr<FBlockStmt> Body;
	};

	struct FSwitchStmt final : FStmt
	{
		FSwitchStmt(
			std::unique_ptr<FExpr> InDiscriminant,
			std::vector<FSwitchCase> InCases)
			: FStmt(EStmtKind::Switch)
			, Discriminant(std::move(InDiscriminant))
			, Cases(std::move(InCases))
		{
		}

		std::unique_ptr<FExpr> Discriminant;
		std::vector<FSwitchCase> Cases;
	};

	struct FBreakStmt final : FStmt
	{
		FBreakStmt() : FStmt(EStmtKind::Break)
		{
		}
	};

	struct FContinueStmt final : FStmt
	{
		FContinueStmt() : FStmt(EStmtKind::Continue)
		{
		}
	};

	struct FFallthroughStmt final : FStmt
	{
		FFallthroughStmt() : FStmt(EStmtKind::Fallthrough)
		{
		}
	};

	struct FGotoStmt final : FStmt
	{
		explicit FGotoStmt(std::string InName)
			: FStmt(EStmtKind::Goto)
			, Name(std::move(InName))
		{
		}

		std::string Name;
	};

	/**

	 * Empty marker statement: `name:` before the next statement.

	 */
	struct FLabelStmt final : FStmt
	{
		explicit FLabelStmt(std::string InName)
			: FStmt(EStmtKind::Label)
			, Name(std::move(InName))
		{
		}

		std::string Name;
	};

	struct FFunctionParam
	{
		std::string TypeName;
		std::string Name;
		bool bIsConst = false;
		bool bIsRef = false;
		// Trailing `T... name` -> body type `array<T>`; call site packs 0+ args.
		bool bIsVariadic = false;
		// Optional default (literal only at compile); trailing params only.
		std::shared_ptr<FExpr> DefaultValue;
		std::string DefaultValueSource;
		FDeclMeta DeclMeta;
	};

	struct FLambdaCapture
	{
		std::string Name;
		bool bByReference = false;
		std::unique_ptr<FExpr> Initializer;
		FSourceLocation Location = {};
		FSourceRange SourceRange;
		FSourceRange NameRange;
	};

	struct FLambdaExpr final : FExpr
	{
		FLambdaExpr()
			: FExpr(EExprKind::Lambda)
		{
		}

		ELambdaCaptureKind CaptureKind = ELambdaCaptureKind::Implicit;
		std::vector<FLambdaCapture> Captures;
		bool bMutable = false;

		bool HasCapture(const std::string& Name, const bool bReference) const
		{
			for (const auto& Capture : Captures)
			{
				if (Capture.Name == Name && Capture.bByReference == bReference) return true;
			}
			return false;
		}

		std::vector<std::string> GetCaptureNames(const bool bReference) const
		{
			std::vector<std::string> Names;
			for (const auto& Capture : Captures)
			{
				if (Capture.bByReference == bReference)
				{
					Names.push_back(Capture.Name);
				}
			}
			return Names;
		}
		std::vector<FFunctionParam> Parameters;
		std::string ReturnType;
		std::unique_ptr<FBlockStmt> Body;
		std::unique_ptr<FExpr> ExpressionBody;
		// Filled by semantic capture collection. AST-based backends use this to
		// preserve class receiver ownership and struct value-snapshot semantics
		// without guessing from identifier spellings.
		mutable std::atomic<bool> bCapturesThis{false};
	};

	/**

	 * `using Base::member;` inside a class/struct body (not a type alias).

	 */
	struct FUsingMemberDecl
	{
		// Optional `Base::` qualifier; empty = search direct bases.
		std::string BaseTypeName;
		std::string MemberName;
		// Access of the using-declaration (the enclosing access section).
		EMemberAccess Access = EMemberAccess::Public;
		FSourceLocation Location = {};
	};

	/**
	 * C++-style type or exact-signature function declaration.
	 */
	struct FFriendDecl
	{
		enum class EKind : std::uint8_t
		{
			Type,
			Function,
		};
		EKind Kind = EKind::Type;
		std::string TypeName;
		std::string ReturnType;
		std::string FunctionName;
		std::vector<FFunctionParam> Parameters;
		bool bIsConstMethod = false;
		FSourceLocation Location = {};
	};

	struct FFieldDecl
	{
		std::string TypeName;
		std::string Name;
		bool bIsConst = false;
		// Contextual `mutable`: writable through a const receiver. Only valid on
		// non-static struct/class instance fields.
		bool bIsMutable = false;
		// C++-style static data member (shared global keyed as Type.Name).
		bool bIsStatic = false;
		// Compiler-synthesized storage slots (for example pair<K,V> results and
		// optional<T> inactive storage) may temporarily hold no value even when
		// T is a non-default-constructible host struct. Never set for user fields.
		bool bAllowUninitializedHostStruct = false;
		EMemberAccess Access = EMemberAccess::Public;
		std::unique_ptr<FExpr> DefaultValue;
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
		std::string DefaultValueSource;
		bool bConstexpr = false;
	};

	struct FMethodDecl
	{
		FTemplateParameterList TemplateParameters;
		std::vector<std::string> TypeParams, TypeParamDefaults;
		std::vector<FTypeParamBound> Bounds;
		std::string ReturnType;
		std::string Name;
		std::vector<FFunctionParam> Parameters;
		std::unique_ptr<FBlockStmt> Body;
		bool bVirtual = false;
		bool bOverride = false;
		// Trailing `final` - cannot be overridden further (`virtual` or `override` required).
		bool bFinal = false;
		// Trailing `) const` - callable on const receivers; body cannot mutate this.
		bool bIsConst = false;
		// C++-style static member function (no `this`; call via Type.Name or instance.Name).
		bool bIsStatic = false;
		// Declaration-only overload: participates in lookup, but any selected call is ill-formed.
		bool bDeleted = false;
		bool bDefaulted = false;
		// Explicit pure-virtual declaration (`virtual ... = 0`). A missing body by
		// itself never implies virtual dispatch, including inside an interface.
		bool bPureVirtual = false;
		EMemberAccess Access = EMemberAccess::Public;
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
		bool bConstexpr = false;
	};

	/**

	 * One base initializer in a constructor mem-initializer list (`: B1(a), B2(b)`).

	 */
	struct FBaseCtorInit
	{
		std::string BaseName;
		std::vector<std::unique_ptr<FExpr>> Args;
		FSourceLocation Location = {};
	};

	/**

	 * One direct-field initializer in a constructor initializer list (`: value(arg)`).

	 */
	struct FFieldCtorInit
	{
		std::string FieldName;
		std::vector<std::unique_ptr<FExpr>> Args;
		FSourceLocation Location = {};
	};

	struct FConstructorDecl
	{
		EMemberAccess Access = EMemberAccess::Public;
		std::vector<FFunctionParam> Parameters;
		// `explicit` constructors remain available to direct construction and
		// cast<T>, but never participate in ordinary assignment/call coercion.
		bool bExplicit = false;
		// Base constructors in source order (`: B1(...), B2(...)`). Empty if omitted.
		std::vector<FBaseCtorInit> BaseInits;
		// Direct instance fields initialized before the constructor body. Field
		// declarations with their own initializer cannot also appear here.
		std::vector<FFieldCtorInit> FieldInits;
		std::unique_ptr<FBlockStmt> Body;
		// `= default` requests a default or same-type const-reference copy constructor.
		bool bDefaulted = false;
		// `= delete` suppresses the matching constructor overload.
		bool bDeleted = false;
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
	};

	struct FDestructorDecl
	{
		std::unique_ptr<FBlockStmt> Body;
		bool bVirtual = false;
		// `virtual ~Type() = 0;`. Like C++, a pure virtual destructor still needs
		// a same-file out-of-class definition that supplies the callable base body.
		bool bPureVirtual = false;
		bool bDefaulted = false;
		EMemberAccess Access = EMemberAccess::Public;
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
	};

	struct FEnumMemberDecl
	{
		std::string Name;
		std::int64_t Value = 0;
		bool bHasExplicitValue = false;
		// Leading annotations on the member (e.g. `UMETA(DisplayName="X") Red,`).
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
		std::shared_ptr<FExpr> Initializer;
		std::string InitializerSource;
	};

	struct FEnumDecl
	{
		std::shared_ptr<const FDeferredSyntax> DeclarationContext;
		FTypeDeclarationScope Scope;
		std::string Name;
		std::string ModulePath;
		bool bExported = false;
		bool bPrecompiledPackage = false;
		/**
		 * `enum class Name` - scoped enumerators: only `Name.Member` / `Name::Member`
		 * resolves; bare `Member` is not injected into the enclosing scope (unlike plain `enum`).
		 */
		bool bScoped = false;
		// Explicit storage domain. Supports signed integral types plus uint32 /
		// unsigned int; default remains `int` for source compatibility.
		std::string UnderlyingTypeName = "int";
		std::vector<FEnumMemberDecl> Members;
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
	};

	struct FTypeParamBound
	{
		std::string TypeParam;
		// Interface type as written (`IFoo` or `Boxable<int>`).
		std::string InterfaceTypeName;
	};

	struct FInterfaceDecl
	{
		FTemplateParameterList TemplateParameters;
		std::shared_ptr<const FDeferredSyntax> DeclarationContext;
		FTypeDeclarationScope Scope;
		std::string Name;
		std::string ModulePath;
		bool bExported = false;
		bool bPrecompiledPackage = false;
		// `interface Name final` - cannot be extended.
		bool bFinal = false;
		// Empty = non-generic.
		std::vector<std::string> TypeParams;
		// Parallel to TypeParams; empty entry = required parameter.
		std::vector<std::string> TypeParamDefaults;
		/**
		 * Parent interfaces as written (declaration order; simple name or type app,
		 * e.g. `IParent` / `IParent<T>` / `IParent<int>`). Empty = no parents.
		 * Multiple parents allowed (diamond OK); see CollectFlattenedInterfaceMethods.
		 */
		std::vector<std::string> BaseInterfaceNames;
		// `where T : I` on generic interfaces.
		std::vector<FTypeParamBound> Bounds;
		/**
		 * Methods: bPureVirtual = explicit `virtual ... = 0;` contract;
		 * Body + bVirtual = dynamically overridable default;
		 * Body without bVirtual = statically bound default. A bare declaration
		 * ending in `;` is rejected rather than becoming implicitly virtual.
		 * Access sections (public/protected/private) apply like class members.
		 */
		std::vector<FMethodDecl> Methods;
		// Instance state slice; merged into implementing class/struct layouts.
		std::vector<FFieldDecl> Fields;
		std::vector<FFriendDecl> Friends;
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
	};

	struct FStructDecl
	{
		FTemplateParameterList TemplateParameters;
		std::shared_ptr<const FDeferredSyntax> DeclarationContext;
		FTypeDeclarationScope Scope;
		std::string Name;
		std::string ModulePath;
		bool bExported = false;
		bool bPrecompiledPackage = false;
		// `static struct Name` is a non-instantiable static-member container.
		// It differs from `static class` only in the default member access.
		bool bIsStatic = false;
		// `struct Name final` - cannot be inherited.
		bool bFinal = false;
		// Empty = non-generic.
		std::vector<std::string> TypeParams;
		// Parallel to TypeParams; empty entry = required parameter.
		std::vector<std::string> TypeParamDefaults;
		// `where T : I` on generic structs.
		std::vector<FTypeParamBound> Bounds;
		// Simple base type name as written; empty = no base. Struct: at most one struct base.
		std::string BaseTypeName;
		// Inheritance access for BaseTypeName (`public` default for struct).
		EMemberAccess BaseInheritanceAccess = EMemberAccess::Public;
		// Interface simple names from the `:` list (struct base goes in BaseTypeName).
		std::vector<std::string> InterfaceTypeNames;
		std::vector<FFieldDecl> Fields;
		std::vector<FMethodDecl> Methods;
		std::vector<FConstructorDecl> Constructors;
		std::unique_ptr<FDestructorDecl> Destructor;
		std::vector<FUsingMemberDecl> UsingMembers;
		std::vector<FFriendDecl> Friends;
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
	};

	struct FClassDecl
	{
		FTemplateParameterList TemplateParameters;
		std::shared_ptr<const FDeferredSyntax> DeclarationContext;
		FTypeDeclarationScope Scope;
		std::string Name;
		std::string ModulePath;
		bool bExported = false;
		bool bPrecompiledPackage = false;
		// `static class Name` is a non-instantiable namespace-like type. Its
		// declared fields and methods are normalized to static members by the
		// parser, while this bit preserves the source-level type contract through
		// compilation, bytecode, reflection, and runtime validation.
		bool bIsStatic = false;
		// `class Name final` - cannot be inherited.
		bool bFinal = false;
		// Empty = non-generic.
		std::vector<std::string> TypeParams;
		// Parallel to TypeParams; empty entry = required parameter.
		std::vector<std::string> TypeParamDefaults;
		// `where T : I` on generic classes.
		std::vector<FTypeParamBound> Bounds;
		// Simple class base type names as written (declaration order). Empty = no class bases.
		std::vector<std::string> BaseTypeNames;
		// Parallel to BaseTypeNames; omitted access defaults to private for class (C++).
		std::vector<EMemberAccess> BaseInheritanceAccess;
		// Interface simple names from the `:` list (class bases go in BaseTypeNames).
		std::vector<std::string> InterfaceTypeNames;
		std::vector<FFieldDecl> Fields;
		std::vector<FMethodDecl> Methods;
		std::vector<FConstructorDecl> Constructors;
		std::unique_ptr<FDestructorDecl> Destructor;
		// Host-invoked class-default phase. It is compiled independently from
		// constructors and is never run implicitly by ordinary object creation.
		std::unique_ptr<FBlockStmt> ClassDefaults;
		std::vector<FUsingMemberDecl> UsingMembers;
		std::vector<FFriendDecl> Friends;
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
	};

	struct FImportDecl
	{
		std::string Path;
		std::string ImporterModulePath;
		std::string ResolvedPath;
		bool bReExport = false;
		// Added by the module loader from configuration, not written in source.
		bool bImplicit = false;
		FSourceLocation Location = {};
	};

	/**
	 * A lexical `using namespace A::B;` directive (qualified names use dots internally).
	 */
	struct FUsingNamespaceDecl
	{
		// Name as written, normalized from `::` to `.`. It may be relative to ScopeNamespace.
		std::string NamespaceName;
		// Namespace containing the directive; empty means module/global scope.
		std::string ScopeNamespace;
		// Directives are module-local and never leak through import/export.
		std::string ModulePath;
		FSourceLocation Location = {};
	};

	/**
	 * Explicit namespace declaration retained for directive validation, including empty namespaces.
	 */
	struct FNamespaceDecl
	{
		std::string Name;
		std::string ModulePath;
		FSourceLocation Location = {};
		FDeclMeta DeclMeta;
	};

	/**
	 * Preprocessed source retained only so an exported open generic can be
	 * instantiated after its defining library has been packaged. Package-loaded
	 * entries may be carried transitively when another library re-exports or uses
	 * the generic. Non-generic declarations from these modules are declaration
	 * stubs; their executable definitions remain in package bytecode.
	 */
	struct FPackageSourceModule
	{
		std::string SourceOwnerId;
		std::string ModulePath;
		std::string Source;
		std::vector<FImportDecl> Imports;
		bool bDeclarationOnly = false;
		bool bFromPackage = false;
		bool bPackageSurface = false;
		std::string FacadePath;
		std::string OwnerComponentId;
		std::vector<FAnnotationGenerationOperation> GeneratedOperations;
		std::vector<FCompileGenerationOperation> CompileGeneratedOperations;
		std::shared_ptr<const FSourceMetadataSnapshot> InitialSourceMetadata;
	};

	/**

	 * `using` / `typedef Alias = Type;` (optional `export`).

	 */
	struct FTypeAliasDecl
	{
		std::shared_ptr<const FDeferredSyntax> DeclarationContext;
		FTypeDeclarationScope Scope;
		std::string Name;
		std::string TypeName;
		std::string ModulePath;
		bool bExported = false;
		bool bPrecompiledPackage = false;
		FSourceLocation Location = {};
		FDeclMeta DeclMeta;
	};

	struct FFunctionDecl
	{
		FTemplateParameterList TemplateParameters;
		std::string ReturnType;
		std::string Name;
		std::string ModulePath;
		// Empty = non-generic.
		std::vector<std::string> TypeParams;
		// Parallel to TypeParams; empty entry = required parameter.
		std::vector<std::string> TypeParamDefaults;
		// `where T : I` constraints; only on generics.
		std::vector<FTypeParamBound> Bounds;
		bool bExported = false;
		// Declaration imported from a compiled .vslib. The compiler reserves its
		// canonical function slot but the package linker supplies the body.
		bool bPrecompiledPackage = false;
		std::vector<FFunctionParam> Parameters;
		std::unique_ptr<FBlockStmt> Body;
		// Declaration-only overload: participates in lookup, but cannot be called or captured.
		bool bDeleted = false;
		FDeclMeta DeclMeta;
		FSourceLocation Location = {};
		bool bConstexpr = false;
	};

	struct FSourceDefinition
	{
		std::string ModulePath, Namespace;
		std::vector<std::string> PrefixTypeParams, PrefixTypeParamDefaults;
		std::vector<FTemplateParameterList> TemplateHeaders;
		std::vector<FToken> Tokens;
		bool bBound = false;
	};
	class FProgramPreparationSeal;
	struct FGeneratedBlockTypeBinding
	{
		std::string ScopeId, AnchorId, Name, TypeName;
		FSourceRange Anchor;
		FDeclMeta Metadata;
		bool bAtEnd = false;
	};
	struct FProgram
	{
		FConstantEvaluationLimits ConstantEvaluationLimits;
		std::vector<FScopedTypeUse> ScopedTypeUses;
		std::vector<FGeneratedBlockTypeBinding> GeneratedBlockTypes;
		std::shared_ptr<const class FModuleEnvironment> ModuleEnvironment;
		std::vector<FExternalDependency> RequiredExternalDependencies;
		std::unordered_map<std::string, std::string> ModuleSourceOwners;
		std::shared_ptr<const FProgramPreparationSeal> PreparationSeal;
		std::shared_ptr<FCompileProcessingSession> CompileProcessingSession;
		std::shared_ptr<IScriptCompileExtensionTask> ScriptCompileExtensionTask;
		std::shared_ptr<const FSourceMetadataSnapshot> SourceMetadata;
		std::vector<FCompileGenerationOperation> CompileGeneratedOperations;
		std::string CompileProcessingFingerprint;
		std::vector<FSourceDefinition> SourceDefinitions;
		bool bDeclarationsPrepared = false;
		std::string PreparationHostFingerprint;
		std::vector<FGeneratedDeclarationDocument> GeneratedDocuments;
		std::vector<FAnnotationDependency> AnnotationDependencies;
		bool bPackageGenerationReplayed = false;
		std::vector<FPublicMacro> PublicMacros;
		// Optional immutable preprocessing snapshots used by tooling and debugging.
		std::unordered_map<std::string, FPreprocessLineMap> PreprocessLineMaps;
		// Keeps lexer/preprocessor backing storage alive for string_view fields in
		// tokens and AST nodes after VeroScriptModuleLoader returns the program.
		// The concrete storage types are intentionally private to the loader.
		std::vector<std::shared_ptr<const void>> LifetimeAnchors;
		// Soft compiler-provided, non-generic types referenced by this source graph.
		// Keeping this explicit lets the compiler avoid emitting their bytecode into
		// every module and prevents duplicate helper definitions across packages.
		std::unordered_set<std::string> UsedCompilerBuiltinTypes;
		// Absolute/normalized path or source label of the entry module; only its main() is the script entry.
		std::string EntryModulePath;
		// Entry + implicit-import files when project-wide internal access is enabled.
		std::unordered_set<std::string> ProjectVisibleModulePaths;
		std::vector<FImportDecl> Imports;
		std::vector<FNamespaceDecl> Namespaces;
		std::vector<FUsingNamespaceDecl> UsingNamespaces;
		std::vector<FTypeAliasDecl> TypeAliases;
		std::vector<FEnumDecl> Enums;
		std::vector<FStructDecl> Structs;
		std::vector<FClassDecl> Classes;
		std::vector<FInterfaceDecl> Interfaces;
		std::vector<FFunctionDecl> Functions;
		// Populated by the module loader. The compiler copies this graph into
		// bytecode only when the exported package surface contains open generics.
		std::vector<FPackageSourceModule> PackageSourceModules;
		// Closed dependency modules corresponding to bPrecompiledPackage declarations.
		std::vector<std::shared_ptr<const FBytecodeModule>> PrecompiledPackageModules;
		std::vector<std::unique_ptr<FStmt>> TopLevelStatements;
		// Immutable preparation identity survives internal clones and AOT reuse.
		std::string DeclarationPreparationFingerprint;
		bool bDeclarationPreparationCacheable = true;
		std::vector<FAnnotationGenerationOperation> DeclarationGeneratedOperations;
	};
	// Generated diagnostic positions stay in the virtual document; lexical
	// lookup uses the original declaration point, including stripped packages.
	inline FSourceLocation ResolveProgramLookupLocation(const FProgram* Program, FSourceLocation Location)
	{
		if (Program && Location.FileLabel.rfind("veroscript-generated:", 0) == 0)
		{
			for (const auto& Doc : Program->GeneratedDocuments)
			{
				if (Doc.FileLabel == Location.FileLabel)
				{
					return {Doc.LookupAnchor.FileLabel, Doc.LookupAnchor.Line, Doc.LookupAnchor.Column};
				}
			}
		}

		return Location;
	}
}

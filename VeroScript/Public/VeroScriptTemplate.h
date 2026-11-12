// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VeroScript
{
	struct FExpr;
	struct FTemplateEvaluationContext;
	struct FTemplateArgument;

	enum class ETemplateParameterKind : std::uint8_t
	{
		Type,
		Value
	};

	/** The declaration owns immutable syntax; bindings never modify these records. */
	struct FTemplateParameter
	{
		bool HasDefault() const noexcept
		{
			return Kind == ETemplateParameterKind::Type ? !DefaultTypeName.empty()
														: DefaultValue != nullptr || !DefaultSource.empty();
		}

		std::string Id;
		std::string Name;
		ETemplateParameterKind Kind = ETemplateParameterKind::Type;
		std::string Namespace;
		std::string ValueTypeName;
		std::string DefaultTypeName;
		std::shared_ptr<const FExpr> DefaultValue;
		std::string DefaultSource;
		FSourceLocation Location;
	};

	/** Canonical parameter records with read-only, precomputed transport views. */
	class FTemplateParameterList
	{
	  public:
		void Assign(std::vector<FTemplateParameter> Parameters, std::size_t EnclosingCount = 0);

		const std::vector<FTemplateParameter> &GetParameters() const noexcept
		{
			return Parameters;
		}

		const std::vector<std::string> &GetNames() const noexcept
		{
			return Names;
		}

		const std::vector<std::string> &GetDefaultSources() const noexcept
		{
			return Defaults;
		}

		std::size_t GetEnclosingCount() const noexcept
		{
			return EnclosingCount;
		}

		std::size_t GetOwnCount() const noexcept
		{
			return Parameters.size() - EnclosingCount;
		}

		bool IsEmpty() const noexcept
		{
			return Parameters.empty();
		}

	  private:
		std::vector<FTemplateParameter> Parameters;
		std::vector<std::string> Names, Defaults;
		std::size_t EnclosingCount = 0;
	};

	// Type-only AST builders remain valid input. A populated structured
	// list is authoritative; public TypeParams continues to contain types only.
	template <typename TDeclaration>
	const std::vector<std::string> &TemplateParameterNames(const TDeclaration &Declaration)
	{
		return Declaration.TemplateParameters.IsEmpty() ? Declaration.TypeParams
														: Declaration.TemplateParameters.GetNames();
	}

	template <typename TDeclaration>
	const std::vector<std::string> &TemplateParameterDefaults(const TDeclaration &Declaration)
	{
		return Declaration.TemplateParameters.IsEmpty() ? Declaration.TypeParamDefaults
														: Declaration.TemplateParameters.GetDefaultSources();
	}

	/** Source declaration metadata. Runtime instances reference Id and own no AST. */
	struct FTemplateDeclarationInfo
	{
		std::string Id, Kind, Name, Owner, ModulePath, ReturnType, SourceFile;
		FTemplateParameterList Parameters, EnclosingParameters;
		std::vector<std::string> ParameterTypeNames;
		EMemberAccess Access = EMemberAccess::Public;
		bool bStatic = false, bConst = false, bExported = false;
		std::uint32_t Line = 0, Column = 0;
	};

	/** Canonical arguments of one closed instance; no value is converted to double. */
	struct FTemplateInstanceInfo
	{
		std::string DeclarationId;
		std::vector<std::string> Arguments, EnclosingArguments;
		// Closed value types in enclosing-then-own parameter order. Captured after
		// binding, so source-free validation does not re-read declaration constants.
		std::vector<std::string> ValueTypeNames;
	};

	bool EqualTemplateParameters(const FTemplateParameterList &, const FTemplateParameterList &);
	bool HasMethodOrValueTemplates(const struct FProgram &);
	bool RequiresCoreTemplateExport(const struct FProgram &);
	std::string FormatTemplateArgument(const FTemplateArgument &);
	std::string FormatTemplateArgumentName(std::string_view);

	struct FTemplateValueType
	{
		// Canonical primitive name or the fully qualified enum declaration identity.
		std::string Name;
		std::uint8_t Width = 0;
		bool bUnsigned = false;
		bool bBoolean = false;
		bool bEnum = false;
		bool bScopedEnum = false;
	};

	/** Exact fixed-width bits. This never stores a floating intermediate or a VM object. */
	struct FTemplateConstant
	{
		FTemplateValueType Type;
		std::uint64_t Bits = 0;
	};

	struct FTemplateArgument
	{
		ETemplateParameterKind Kind = ETemplateParameterKind::Type;
		std::string TypeName;
		FTemplateConstant Value;
	};

	enum class ETemplateEvaluationStatus : std::uint8_t
	{
		Constant,
		Dependent,
		NotConstant,
		Invalid,
		Interrupted,
		LimitExceeded,
		OutOfMemory,
		InternalError
	};

	struct FTemplateEvaluationResult
	{
		bool Succeeded() const noexcept
		{
			return Status == ETemplateEvaluationStatus::Constant;
		}

		ETemplateEvaluationStatus Status = ETemplateEvaluationStatus::NotConstant;
		FTemplateConstant Value;
		std::string Reason;
		FSourceLocation Location;
	};

	/** Resolver syntax is borrowed for the evaluation call; Identity includes its binding environment. */
	struct FTemplateConstantSymbol
	{
		ETemplateEvaluationStatus Status = ETemplateEvaluationStatus::NotConstant;
		std::string Identity;
		FTemplateConstant Value;
		const FExpr *Expression = nullptr;
		std::string DeclaredTypeName;
		// Optional immutable declaration context; borrowed for the entire evaluation.
		const FTemplateEvaluationContext *DeclarationContext = nullptr;
		bool bExplicitConversion = false;
		std::string DeclarationGroup;
		std::size_t DeclarationOrder = 0;
	};

	/** Immutable bound view. AST and context remain owned by the compilation. */
	struct FConstantFunction
	{
		ETemplateEvaluationStatus Status = ETemplateEvaluationStatus::NotConstant;
		std::string Identity, Reason;
		FTemplateValueType ReturnType;
		std::vector<std::string> ParameterNames;
		std::vector<FTemplateValueType> ParameterTypes;
		std::vector<const FExpr *> Defaults;
		std::vector<bool> ParameterConsts;
		const struct FBlockStmt *Body = nullptr;
		const FTemplateEvaluationContext *DeclarationContext = nullptr;
		const FTemplateEvaluationContext *DefaultContext = nullptr;
	};

	struct FConstantEvaluationLimits
	{
		std::uint64_t MaxWork = 1000000;
		std::uint32_t MaxCallDepth = 64;
		std::uint32_t MaxExpressionDepth = 256;
		std::uint64_t MaxMemoryBytes = 16 * 1024 * 1024;
		std::uint64_t MaxCacheBytes = 8 * 1024 * 1024;
	};

	/** Per compilation, never shared between parallel requests or persisted. */
	struct FConstantEvaluationState
	{
		FConstantEvaluationLimits Limits;
		std::uint64_t Work = 0, MemoryBytes = 0;
		std::uint32_t NativeDepth = 0, CallDepth = 0;
	};

	struct FTemplateEvaluationContext
	{
		std::function<FTemplateConstantSymbol(std::string_view, const FSourceLocation &)> ResolveName;
		std::function<ETemplateEvaluationStatus(std::string_view, FTemplateValueType &)> ResolveType;
		std::function<void(std::string_view)> RecordDependency;
		std::function<bool(std::uint64_t)> CheckWork;
		std::uint64_t MaxNodes = 65536;
		// Recursive native evaluation has a hard stack-safe ceiling of 256.
		std::uint32_t MaxDepth = 256;
		// Appended to preserve aggregate initialization of the original context.
		std::function<void(std::string_view, const FTemplateConstant &)> RecordConstant;
		std::function<FConstantFunction(const FExpr &, const std::vector<FTemplateEvaluationResult> &)> ResolveCall;
		std::shared_ptr<FConstantEvaluationState> State;

	};

	/** Recognizes only existing integral aliases and bool; enum resolution belongs to the caller. */
	bool TryGetTemplateValueType(std::string_view Name, FTemplateValueType &OutType);
	FTemplateEvaluationResult EvaluateTemplateConstant(const FExpr &, const FTemplateEvaluationContext & = {});
	FTemplateEvaluationResult ValidateConstantFunction(const FConstantFunction &, const FTemplateEvaluationContext &);
	FTemplateEvaluationResult ConvertTemplateConstant(const FTemplateConstant &, const FTemplateValueType &,
													  bool bExplicit = false, const FSourceLocation & = {});
	/** Structured argument encoding is distinct from both display syntax and type-name strings. */
	std::string EncodeTemplateArgument(const FTemplateArgument &);
	bool DecodeTemplateArgument(std::string_view, FTemplateArgument &);
	std::string MakeTemplateInstanceKey(std::string_view DeclarationId, std::string_view OwnerId,
										const std::vector<FTemplateArgument> &);
} // namespace VeroScript

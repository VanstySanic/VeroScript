// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptResources.h"
#include "VeroScriptExternalDependency.h"
#include <functional>
#include <memory>
#include <vector>

namespace VeroScript
{
	class IAnnotationProcessor;
	class FAnnotationProcessorRegistry;

	enum class EAnnotationTarget : std::uint32_t
	{
		Struct = 1,
		Class = 2,
		Interface = 4,
		Enum = 8,
		Field = 16,
		StaticField = 32,
		Method = 64,
		StaticMethod = 128,
		Constructor = 256,
		Destructor = 512,
		Function = 1024,
		GlobalVariable = 2048,
		EnumMember = 4096,
		TypeAlias = 8192, Namespace = 16384, Parameter = 32768, Module = 65536
	};
	constexpr std::uint32_t AnnotationTargetMask(EAnnotationTarget Target)
	{
		return static_cast<std::uint32_t>(Target);
	}
	inline constexpr std::uint32_t AnnotationAllTargets = 131071;
	enum class EAnnotationOperation : std::uint8_t
	{
		AddMethod = 1,
		CompleteMethodBody = 2
	};
	struct FAnnotationParameterView
	{
		std::string Name, TypeName;
		bool bIsRef = false, bIsConst = false, bIsVariadic = false, bHasDefault = false;
		std::string DefaultValueSource;
		std::string CanonicalTypeName;
		std::string Id;
		FDeclMeta Metadata;
	};
	struct FAnnotationTypeBoundView
	{
		std::string TypeParameter, InterfaceTypeName, CanonicalInterfaceTypeName;
	};
	struct FAnnotationTemplateParameterView
	{
		std::string Id, Name, ValueTypeName, DefaultSource;
		bool bValue = false, bHasDefault = false, bEnclosing = false;
	};
	// Owned value snapshots: these views never expose mutable AST pointers.
	struct FAnnotationDeclarationView
	{
		std::string ScopeId, ParentScopeId;
		bool bNested = false, bLocal = false;
		std::string Id, OwnerId, Name, ModulePath, TypeName;
		EAnnotationTarget Target = EAnnotationTarget::Function;
		EMemberAccess Access = EMemberAccess::Public;
		bool bExported = false, bHasBody = false, bVirtual = false, bOverride = false;
		bool bFinal = false, bIsConst = false, bIsStatic = false, bDeleted = false, bPureVirtual = false;
		bool bHasDefault = false;
		bool bFromPackage = false, bGenerated = false;
		bool bIsRef = false, bIsMutable = false, bExplicit = false, bDefaulted = false;
		std::string DefaultValueSource;
		std::vector<std::string> TypeParameters, BaseTypes;
		std::vector<FAnnotationTemplateParameterView> TemplateParameters;
		std::vector<FAnnotationParameterView> Parameters;
		FDeclMeta Metadata;
		FAnnotationSourcePosition Location;
		std::string BodyModulePath;
		std::string CanonicalTypeName;
		std::vector<std::string> CanonicalBaseTypes;
		// Parallel to TypeParameters and BaseTypes respectively. Interface bases
		// have public access; constraints retain both written and canonical names.
		std::vector<std::string> TypeParameterDefaults;
		std::vector<FAnnotationTypeBoundView> TypeBounds;
		std::vector<EMemberAccess> BaseAccess;
		std::string BodySource;
		bool bConstexpr = false;
	};
	struct FAnnotationDependency
	{
		std::string Path, Content;
		bool bExists = false;
	};
	struct FAnnotationContext
	{
		const FAnnotationDeclarationView &Declaration;
		const FDeclarationAnnotation &Annotation;
		const FAnnotationDeclarationView *Owner = nullptr;
		const std::vector<FAnnotationDeclarationView> &Declarations;
		std::function<void()> CheckCancelled;
		// A host supplied reader is optional. Every read (including missing files)
		// is recorded by the core for cache/watch invalidation.
		std::function<bool(std::string_view, std::string &)> ReadDependency;
		std::function<bool(const FResourceRequest&, FResourceData&)> ReadResource;
	};
	struct FGeneratedDeclarationDocument
	{
		std::string FileLabel, Source, GenerationId;
		FAnnotationSourcePosition Origin;
		FAnnotationSourcePosition LookupAnchor;
	};
	struct FAnnotationGenerationOperation
	{
		EAnnotationOperation Kind = EAnnotationOperation::AddMethod;
		std::string TargetId, Source, ProcessorId, GenerationId;
		EMemberAccess Access = EMemberAccess::Public;
		FAnnotationSourcePosition Origin;
	};
	struct FAnnotationProcessorDesc
	{
		std::string Name, ProcessorId, Version, ConfigurationKey;
		std::uint32_t Targets = 0;
		std::uint8_t Operations = 0;
		std::int32_t Order = 0;
		std::function<std::unique_ptr<IAnnotationProcessor>()> Factory;
		bool Cacheable = true;
	};

	struct FAnnotationProcessingOptions
	{
		std::shared_ptr<const FAnnotationProcessorRegistry> Registry;
		std::function<void()> CheckCancelled;
		std::function<bool(std::string_view, std::string &)> ReadDependency;
		std::size_t MaxOperations = 4096, MaxSourceBytes = 16 * 1024 * 1024;
		std::size_t MaxProcessorsPerAnnotation = 64;
	};

	class FAnnotationOutput
	{
	public:
		void SubmitMethod(std::string OwnerId, EMemberAccess Access, std::string MethodSource);
		void CompleteMethodBody(std::string MethodId, std::string BodySource);
		void ReportDiagnostic(FScriptError Diagnostic);
		const std::vector<FAnnotationGenerationOperation> &GetOperations() const
		{
			return Operations;
		}
		const std::vector<FScriptError> &GetDiagnostics() const
		{
			return Diagnostics;
		}

	private:
		std::vector<FAnnotationGenerationOperation> Operations;
		std::vector<FScriptError> Diagnostics;
	};
	class IAnnotationProcessor
	{
	public:
		virtual ~IAnnotationProcessor() = default;
		virtual void Process(const FAnnotationContext &, FAnnotationOutput &) = 0;
	};
	class FAnnotationProcessorRegistry
	{
	public:
		bool Register(FAnnotationProcessorDesc Desc, std::string &Error);
		void Freeze()
		{
			bFrozen = true;
		}
		bool IsFrozen() const
		{
			return bFrozen;
		}
		const std::vector<FAnnotationProcessorDesc> &GetProcessors() const
		{
			return Processors;
		}

	private:
		bool bFrozen = false;
		std::vector<FAnnotationProcessorDesc> Processors;
	};
} // namespace VeroScript

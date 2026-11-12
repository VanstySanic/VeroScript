// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptNative.h"
#include "VeroScriptAst.h"
#include "VeroScriptMethodBinding.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VeroScript
{
	enum class EBinaryOp : std::uint8_t;
	enum class EUnaryOp : std::uint8_t;

	struct FProgram;
	struct FFunctionDecl;
	struct FMethodDecl;
	struct FAssignExpr;
	struct FBinaryExpr;
	struct FUnaryExpr;
	struct FMemberAccessInfo;
	class FNamespaceLookup;
	namespace ModuleVisibility
	{
		class FImportIndex;
	}

	/**
	 * AST -> bytecode compiler for a parsed VeroScript program.
	 */
	class VeroScriptCompiler
	{
		friend struct FProgramPreparationAccess;
		using FDeclarationIndex = std::unordered_map<std::string, std::vector<std::size_t>>;

	public:
		std::unique_ptr<FBytecodeModule> CompilePrepared(const class FPreparedProgram&,
		    const std::vector<FNativeFunction>* = nullptr, const IHostBindingCatalog* = nullptr);
		std::unique_ptr<FBytecodeModule> Compile(const FProgram& Program,
		    const std::vector<FNativeFunction>* Natives = nullptr,
		    const IHostBindingCatalog* Bindings = nullptr);
		const std::vector<FMethodBindingCodegenInfo>& GetMethodBindingCodegenInfo() const
		{
			return MethodBindingCodegenInfo;
		}
		bool HasError() const
		{
			return bHasError || !Diagnostics.empty();
		}
		const FScriptError& GetError() const
		{
			return Error;
		}
		const std::vector<FScriptError>& GetDiagnostics() const
		{
			return Diagnostics;
		}
		// Collect independent unit failures (functions/methods) up to MaxErrors.
		void SetCollectMultipleErrors(bool bEnabled, std::size_t MaxErrors = 32);
		// Canonical declaration types, including aliases and using directives.
		// Standalone emitters must reject runtime-only types even in unused signatures.
		void SetCollectRuntimeMemoryTypes(bool bEnabled)
		{
			bCollectRuntimeMemoryTypes = bEnabled;
		}
		bool UsesRuntimeMemoryTypes() const
		{
			return bUsesRuntimeMemoryTypes;
		}

	public:
		void SetConstantEvaluationLimits(const FConstantEvaluationLimits& Limits)
		{
			ConstantEvaluationLimits = Limits;
			bConstantEvaluationLimitsExplicit = true;
		}

	private:
		struct FSourceMethodContext
		{
			const FMethodDecl* Method = nullptr;
			std::string OwnerLinkage;
			std::vector<std::string> OwnerParameters;
		};
		struct FLambdaOwnerIdentity
		{
			std::uint64_t ParentIdentity = 0;
			std::uint64_t SemanticIdentity = 0;
		};


		FConstantEvaluationLimits ConstantEvaluationLimits;
		bool bConstantEvaluationLimitsExplicit = false;
		mutable std::shared_ptr<FConstantEvaluationState> ConstantEvaluationState;
		std::shared_ptr<struct FConstantBranchState> ConstantBranchState;
		bool ValidateConstexprFunction(const std::string&, const std::vector<FFunctionParam>&, const FBlockStmt&, const std::string&);
		bool LowerConstantBranches(const FBlockStmt&, std::unique_ptr<FBlockStmt>&,
		    const std::vector<FFunctionParam>* Parameters = nullptr);
		// Deferred default clash: (display name, first iface, second iface, left method*).
		using FDeferredIfaceDefaultConflict =
		    std::tuple<std::string, std::string, std::string, const struct FMethodDecl*>;

		struct FLocalConstantInitializer
		{
			const FExpr* Expression = nullptr;
			std::string TypeName, Identity, ModulePath, Namespace, Owner, Access, FunctionName;
			std::unordered_map<std::string, std::string> Bindings;
			std::vector<std::unordered_map<std::string, std::string>> EnumScopes;
			std::size_t ScopeCount = 0;
			std::size_t GlobalSlotBound = 0;
			std::uint16_t SlotBound = 0;
		};

		struct FScope
		{
			/**
			 * `for (ref pair p : m)` - KeySlot holds key copy; RefSlot holds FMapEntryRef.
			 */
			struct FMapEntryViewBinding
			{
				std::uint16_t KeySlot = 0;
				std::uint16_t RefSlot = 0;
			};

			std::unordered_map<std::string, std::uint16_t> Locals;
			std::unordered_map<std::string, std::string> StaticLocals;
			std::unordered_map<std::string, std::string> LocalTypes;
			std::unordered_map<std::string, std::string> EnumConstants;
			std::unordered_set<std::string> LocalConsts;
			std::unordered_map<std::string, FTemplateConstant> TemplateConstants;
			std::unordered_map<std::string, FLocalConstantInitializer> ConstantInitializers;
			std::unordered_set<std::string> LocalRefs;
			std::unordered_map<std::string, FMapEntryViewBinding> LocalMapEntryViews;
		};

		struct FLocalStaticConstantBinding
		{
			std::optional<FTemplateConstant> Value;
			FLocalConstantInitializer Initializer;
			std::shared_ptr<const FExpr> OwnedExpression;
		};

		struct FEnclosingConstantScope
		{
			std::string FunctionName;
			const std::vector<FScope>* Scopes = nullptr;
		};

		struct FLoopContext
		{
			std::vector<std::size_t> BreakPatches;
			std::vector<std::size_t> ContinuePatches;
			bool bSwitch = false;
		};

		struct FLabelInfo
		{
			std::int32_t Offset = -1;
			int ScopeDepth = 0;
			std::vector<std::uint32_t> ExceptionScopePath;
			FSourceLocation Location;
		};

		struct FGotoPatch
		{
			std::string Name;
			std::size_t OperandOffset = 0;
			// Bytecode IP of the Goto's Jmp opcode (before operand).
			std::int32_t GotoIp = 0;
			int ScopeDepth = 0;
			std::vector<std::uint32_t> ExceptionScopePath;
			FSourceLocation Location;
		};

		struct FLocalInitMarker
		{
			std::string Name;
			std::int32_t Offset = 0;
			int ScopeDepth = 0;
		};

		/**
		 * Top-level using/typedef alias (module visibility checked on expand).
		 */
		struct FUserTypeAlias
		{
			std::string TargetTypeName;
			std::string ModulePath;
			std::vector<std::string> EnclosingParameterNames;
			bool bExported = false;
		};

		struct FResolvedScriptOperator
		{
			const FMethodDecl* Method = nullptr;
			const FFunctionDecl* Function = nullptr;
			std::string OwnerType;
			std::string OwnerLinkage;
			std::string ReturnType;
			std::vector<std::string> ParamTypes;
			std::vector<std::string> GenericTypes;
		};

		class FEmitter
		{
		public:
			FEmitter(std::vector<std::uint8_t>& InCode, std::vector<FDebugLine>* InDebugLines = nullptr,
			    const FSourceLocation* InLocation = nullptr, std::vector<std::string>* InDebugFiles = nullptr,
			    const std::vector<FNativeFunction>* InNatives = nullptr,
			    std::vector<FBytecodeNativeReference>* InNativeReferences = nullptr);

			void Emit(EOpcode Opcode);
			void EmitU8Operand(EOpcode Opcode, std::uint8_t Value);
			void EmitU16Operand(EOpcode Opcode, std::uint16_t Value);
			void EmitU16U16Operands(EOpcode Opcode, std::uint16_t First, std::uint16_t Second);
			std::size_t EmitI32Operand(EOpcode Opcode, std::int32_t Value);
			void EmitCall(std::uint16_t FunctionIndex, std::uint8_t ArgumentCount);
			void EmitCallMethod(std::uint16_t MethodNameIndex, std::uint8_t ArgumentCount);
			void EmitCallBoundMethod(std::uint16_t FunctionIndex, std::uint8_t ArgumentCount);
			void EmitCallBuiltin(EBuiltinId Builtin, std::uint8_t ArgumentCount);
			void EmitCallNative(std::uint16_t NativeIndex, std::uint8_t ArgumentCount);
			void EmitMakeClosure(std::uint16_t FunctionIndex, std::uint8_t CaptureCount);
			void EmitCallIndirect(std::uint8_t ArgumentCount);
			void PatchI32(std::size_t OperandOffset, std::int32_t Value);
			std::int32_t CurrentOffset() const;
			void AppendCode(
			    const std::vector<std::uint8_t>& Bytes,
			    const std::vector<FDebugLine>* SourceLines = nullptr);

		private:
			void RecordOpcodeLocation();
			std::uint16_t InternDebugFile(std::string_view FileLabel);

			const std::vector<FNativeFunction>* Natives = nullptr;
			std::vector<FBytecodeNativeReference>* NativeReferences = nullptr;
			std::vector<std::uint8_t>& Code;
			std::vector<FDebugLine>* DebugLines = nullptr;
			const FSourceLocation* EmitLocation = nullptr;
			std::vector<std::string>* DebugFiles = nullptr;
		};

		void RecordMemoryTypeUses(const FProgram& Program);

		FEmitter MakeEmitter();

		FEmitter MakeEmitter(std::vector<std::uint8_t>& Code, std::vector<FDebugLine>* DebugLines);

		void ReportError(std::string Message, const FSourceLocation& Location = {}) const;

		void ReportErrorId(
		    std::string_view MessageId,
		    const FSourceLocation& Location = {},
		    std::initializer_list<FDiagnosticArg> Args = {}) const;

		void CommitCurrentError() const;

		bool RecoverAfterCompileUnitError();

		// Start-of-pipeline state and soft standard-library declarations. Kept out of
		// Compile() so the orchestration path and its phase boundaries remain reviewable.
		void IndexSourceDeclarations(const FProgram& Program);
		void ResetCompilationState(
		    const FProgram& Program,
		    const std::vector<FNativeFunction>* Natives,
		    const IHostBindingCatalog* Bindings,
		    const ModuleVisibility::FImportIndex& ImportIndex);

		bool RegisterBuiltinTemplates(const FProgram& Program);
		bool RegisterSourceMetadataTemplates(const FProgram& Program);

		void FinalizePackageSurface(const FProgram& Program);
		void CollectTemplateDeclarationMetadata(const FProgram& Program);
		void ApplyTemplateInstanceMetadata(FTemplateInstanceInfo& Instance, const FDeclMeta* Declaration);
		struct FTemplateMetadataBinding
		{
			std::string Id;
			std::vector<std::string> Names, EnclosingNames;
		};

		std::uint16_t AddString(std::string_view Value);

		std::uint16_t AddWString(std::u16string_view Value);

		std::uint16_t AddConstant(const FBytecodeConstant& Constant);

		std::uint16_t AddGlobal(const std::string& Name);

		static const std::vector<std::size_t>& FindDeclarationCandidates(
		    const FDeclarationIndex& Index, const std::string& Linkage);
		std::uint16_t FindRegisteredTypeIndex(const std::string& TypeName) const;
		std::uint16_t FindOrAddType(const std::string& TypeName);
		void PopulateSourceTypeScope(const FTypeDeclarationScope& Declaration,
		    const std::vector<std::string>& Arguments, FTypeDeclarationScope& Scope,
		    std::vector<FNestedTypeDeclaration>& NestedTypes) const;

		bool ValidateFieldTypeName(const std::string& TypeName, const FSourceLocation& Location);

		std::uint16_t FindFunctionIndex(const std::string& Name) const;

		bool CanAccessNativeBinding(const FBindingOrigin&, const std::string& TypeName = {}) const;
		const FNativeFunction* FindNative(const std::string& Name, std::uint16_t& OutIndex) const;
		const FNativeFunction* FindNativeForCall(const std::string& Name, FExprArgumentView Arguments,
		    std::uint16_t& OutIndex, const FSourceLocation& Location, bool bReportErrors = true,
		    int ReceiverMode = -1, bool bReceiverIsConst = false) const;

		bool ResolveGlobal(const std::string& Name, std::uint16_t& OutSlot) const;

		std::uint16_t FindFieldIndex(std::uint16_t TypeIndex, const std::string& FieldName) const;

		std::uint16_t AllocateLocal(const std::string& Name);

		bool ResolveLocal(const std::string& Name, std::uint16_t& OutSlot) const;
		std::string ResolveLocalStaticLinkageName(const std::string& Name) const;
		std::string ResolveLocalEnumConstantLinkageName(const std::string& Name) const;

		bool ResolveThisField(const std::string& Name, std::uint16_t& OutFieldIndex) const;

		void BeginScope();

		void EndScope();

		static std::string TypeNameLeaf(const std::string& Name);

		bool CompileLiteral(const struct FExpr& Expression);

		bool CompileExpression(const struct FExpr& Expression);

		bool CompileExpressionImpl(const struct FExpr& Expression);

		bool CompilePrimaryExpression(const struct FExpr& Expression);

		bool CompileBinaryExpression(const struct FExpr& Expression);

		bool CompileAssignmentExpression(const struct FExpr& Expression);

		bool CompileMemberAccessExpression(const struct FExpr& Expression);

		bool CompileMethodCallExpression(const struct FExpr& Expression);

		bool CompileCallExpression(const struct FExpr& Expression);

		bool CompileInitializerExpression(const struct FExpr& Expression);

		bool CompileFormatFamilyCall(const struct FCallExpr& Call, FEmitter& Emitter);
		bool CompileMemoryCall(const struct FCallExpr& Call, EBuiltinId Builtin, FEmitter& Emitter);
		bool CompileTypedStandardLibraryCall(const struct FCallExpr& Call, EBuiltinId Builtin, FEmitter& Emitter);
		bool CompileVariantCall(const struct FCallExpr& Call, FEmitter& Emitter);
		std::string CanonicalVariantTypeName(const std::string& TypeName) const;
		bool CompileStandardLibraryOptionalCall(const struct FCallExpr& Call, EBuiltinId Builtin, FEmitter& Emitter);
		bool IsMethodBindingFactoryDeclaration(const FFunctionDecl& Function) const;
		bool ResolveMethodBindingFactoryType(const struct FCallExpr& Call,
		    std::string& OutType, std::string& OutReceiver, std::string& OutFunctionType) const;
		bool CompileMethodBindingFactory(const struct FCallExpr& Call, FEmitter& Emitter);
		bool CompileBoundMethodInvoke(const struct FCallExpr& Call, FEmitter& Emitter);

		bool CompileStatement(const struct FStmt& Statement);
		bool IsForbiddenRuntimeCapture(const std::string& Name) const;
		std::string FindLexicalMemberOwner(const std::string& Name, bool& OutForbidden) const;
		std::unique_ptr<FMethodCallExpr> MakeLexicalStaticCall(const FCallExpr& Call) const;
		struct FSourceTypeContext
		{
			const FTypeDeclarationScope* Scope = nullptr;
			std::shared_ptr<const FDeferredSyntax> Context;
		};
		const FSourceTypeContext* FindSourceTypeContext(const std::string& Linkage) const;
		bool CheckScopedTypeRequirements(std::string_view ScopeId = {}, std::string_view FunctionOwner = {},
		    const std::unordered_map<std::string, std::string>& Bindings = {});
		std::unordered_map<std::string, FSourceTypeContext> SourceTypeContexts;
		FDeclarationIndex SourceStructIndices;
		FDeclarationIndex SourceClassIndices;
		FDeclarationIndex SourceInterfaceIndices;
		FDeclarationIndex SourceStructNames, SourceClassNames, SourceInterfaceNames, SourceEnumNames;
		// Positions are rebuilt for each immutable input program alongside the
		// existing source-name indices. Runtime metadata queries keep all filters.
		FDeclarationIndex SourceTypeScopeIndices[3];
		FDeclarationIndex SourceTypeChildIndices[4];
		std::unordered_map<const FDeclMeta*, FSourceMethodContext> SourceMethodContexts;
		std::unordered_map<std::string, std::vector<const FScopedTypeUse*>> ScopedUsesByType, ScopedUsesByFunction;
		void EmitReflectionScopeBinding(FEmitter& Emitter, std::uint16_t TypeIndex);
		const struct FEnumDecl* FindEnumDeclaration(const std::string& Name) const;
		std::string EnsureEnumMetadata(const struct FEnumDecl& Declaration,
		    const std::vector<std::string>& Arguments) const;

		bool CompileBlock(const struct FBlockStmt& Block, const bool bPreserveScope = false);

		bool CompileFunction(
		    const std::string& Name,
		    const std::string& ReturnType,
		    const std::vector<struct FFunctionParam>& Parameters,
		    const struct FBlockStmt& Body,
		    const bool bIsMethod,
		    std::uint16_t OwnerTypeIndex,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location,
		    bool bIsVirtual = false,
		    const std::vector<FBytecodeCapture>* CaptureLocals = nullptr,
		    bool bIsConstMethod = false,
		    const FDeclMeta* DeclMeta = nullptr,
		    const std::string* LexicalAccessFunctionLinkage = nullptr,
		    bool bAllowImplicitZeroReturn = false, bool bConstexpr = false);

		bool CompileLambdaExpression(const struct FLambdaExpr& Lambda, FEmitter& Emitter,
		    bool bPreserveBindingIdentity = false);
		bool CompileLocalStatic(const struct FVarDeclStmt& Declaration, FEmitter& Emitter);
		std::uint64_t ComputeCallableSemanticIdentity(const struct FStmt* Body, const struct FExpr* Expression = nullptr) const;
		std::uint64_t ComputeLambdaParentIdentity(const std::string& Owner);
		struct FLambdaInitializerContext
		{
			explicit FLambdaInitializerContext(VeroScriptCompiler& Compiler);
			~FLambdaInitializerContext();
			VeroScriptCompiler& Compiler;
			const struct FStmt* PreviousStatement;
			const struct FExpr* PreviousExpression;
			const struct FFieldDecl* PreviousField;
		};

		bool CollectLambdaCaptures(
		    const struct FLambdaExpr& Lambda,
		    std::vector<std::pair<std::string, std::string>>& OutCaptures,
		    bool& OutNeedsThis,
		    const FSourceLocation& Location);

		const struct FFunctionDecl* FindUniqueFreeFunction(
		    const std::string& Name,
		    const std::string& ReferenceModulePath) const;

		std::vector<std::string> BuildFreeFunctionLookupNames(
		    const std::string& Name,
		    const std::string& ReferenceModulePath) const;

		std::string MakeFunctionTypeFromDecl(const struct FFunctionDecl& Function) const;

		bool IsLocalRef(const std::string& Name) const;

		bool TryGetMapEntryViewBinding(
		    const std::string& Name,
		    FScope::FMapEntryViewBinding& OutBinding) const;

		/**
		 * If Expression is entry-view `p` / `p.first` / `p.second` / `p.second.x...`,
		 * fills Binding and Members (empty for bare `p`; otherwise fields from pair root).
		 */
		bool TryGetMapEntryViewMemberPath(
		    const struct FExpr& Expression,
		    FScope::FMapEntryViewBinding& OutBinding,
		    std::vector<std::string>& OutMembers) const;

		/**
		 * Leave a FieldRef on the stack for `p.second.field...` via MapEntryRef + MakeFieldRef
		 * (no StoreField on a loaded handle - write-through is explicit).
		 * FieldsBeyondSecond must be non-empty.
		 */
		bool EmitMapEntryViewValueFieldRef(
		    FEmitter& Emitter,
		    const FScope::FMapEntryViewBinding& Binding,
		    const std::vector<std::string>& FieldsBeyondSecond);

		// Materialize value pair from entry-view local (key + map value).
		bool EmitMaterializeMapEntryPair(
		    FEmitter& Emitter,
		    const FScope::FMapEntryViewBinding& Binding,
		    const std::string& PairTypeName,
		    const FSourceLocation& Location);

		// Reserve a FunctionIndices slot so later bodies can CallBoundMethod / Call by index.
		bool EnsureFunctionStub(
		    const std::string& Name,
		    const std::string& ReturnType,
		    std::uint8_t ParamCount,
		    bool bIsMethod,
		    std::uint16_t OwnerTypeIndex,
		    bool bIsVirtual,
		    const FSourceLocation& Location,
		    bool bIsConstMethod = false);

		void SetFunctionDeclarationSignatureMetadata(
		    FBytecodeFunction& Function,
		    const std::string& ReturnType,
		    const std::vector<struct FFunctionParam>& Parameters,
		    const std::string& OwnerModulePath);

		bool EnsureMethodDeclarationStub(
		    const std::string& Name,
		    const FMethodDecl& Method,
		    std::uint16_t OwnerTypeIndex,
		    bool bIsVirtual,
		    const std::string& OwnerModulePath);

		bool RegisterStaticMethodDeclarations(
		    const std::string& OwnerTypeName,
		    const std::string& OwnerModulePath,
		    const std::vector<struct FMethodDecl>& Methods);

		bool EnsurePureVirtualFunctionDeclaration(
		    const std::string& Name,
		    const std::string& ReturnType,
		    const std::vector<struct FFunctionParam>& Parameters,
		    std::uint16_t OwnerTypeIndex,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location,
		    bool bIsConstMethod,
		    EMemberAccess Access,
		    const FDeclMeta& DeclMeta);

		bool EnsureDeletedFunctionDeclaration(
		    const std::string& Name,
		    const std::string& ReturnType,
		    const std::vector<struct FFunctionParam>& Parameters,
		    bool bIsMethod,
		    std::uint16_t OwnerTypeIndex,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location,
		    bool bIsConstMethod,
		    const FDeclMeta& DeclMeta);

		bool CompileConstructor(
		    const std::string& TypeLinkageName,
		    const std::string& TypeSimpleName,
		    const struct FConstructorDecl& Constructor,
		    const std::vector<struct FFieldDecl>& OwnFields,
		    const std::vector<std::string>& DeclaredBaseTypeNames,
		    std::uint16_t OwnerTypeIndex,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location);

		bool CompileDestructor(
		    const std::string& TypeLinkageName,
		    const std::string& TypeSimpleName,
		    const struct FDestructorDecl& Destructor,
		    std::uint16_t OwnerTypeIndex,
		    const std::string& OwnerModulePath);

		// Resolve goto patches against LabelTable / LocalInitMarkers for the current function.
		bool ResolveAndPatchGotos();

		// Peephole-fuse local increment/add patterns in a compiled function's bytecode.
		void OptimizeFunctionBytecode(FBytecodeFunction& Function);

		// Apply the same peephole fusion to module-level InitCode / InitDebugLines.
		void OptimizeInitBytecode();

		// After all functions exist, inline trivial binary Call sites module-wide.
		void OptimizeModuleCalls();

		bool CompileDefaultValue(const struct FExpr& Expression, FBytecodeConstant& OutConstant);

		bool TryCompileDefaultConstant(const struct FExpr& Expression, FBytecodeConstant& OutConstant);

		// Params without defaults/variadic (prefix length); trailing defaults + optional `T...` allowed after.
		static std::size_t CountRequiredParams(const std::vector<struct FFunctionParam>& Parameters);

		static bool IsVariadicParamList(const std::vector<struct FFunctionParam>& Parameters);

		static std::size_t FixedParamCount(const std::vector<struct FFunctionParam>& Parameters);

		static bool ParamsAcceptArgCount(
		    const std::vector<struct FFunctionParam>& Parameters,
		    std::size_t ArgCount);

		static std::string MakeDynamicArrayTypeName(const std::string& ElementTypeName);

		// Push literal defaults for omitted trailing args (+ empty array for trailing `T...`).
		bool EmitTrailingDefaultArguments(
		    FEmitter& Emitter,
		    const std::vector<struct FFunctionParam>& Parameters,
		    std::size_t ProvidedCount,
		    const std::string& DeclModulePath,
		    const std::unordered_map<std::string, std::string>& Subst,
		    const FSourceLocation& Location);

		// Pack call-site rest args into `array<T>` (or pass through a single `array<T>`).
		bool EmitVariadicRestArgument(
		    FEmitter& Emitter,
		    const std::vector<struct FFunctionParam>& Parameters,
		    const class FExprArgumentView Arguments,
		    std::size_t FixedCount,
		    const std::string& DeclModulePath,
		    const std::unordered_map<std::string, std::string>& Subst,
		    const FSourceLocation& Location);

		bool TryEvalSwitchCaseConstant(
		    const struct FExpr& Expression,
		    std::int64_t& OutValue,
		    const FSourceLocation& Location);

		bool TryEvalSwitchCaseStringConstant(
		    const struct FExpr& Expression,
		    std::string& OutValue,
		    const FSourceLocation& Location);

		bool TryEvalSwitchCaseWStringConstant(
		    const struct FExpr& Expression,
		    std::u16string& OutValue,
		    const FSourceLocation& Location);

		bool CompileFieldInitExpr(
		    const struct FExpr& Expression,
		    const std::string& TargetTypeName,
		    std::vector<std::uint8_t>& OutCode,
		    std::vector<FDebugLine>* OutDebugLines = nullptr);

		static bool ExpressionCrossesSourceLine(
		    const struct FExpr& Root,
		    const FSourceLocation& SourceLocation);

		bool CompileNativeArrayInit(
		    const std::string& TypeName,
		    const struct FArrayInitExpr& Init,
		    FEmitter& Emitter,
		    const FSourceLocation& Location);

		bool CompileContainerListInit(
		    const std::string& TypeName,
		    const struct FArrayInitExpr& Init,
		    FEmitter& Emitter,
		    const FSourceLocation& Location);

		bool CompileMapInit(
		    const std::string& TypeName,
		    const struct FMapInitExpr& Init,
		    FEmitter& Emitter,
		    const FSourceLocation& Location);

		bool CompileFieldDefault(const struct FFieldDecl& Field, FBytecodeFieldDesc& OutFieldDesc);

		// Compile defaults after a closed owner and its callable declarations exist.
		bool CompileOwnFieldDefaults(
		    const std::string& OwnerTypeName,
		    std::uint16_t OwnerIndex,
		    const std::vector<struct FFieldDecl>& Fields,
		    bool bInterface = false);

		void PatchRelativeJump(FEmitter& Emitter, std::size_t OperandOffset);

		void CloseLoop(FEmitter& Emitter, std::size_t JumpIfFalseOperand, bool bHasConditionJump);

		const struct FFunctionDecl* FindFunctionDeclForReference(
		    const std::string& Name,
		    const std::string& ReferenceModulePath) const;

		// Generic free function selected by explicit type-argument prefix and
		// specialized value-parameter compatibility.
		const struct FFunctionDecl* FindGenericFreeFunctionDecl(
		    const std::string& Name,
		    const std::string& ReferenceModulePath,
		    const std::vector<std::string>& TypeArgs,
		    const std::vector<std::string>& ArgTypeNames,
		    bool* OutAmbiguous = nullptr) const;

		/**
		 * Select generic free function for a call (explicit TypeArgs or unique inferable).
		 * Reports errors when ambiguous / missing.
		 */
		bool ResolveGenericFreeFunctionForCall(
		    const std::string& Name,
		    const std::vector<std::string>& TypeArgs,
		    const std::vector<std::string>& ArgTypeNames,
		    const FFunctionDecl*& OutFunction,
		    const FSourceLocation& Location,
		    bool bExplicitTemplateArguments = false);

		// Overload resolve for non-generic free functions (param-type scoring).
		bool ResolveFunctionForCall(
		    const std::string& Name,
		    const std::vector<std::string>& ArgTypeNames,
		    const FFunctionDecl*& OutFunction,
		    const FSourceLocation& Location);

		// Select best free-function overload without reporting errors.
		const FFunctionDecl* SelectFreeFunctionOverload(
		    const std::string& Name,
		    const std::vector<std::string>& ArgTypeNames,
		    bool* OutAmbiguous = nullptr,
		    bool bAllowTypeMismatchCandidate = true) const;

		// Infer concrete type args for a generic free function from value-arg types.
		bool TryInferGenericFunctionTypeParams(
		    const struct FFunctionDecl& Function,
		    const std::vector<std::string>& ArgTypeNames,
		    std::vector<std::string>& OutConcreteTypes) const;

		bool TryInferGenericFunctionTypeParams(
		    const struct FFunctionDecl& Function,
		    const std::vector<std::string>& ArgTypeNames,
		    const std::vector<std::string>& ExplicitTypeArgs,
		    std::vector<std::string>& OutConcreteTypes,
		    const std::unordered_map<std::string, std::string>* EnclosingBindings = nullptr) const;

		int ScoreSpecializedGenericCall(
		    const struct FFunctionDecl& Function,
		    const std::vector<std::string>& ArgTypeNames,
		    const std::vector<std::string>& ConcreteTypes) const;

		// Silent `where` bound check for overload resolution (no diagnostics).
		bool SatisfiesTypeParamBounds(
		    const std::vector<std::string>& TypeParams,
		    const std::vector<struct FTypeParamBound>& Bounds,
		    const std::vector<std::string>& ConcreteTypes,
		    const std::string& ModulePath) const;

		// Select generic free-function overload with inferred type arguments.
		const FFunctionDecl* SelectGenericFreeFunctionOverload(
		    const std::string& Name,
		    const std::vector<std::string>& ArgTypeNames,
		    std::vector<std::string>& OutConcreteTypes,
		    bool* OutAmbiguous = nullptr,
		    bool bIncludeBoundRejected = false) const;

		// Uniform-call fallback: `value.f(args)` may call visible `f(value, args)`
		// only when no real member, host member, or built-in member matches.
		bool HasExtensionMethodCandidate(
		    const std::string& Name,
		    const std::vector<std::string>& TypeArgs,
		    const std::vector<std::string>& ArgTypeNames) const;

		// Non-generic first, then inferred generic; for operator overload resolution.
		bool ResolveFreeOperatorOverload(
		    const std::string& Name,
		    const std::vector<std::string>& ArgTypeNames,
		    const FFunctionDecl*& OutFunction,
		    std::vector<std::string>& OutGenericConcreteTypes,
		    bool* OutAmbiguous = nullptr,
		    bool bAllowTypeMismatchCandidate = false) const;

		std::vector<std::string> SpecializedFreeFunctionParamTypes(
		    const struct FFunctionDecl& Function,
		    const std::vector<std::string>& GenericConcreteTypes) const;

		static bool FreeFunctionNameMatches(
		    const std::string& RequestedName,
		    const std::string& DeclName);

		std::string MakeFreeFunctionLinkageName(const struct FFunctionDecl& Function) const;

		bool IsClassOrStructType(const std::string& TypeName) const;

		// Class, struct, or interface - types that may declare operator methods.
		bool TypeSupportsOperatorOverload(const std::string& TypeName) const;

		const char* BinaryOperatorMethodName(EBinaryOp Op) const;

		// True compound-assign method name (operator+=); null if Op has no compound form.
		const char* CompoundOperatorMethodName(EBinaryOp Op) const;

		const char* UnaryOperatorMethodName(EUnaryOp Op) const;

		// Mirror relational op for a>b <-> b<a synthesis; null if Op has no mirror.
		std::optional<EBinaryOp> MakeMirrorBinaryOp(EBinaryOp Op) const;

		std::string ConversionOperatorMethodName(const std::string& TargetTypeName) const;
		// Operator syntax implicitly binds references, unlike ordinary explicit-ref calls.
		bool PreferOperatorReferenceOverload(const std::vector<std::string>& Arguments,
		    const std::vector<std::string>& Left, const std::vector<std::string>& Right) const;
		std::string EncodeOperatorArgType(const FExpr& Argument) const;
		bool CompileOperatorArgument(FEmitter& Emitter, const FExpr& Argument,
		    const std::string& ParamType, const FSourceLocation& Location);
		bool ResolveScriptOperator(const std::string& Name, const FExpr& Receiver,
		    const std::vector<const FExpr*>& Arguments, FResolvedScriptOperator& Out,
		    bool& OutAmbiguous) const;
		bool CompileScriptOperator(FEmitter& Emitter, const std::string& Name,
		    const FExpr& Receiver, const std::vector<const FExpr*>& Arguments,
		    const FResolvedScriptOperator& Resolved, const FSourceLocation& Location);
		bool TryCompileOverloadedAssignment(FEmitter& Emitter, const FAssignExpr& Assign,
		    const FSourceLocation& Location);
		bool TryResolveOverloadedAssignmentType(const FAssignExpr& Assign,
		    std::string& OutTypeName) const;
		bool TryCompileOverloadedBinary(
		    FEmitter& Emitter,
		    const FBinaryExpr& Binary,
		    const FSourceLocation& Location);

		bool TryCompileOverloadedUnary(
		    FEmitter& Emitter,
		    const FUnaryExpr& Unary,
		    const FSourceLocation& Location);

		// Emit member operator call; stack must already be [receiver, args...].
		bool EmitMemberOperatorCall(
		    FEmitter& Emitter,
		    const std::string& OwnerTypeName,
		    const std::string& MethodName,
		    const FMethodDecl& MethodDecl,
		    const std::string& OwnerLinkage,
		    std::uint8_t ArgumentCount,
		    const FSourceLocation& Location,
		    bool bArgumentsCopied = false);
		// Emit free-function operator; stack must already be [args...].
		bool EmitFreeOperatorCall(
		    FEmitter& Emitter,
		    const FFunctionDecl& Function,
		    std::uint8_t ArgumentCount,
		    const FSourceLocation& Location,
		    const std::vector<std::string>* GenericConcreteTypes = nullptr,
		    bool bArgumentsCopied = false);
		/**
		 * Clone by-value struct/T[N] args sitting on the top of the stack.
		 * Stack layout: [...below..., arg0, arg1, ..., argN-1] with N == ArgumentCount.
		 * ParamTypes[i] may be "ref T" / "const ref T" (those are not cloned).
		 */
		void EmitClonePassByValueArgs(
		    FEmitter& Emitter,
		    const std::vector<std::string>& ParamTypes,
		    std::uint8_t ArgumentCount);

		bool TryEmitOverloadedCompoundOp(
		    FEmitter& Emitter,
		    EBinaryOp Op,
		    const std::string& LeftType,
		    const std::string& RightType,
		    const FSourceLocation& Location,
		    std::string* OutResultTypeName = nullptr,
		    bool bRightIsConst = false);

		/**
		 * Overloaded prefix/postfix ++/-- (member or free). On success emits the full
		 * assign-expression value; does not fall through to +=/-= 1.
		 */
		bool TryEmitOverloadedIncDec(
		    FEmitter& Emitter,
		    const struct FAssignExpr& Assign,
		    const std::string& TargetTypeName,
		    const FSourceLocation& Location);

		bool TryResolveOverloadedBinaryType(
		    const FBinaryExpr& Binary,
		    std::string& OutTypeName,
		    const std::string* KnownLeftType = nullptr,
		    const std::string* KnownRightType = nullptr) const;

		bool TryResolveOverloadedUnaryType(
		    const FUnaryExpr& Unary,
		    std::string& OutTypeName) const;

		bool TryResolveVariableTypeName(const std::string& Name, std::string& OutTypeName) const;

		bool IsConstBinding(const std::string& Name) const;

		bool FindFieldIsConstOnOwnerType(
		    const std::string& OwnerTypeName,
		    const std::string& FieldName,
		    bool& OutIsConst,
		    bool* OutIsMutable = nullptr) const;

		bool IsConstAssignTarget(const struct FExpr& Target) const;

		bool IsKnownContainerMutator(const std::string& MethodName) const;

		// Expected arity for known container methods; -1 if not a container method name.
		int ExpectedContainerMethodArgCount(const std::string& MethodName) const;

		bool IsLenCompatibleTypeName(const std::string& TypeName) const;

		bool IsNumericTypeName(const std::string& TypeName) const;

		// Instance receivers borrow addressable subobjects; value indexing still
		// materializes a copy when the index expression itself is consumed by value.
		bool CompileBorrowedReceiver(FEmitter& Emitter, const FExpr& Receiver);
		bool CompileRefArgument(
		    FEmitter& Emitter,
		    const struct FExpr& Argument,
		    const FSourceLocation& Location,
		    bool bMutableRef);

		bool CanBindImplicitRefArgument(
		    const struct FExpr& Argument,
		    bool bMutableRef) const;

		/**
		 * Emit GetIndexRef / GetMapEntryRef chain for `a[i]` / `m[k]` / `m[k][i]` / `obj.field[i]`.
		 * Roots: local / global / this-field identifier, or MemberAccess (MakeFieldRef).
		 * Non-local identifier roots are seeded via Make*Ref + BindLocal.
		 * Leaves a final IndexRef or MapEntryRef on the stack. Intermediates are BindLocal temps.
		 * Supports array/deque/native/script-args (int index) and map/ordered_map (typed keys).
		 */
		bool EmitGetIndexRefChain(
		    FEmitter& Emitter,
		    const struct FExpr& IndexExpression,
		    const FSourceLocation& Location,
		    bool bBorrow = true);

		/**
		 * True when EmitGetIndexRefChain can alias the expression (no diagnostics).
		 * @param bAllowMap When false (assignment write-back), map/ordered_map layers are
		 *        excluded so missing keys continue to use SetIndex insert semantics.
		 */
		bool CanEmitGetIndexRefChain(
		    const struct FExpr& IndexExpression,
		    bool bAllowMap = true) const;

		bool ValidateRefArgumentType(
		    const std::string& ParamTypeName,
		    const struct FExpr& Argument,
		    const FSourceLocation& Location);

		const FExpr& UnwrapRefOperand(const struct FExpr& Expression) const;

		bool FindFieldTypeOnOwnerType(
		    const std::string& OwnerTypeName,
		    const std::string& FieldName,
		    std::string& OutFieldTypeName) const;

		bool LookupMemberAccess(
		    const std::string& OwnerTypeLinkage,
		    const std::string& MemberName,
		    bool bIsMethod,
		    FMemberAccessInfo& Out,
		    int Depth = 0,
		    std::size_t ArgCount = static_cast<std::size_t>(-1),
		    const FMethodDecl* PreferMethod = nullptr) const;

		bool ValidateMemberAccess(
		    const FSourceLocation& Location,
		    const std::string& OwnerTypeName,
		    const std::string& MemberName,
		    bool bIsMethod,
		    std::size_t ArgCount = static_cast<std::size_t>(-1),
		    const FMethodDecl* PreferMethod = nullptr) const;

		bool IsCurrentFunctionFriendOf(
		    const FMemberAccessInfo& Info,
		    const std::string& ReceiverTypeLinkage) const;

		// Access-check a bare this-field name while compiling a method/ctor body.
		bool ValidateThisFieldAccess(
		    const FSourceLocation& Location,
		    const std::string& FieldName) const;

		bool TryResolveExprTypeName(const struct FExpr& Expression, std::string& OutTypeName) const;

		bool TryResolveExprRawTypeName(const struct FExpr& Expression, std::string& OutTypeName) const;

		bool TryMatchTypeParam(
		    const std::string& ParamType,
		    const std::string& ArgType,
		    const std::string& TypeParam,
		    std::string& OutConcrete) const;

		bool TryResolveIndexElementTypeName(
		    const struct FExpr& ContainerExpr,
		    std::string& OutElementTypeName) const;

		const struct FMethodDecl* FindMethodDeclForReference(
		    const std::string& OwnerTypeName,
		    const std::string& MethodName,
		    std::size_t ArgCount = static_cast<std::size_t>(-1)) const;

		/**
		 * First declaration-order match of MethodName on OwnerTypeName / bases / interfaces.
		 * When ArgCount != npos, only methods with that arity are considered (arity overloads).
		 */
		bool FindMethodDeclOwnerLinkage(
		    const std::string& OwnerTypeName,
		    const std::string& MethodName,
		    std::string& OutOwnerLinkageName,
		    const struct FMethodDecl*& OutMethod,
		    std::size_t ArgCount = static_cast<std::size_t>(-1)) const;

		/**
		 * Resolve method by name+argument types on OwnerTypeName / bases.
		 * ArgTypeNames size is arity; empty entries mean unknown (arity-only soft match).
		 * Reports compile error and returns false when multiple unrelated bases provide the same
		 * signature and the static type does not declare the method.
		 * When bAllowTypeMismatchCandidate is false, arity-only type-mismatch (-2) candidates
		 * are ignored (needed for operator resolve so free overloads can win).
		 */
		bool ResolveMethodForCall(
		    const std::string& OwnerTypeName,
		    const std::string& MethodName,
		    const std::vector<std::string>& ArgTypeNames,
		    std::string& OutOwnerLinkageName,
		    const struct FMethodDecl*& OutMethod,
		    const FSourceLocation& Location,
		    bool bAllowTypeMismatchCandidate = true,
		    bool bReportErrors = true,
		    bool* OutAmbiguous = nullptr,
		    bool bReceiverIsConst = false,
		    const std::vector<std::string>* ExplicitTemplateArguments = nullptr,
		    const std::string* ExpectedReturnType = nullptr) const;

		const FMethodDecl* SpecializeMethodSignature(const FMethodDecl& Method,
		    const std::string& Owner, const std::string& DeclModulePath,
		    const std::unordered_map<std::string, std::string>& OwnerBindings,
		    const std::vector<std::string>& ArgumentTypes,
		    const std::vector<std::string>* ExplicitArguments, const std::string* ExpectedReturnType = nullptr) const;
		bool ResolveTemplateMethodReference(const FMemberAccessExpr& Reference,
		    const FMethodDecl*& Method, std::string& Owner, std::string& FunctionType) const;
		bool CompileTemplateMethodReference(const FMemberAccessExpr& Reference, FEmitter& Emitter);
		bool ResolveTemplateFunctionReference(const FIdentifierExpr& Reference,
		    const FFunctionDecl*& Function, std::vector<std::string>& Arguments, std::string& FunctionType) const;
		bool CompileTemplateFunctionReference(const FIdentifierExpr& Reference, FEmitter& Emitter);
		bool EnsureMethodSpecialization(const FMethodDecl& Method,
		    const std::string& Owner, const FSourceLocation& Location);

		/**
		 * Resolve a bare `Method(...)` as a method of the enclosing instance.
		 * Callers invoke this only after local/global function values, builtins,
		 * natives, free functions, and constructors have failed to resolve.
		 */
		bool ResolveImplicitThisMethodForCall(
		    const struct FCallExpr& Call,
		    std::string& OutOwnerLinkageName,
		    const struct FMethodDecl*& OutMethod,
		    bool bReportErrors = false) const;

		std::string CurrentImplicitThisOwnerTypeName() const;

		bool BareCallNeedsImplicitThisCapture(const struct FCallExpr& Call) const;

		// Qualified (+ optional ref prefix) param types for method table keys.
		std::string QualifyMethodTypeName(
		    const struct FMethodDecl& Method, const std::string& TypeName,
		    const std::string& DeclModulePath,
		    const std::unordered_map<std::string, std::string>& Subst) const;

		std::vector<std::string> CanonicalMethodParamTypeNames(
		    const struct FMethodDecl& Method,
		    const std::string& DeclModulePath,
		    const std::unordered_map<std::string, std::string>& Subst) const;

		std::vector<std::string> CanonicalParamTypeNames(
		    const std::vector<struct FFunctionParam>& Parameters,
		    const std::string& DeclModulePath,
		    const std::unordered_map<std::string, std::string>& Subst) const;

		bool ValidateConversionMethods(const std::vector<struct FMethodDecl>& Methods,
		    const std::string& DeclModulePath,
		    const std::unordered_map<std::string, std::string>& Subst) const;

		// Leaf "Name#..." for CallMethod string pool (param-type mangled).
		std::string MakeMethodCallLeafName(
		    const std::string& MethodName,
		    const struct FMethodDecl& Method,
		    const std::string& DeclModulePath,
		    const std::unordered_map<std::string, std::string>& Subst) const;

		bool MethodSignaturesMatch(
		    const struct FMethodDecl& Left,
		    const struct FMethodDecl& Right,
		    const std::string& LeftModulePath,
		    const std::string& RightModulePath) const;

		bool MethodSignaturesMatch(
		    const struct FMethodDecl& Left,
		    const struct FMethodDecl& Right,
		    const std::string& LeftModulePath,
		    const std::string& RightModulePath,
		    const std::unordered_map<std::string, std::string>& RightSubst) const;

		bool MethodSignaturesMatch(
		    const struct FMethodDecl& Left,
		    const struct FMethodDecl& Right,
		    const std::string& LeftModulePath,
		    const std::string& RightModulePath,
		    const std::unordered_map<std::string, std::string>& LeftSubst,
		    const std::unordered_map<std::string, std::string>& RightSubst) const;

		bool FindVirtualBaseMethod(
		    const std::string& OwnerLinkageName,
		    const struct FMethodDecl& Method,
		    const std::string& OwnerModulePath,
		    std::string& OutBaseLinkageName,
		    const struct FMethodDecl*& OutBaseMethod) const;

		// True if a diagnostic was reported (caller should abort).
		bool ReportIfCannotOverrideFinalMethod(
		    bool bFoundVirtualBase,
		    const struct FMethodDecl* BaseMethod,
		    const struct FMethodDecl& Method,
		    const FSourceLocation& Location);

		const FHostMemberDesc* ResolveHostOperator(
		    const std::string& OwnerTypeName,
		    std::string_view OperatorName,
		    const std::vector<std::string>& ArgTypeNames,
		    bool& bOutAmbiguous,
		    std::uint16_t* OutNativeIndex = nullptr,
		    bool bReceiverIsConst = false) const;

		std::string HostOperatorReturnTypeName(
		    const FHostMemberDesc& Operator,
		    const std::string& OwnerTypeName) const;

		bool EmitHostOperatorCall(
		    FEmitter& Emitter,
		    const FHostMemberDesc& Operator,
		    std::uint16_t NativeIndex,
		    const std::vector<std::string>& ActualArgTypes,
		    const FSourceLocation& Location,
		    bool bLastArgumentIsConst = false);

		const FHostMemberDesc* ResolveHostConstructor(
		    const std::string& TypeName,
		    const std::vector<std::string>& ArgTypeNames,
		    bool& bOutAmbiguous,
		    std::uint16_t* OutNativeIndex = nullptr) const;

		// Deleted methods may hide non-virtual members or add overloads, but they
		// cannot occupy an inherited virtual/interface slot: doing so would make
		// dispatch behavior depend on the static receiver type.
		bool ReportIfDeletedMethodOverridesVirtual(
		    const std::string& OwnerLinkageName,
		    const struct FMethodDecl& Method,
		    const std::string& OwnerModulePath);

		bool IsVirtualOrOverridesVirtual(
		    const std::string& OwnerTypeName,
		    const struct FMethodDecl& Method) const;

		bool IsSubtypeOf(const std::string& SourceTypeName, const std::string& TargetTypeName) const;

		bool IsInterfaceTypeName(const std::string& TypeName) const;

		std::uint16_t FindInterfaceIndex(const std::string& InterfaceLinkageName) const;

		bool ResolveInheritanceInterfaces(
		    const std::vector<std::string>& InterfaceTypeNames,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location,
		    std::vector<std::uint16_t>& OutInterfaceIndices);

		bool TypeProvidesMethod(
		    const std::string& TypeLinkageName,
		    const struct FMethodDecl& Required,
		    const std::string& RequiredModulePath) const;

		bool TypeProvidesMethod(
		    const std::string& TypeLinkageName,
		    const struct FMethodDecl& Required,
		    const std::string& RequiredModulePath,
		    const std::unordered_map<std::string, std::string>& RequiredSubst) const;

		/**
		 * True when a matching method for a non-virtual interface default is a genuine
		 * shadow: some type in the hierarchy that implements IfaceLinkage declares it,
		 * and no interface-independent base class also declares it. The latter case
		 * (e.g. `class D : B1, IFoo` where B1 provides the method) is a legitimate
		 * implementation-by-inheritance, not a divergent shadow.
		 */
		bool ShadowsNonVirtualInterfaceDefault(
		    const std::string& TypeLinkageName,
		    const struct FMethodDecl& Required,
		    const std::string& RequiredModulePath,
		    const std::unordered_map<std::string, std::string>& RequiredSubst,
		    const std::string& IfaceLinkage) const;

		bool VerifyInterfaceConformance(
		    const std::string& TypeLinkageName,
		    const std::vector<std::string>& InterfaceTypeNames,
		    const std::vector<std::uint16_t>& ImplementedInterfaceIndices,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location);

		/** Flatten methods including inherited parents. Pair first = declaring interface
		 *  linkage as instantiated in this chain (for generic type-subst + default bind). */
		bool CollectFlattenedInterfaceMethods(
		    const std::string& InterfaceLinkageName,
		    std::vector<std::pair<std::string, const struct FMethodDecl*>>& OutMethods,
		    std::unordered_set<std::string>& VisitedInterfaces,
		    const FSourceLocation* ConflictLocation = nullptr,
		    std::vector<FDeferredIfaceDefaultConflict>* DeferredDefaultConflicts = nullptr) const;

		// Parents-first, visit-once diamond dedupe; sibling same-name fields conflict.
		bool CollectFlattenedInterfaceFields(
		    const std::string& InterfaceLinkageName,
		    std::vector<std::pair<std::string, const struct FFieldDecl*>>& OutFields,
		    std::unordered_set<std::string>& VisitedInterfaces,
		    const FSourceLocation* ConflictLocation = nullptr) const;

		// Merge flattened interface field slices into TypeDesc (skip identical names already present).
		bool AppendFlattenedInterfaceFields(
		    FBytecodeTypeDesc& TypeDesc,
		    const std::vector<std::string>& InterfaceTypeNames,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location);

		// Gray/Black DFS on Module->Interfaces parent lists. False -> already reported.
		bool CheckInterfaceInheritanceCycles();

		const struct FInterfaceDecl* FindInterfaceDeclByLinkage(const std::string& LinkageName) const;

		const struct FInterfaceDecl* FindGenericInterfaceDecl(
		    const std::string& SimpleName,
		    const std::string& ReferenceModulePath,
		    std::size_t TypeParamArity) const;

		bool EnsureInterfaceInstantiation(
		    const struct FInterfaceDecl& InterfaceDecl,
		    const std::vector<std::string>& ConcreteTypes,
		    std::uint16_t& OutInterfaceIndex,
		    const FSourceLocation& Location);

		bool EnsureInterfaceTypeResolved(
		    const std::string& TypeName,
		    std::string& OutLinkageName,
		    std::uint16_t* OutInterfaceIndex,
		    const FSourceLocation& Location);

		bool BuildInterfaceTypeSubst(
		    const std::string& InterfaceLinkageOrApp,
		    std::unordered_map<std::string, std::string>& OutSubst) const;

		// Direct base type linkage names in declaration order.
		std::vector<std::string> GetAstBaseTypeNames(const std::string& TypeLinkageName) const;

		/**
		 * Host type name used for method/property dispatch: Owner if it is a host type,
		 * else HostBaseTypeName of a script type that inherits a host Class/Struct.
		 */
		std::string ResolveHostDispatchTypeName(const std::string& OwnerTypeName) const;

		/**
		 * Host base of the owner type of the enclosing instance method (declared directly
		 * or inherited through script bases). Empty outside a host-subclass instance method.
		 */
		std::string CurrentMethodHostBaseTypeName() const;

		/**
		 * True when Object is the bare `Super` receiver of `Super.M(...)` / `Super::M(...)`.
		 * A local, field, global, or type named `Super` keeps its ordinary meaning.
		 */
		bool IsSuperReceiver(const struct FExpr& Object) const;

		/**
		 * Emit an implicit-`this` host call: LoadThis (host instance methods take the
		 * receiver as param 0), arguments, CallNative.
		 */
		bool EmitHostBaseMethodCall(
		    FEmitter& Emitter,
		    const std::string& MethodName,
		    const FNativeFunction& HostNative,
		    std::uint16_t HostNativeIndex,
		    const class FExprArgumentView Arguments,
		    const FSourceLocation& Location);

		/**
		 * Compound assign (`+=`, `++`, ...) against the implicit receiver, as used by a bare
		 * host-base property name and by `Super.X`. With ThisFieldName empty this is a host
		 * property (getter -> op -> setter); otherwise it is a flattened script base field.
		 */
		bool EmitImplicitReceiverCompoundAssign(
		    FEmitter& Emitter,
		    const struct FAssignExpr& Assign,
		    std::uint16_t HostGetterIndex,
		    std::uint16_t HostSetterIndex,
		    const std::string& ThisFieldName,
		    const std::string& TargetTypeName,
		    const std::string& RightTypeName,
		    const FSourceLocation& Location);

		// `Super.M(...)`: static bind to the script base method, else the host base native.
		bool CompileSuperMethodCall(const struct FMethodCallExpr& MethodCall, FEmitter& Emitter);

		// Resolve MethodName on the script base hierarchy of the enclosing method's owner.
		bool TryResolveSuperScriptMethod(
		    const std::string& MethodName,
		    const std::vector<std::string>& ArgTypeNames,
		    const FSourceLocation& Location,
		    std::string& OutOwnerLinkageName,
		    const struct FMethodDecl*& OutMethod) const;

		// Resolve bare or type-app (`B<int>`) inheritance names to linkage / mangled instance.
		std::string NormalizeInheritanceTypeName(
		    const std::string& TypeAsWritten,
		    const std::string& ReferenceModulePath) const;
		std::string ResolveInheritanceBaseLinkage(
		    const std::string& TypeAsWritten,
		    const std::string& ReferenceModulePath) const;

		// Direct implemented interface linkage names (AST; generics substituted when mangled).
		std::vector<std::string> GetAstImplementedInterfaceLinkages(
		    const std::string& TypeLinkageName) const;

		// Fill OutAncestors with TypeLinkageName and all transitive bases.
		void CollectAncestorLinkageSet(
		    const std::string& TypeLinkageName,
		    std::unordered_set<std::string>& OutAncestors) const;

		bool CheckDiamondInheritance(
		    const std::vector<std::string>& BaseTypeNamesSimple,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location);

		bool TypeHasUserConstructors(const std::string& TypeLinkageName) const;

		/**
		 * Resolve a constructor by argument types (same scoring as methods).
		 * OutCtorKey is the full table key `Owner::Type#paramSuffix`.
		 */
		bool ResolveConstructorForCall(
		    const std::string& TypeLinkageName,
		    const std::vector<std::string>& ArgTypeNames,
		    const struct FConstructorDecl*& OutConstructor,
		    std::string& OutCtorKey,
		    const FSourceLocation& Location,
		    bool bReportErrors = true,
		    bool* OutAmbiguous = nullptr,
		    bool bAllowExplicit = true,
		    bool bAllowParamUserDefinedConversions = true) const;

		bool TypeHasZeroArgConstructor(const std::string& TypeLinkageName) const;

		// Collect effective virtual slots across script bases and interfaces. A
		// concrete override clears every matching inherited pure slot; unrelated
		// base slots remain independent.
		bool TypeHasUnimplementedPureVirtual(
		    const std::string& TypeLinkageName,
		    std::string* OutMethodName = nullptr) const;

		bool RequireConcreteType(
		    const std::string& TypeLinkageName,
		    const FSourceLocation& Location);

		// Simple type name used in ctor keys (`Type::Name#...`).
		std::string GetTypeSimpleName(const std::string& TypeLinkageName) const;

		// Build constructor table key for a decl (with optional owner subst).
		std::string MakeConstructorKey(
		    const std::string& TypeLinkageName,
		    const std::string& TypeSimpleName,
		    const struct FConstructorDecl& Constructor,
		    const std::string& DeclModulePath,
		    const std::unordered_map<std::string, std::string>& Subst) const;

		bool UsesTrivialDefaultConstruction(std::uint16_t TypeIndex) const;

		bool EnsureImplicitDefaultConstructor(
		    const std::string& TypeLinkageName,
		    std::uint16_t TypeIndex,
		    const FSourceLocation& Location,
		    std::uint16_t& OutFunctionIndex);

		bool EnsureDefaultValueConstruction(
		    const std::string& TypeName,
		    const FSourceLocation& Location);

		bool EmitDefaultInitializedValue(
		    FEmitter& Emitter,
		    const std::string& TypeName,
		    const FSourceLocation& Location);

		// Initialize an implicit base constructor on the current raw receiver.
		bool EmitImplicitAncestorDefaultCtors(
		    FEmitter& Emitter,
		    const std::string& TypeLinkageName,
		    const FSourceLocation& Location,
		    bool bObjectOnStack = true);

		// Params match a virtual base but return type differs - reports and returns true.
		bool ReportIncompatibleVirtualOverride(
		    const std::string& OwnerLinkageName,
		    const struct FMethodDecl& Method,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location);

		// Raw allocation followed by the lazily compiled implicit constructor.
		bool EmitDefaultTypeConstruction(
		    FEmitter& Emitter,
		    const std::string& TypeLinkageName,
		    std::uint16_t TypeIndex,
		    const FSourceLocation& Location);

		bool AppendInheritedAndOwnFields(
		    FBytecodeTypeDesc& TypeDesc,
		    const std::string& OwnTypeName,
		    const std::vector<std::string>& BaseTypeNamesSimple,
		    EScriptObjectKind ExpectedKind,
		    const std::vector<struct FFieldDecl>& OwnFields,
		    const std::string& OwnerModulePath,
		    const FSourceLocation& Location,
		    const std::vector<std::string>& InterfaceTypeNames = {},
		    const std::vector<EMemberAccess>& BaseAccess = {},
		    bool bCompileFieldDefaults = true);

		std::string ResolveOwnerModulePath(const std::string& OwnerTypeName) const;

		bool ValidateDynamicArrayAssignment(
		    const std::string& TargetTypeName,
		    const struct FExpr& ValueExpr,
		    const FSourceLocation& Location);

		bool EmitValueCoercionForTarget(
		    FEmitter& Emitter,
		    const std::string& TargetTypeName,
		    const struct FExpr& SourceExpr,
		    const FSourceLocation& Location);

		bool EmitValueCoercionBetweenTypes(
		    FEmitter& Emitter,
		    const std::string& TargetTypeName,
		    const std::string& SourceTypeName,
		    const FSourceLocation& Location,
		    bool bAllowExplicitConstructor = false,
		    bool bSourceIsConst = false);

		// True if Source can be assigned/coerced to Target without emitting.
		bool CanAssignBetweenTypes(
		    const std::string& TargetTypeName,
		    const std::string& SourceTypeName,
		    bool bAllowUserDefinedConversions = true) const;

		bool RequireBoolCondition(const struct FExpr& Condition, const FSourceLocation& Location);

		bool EmitCompoundAssignOp(
		    FEmitter& Emitter,
		    EBinaryOp Op,
		    const std::string& LeftType,
		    const std::string& RightType,
		    const FSourceLocation& Location,
		    std::string* OutOverloadedResultType = nullptr,
		    const FExpr* RightExpression = nullptr);

		// Builtin / overloaded compound-assign result type for store coercion.
		std::string ResolveCompoundAssignResultType(
		    EBinaryOp Op,
		    const std::string& TargetTypeName,
		    const std::string& RightTypeName,
		    const std::string& OverloadedResultType) const;

		// Prefix/postfix ++/-- desugars to +=/-= 1 (signed int literal). Promote that unit
		// for unsigned (ConstI32/U32) and floating (ConstF64) targets so JIT CFG stays typed.
		bool TryEmitIncDecUnitOne(
		    FEmitter& Emitter,
		    const FAssignExpr& Assign,
		    const std::string& TargetTypeName,
		    std::string& InOutRightTypeName);

		static bool IsSignedIntegerTypeName(std::string_view TypeName);

		std::string GetSymbolLinkageName(
		    const std::string& ModulePath,
		    bool bExported,
		    const std::string& SimpleName) const;

		bool CanAccessSymbol(
		    const std::string& SymbolModulePath,
		    bool bExported,
		    const std::string& ReferenceModulePath) const;

		std::string ResolveTypeLinkageName(
		    const std::string& SimpleName,
		    const std::string& ReferenceModulePath,
		    std::size_t TypeParamArity = static_cast<std::size_t>(-1)) const;
		std::string ResolveTypeLinkageNameExact(
		    const std::string& Name,
		    const std::string& ReferenceModulePath,
		    std::size_t TypeParamArity = static_cast<std::size_t>(-1)) const;
		std::vector<std::string> BuildLexicalNamespaceLookupNames(
		    const std::string& Name) const;

		std::vector<std::string> BuildUsingNamespaceLookupNames(
		    const std::string& Name,
		    const std::string& ReferenceModulePath) const;

		std::vector<std::string> BuildUsingNamespaceTargets(
		    const struct FUsingNamespaceDecl& Directive) const;

		bool IsUsingNamespaceDirectiveVisible(
		    const struct FUsingNamespaceDecl& Directive,
		    const std::string& ReferenceModulePath) const;

		bool NamespaceExists(const std::string& Name, const std::string& ReferenceModulePath = {}) const;

		std::string NamespaceFromFunctionLinkage(const std::string& Name) const;

		bool ValidateUsingNamespaceDirectives() const;

		// Rewrite user type leaves inside container / T[N] names to linkage names so the VM
		// does not confuse an exported T with a private same-named type from another module.
		std::string QualifyTypeName(
		    const std::string& TypeName,
		    const std::string& ReferenceModulePath) const;

		// Coercion equality: keep exact type-table names (exports) so they are not
		// rebound to a same-named private type in CurrentModulePath.
		std::string CanonicalTypeNameForCoercion(const std::string& TypeName) const;

		/**
		 * Score one call-arg type against one param canonical encoding for overloads.
		 * Handles `ref T` / `const ref T` (mutable ref is assignable to const-ref params).
		 * On success sets OutScoreContribution to 1 (subtype / const-widening) or 2 (exact).
		 */
		bool TryScoreArgParamTypes(
		    const std::string& ArgTypeName,
		    const std::string& ParamTypeName,
		    int& OutScoreContribution,
		    bool bAllowUserDefinedConversions = true) const;

		// Encode a `ref` call arg as `"ref T"` / `"const ref T"` for overload scoring.
		std::string EncodeRefCallArgType(const FExpr& Operand, std::string BaseType) const;

		std::string DisplayTypeName(std::string TypeName);

		std::string SubstituteTypeName(const std::string& TypeName) const;

		std::string SubstituteTypeNameWith(
		    const std::string& TypeName,
		    const std::unordered_map<std::string, std::string>& Subst) const;

		static std::size_t RequiredGenericTypeArgumentCount(
		    const std::vector<std::string>& TypeParams,
		    const std::vector<std::string>& TypeParamDefaults);

		static bool AcceptsGenericTypeArgumentCount(
		    const std::vector<std::string>& TypeParams,
		    const std::vector<std::string>& TypeParamDefaults,
		    std::size_t ProvidedCount);

		bool CompleteGenericTypeArguments(
		    const std::vector<std::string>& TypeParams,
		    const std::vector<std::string>& TypeParamDefaults,
		    const std::string& DeclarationModulePath,
		    std::vector<std::string>& InOutConcreteTypes,
		    const FSourceLocation& Location,
		    const std::string& DisplayName,
		    const FTemplateParameterList* Parameters = nullptr, bool TypesAreCanonical = false);

		bool ResolveTemplateValueType(const std::string& Name, const std::string& ModulePath,
		    const std::unordered_map<std::string, std::string>& Bindings, FTemplateValueType& OutType) const;
		FTemplateEvaluationResult EvaluateTemplateSource(const std::string& Source,
		    const std::string& ModulePath, const std::unordered_map<std::string, std::string>& Bindings,
		    const FSourceLocation& Location, const std::string* Namespace = nullptr, bool AllowLocals = true) const;
		FTemplateEvaluationResult EvaluateTemplateExpression(const FExpr& Expression,
		    const std::string& ModulePath, const std::unordered_map<std::string, std::string>& Bindings,
		    const std::string& Namespace, bool AllowLocals, const FConstantFunction* Validate = nullptr, bool AllowFunctionCalls = true) const;
		bool CompleteTemplateArguments(const FTemplateParameterList& Parameters,
		    const std::string& DeclarationModule, std::vector<std::string>& Arguments,
		    const FSourceLocation& Location, bool Report = false,
		    const std::unordered_map<std::string, std::string>* EnclosingBindings = nullptr,
		    bool TypesAreCanonical = false) const;

		bool CompleteGenericFunctionTypeArguments(
		    const struct FFunctionDecl& Function,
		    std::vector<std::string>& InOutConcreteTypes,
		    const FSourceLocation& Location);

		bool EnsureGenericInstantiation(
		    const struct FFunctionDecl& Function,
		    const std::vector<std::string>& ConcreteTypes,
		    std::uint16_t& OutFunctionIndex,
		    const FSourceLocation& Location);

		// Validate that `where` bounds name interfaces (declaration-time).
		bool ValidateTypeParamBoundDeclarations(
		    const std::vector<std::string>& TypeParams,
		    const std::vector<struct FTypeParamBound>& Bounds,
		    const std::string& ModulePath,
		    const FSourceLocation& Location,
		    const char* GenericKindNoun, const FTemplateParameterList* Parameters = nullptr);

		// Concrete args must satisfy `where T : I` (instantiate-time).
		bool CheckTypeParamBounds(
		    const std::vector<std::string>& TypeParams,
		    const std::vector<struct FTypeParamBound>& Bounds,
		    const std::vector<std::string>& ConcreteTypes,
		    const std::string& ModulePath,
		    const FSourceLocation& Location,
		    const char* GenericKindNoun);

		bool CheckGenericFunctionBounds(
		    const struct FFunctionDecl& Function,
		    const std::vector<std::string>& ConcreteTypes,
		    const FSourceLocation& Location);

		bool ValidateGenericFunctionBoundDeclarations(
		    const struct FFunctionDecl& Function,
		    const FSourceLocation& Location);

		bool SplitUserGenericTypeApp(
		    const std::string& TypeName,
		    std::string& OutBaseName,
		    std::vector<std::string>& OutArgTypes) const;

		static std::vector<std::string> SplitTopLevelDelimited(
		    const std::string& Text,
		    char Delimiter);

		std::vector<std::string> DecodeGenericConcreteTypes(
		    const std::string& InstantiatedLinkageName) const;

		std::string MangleGenericInstanceName(
		    const std::string& BaseLinkageName,
		    const std::vector<std::string>& ConcreteTypes) const;

		bool TryBuildGenericOwnerSubst(
		    const std::string& OwnerTypeName,
		    std::unordered_map<std::string, std::string>& OutSubst,
		    const FSourceLocation& Location) const;

		bool ApplyTypeParamSubst(
		    const std::vector<std::string>& TypeParams,
		    const std::vector<std::string>& ConcreteTypes,
		    std::unordered_map<std::string, std::string>& InOutSubst,
		    const FSourceLocation& Location);

		std::string GenericTemplateBaseLinkage(const std::string& TypeName) const;

		const struct FStructDecl* FindGenericStructDecl(
		    const std::string& SimpleName,
		    const std::string& ReferenceModulePath,
		    std::size_t TypeParamArity) const;

		const struct FClassDecl* FindGenericClassDecl(
		    const std::string& SimpleName,
		    const std::string& ReferenceModulePath,
		    std::size_t TypeParamArity) const;

		bool EnsureStructInstantiation(
		    const struct FStructDecl& StructDecl,
		    const std::vector<std::string>& ConcreteTypes,
		    std::uint16_t& OutTypeIndex,
		    const FSourceLocation& Location);

		bool EnsureClassInstantiation(
		    const struct FClassDecl& ClassDecl,
		    const std::vector<std::string>& ConcreteTypes,
		    std::uint16_t& OutTypeIndex,
		    const FSourceLocation& Location);

		bool EnsureStructTypeResolved(
		    const std::string& TypeName,
		    std::string& OutLinkageName,
		    std::uint16_t* OutTypeIndex,
		    const FSourceLocation& Location);

		std::string GenericCtorSimpleName(const std::string& TypeName) const;

		bool MatchesExpectedGenericCtor(
		    const std::string& CalleeName,
		    const std::string& ExpectedTypeName) const;
		bool TryInferBuiltinHandleConstructorType(
		    const FCallExpr& Call, const std::string& CalleeName, std::string& OutTypeName) const;
		std::string ResolveGlobalLinkageName(
		    const std::string& Name,
		    const std::string& ReferenceModulePath) const;

		std::string ResolveGlobalLinkageNameExact(
		    const std::string& Name,
		    const std::string& ReferenceModulePath) const;

		bool IsStructType(const std::string& TypeName) const;

		bool IsAllowedMapOrSetKeyTypeForCompile(std::string_view TypeName) const;

		bool ParseMapTypeForCompile(
		    std::string_view TypeName,
		    std::string& OutKeyType,
		    std::string& OutValueType) const;

		bool ParseSetElementTypeForCompile(
		    std::string_view TypeName,
		    std::string& OutElementType) const;

		bool ParseOrderedMapTypeForCompile(
		    std::string_view TypeName,
		    std::string& OutKeyType,
		    std::string& OutValueType) const;

		bool NeedsPassByValueCopy(const std::string& TypeName) const;
		bool IsCopyConstructor(const FConstructorDecl& Constructor, const std::string& Owner,
		    const std::string& DeclModule) const;
		bool CompileDefaultedMethodBody(const std::vector<FFunctionParam>& Parameters,
		    ESpecialMemberKind Kind, const FSourceLocation& Location);
		bool CompileMemberwiseAssignment(FEmitter& Emitter, const FExpr& Target,
		    const FExpr& Source, const std::string& Type, const FSourceLocation& Location);
		bool CompileMemberwiseFields(FEmitter& Emitter, std::uint16_t TypeIndex,
		    const std::string& Left, const std::string& Right, ESpecialMemberKind Kind,
		    std::vector<std::size_t>& FailJumps, const FSourceLocation& Location);
		bool FinalizeSpecialMembers();

		/**
		 * True when Expression evaluates to an already-owned, unaliased value-type
		 * (struct / T[N]). Constructors, aggregate results and script calls transfer
		 * their owned result. Existing storage and reference returns still require
		 * language copy construction when an independent value is requested.
		 */
		bool ExpressionYieldsUnaliasedByValue(const struct FExpr& Expression, const std::string& TargetType = {}) const;
		void EmitValueCopy(FEmitter& Emitter, const std::string& TypeName, bool bAllowExplicit = false);
		bool CanTransferCoercedValue(const FExpr& Expression, const std::string& TargetType) const;

		std::uint16_t AllocateAssignTemp();

		const FUserTypeAlias* FindUserTypeAlias(
		    const std::string& SimpleName,
		    const std::string& ReferenceModulePath) const;

		std::size_t TypeParamArityFromLinkage(const std::string& TypeLinkageName) const;

		bool IsEnumTypeName(std::string_view TypeName) const;

		bool IsScopedEnumTypeName(std::string_view TypeName) const;

		std::string ExpandUserTypeAlias(
		    const std::string& TypeName,
		    const std::string& ReferenceModulePath,
		    int Depth = 0) const;

		std::string ExpandHostTypeAlias(
		    const std::string& TypeName,
		    int Depth = 0) const;

		std::string NormalizeTypeAlias(std::string TypeName) const;

		std::string NormalizeTypeAlias(
		    std::string TypeName,
		    const std::string& ReferenceModulePath) const;

		std::string ResolveEnumConstantLinkageName(
		    const std::string& Name,
		    const std::string& ReferenceModulePath) const;

		bool IsEnumConstantName(const std::string& Name) const;

		bool TryResolveEnumQualifiedConstant(
		    const struct FExpr& ObjectExpr,
		    const std::string& MemberName,
		    std::int64_t& OutValue,
		    std::string& OutDisplayName,
		    std::string* OutScopedEnumTypeName = nullptr) const;

		bool IsTypeNameWithoutValueBinding(const std::string& Name) const;

		// A complete type receiver, excluding member-value chains rooted in a type.
		bool TryResolveTypeNameReceiver(
		    const struct FExpr& Expression, std::string& OutTypeName) const;

		// Global name for a C++-style static data member: `TypeLinkage.FieldName`.
		std::string MakeStaticFieldGlobalName(
		    const std::string& TypeLinkage,
		    const std::string& FieldName) const
		{
			return TypeLinkage + "." + FieldName;
		}

		/**
		 * Resolve `Object.Member` to a static data member. Two receiver shapes are
		 * supported:
		 *  - type-name: ObjectExpr is a bare identifier naming a type (no value
		 *    binding). No object is evaluated; OutIsTypeNameReceiver=true.
		 *  - instance (C++-style): ObjectExpr has a resolvable object type that
		 *    declares/inherits the static field. Callers must evaluate the object
		 *    for side effects then Pop before using the backing global;
		 *    OutIsTypeNameReceiver=false.
		 * Searches the owning type, its bases and implemented interfaces for a static
		 * field named MemberName. On success fills the backing global name, the
		 * (qualified) field type, constness, the owner type name to use for
		 * access-checks, and which receiver shape matched.
		 */
		bool TryResolveStaticField(
		    const struct FExpr& ObjectExpr,
		    const std::string& MemberName,
		    std::string& OutGlobalName,
		    std::string& OutTypeName,
		    bool& OutIsConst,
		    std::string& OutAccessOwnerTypeName,
		    bool& OutIsTypeNameReceiver) const;

		// Resolve a read-only host static constant addressed as `Type.Name`.
		const FNativeFunction* TryResolveHostStaticConstant(
		    const struct FExpr& ObjectExpr,
		    const std::string& MemberName,
		    std::uint16_t& OutNativeIndex) const;

		/**
		 * Resolve `Ns.Member` / `Ns.Sub.Member` to a namespace-qualified
		 * module-scope global. The root identifier must be neither a value binding,
		 * an enum type, nor a type name.
		 */
		bool TryResolveNamespaceQualifiedGlobal(
		    const struct FExpr& ObjectExpr,
		    const std::string& MemberName,
		    std::string& OutGlobalLinkageName) const;

		// Flatten an identifier/member-only expression into its dotted source name.
		bool TryBuildQualifiedExpressionName(
		    const struct FExpr& ObjectExpr,
		    std::string& OutQualifiedName) const;

		/**
		 * Flatten a namespace receiver (`Ns` / `Ns.Sub`) and append MemberName.
		 * Returns false when the root is a value, enum, or type receiver so ordinary
		 * member/static dispatch keeps precedence over namespace lookup.
		 */
		bool TryBuildNamespaceQualifiedName(
		    const struct FExpr& ObjectExpr,
		    const std::string& MemberName,
		    std::string& OutQualifiedName) const;

		// Recursive helper: find a static field on a type linkage / its bases / interfaces.
		bool FindStaticFieldOnTypeLinkage(
		    const std::string& TypeLinkageName,
		    const std::string& FieldName,
		    std::string& OutGlobalName,
		    std::string& OutTypeName,
		    bool& OutIsConst,
		    int Depth = 0) const;

		// Register + init all static data members (structs / classes / interfaces).
		bool RegisterStaticFieldGlobals();

		bool CompileStaticFieldInitializers();

		/**
		 * Register the backing globals for one type's static data members. Used for
		 * non-generic types (Compile-start) and for each generic instantiation
		 * (first `Ensure*Instantiation`, with `ActiveTypeSubst` live so `static T`
		 * fields substitute). `TypeLinkage` is the full (mangled) instantiation name;
		 * already-registered globals are skipped (idempotent).
		 */
		bool RegisterStaticFieldsForTypeLinkage(
		    const std::string& TypeLinkage,
		    const std::string& ModulePath,
		    const std::vector<struct FFieldDecl>& Fields);

		/**
		 * Emit initializers (into module init code) for one type's static data
		 * members. Safe to call mid-body: it neutralizes the active function/scope
		 * state so code lands in `Module->InitCode`. Idempotent via
		 * `InitializedStaticFieldGlobals`.
		 */
		bool CompileStaticFieldInitializersForTypeLinkage(
		    const std::string& TypeLinkage,
		    const std::string& ModulePath,
		    const std::vector<struct FFieldDecl>& Fields);

		/**
		 * Queue during early type layout and function-body compile. The static
		 * initialization phase also resolves queued dependencies before their use.
		 */
		bool QueueOrCompileStaticFieldInitializersForTypeLinkage(
		    const std::string& TypeLinkage,
		    const std::string& ModulePath,
		    const std::vector<struct FFieldDecl>& Fields);

		// Drain `PendingGenericStaticFieldInits` (loops until empty / nested Ensures).
		bool FlushPendingGenericStaticFieldInitializers(const std::string& RequiredType = {});

		/**
		 * Force a generic instantiation's static globals to exist before a
		 * type-name static access (`Box<int>.Count`) loads them. No-op for
		 * non-generic owners (registered at Compile start). Returns true when the
		 * backing global is resolvable afterwards.
		 */
		bool EnsureStaticFieldGlobalRegistered(
		    const std::string& OwnerTypeName,
		    const std::string& GlobalName,
		    const FSourceLocation& Location);

		// Soft-injected open generics (pair) when the program does not define them.
		std::vector<FStructDecl> BuiltinStructTemplates;

		// Soft-injected open class generics (Handle) when the program does not define them.
		std::vector<FClassDecl> BuiltinClassTemplates;
		std::unique_ptr<FBytecodeModule> Module;
		// Type names are append-only within one compiled module; never retain pointers
		// into its vectors, which grow during implicit/template instantiation.
		mutable const FBytecodeModule* IndexedTypeModule = nullptr;
		mutable std::size_t IndexedTypeCount = 0;
		mutable std::unordered_map<std::string, std::uint16_t> RegisteredTypeIndices;
		const FProgram* ActiveProgram = nullptr;
		mutable std::shared_ptr<FNamespaceLookup> NamespaceLookup;
		mutable std::size_t NamespaceDeclarationCount = 0;
		const ModuleVisibility::FImportIndex* ActiveImportIndex = nullptr;
		const std::vector<FNativeFunction>* ActiveNatives = nullptr;
		const IHostBindingCatalog* ActiveBindings = nullptr;
		std::shared_ptr<const IHostBindingCatalog> StandardBindings;
		std::shared_ptr<const IHostBindingCatalog> ScopedBindings;
		mutable std::unordered_map<std::string, std::unordered_set<std::string>> NativeExposure;
		FBytecodeFunction* CurrentFunction = nullptr;
		std::vector<FScope> Scopes;
		std::vector<FLoopContext> LoopStack;
		std::unordered_map<std::string, FLabelInfo> LabelTable;
		std::vector<FGotoPatch> GotoPatches;
		std::vector<FLocalInitMarker> LocalInitMarkers;

		// Innermost caught value slot; used to compile a bare `throw;`.
		std::vector<std::uint16_t> ActiveCatchSlots;

		// Distinguishes sibling try/catch/finally bodies that happen to have the
		// same lexical depth. A goto may not enter a different protected body.
		std::vector<std::uint32_t> ActiveExceptionScopePath;
		std::uint32_t ActiveFinallyDepth = 0;
		std::uint16_t NextLocalSlot = 0;
		std::uint32_t NextAssignTempId = 0;
		std::uint32_t NextExceptionScopeId = 1;
		std::uint32_t NextLambdaId = 0;
		std::unordered_map<std::string, std::uint32_t> LambdaOrdinals;
		std::unordered_map<std::string, FLambdaOwnerIdentity> LambdaOwnerIdentities;
		const struct FBlockStmt* CurrentFunctionSourceBody = nullptr;
		const struct FStmt* CurrentInitializerStatement = nullptr;
		const struct FExpr* CurrentInitializerExpression = nullptr;
		const struct FFieldDecl* CurrentInitializerField = nullptr;
		std::vector<FMethodBindingCodegenInfo> MethodBindingCodegenInfo;
		const struct FLambdaExpr* IsolatedCallableLambda = nullptr;
		std::uint16_t CurrentOwnerTypeIndex = 0;

		// Linkage name of the type whose method/ctor body is being compiled (member access).
		std::string CurrentOwnerTypeLinkage;

		// Exact lexical function granting friend access; nested lambdas inherit it.
		std::string CurrentAccessFunctionLinkage;

		// Mutable: silent const helpers (bounds / type resolve) temporarily rebind module.
		mutable std::string CurrentModulePath;

		// Lexical namespace of the body/global initializer currently being compiled.
		std::string CurrentNamespacePrefix;
		std::string CurrentFunctionReturnType;

		// Consumed by the next outer CompileExpression call only. Nested operands
		// continue to use ordinary value semantics.
		bool bPreserveNextReferenceResult = false;
		bool bCurrentExpressionPreservesReference = false;

		// Estimated host-stack cost of the active recursive expression-emission
		// chain. Large expression families have different Debug stack footprints;
		// a weighted budget rejects adversarial ASTs before exhausting a host thread.
		std::size_t ActiveExpressionCompileStackCost = 0;

		// While compiling interface default method bodies; types `this` as the interface.
		std::string CurrentInterfaceOwnerLinkage;
		std::string ExpressionExpectedType;
		std::unordered_map<std::string, std::string> GlobalVarTypes;
		std::unordered_set<std::string> GlobalVarConsts;
		// Compiler-only const-static bindings; bytecode ConstantValue remains constexpr-only.
		std::unordered_map<std::string, FLocalStaticConstantBinding> LocalStaticConstants;
		std::vector<FEnclosingConstantScope> EnclosingConstantScopes;
		std::unordered_set<std::string> TypesInProgress;
		std::unordered_map<std::string, std::string> ActiveTypeSubst;
		std::unordered_set<std::string> InstantiatingGenerics;
		struct FMethodSpecialization
		{
			const FMethodDecl* Declaration = nullptr;
			std::unique_ptr<FMethodDecl> Signature;
			std::string ModulePath;
			std::unordered_map<std::string, std::string> Bindings;
		};
		std::unordered_map<const FDeclMeta*, FTemplateMetadataBinding> TemplateMetadataBindings;
		mutable std::unordered_map<std::string, FMethodSpecialization> MethodSpecializations;
		mutable std::unordered_map<const FMethodDecl*, std::string> MethodSpecializationKeys;

		// Enum type names (linkage and/or simple) alias int; members are module-scope int constants.
		std::unordered_set<std::string> EnumTypeNames;
		std::unordered_map<std::string, std::int64_t> EnumConstants;
		std::unordered_map<std::string, std::string> EnumConstantTypeNames;

		// Multimap: same simple name may exist per module; expand prefers reference module.
		std::unordered_multimap<std::string, FUserTypeAlias> UserTypeAliases;

		// Parallel to Scopes: local `using`/`typedef` aliases (innermost last).
		std::vector<std::unordered_map<std::string, std::string>> LocalTypeAliasScopes;
		FSourceLocation CurrentEmitLocation = {};

		bool bCollectRuntimeMemoryTypes = false;
		bool bUsesRuntimeMemoryTypes = false;
		// Mutable so const helpers (e.g. interface flatten) may report diagnostics.
		mutable bool bHasError = false;
		bool bCollectMultipleErrors = true;
		std::size_t MaxCollectedErrors = 32;
		mutable FScriptError Error;
		mutable std::vector<FScriptError> Diagnostics;
		struct FCopyRequirement
		{
			std::string TypeName;
			std::string FunctionName;
			FSourceLocation Location;
			bool bAllowExplicit = false;
			std::string AccessFunction;
		};
		mutable std::vector<FCopyRequirement> CopyRequirements;

		// Static field globals already emitted an initializer (idempotency guard).
		std::unordered_set<std::string> InitializedStaticFieldGlobals;
		// Type layout can instantiate generics before ordinary static initialization.
		bool bStaticFieldInitializationStarted = false;

		/**
		 * Mangled generic linkages whose static-field initializers were deferred
		 * until after non-generic static init (see QueueOrCompile...).
		 */
		std::vector<std::string> PendingGenericStaticFieldInits;
	};
}

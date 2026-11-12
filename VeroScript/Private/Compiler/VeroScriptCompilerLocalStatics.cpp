// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAst.h"
#include "VeroScriptAstClone.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptCompilerVariantInternal.h"
#include "VeroScriptAnalysisControl.h"

#include <cstring>

namespace VeroScript
{
	VeroScriptCompiler::FLambdaInitializerContext::FLambdaInitializerContext(VeroScriptCompiler& InCompiler)
	    : Compiler(InCompiler), PreviousStatement(InCompiler.CurrentInitializerStatement), PreviousExpression(InCompiler.CurrentInitializerExpression), PreviousField(InCompiler.CurrentInitializerField)
	{
	}

	VeroScriptCompiler::FLambdaInitializerContext::~FLambdaInitializerContext()
	{
		Compiler.CurrentInitializerStatement = PreviousStatement;
		Compiler.CurrentInitializerExpression = PreviousExpression;
		Compiler.CurrentInitializerField = PreviousField;
	}

	bool VeroScriptCompiler::CompileLocalStatic(const FVarDeclStmt& Declaration, FEmitter& Emitter)
	{
		if (!CurrentFunction || Scopes.empty() || Declaration.bIsRef)
		{
			ReportErrorId("diag.local_static_requires_value", Declaration.Location);
			return false;
		}
		if (Scopes.back().Locals.count(Declaration.Name) || Scopes.back().StaticLocals.count(Declaration.Name) || Scopes.back().EnumConstants.count(Declaration.Name))
		{
			ReportError("duplicate local: " + Declaration.Name, Declaration.Location);
			return false;
		}
		if (const auto Binding = ActiveTypeSubst.find(Declaration.Name);
		    Binding != ActiveTypeSubst.end() && Binding->second.rfind("@value:", 0) == 0)
		{
			ReportError("a runtime variable cannot shadow a template value parameter: " + Declaration.Name, Declaration.Location);
			return false;
		}
		const std::string Owner = CurrentFunction->Name;
		const auto Ordinal = CurrentFunction->LocalStaticInitializers.size();
		const std::string GlobalName = Owner + "$static$" + std::to_string(Ordinal) + "$" + Declaration.Name;
		const auto GlobalSlot = AddGlobal(GlobalName);
		if (HasError()) return false;
		Module->GlobalMetadata[GlobalSlot].bLocalStatic = true;
		CurrentFunction->bUsesLocalStaticStorage = true;
		std::string Type = CanonicalTypeNameForCoercion(SubstituteTypeName(Declaration.TypeName));
		if (Type == "auto" && Declaration.Initializer)
		{
			if (!TryResolveExprTypeName(*Declaration.Initializer, Type))
			{
				ReportError("cannot infer local static type: " + Declaration.Name, Declaration.Location);
				return false;
			}
		}
		GlobalVarTypes[GlobalName] = Type;
		if (Declaration.bIsConst)
		{
			GlobalVarConsts.insert(GlobalName);
		}
		Scopes.back().StaticLocals.emplace(Declaration.Name, GlobalName);
		FBytecodeLocalStaticInitializer Initializer;
		Initializer.GlobalSlot = GlobalSlot;
		Initializer.Name = Declaration.Name;
		Initializer.BeginOffset = static_cast<std::uint32_t>(Emitter.CurrentOffset());
		// Reserve the declaration ordinal before nested lambda compilation.
		CurrentFunction->LocalStaticInitializers.push_back(Initializer);
		Emitter.EmitU16Operand(EOpcode::BeginLocalStatic, GlobalSlot);
		const auto JumpOperand = static_cast<std::size_t>(Emitter.CurrentOffset());
		Bytecode::WriteI32LE(CurrentFunction->Code, 0);
		FVarDeclStmt Temporary(Declaration.TypeName, "$static_initializer$" + std::to_string(GlobalSlot),
		    Declaration.Initializer ? CloneExpression(*Declaration.Initializer) : nullptr);
		Temporary.Location = Declaration.Location;
		Temporary.bIsConst = Declaration.bIsConst;
		Temporary.bConstexpr = Declaration.bConstexpr;
		const auto ConstantStart = Module->CompiledConstants.size();
		if (!CompileStatement(Temporary)) return false;
		for (std::size_t Index = ConstantStart; Index < Module->CompiledConstants.size(); ++Index)
		{
			if (Module->CompiledConstants[Index].Name == Temporary.Name)
			{
				Module->CompiledConstants[Index].Name = Declaration.Name;
			}
		}
		if (!LocalInitMarkers.empty() && LocalInitMarkers.back().Name == Temporary.Name)
		{
			LocalInitMarkers.back().Name = Declaration.Name;
		}
		const auto Local = Scopes.back().Locals.find(Temporary.Name);
		if (Local == Scopes.back().Locals.end() || CurrentFunction->Code.size() < 3u || static_cast<EOpcode>(CurrentFunction->Code[CurrentFunction->Code.size() - 3u]) != EOpcode::StoreLocal)
		{
			ReportError("invalid local static initialization lowering", Declaration.Location);
			return false;
		}
		const auto LocalSlot = Local->second;
		for (std::size_t Ip = Initializer.BeginOffset + 7u; Ip < CurrentFunction->Code.size();)
		{
			const auto Op = static_cast<EOpcode>(CurrentFunction->Code[Ip]);
			if ((Op == EOpcode::LoadGlobal || Op == EOpcode::StoreGlobal || Op == EOpcode::MakeGlobalRef) && Bytecode::ReadU16LE(CurrentFunction->Code.data() + Ip + 1u) == GlobalSlot)
			{
				ReportErrorId("diag.local_static_self_initialization", Declaration.Location, {Declaration.Name});
				return false;
			}
			Ip += Bytecode::InstructionSize(Op);
		}
		Type = Scopes.back().LocalTypes.at(Temporary.Name);
		if (CompilerDetail::IsVariantIntrinsicName(Declaration.Name) && Runtime::IsFunctionTypeName(Type))
		{
			ReportErrorId("diag.variant_reserved_name", Declaration.Location);
			return false;
		}
		GlobalVarTypes[GlobalName] = Type;
		Module->GlobalTypeNames[GlobalSlot] = Type;
		auto& Meta = Module->GlobalMetadata[GlobalSlot];
		Meta.ModulePath = CurrentModulePath;
		Meta.DeclaredTypeName = Declaration.TypeName;
		Meta.bConst = Declaration.bIsConst;
		Meta.bConstexpr = Declaration.bConstexpr;
		if (const auto Constant = Scopes.back().TemplateConstants.find(Temporary.Name);
		    Declaration.bConstexpr && Constant != Scopes.back().TemplateConstants.end())
		{
			Meta.ConstantValue = Constant->second;
			Scopes.back().TemplateConstants[Declaration.Name] = Constant->second;
		}
		if (Declaration.bIsConst && !Declaration.bConstexpr)
		{
			// Reuse ordinary const evaluation, including its deferred constexpr calls.
			// Keep this compiler-side: serialized ConstantValue is constexpr-only.
			FLocalStaticConstantBinding Binding;
			if (const auto Constant = Scopes.back().TemplateConstants.find(Temporary.Name);
			    Constant != Scopes.back().TemplateConstants.end())
			{
				Binding.Value = Constant->second;
			}
			else if (const auto Pending = Scopes.back().ConstantInitializers.find(Temporary.Name);
			    Pending != Scopes.back().ConstantInitializers.end())
			{
				Binding.Initializer = Pending->second;
				Binding.Initializer.Identity = GlobalName;
				Binding.OwnedExpression = std::move(Temporary.Initializer);
				Binding.Initializer.Expression = Binding.OwnedExpression.get();
			}
			if (Binding.Value || Binding.Initializer.Expression)
			{
				LocalStaticConstants.emplace(GlobalName, std::move(Binding));
			}
		}
		Meta.bLocalStatic = true;
		Meta.DeclMeta = Declaration.DeclMeta;
		Initializer.CommitOffset = static_cast<std::uint32_t>(CurrentFunction->Code.size() - 3u);
		CurrentFunction->Code[Initializer.CommitOffset] = static_cast<std::uint8_t>(EOpcode::CommitLocalStatic);
		CurrentFunction->Code[Initializer.CommitOffset + 1u] = static_cast<std::uint8_t>(GlobalSlot);
		CurrentFunction->Code[Initializer.CommitOffset + 2u] = static_cast<std::uint8_t>(GlobalSlot >> 8u);
		Initializer.EndOffset = static_cast<std::uint32_t>(Emitter.CurrentOffset());
		Emitter.PatchI32(JumpOperand, static_cast<std::int32_t>(Initializer.EndOffset - JumpOperand - 4u));
		CurrentFunction->LocalStaticInitializers[Ordinal] = std::move(Initializer);
		Scopes.back().Locals.erase(Temporary.Name);
		Scopes.back().LocalTypes.erase(Temporary.Name);
		Scopes.back().LocalConsts.erase(Temporary.Name);
		Scopes.back().TemplateConstants.erase(Temporary.Name);
		Scopes.back().ConstantInitializers.erase(Temporary.Name);
		if (NextLocalSlot == LocalSlot + 1u)
		{
			--NextLocalSlot;
			CurrentFunction->LocalCount = NextLocalSlot;
			CurrentFunction->LocalNames.resize(NextLocalSlot);
		}
		return true;
	}

	std::uint64_t VeroScriptCompiler::ComputeCallableSemanticIdentity(const FStmt* Body, const FExpr* RootExpression) const
	{
		std::uint64_t Identity = 14695981039346656037ull;
		const auto Add = [&](const std::string_view Text)
		{
			Identity = Runtime::Fnv1a64(Identity, std::to_string(Text.size()));
			Identity = Runtime::Fnv1a64(Identity, ":");
			Identity = Runtime::Fnv1a64(Identity, Text);
		};
		const auto Number = [&](const std::uint64_t Value)
		{
			Add(std::to_string(Value));
		};
		const auto Names = [&](const auto& Values)
		{
			Number(Values.size());
			for (const auto& Value : Values)
			{
				Add(Value);
			}
		};
		const auto Parameters = [&](const auto& Values)
		{
			Number(Values.size());
			for (const auto& Parameter : Values)
			{
				Add(Parameter.Name);
				Add(Parameter.TypeName);
				Number(Parameter.bIsConst);
				Number(Parameter.bIsRef);
				Number(Parameter.bIsVariadic);
			}
		};
		if (CurrentFunction)
		{
			Add(CurrentFunction->ClosureTypeName);
		}
		const auto VisitExpression = [&](const FExpr& Expression)
		{
			CheckAnalysisCancellation();
			Number(static_cast<std::uint8_t>(Expression.Kind));
			switch (Expression.Kind)
			{
			case EExprKind::IntegerLiteral:
				{
					const auto& Value = static_cast<const FIntegerLiteralExpr&>(Expression);
					Number(static_cast<std::uint32_t>(Value.Value));
					Number(Value.UnsignedValue);
					Number(Value.Value64);
					Number(Value.UnsignedValue64);
					Number(Value.bUnsigned);
					Number(Value.bIs64);
					break;
				}
			case EExprKind::CharacterLiteral:
				{
					Number(static_cast<const FCharacterLiteralExpr&>(Expression).Value);
					break;
				}
			case EExprKind::FloatLiteral:
				{
					const auto& Value = static_cast<const FFloatLiteralExpr&>(Expression);
					std::uint64_t Bits = 0;
					std::memcpy(&Bits, &Value.Value, sizeof(Bits));
					Number(Bits);
					Number(Value.bFloat32);
					break;
				}
			case EExprKind::StringLiteral:
				{
					Add(static_cast<const FStringLiteralExpr&>(Expression).Value);
					break;
				}
			case EExprKind::WStringLiteral:
				{
					const auto& Value = static_cast<const FWStringLiteralExpr&>(Expression).Value;
					Number(Value.size());
					for (const auto Character : Value)
					{
						Number(Character);
					}
					break;
				}
			case EExprKind::BooleanLiteral:
				{
					Number(static_cast<const FBooleanLiteralExpr&>(Expression).Value);
					break;
				}
			case EExprKind::Identifier:
				{
					const auto& Value = static_cast<const FIdentifierExpr&>(Expression);
					Add(Value.Name);
					Names(Value.TypeArgs);
					break;
				}
			case EExprKind::Unary:
				{
					Number(static_cast<std::uint8_t>(static_cast<const FUnaryExpr&>(Expression).Op));
					break;
				}
			case EExprKind::Binary:
				{
					Number(static_cast<std::uint8_t>(static_cast<const FBinaryExpr&>(Expression).Op));
					break;
				}
			case EExprKind::Assign:
				{
					const auto& Value = static_cast<const FAssignExpr&>(Expression);
					Number(Value.bIsCompound);
					Number(static_cast<std::uint8_t>(Value.CompoundOp));
					Number(Value.bFromPrefixIncDec);
					Number(Value.bFromPostfixIncDec);
					break;
				}
			case EExprKind::Call:
				{
					const auto& Value = static_cast<const FCallExpr&>(Expression);
					Add(Value.Callee);
					Names(Value.TypeArgs);
					Number(Value.GetArguments().size());
					break;
				}
			case EExprKind::MethodCall:
				{
					const auto& Value = static_cast<const FMethodCallExpr&>(Expression);
					Add(Value.Method);
					Names(Value.TypeArgs);
					Number(Value.Arguments.size());
					break;
				}
			case EExprKind::MemberAccess:
				{
					const auto& Value = static_cast<const FMemberAccessExpr&>(Expression);
					Add(Value.Member);
					Names(Value.TypeArgs);
					break;
				}
			case EExprKind::ArrayInit:
				{
					const auto& Value = static_cast<const FArrayInitExpr&>(Expression);
					Add(Value.TypeName);
					Number(Value.Elements.size());
					break;
				}
			case EExprKind::MapInit:
				{
					const auto& Value = static_cast<const FMapInitExpr&>(Expression);
					Add(Value.TypeName);
					Number(Value.Entries.size());
					break;
				}
			case EExprKind::Lambda:
				{
					const auto& Value = static_cast<const FLambdaExpr&>(Expression);
					Number(static_cast<std::uint8_t>(Value.CaptureKind));
					Number(Value.bMutable);
					Add(Value.ReturnType);
					Parameters(Value.Parameters);
					Number(Value.Captures.size());
					for (const auto& Capture : Value.Captures)
					{
						Add(Capture.Name);
						Number(Capture.bByReference);
						Number(Capture.Initializer != nullptr);
					}
					break;
				}
			default:
				{
					break;
				}
			}
		};
		const auto VisitStatement = [&](const FStmt& Statement)
		{
			CheckAnalysisCancellation();
			Number(static_cast<std::uint8_t>(Statement.Kind));
			switch (Statement.Kind)
			{
			case EStmtKind::Block:
				{
					Number(static_cast<const FBlockStmt&>(Statement).Statements.size());
					break;
				}
			case EStmtKind::VarDeclGroup:
				{
					Number(static_cast<const FVarDeclGroupStmt&>(Statement).Declarations.size());
					break;
				}
			case EStmtKind::Return:
				{
					Number(static_cast<const FReturnStmt&>(Statement).Value != nullptr);
					break;
				}
			case EStmtKind::Throw:
				{
					Number(static_cast<const FThrowStmt&>(Statement).Value != nullptr);
					break;
				}
			case EStmtKind::If:
				{
					const auto& Value = static_cast<const FIfStmt&>(Statement);
					Number(Value.bConstexpr);
					Number(Value.ElseBranch != nullptr);
					break;
				}
			case EStmtKind::For:
				{
					const auto& Value = static_cast<const FForStmt&>(Statement);
					Number(Value.Init != nullptr);
					Number(Value.Condition != nullptr);
					Number(Value.Step != nullptr);
					Number(Value.InitExpressions.size());
					Number(Value.StepExpressions.size());
					break;
				}
			case EStmtKind::VarDecl:
				{
					const auto& Value = static_cast<const FVarDeclStmt&>(Statement);
					Add(Value.Name);
					Add(Value.TypeName);
					Number(Value.bIsLocalStatic);
					Number(Value.bIsConst);
					Number(Value.bIsRef);
					Number(Value.bConstexpr);
					break;
				}
			case EStmtKind::TypeAlias:
				{
					const auto& Value = static_cast<const FTypeAliasStmt&>(Statement);
					Add(Value.Name);
					Add(Value.TypeName);
					Number(Value.bTypeDefinition);
					break;
				}
			case EStmtKind::Try:
				{
					const auto& Value = static_cast<const FTryStmt&>(Statement);
					Number(Value.Catches.size());
					Number(Value.FinallyBlock != nullptr);
					for (const auto& Handler : Value.Catches)
					{
						Add(Handler.Name);
						Add(Handler.TypeName);
						Number(Handler.bIsRef);
						Number(Handler.bIsConst);
					}
					break;
				}
			case EStmtKind::ForEach:
				{
					const auto& Value = static_cast<const FForEachStmt&>(Statement);
					Add(Value.ElementName);
					Add(Value.ElementTypeName);
					Add(Value.KeyName);
					Add(Value.KeyTypeName);
					Number(Value.bHasKeyBinding);
					Number(Value.bIsConst);
					Number(Value.bIsRef);
					Number(Value.bKeyIsConst);
					break;
				}
			case EStmtKind::Switch:
				{
					const auto& Value = static_cast<const FSwitchStmt&>(Statement);
					Number(Value.Cases.size());
					for (const auto& Case : Value.Cases)
					{
						Number(Case.bIsDefault);
						Number(Case.Labels.size());
					}
					break;
				}
			case EStmtKind::Goto:
				{
					Add(static_cast<const FGotoStmt&>(Statement).Name);
					break;
				}
			case EStmtKind::Label:
				{
					Add(static_cast<const FLabelStmt&>(Statement).Name);
					break;
				}
			default:
				{
					break;
				}
			}
		};
		if (Body)
		{
			VisitStatementNodes(*Body, VisitExpression, VisitStatement);
		}
		else if (RootExpression)
		{
			VisitExpressionNodes(*RootExpression, VisitExpression, VisitStatement);
		}
		// Include resolved constant operands: changing a constexpr dependency must
		// not look identical merely because its source identifier did not change.
		for (std::size_t Ip = 0; CurrentFunction && Ip < CurrentFunction->Code.size();)
		{
			CheckAnalysisCancellation();
			const auto Op = static_cast<EOpcode>(CurrentFunction->Code[Ip]);
			if (Op == EOpcode::ConstI32 || Op == EOpcode::ConstF64 || Op == EOpcode::ConstStr)
			{
				const auto& Constant = Module->Constants[Bytecode::ReadU16LE(CurrentFunction->Code.data() + Ip + 1u)];
				Number(static_cast<std::uint8_t>(Constant.Kind));
				switch (Constant.Kind)
				{
				case EConstantKind::I8:
					{
						Number(Constant.I8);
						break;
					}
				case EConstantKind::U8:
					{
						Number(Constant.U8);
						break;
					}
				case EConstantKind::I16:
					{
						Number(Constant.I16);
						break;
					}
				case EConstantKind::U16:
					{
						Number(Constant.U16);
						break;
					}
				case EConstantKind::I32:
					{
						Number(Constant.I32);
						break;
					}
				case EConstantKind::U32:
					{
						Number(Constant.U32);
						break;
					}
				case EConstantKind::I64:
					{
						Number(Constant.I64);
						break;
					}
				case EConstantKind::U64:
					{
						Number(Constant.U64);
						break;
					}
				case EConstantKind::F32:
					{
						std::uint32_t Bits = 0;
						std::memcpy(&Bits, &Constant.F32, sizeof(Bits));
						Number(Bits);
						break;
					}
				case EConstantKind::F64:
					{
						std::uint64_t Bits = 0;
						std::memcpy(&Bits, &Constant.F64, sizeof(Bits));
						Number(Bits);
						break;
					}
				case EConstantKind::Enum:
					{
						Add(Module->StringPool[Constant.StringIndex]);
						Number(Constant.I64);
						break;
					}
				case EConstantKind::Str:
					{
						Add(Module->StringPool[Constant.StringIndex]);
						break;
					}
				case EConstantKind::WStr:
					{
						const auto& Text = Module->WStringPool[Constant.StringIndex];
						Number(Text.size());
						for (const auto Character : Text)
						{
							Number(Character);
						}
						break;
					}
				default:
					{
						break;
					}
				}
			}
			else if (Op == EOpcode::AddLocalImm || Op == EOpcode::JmpIfLocalGeImm)
			{
				Number(Bytecode::ReadI32LE(CurrentFunction->Code.data() + Ip + 3u));
			}
			Ip += Bytecode::InstructionSize(Op);
		}
		return Identity == 0 ? 1 : Identity;
	}

	std::uint64_t VeroScriptCompiler::ComputeLambdaParentIdentity(const std::string& Owner)
	{
		auto& Cached = LambdaOwnerIdentities[Owner];
		if (Cached.SemanticIdentity != 0) return Cached.SemanticIdentity;
		std::uint64_t Identity = 14695981039346656037ull;
		if (CurrentFunction && CurrentFunctionSourceBody)
		{
			Identity = ComputeCallableSemanticIdentity(CurrentFunctionSourceBody);
		}
		else if (CurrentInitializerStatement || CurrentInitializerExpression)
		{
			Identity = ComputeCallableSemanticIdentity(CurrentInitializerStatement, CurrentInitializerExpression);
		}
		else if (ActiveProgram)
		{
			// Anonymous top-level expressions have no declaration name. Retain them
			// only while the enclosing initializer sequence remains semantically equal.
			for (const auto& Statement : ActiveProgram->TopLevelStatements)
			{
				if (Statement)
				{
					Identity = Runtime::Fnv1a64(Identity, std::to_string(ComputeCallableSemanticIdentity(Statement.get())));
				}
			}
		}
		if (Cached.ParentIdentity != 0)
		{
			Identity = Runtime::Fnv1a64(Cached.ParentIdentity, std::to_string(Identity));
		}
		Cached.SemanticIdentity = Identity == 0 ? 1 : Identity;
		return Cached.SemanticIdentity;
	}
}

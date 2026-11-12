// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "TypeSupport/VeroScriptConstantRepresentation.h"
#include "VeroScriptTemplate.h"
#include "VeroScriptConstantEvaluation.h"
#include <deque>
#include "VeroScriptAst.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptNumerics.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript
{
	namespace
	{
		using EStatus = ETemplateEvaluationStatus;
		using FResult = FTemplateEvaluationResult;
		constexpr std::size_t MaxIdentityBytes = 1048576;

		std::uint64_t Mask(const std::uint8_t Width)
		{
			return Width == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << Width) - 1;
		}

		bool ValidType(const FTemplateValueType &Type)
		{
			if (Type.Name.empty() || Type.Name.size() > MaxIdentityBytes || Type.Name.find('\0') != std::string::npos)
			{
				return false;
			}
			if (Type.bEnum)
			{
				return !Type.bBoolean && (Type.Width == 8 || Type.Width == 16 || Type.Width == 32 || Type.Width == 64);
			}
			FTemplateValueType Primitive;
			return !Type.bScopedEnum && TryGetTemplateValueType(Type.Name, Primitive) && Primitive.Name == Type.Name &&
			    Primitive.Width == Type.Width && Primitive.bUnsigned == Type.bUnsigned &&
			    Primitive.bBoolean == Type.bBoolean;
		}

		bool ValidValue(const FTemplateConstant &Value)
		{
			return ValidType(Value.Type) && (Value.Bits & ~Mask(Value.Type.Width)) == 0;
		}

		FResult Failure(const EStatus Status, std::string Reason, const FSourceLocation &Location)
		{
			FResult Result;
			Result.Status = Status;
			Result.Reason = std::move(Reason);
			Result.Location = Location;
			return Result;
		}

		FResult Constant(FTemplateConstant Value, const FSourceLocation &Location = {})
		{
			FResult Result;
			Result.Status = EStatus::Constant;
			Result.Value = std::move(Value);
			Result.Location = Location;
			return Result;
		}

		FTemplateValueType Primitive(const std::string_view Name)
		{
			FTemplateValueType Type;
			TryGetTemplateValueType(Name, Type);
			return Type;
		}

		FResult Boolean(const bool Value, const FSourceLocation &Location)
		{
			return Constant({Primitive("bool"), Value ? 1u : 0u}, Location);
		}

		FNumericOperationContext NumericContext()
		{
			FNumericOperationContext Context;
			// At most one scalar operation is widened before its declared range check.
			Context.Limits.MaxValueBits = 128;
			Context.Limits.MaxScratchBytes = 4096;
			Context.Limits.MaxInputBytes = 256;
			Context.Limits.MaxOutputBytes = 256;
			Context.Limits.MaxWorkUnits = 65536;
			return Context;
		}

		bool ToNumber(const FTemplateConstant &Value, FBigInteger &Out, FNumericOperationContext &Context,
		    FNumericError &Error)
		{
			if (Value.Type.bUnsigned || Value.Type.bBoolean ||
			    !(Value.Bits & (std::uint64_t{1} << (Value.Type.Width - 1))))
			{
				return Numerics::TryFromUInt64(Value.Bits, Out, Context, Error);
			}
			const auto Magnitude = ((~Value.Bits) + 1) & Mask(Value.Type.Width);
			const auto Signed = Magnitude == (std::uint64_t{1} << 63) ? std::numeric_limits<std::int64_t>::min()
			                                                          : -static_cast<std::int64_t>(Magnitude);
			return Numerics::TryFromInt64(Signed, Out, Context, Error);
		}

		FResult NumericFailure(const FNumericError &Error, const FSourceLocation &Location)
		{
			const auto Status = Error.Status == ENumericStatus::Interrupted ? EStatus::Interrupted
			    : Error.Status == ENumericStatus::LimitExceeded             ? EStatus::LimitExceeded
			    : Error.Status == ENumericStatus::OutOfMemory               ? EStatus::OutOfMemory
			    : Error.Status == ENumericStatus::InternalError             ? EStatus::InternalError
			                                                                : EStatus::Invalid;
			return Failure(Status, Error.Reason ? Error.Reason : "invalid constant arithmetic", Location);
		}

		FResult FromNumber(const FBigInteger &Number, const FTemplateValueType &Type, FNumericOperationContext &Context,
		    const FSourceLocation &Location)
		{
			FNumericError Error;
			std::uint64_t Bits = 0;
			if (Type.bBoolean)
			{
				Bits = Number.IsZero() ? 0u : 1u;
			}
			else if (Type.bUnsigned)
			{
				if (!Numerics::TryToUInt64(Number, Bits, Context, Error))
				{
					return NumericFailure(Error, Location);
				}
				if (Bits > Mask(Type.Width))
				{
					return Failure(EStatus::Invalid, "template integer is out of range", Location);
				}
			}
			else
			{
				std::int64_t Signed = 0;
				if (!Numerics::TryToInt64(Number, Signed, Context, Error))
				{
					return NumericFailure(Error, Location);
				}
				if (Type.Width != 64)
				{
					const auto Limit = std::int64_t{1} << (Type.Width - 1);
					if (Signed < -Limit || Signed >= Limit)
					{
						return Failure(EStatus::Invalid, "template integer is out of range", Location);
					}
				}
				Bits = static_cast<std::uint64_t>(Signed) & Mask(Type.Width);
			}
			return Constant({Type, Bits}, Location);
		}

		bool SameType(const FTemplateValueType &A, const FTemplateValueType &B)
		{
			return A.Name == B.Name && A.Width == B.Width && A.bUnsigned == B.bUnsigned && A.bBoolean == B.bBoolean &&
			    A.bEnum == B.bEnum && A.bScopedEnum == B.bScopedEnum;
		}

		bool Comparison(const EBinaryOp Op)
		{
			return Op == EBinaryOp::Equal || Op == EBinaryOp::NotEqual || Op == EBinaryOp::Less ||
			    Op == EBinaryOp::LessEqual || Op == EBinaryOp::Greater || Op == EBinaryOp::GreaterEqual;
		}

		struct FNodeKey
		{
			const FExpr *Expr;
			const FTemplateEvaluationContext *Context;

			bool operator==(const FNodeKey &Other) const noexcept
			{
				return Expr == Other.Expr && Context == Other.Context;
			}
		};

		struct FNodeHash
		{
			std::size_t operator()(const FNodeKey &Key) const noexcept
			{
				return std::hash<const void *>{}(Key.Expr) ^ (std::hash<const void *>{}(Key.Context) << 1);
			}
		};

		class FEvaluator
		{
		public:
			explicit FEvaluator(const FTemplateEvaluationContext &InContext) : Root(InContext), Context(&InContext)
			{
				EvaluationState = InContext.State ? InContext.State : std::make_shared<FConstantEvaluationState>();
				Numbers = NumericContext();
				Numbers.Work.UserData = this;
				Numbers.Work.Check = [](void *Data, const std::uint64_t Work)
				{
					return static_cast<FEvaluator *>(Data)->Check(Work);
				};
			}

			FResult Run(const FExpr &Expr)
			{
				auto Shape = Visit(Expr, true, 0);
				auto Result = Shape.Succeeded() ? Visit(Expr, false, 0) : Shape;
				if (Stop != EStatus::Constant)
				{
					Result.Status = Stop;
				}
				return Result;
			}

			FResult Validate(const FConstantFunction &Function)
			{
				return ExecuteFunction(Function, {}, true, 0);
			}

		private:
			#include "ConstantEvaluation/VeroScriptConstantFunction.inl"
			bool Check(const std::uint64_t Units)
			{
				if (Stop != EStatus::Constant)
				{
					return false;
				}
				const auto Limit = Root.MaxNodes ? Root.MaxNodes : 65536;
				if (Units > Limit || Work > Limit - Units)
				{
					Stop = EStatus::LimitExceeded;
					return false;
				}
				Work += Units;
				if (EvaluationState->MemoryBytes > EvaluationState->Limits.MaxMemoryBytes || EvaluationState->Limits.MaxWork == 0 || EvaluationState->Limits.MaxCallDepth == 0 || EvaluationState->Limits.MaxExpressionDepth == 0 || EvaluationState->Limits.MaxMemoryBytes == 0 ||
				    Units > EvaluationState->Limits.MaxWork || EvaluationState->Work > EvaluationState->Limits.MaxWork - Units)
				{
					Stop = EStatus::LimitExceeded;
					return false;
				}
				EvaluationState->Work += Units;
				if (Root.CheckWork && !Root.CheckWork(Units))
				{
					Stop = EStatus::Interrupted;
					return false;
				}
				return true;
			}

			std::string NamePath(const FExpr &Expr)
			{
				std::vector<std::string_view> Parts;
				const FExpr *Cursor = &Expr;
				const auto Limit = (std::min)(Root.MaxDepth ? Root.MaxDepth : 256u, 256u);
				while (Cursor && Cursor->Kind == EExprKind::MemberAccess)
				{
					if (!Check(1))
					{
						return {};
					}
					if (Parts.size() >= Limit)
					{
						Stop = EStatus::LimitExceeded;
						return {};
					}
					const auto &Member = static_cast<const FMemberAccessExpr &>(*Cursor);
					if (Member.bExplicitTemplateArguments || !Member.TypeArgs.empty())
					{
						return {};
					}
					Parts.push_back(Member.Member);
					Cursor = Member.Object.get();
				}
				if (!Cursor || Cursor->Kind != EExprKind::Identifier)
				{
					return {};
				}
				const auto &Identifier = static_cast<const FIdentifierExpr &>(*Cursor);
				if (Identifier.bExplicitTemplateArguments || !Identifier.TypeArgs.empty())
				{
					return {};
				}
				if (Identifier.Name.size() > MaxIdentityBytes)
				{
					Stop = EStatus::LimitExceeded;
					return {};
				}
				std::string Result = Identifier.Name;
				for (auto It = Parts.rbegin(); It != Parts.rend(); ++It)
				{
					if (It->size() >= MaxIdentityBytes || Result.size() >= MaxIdentityBytes - It->size())
					{
						Stop = EStatus::LimitExceeded;
						return {};
					}
					Result += '.';
					Result.append(*It);
				}
				return Result;
			}

			EStatus ResolveType(const std::string_view Name, FTemplateValueType &Out)
			{
				if (Context->ResolveType)
				{
					const auto State = Context->ResolveType(Name, Out);
					if (State != EStatus::NotConstant)
					{
						return State == EStatus::Constant && !ValidType(Out) ? EStatus::Invalid : State;
					}
				}
				return TryGetTemplateValueType(Name, Out) ? EStatus::Constant : EStatus::Invalid;
			}

			FResult Symbol(const FExpr &Expr, const bool Shape, const std::uint32_t Depth)
			{
				if (Expr.Kind == EExprKind::Identifier)
				{
					if (auto *Local = FindLocal(static_cast<const FIdentifierExpr &>(Expr).Name))
					{
						auto &Frame = Frames.back();
						if (Frame.bRequiredInitializer && !Local->bConstantExpression)
						{
							return Failure(EStatus::NotConstant, "ordinary parameter or local is not a constant expression", Expr.Location);
						}
						if (Frame.bRequiredInitializer && Local->Initializer)
						{
							const auto PreviousLimit = Frame.LookupOrderLimit;
							Frame.LookupOrderLimit = (std::min)(PreviousLimit, Local->Order - 1);
							struct FRestoreLimit
							{
								FConstantFrame &Frame;
								std::uint64_t Previous;
								~FRestoreLimit()
								{
									Frame.LookupOrderLimit = Previous;
								}
							} Restore{Frame, PreviousLimit};
							auto Value = Visit(*Local->Initializer, false, Depth + 1);
							if (!Value.Succeeded()) return Value;
							return ConvertTemplateConstant(Value.Value, Local->Value.Value.Type, false, Expr.Location);
						}
						return Local->Value;
					}
				}
				const FNodeKey Key{&Expr, Context};
				auto Found = Symbols.find(Key);
				if (Found == Symbols.end())
				{
					const auto Name = NamePath(Expr);
					if (Name.empty() || !Context->ResolveName)
					{
						return Failure(Stop == EStatus::Constant ? EStatus::NotConstant : Stop,
						    "name is not a compile-time constant or exceeds its budget", Expr.Location);
					}
					Found = Symbols.emplace(Key, Context->ResolveName(Name, Expr.Location)).first;
				}
				const auto Definition = Found->second;
				if (Definition.Status != EStatus::Constant &&
				    !(Shape && Definition.Status == EStatus::Dependent && Definition.Expression))
				{
					FResult Result = Failure(Definition.Status, "unbound or nonconstant name", Expr.Location);
					Result.Value.Type = Definition.Value.Type;
					return Result;
				}
				if (!Definition.Identity.empty() && Root.RecordDependency)
				{
					Root.RecordDependency(Definition.Identity);
				}
				if (!Definition.Expression)
				{
					return ValidValue(Definition.Value)
					    ? Constant(Definition.Value, Expr.Location)
					    : Failure(EStatus::Invalid, "invalid constant binding", Expr.Location);
				}
				if (Definition.Identity.empty())
				{
					return Failure(EStatus::InternalError, "constant initializer has no declaration identity",
					    Expr.Location);
				}
				auto &Cache = Shape ? ShapeCache : ValueCache;
				if (const auto Cached = Cache.find(Definition.Identity); Cached != Cache.end())
				{
					return Cached->second;
				}
				auto &Active = Shape ? ActiveShapes : ActiveValues;
				if (!Active.insert(Definition.Identity).second)
				{
					return Failure(EStatus::Invalid, "cyclic constant initializer", Expr.Location);
				}
				const auto Group = ActiveDeclarationGroups.find(Definition.DeclarationGroup);
				const bool HasGroup = !Definition.DeclarationGroup.empty();
				if (HasGroup && Group != ActiveDeclarationGroups.end() && Definition.DeclarationOrder >= Group->second)
				{
					return Failure(EStatus::Invalid, "enum initializer refers to a nonpreceding member", Expr.Location);
				}
				const auto OldOrder = Group == ActiveDeclarationGroups.end() ? std::size_t(-1) : Group->second;
				if (HasGroup)
				{
					ActiveDeclarationGroups[Definition.DeclarationGroup] = Definition.DeclarationOrder;
				}
				const auto Previous = Context;
				Context = Definition.DeclarationContext ? Definition.DeclarationContext : Context;
				auto Result = Visit(*Definition.Expression, Shape, Depth + 1);
				if ((Result.Succeeded() || Result.Status == EStatus::Dependent) && !Definition.DeclaredTypeName.empty())
				{
					FTemplateValueType Type;
					const auto State = ResolveType(Definition.DeclaredTypeName, Type);
					if (State != EStatus::Constant)
					{
						Result = Failure(State, "invalid constant declaration type", Expr.Location);
					}
					else if (Result.Status == EStatus::Dependent)
					{
						Result.Value.Type = Type;
					}
					else
					{
						Result = ConvertTemplateConstant(Result.Value, Type, Definition.bExplicitConversion, Expr.Location);
					}
				}
				if (Result.Succeeded() && Definition.Status == EStatus::Dependent)
				{
					Result.Status = EStatus::Dependent;
				}
				Context = Previous;
				if (HasGroup)
				{
					if (OldOrder == std::size_t(-1))
					{
						ActiveDeclarationGroups.erase(Definition.DeclarationGroup);
					}
					else
					{
						ActiveDeclarationGroups[Definition.DeclarationGroup] = OldOrder;
					}
				}
				Active.erase(Definition.Identity);
				if (!Shape && Result.Succeeded() && Root.RecordConstant)
				{
					Root.RecordConstant(Definition.Identity, Result.Value);
				}
				if ((Result.Status == EStatus::Constant || Result.Status == EStatus::Dependent) && CanCache(Definition.Identity.size() + 256))
				{
					Cache.emplace(Definition.Identity, Result);
				}
				return Result;
			}

			FResult Visit(const FExpr &Expr, const bool Shape, const std::uint32_t Depth)
			{
				if (!Check(1))
				{
					return Failure(Stop, "constant evaluation interrupted or exhausted its budget", Expr.Location);
				}
				if (EvaluationState->NativeDepth >= 256 || Depth > EvaluationState->Limits.MaxExpressionDepth)
				{
					return Failure(EStatus::LimitExceeded, "constant native recursion depth exceeded", Expr.Location);
				}
				++EvaluationState->NativeDepth;
				struct FPopDepth
				{
					FConstantEvaluationState &State;
					~FPopDepth()
					{
						--State.NativeDepth;
					}
				} PopDepth{*EvaluationState};
				if (Depth > (std::min)(Root.MaxDepth ? Root.MaxDepth : 256u, 256u))
				{
					return Failure(EStatus::LimitExceeded, "constant expression is too deeply nested", Expr.Location);
				}
				const FNodeKey Key{&Expr, Context};
				if (Shape && Frames.empty())
				{
					if (const auto Found = Shapes.find(Key); Found != Shapes.end())
					{
						return Found->second;
					}
				}
				auto Result = VisitNode(Expr, Shape, Depth);
				if (Shape && Frames.empty() && (Result.Status == EStatus::Constant || Result.Status == EStatus::Dependent) && CanCache(256))
				{
					Shapes.emplace(Key, Result);
				}
				return Result;
			}

			FResult VisitNode(const FExpr &Expr, const bool Shape, const std::uint32_t Depth)
			{
				switch (Expr.Kind)
				{
				case EExprKind::IntegerLiteral:
					{
						const auto &Literal = static_cast<const FIntegerLiteralExpr &>(Expr);
						const auto Bits = Literal.bIs64
						    ? (Literal.bUnsigned ? Literal.UnsignedValue64
						                         : static_cast<std::uint64_t>(Literal.Value64))
						    : (Literal.bUnsigned ? Literal.UnsignedValue
						                         : static_cast<std::uint32_t>(Literal.Value));
						return Constant({Primitive(Literal.bIs64 ? (Literal.bUnsigned ? "uint64" : "int64")
						                                         : (Literal.bUnsigned ? "unsigned int" : "int")),
						                    Bits},
						    Expr.Location);
					}
				case EExprKind::CharacterLiteral:
					{
						return Constant(
						    {Primitive("char"),
						        static_cast<std::uint8_t>(static_cast<const FCharacterLiteralExpr &>(Expr).Value)},
						    Expr.Location);
					}
				case EExprKind::BooleanLiteral:
					{
						return Boolean(static_cast<const FBooleanLiteralExpr &>(Expr).Value, Expr.Location);
					}
				case EExprKind::Identifier:
				case EExprKind::MemberAccess:
					{
						return Symbol(Expr, Shape, Depth);
					}
				case EExprKind::Call:
					{
						const auto &Call = static_cast<const FCallExpr &>(Expr);
						if (Call.Callee != "cast" || Call.CalleeExpr || Call.TypeArgs.size() != 1 ||
						    Call.GetArguments().size() != 1)
						{
							return this->Call(Expr, Shape, Depth);
						}
						if (!Call.GetArguments()[0])
						{
							return Failure(EStatus::Invalid, "missing constant cast operand", Expr.Location);
						}
						FTemplateValueType Type;
						const auto State = ResolveType(Call.TypeArgs[0], Type);
						auto Value = Visit(*Call.GetArguments()[0], Shape, Depth + 1);
						if (Value.Status != EStatus::Constant && Value.Status != EStatus::Dependent)
						{
							return Value;
						}
						if (State != EStatus::Constant)
						{
							return Failure(State, "unsupported or dependent constant cast type", Expr.Location);
						}
						if (Value.Status == EStatus::Dependent)
						{
							Value.Value.Type = Type;
							return Value;
						}
						return Shape ? Constant({Type, 0}, Expr.Location)
						             : ConvertTemplateConstant(Value.Value, Type, true, Expr.Location);
					}
				case EExprKind::MethodCall:
					return Call(Expr, Shape, Depth);
				case EExprKind::Assign:
					return Assign(static_cast<const FAssignExpr &>(Expr), Shape, Depth);
				case EExprKind::Unary:
					{
						const auto &Unary = static_cast<const FUnaryExpr &>(Expr);
						if (!Unary.Operand)
						{
							return Failure(EStatus::Invalid, "missing unary constant operand", Expr.Location);
						}
						auto Value = Visit(*Unary.Operand, Shape, Depth + 1);
						if (Value.Status != EStatus::Constant && Value.Status != EStatus::Dependent)
						{
							return Value;
						}
						if (Value.Value.Type.bScopedEnum || (Unary.Op != EUnaryOp::Not && Value.Value.Type.bBoolean))
						{
							return Failure(EStatus::Invalid, "invalid unary constant operand", Expr.Location);
						}
						if (Value.Status == EStatus::Dependent)
						{
							if (Unary.Op == EUnaryOp::Not)
							{
								Value.Value.Type = Primitive("bool");
							}
							return Value;
						}
						if (Unary.Op == EUnaryOp::Not)
						{
							return Boolean(Shape || !Value.Value.Bits, Expr.Location);
						}
						const auto Type = Primitive(Value.Value.Type.Width == 64
						        ? (Value.Value.Type.bUnsigned ? "uint64" : "int64")
						        : (Value.Value.Type.bUnsigned ? "unsigned int" : "int"));
						if (Shape)
						{
							return Constant({Type, 0}, Expr.Location);
						}
						FNumericError Error;
						FBigInteger Number, Result;
						if (!ToNumber(Value.Value, Number, Numbers, Error))
						{
							return NumericFailure(Error, Expr.Location);
						}
						if (Unary.Op == EUnaryOp::BitNot)
						{
							const auto Converted = FromNumber(Number, Type, Numbers, Expr.Location);
							return Converted.Succeeded()
							    ? Constant({Type, (~Converted.Value.Bits) & Mask(Type.Width)}, Expr.Location)
							    : Converted;
						}
						if (!Numerics::TryNegate(Number, Result, Numbers, Error))
						{
							return NumericFailure(Error, Expr.Location);
						}
						return FromNumber(Result, Type, Numbers, Expr.Location);
					}
				case EExprKind::Binary:
					{
						return Binary(static_cast<const FBinaryExpr &>(Expr), Shape, Depth);
					}
				case EExprKind::Conditional:
					{
						const auto &Conditional = static_cast<const FConditionalExpr &>(Expr);
						if (!Conditional.Condition || !Conditional.ThenExpr || !Conditional.ElseExpr)
						{
							return Failure(EStatus::Invalid, "missing conditional constant operand", Expr.Location);
						}
						auto Condition = Visit(*Conditional.Condition, Shape, Depth + 1);
						if (Condition.Status != EStatus::Constant && Condition.Status != EStatus::Dependent)
						{
							return Condition;
						}
						if (Condition.Value.Type.bScopedEnum)
						{
							return Failure(EStatus::Invalid, "scoped enum is not a boolean condition", Expr.Location);
						}
						if (!Shape)
						{
							if (!Condition.Succeeded())
							{
								return Condition;
							}
							const auto Common =
							    MergeTypes(Visit(*Conditional.ThenExpr, true, Depth + 1),
							        Visit(*Conditional.ElseExpr, true, Depth + 1), Expr.Location);
							if (!Common.Succeeded())
							{
								return Common;
							}
							auto Selected = Visit(Condition.Value.Bits ? *Conditional.ThenExpr : *Conditional.ElseExpr,
							    false, Depth + 1);
							return Selected.Succeeded() ? ConvertTemplateConstant(Selected.Value, Common.Value.Type,
							                                  false, Expr.Location)
							                            : Selected;
						}
						auto Left = Visit(*Conditional.ThenExpr, true, Depth + 1);
						auto Right = Visit(*Conditional.ElseExpr, true, Depth + 1);
						auto Result = MergeTypes(Left, Right, Expr.Location);
						if (Result.Succeeded() && Condition.Status == EStatus::Dependent)
						{
							Result.Status = EStatus::Dependent;
						}
						return Result;
					}
				default:
					{
						return Failure(EStatus::NotConstant, "expression is not a supported pure scalar constant",
						    Expr.Location);
					}
				}
			}

			FResult MergeTypes(const FResult &Left, const FResult &Right, const FSourceLocation &Location)
			{
				if (Left.Status != EStatus::Constant && Left.Status != EStatus::Dependent)
				{
					return Left;
				}
				if (Right.Status != EStatus::Constant && Right.Status != EStatus::Dependent)
				{
					return Right;
				}
				const bool Dependent = Left.Status == EStatus::Dependent || Right.Status == EStatus::Dependent;
				if (!ValidType(Left.Value.Type) || !ValidType(Right.Value.Type))
				{
					return Failure(Dependent ? EStatus::Dependent : EStatus::Invalid, {}, Location);
				}
				const auto &A = Left.Value.Type;
				const auto &B = Right.Value.Type;
				if (SameType(A, B))
				{
					auto Result = Constant({A, 0}, Location);
					Result.Status = Dependent ? EStatus::Dependent : EStatus::Constant;
					return Result;
				}
				if (A.bBoolean || B.bBoolean || A.bScopedEnum || B.bScopedEnum || A.bUnsigned != B.bUnsigned)
				{
					return Failure(EStatus::Invalid, "incompatible scalar constant types", Location);
				}
				auto Result =
				    Constant({Primitive(A.Width == 64 || B.Width == 64 ? (A.bUnsigned ? "uint64" : "int64")
				                                                       : (A.bUnsigned ? "unsigned int" : "int")),
				                 0},
				        Location);
				Result.Status = Dependent ? EStatus::Dependent : EStatus::Constant;
				return Result;
			}

			FResult BinaryType(const FBinaryExpr &Expr, const FResult &Left, const FResult &Right)
			{
				const auto &A = Left.Value.Type;
				const auto &B = Right.Value.Type;
				const bool Logical = Expr.Op == EBinaryOp::LogicalAnd || Expr.Op == EBinaryOp::LogicalOr;
				const bool Compare = Comparison(Expr.Op);
				const bool Shift = Expr.Op == EBinaryOp::ShiftLeft || Expr.Op == EBinaryOp::ShiftRight;
				const bool Dependent = Left.Status == EStatus::Dependent || Right.Status == EStatus::Dependent;
				if ((!Compare && (A.bScopedEnum || B.bScopedEnum)) ||
				    (!Compare && !Logical && (A.bBoolean || B.bBoolean)))
				{
					return Failure(EStatus::Invalid, "invalid scalar constant operands", Expr.Location);
				}
				if (!ValidType(A) || !ValidType(B))
				{
					return Failure(Dependent ? EStatus::Dependent : EStatus::Invalid, "unbound constant operand type",
					    Expr.Location);
				}
				if (Compare && (A.bScopedEnum || B.bScopedEnum) && !SameType(A, B))
				{
					return Failure(EStatus::Invalid, "scoped enum constant requires a matching enum operation",
					    Expr.Location);
				}
				if (!Logical && !Compare && !Shift && A.bUnsigned != B.bUnsigned)
				{
					return Failure(EStatus::Invalid, "mixed signed and unsigned constant arithmetic requires a cast",
					    Expr.Location);
				}
				const auto Width = Shift ? A.Width : (std::max)(A.Width, B.Width);
				auto Result =
				    Constant({Logical || Compare ? Primitive("bool")
				                                 : Primitive(Width == 64 ? (A.bUnsigned ? "uint64" : "int64")
				                                                         : (A.bUnsigned ? "unsigned int" : "int")),
				                 0},
				        Expr.Location);
				if (Dependent)
				{
					Result.Status = EStatus::Dependent;
				}
				return Result;
			}

			FResult Binary(const FBinaryExpr &Expr, const bool Shape, const std::uint32_t Depth)
			{
				if (Expr.Op == EBinaryOp::In)
				{
					return Failure(EStatus::NotConstant, "membership is not a scalar constant operation",
					    Expr.Location);
				}
				if (!Expr.Left || !Expr.Right)
				{
					return Failure(EStatus::Invalid, "missing binary constant operand", Expr.Location);
				}
				auto Left = Visit(*Expr.Left, Shape, Depth + 1);
				if (Left.Status != EStatus::Constant && Left.Status != EStatus::Dependent)
				{
					return Left;
				}
				const bool Logical = Expr.Op == EBinaryOp::LogicalAnd || Expr.Op == EBinaryOp::LogicalOr;
				if (!Shape && Logical && Left.Succeeded())
				{
					if ((Expr.Op == EBinaryOp::LogicalAnd && !Left.Value.Bits) ||
					    (Expr.Op == EBinaryOp::LogicalOr && Left.Value.Bits))
					{
						return Boolean(Left.Value.Bits != 0, Expr.Location);
					}
				}
				auto Right = Visit(*Expr.Right, Shape, Depth + 1);
				if (Right.Status != EStatus::Constant && Right.Status != EStatus::Dependent)
				{
					return Right;
				}
				auto Type = BinaryType(Expr, Left, Right);
				if (Shape || !Type.Succeeded())
				{
					return Type;
				}
				if (Logical)
				{
					return Boolean(Expr.Op == EBinaryOp::LogicalAnd ? Left.Value.Bits && Right.Value.Bits
					                                                : Left.Value.Bits || Right.Value.Bits,
					    Expr.Location);
				}
				if (Comparison(Expr.Op))
				{
					if (Shape)
					{
						return Boolean(false, Expr.Location);
					}
					FNumericError Error;
					FBigInteger X, Y;
					int Order = 0;
					if (!ToNumber(Left.Value, X, Numbers, Error) || !ToNumber(Right.Value, Y, Numbers, Error) ||
					    !Numerics::TryCompare(X, Y, Order, Numbers, Error))
					{
						return NumericFailure(Error, Expr.Location);
					}
					return Boolean(Expr.Op == EBinaryOp::Equal ? Order == 0
					        : Expr.Op == EBinaryOp::NotEqual   ? Order != 0
					        : Expr.Op == EBinaryOp::Less       ? Order < 0
					        : Expr.Op == EBinaryOp::LessEqual  ? Order <= 0
					        : Expr.Op == EBinaryOp::Greater    ? Order > 0
					                                           : Order >= 0,
					    Expr.Location);
				}
				const bool Shift = Expr.Op == EBinaryOp::ShiftLeft || Expr.Op == EBinaryOp::ShiftRight;
				FNumericError Error;
				FBigInteger X, Y, Number;
				if (!ToNumber(Left.Value, X, Numbers, Error) || !ToNumber(Right.Value, Y, Numbers, Error))
				{
					return NumericFailure(Error, Expr.Location);
				}
				if (Shift)
				{
					std::int64_t Count = 0;
					if (!Numerics::TryToInt64(Y, Count, Numbers, Error) || Count < 0 ||
					    Count >= Type.Value.Type.Width || (Expr.Op == EBinaryOp::ShiftLeft && X.IsNegative()))
					{
						return Failure(EStatus::Invalid, "invalid constant shift", Expr.Location);
					}
					if (!Numerics::TryShift(X, Count, Expr.Op == EBinaryOp::ShiftLeft, Number, Numbers, Error))
					{
						return NumericFailure(Error, Expr.Location);
					}
				}
				else
				{
					ENumericBinaryOperation Op;
					switch (Expr.Op)
					{
					case EBinaryOp::Add:
						{
							Op = ENumericBinaryOperation::Add;
							break;
						}
					case EBinaryOp::Subtract:
						{
							Op = ENumericBinaryOperation::Subtract;
							break;
						}
					case EBinaryOp::Multiply:
						{
							Op = ENumericBinaryOperation::Multiply;
							break;
						}
					case EBinaryOp::Divide:
						{
							Op = ENumericBinaryOperation::Divide;
							break;
						}
					case EBinaryOp::Modulo:
						{
							Op = ENumericBinaryOperation::Remainder;
							break;
						}
					case EBinaryOp::BitAnd:
						{
							Op = ENumericBinaryOperation::And;
							break;
						}
					case EBinaryOp::BitOr:
						{
							Op = ENumericBinaryOperation::Or;
							break;
						}
					case EBinaryOp::BitXor:
						{
							Op = ENumericBinaryOperation::Xor;
							break;
						}
					default:
						{
							return Failure(EStatus::Invalid, "unsupported constant operator", Expr.Location);
						}
					}
					if (!Numerics::TryBinary(Op, X, Y, Number, Numbers, Error))
					{
						return NumericFailure(Error, Expr.Location);
					}
				}
				return FromNumber(Number, Type.Value.Type, Numbers, Expr.Location);
			}

			std::shared_ptr<FConstantEvaluationState> EvaluationState;
			const FTemplateEvaluationContext &Root;
			const FTemplateEvaluationContext *Context;
			FNumericOperationContext Numbers;
			std::uint64_t Work = 0;
			EStatus Stop = EStatus::Constant;
			std::unordered_map<FNodeKey, FTemplateConstantSymbol, FNodeHash> Symbols;
			std::unordered_map<FNodeKey, FResult, FNodeHash> Shapes;
			std::unordered_map<std::string, FResult> ShapeCache, ValueCache;
			std::unordered_set<std::string> ActiveShapes, ActiveValues;
			std::unordered_map<std::string, std::size_t> ActiveDeclarationGroups;
		};

		void AppendString(std::string &Out, const std::string_view Value)
		{
			Out += std::to_string(Value.size());
			Out += ':';
			Out.append(Value);
		}

		bool ReadString(std::string_view &Input, std::string &Out)
		{
			const auto Colon = Input.find(':');
			if (Colon == std::string_view::npos || Colon == 0 || Colon > 10)
			{
				return false;
			}
			std::uint64_t Size = 0;
			const auto Parsed = std::from_chars(Input.data(), Input.data() + Colon, Size);
			if (Parsed.ec != std::errc{} || Parsed.ptr != Input.data() + Colon || Size > MaxIdentityBytes ||
			    Size > Input.size() - Colon - 1)
			{
				return false;
			}
			Input.remove_prefix(Colon + 1);
			Out.assign(Input.substr(0, static_cast<std::size_t>(Size)));
			Input.remove_prefix(static_cast<std::size_t>(Size));
			return Out.find('\0') == std::string::npos;
		}
	} // namespace

	void FTemplateParameterList::Assign(std::vector<FTemplateParameter> InParameters,
	    const std::size_t InEnclosingCount)
	{
		if (InEnclosingCount > InParameters.size())
		{
			throw std::invalid_argument("invalid enclosing template parameter count");
		}
		std::vector<std::string> InNames, InDefaults;
		InNames.reserve(InParameters.size());
		InDefaults.reserve(InParameters.size());
		for (std::size_t Index = 0; Index < InParameters.size(); ++Index)
		{
			const auto &Parameter = InParameters[Index];
			InNames.push_back(Parameter.Name);
			InDefaults.push_back(Index < InEnclosingCount            ? std::string{}
			        : Parameter.Kind == ETemplateParameterKind::Type ? Parameter.DefaultTypeName
			                                                         : Parameter.DefaultSource);
		}
		Parameters.swap(InParameters);
		Names.swap(InNames);
		Defaults.swap(InDefaults);
		EnclosingCount = InEnclosingCount;
	}

	bool TryGetTemplateValueType(std::string_view Name, FTemplateValueType &OutType)
	{
		while (!Name.empty() && Name.front() == ' ')
		{
			Name.remove_prefix(1);
		}
		while (!Name.empty() && Name.back() == ' ')
		{
			Name.remove_suffix(1);
		}
		if (Name.substr(0, 6) == "const ")
		{
			Name.remove_prefix(6);
		}
		FTemplateValueType Type;
		if (Name == "bool")
		{
			Type = {"bool", 1, true, true};
		}
		else if (Name == "char" || Name == "int8" || Name == "signed char")
		{
			Type = {"char", 8};
		}
		else if (Name == "uint8" || Name == "unsigned char")
		{
			Type = {"unsigned char", 8, true};
		}
		else if (Name == "short" || Name == "int16" || Name == "signed short")
		{
			Type = {"short", 16};
		}
		else if (Name == "uint16" || Name == "unsigned short")
		{
			Type = {"unsigned short", 16, true};
		}
		else if (Name == "int" || Name == "int32" || Name == "signed int")
		{
			Type = {"int", 32};
		}
		else if (Name == "uint32" || Name == "unsigned int" || Name == "unsigned")
		{
			Type = {"unsigned int", 32, true};
		}
		else if (Name == "int64" || Name == "long" || Name == "signed long")
		{
			Type = {"int64", 64};
		}
		else if (Name == "uint64" || Name == "unsigned long")
		{
			Type = {"uint64", 64, true};
		}
		else
		{
			return false;
		}
		OutType = std::move(Type);
		return true;
	}

	FTemplateEvaluationResult ConvertTemplateConstant(const FTemplateConstant &Value, const FTemplateValueType &Type,
	    const bool Explicit, const FSourceLocation &Location)
	{
		if (!ValidValue(Value) || !ValidType(Type))
		{
			return Failure(EStatus::Invalid, "invalid template scalar representation", Location);
		}
		if (!Explicit && ((Type.bBoolean != Value.Type.bBoolean) || (Type.bEnum && (!Value.Type.bEnum || Type.Name != Value.Type.Name)) || (Value.Type.bScopedEnum && (!Type.bEnum || Type.Name != Value.Type.Name))))
		{
			return Failure(EStatus::Invalid, "template constant requires an explicit scalar conversion", Location);
		}
		auto Context = NumericContext();
		FNumericError Error;
		FBigInteger Number;
		if (!ToNumber(Value, Number, Context, Error))
		{
			return NumericFailure(Error, Location);
		}
		return FromNumber(Number, Type, Context, Location);
	}

	std::unique_ptr<FExpr> MakeConstantExpression(const FTemplateConstant &Value, const FSourceLocation &Location)
	{
		if (!ValidValue(Value))
		{
			throw std::invalid_argument("invalid constant literal value");
		}
		std::unique_ptr<FExpr> Result;
		if (Value.Type.bBoolean)
		{
			Result = std::make_unique<FBooleanLiteralExpr>(Value.Bits != 0);
		}
		else
		{
			std::uint64_t Bits = Value.Bits & Mask(Value.Type.Width);
			if (!Value.Type.bUnsigned && Value.Type.Width < 64 && (Bits & (std::uint64_t{1} << (Value.Type.Width - 1))))
			{
				Bits |= ~Mask(Value.Type.Width);
			}
			if (Value.Type.bUnsigned)
			{
				Result = Value.Type.Width == 64
				    ? std::make_unique<FIntegerLiteralExpr>(Bits, true)
				    : std::make_unique<FIntegerLiteralExpr>(static_cast<std::uint32_t>(Bits), true);
			}
			else
			{
				Result = Value.Type.Width == 64
				    ? std::make_unique<FIntegerLiteralExpr>(DecodeConstantSignedBits(Bits))
				    : std::make_unique<FIntegerLiteralExpr>(static_cast<std::int32_t>(DecodeConstantSignedBits(Bits)));
			}
			Result->Location = Location;
			const std::string Natural = Value.Type.Width == 64 ? (Value.Type.bUnsigned ? "uint64" : "int64") : (Value.Type.bUnsigned ? "unsigned int" : "int");
			if (Value.Type.bEnum || Value.Type.Name != Natural)
			{
				auto Cast = std::make_unique<FCallExpr>("cast", std::vector<std::unique_ptr<FExpr>>{});
				Cast->TypeArgs.push_back(Value.Type.Name);
				Cast->Arguments.push_back(std::move(Result));
				Result = std::move(Cast);
			}
		}
		Result->Location = Location;
		return Result;
	}

	FTemplateEvaluationResult ValidateConstantFunction(const FConstantFunction &Function, const FTemplateEvaluationContext &Context)
	{
		try
		{
			return FEvaluator(Context).Validate(Function);
		}
		catch (const FAnalysisCancelled &)
		{
			throw;
		}
		catch (const std::bad_alloc &)
		{
			return Failure(EStatus::OutOfMemory, "constant function validation allocation failed", {});
		}
		catch (const std::exception &Error)
		{
			return Failure(EStatus::InternalError, Error.what(), {});
		}
	}

	FTemplateEvaluationResult EvaluateTemplateConstant(const FExpr &Expr, const FTemplateEvaluationContext &Context)
	{
		try
		{
			return FEvaluator(Context).Run(Expr);
		}
		catch (const FAnalysisCancelled &)
		{
			throw;
		}
		catch (const std::bad_alloc &)
		{
			return Failure(EStatus::OutOfMemory, "constant evaluation allocation failed", Expr.Location);
		}
		catch (const std::exception &Error)
		{
			return Failure(EStatus::InternalError, Error.what(), Expr.Location);
		}
	}

	std::string EncodeTemplateArgument(const FTemplateArgument &Argument)
	{
		std::string Out;
		if (Argument.Kind == ETemplateParameterKind::Type)
		{
			if (Argument.TypeName.empty() || Argument.TypeName.size() > MaxIdentityBytes ||
			    Argument.TypeName.find('\0') != std::string::npos)
			{
				return {};
			}
			Out = "T";
			AppendString(Out, Argument.TypeName);
			return Out;
		}
		if (Argument.Kind != ETemplateParameterKind::Value || !ValidValue(Argument.Value))
		{
			return {};
		}
		const auto &Type = Argument.Value.Type;
		Out = "V";
		AppendString(Out, Type.Name);
		Out += std::to_string(Type.Width) + ':';
		Out += std::to_string((Type.bUnsigned ? 1u : 0u) | (Type.bBoolean ? 2u : 0u) | (Type.bEnum ? 4u : 0u) |
		           (Type.bScopedEnum ? 8u : 0u)) +
		    ':';
		Out += std::to_string(Argument.Value.Bits);
		return Out;
	}

	bool DecodeTemplateArgument(std::string_view Input, FTemplateArgument &Out)
	{
		if (Input.empty())
		{
			return false;
		}
		const auto Original = Input;
		FTemplateArgument Argument;
		const char Tag = Input.front();
		Input.remove_prefix(1);
		if (Tag == 'T')
		{
			if (!ReadString(Input, Argument.TypeName) || Argument.TypeName.empty() || !Input.empty())
			{
				return false;
			}
		}
		else if (Tag == 'V')
		{
			Argument.Kind = ETemplateParameterKind::Value;
			if (!ReadString(Input, Argument.Value.Type.Name))
			{
				return false;
			}
			const auto ReadNumber = [&](std::uint64_t &Value, const bool Last)
			{
				const auto End = Last ? Input.size() : Input.find(':');
				if (End == std::string_view::npos || !End)
				{
					return false;
				}
				const auto Parsed = std::from_chars(Input.data(), Input.data() + End, Value);
				if (Parsed.ec != std::errc{} || Parsed.ptr != Input.data() + End)
				{
					return false;
				}
				Input.remove_prefix(End + (Last ? 0u : 1u));
				return true;
			};
			std::uint64_t Width = 0, Flags = 0;
			if (!ReadNumber(Width, false) || !ReadNumber(Flags, false) || !ReadNumber(Argument.Value.Bits, true) ||
			    !Input.empty() || Width > 64 || Flags > 15)
			{
				return false;
			}
			auto &Type = Argument.Value.Type;
			Type.Width = static_cast<std::uint8_t>(Width);
			Type.bUnsigned = (Flags & 1u) != 0;
			Type.bBoolean = (Flags & 2u) != 0;
			Type.bEnum = (Flags & 4u) != 0;
			Type.bScopedEnum = (Flags & 8u) != 0;
			if (!ValidValue(Argument.Value))
			{
				return false;
			}
		}
		else
		{
			return false;
		}
		if (EncodeTemplateArgument(Argument) != Original)
		{
			return false;
		}
		Out = std::move(Argument);
		return true;
	}

	std::string MakeTemplateInstanceKey(const std::string_view DeclarationId, const std::string_view OwnerId,
	    const std::vector<FTemplateArgument> &Arguments)
	{
		if (DeclarationId.empty() || DeclarationId.size() > MaxIdentityBytes || OwnerId.size() > MaxIdentityBytes ||
		    DeclarationId.find('\0') != std::string_view::npos || OwnerId.find('\0') != std::string_view::npos)
		{
			return {};
		}
		std::string Result = "@template:";
		AppendString(Result, DeclarationId);
		AppendString(Result, OwnerId);
		Result += std::to_string(Arguments.size()) + ':';
		for (const auto &Argument : Arguments)
		{
			const auto Encoded = EncodeTemplateArgument(Argument);
			if (Encoded.empty())
			{
				return {};
			}
			AppendString(Result, Encoded);
		}
		return Result;
	}

	bool EqualTemplateParameters(const FTemplateParameterList &Left, const FTemplateParameterList &Right)
	{
		if (Left.GetEnclosingCount() != Right.GetEnclosingCount() ||
		    Left.GetParameters().size() != Right.GetParameters().size())
		{
			return false;
		}
		for (std::size_t Index = 0; Index < Left.GetParameters().size(); ++Index)
		{
			const auto &A = Left.GetParameters()[Index];
			const auto &B = Right.GetParameters()[Index];
			if (std::tie(A.Id, A.Name, A.Namespace, A.Kind, A.ValueTypeName, A.DefaultTypeName, A.DefaultSource) !=
			    std::tie(B.Id, B.Name, B.Namespace, B.Kind, B.ValueTypeName, B.DefaultTypeName, B.DefaultSource))
			{
				return false;
			}
		}
		return true;
	}

	bool HasMethodOrValueTemplates(const FProgram &Program)
	{
		const auto Values = [](const auto &Declaration)
		{
			return std::any_of(Declaration.TemplateParameters.GetParameters().begin(),
			    Declaration.TemplateParameters.GetParameters().end(),
			    [](const auto &Parameter)
			{
				return Parameter.Kind == ETemplateParameterKind::Value;
			});
		};
		const auto Types = [&](const auto &Declarations)
		{
			return std::any_of(Declarations.begin(), Declarations.end(),
			    [&](const auto &Declaration)
			{
				return Values(Declaration) ||
				    std::any_of(Declaration.Methods.begin(), Declaration.Methods.end(),
				        [](const auto &Method)
				{
					return !TemplateParameterNames(Method).empty();
				});
			});
		};
		return Types(Program.Classes) || Types(Program.Structs) || Types(Program.Interfaces) ||
		    std::any_of(Program.Functions.begin(), Program.Functions.end(), Values);
	}

	bool RequiresCoreTemplateExport(const FProgram &Program)
	{
		if (HasMethodOrValueTemplates(Program))
		{
			return true;
		}
		// A generic body can have different mandatory constants per instance.
		// Standalone emitters do not yet carry that per-instance lowering table.
		const auto HasConstantBody = [](const auto &Function)
		{
			bool Found = Function.bConstexpr;
			if (Function.Body)
			{
				VisitBlockNodes(*Function.Body, [](const FExpr &)
				{
				}, [&](const FStmt &Statement)
				{
					if (Statement.Kind == EStmtKind::VarDecl)
					{
						Found = Found || static_cast<const FVarDeclStmt &>(Statement).bConstexpr;
					}
					if (Statement.Kind == EStmtKind::If)
					{
						Found = Found || static_cast<const FIfStmt &>(Statement).bConstexpr;
					}
				});
			}
			return Found;
		};
		for (const auto &Function : Program.Functions)
		{
			if (!TemplateParameterNames(Function).empty() && HasConstantBody(Function)) return true;
		}
		const auto HasGenericConstants = [&](const auto &Declarations)
		{
			for (const auto &Declaration : Declarations)
			{
				if (TemplateParameterNames(Declaration).empty())
				{
					continue;
				}
				for (const auto &Field : Declaration.Fields)
				{
					if (Field.bConstexpr) return true;
				}
				for (const auto &Method : Declaration.Methods)
				{
					if (HasConstantBody(Method)) return true;
				}
			}
			return false;
		};
		if (HasGenericConstants(Program.Classes) || HasGenericConstants(Program.Structs) || HasGenericConstants(Program.Interfaces)) return true;
		bool Found = false;
		VisitProgramNodes(
		    Program,
		    [&](const FExpr &Expression)
		{
			if (Expression.Kind == EExprKind::Identifier)
			{
				Found = Found || static_cast<const FIdentifierExpr &>(Expression).bExplicitTemplateArguments;
			}
			else if (Expression.Kind == EExprKind::MemberAccess)
			{
				Found = Found || static_cast<const FMemberAccessExpr &>(Expression).bExplicitTemplateArguments;
			}
			else if (Expression.Kind == EExprKind::Call)
			{
				const auto &Call = static_cast<const FCallExpr &>(Expression);
				if (!Call.bExplicitTemplateArguments || !Call.TypeArgs.empty())
				{
					return;
				}
				const auto Type = [&](const auto &Declarations)
				{
					return std::any_of(Declarations.begin(), Declarations.end(),
					    [&](const auto &Declaration)
					{
						return Declaration.Name == Call.Callee;
					});
				};
				Found = Found || (!Type(Program.Classes) && !Type(Program.Structs) && !Type(Program.Interfaces));
			}
		},
		    [](const FStmt &)
		{
		});
		return Found;
	}

	std::string FormatTemplateArgument(const FTemplateArgument &Argument)
	{
		if (Argument.Kind == ETemplateParameterKind::Type)
		{
			return Argument.TypeName;
		}
		if (EncodeTemplateArgument(Argument).empty())
		{
			return {};
		}
		const auto &Value = Argument.Value;
		if (Value.Type.bBoolean)
		{
			return Value.Bits != 0 ? "true" : "false";
		}
		const auto Mask = Value.Type.Width == 64 ? std::numeric_limits<std::uint64_t>::max()
		                                         : (std::uint64_t{1} << Value.Type.Width) - 1;
		std::string Number;
		if (Value.Type.bUnsigned)
		{
			Number = std::to_string(Value.Bits);
		}
		else
		{
			const auto Signed = (Value.Bits & (std::uint64_t{1} << (Value.Type.Width - 1))) != 0
			    ? -1 - static_cast<std::int64_t>((~Value.Bits) & Mask)
			    : static_cast<std::int64_t>(Value.Bits);
			Number = std::to_string(Signed);
		}
		return Value.Type.bEnum ? Value.Type.Name + "(" + Number + ")" : Number;
	}

	std::string FormatTemplateArgumentName(const std::string_view Name)
	{
		FTemplateArgument Argument;
		if (Name.substr(0, 7) == "@value:" && DecodeTemplateArgument(Name.substr(7), Argument))
		{
			return FormatTemplateArgument(Argument);
		}
		return std::string(Name);
	}

} // namespace VeroScript

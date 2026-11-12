// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

	case EExprKind::ArrayInit:
		{
			const FArrayInitExpr& Init = static_cast<const FArrayInitExpr&>(Expression);
			const std::string TargetType=Init.TypeName.empty() ? ExpressionExpectedType : Init.TypeName;
			if (TargetType.empty())
			{
				ReportError(
						"array initializer requires a container target type",
						Expression.Location);
				return false;
			}
			const std::string TypeName =
					NormalizeTypeAlias(SubstituteTypeName(TargetType));
			if (Runtime::IsNativeArrayTypeName(TypeName))
			{
				return CompileNativeArrayInit(TypeName, Init, Emitter, Expression.Location);
			}
			return CompileContainerListInit(TypeName, Init, Emitter, Expression.Location);
		}
	case EExprKind::MapInit:
		{
			const FMapInitExpr& Init = static_cast<const FMapInitExpr&>(Expression);
			if (Init.TypeName.empty())
			{
				ReportError(
						"map initializer is only allowed in a variable or field declaration",
						Expression.Location);
				return false;
			}
			const std::string TypeName =
					NormalizeTypeAlias(SubstituteTypeName(Init.TypeName));
			return CompileMapInit(TypeName, Init, Emitter, Expression.Location);
		}
	default:
		{
			ReportError("unsupported expression", Expression.Location);
			return false;
		}

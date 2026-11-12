// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Operations;

namespace VeroScript.CSharpTranspiler;

public static partial class CSharpTranspiler
{
	private sealed partial class Emitter
	{
		private readonly SortedDictionary<string, string> numericHelpers = new(StringComparer.Ordinal);
		private string? numericNamespace;

		private static bool IsIntegral(SpecialType type) => type is
			SpecialType.System_SByte or SpecialType.System_Byte or SpecialType.System_Int16
			or SpecialType.System_UInt16 or SpecialType.System_Int32 or SpecialType.System_UInt32
			or SpecialType.System_Int64 or SpecialType.System_UInt64;

		private static bool IsFloating(SpecialType type) => type is
			SpecialType.System_Single or SpecialType.System_Double;

		private static bool IsSigned(SpecialType type) => type is
			SpecialType.System_SByte or SpecialType.System_Int16 or SpecialType.System_Int32
			or SpecialType.System_Int64;

		private static (string Name, string Type, int Width) IntegralType(SpecialType type) => type switch
		{
			SpecialType.System_SByte => ("i8", "int8", 8),
			SpecialType.System_Byte => ("u8", "uint8", 8),
			SpecialType.System_Int16 => ("i16", "int16", 16),
			SpecialType.System_UInt16 => ("u16", "uint16", 16),
			SpecialType.System_Int32 => ("i32", "int", 32),
			SpecialType.System_UInt32 => ("u32", "unsigned int", 32),
			SpecialType.System_Int64 => ("i64", "int64", 64),
			SpecialType.System_UInt64 => ("u64", "uint64", 64),
			_ => throw new ArgumentOutOfRangeException(nameof(type))
		};

		private string NumericNamespace()
		{
			if (numericNamespace is not null) return numericNamespace;
			string hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(
				sourcePath + "\n" + model.SyntaxTree.GetText())))[..16].ToLowerInvariant();
			string candidate = "__vs_cs_numeric_" + hash;
			while (identifierNames.Value.Contains(candidate))
			{
				candidate += "_";
			}
			return numericNamespace = candidate;
		}

		private string NumericHelper(string name, string definition)
		{
			numericHelpers.TryAdd(name, definition);
			return "::" + NumericNamespace() + "::" + name;
		}

		private void EmitNumericHelpers()
		{
			if (numericHelpers.Count == 0) return;
			Line();
			Line("// Preserve C# scalar semantics and evaluation order.");
			Line("namespace " + NumericNamespace());
			Line("{");
			++indent;
			foreach (string definition in numericHelpers.Values)
			{
				Line(definition);
			}
			--indent;
			Line("}");
		}

		private string IntegerBits(string expression, SpecialType source)
		{
			if (!IsSigned(source)) return "cast<uint64>(" + expression + ")";
			string helper = NumericHelper("bits_i64",
				"uint64 bits_i64(int64 value) { if (value >= 0L) return cast<uint64>(value); "
				+ "return cast<uint64>((value + 9223372036854775807L) + 1L) + 9223372036854775808UL; }");
			return helper + "(cast<int64>(" + expression + "))";
		}

		private string IntegerFromBits(string bits, SpecialType target)
		{
			(string name, string type, int width) = IntegralType(target);
			string body;
			if (width == 64)
			{
				body = IsSigned(target)
					? "if (value <= 9223372036854775807UL) return cast<int64>(value); "
					  + "return (cast<int64>(value - 9223372036854775808UL) - 9223372036854775807L) - 1L;"
					: "return value;";
			}
			else
			{
				ulong modulus = 1UL << width;
				body = $"uint64 low = value & {modulus - 1}UL; ";
				body += IsSigned(target)
					? $"return cast<{type}>(low < {modulus / 2}UL ? cast<int64>(low) "
					  + $": cast<int64>(low) - {modulus}L);"
					: $"return cast<{type}>(low);";
			}
			string helper = NumericHelper("from_" + name,
				$"{type} from_{name}(uint64 value) {{ {body} }}");
			return helper + "(" + bits + ")";
		}

		private string ConvertInteger(string expression, SpecialType source, SpecialType target)
		{
			if (source == target) return expression;
			return IntegerFromBits(IntegerBits(expression, source), target);
		}

		private bool TryNumericConstant(ExpressionSyntax expression, out string text)
		{
			text = "";
			SpecialType type = model.GetTypeInfo(expression).Type?.SpecialType ?? SpecialType.None;
			if (!IsIntegral(type) && !IsFloating(type)) return false;
			Optional<object?> value = model.GetConstantValue(expression);
			if (!value.HasValue || value.Value is null) return false;
			// Keep unsupported checked/unchecked syntax explicit, even when Roslyn folds it.
			if (expression.DescendantNodesAndSelf().Any(node => node is CheckedExpressionSyntax)) return false;
			SpecialType converted = model.GetTypeInfo(expression).ConvertedType?.SpecialType ?? type;
			object numberValue = value.Value;
			if (IsIntegral(type) && IsFloating(converted))
			{
				numberValue = converted == SpecialType.System_Single
					? (object)Convert.ToSingle(numberValue, CultureInfo.InvariantCulture)
					: Convert.ToDouble(numberValue, CultureInfo.InvariantCulture);
				type = converted;
			}
			text = type switch
			{
				SpecialType.System_UInt32 => Convert.ToString(numberValue, CultureInfo.InvariantCulture) + "U",
				SpecialType.System_Int64 => Convert.ToString(numberValue, CultureInfo.InvariantCulture) + "L",
				SpecialType.System_UInt64 => Convert.ToString(numberValue, CultureInfo.InvariantCulture) + "UL",
				SpecialType.System_Single => ((float)numberValue).ToString("R", CultureInfo.InvariantCulture) + "f",
				SpecialType.System_Double => DoubleLiteral((double)numberValue),
				_ => Convert.ToString(numberValue, CultureInfo.InvariantCulture)!
			};
			if (value.Value is float single && !float.IsFinite(single)
				|| value.Value is double number && !double.IsFinite(number))
			{
				text = FailLiteral(expression, "non-finite floating-point constant");
			}
			return true;
		}

		private static string DoubleLiteral(double value)
		{
			string text = value.ToString("R", CultureInfo.InvariantCulture);
			return text.Contains('.') || text.Contains('E') ? text : text + ".0";
		}

		private string NumericBinary(string left, string right, string op, SpecialType type)
		{
			(string name, string target, int width) = IntegralType(type);
			if (op is "<<" or ">>") return $"({left} {op} ({right} & {width - 1}))";
			if (IsSigned(type) && op is "+" or "-" or "*")
			{
				string operation = op switch { "+" => "add", "-" => "sub", _ => "mul" };
				string result = IntegerFromBits(
					$"({IntegerBits("left", type)} {op} {IntegerBits("right", type)})", type);
				string helper = NumericHelper(operation + "_" + name,
					$"{target} {operation}_{name}({target} left, {target} right) {{ return {result}; }}");
				return helper + $"({left}, {right})";
			}
			if (op is "/" or "%")
			{
				string operation = op == "/" ? "div" : "mod";
				string zero = IsSigned(type) ? "0" : width == 64 ? "0UL" : "0U";
				string checks = $"if (right == {zero}) throw \"Attempted to divide by zero.\"; ";
				if (IsSigned(type))
				{
					string minimum = width == 64 ? "-9223372036854775808L" : "-2147483648";
					checks += $"if (left == {minimum} && right == -1) "
						+ "throw \"Arithmetic operation resulted in an overflow.\"; ";
				}
				string helper = NumericHelper(operation + "_" + name,
					$"{target} {operation}_{name}({target} left, {target} right) "
					+ $"{{ {checks}return left {op} right; }}");
				return helper + $"({left}, {right})";
			}
			return $"({left} {op} {right})";
		}

		private string BinaryExpression(BinaryExpressionSyntax binary)
		{
			if (model.GetOperation(binary) is not IBinaryOperation operation)
			{
				return $"({Expression(binary.Left)} {binary.OperatorToken.Text} {Expression(binary.Right)})";
			}
			SpecialType leftType = operation.LeftOperand.Type?.SpecialType ?? SpecialType.None;
			SpecialType rightType = operation.RightOperand.Type?.SpecialType ?? SpecialType.None;
			string op = binary.OperatorToken.Text;
			if (op == "+" && operation.Type?.SpecialType == SpecialType.System_String)
			{
				return "(" + StringOperand(binary.Left) + " + " + StringOperand(binary.Right) + ")";
			}
			if (op is not ("==" or "!=" or "<" or "<=" or ">" or ">=")
				&& (operation.LeftOperand.Type?.TypeKind == TypeKind.Enum
					|| operation.RightOperand.Type?.TypeKind == TypeKind.Enum))
			{
				return FailLiteral(binary, "enum arithmetic or bitwise operation");
			}
			if (op is "/" or "%" && (IsFloating(leftType) || IsFloating(rightType)))
			{
				return FailLiteral(binary, "floating-point division or remainder (different zero/NaN semantics)");
			}
			string left = ConvertedNumericExpression(binary.Left, leftType);
			string right = ConvertedNumericExpression(binary.Right, rightType);
			if (IsIntegral(leftType) && IsIntegral(rightType))
			{
				return NumericBinary(left, right, op, leftType);
			}
			return $"({left} {op} {right})";
		}

		private string ConvertedNumericExpression(ExpressionSyntax expression, SpecialType target)
		{
			string value = Expression(expression);
			SpecialType source = model.GetTypeInfo(expression).Type?.SpecialType ?? SpecialType.None;
			if (IsIntegral(source) && IsIntegral(target)) return ConvertInteger(value, source, target);
			// Convert before branch selection or the core's double-based floating comparison.
			if (IsFloating(target) && (IsIntegral(source) || IsFloating(source)) && source != target)
			{
				return $"cast<{(target == SpecialType.System_Single ? "float" : "double")}>({value})";
			}
			return value;
		}

		private string UnaryExpression(ExpressionSyntax syntax, ExpressionSyntax operand, string op, bool postfix)
		{
			if (model.GetTypeInfo(operand).Type?.TypeKind == TypeKind.Enum)
			{
				return FailLiteral(syntax, "enum unary operation");
			}
			SpecialType type = model.GetTypeInfo(syntax).Type?.SpecialType ?? SpecialType.None;
			string value = Expression(operand);
			if (!IsIntegral(type)) return postfix ? $"({value}{op})" : $"({op}{value})";
			if (op is "++" or "--")
			{
				(string name, string target, _) = IntegralType(type);
				string operation = (postfix ? "post_" : "pre_") + (op == "++" ? "inc_" : "dec_") + name;
				string changed = IntegerFromBits(IntegerBits("previous", type)
					+ (op == "++" ? " + 1UL" : " - 1UL"), type);
				string helper = NumericHelper(operation,
					$"{target} {operation}(ref {target} value) {{ {target} previous = value; "
					+ $"value = {changed}; return {(postfix ? "previous" : "value")}; }}");
				return helper + "(ref " + value + ")";
			}
			value = ConvertInteger(value, model.GetTypeInfo(operand).Type!.SpecialType, type);
			if (op == "-") return NumericBinary("cast<" + IntegralType(type).Type + ">(0)", value, "-", type);
			return "(" + op + value + ")";
		}

		private string AssignmentExpression(AssignmentExpressionSyntax assignment)
		{
			string left = Expression(assignment.Left);
			string right = Expression(assignment.Right);
			string op = assignment.OperatorToken.Text;
			if (op == "=") return left + " = " + right;
			SpecialType target = model.GetTypeInfo(assignment.Left).Type?.SpecialType ?? SpecialType.None;
			if (target == SpecialType.System_String && op == "+=")
			{
				return StringCompoundAssignment(assignment, left);
			}
			if (model.GetTypeInfo(assignment.Left).Type?.TypeKind == TypeKind.Enum)
			{
				return FailLiteral(assignment, "enum compound assignment");
			}
			if (!IsIntegral(target))
			{
				if (op is "/=" or "%=" && IsFloating(target))
				{
					return FailLiteral(assignment, "floating-point compound division or remainder");
				}
				return left + " " + op + " " + right;
			}
			SpecialType source = (model.GetOperation(assignment) as ICompoundAssignmentOperation)
				?.Value.Type?.SpecialType ?? SpecialType.None;
			if (!IsIntegral(source)) return FailLiteral(assignment, "integer compound assignment with a non-integer operand");
			SpecialType promoted = CompoundPromotedType(target, source, op);
			(string name, string type, _) = IntegralType(target);
			(string rightName, string rightType, _) = IntegralType(source);
			string binaryOp = op[..^1];
			string operation = binaryOp switch
			{
				"+" => "add", "-" => "sub", "*" => "mul", "/" => "div", "%" => "mod",
				"&" => "and", "|" => "or", "^" => "xor", "<<" => "shl", ">>" => "shr",
				_ => throw new InvalidOperationException("Unexpected compound integer operator.")
			};
			string helperName = "assign_" + operation + "_" + name + "_" + rightName;
			string old = ConvertInteger("previous", target, promoted);
			string argument = binaryOp is "<<" or ">>" ? "right" : ConvertInteger("right", source, promoted);
			string result = ConvertInteger(NumericBinary(old, argument, binaryOp, promoted), promoted, target);
			string helper = NumericHelper(helperName,
				$"{type} {helperName}(ref {type} value, fn()->{rightType} rhs) {{ {type} previous = value; "
				+ $"{rightType} right = rhs(); value = {result}; return value; }}");
			return helper + $"(ref {left}, [&]() -> {rightType} => {right})";
		}

		private static SpecialType CompoundPromotedType(SpecialType target, SpecialType source, string op)
		{
			if (target is SpecialType.System_SByte or SpecialType.System_Byte
				or SpecialType.System_Int16 or SpecialType.System_UInt16)
			{
				target = SpecialType.System_Int32;
			}
			if (op is "<<=" or ">>=") return target;
			if (source == SpecialType.System_UInt64 || target == SpecialType.System_UInt64)
			{
				return SpecialType.System_UInt64;
			}
			if (source == SpecialType.System_Int64 || target == SpecialType.System_Int64)
			{
				return SpecialType.System_Int64;
			}
			if (source == SpecialType.System_UInt32 || target == SpecialType.System_UInt32)
			{
				return source == SpecialType.System_UInt32 && target == SpecialType.System_UInt32
					? SpecialType.System_UInt32 : SpecialType.System_Int64;
			}
			return SpecialType.System_Int32;
		}

		private string CastExpression(CastExpressionSyntax cast)
		{
			ITypeSymbol? targetSymbol = model.GetTypeInfo(cast.Type).Type;
			ITypeSymbol? sourceSymbol = model.GetTypeInfo(cast.Expression).Type;
			SpecialType target = targetSymbol?.SpecialType ?? SpecialType.None;
			SpecialType source = sourceSymbol?.SpecialType ?? SpecialType.None;
			bool targetEnum = targetSymbol is INamedTypeSymbol { TypeKind: TypeKind.Enum };
			bool sourceEnum = sourceSymbol is INamedTypeSymbol { TypeKind: TypeKind.Enum };
			if (targetEnum)
			{
				target = ((INamedTypeSymbol)targetSymbol!).EnumUnderlyingType!.SpecialType;
			}
			if (sourceEnum)
			{
				source = ((INamedTypeSymbol)sourceSymbol!).EnumUnderlyingType!.SpecialType;
			}
			if (IsIntegral(source) && IsIntegral(target))
			{
				string value = Expression(cast.Expression);
				if (sourceEnum)
				{
					value = "cast<" + IntegralType(source).Type + ">(" + value + ")";
				}
				value = ConvertInteger(value, source, target);
				return targetEnum ? "cast<" + TypeName(targetSymbol, cast.Type) + ">(" + value + ")" : value;
			}
			if (IsFloating(source) && IsIntegral(target))
			{
				return FailLiteral(cast, "floating-point to integer conversion (different overflow semantics)");
			}
			if (target == SpecialType.System_Single
				&& source is SpecialType.System_Double or SpecialType.System_Int64 or SpecialType.System_UInt64)
			{
				return FailLiteral(cast, "floating-point narrowing or 64-bit integer to float conversion");
			}
			return "cast<" + TypeName(model.GetTypeInfo(cast.Type).Type, cast.Type) + ">(" +
				Expression(cast.Expression) + ")";
		}
	}
}
